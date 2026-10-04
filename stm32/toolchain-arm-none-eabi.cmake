# Bare-metal Arm toolchain. Any arm-none-eabi GCC will do; set STM32_TOOLCHAIN_BIN to choose one,
# otherwise the one installed with the Pico SDK is used, then whatever is on PATH.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT STM32_TOOLCHAIN_BIN AND DEFINED ENV{STM32_TOOLCHAIN_BIN})
    set(STM32_TOOLCHAIN_BIN $ENV{STM32_TOOLCHAIN_BIN})
endif()

if(NOT STM32_TOOLCHAIN_BIN)
    file(GLOB pico_toolchains "$ENV{HOME}/.pico-sdk/toolchain/*/bin")
    if(pico_toolchains)
        list(GET pico_toolchains -1 STM32_TOOLCHAIN_BIN)
    endif()
endif()

find_program(ARM_GCC arm-none-eabi-gcc HINTS ${STM32_TOOLCHAIN_BIN})
if(NOT ARM_GCC)
    message(FATAL_ERROR "arm-none-eabi-gcc not found. Set -DSTM32_TOOLCHAIN_BIN=<path to its bin dir>.")
endif()
get_filename_component(ARM_BIN ${ARM_GCC} DIRECTORY)

set(CMAKE_C_COMPILER ${ARM_BIN}/arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER ${ARM_BIN}/arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER ${ARM_BIN}/arm-none-eabi-gcc)
set(CMAKE_OBJCOPY ${ARM_BIN}/arm-none-eabi-objcopy CACHE FILEPATH "")
set(CMAKE_SIZE ${ARM_BIN}/arm-none-eabi-size CACHE FILEPATH "")

# The compiler can't link a hosted test program without our linker script.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
