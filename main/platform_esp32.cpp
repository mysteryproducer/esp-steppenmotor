// ESP-IDF implementation of platform.hpp, plus the app_main entry point.

#include "platform.hpp"
#include "banners.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

#include "app.hpp"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#elif CONFIG_ESP_CONSOLE_UART
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#endif

namespace {

// Comfortably within the 74HC595's limits, and updates a 7-chip chain in well under a step interval.
constexpr int SPI_CLOCK_HZ = 1'000'000;
// Waits shorter than this are busy-waited rather than handed to a timer.
constexpr int64_t MIN_BLOCKING_WAIT_US = 200;

constexpr const char* NVS_NAMESPACE = "banners";
constexpr const char* NVS_KEY = "settings";

spi_device_handle_t s_spi = nullptr;
esp_timer_handle_t s_wake_timer = nullptr;
TaskHandle_t s_waiting_task = nullptr;

#if CONFIG_ESP_CONSOLE_UART
constexpr uart_port_t CONSOLE_UART = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);
#endif

gpio_num_t to_gpio(int pin) {
    return static_cast<gpio_num_t>(pin);
}

bool nvs_ready() {
    static bool ready = [] {
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            err = nvs_flash_init();
        }
        return err == ESP_OK;
    }();
    return ready;
}

}  // namespace

namespace platform {

int64_t now_us() {
    return esp_timer_get_time();
}

void sleep_until_us(int64_t time_us) {
    for (int64_t delay_us; (delay_us = time_us - esp_timer_get_time()) > 0;) {
        if (delay_us < MIN_BLOCKING_WAIT_US) {
            esp_rom_delay_us(static_cast<uint32_t>(delay_us));
            return;
        }
        if (!s_wake_timer) {
            const esp_timer_create_args_t timer_args = {
                .callback = [](void*) { xTaskNotifyGive(s_waiting_task); },
                .arg = nullptr,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "wake",
                .skip_unhandled_events = false,
            };
            ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_wake_timer));
        }
        s_waiting_task = xTaskGetCurrentTaskHandle();
        esp_timer_start_once(s_wake_timer, delay_us);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_us / 1000) + 2);
        esp_timer_stop(s_wake_timer);  // in case the wait timed out instead
    }
}

void busy_wait_us(uint32_t us) {
    esp_rom_delay_us(us);
}

void configurePin(const GpioPinConfig& config) {
    gpio_set_level(to_gpio(config.pin), config.level?1:0);  // preset, so enabling the output doesn't glitch
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << config.pin;
    switch (config.mode) {
        case GpioPinMode::Input:
            io.mode = GPIO_MODE_INPUT;
            break;
        case GpioPinMode::Output:
            io.mode = GPIO_MODE_OUTPUT;
            ESP_ERROR_CHECK(gpio_sleep_sel_dis(to_gpio(config.pin)));
            break;
        case GpioPinMode::InOut:
            io.mode = GPIO_MODE_INPUT_OUTPUT;
            ESP_ERROR_CHECK(gpio_sleep_sel_dis(to_gpio(config.pin)));
            break;
    }
    io.pull_up_en = config.pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    
    ESP_ERROR_CHECK(gpio_config(&io));
}

void write_output(int pin, bool level) {
    gpio_set_level(to_gpio(pin), level);
}

int read_input(int pin) {
    return gpio_get_level(to_gpio(pin));
}

void spi_begin(int data_pin, int clock_pin, size_t max_bytes) {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = data_pin;
    bus.miso_io_num = -1;
    bus.sclk_io_num = clock_pin;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.data4_io_num = -1;
    bus.data5_io_num = -1;
    bus.data6_io_num = -1;
    bus.data7_io_num = -1;
    bus.max_transfer_sz = static_cast<int>(max_bytes);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED));

    spi_device_interface_config_t device = {};
    device.mode = 0;  // 74HC595 shifts on the rising clock edge
    device.clock_speed_hz = SPI_CLOCK_HZ;
    device.spics_io_num = -1;
    device.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &device, &s_spi));
}

void spi_write(const uint8_t* data, size_t size) {
    spi_transaction_t transaction = {};
    transaction.length = size * 8;
    transaction.tx_buffer = data;
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &transaction));
}

uint32_t random_u32() {
    return esp_random();
}

void log(LogLevel level, const char* tag, const char* format, ...) {
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);
    switch (level) {
        case LogLevel::Error: ESP_LOGE(tag, "%s", message); break;
        case LogLevel::Warning: ESP_LOGW(tag, "%s", message); break;
        case LogLevel::Info: ESP_LOGI(tag, "%s", message); break;
    }
}

void console_begin() {
    // Console input needs the driver for whichever port is the primary console.
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t driver = {};
    driver.tx_buffer_size = 256;
    driver.rx_buffer_size = 256;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&driver));
    usb_serial_jtag_vfs_use_driver();
#elif CONFIG_ESP_CONSOLE_UART
    ESP_ERROR_CHECK(uart_driver_install(CONSOLE_UART, 256, 0, 0, nullptr, 0));
    uart_vfs_dev_use_driver(CONSOLE_UART);
#endif
}

bool console_host_attached() {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    return usb_serial_jtag_is_connected();
#else
    return false;  // a UART can't tell
#endif
}

size_t console_read(char* buffer, size_t size, uint32_t timeout_ms) {
    if (size == 0) {
        return 0;
    }
    int count = 0;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    count = usb_serial_jtag_read_bytes(buffer, 1, pdMS_TO_TICKS(timeout_ms));
    if (count > 0 && size > 1) {
        count += std::max(0, usb_serial_jtag_read_bytes(buffer + 1, size - 1, 0));
    }
#elif CONFIG_ESP_CONSOLE_UART
    count = uart_read_bytes(CONSOLE_UART, buffer, 1, pdMS_TO_TICKS(timeout_ms));
    size_t buffered = 0;
    if (count > 0 && size > 1 && uart_get_buffered_data_len(CONSOLE_UART, &buffered) == ESP_OK &&
        buffered > 0) {
        count += std::max(0, uart_read_bytes(CONSOLE_UART, buffer + 1,
                                             std::min(buffered, size - 1), 0));
    }
#else
    vTaskDelay(pdMS_TO_TICKS(timeout_ms));
#endif
    return count > 0 ? static_cast<size_t>(count) : 0;
}

bool storage_load(uint8_t* data, size_t size) {
    nvs_handle_t handle;
    if (!nvs_ready() || nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t length = size;
    const esp_err_t err = nvs_get_blob(handle, NVS_KEY, data, &length);
    nvs_close(handle);
    return err == ESP_OK && length == size;
}

bool storage_save(const uint8_t* data, size_t size) {
    nvs_handle_t handle;
    if (!nvs_ready() || nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(handle, NVS_KEY, data, size);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

void low_power_sleep(uint32_t seconds) {
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(uint64_t{seconds} * 1'000'000));
    esp_light_sleep_start();
}

}  // namespace platform

extern "C" void app_main(void) {
    // A task of our own, so its stack size is set here rather than in sdkconfig.
//    xTaskCreate([](void*) { run_banners(); }, "banners", 8192, nullptr, 5, nullptr);
    platform::GpioPinConfig config = { .pin = 8, .mode = platform::GpioPinMode::Output, .pullup = false, .level = false };
    configurePin(config);
    config.pin = 9;
    configurePin(config);
    config.pin = 10;
    configurePin(config);
    config.pin = 11;
    configurePin(config);

    uint8_t index=0;
    while(true) {
        ++index;
        uint8_t pattern = GpioPulseDriven::HALF_STEP[7-(index % 8)];
        for (int i = 0; i < 4; ++i) {
            platform::write_output(8 + i, (pattern >> i) & 1);
        }
        platform::busy_wait_us(1000);
    }

}
