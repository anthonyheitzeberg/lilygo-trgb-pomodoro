#pragma once

/**
 * @file input.h
 * @brief Debounced GPIO button input handler.
 *
 * The LILYGO T-RGB board has two physical buttons:
 *   - BTN_START_PAUSE  → calls pomo_start_pause()
 *   - BTN_RESET        → calls pomo_reset()
 *
 * Debouncing strategy: software debounce via a FreeRTOS task that samples
 * GPIO every 10 ms. When a button transitions from high→low (active-low
 * wiring), a 50 ms stable period is required before the press is registered.
 *
 * Learning note — hardware debounce vs software debounce ─────────────────────
 *   Mechanical buttons "bounce" — the contact opens and closes rapidly for
 *   ~5–50 ms after each press. Without debouncing, one physical press looks
 *   like 5–20 presses to the MCU.
 *
 *   Hardware debounce: RC filter + Schmitt trigger — zero CPU overhead.
 *   Software debounce: sample the pin, require N consecutive same readings.
 *   We use software here because the LILYGO board has no RC filter on the
 *   button lines and adding one would require hardware modification.
 * ────────────────────────────────────────────────────────────────────────────
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** GPIO numbers for the two buttons on the LILYGO T-RGB board.
 *  Adjust if your board revision differs. */
#define BTN_START_PAUSE_GPIO  0   ///< Boot button doubles as Start/Pause
#define BTN_RESET_GPIO        14  ///< Side button = Reset

/**
 * @brief Initialise the input subsystem and start the polling task.
 *
 * Must be called after pomo_init() because button callbacks call pomo_ APIs.
 */
esp_err_t input_init(void);

#ifdef __cplusplus
}
#endif