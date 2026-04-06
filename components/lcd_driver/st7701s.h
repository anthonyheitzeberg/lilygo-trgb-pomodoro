#pragma once

/**
 * @file st7701s.h
 * @brief Low-level ST7701S LCD controller driver.
 *
 * The LILYGO T-RGB board wires the ST7701S over:
 *   - SPI (QSPI / 3-wire SPI) for command/init sequences
 *   - RGB parallel interface for pixel data (16-bit RGB565)
 *
 * This layer owns only the SPI init sequence. Pixel data is pushed
 * automatically by the ESP32-S3 RGB LCD peripheral via DMA — we never
 * manually write pixels here.
 *
 * Learning note ──────────────────────────────────────────────────────────────
 *   The ST7701S has two buses:
 *     1. A slow SPI bus used ONCE at boot to send ~30 init commands.
 *     2. A fast RGB parallel bus used every frame for pixel data.
 *   ESP-IDF's `esp_lcd_panel_rgb` driver manages bus #2 entirely for you.
 *   We only need to touch bus #1 in this file.
 * ────────────────────────────────────────────────────────────────────────────
 */

#include "esp_err.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/** All GPIO numbers for the LILYGO T-RGB 2.1 inch board. */
typedef struct {
    /* SPI command bus (3-wire, used only for init) */
    int spi_cs;     ///< Chip-select for the init SPI bus
    int spi_sclk;   ///< Clock
    int spi_mosi;   ///< MOSI (data out to LCD)

    /* Hardware reset */
    int reset;      ///< Active-low reset pin (-1 if not wired)

    /* Backlight (PWM or plain GPIO) */
    int backlight;  ///< -1 if not used
} st7701s_io_config_t;

/**
 * @brief Send the full ST7701S initialization command sequence over SPI.
 *
 * Call this ONCE, before you start the RGB peripheral.
 *
 * @param io  Pin configuration for the command SPI bus.
 * @return    ESP_OK on success.
 */
esp_err_t st7701s_init(const st7701s_io_config_t *io);

#ifdef __cplusplus
}
#endif