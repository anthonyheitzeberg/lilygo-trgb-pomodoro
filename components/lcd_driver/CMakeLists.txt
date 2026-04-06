idf_component_register(
    SRCS
        "st7701s.c"
        "lcd_panel.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver          # esp_lcd, gpio, spi_master live here
        esp_lcd         # esp_lcd_panel_rgb and friends
        log
)