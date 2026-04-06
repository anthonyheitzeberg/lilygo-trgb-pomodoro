/**
 * @file lcd_panel.c
 * @brief RGB LCD panel init using ESP-IDF's esp_lcd_panel_rgb driver.
 *
 * Learning notes are inline — this file is a tour of the esp_lcd subsystem.
 */

#include "lcd_panel.h"

#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

static const char *TAG = "lcd_panel";

/* Module-private state */
static esp_lcd_panel_handle_t s_panel = NULL;
static uint16_t               *s_fb   = NULL;

esp_err_t lcd_panel_init(const lcd_panel_io_config_t *io)
{
    ESP_LOGI(TAG, "Configuring RGB LCD peripheral...");

    /*
     * Learning note — esp_lcd_rgb_panel_config_t ───────────────────────────
     *
     * This struct describes the electrical timing of the RGB interface.
     * The numbers come from the display datasheet's "RGB timing" section.
     *
     * Key fields:
     *   clk_src          — clock source. LCD_CLK_SRC_PLL240M gives us the
     *                       best phase accuracy at high pixel clocks.
     *   timings.pclk_hz  — pixel clock in Hz. 480×480 @ 60 fps with
     *                       typical blanking ≈ 16 MHz.
     *   timings.h/v_res  — active area in pixels.
     *   timings.hsync_*  — horizontal blanking intervals (front porch,
     *                       back porch, pulse width). From datasheet.
     *   timings.vsync_*  — same, vertical direction.
     *   data_width        — 16 bits (RGB565).
     *   num_fbs           — number of framebuffers. 1 is fine for our use
     *                       case; 2 enables double-buffering (tear-free).
     *   bounce_buffer_size_px — an optional SRAM "bounce buffer" the DMA
     *                       engine uses to copy from PSRAM in chunks.
     *                       Needed because the RGB peripheral can't DMA
     *                       directly from PSRAM at full speed on some
     *                       revisions. Sizing it to 10 * LCD_H_RES gives
     *                       a good balance of speed vs SRAM usage.
     * ────────────────────────────────────────────────────────────────────────
     */
    esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src = LCD_CLK_SRC_PLL240M,

        .timings = {
            .pclk_hz            = 16 * 1000 * 1000,  /* 16 MHz pixel clock  */
            .h_res              = LCD_H_RES,
            .v_res              = LCD_V_RES,
            /* Horizontal blanking (values tuned for ST7701S @ 480×480) */
            .hsync_pulse_width  = 10,
            .hsync_back_porch   = 10,
            .hsync_front_porch  = 20,
            /* Vertical blanking */
            .vsync_pulse_width  = 10,
            .vsync_back_porch   = 10,
            .vsync_front_porch  = 10,
            /* Polarity — ST7701S uses active-low syncs, active-high DE */
            .flags.pclk_active_neg = 1,
        },

        .data_width = 16,

        /* Pin assignments — control signals */
        .pclk_gpio_num  = io->pclk,
        .vsync_gpio_num = io->vsync,
        .hsync_gpio_num = io->hsync,
        .de_gpio_num    = io->de,

        /* Pin assignments — 16-bit data bus */
        .data_gpio_nums = {
            io->data[0],  io->data[1],  io->data[2],  io->data[3],
            io->data[4],  io->data[5],  io->data[6],  io->data[7],
            io->data[8],  io->data[9],  io->data[10], io->data[11],
            io->data[12], io->data[13], io->data[14], io->data[15],
        },

        .num_fbs = 1,

        /*
         * Bounce buffer: 10 lines of SRAM. The RGB peripheral DMA engine
         * reads pixel data from PSRAM through this buffer.
         * Without it, cache misses can cause visible display glitches.
         */
        .bounce_buffer_size_px = 10 * LCD_H_RES,

        /* Allocate the framebuffer in PSRAM (flag = MALLOC_CAP_SPIRAM) */
        .flags.fb_in_psram = 1,
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_cfg, &s_panel));

    /* Reset and initialise the panel handle (does NOT send SPI commands —
     * that was already done by st7701s_init()). */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    /* Retrieve the framebuffer pointer.
     * esp_lcd_rgb_panel_get_frame_buffer() populates up to 3 fb pointers
     * matching the num_fbs we requested. */
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