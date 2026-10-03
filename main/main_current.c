#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "sct013.h"
#include "network_manager.h"

#define WIFI_SSID "secret_lab"
#define WIFI_PASS "Osaxzp72"

static const char *TAG = "MAIN_APP";

void app_main(void)
{
    // 1. Inicializa NVS (Exigido pelo Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Inicializa Conectividade via Componente
    network_wifi_init(WIFI_SSID, WIFI_PASS);
    network_mdns_init("esp8266_000", "ESP8266 Power Monitor");
    network_webserver_start();

    ESP_LOGI(TAG, "Rede configurada! Iniciando loop principal de medição...");

    sct013_config_t sensor_cfg = SCT013_CONFIG_DEFAULT();
    sensor_cfg.grid_voltage_rms = 220.0f;
    sensor_cfg.power_factor = 0.95f;
    ESP_ERROR_CHECK(sct013_init(&sensor_cfg));

    sct013_metrics_t metrics;

    while (1) {
        sct013_get_metrics(&metrics);

        int i_p = (int)metrics.current_rms;
        int i_f = (int)((metrics.current_rms - i_p) * 100);

        int p_act = (int)metrics.power_active;
        int kwh_p = (int)metrics.energy_kwh;
        int kwh_f = (int)((metrics.energy_kwh - kwh_p) * 1000);

        ESP_LOGI(TAG, "[Medição] Corrente: %d.%02d A | Potência Ativa: %d W | Consumo: %d.%03d kWh",
                 i_p, (i_f < 0 ? -i_f : i_f),
                 p_act,
                 kwh_p, (kwh_f < 0 ? -kwh_f : kwh_f));

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}