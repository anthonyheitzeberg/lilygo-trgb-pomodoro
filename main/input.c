/**
 * @file input.c
 * @brief Debounced button polling using a FreeRTOS task.
 */

#include "input.h"
#include "pomodoro.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "input";

/* Debounce tuning */
#define SAMPLE_PERIOD_MS  10   /* poll interval */
#define DEBOUNCE_MS       50   /* stable-low required before registering press */
#define DEBOUNCE_TICKS    (DEBOUNCE_MS / SAMPLE_PERIOD_MS)  /* = 5 samples */

/* ── Per-button state machine ───────────────────────────────────────────────
 *
 * Learning note — debounce state machine ────────────────────────────────────
 *
 *   Each button independently tracks:
 *     stable_count : consecutive samples that agree with current reading
 *     pressed      : true while the button is held (prevents repeat fires)
 *
 *   Transitions:
 *     RELEASED, sees LOW → stable_count++
 *       if stable_count >= DEBOUNCE_TICKS → fire action, set pressed=true
 *     PRESSED,  sees HIGH → stable_count++
 *       if stable_count >= DEBOUNCE_TICKS → clear pressed
 *     Any state, sees same level → reset stable_count (or keep incrementing)
 *
 *   This is called "integrator debounce" — simple and reliable.
 * ────────────────────────────────────────────────────────────────────────────
 */
typedef struct {
    int     gpio;
    void  (*on_press)(void);   /* callback when a clean press is detected */
    int     stable_count;
    bool    pressed;
} button_t;

static void on_start_pause(void) { pomo_start_pause(); }
static void on_reset(void)       { pomo_reset(); }

static button_t s_buttons[] = {
    { .gpio = BTN_START_PAUSE_GPIO, .on_press = on_start_pause },
    { .gpio = BTN_RESET_GPIO,       .on_press = on_reset       },
};
#define NUM_BUTTONS (sizeof(s_buttons) / sizeof(s_buttons[0]))

/* ── Polling task ────────────────────────────────────────────────────────────*/

static void input_task(void *arg)
{
    ESP_LOGI(TAG, "Input polling task started");

    while (1) {
        for (size_t i = 0; i < NUM_BUTTONS; i++) {
            button_t *btn = &s_buttons[i];
            int level = gpio_get_level(btn->gpio);  /* 0 = LOW = pressed */

            if (level == 0) {
                /* Button appears pressed — accumulate stable count */
                btn->stable_count++;
                if (btn->stable_count >= DEBOUNCE_TICKS && !btn->pressed) {
                    btn->pressed = true;
                    btn->stable_count = 0;
                    ESP_LOGI(TAG, "Button GPIO%d pressed", btn->gpio);
                    btn->on_press();
                }
            } else {
                /* Button appears released */
                if (btn->pressed) {
                    btn->stable_count++;
                    if (btn->stable_count >= DEBOUNCE_TICKS) {
                        btn->pressed = false;
                        btn->stable_count = 0;
                    }
                } else {
                    btn->stable_count = 0;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

/* ── Public API ─────────────────────────────────────────────────────────────*/

esp_err_t input_init(void)
{
    /* Configure all button GPIOs as inputs with internal pull-ups.
     *
     * Learning note — pull-ups ───────────────────────────────────────────────
     *   When a button is open (not pressed), the pin is floating — it could
     *   read as either 0 or 1 unpredictably. A pull-up resistor ties the pin
     *   to VCC, so it reads HIGH when open. When pressed, the button connects
     *   the pin to GND → reads LOW. This is called "active-low" wiring.
     *   The ESP32-S3 has ~45 kΩ internal pull-ups — sufficient for buttons.
     * ────────────────────────────────────────────────────────────────────────
     */
    for (size_t i = 0; i < NUM_BUTTONS; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << s_buttons[i].gpio),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,  /* we poll, not interrupt */
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
        ESP_LOGI(TAG, "Button GPIO%d configured", s_buttons[i].gpio);
    }

    /*
     * Learning note — xTaskCreate ────────────────────────────────────────────
     *   Parameters:
     *     pvTaskCode    : function to run as the task
     *     pcName        : debug label (shown in 'tasks' monitor command)
     *     usStackDepth  : stack size in WORDS (not bytes) on most ports
     *     pvParameters  : argument passed to pvTaskCode
     *     uxPriority    : 0 = lowest, configMAX_PRIORITIES-1 = highest
     *     pxCreatedTask : optional handle, NULL if you don't need it
     *
     *   Priority choice: tskIDLE_PRIORITY + 1 keeps input responsive without
     *   starving the display task which runs at priority + 2.
     * ────────────────────────────────────────────────────────────────────────
     */
    xTaskCreate(
        input_task,            /* task function  */
        "input",               /* name           */
        2048,                  /* stack (words)  */
        NULL,                  /* arg            */
        tskIDLE_PRIORITY + 1,  /* priority       */
        NULL                   /* handle out     */
    );

    return ESP_OK;
}