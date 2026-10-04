// Bare-metal STM32 implementation of platform.hpp, plus main() and the newlib stubs printf needs.
//
// Only the registers that every modern STM32 shares are used (GPIO, the SPI found on C5/H5/H7/U5,
// USART, a general-purpose timer, flash), so porting to another STM32 is mostly a new board header.

#include "platform.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include BANNERS_BOARD_HEADER
#include BOARD_DEVICE_HEADER

#include "app.hpp"

namespace {

// Comfortably within the 74HC595's limits, and updates a 7-chip chain in well under a step interval.
constexpr uint32_t SPI_CLOCK_HZ = 1'000'000;
// Peripherals that stop responding are given up on rather than hanging the installation.
constexpr int64_t PERIPHERAL_TIMEOUT_US = 50'000;

// ---------------------------------------------------------------------------
// GPIO
// ---------------------------------------------------------------------------

GPIO_TypeDef* const GPIO_PORTS[] = {GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, nullptr, nullptr, GPIOH};
constexpr uint32_t GPIO_CLOCKS[] = {RCC_AHB2ENR_GPIOAEN, RCC_AHB2ENR_GPIOBEN, RCC_AHB2ENR_GPIOCEN,
                                    RCC_AHB2ENR_GPIODEN, RCC_AHB2ENR_GPIOEEN, 0,
                                    0,                   RCC_AHB2ENR_GPIOHEN};

GPIO_TypeDef* port_of(int pin) {
    return GPIO_PORTS[(pin >> 4) & 7];
}

int index_of(int pin) {
    return pin & 15;
}

void enable_port_clock(int pin) {
    RCC->AHB2ENR |= GPIO_CLOCKS[(pin >> 4) & 7];
    (void)RCC->AHB2ENR;  // let the clock settle before the port is touched
}

void set_field(volatile uint32_t& reg, int index, int bits_per_field, uint32_t value) {
    const int shift = index * bits_per_field;
    const uint32_t mask = ((1u << bits_per_field) - 1) << shift;
    reg = (reg & ~mask) | (value << shift);
}

enum Mode : uint32_t { MODE_INPUT = 0, MODE_OUTPUT = 1, MODE_ALTERNATE = 2 };

void configure_alternate(int pin, int function) {
    enable_port_clock(pin);
    GPIO_TypeDef* port = port_of(pin);
    const int index = index_of(pin);
    set_field(port->AFR[index >> 3], index & 7, 4, static_cast<uint32_t>(function));
    set_field(port->OSPEEDR, index, 2, 2);  // high speed, for the SPI pins
    set_field(port->MODER, index, 2, MODE_ALTERNATE);
}

// ---------------------------------------------------------------------------
// Microsecond clock
// ---------------------------------------------------------------------------

uint32_t s_timer_mask = 0xFFFFFFFF;

void timer_begin() {
    BOARD_TIMER_CLOCK_ON();
    BOARD_TIMER->CR1 = 0;
    BOARD_TIMER->PSC = (SystemCoreClock / 1'000'000) - 1;
    BOARD_TIMER->ARR = 0xFFFFFFFF;
    s_timer_mask = BOARD_TIMER->ARR;  // 16-bit timers keep only the low half
    BOARD_TIMER->EGR = TIM_EGR_UG;
    BOARD_TIMER->CR1 = TIM_CR1_CEN;
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

void console_put(char c) {
    const int64_t deadline = platform::now_us() + PERIPHERAL_TIMEOUT_US;
    while (!(BOARD_CONSOLE_USART->ISR & USART_ISR_TXE_TXFNF)) {
        if (platform::now_us() > deadline) {
            return;  // nothing is draining the port; drop the character rather than stall
        }
    }
    BOARD_CONSOLE_USART->TDR = static_cast<uint8_t>(c);
}

// ---------------------------------------------------------------------------
// Random numbers
// ---------------------------------------------------------------------------

bool s_rng_ready = false;
uint32_t s_random_state = 0;

void random_begin() {
    RCC->AHB2ENR |= RCC_AHB2ENR_RNGEN;
    (void)RCC->AHB2ENR;
    RNG->CR = RNG_CR_RNGEN;
    const int64_t deadline = platform::now_us() + PERIPHERAL_TIMEOUT_US;
    while (!(RNG->SR & RNG_SR_DRDY)) {
        if (platform::now_us() > deadline) {
            return;  // no RNG clock; random_u32() falls back to a seeded generator
        }
    }
    s_rng_ready = true;
}

}  // namespace

namespace platform {

int64_t now_us() {
    // The hardware counter is only 32 bits (16 on some timers), so the wraps are counted here.
    // Every wait in this firmware polls, which keeps this called far more often than it wraps.
    static uint32_t previous = 0;
    static int64_t wraps = 0;
    const uint32_t ticks = BOARD_TIMER->CNT & s_timer_mask;
    if (ticks < previous) {
        wraps += static_cast<int64_t>(s_timer_mask) + 1;
    }
    previous = ticks;
    return wraps + ticks;
}

void sleep_until_us(int64_t time_us) {
    while (now_us() < time_us) {
        __NOP();
    }
}

void busy_wait_us(uint32_t us) {
    sleep_until_us(now_us() + us);
}

void configure_output(int pin, bool level) {
    enable_port_clock(pin);
    GPIO_TypeDef* port = port_of(pin);
    const int index = index_of(pin);
    port->BSRR = 1u << (level ? index : index + 16);  // preset, so enabling the output doesn't glitch
    set_field(port->OTYPER, index, 1, 0);             // push-pull
    set_field(port->PUPDR, index, 2, 0);
    set_field(port->MODER, index, 2, MODE_OUTPUT);
}

void write_output(int pin, bool level) {
    const int index = index_of(pin);
    port_of(pin)->BSRR = 1u << (level ? index : index + 16);
}

void configure_input_pullup(int pin) {
    enable_port_clock(pin);
    GPIO_TypeDef* port = port_of(pin);
    const int index = index_of(pin);
    set_field(port->PUPDR, index, 2, 1);  // pull-up
    set_field(port->MODER, index, 2, MODE_INPUT);
}

int read_input(int pin) {
    return (port_of(pin)->IDR >> index_of(pin)) & 1;
}

void spi_begin(int data_pin, int clock_pin, size_t) {
    configure_alternate(data_pin, board::SPI_AF);
    configure_alternate(clock_pin, board::SPI_AF);
    BOARD_SPI_CLOCK_ON();
    (void)RCC->APB2ENR;

    // The baud rate divider is 2^(MBR + 1); take the first one at or below SPI_CLOCK_HZ.
    uint32_t divider = 0;
    while (divider < 7 && (SystemCoreClock >> (divider + 1)) > SPI_CLOCK_HZ) {
        ++divider;
    }
    BOARD_SPI->CR1 = 0;
    BOARD_SPI->CFG1 = (7u << SPI_CFG1_DSIZE_Pos) | (divider << SPI_CFG1_MBR_Pos);  // 8-bit words
    // Master, transmit only, software slave select, and mode 0 (idle low, sample on the rising edge).
    BOARD_SPI->CFG2 = SPI_CFG2_MASTER | SPI_CFG2_SSM | (1u << SPI_CFG2_COMM_Pos);
    BOARD_SPI->CR1 = SPI_CR1_SSI;
}

void spi_write(const uint8_t* data, size_t size) {
    const int64_t deadline = now_us() + PERIPHERAL_TIMEOUT_US;
    BOARD_SPI->CR1 = SPI_CR1_SSI;  // SPE off while the transfer size is set
    BOARD_SPI->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;
    BOARD_SPI->CR2 = size;
    BOARD_SPI->CR1 = SPI_CR1_SSI | SPI_CR1_SPE;
    BOARD_SPI->CR1 = SPI_CR1_SSI | SPI_CR1_SPE | SPI_CR1_CSTART;

    for (size_t i = 0; i < size; ++i) {
        while (!(BOARD_SPI->SR & SPI_SR_TXP)) {
            if (now_us() > deadline) {
                BOARD_SPI->CR1 = SPI_CR1_SSI;
                return;
            }
        }
        *reinterpret_cast<volatile uint8_t*>(&BOARD_SPI->TXDR) = data[i];
    }
    while (!(BOARD_SPI->SR & SPI_SR_EOT)) {
        if (now_us() > deadline) {
            break;
        }
    }
    BOARD_SPI->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;
    BOARD_SPI->CR1 = SPI_CR1_SSI;
}

uint32_t random_u32() {
    if (s_rng_ready) {
        const int64_t deadline = now_us() + PERIPHERAL_TIMEOUT_US;
        while (!(RNG->SR & RNG_SR_DRDY)) {
            if (now_us() > deadline) {
                s_rng_ready = false;  // stop waiting on it from here on
                break;
            }
        }
        if (s_rng_ready) {
            return RNG->DR;
        }
    }
    // Fallback: xorshift32, seeded from the timer the first time it is needed.
    if (s_random_state == 0) {
        s_random_state = static_cast<uint32_t>(now_us()) | 1u;
    }
    s_random_state ^= s_random_state << 13;
    s_random_state ^= s_random_state >> 17;
    s_random_state ^= s_random_state << 5;
    return s_random_state;
}

void log(LogLevel level, const char* tag, const char* format, ...) {
    constexpr char LETTERS[] = {'E', 'W', 'I'};
    printf("%c (%lu) %s: ", LETTERS[static_cast<int>(level)], static_cast<unsigned long>(now_us() / 1000), tag);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

void console_begin() {
    configure_alternate(board::CONSOLE_TX_PIN, board::CONSOLE_AF);
    configure_alternate(board::CONSOLE_RX_PIN, board::CONSOLE_AF);
    set_field(port_of(board::CONSOLE_RX_PIN)->PUPDR, index_of(board::CONSOLE_RX_PIN), 2, 1);
    BOARD_CONSOLE_CLOCK_ON();
    (void)RCC->APB2ENR;

    BOARD_CONSOLE_USART->CR1 = 0;
    BOARD_CONSOLE_USART->BRR = (SystemCoreClock + board::CONSOLE_BAUD / 2) / board::CONSOLE_BAUD;
    BOARD_CONSOLE_USART->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    setvbuf(stdout, nullptr, _IONBF, 0);
}

bool console_host_attached() {
    // A UART can't tell, and there is no low-power sleep on this target yet, so the answer that keeps
    // the console usable between shuffles is "yes".
    return true;
}

size_t console_read(char* buffer, size_t size, uint32_t timeout_ms) {
    const int64_t deadline = now_us() + int64_t{timeout_ms} * 1000;
    size_t count = 0;
    while (count < size) {
        if (BOARD_CONSOLE_USART->ISR & USART_ISR_ORE) {
            BOARD_CONSOLE_USART->ICR = USART_ICR_ORECF;  // otherwise reception stalls
        }
        if (BOARD_CONSOLE_USART->ISR & USART_ISR_RXNE_RXFNE) {
            buffer[count++] = static_cast<char>(BOARD_CONSOLE_USART->RDR);
            continue;
        }
        if (count > 0 || now_us() >= deadline) {
            break;  // take what arrived; only wait when nothing has
        }
    }
    return count;
}

bool storage_load(uint8_t* data, size_t size) {
    if (size > FLASH_PAGE_SIZE) {
        return false;
    }
    std::memcpy(data, reinterpret_cast<const void*>(board::STORAGE_ADDRESS), size);
    return true;  // Settings::load() validates the contents
}

bool storage_save(const uint8_t* data, size_t size) {
    // Flash is programmed 128 bits at a time, so the blob is padded out to a multiple of 16 bytes.
    alignas(4) static uint8_t page[1024];
    if (size > sizeof page) {
        return false;
    }
    std::memset(page, 0xFF, sizeof page);
    std::memcpy(page, data, size);
    const size_t words = ((size + 15) / 16) * 4;

    const auto wait_while_busy = [] {
        const int64_t deadline = now_us() + PERIPHERAL_TIMEOUT_US * 20;
        while (FLASH->SR & FLASH_SR_BSY) {
            if (now_us() > deadline) {
                return false;
            }
        }
        return true;
    };

    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123;
        FLASH->KEYR = 0xCDEF89AB;
    }
    if (FLASH->CR & FLASH_CR_LOCK || !wait_while_busy()) {
        return false;
    }
    FLASH->CCR = FLASH_CCR_CLR_EOP | FLASH_CCR_CLR_WRPERR | FLASH_CCR_CLR_PGSERR | FLASH_CCR_CLR_STRBERR |
                 FLASH_CCR_CLR_INCERR;

    FLASH->CR = FLASH_CR_PER | (board::STORAGE_PAGE << FLASH_CR_PNB_Pos) |
                (board::STORAGE_IN_SECOND_BANK ? FLASH_CR_BKSEL : 0) | FLASH_CR_STRT;
    const bool erased = wait_while_busy();
    FLASH->CCR = FLASH_CCR_CLR_EOP;

    bool written = erased;
    if (written) {
        auto* target = reinterpret_cast<volatile uint32_t*>(board::STORAGE_ADDRESS);
        const auto* source = reinterpret_cast<const uint32_t*>(page);
        FLASH->CR = FLASH_CR_PG;
        for (size_t i = 0; i < words && written; i += 4) {
            for (size_t word = 0; word < 4; ++word) {
                target[i + word] = source[i + word];
                __DSB();
            }
            written = wait_while_busy();
            FLASH->CCR = FLASH_CCR_CLR_EOP;
        }
    }

    FLASH->CR = FLASH_CR_LOCK;
    return written && !(FLASH->SR & (FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_INCERR));
}

void low_power_sleep(uint32_t seconds) {
    // Plain wait for now: the chip keeps running at full speed. Next to fourteen steppers it hardly
    // registers, but STOP mode with an RTC wake-up would be the thing to add here.
    sleep_until_us(now_us() + int64_t{seconds} * 1'000'000);
}

}  // namespace platform

int main() {
    timer_begin();
    random_begin();
    run_banners();
}

// ---------------------------------------------------------------------------
// Newlib stubs: printf writes to the console, malloc gets the heap from the linker script
// ---------------------------------------------------------------------------

extern "C" {

int _write(int, const char* data, int length) {
    for (int i = 0; i < length; ++i) {
        if (data[i] == '\n') {
            console_put('\r');
        }
        console_put(data[i]);
    }
    return length;
}

void* _sbrk(ptrdiff_t increment) {
    extern char _heap_start;
    extern char _heap_end;
    static char* current = &_heap_start;
    if (current + increment > &_heap_end) {
        return reinterpret_cast<void*>(-1);
    }
    char* const previous = current;
    current += increment;
    return previous;
}

int _read(int, char*, int) { return -1; }
int _close(int) { return -1; }
int _lseek(int, int, int) { return 0; }
int _fstat(int, struct stat*) { return -1; }
int _isatty(int) { return 1; }
int _kill(int, int) { return -1; }
int _getpid() { return 1; }
void _exit(int) {
    while (true) {
    }
}

}  // extern "C"
