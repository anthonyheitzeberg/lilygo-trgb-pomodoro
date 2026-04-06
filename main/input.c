/**
 * @file input.c
 * @brief Single-button input handler with short / long press detection.
 *
 * The LILYGO T-RGB has only ONE physical button (GPIO 0 / BOOT button).
 * Every other GPIO is used by the RGB parallel bus or the I2C expander.
 *
 * We map two Pomodoro actions to the single button by press duration:
 *   Short press (< LONG_PRESS_MS ms): Start / Pause
 *   Long press  (≥ LONG_PRESS_MS ms): Reset
 *
 * ── State machine overview ───────────────────────────────────────────────────
 *
 *   IDLE ──[pin goes LOW + debounced]──► HELD (record press_start_ms)
 *   HELD ──[pin goes HIGH + debounced]──► IDLE
 *                                          if held_ms < LONG_PRESS_MS → start/pause
 *                                          if held_ms ≥ LONG_PRESS_MS → reset
 *
 * ── Why detect on release, not on press? ────────────────────────────────────
 *
 *   We can't tell if a press is "short" or "long" until the user lifts their
 *   finger. Firing on the falling edge would mean we'd never know to do reset.
 *   Firing on release lets us measure the full press duration first.
 */

#include "input.h"
#include "pomodoro.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"   /* for esp_timer_get_time() — microsecond wall clock */

static const char *TAG = "input";

/* ── Debounce tuning ────────────────────────────────────────────────────────*/

#define SAMPLE_PERIOD_MS  10   /* poll every 10 ms */
#define DEBOUNCE_TICKS     5   /* 5 × 10 ms = 50 ms stable required */

/* ── Button FSM ──────────────────────────────────────────────────────────────
 *
 * Learning note — debounce + hold detection ─────────────────────────────────
 *
 *   Mechanical buttons bounce (rapid open/close) for ~5–50 ms after each
 *   edge. Without debouncing one physical press looks like many presses.
 *
 *   We use an "integrator": count consecutive samples in the same state.
 *   Only transition when the count reaches DEBOUNCE_TICKS.
 *
 *   press_start_us is set (using the µs wall clock) when a debounced press
 *   is first confirmed. On debounced release we compute the hold duration
 *   and dispatch the correct action.
 * ────────────────────────────────────────────────────────────────────────────
 */
typedef enum { BTN_IDLE, BTN_HELD } btn_state_t;

static struct {
    btn_state_t state;
    int         stable_count;
    int64_t     press_start_us;   /* monotonic µs timestamp at press confirm */
} s_btn = { BTN_IDLE, 0, 0 };

/* ── Polling task ────────────────────────────────────────────────────────────*/

static void input_task(void *arg)
{
    ESP_LOGI(TAG, "Input task started (GPIO%d  short=start/pause  long=reset)",
             BTN_GPIO);

    while (1) {
        int level = gpio_get_level(BTN_GPIO);   /* 0 = LOW = pressed */

        switch (s_btn.state) {

        case BTN_IDLE:
            if (level == 0) {
                /* Pin is LOW — accumulate debounce count */
                s_btn.stable_count++;
                if (s_btn.stable_count >= DEBOUNCE_TICKS) {
                    /* Debounced press confirmed */
                    s_btn.state          = BTN_HELD;
                    s_btn.stable_count   = 0;
                    s_btn.press_start_us = esp_timer_get_time();
                    ESP_LOGI(TAG, "Button pressed");
                }
            } else {
                s_btn.stable_count = 0;   /* glitch — reset counter */
            }
            break;

        case BTN_HELD:
            if (level == 1) {
                /* Pin returned HIGH — accumulate debounce count for release */
                s_btn.stable_count++;
                if (s_btn.stable_count >= DEBOUNCE_TICKS) {
                    /* Debounced release confirmed — compute hold duration */
                    int64_t held_ms = (esp_timer_get_time() - s_btn.press_start_us)
                                      / 1000;
                    s_btn.state        = BTN_IDLE;
                    s_btn.stable_count = 0;

                    if (held_ms >= LONG_PRESS_MS) {
                        ESP_LOGI(TAG, "Long press (%lld ms) → Reset", held_ms);
                        pomo_reset();
                    } else {
                        ESP_LOGI(TAG, "Short press (%lld ms) → Start/Pause", held_ms);
                        pomo_start_pause();
                    }
                }
            } else {
                s_btn.stable_count = 0;   /* still held — reset release counter */
            }
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

/* ── Public API ─────────────────────────────────────────────────────────────*/

esp_err_t input_init(void)
{
    /*
     * Learning note — pull-ups ───────────────────────────────────────────────
     *   Without a pull-up the pin floats when the button is open and can read
     *   as any random level. An internal pull-up ties the pin HIGH (≈ VCC).
     *   Pressing the button connects it to GND → reads LOW. This is called
     *   "active-low" wiring and is the standard on most dev boards.
     *   The ESP32-S3 has ~45 kΩ internal pull-ups, fine for pushbuttons.
     */
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BTN_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_LOGI(TAG, "Button configured on GPIO%d", BTN_GPIO);

    /*
     * Learning note — xTaskCreate ────────────────────────────────────────────
     *   pvTaskCode    : function to run
     *   pcName        : debug label (visible in 'tasks' monitor command)
     *   usStackDepth  : stack size in WORDS
     *   pvParameters  : passed to pvTaskCode as `arg`
     *   uxPriority    : 0 = lowest, configMAX_PRIORITIES-1 = highest
     *   pxCreatedTask : optional handle (NULL = don't care)
     *
     *   Priority tskIDLE_PRIORITY + 1: responsive to input without starving
     *   the display task (which runs at + 2).
     */
    xTaskCreate(input_task, "input", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);

    return ESP_OK;
}