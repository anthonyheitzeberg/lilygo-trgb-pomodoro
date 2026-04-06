/**
 * @file pomodoro.c
 * @brief Pomodoro FSM + countdown using ESP-IDF's esp_timer.
 *
 * Learning note — esp_timer vs FreeRTOS timers ────────────────────────────
 *
 *   ESP-IDF offers two timer APIs:
 *
 *   1. FreeRTOS timers (xTimerCreate)
 *      - Run in the FreeRTOS timer service task
 *      - Resolution = 1 tick (1 ms with CONFIG_FREERTOS_HZ=1000)
 *      - Callbacks share a single task → one slow callback blocks others
 *
 *   2. esp_timer (this file)
 *      - Backed by the hardware "high-resolution timer" peripheral
 *      - Resolution = 1 µs (much finer)
 *      - Callbacks run in a dedicated high-priority ISR-like context
 *      - Preferred for anything that must not drift
 *
 *   For a Pomodoro we only need 1-second resolution, but esp_timer is
 *   still the right choice because it won't accumulate drift over 25 min.
 * ────────────────────────────────────────────────────────────────────────────
 */

#include "pomodoro.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"  /* for spinlock / critical section */

static const char *TAG = "pomodoro";

/* ── Module state ───────────────────────────────────────────────────────────
 *
 * All mutable state lives here. It is guarded by a spinlock so the display
 * task (which calls pomo_get_status from a different core) always sees a
 * consistent snapshot.
 *
 * Learning note — portMUX_TYPE ───────────────────────────────────────────────
 *   On the dual-core ESP32-S3, a plain `bool` flag is NOT safe to share
 *   between cores. portENTER_CRITICAL / portEXIT_CRITICAL use a spinlock that
 *   works across both cores. Always guard shared mutable state this way.
 * ────────────────────────────────────────────────────────────────────────────
 */
static portMUX_TYPE  s_mux    = portMUX_INITIALIZER_UNLOCKED;
static pomo_state_t  s_state  = POMO_STATE_IDLE;
static uint32_t      s_work   = POMODORO_WORK_SECS;
static uint32_t      s_brk    = POMODORO_BREAK_SECS;
static uint32_t      s_sessions = 0;

static esp_timer_handle_t s_tick_timer = NULL;

/* ── Timer callback (fires every 1 second) ───────────────────────────────── */

static void tick_cb(void *arg)
{
    portENTER_CRITICAL(&s_mux);

    switch (s_state) {

    case POMO_STATE_WORK_RUNNING:
        if (s_work > 0) {
            s_work--;
        }
        if (s_work == 0) {
            /* Work session expired — start break automatically */
            s_state = POMO_STATE_BREAK_RUNNING;
            s_brk   = POMODORO_BREAK_SECS;
            ESP_EARLY_LOGI(TAG, "Work done! Break started.");
        }
        break;

    case POMO_STATE_BREAK_RUNNING:
        if (s_brk > 0) {
            s_brk--;
        }
        if (s_brk == 0) {
            /* Break expired — return to IDLE, increment session counter */
            s_sessions++;
            s_state = POMO_STATE_IDLE;
            s_work  = POMODORO_WORK_SECS;
            s_brk   = POMODORO_BREAK_SECS;
            ESP_EARLY_LOGI(TAG, "Break done! Sessions: %lu", s_sessions);
        }
        break;

    default:
        /* IDLE and WORK_PAUSED do not tick */
        break;
    }

    portEXIT_CRITICAL(&s_mux);
}

/* ── Public API ─────────────────────────────────────────────────────────────*/

void pomo_init(void)
{
    /*
     * Learning note — esp_timer_create ──────────────────────────────────────
     *   We use a *periodic* timer (ESP_TIMER_TASK dispatch) that fires every
     *   1,000,000 µs (1 second). Periodic timers auto-reload; we never need
     *   to restart them manually.
     *
     *   ESP_TIMER_TASK means the callback runs in the esp_timer task context
     *   (not an ISR), so it's safe to call ESP_EARLY_LOGI and other non-ISR
     *   functions. If you need sub-microsecond latency you'd use
     *   ESP_TIMER_ISR instead, but that imposes stricter restrictions.
     * ────────────────────────────────────────────────────────────────────────
     */
    esp_timer_create_args_t args = {
        .callback        = tick_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "pomo_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_tick_timer));

    /* Start the timer immediately. It will fire every second.
     * tick_cb only acts when the state is RUNNING, so it is harmless
     * while in IDLE. */
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_tick_timer, 1000000ULL));

    ESP_LOGI(TAG, "Pomodoro timer initialised. Work=%d s  Break=%d s",
             POMODORO_WORK_SECS, POMODORO_BREAK_SECS);
}

void pomo_start_pause(void)
{
    portENTER_CRITICAL(&s_mux);

    switch (s_state) {
    case POMO_STATE_IDLE:
        s_state = POMO_STATE_WORK_RUNNING;
        ESP_EARLY_LOGI(TAG, "Started.");
        break;

    case POMO_STATE_WORK_RUNNING:
        s_state = POMO_STATE_WORK_PAUSED;
        ESP_EARLY_LOGI(TAG, "Paused.");
        break;

    case POMO_STATE_WORK_PAUSED:
        s_state = POMO_STATE_WORK_RUNNING;
        ESP_EARLY_LOGI(TAG, "Resumed.");
        break;

    case POMO_STATE_BREAK_RUNNING:
        /* Start/Pause does nothing during a break — break can't be skipped */
        break;
    }

    portEXIT_CRITICAL(&s_mux);
}

void pomo_reset(void)
{
    portENTER_CRITICAL(&s_mux);
    s_state = POMO_STATE_IDLE;
    s_work  = POMODORO_WORK_SECS;
    s_brk   = POMODORO_BREAK_SECS;
    /* Note: we intentionally do NOT reset s_sessions — the counter persists */
    portEXIT_CRITICAL(&s_mux);

    ESP_LOGI(TAG, "Reset.");
}

void pomo_get_status(pomo_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    out->state                = s_state;
    out->work_remaining_secs  = s_work;
    out->break_remaining_secs = s_brk;
    out->sessions_completed   = s_sessions;
    portEXIT_CRITICAL(&s_mux);
}