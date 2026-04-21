#include "wifi.h"

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "status.h"
#include "wifi_secrets.h"

#define WIFI_MAXIMUM_RETRY 10

static const char *TAG = "wifi";

static int retry_count;
static bool wifi_up;
static TickType_t next_log_tick;

static void wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void) arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "starting station mode");
        status_set_mode(STATUS_BOOTING);
        ESP_ERROR_CHECK(esp_wifi_connect());
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_up = false;

        if (retry_count < WIFI_MAXIMUM_RETRY) {
            retry_count++;
            status_set_mode(STATUS_BOOTING);
            ESP_LOGW(TAG, "disconnected, retry %d/%d", retry_count, WIFI_MAXIMUM_RETRY);
            ESP_ERROR_CHECK(esp_wifi_connect());
        } else {
            status_set_mode(STATUS_ERROR);
            ESP_LOGE(TAG, "could not connect to %s", WIFI_SSID);
        }

        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;

        retry_count = 0;
        wifi_up = true;
        next_log_tick = xTaskGetTickCount() + pdMS_TO_TICKS(30000);

        status_set_mode(STATUS_RUNNING);
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        return;
    }
}

static void wifi_storage_init(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);
}

void wifi_init(void)
{
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };

    wifi_storage_init();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    retry_count = 0;
    wifi_up = false;
    next_log_tick = 0;

    ESP_LOGI(TAG, "connecting to %s", WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_poll(void)
{
    TickType_t now = xTaskGetTickCount();

    if (!wifi_up || now < next_log_tick) {
        return;
    }

    ESP_LOGI(TAG, "link up");
    next_log_tick = now + pdMS_TO_TICKS(30000);
}
