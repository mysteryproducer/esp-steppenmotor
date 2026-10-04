#pragma once

// WeAct Studio STM32C5 100-pin Core Board (STM32C552VET6: 512 KB flash, 128 KB RAM, 144 MHz Cortex-M33).
//
// To support another STM32, copy this file, adjust it, and build with -DSTM32_BOARD=<name>. Pins are
// plain constants; the peripherals are macros so that this header stays free of CMSIS includes.

namespace board {

// Pins are encoded as port * 16 + index, so PA0 is 0, PB0 is 16, and so on.
constexpr int pin(char port, int index) {
    return ((port - 'A') << 4) | index;
}

// ---------------------------------------------------------------------------
// 74HC595 chain (config::OutputMode::ShiftRegister)
// ---------------------------------------------------------------------------

inline constexpr int SR_DATA_PIN = pin('A', 7);    // SER   (pin 14), SPI1_MOSI
inline constexpr int SR_CLOCK_PIN = pin('A', 5);   // SRCLK (pin 11), SPI1_SCK
inline constexpr int SR_LATCH_PIN = pin('A', 4);   // RCLK  (pin 12)
inline constexpr int SR_ENABLE_PIN = pin('A', 6);  // /OE   (pin 13), 10k pull-up to 3V3

// ---------------------------------------------------------------------------
// Four pins per motor (config::OutputMode::DirectGpio)
// ---------------------------------------------------------------------------
//
// 56 coil lines, avoiding PA4-PA7 (shift register), PA9/PA10 (console), PA11/PA12 (USB),
// PA13/PA14 (SWD), PB3/PB4 (JTAG on reset), PC13 (LED), PC14/PC15 and PH0/PH1 (crystals),
// PH2 (BOOT0) and PH15 (user button).

inline constexpr int DIRECT_GPIO_PINS[][4] = {
    {pin('D', 0), pin('D', 1), pin('D', 2), pin('D', 3)},
    {pin('D', 4), pin('D', 5), pin('D', 6), pin('D', 7)},
    {pin('D', 8), pin('D', 9), pin('D', 10), pin('D', 11)},
    {pin('D', 12), pin('D', 13), pin('D', 14), pin('D', 15)},
    {pin('E', 0), pin('E', 1), pin('E', 2), pin('E', 3)},
    {pin('E', 4), pin('E', 5), pin('E', 6), pin('E', 7)},
    {pin('E', 8), pin('E', 9), pin('E', 10), pin('E', 11)},
    {pin('E', 12), pin('E', 13), pin('E', 14), pin('E', 15)},
    {pin('C', 0), pin('C', 1), pin('C', 2), pin('C', 3)},
    {pin('C', 4), pin('C', 5), pin('C', 6), pin('C', 7)},
    {pin('C', 8), pin('C', 9), pin('C', 10), pin('C', 11)},
    {pin('B', 5), pin('B', 6), pin('B', 7), pin('B', 8)},
    {pin('B', 9), pin('B', 10), pin('B', 12), pin('B', 13)},
    {pin('B', 14), pin('B', 15), pin('A', 0), pin('A', 1)},
};

// ---------------------------------------------------------------------------
// Home sensors and console
// ---------------------------------------------------------------------------

inline constexpr int HOME_PIN_A = pin('A', 2);
inline constexpr int HOME_PIN_B = pin('A', 3);

inline constexpr int CONSOLE_TX_PIN = pin('A', 9);   // USART1_TX, on header P5
inline constexpr int CONSOLE_RX_PIN = pin('A', 10);  // USART1_RX, on header P5
inline constexpr int CONSOLE_BAUD = 115200;

inline constexpr int LED_PIN = pin('C', 13);  // onboard LED; not used by the firmware

// Alternate function numbers for the pins above.
inline constexpr int SPI_AF = 5;
inline constexpr int CONSOLE_AF = 7;

// Settings are kept in the last 8 KB flash page, which the linker script leaves free.
inline constexpr unsigned STORAGE_ADDRESS = 0x0807E000u;
inline constexpr unsigned STORAGE_PAGE = 31;  // page within its bank
inline constexpr bool STORAGE_IN_SECOND_BANK = true;

}  // namespace board

// Peripherals, as macros so that this header needs no CMSIS includes.
#define BOARD_DEVICE_HEADER "stm32c552xx.h"

#define BOARD_SPI SPI1
#define BOARD_SPI_CLOCK_ON() (RCC->APB2ENR |= RCC_APB2ENR_SPI1EN)

#define BOARD_CONSOLE_USART USART1
#define BOARD_CONSOLE_CLOCK_ON() (RCC->APB2ENR |= RCC_APB2ENR_USART1EN)

// Any 32-bit timer; it free-runs at 1 MHz as the microsecond clock.
#define BOARD_TIMER TIM2
#define BOARD_TIMER_CLOCK_ON() (RCC->APB1LENR |= RCC_APB1LENR_TIM2EN)
