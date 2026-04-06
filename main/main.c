/**
 * @file main.c
 * @brief Application entry point.
 *
 * app_main() is ESP-IDF's equivalent of main() — it runs after the
 * RTOS scheduler has started, so FreeRTOS APIs are safe to call here.
 *
 * Responsibility of this file: initialise every subsystem in the correct
 * order, then launch the display task. That's it — no logic lives here.
 *
 * Startup order matters:
 *   1. LCD hardware init (SPI init sequence — must happen before RGB DMA)
 *   2. RGB panel init  (DMA starts, framebuffer allocated)
 *   3. Pomodoro logic  (esp_timer created — RTOS must be running)
 *   4. Input handler   (buttons configured, polling task spawned)
 *   5. Display task    (starts rendering loop)
 *
 * ──────────────────────────────────────────────────────────────────────────
 * LILYGO T-RGB 2.1" pin map (ESP32-S3R8)
 * ──────────────────────────────────────────────────────────────────────────
 * SPI (command bus)
 *   CS    → GPIO 39
 *   SCLK  → GPIO 48
 *   MOSI  → GPIO 47
 * Reset   → GPIO 40
 * BL      → GPIO 46  (backlight)
 *
 * RGB parallel bus
 *   PCLK  → GPIO 21
 *   VSYNC → GPIO  3
 *   HSYNC → GPIO 46  ← shared with BL? Check your board rev!
 *   DE    → GPIO  5
 *   D0-D4 (B) → GPIO 14, 38, 18, 17, 16
 *   D5-D10(G) → GPIO 15, 13, 12, 11, 10,  9
 *   D11-D15(R)→ GPIO  8,  7,  6,  5,  4
 *
 * NOTE: Pin numbers above are from LILYGO's open-source schematic for the
 * T-RGB v1.1. Always verify against your specific board revision's schematic
 * at https://github.com/Xinyuan-LilyGO/LilyGo-T-RGB before flashing.
 * ──────────────────────────────────────────────────────────────────────────
 */

#include "st7701s.h"
#include "lcd_panel.h"
#include "pomodoro.h"
#include "display.h"
#include "input.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

/* ── Pin definitions ────────────────────────────────────────────────────────
 * Centralise all board-specific constants here so the rest of the code
 * is hardware-agnostic.
 */

/* SPI command bus */
#define PIN_SPI_CS    39
#define PIN_SPI_SCLK  48
#define PIN_SPI_MOSI  47
#define PIN_RESET     40
#define PIN_BL        46

/* RGB parallel bus */
#define PIN_PCLK   21
#define PIN_VSYNC   3
#define PIN_HSYNC  46   /* verify schematic — may differ on your rev */
#define PIN_DE      5

/* Data bus: B[4:0], G[5:0], R[4:0] → 16 total */
#define RGB_DATA_PINS  \
    14, 38, 18, 17, 16,      /* B4..B0  */ \
    15, 13, 12, 11, 10,  9,  /* G5..G0  */ \
     8,  7,  6,  5,  4       /* R4..R0  */

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

    /* 1. ST7701S init sequence (SPI) */
    st7701s_io_config_t lcd_io = {
        .spi_cs   = PIN_SPI_CS,
        .spi_sclk = PIN_SPI_SCLK,
        .spi_mosi = PIN_SPI_MOSI,
        .reset    = PIN_RESET,
        .backlight = PIN_BL,
    };
    ESP_ERROR_CHECK(st7701s_init(&lcd_io));

    /* 2. RGB panel + framebuffer */
    lcd_panel_io_config_t panel_io = {
        .pclk  = PIN_PCLK,
        .vsync = PIN_VSYNC,
        .hsync = PIN_HSYNC,
        .de    = PIN_DE,
        .data  = { RGB_DATA_PINS },
    };
    ESP_ERROR_CHECK(lcd_panel_init(&panel_io));

    /* Initial screen fill so we don't show garbage while Pomodoro inits */
    display_clear(COLOR_WHITE);

    /* 3. Pomodoro state machine + 1-second timer */
    pomo_init();

    /* 4. Input: configure buttons, start polling task */
    ESP_ERROR_CHECK(input_init());

    /* 5. Display rendering task */
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