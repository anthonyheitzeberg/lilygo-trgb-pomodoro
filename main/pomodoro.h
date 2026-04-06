#pragma once

/**
 * @file pomodoro.h
 * @brief Pomodoro timer state machine.
 *
 * This module is pure logic — no hardware, no display. It owns the FSM,
 * the countdown, and the session counter. Everything else reads from it.
 *
 * State diagram:
 *
 *   IDLE ──[start]──► WORK_RUNNING ──[pause]──► WORK_PAUSED
 *                          │                         │
 *                       [expire]                 [start]
 *                          │                         │
 *                          ▼                         ▼
 *                    BREAK_RUNNING ◄────────── WORK_RUNNING
 *                          │
 *                       [expire]
 *                          │
 *                          ▼
 *                        IDLE   (session count++)
 *
 *   Any state ──[reset]──► IDLE
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuration ──────────────────────────────────────────────────────────*/

#define POMODORO_WORK_SECS   (25 * 60)  ///< 25-minute work interval
#define POMODORO_BREAK_SECS  ( 5 * 60)  ///< 5-minute break interval

/* ── Types ──────────────────────────────────────────────────────────────────*/

/** Every possible state of the Pomodoro FSM. */
typedef enum {
    POMO_STATE_IDLE,          ///< Not started / just reset
    POMO_STATE_WORK_RUNNING,  ///< Work timer counting down
    POMO_STATE_WORK_PAUSED,   ///< Work timer paused mid-session
    POMO_STATE_BREAK_RUNNING, ///< Break timer counting down
} pomo_state_t;

/** Full snapshot of the Pomodoro's current state (read by display layer). */
typedef struct {
    pomo_state_t state;
    uint32_t     work_remaining_secs;   ///< Seconds left in current work slot
    uint32_t     break_remaining_secs;  ///< Seconds left in current break slot
    uint32_t     sessions_completed;    ///< How many full work+break cycles done
} pomo_status_t;

/* ── API ────────────────────────────────────────────────────────────────────*/

/**
 * @brief Initialise the Pomodoro module.
 *
 * Creates the internal esp_timer that fires every second and advances
 * the countdown. Call once from app_main before any other pomo_ function.
 */
void pomo_init(void);

/**
 * @brief Press the Start/Pause button.
 *
 * - IDLE          → WORK_RUNNING  (start)
 * - WORK_RUNNING  → WORK_PAUSED   (pause)
 * - WORK_PAUSED   → WORK_RUNNING  (resume)
 * - BREAK_RUNNING → no-op         (can't pause break in this design)
 */
void pomo_start_pause(void);

/**
 * @brief Press the Reset button — returns unconditionally to IDLE.
 */
void pomo_reset(void);

/**
 * @brief Read a consistent snapshot of the current state.
 *
 * Thread-safe: takes a critical section internally so the display task
 * always sees an atomic view.
 *
 * @param[out] out  Filled with current state.
 */
void pomo_get_status(pomo_status_t *out);

#ifdef __cplusplus
}
#endif