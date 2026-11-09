idf_component_register(SRCS "lcd_bringup.c" "touch_input.c" "ui_canvas.c"
                            "ui_font_data.c" "ui_pages.c" "ui_app.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_hw_support
                                     esp_driver_usb_serial_jtag esp_timer nvs_flash vfs)
