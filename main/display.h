#pragma once

/**
 * @file display.h
 * @brief Framebuffer rendering layer — draws the Pomodoro UI.
 *
 * This module knows about pixels and fonts. It does NOT know about timers,
 * buttons, or hardware initialisation — those are handled elsewhere.
 *
 * Rendering pipeline:
 *   1. display_clear()          — flood-fill framebuffer with a colour
 *   2. display_draw_text()      — stamp glyphs into the framebuffer
 *   3. display_render_status()  — high-level: draws the full Pomodoro UI
 *
 * Font: we use a simple 8×8 bitmap font embedded as a C array.
 *       Each character is 8 bytes, one byte per row, MSB = leftmost pixel.
 *       We scale it up 4× for readability (effective size: 32×32 per char).
 */

#include <stdint.h>
#include "lcd_panel.h"
#include "pomodoro.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Font scale factor. 1 = 8×8 px, 4 = 32×32 px per glyph. */
#define FONT_SCALE 6

/** Glyph width/height in screen pixels after scaling */
#define GLYPH_W (8 * FONT_SCALE)
#define GLYPH_H (8 * FONT_SCALE)

/**
 * @brief Flood-fill the entire framebuffer with a single colour.
 * @param color  RGB565 colour.
 */
void display_clear(uint16_t color);

/**
 * @brief Draw a null-terminated ASCII string into the framebuffer.
 *
 * Characters outside the printable range (0x20–0x7E) are rendered as space.
 *
 * @param x      Top-left X coordinate (pixels from left edge).
 * @param y      Top-left Y coordinate (pixels from top edge).
 * @param text   Null-terminated string.
 * @param fg     Foreground colour (RGB565).
 * @param bg     Background colour (RGB565).
 */
void display_draw_text(int x, int y, const char *text, uint16_t fg, uint16_t bg);

/**
 * @brief Draw a filled rectangle.
 */
void display_fill_rect(int x, int y, int w, int h, uint16_t color);

/**
 * @brief High-level render: draw the complete Pomodoro UI from a status snapshot.
 *
 * Call this from the display task whenever the state changes.
 *
 * @param status  Current Pomodoro state (obtained from pomo_get_status).
 */
void display_render_status(const pomo_status_t *status);

#ifdef __cplusplus
}
#endif