/**
 * @file main.c
 * @brief Application entry point for the Pomodoro Timer.
 *
 * ── Startup order ────────────────────────────────────────────────────────────
 *
 *   The order below is mandatory — skipping or rearranging steps causes hangs
 *   or blank screens:
 *
 *   1. xl9555_init()    — bring up the I2C GPIO expander that controls the
 *                          LCD's SPI reset/CS/MOSI/SCLK lines and the power
 *                          rail enable pin.
 *   2. st7701s_init()   — send the ~80-register ST7701S init sequence via
 *                          XL9555 bit-bang SPI, then enable the backlight.
 *   3. lcd_panel_init() — start the ESP32-S3 RGB DMA engine. The DMA reads
 *                          the framebuffer continuously from PSRAM; it must
 *                          start AFTER the LCD controller is awake.
 *   4. display_clear()  — flood-fill the framebuffer so the first frame
 *                          shown is white, not garbage from uninitialised PSRAM.
 *   5. pomo_init()      — create the 1-second esp_timer (RTOS must be running).
 *   6. input_init()     — configure GPIO 0 button and start polling task.
 *   7. xTaskCreate()    — launch the display rendering loop.
 *
 * ── LILYGO T-RGB 2.1" hardware reality ──────────────────────────────────────
 *
 *   Most ESP32 LCD boards wire the LCD controller over a direct SPI bus.
 *   This board cannot — all GPIOs are consumed by the 16-bit RGB data bus.
 *
 *   Instead the board uses an XL9555 I2C GPIO expander (16 pins, address 0x20)
 *   on I2C SDA=GPIO8, SCL=GPIO48. The LCD SPI pins (CS, MOSI, SCLK, RST) are
 *   XL9555 outputs, NOT ESP32 GPIOs.
 *
 *   The RGB parallel bus control pins are:
 *     PCLK  → GPIO 42   VSYNC → GPIO 41   HSYNC → GPIO 47
 *     DE    → GPIO 45   BL    → GPIO 46
 *
 *   The 16 data pins (RGB565 on an 18-bit physical bus — 2 LSBs unconnected):
 *     Red[5:1]  → GPIO {2,3,5,6,7}
 *     Green[5:0]→ GPIO {9,10,11,12,13,14}
 *     Blue[5:1] → GPIO {15,16,17,18,21}
 *
 * Reference: https://github.com/Xinyuan-LilyGO/LilyGo-T-RGB (MIT licence)
 */

#include "xl9555.h"
#include "st7701s.h"
#include "lcd_panel.h"
#include "pomodoro.h"
#include "display.h"
#include "input.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

/* ── Display task ───────────────────────────────────────────────────────────
 *
 * Wakes every 200 ms, reads the Pomodoro state, and re-renders the screen.
 * 200 ms is more than fast enough for a 1-second timer, and avoids burning
 * CPU on pixel-pushing.
 *
 * Learning note — why a dedicated task? ─────────────────────────────────────
 *   Rendering ~460 KB of pixels takes a few milliseconds on the CPU.
 *   If we did this in the timer callback (which runs in a high-priority
 *   context) we would block other tasks. A separate lower-priority task
 *   lets FreeRTOS interleave rendering with everything else cleanly.
 * ────────────────────────────────────────────────────────────────────────────
 */
static void display_task(void *arg)
{
    pomo_status_t prev = {0};
    pomo_status_t curr;

    /* Force a full render on the first iteration */
    prev.state = (pomo_state_t)-1;

    while (1) {
        pomo_get_status(&curr);

        /*
         * Only re-render when something changed. Since the timer fires
         * every second and we poll every 200 ms, we'll catch every update.
         */
        bool changed = (curr.state                != prev.state)
                    || (curr.work_remaining_secs  != prev.work_remaining_secs)
                    || (curr.break_remaining_secs != prev.break_remaining_secs)
                    || (curr.sessions_completed   != prev.sessions_completed);

        if (changed) {
            display_render_status(&curr);
            prev = curr;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ── Entry point ────────────────────────────────────────────────────────────*/

void app_main(void)
{
    ESP_LOGI(TAG, "=== Pomodoro Timer boot ===");

    /* 1. XL9555 I2C GPIO expander — must come first, everything depends on it */
    ESP_ERROR_CHECK(xl9555_init());

    /* 2. ST7701S LCD controller — sends 80+ register writes via XL9555 SPI,
     *    then enables the backlight. Takes ~1-2 s (I2C bit-bang is slow). */
    ESP_ERROR_CHECK(st7701s_init());

    /* 3. RGB DMA panel — pins and timing are hardcoded in lcd_panel.c */
    ESP_ERROR_CHECK(lcd_panel_init());

    /* Initial screen fill — prevents showing garbage from uninitialised PSRAM */
    display_clear(COLOR_WHITE);

    /* 4. Pomodoro state machine + 1-second timer */
    pomo_init();

    /* 5. Input: configure GPIO 0 button, start polling task */
    ESP_ERROR_CHECK(input_init());

    /* 6. Display rendering task */
    xTaskCreate(
        display_task,
        "display",
        4096,                  /* needs more stack for snprintf + rendering */
        NULL,
        tskIDLE_PRIORITY + 2,  /* slightly higher than input so screen stays fresh */
        NULL
    );

    ESP_LOGI(TAG, "All systems go. Enjoy your focus session!");

    /* app_main can return — the scheduler keeps our tasks running */
}