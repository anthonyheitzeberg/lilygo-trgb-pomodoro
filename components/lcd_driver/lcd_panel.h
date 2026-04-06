#pragma once

/**
 * @file lcd_panel.h
 * @brief High-level panel abstraction over the ESP32-S3 RGB LCD peripheral.
 *
 * This layer owns:
 *   - Configuring the RGB timing signals (HSYNC, VSYNC, DE, PCLK, data pins)
 *   - Allocating the framebuffer in PSRAM
 *   - Exposing a simple "get framebuffer pointer" API
 *
 * The rest of the application draws into the framebuffer directly; the RGB
 * DMA engine pushes it to the display automatically every frame.
 *
 * Learning note ──────────────────────────────────────────────────────────────
 *   Pixel format: RGB565 (16 bits per pixel)
 *     - Bits [15:11] = Red   (5 bits)
 *     - Bits [10:5]  = Green (6 bits)
 *     - Bits [4:0]   = Blue  (5 bits)
 *   The display is 480×480, so the framebuffer is 480×480×2 = 460,800 bytes
 *   (~450 KB). That fits comfortably in the 8 MB PSRAM.
 * ────────────────────────────────────────────────────────────────────────────
 */

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_rgb.h"   /* esp_lcd_panel_handle_t */

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_H_RES   480   ///< Horizontal resolution in pixels
#define LCD_V_RES   480   ///< Vertical resolution in pixels

/**
 * @brief Initialise the RGB LCD panel and allocate the framebuffer.
 *
 * Pins and timing are hardcoded for the LILYGO T-RGB 2.1" board.
 * After this call the DMA engine is running and refreshing the display.
 * Draw into the framebuffer returned by lcd_panel_get_fb().
 *
 * @return ESP_OK on success.
 */
esp_err_t lcd_panel_init(void);

/**
 * @brief Return a pointer to the RGB565 framebuffer.
 *
 * The buffer has LCD_H_RES * LCD_V_RES * 2 bytes.
 * Pixel at (x, y) is at offset (y * LCD_H_RES + x) * 2.
 *
 * There is no locking — callers must complete writes before the next
 * vsync if they want tear-free rendering. For a Pomodoro timer that
 * updates once per second this is trivially satisfied.
 */
uint16_t *lcd_panel_get_fb(void);

/**
 * @brief Convert separate R, G, B byte values to a packed RGB565 word.
 *
 * Convenience macro — avoids scattering bit-shift magic around the codebase.
 *
 * @param r  Red   (0–255)
 * @param g  Green (0–255)
 * @param b  Blue  (0–255)
 */
#define RGB565(r, g, b) \
    (uint16_t)( (((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3) )

/** Handy colour constants */
#define COLOR_WHITE  RGB565(255, 255, 255)
#define COLOR_BLACK  RGB565(0,   0,   0  )
#define COLOR_RED    RGB565(255, 0,   0  )
#define COLOR_GREEN  RGB565(0,   200, 0  )
#define COLOR_BLUE   RGB565(0,   0,   255)
#define COLOR_GRAY   RGB565(180, 180, 180)
#define COLOR_ORANGE RGB565(255, 140, 0  )

#ifdef __cplusplus
}
#endif