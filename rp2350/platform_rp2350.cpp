// Pico SDK implementation of platform.hpp for the RP2350, plus the main() entry point.

#include "platform.hpp"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "app.hpp"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/spi.h"
#include "pico/flash.h"
#include "pico/low_power.h"
#include "pico/rand.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "tusb.h"

namespace {

// Comfortably within the 74HC595's limits, and updates a 7-chip chain in well under a step interval.
constexpr uint SPI_CLOCK_HZ = 1'000'000;

// Settings live in the last flash sector, well clear of the firmware.
constexpr uint32_t STORAGE_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

spi_inst_t* s_spi = nullptr;

struct FlashWrite {
    const uint8_t* data;
    size_t size;
};

}  // namespace

namespace platform {

int64_t now_us() {
    return static_cast<int64_t>(time_us_64());
}

void sleep_until_us(int64_t time_us) {
    if (time_us > now_us()) {
        sleep_until(from_us_since_boot(static_cast<uint64_t>(time_us)));
    }
}

void busy_wait_us(uint32_t us) {
    busy_wait_us_32(us);
}

void configure_output(int pin, bool level) {
    gpio_init(pin);
    gpio_put(pin, level);  // preset, so enabling the output doesn't glitch
    gpio_set_dir(pin, GPIO_OUT);
}

void write_output(int pin, bool level) {
    gpio_put(pin, level);
}

void configure_input_pullup(int pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

int read_input(int pin) {
    return gpio_get(pin) ? 1 : 0;
}

void spi_begin(int data_pin, int clock_pin, size_t) {
    // GPIO n belongs to SPI((n / 8) % 2); config.hpp checks that the pins are SCK and TX of one block.
    s_spi = (clock_pin / 8) % 2 ? spi1 : spi0;
    spi_init(s_spi, SPI_CLOCK_HZ);
    spi_set_format(s_spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(clock_pin, GPIO_FUNC_SPI);
    gpio_set_function(data_pin, GPIO_FUNC_SPI);
}

void spi_write(const uint8_t* data, size_t size) {
    spi_write_blocking(s_spi, data, size);
}

uint32_t random_u32() {
    return get_rand_32();
}

void log(LogLevel level, const char* tag, const char* format, ...) {
    constexpr char LETTERS[] = {'E', 'W', 'I'};
    printf("%c (%" PRIu32 ") %s: ", LETTERS[static_cast<int>(level)], to_ms_since_boot(get_absolute_time()), tag);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

void console_begin() {
    stdio_init_all();
    // Give a terminal that's already open a moment to reconnect, so the boot prompt isn't missed.
    for (int i = 0; i < 20 && !stdio_usb_connected(); ++i) {
        sleep_ms(100);
    }
}

bool console_host_attached() {
    return tud_mounted();
}

size_t console_read(char* buffer, size_t size, uint32_t timeout_ms) {
    size_t count = 0;
    for (int c = getchar_timeout_us(timeout_ms * 1000); c != PICO_ERROR_TIMEOUT && count < size;
         c = getchar_timeout_us(0)) {
        buffer[count++] = static_cast<char>(c);
    }
    return count;
}

bool storage_load(uint8_t* data, size_t size) {
    if (size > FLASH_SECTOR_SIZE) {
        return false;
    }
    std::memcpy(data, reinterpret_cast<const uint8_t*>(XIP_BASE + STORAGE_OFFSET), size);
    return true;  // Settings::load() validates the contents
}

bool storage_save(const uint8_t* data, size_t size) {
    if (size > FLASH_SECTOR_SIZE) {
        return false;
    }
    // Flash is programmed a whole page at a time.
    std::vector<uint8_t> pages((size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE * FLASH_PAGE_SIZE, 0xFF);
    std::memcpy(pages.data(), data, size);
    FlashWrite write = {pages.data(), pages.size()};
    // Keeps interrupts (and the other core, if it is ever used) away from flash while it is rewritten.
    const int result = flash_safe_execute(
        [](void* param) {
            const auto* job = static_cast<const FlashWrite*>(param);
            flash_range_erase(STORAGE_OFFSET, FLASH_SECTOR_SIZE);
            flash_range_program(STORAGE_OFFSET, job->data, job->size);
        },
        &write, 1000);
    return result == PICO_OK;
}

void low_power_sleep(uint32_t seconds) {
    // Dormant keeps RAM and GPIO levels; the always-on timer, clocked by the low-power oscillator, wakes it.
    low_power_dormant_for_ms(seconds * 1000, DORMANT_CLOCK_SOURCE_LPOSC, nullptr);
}

}  // namespace platform

int main() {
    run_banners();
}
