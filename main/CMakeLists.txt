# The banner code in ../src is shared with the RP2350 build in ../rp2350.
idf_component_register(SRCS "platform_esp32.cpp"
                            "../src/app.cpp"
                            "../src/banner_controller.cpp"
                            "../src/coil_outputs.cpp"
                            "../src/console.cpp"
                            "../src/motion.cpp"
                            "../src/settings.cpp"
                       PRIV_INCLUDE_DIRS "../src"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_driver_uart esp_driver_usb_serial_jtag
                                     esp_hw_support esp_timer nvs_flash vfs)
