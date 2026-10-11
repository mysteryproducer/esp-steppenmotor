#pragma once

// What the banner code needs from the chip. Implemented in main/platform_esp32.cpp (ESP-IDF) and
// rp2350/platform_rp2350.cpp (Pico SDK).

#include <cstddef>
#include <cstdint>

namespace platform {

    typedef enum { Input, Output, InOut } GpioPinMode;
    typedef struct {
        int pin;
        GpioPinMode mode;
        bool pullup;
        bool level;  // for outputs, the level to drive when enabled
    } GpioPinConfig;

    // Microseconds since boot.
    int64_t now_us();
    // Blocks until now_us() reaches time_us, with sub-millisecond accuracy.
    void sleep_until_us(int64_t time_us);
    void busy_wait_us(uint32_t us);

    // Outputs keep their level through low_power_sleep().
//    void configure_output(int pin, bool level);
    void configurePin(const GpioPinConfig& config);
    void write_output(int pin, bool level);
//    void configure_input_pullup(int pin);
    int read_input(int pin);

    // Transmit-only SPI (mode 0, MSB first) for the shift register chain.
    void spi_begin(int data_pin, int clock_pin, size_t max_bytes);
    void spi_write(const uint8_t* data, size_t size);

    uint32_t random_u32();

    enum class LogLevel { Error, Warning, Info };
    void log(LogLevel level, const char* tag, const char* format, ...) __attribute__((format(printf, 3, 4)));

    // Serial console input; output goes through printf.
    void console_begin();
    // True while a USB host is attached, in which case it's worth staying awake for the console.
    bool console_host_attached();
    // Reads up to `size` bytes, waiting at most timeout_ms for the first. Returns how many were read.
    size_t console_read(char* buffer, size_t size, uint32_t timeout_ms);

    // Non-volatile storage for a single settings blob.
    bool storage_load(uint8_t* data, size_t size);
    bool storage_save(const uint8_t* data, size_t size);

    // Low-power sleep that keeps RAM contents and output levels.
    void low_power_sleep(uint32_t seconds);

}  // namespace platform

#define LOG_ERROR(tag, ...) platform::log(platform::LogLevel::Error, tag, __VA_ARGS__)
#define LOG_WARN(tag, ...) platform::log(platform::LogLevel::Warning, tag, __VA_ARGS__)
#define LOG_INFO(tag, ...) platform::log(platform::LogLevel::Info, tag, __VA_ARGS__)
