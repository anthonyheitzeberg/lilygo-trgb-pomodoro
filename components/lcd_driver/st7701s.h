#pragma once

/**
 * @file st7701s.h
 * @brief ST7701S LCD controller init driver for LILYGO T-RGB 2.1".
 *
 * ── Architecture on this board ───────────────────────────────────────────────
 *
 *   Most ESP32 LCD boards wire the ST7701S SPI init bus directly to free
 *   GPIOs. The LILYGO T-RGB does NOT — there are no spare GPIOs. Instead:
 *
 *     ESP32-S3 ──(I2C)──► XL9555 GPIO expander ──(bit-bang SPI)──► ST7701S
 *
 *   So st7701s_init() drives the SPI init sequence *through* the XL9555 driver
 *   (xl9555.h), which in turn talks to the XL9555 over I2C.
 *
 * ── Two-bus design of the ST7701S ────────────────────────────────────────────
 *
 *   Bus 1 — SPI (3-wire, 9-bit): used ONCE at boot to send ~80 register
 *            writes that configure timing, gamma, and power rails.
 *   Bus 2 — RGB parallel (16-bit): used every frame for pixel data, managed
 *            entirely by the ESP32-S3 LCD DMA engine (see lcd_panel.h).
 *
 * ── 9-bit SPI frame format ────────────────────────────────────────────────────
 *
 *   Each frame is 9 bits, MSB first:
 *     bit 8 = D/C (0 = command register address, 1 = data byte)
 *     bit 7-0 = payload byte
 *
 *   CS goes LOW for the duration of each 9-bit frame.
 *
 * ── Backlight ────────────────────────────────────────────────────────────────
 *
 *   GPIO 46 drives a backlight LED driver (SY series) using a custom
 *   pulse-counting protocol:
 *     • Pull LOW for >3 ms → reset to minimum brightness (step 1).
 *     • Each rising edge increments one brightness step (max 16 steps).
 *     • Drive HIGH and leave it → max brightness immediately from off-state.
 *
 *   st7701s_init() sets maximum backlight brightness automatically.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Send the ST7701S init sequence and enable the backlight.
 *
 * Must be called once, after xl9555_init(), before lcd_panel_init().
 *
 * @return ESP_OK on success.
 */
esp_err_t st7701s_init(void);

#ifdef __cplusplus
}
#endif