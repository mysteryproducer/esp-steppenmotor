cmake_minimum_required(VERSION 3.20)

# Bare-metal STM32 build. Configure with the toolchain file:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-arm-none-eabi.cmake && cmake --build build
#
# To target another STM32, add a header to boards/ and set these three cache variables.
set(STM32_BOARD "weact_c552ve" CACHE STRING "Board header in boards/, without .hpp")
set(STM32_DEVICE "STM32C552xx" CACHE STRING "CMSIS device define, e.g. STM32C552xx")
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/linker/stm32c552xe_banners.ld"
    CACHE FILEPATH "Linker script")
set(STM32_CPU_FLAGS "-mcpu=cortex-m33;-mthumb;-mfpu=fpv5-sp-d16;-mfloat-abi=hard"
    CACHE STRING "Architecture flags")

project(banners C CXX ASM)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ST's device pack supplies the CMSIS device header, startup code and SystemInit; CMSIS core supplies
# core_cm33.h. Point STM32_DFP_DIR / CMSIS_CORE_DIR at local copies to build without network access.
include(FetchContent)
set(STM32_DFP_DIR "" CACHE PATH "Local checkout of stm32c5xx-dfp (optional)")
set(CMSIS_CORE_DIR "" CACHE PATH "Local checkout of cmsis-core (optional)")

if(STM32_DFP_DIR)
    set(dfp_SOURCE_DIR ${STM32_DFP_DIR})
else()
    FetchContent_Declare(dfp
        GIT_REPOSITORY https://github.com/STMicroelectronics/stm32c5xx-dfp.git
        GIT_TAG 2.1.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(dfp)
endif()

if(CMSIS_CORE_DIR)
    set(cmsis_SOURCE_DIR ${CMSIS_CORE_DIR})
else()
    FetchContent_Declare(cmsis
        GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis-core.git
        GIT_TAG master
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(cmsis)
endif()

string(TOLOWER ${STM32_DEVICE} device_lower)

# The banner code in ../src is shared with the ESP-IDF build in ../main and the Pico SDK build in ../rp2350.
set(SHARED_DIR ${CMAKE_CURRENT_LIST_DIR}/../src)

add_executable(banners
    platform_stm32.cpp
    ${dfp_SOURCE_DIR}/Source/startup_${device_lower}.c
    ${dfp_SOURCE_DIR}/Source/Templates/system_stm32c5xx.c
    ${SHARED_DIR}/app.cpp
    ${SHARED_DIR}/banner_controller.cpp
    ${SHARED_DIR}/coil_outputs.cpp
    ${SHARED_DIR}/console.cpp
    ${SHARED_DIR}/motion.cpp
    ${SHARED_DIR}/settings.cpp
)

target_include_directories(banners PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/boards
    ${SHARED_DIR}
    ${dfp_SOURCE_DIR}/Include
    ${cmsis_SOURCE_DIR}/CMSIS/Core/Include
)

# The 74HC595 chain is the default; -DSTM32_DIRECT_GPIO=ON drives the coils straight from pins.
option(STM32_DIRECT_GPIO "Drive coils from GPIOs instead of shift registers" OFF)

target_compile_definitions(banners PRIVATE
    BANNERS_STM32
    $<$<BOOL:${STM32_DIRECT_GPIO}>:BANNERS_DIRECT_GPIO>
    BANNERS_BOARD_HEADER="${STM32_BOARD}.hpp"
    ${STM32_DEVICE}
)

target_compile_options(banners PRIVATE
    ${STM32_CPU_FLAGS}
    -Wall -Wextra -Wno-unused-parameter -Wno-psabi
    -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics>
)

target_link_options(banners PRIVATE
    ${STM32_CPU_FLAGS}
    -T${STM32_LINKER_SCRIPT}
    --specs=nano.specs
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/banners.map
    -Wl,--print-memory-usage
)

# .bin for dfu-util or ST-LINK, .hex for STM32CubeProgrammer.
add_custom_command(TARGET banners POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:banners> banners.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:banners> banners.hex
    COMMENT "Creating banners.bin and banners.hex")
