/**
 * @file st7701s.c
 * @brief ST7701S initialization sequence over 3-wire SPI.
 *
 * Learning notes are inline — read them top to bottom for a tour of
 * how SPI peripheral init works in ESP-IDF.
 */

#include "st7701s.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "st7701s";

/* ── Helpers ────────────────────────────────────────────────────────────────
 *
 * The ST7701S uses a simple 9-bit SPI protocol:
 *   bit[8]   = 0 → command byte follows
 *   bit[8]   = 1 → data byte follows
 *
 * We achieve this by treating the D/C flag as the MSB of a 9-bit word
 * and using SPI_TRANS_USE_TXDATA for small payloads (avoids malloc).
 */

#define CMD  0   // bit[8] = 0
#define DATA 1   // bit[8] = 1

/**
 * @brief Send one 9-bit word (command or data) over SPI.
 *
 * Learning note ─────────────────────────────────────────────────────────────
 *   spi_transaction_t is the ESP-IDF struct that describes one SPI transfer.
 *   Fields we use:
 *     .flags        = SPI_TRANS_USE_TXDATA  → data lives in the struct itself,
 *                                             no heap allocation needed.
 *     .length       = number of BITS to send (not bytes!).
 *     .tx_data[]    = up to 4 bytes of inline payload.
 *   spi_device_polling_transmit() blocks until the transfer is done —
 *   perfect for a one-time init sequence where we don't need async.
 * ────────────────────────────────────────────────────────────────────────────
 */
static void send_9bit(spi_device_handle_t spi, uint8_t dc, uint8_t byte)
{
    /* Pack the D/C bit + 8 data bits into a 16-bit word, MSB first.
     * We'll send 9 bits total. */
    uint16_t word = ((uint16_t)dc << 8) | byte;

    spi_transaction_t t = {
        .flags  = SPI_TRANS_USE_TXDATA,
        .length = 9,                        /* 9 bits */
        /* tx_data is a uint8_t[4] union — store our 9-bit word big-endian */
        .tx_data = {
            (uint8_t)(word >> 1),           /* upper 8 bits  */
            (uint8_t)(word << 7),           /* lower 1 bit, shifted to MSB */
            0, 0
        },
    };

    /*
     * Alternative mental model: think of it as sending 0x0_XY where
     * X = dc and Y = byte, spread across 9 clock edges.
     */
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t));
}

static inline void write_cmd(spi_device_handle_t spi, uint8_t cmd)
{
    send_9bit(spi, CMD, cmd);
}

static inline void write_data(spi_device_handle_t spi, uint8_t data)
{
    send_9bit(spi, DATA, data);
}

/* ── Init sequence ──────────────────────────────────────────────────────────
 *
 * This is the "magic blob" every LCD driver needs — a vendor-specific list
 * of register writes that configure timing, gamma, power rails, etc.
 * Values come from LILYGO's reference firmware and the ST7701S datasheet.
 *
 * Format: {CMD_OR_DATA, value}
 *   CMD_OR_DATA = 0 → command register address
 *   CMD_OR_DATA = 1 → data byte for the previous command
 */
typedef struct { uint8_t dc; uint8_t val; } init_cmd_t;

static const init_cmd_t ST7701S_INIT_SEQ[] = {
    /* ── Page 1 commands ── */
    {CMD,  0xFF}, {DATA, 0x77}, {DATA, 0x01}, {DATA, 0x00},
    {DATA, 0x00}, {DATA, 0x10},

    {CMD,  0xC0}, {DATA, 0x3B}, {DATA, 0x00},
    {CMD,  0xC1}, {DATA, 0x0D}, {DATA, 0x02},
    {CMD,  0xC2}, {DATA, 0x31}, {DATA, 0x05},
    {CMD,  0xCD}, {DATA, 0x00},

    /* Positive voltage gamma */
    {CMD,  0xB0},
    {DATA, 0x00}, {DATA, 0x11}, {DATA, 0x18}, {DATA, 0x0E},
    {DATA, 0x11}, {DATA, 0x06}, {DATA, 0x07}, {DATA, 0x08},
    {DATA, 0x07}, {DATA, 0x22}, {DATA, 0x04}, {DATA, 0x12},
    {DATA, 0x0F}, {DATA, 0xAA}, {DATA, 0x31}, {DATA, 0x18},

    /* Negative voltage gamma */
    {CMD,  0xB1},
    {DATA, 0x00}, {DATA, 0x11}, {DATA, 0x19}, {DATA, 0x0E},
    {DATA, 0x12}, {DATA, 0x07}, {DATA, 0x08}, {DATA, 0x08},
    {DATA, 0x08}, {DATA, 0x22}, {DATA, 0x04}, {DATA, 0x11},
    {DATA, 0x11}, {DATA, 0xA9}, {DATA, 0x32}, {DATA, 0x18},

    /* ── Page 2 commands ── */
    {CMD,  0xFF}, {DATA, 0x77}, {DATA, 0x01}, {DATA, 0x00},
    {DATA, 0x00}, {DATA, 0x11},

    {CMD,  0xB0}, {DATA, 0x60},   /* Positive voltage */
    {CMD,  0xB1}, {DATA, 0x30},   /* Negative voltage */
    {CMD,  0xB2}, {DATA, 0x87},
    {CMD,  0xB3}, {DATA, 0x80},
    {CMD,  0xB5}, {DATA, 0x49},
    {CMD,  0xB7}, {DATA, 0x85},
    {CMD,  0xB8}, {DATA, 0x21},
    {CMD,  0xC1}, {DATA, 0x78},
    {CMD,  0xC2}, {DATA, 0x78},

    /* Power-on sequence delay */
    {CMD,  0xD0}, {DATA, 0x88},

    /* ── Page 3 (GIP timing) ── */
    {CMD,  0xFF}, {DATA, 0x77}, {DATA, 0x01}, {DATA, 0x00},
    {DATA, 0x00}, {DATA, 0x12},

    {CMD,  0xD1}, {DATA, 0x81},
    {CMD,  0xD2}, {DATA, 0x06},

    /* ── Page 0 / standard commands ── */
    {CMD,  0xFF}, {DATA, 0x77}, {DATA, 0x01}, {DATA, 0x00},
    {DATA, 0x00}, {DATA, 0x00},

    {CMD,  0x36}, {DATA, 0x00},   /* Memory Access Control (rotation = 0°) */
    {CMD,  0x3A}, {DATA, 0x60},   /* Interface pixel format: 18-bit RGB */

    {CMD,  0x11},                 /* Sleep Out — must wait 120 ms after */
};

/* ── Public API ─────────────────────────────────────────────────────────────*/

esp_err_t st7701s_init(const st7701s_io_config_t *io)
{
    ESP_LOGI(TAG, "Initialising ST7701S...");

    /* ── 1. Hardware reset ─────────────────────────────────────────────────
     *
     * Learning note: gpio_config() is the preferred way to configure a pin.
     * Always fill the entire gpio_config_t — avoid partial initialisation.
     */
    if (io->reset >= 0) {
        gpio_config_t rst_cfg = {
            .pin_bit_mask = (1ULL << io->reset),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&rst_cfg));

        gpio_set_level(io->reset, 0);
        vTaskDelay(pdMS_TO_TICKS(10));   /* hold reset low ≥ 10 ms */
        gpio_set_level(io->reset, 1);
        vTaskDelay(pdMS_TO_TICKS(120));  /* wait for internal oscillator */
    }

    /* ── 2. Configure backlight ────────────────────────────────────────────*/
    if (io->backlight >= 0) {
        gpio_config_t bl_cfg = {
            .pin_bit_mask = (1ULL << io->backlight),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&bl_cfg));
        gpio_set_level(io->backlight, 1);  /* on */
    }

    /* ── 3. Set up the SPI bus ─────────────────────────────────────────────
     *
     * Learning note ─────────────────────────────────────────────────────────
     *   ESP-IDF SPI setup is two-step:
     *     a) spi_bus_initialize() — configures the physical pins once.
     *     b) spi_bus_add_device()  — attaches a logical device (with its own
     *                                CS, clock speed, and mode) to that bus.
     *   You can have multiple devices on one bus.
     *
     *   SPI_HOST selection:
     *     SPI2_HOST (HSPI) and SPI3_HOST (VSPI) are the two user-facing hosts.
     *     We use SPI2_HOST for the LCD command bus.
     *
     *   DMA channel:
     *     SPI_DMA_CH_AUTO lets IDF pick the DMA channel. For an init-only bus
     *     that only sends tiny 9-bit frames we could also use SPI_DMA_DISABLED,
     *     but AUTO is safer for future use.
     * ────────────────────────────────────────────────────────────────────────
     */
    spi_bus_config_t buscfg = {
        .mosi_io_num     = io->spi_mosi,
        .miso_io_num     = -1,           /* LCD is write-only on this bus */
        .sclk_io_num     = io->spi_sclk,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4,            /* we send at most 4 bytes at a time */
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000,  /* 1 MHz — conservative for init */
        .mode           = 0,                 /* SPI mode 0: CPOL=0, CPHA=0   */
        .spics_io_num   = io->spi_cs,
        .queue_size     = 1,
        /* No pre/post callbacks needed for synchronous polling transfers */
    };
    spi_device_handle_t spi;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi));

    /* ── 4. Send init sequence ─────────────────────────────────────────────*/
    size_t n = sizeof(ST7701S_INIT_SEQ) / sizeof(ST7701S_INIT_SEQ[0]);
    for (size_t i = 0; i < n; i++) {
        send_9bit(spi, ST7701S_INIT_SEQ[i].dc, ST7701S_INIT_SEQ[i].val);
    }

    /* Sleep Out requires 120 ms before sending further commands */
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Display On */
    write_cmd(spi, 0x29);
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG, "ST7701S ready");

    /*
     * We intentionally leave the SPI device attached. In a real product you
     * might release it here since the RGB peripheral takes over from now on.
     * Keeping it lets you send runtime commands (e.g. brightness via WRDISBV).
     */
    return ESP_OK;
}