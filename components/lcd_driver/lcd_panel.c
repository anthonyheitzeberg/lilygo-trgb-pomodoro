/**
 * @file lcd_panel.c
 * @brief RGB panel init for LILYGO T-RGB 2.1" ST7701S using ESP-IDF esp_lcd.
 */

#include "lcd_panel.h"

#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

static const char *TAG = "lcd_panel";

/* Module-private state */
static esp_lcd_panel_handle_t s_panel = NULL;
static uint16_t               *s_fb   = NULL;

esp_err_t lcd_panel_init(void)
{
    ESP_LOGI(TAG, "Configuring RGB LCD peripheral...");

    esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src = LCD_CLK_SRC_PLL160M,

        .timings = {
            .pclk_hz            = 8 * 1000 * 1000,
            .h_res              = LCD_H_RES,
            .v_res              = LCD_V_RES,
            .hsync_pulse_width  = 1,
            .hsync_back_porch   = 30,
            .hsync_front_porch  = 50,
            .vsync_pulse_width  = 1,
            .vsync_back_porch   = 30,
            .vsync_front_porch  = 20,
            .flags.pclk_active_neg = 1,
        },

        .data_width = 16,

        .pclk_gpio_num  = 42,
        .vsync_gpio_num = 41,
        .hsync_gpio_num = 47,
        .de_gpio_num    = 45,
        .disp_gpio_num  = -1,

        .data_gpio_nums = {
             7,  6,  5,  3,  2,
            14, 13, 12, 11, 10, 9,
            21, 18, 17, 16, 15,
        },

        .num_fbs = 1,
        .bounce_buffer_size_px = 10 * LCD_H_RES,
        .flags.fb_in_psram = 1,
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    void *fb = NULL;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &fb));
    s_fb = (uint16_t *)fb;

    ESP_LOGI(TAG, "RGB panel up. Framebuffer @ %p (%u bytes)",
             s_fb, LCD_H_RES * LCD_V_RES * sizeof(uint16_t));

    return ESP_OK;
}

uint16_t *lcd_panel_get_fb(void)
{
    return s_fb;
}
