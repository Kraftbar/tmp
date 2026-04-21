#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "status.h"
#include "wifi.h"

static const char *TAG = "main";

void app_main(void)
{
    board_init();
    status_init();

    ESP_LOGI(TAG, "project start");
    ESP_LOGI(TAG, "status LED on GPIO %d", (int) board_status_led_gpio());

    wifi_init();

    while (1) {
        status_poll();
        wifi_poll();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
