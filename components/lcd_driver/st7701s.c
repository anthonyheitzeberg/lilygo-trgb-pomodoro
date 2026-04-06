/**
 * @file st7701s.c
 * @brief ST7701S init via XL9555 bit-bang SPI for LILYGO T-RGB 2.1".
 *
 * Learning notes are inline throughout this file.
 */

#include "st7701s.h"
#include "xl9555.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "st7701s";

/* ── Board constants ────────────────────────────────────────────────────────*/

/** GPIO driving the backlight LED driver IC (SY-series pulse-count dimmer). */
#define PIN_BL   46

/* ── Bit-banged 9-bit SPI via XL9555 ────────────────────────────────────────
 *
 * Learning note — why bit-bang and not hardware SPI? ─────────────────────────
 *
 *   On the LILYGO T-RGB the ST7701S SPI pins (CS, MOSI, SCLK) are wired to
 *   the XL9555 I2C GPIO expander, NOT to ESP32 GPIO pins. There are no spare
 *   ESP32 GPIOs — every pin is consumed by the 16-bit RGB data bus.
 *
 *   Bit-banging means we emulate SPI in software: we toggle individual XL9555
 *   outputs (via I2C writes) to produce the CS / clock / data signals.
 *
 *   Performance trade-off:
 *     Hardware SPI  → ~10 ns per bit (50+ MHz)
 *     Bit-bang here → ~5 µs per bit (I2C 400 kHz overhead)
 *   For a one-time init sequence of ~80 register writes ≈ 1 second. Fine.
 * ────────────────────────────────────────────────────────────────────────────
 */

/**
 * @brief Send one 9-bit SPI frame: bit[8]=dc, bits[7:0]=byte.
 *
 * SPI mode 0 (CPOL=0, CPHA=0):
 *   - SCLK idles LOW.
 *   - Data is placed on MOSI before the rising edge of SCLK.
 *   - Data is captured on the rising edge.
 *
 * Bit order: MSB first (bit 8 goes first on the wire).
 */
static void spi_send_9bit(uint8_t dc, uint8_t byte)
{
    /* Pack D/C flag + 8-bit payload into a 9-bit value */
    uint16_t word = ((uint16_t)dc << 8) | byte;

    /* Assert CS (active LOW) */
    xl9555_set_level(XL9555_IO_LCD_CS, false);

    /* Shift out 9 bits, MSB first */
    for (int bit = 8; bit >= 0; bit--) {
        bool mosi = (word >> bit) & 1u;

        /*
         * Step 1: put data on MOSI while SCLK is LOW.
         * Step 2: raise SCLK — the ST7701S samples MOSI on this edge.
         * Step 3: lower SCLK back (implicit: next iteration starts with SCLK=0).
         *
         * Each xl9555_set_level() call flushes one I2C byte to hardware,
         * so "MOSI then SCLK" == two I2C writes per bit.
         */
        xl9555_set_level(XL9555_IO_LCD_MOSI, mosi);
        xl9555_set_level(XL9555_IO_LCD_SCLK, true);
        xl9555_set_level(XL9555_IO_LCD_SCLK, false);
    }

    /* Deassert CS */
    xl9555_set_level(XL9555_IO_LCD_CS, true);
}

static inline void spi_cmd(uint8_t cmd)   { spi_send_9bit(0, cmd); }
static inline void spi_data(uint8_t data) { spi_send_9bit(1, data); }

/* ── ST7701S init command table ─────────────────────────────────────────────
 *
 * Learning note — register init tables ──────────────────────────────────────
 *
 *   Every LCD controller needs a "magic blob" — a vendor-supplied sequence of
 *   register writes that configure:
 *     • Power supply voltages (VCOM, AVDD, VGH/VGL)
 *     • Gamma curve correction (positive and negative gamma)
 *     • Gate/Source driver timing (GIP)
 *     • Interface format (RGB565 vs RGB666)
 *     • Display on/off, inversion, etc.
 *
 *   These values come directly from LILYGO's reference firmware (open source,
 *   MIT licence). The ST7701S datasheet explains what each register does.
 *
 * Table format:
 *   cmd       = register address (sent as 9-bit frame with D/C=0)
 *   data[]    = data bytes (each sent with D/C=1)
 *   n         = number of data bytes; 0x80 flag means "delay 100 ms after"
 *   0xFF,0    = end-of-table sentinel
 * ────────────────────────────────────────────────────────────────────────────
 */
typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t n;   /* lower 5 bits = byte count; bit 7 = add 100 ms delay */
} lcd_reg_t;

static const lcd_reg_t ST7701S_INIT[] = {
    /* ── Page 1: gamma + power ─────────────────────────────────────── */
    {0xFF, {0x77,0x01,0x00,0x00,0x10}, 0x05},
    {0xC0, {0x3B,0x00},                0x02},  /* Line Number = 0x3B → 480 lines */
    {0xC1, {0x0B,0x02},                0x02},  /* VBP, VFP */
    {0xC2, {0x07,0x02},                0x02},  /* PCLK inversion */
    {0xCC, {0x10},                     0x01},
    {0xCD, {0x08},                     0x01},  /* RGB666 CDI setting */

    /* Positive gamma (16 bytes) */
    {0xB0, {0x00,0x11,0x16,0x0E,0x11,0x06,0x05,0x09,
            0x08,0x21,0x06,0x13,0x10,0x29,0x31,0x18}, 0x10},
    /* Negative gamma (16 bytes) */
    {0xB1, {0x00,0x11,0x16,0x0E,0x11,0x07,0x05,0x09,
            0x09,0x21,0x05,0x13,0x11,0x2A,0x31,0x18}, 0x10},

    /* ── Page 2: power control ──────────────────────────────────────── */
    {0xFF, {0x77,0x01,0x00,0x00,0x11}, 0x05},
    {0xB0, {0x6D}, 0x01},  /* Positive VOP */
    {0xB1, {0x37}, 0x01},  /* Negative VOP */
    {0xB2, {0x81}, 0x01},
    {0xB3, {0x80}, 0x01},
    {0xB5, {0x43}, 0x01},  /* VGL clamp */
    {0xB7, {0x85}, 0x01},
    {0xB8, {0x20}, 0x01},
    {0xC1, {0x78}, 0x01},
    {0xC2, {0x78}, 0x01},
    {0xC3, {0x8C}, 0x01},
    {0xD0, {0x88}, 0x01},  /* Power-on sequence */

    /* ── Page 2: GIP source/gate timing ────────────────────────────── */
    {0xE0, {0x00,0x00,0x02},                                           0x03},
    {0xE1, {0x03,0xA0,0x00,0x00,0x04,0xA0,0x00,0x00,0x00,0x20,0x20}, 0x0B},
    {0xE2, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00},                                           0x0D},
    {0xE3, {0x00,0x00,0x11,0x00},                                      0x04},
    {0xE4, {0x22,0x00},                                                0x02},
    {0xE5, {0x05,0xEC,0xA0,0xA0,0x07,0xEE,0xA0,0xA0,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},                  0x10},
    {0xE6, {0x00,0x00,0x11,0x00},                                      0x04},
    {0xE7, {0x22,0x00},                                                0x02},
    {0xE8, {0x06,0xED,0xA0,0xA0,0x08,0xEF,0xA0,0xA0,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},                  0x10},
    {0xEB, {0x00,0x00,0x40,0x40,0x00,0x00,0x00},                       0x07},
    {0xED, {0xFF,0xFF,0xFF,0xBA,0x0A,0xBF,0x45,0xFF,
            0xFF,0x54,0xFB,0xA0,0xAB,0xFF,0xFF,0xFF},                  0x10},
    {0xEF, {0x10,0x0D,0x04,0x08,0x3F,0x1F},                           0x06},

    /* ── Page 3 ─────────────────────────────────────────────────────── */
    {0xFF, {0x77,0x01,0x00,0x00,0x13}, 0x05},
    {0xEF, {0x08},                      0x01},

    /* ── Page 0: standard MIPI commands ────────────────────────────── */
    {0xFF, {0x77,0x01,0x00,0x00,0x00}, 0x05},
    {0x36, {0x08},                      0x01},  /* Memory Access Ctrl */
    {0x3A, {0x66},                      0x01},  /* Pixel format: RGB666 on-wire
                                                 *   (ESP32 sends 16-bit / RGB565
                                                 *    but the physical bus is 18-bit.
                                                 *    0x66 = MCU 18-bit, RGB 18-bit.
                                                 *    The pin mapping skips the LSB
                                                 *    of R and B naturally.) */
    {0x11, {0x00}, 0x80},  /* Sleep Out — 0x80 flag → 100 ms delay */
    {0x29, {0x00}, 0x80},  /* Display On */

    {0x00, {0x00}, 0xFF},  /* End sentinel */
};

/* ── Backlight ───────────────────────────────────────────────────────────────
 *
 * The backlight LED driver uses a pulse-count dimming protocol:
 *   • Initial rising edge from 0 V → sets brightness to full (16/16).
 *   • Each additional LOW→HIGH toggle decrements one brightness step.
 *   • Driving LOW for >3 ms resets to off.
 *
 * To get maximum brightness from cold start:
 *   1. Drive LOW (off / reset state)
 *   2. Wait >3 ms
 *   3. Drive HIGH once → jumps to maximum brightness (step 16)
 */
static void backlight_on(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BL),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    gpio_set_level(PIN_BL, 0);
    vTaskDelay(pdMS_TO_TICKS(5));   /* > 3 ms: reset the dimmer IC */
    gpio_set_level(PIN_BL, 1);     /* first rising edge → full brightness */
}

/* ── Public API ─────────────────────────────────────────────────────────────*/

esp_err_t st7701s_init(void)
{
    ESP_LOGI(TAG, "Initialising ST7701S (via XL9555 bit-bang SPI)...");

    /* ── 1. Hardware reset (via XL9555 IO6) ─────────────────────────────────
     *
     * The reset pin is active LOW. Pull it LOW for 20 ms, then HIGH.
     * The ST7701S datasheet requires ≥10 µs active reset; we use 20 ms to be
     * generous. After release, the chip needs 120 ms to stabilise its internal
     * oscillator before accepting SPI commands.
     */
    xl9555_set_level(XL9555_IO_LCD_RST, false);
    vTaskDelay(pdMS_TO_TICKS(20));
    xl9555_set_level(XL9555_IO_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* ── 2. Send init register table ────────────────────────────────────────*/
    for (int i = 0; ST7701S_INIT[i].n != 0xFF; i++) {
        const lcd_reg_t *r = &ST7701S_INIT[i];
        uint8_t count = r->n & 0x1F;   /* lower 5 bits = byte count */
        bool    delay = r->n & 0x80;   /* bit 7 = insert 100 ms delay */

        spi_cmd(r->cmd);
        for (int j = 0; j < count; j++) {
            spi_data(r->data[j]);
        }

        if (delay) {
            vTaskDelay(pdMS_TO_TICKS(120));
        }
    }

    ESP_LOGI(TAG, "ST7701S init done");

    /* ── 3. Turn on backlight ───────────────────────────────────────────────*/
    backlight_on();
    ESP_LOGI(TAG, "Backlight on");

    return ESP_OK;
}