#include "status.h"

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "board.h"

static const char *TAG = "status";

static enum status_mode current_mode = STATUS_BOOTING;
static TickType_t next_toggle_tick;
static bool led_on;

static const char *status_name(enum status_mode mode)
{
    switch (mode) {
    case STATUS_BOOTING:
        return "booting";
    case STATUS_RUNNING:
        return "running";
    case STATUS_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

static TickType_t status_interval(enum status_mode mode)
{
    switch (mode) {
    case STATUS_BOOTING:
        return pdMS_TO_TICKS(150);
    case STATUS_RUNNING:
        return pdMS_TO_TICKS(1000);
    case STATUS_ERROR:
        return pdMS_TO_TICKS(100);
    default:
        return pdMS_TO_TICKS(500);
    }
}

void status_init(void)
{
    current_mode = STATUS_BOOTING;
    next_toggle_tick = xTaskGetTickCount();
    led_on = false;

    board_status_led_set(false);
    ESP_LOGI(TAG, "status %s", status_name(current_mode));
}

void status_set_mode(enum status_mode mode)
{
    if (current_mode == mode) {
        return;
    }

    current_mode = mode;
    next_toggle_tick = xTaskGetTickCount();
    led_on = false;

    board_status_led_set(false);
    ESP_LOGI(TAG, "status %s", status_name(current_mode));
}

void status_poll(void)
{
    TickType_t now = xTaskGetTickCount();

    if (now < next_toggle_tick) {
        return;
    }

    led_on = !led_on;
    board_status_led_set(led_on);
    next_toggle_tick = now + status_interval(current_mode);
}
