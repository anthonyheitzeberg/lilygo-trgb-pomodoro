/**
 * @file xl9555.c
 * @brief XL9555 16-bit I2C GPIO expander — minimal driver for LCD init.
 *
 * ── How I2C works (quick primer) ─────────────────────────────────────────────
 *
 *   I2C is a two-wire serial bus (SDA = data, SCL = clock).
 *   Every transaction has the form:
 *
 *     START → [addr | R/W] → ACK → [reg] → ACK → [data] → ACK → STOP
 *
 *   Writing a register on the XL9555:
 *     1. START
 *     2. 7-bit device address + WRITE bit (0)  → ACK from XL9555
 *     3. 1-byte register address               → ACK
 *     4. 1-byte data value                     → ACK
 *     5. STOP
 *
 *   ESP-IDF's new I2C master driver (esp_driver_i2c) handles all of this.
 *   We just fill a transfer descriptor and call i2c_master_transmit().
 *
 * ── Bit-banged SPI through XL9555 ────────────────────────────────────────────
 *
 *   Each "bit" of the SPI stream requires two I2C writes (SCLK low with MOSI
 *   set, then SCLK high). That sounds slow — and it is (~2 ms per 9-bit frame
 *   at 400 kHz I2C). But the ST7701S init sequence only runs once at boot, so
 *   the 1-2 second delay is acceptable.
 *
 *   If you later need fast register updates (e.g. brightness PWM) you can
 *   cache the Port 0 state and write only one I2C byte per call, which is
 *   exactly what xl9555_set_level() does.
 */

#include "xl9555.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "xl9555";

/* ── Module state ───────────────────────────────────────────────────────────
 *
 * We keep a shadow copy of Port 0's output register so we can update a single
 * bit without first reading the chip (saves one I2C round-trip per write).
 *
 * Learning note — shadow registers ──────────────────────────────────────────
 *   Many I2C peripherals are write-only (no meaningful read of output state).
 *   A "shadow" in RAM mirrors what we last wrote, so bit-twiddling is fast.
 *   Always write the shadow, then flush it to hardware in one transaction.
 */
static i2c_master_dev_handle_t s_dev  = NULL;
static uint8_t                 s_port0 = 0xFF;  /* current Port 0 output */

/* ── Private helpers ────────────────────────────────────────────────────────*/

static void write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };

    /*
     * Learning note — i2c_master_transmit ──────────────────────────────────
     *   Sends `buf` as a consecutive I2C write:
     *     START → addr+W → buf[0] (reg) → buf[1] (val) → STOP
     *   Timeout is in milliseconds. -1 means "block until done".
     *   ESP_ERROR_CHECK aborts via esp_system_abort() if it returns non-OK.
     *   We skip it here to keep init robust; a single failed I2C write to
     *   the XL9555 during the LCD init sequence is survivable.
     */
    esp_err_t err = i2c_master_transmit(s_dev, buf, sizeof(buf), 50 /*ms*/);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C write reg 0x%02x failed: %s", reg, esp_err_to_name(err));
    }
}

/* Flush shadow register to hardware. */
static inline void flush_port0(void)
{
    write_reg(0x02 /*OUTP0*/, s_port0);
}

/* ── Public API ─────────────────────────────────────────────────────────────*/

esp_err_t xl9555_init(void)
{
    ESP_LOGI(TAG, "Initialising XL9555 on I2C (SDA=GPIO%d  SCL=GPIO%d  addr=0x%02X)",
             XL9555_SDA_GPIO, XL9555_SCL_GPIO, XL9555_I2C_ADDR);

    /* ── 1. Create the I2C master bus ────────────────────────────────────────
     *
     * Learning note — I2C bus vs device ──────────────────────────────────────
     *   ESP-IDF's new i2c_master API separates:
     *     i2c_master_bus   = the physical bus (SDA/SCL lines + hardware port)
     *     i2c_master_dev   = one logical device on that bus (address, speed)
     *   Multiple devices can share one bus. Each one gets its own handle.
     * ────────────────────────────────────────────────────────────────────────
     */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port            = XL9555_I2C_PORT,
        .sda_io_num          = XL9555_SDA_GPIO,
        .scl_io_num          = XL9555_SCL_GPIO,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,     /* filter glitches up to 7 APB cycles */
        .intr_priority       = 0,     /* use default interrupt priority */
        .trans_queue_depth   = 0,     /* synchronous mode */
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    /* ── 2. Attach the XL9555 device ─────────────────────────────────────────
     *
     * 400 kHz is "Fast Mode" I2C. The XL9555 supports up to 400 kHz.
     * The LILYGO library temporarily boosts to 1 MHz during init, but
     * 400 kHz is safe and the library drops back to 400 kHz after anyway.
     */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = XL9555_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev));

    /* ── 3. Configure Port 0 as all-outputs ──────────────────────────────────
     *
     * XL9555 Config register: 0 = output, 1 = input.
     * We set all 8 Port-0 pins to output (0x00).
     */
    write_reg(0x06 /*CFG0*/, 0x00);

    /* ── 4. Write safe initial state ──────────────────────────────────────────
     *
     * power_enable (IO0) HIGH  — turn on display power rail
     * CS   (IO3) HIGH          — SPI idle (deselected)
     * MOSI (IO4) LOW           — quiescent
     * SCLK (IO5) LOW           — quiescent
     * RST  (IO6) HIGH          — not in reset
     * SD_CS(IO7) HIGH          — SD idle
     * All others HIGH (unused; pull-up keeps I/O harmless)
     *
     * s_port0 starts at 0xFF so all bits are already initially HIGH.
     * Just clear MOSI and SCLK.
     */
    s_port0 &= ~(1 << XL9555_IO_LCD_MOSI);
    s_port0 &= ~(1 << XL9555_IO_LCD_SCLK);
    flush_port0();

    ESP_LOGI(TAG, "XL9555 ready. Port0=0x%02X", s_port0);
    return ESP_OK;
}

void xl9555_set_level(uint8_t pin, bool level)
{
    /* Update shadow register */
    if (level) {
        s_port0 |=  (uint8_t)(1u << pin);
    } else {
        s_port0 &= ~(uint8_t)(1u << pin);
    }

    /* Flush to hardware — one 2-byte I2C write */
    flush_port0();
}
