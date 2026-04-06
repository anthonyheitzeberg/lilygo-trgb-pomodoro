#pragma once

/**
 * @file input.h
 * @brief Single-button input handler (short press / long press).
 *
 * The LILYGO T-RGB board effectively exposes one usable user button:
 * GPIO 0 (BOOT). Other candidate GPIOs are consumed by the RGB display bus.
 *
 * Action mapping:
 *   - Short press (< LONG_PRESS_MS)  → pomo_start_pause()
 *   - Long press  (>= LONG_PRESS_MS) → pomo_reset()
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

/** The LILYGO T-RGB has one physical button: the BOOT button (GPIO 0).
 *  Every other GPIO is consumed by the RGB data bus or the I2C expander.
 *
 *  We map both actions to the single button using press duration:
 *    Short press  (< LONG_PRESS_MS)  → Start / Pause
 *    Long press   (≥ LONG_PRESS_MS)  → Reset
 */
#define BTN_GPIO          0     ///< GPIO 0 — the physical BOOT button
#define LONG_PRESS_MS   800     ///< Hold ≥ 800 ms to trigger reset

/**
 * @brief Initialise the input subsystem and start the polling task.
 *
 * Must be called after pomo_init() because button callbacks call pomo_ APIs.
 */
esp_err_t input_init(void);

#ifdef __cplusplus
}
#endif