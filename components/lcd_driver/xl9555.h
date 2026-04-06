#pragma once

/**
 * @file xl9555.h
 * @brief XL9555 16-bit I2C GPIO expander driver.
 *
 * ── Why does this board need an I/O expander? ────────────────────────────────
 *
 *   The LILYGO T-RGB's ST7701S LCD controller needs a slow 9-bit SPI init
 *   sequence at boot. However the ESP32-S3 has no spare hardware SPI bus for
 *   it — every available GPIO is already consumed by the RGB parallel data bus.
 *
 *   The XL9555 solves this: a single I2C bus (only 2 GPIO pins) controls a
 *   16-pin digital output port. The board bit-bangs SPI *through* those
 *   output pins to reach the LCD controller.
 *
 * ── XL9555 hardware summary ──────────────────────────────────────────────────
 *
 *   Package:   16 I/O pins split into Port 0 (IO0–IO7) and Port 1 (IO8–IO15).
 *   I2C addr:  0x20 (A2=A1=A0=GND on this board).
 *   Registers:
 *     0x00  Input Port 0   (read-only)
 *     0x01  Input Port 1   (read-only)
 *     0x02  Output Port 0  (read/write)
 *     0x03  Output Port 1  (read/write)
 *     0x06  Config Port 0  (0 = output, 1 = input)
 *     0x07  Config Port 1  (0 = output, 1 = input)
 *
 * ── LILYGO T-RGB 2.1" pin assignments (Port 0) ───────────────────────────────
 *
 *   IO0  power_enable (must be HIGH for display rail)
 *   IO3  SPI_CS   for ST7701S init
 *   IO4  SPI_MOSI for ST7701S init
 *   IO5  SPI_SCLK for ST7701S init
 *   IO6  LCD_RESET (active-low)
 *   IO7  SD card CS
 *
 * The rest of Port 0 and all of Port 1 are used by touch / other peripherals.
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Board-specific constants ───────────────────────────────────────────────*/

/** I2C address of the XL9555 on the LILYGO T-RGB (A2=A1=A0=GND). */
#define XL9555_I2C_ADDR   0x20

/** ESP32 I2C bus to use. */
#define XL9555_I2C_PORT   I2C_NUM_0

/** GPIO for I2C data (SDA). */
#define XL9555_SDA_GPIO   8

/** GPIO for I2C clock (SCL). */
#define XL9555_SCL_GPIO   48

/* ── XL9555 IO pin numbers (0-15) ──────────────────────────────────────────*/

/*  Port 0 (pins 0-7) = I2C register pair 0x02/0x06                          */
#define XL9555_IO_POWER_EN   0   ///< Display power rail enable (active HIGH)
#define XL9555_IO_LCD_CS     3   ///< ST7701S SPI chip-select   (active LOW)
#define XL9555_IO_LCD_MOSI   4   ///< ST7701S SPI MOSI
#define XL9555_IO_LCD_SCLK   5   ///< ST7701S SPI clock
#define XL9555_IO_LCD_RST    6   ///< ST7701S reset              (active LOW)
#define XL9555_IO_SD_CS      7   ///< SD card SPI CS

/* ── API ────────────────────────────────────────────────────────────────────*/

/**
 * @brief Initialise the I2C bus and the XL9555 chip.
 *
 * Configures all Port 0 pins needed for LCD init as outputs.
 * Safe initial state: power_enable=HIGH, CS=HIGH, SCLK=LOW, MOSI=LOW,
 *                     RST=HIGH (not in reset).
 *
 * Must be called before any other xl9555_* function.
 *
 * @return ESP_OK on success.
 */
esp_err_t xl9555_init(void);

/**
 * @brief Set a single XL9555 IO pin HIGH or LOW.
 *
 * Only Port 0 pins (0-7) are used by this driver.
 *
 * @param pin    Pin number (0-15, but only 0-7 are driven here).
 * @param level  true = HIGH, false = LOW.
 */
void xl9555_set_level(uint8_t pin, bool level);

#ifdef __cplusplus
}
#endif
