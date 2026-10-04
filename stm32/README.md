# STM32 build

Bare-metal build of the banner firmware, sharing all the code in `../src` with the ESP-IDF build in
`../main` and the Pico SDK build in `../rp2350`. The default board is the WeAct STM32C5 100-pin Core
Board (STM32C552VET6).

No HAL is used: only registers that every modern STM32 shares (GPIO, the SPI found on C5/H5/H7/U5,
USART, a general-purpose timer and flash). ST's device pack supplies the CMSIS headers, startup code
and `SystemInit`; CMake fetches it on the first configure.

## Building

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-arm-none-eabi.cmake
cmake --build build
```

This produces `build/banners.elf`, `banners.bin` and `banners.hex`. Any `arm-none-eabi` GCC works; the
toolchain file falls back to the one installed with the Pico SDK.

| Option | Default | Purpose |
| --- | --- | --- |
| `STM32_DIRECT_GPIO` | `OFF` | Drive coils straight from pins instead of the 74HC595 chain |
| `STM32_BOARD` | `weact_c552ve` | Board header in `boards/`, without `.hpp` |
| `STM32_DEVICE` | `STM32C552xx` | CMSIS device define; picks the matching startup file |
| `STM32_LINKER_SCRIPT` | `linker/stm32c552xe_banners.ld` | Memory layout |
| `STM32_CPU_FLAGS` | Cortex-M33 with FPU | Architecture flags |
| `STM32_DFP_DIR`, `CMSIS_CORE_DIR` | fetched | Local checkouts, for building offline |
| `STM32_TOOLCHAIN_BIN` | auto | Directory holding `arm-none-eabi-gcc` |

## Another STM32

Copy `boards/weact_c552ve.hpp`, change the pins and peripherals, and build with `-DSTM32_BOARD=<name>`
`-DSTM32_DEVICE=<device>` and that family's linker script and CPU flags. The H7 keeps the same SPI,
GPIO and USART registers, so `platform_stm32.cpp` should need little or no change; its device pack is a
different repository, so the `FetchContent` block in `CMakeLists.txt` needs repointing.

## Wiring (default board header)

| Signal | Pin |
| --- | --- |
| 74HC595 SER / SRCLK / RCLK / `/OE` | PA7 / PA5 / PA4 / PA6 |
| Home sensors, set A / set B | PA2 / PA3 |
| Console TX / RX | PA9 / PA10 (header P5) |

With `STM32_DIRECT_GPIO=ON`, the 56 coil lines come from ports D, E, C, B and PA0/PA1 — see
`DIRECT_GPIO_PINS` in the board header.

## Flashing

- **ST-LINK (V2 or V3):** flash `banners.hex` with STM32CubeProgrammer, or use the STM32 VS Code
  extension. SWDIO is PA13 and SWCLK is PA14, on header P5.
- **USB DFU:** hold BOOT, plug in, then
  `dfu-util -a 0 -s 0x08000000:leave -D build/banners.bin`. Whether the C5's ROM bootloader offers DFU
  is unconfirmed — `dfu-util -l` while holding BOOT will say.

## Console

USART1 at 115200 8N1 on PA9/PA10, so it needs a USB-serial adapter (or an ST-LINK V3's virtual COM
port). Press Enter within 5 s of reset for the console; it is also reachable between shuffles.

## Not done yet

- **USB.** The chip has USB full-speed device on PA11/PA12, but there is no USB stack here, so the
  console is UART-only.
- **Low-power sleep.** `low_power_sleep()` is a plain wait: the chip keeps running between shuffles.
  STOP mode with an RTC wake-up (the board has a 32.768 kHz crystal) would be the thing to add.
- **Saving settings** writes the last flash page, using the register definitions in ST's device header
  and the usual unlock/erase/128-bit-program sequence for this flash generation. It has not been run on
  hardware and was not checked against the reference manual; `save` reports failure if it does not take.
