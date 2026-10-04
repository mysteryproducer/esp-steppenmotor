# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD weact_studio_rp2350b_core CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(banners C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

# The banner code in ../src is shared with the ESP-IDF build in ../main.
set(SHARED_DIR ${CMAKE_CURRENT_LIST_DIR}/../src)

add_executable(banners
    platform_rp2350.cpp
    ${SHARED_DIR}/app.cpp
    ${SHARED_DIR}/banner_controller.cpp
    ${SHARED_DIR}/coil_outputs.cpp
    ${SHARED_DIR}/console.cpp
    ${SHARED_DIR}/motion.cpp
    ${SHARED_DIR}/settings.cpp
)

target_include_directories(banners PRIVATE ${SHARED_DIR})
target_compile_options(banners PRIVATE -Wall -Wextra -Wno-unused-parameter)
# The default 2 KB main stack is tight for the console's printf use; 4 KB is the most SCRATCH_Y holds.
target_compile_definitions(banners PRIVATE PICO_STACK_SIZE=0x1000)

target_link_libraries(banners PRIVATE
    hardware_flash
    hardware_spi
    pico_flash
    pico_low_power
    pico_rand
    pico_stdlib
)

pico_enable_stdio_usb(banners 1)
pico_enable_stdio_uart(banners 0)

# Produces banners.uf2 alongside banners.elf.
pico_add_extra_outputs(banners)
