#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include <time.h>
#include <sys/time.h>
#include "lwip/apps/sntp.h"

#include "sct013.h"
#include "network_manager.h"
#include "data_store.h"

#define WIFI_SSID "secret_lab"
#define WIFI_PASS "Osaxzp72"

static const char *TAG = "MAIN_APP";

static void initialize_sntp(void)
{
    ESP_LOGI("RTC", "Inicializando SNTP...");
    
    // Define o modo como POLL
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    // Configura os servidores NTP
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.google.com");
    
    // Inicializa o serviço SNTP
    sntp_init();

    // Configura o fuso horário (Exemplo: Brasil UTC-3)
    setenv("TZ", "BRT3BRST,M10.3.0/0,M2.3.0/0", 1);
    tzset();
}


void app_main(void)
{
    // 1. Inicializa NVS (Exigido pelo Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    data_store_init();

    network_wifi_init(WIFI_SSID, WIFI_PASS);
    network_mdns_init("esp8266_000", "ESP8266 Power Monitor");
    network_webserver_start();

    initialize_sntp();
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Rede configurada! Iniciando loop principal de medição...");

    sct013_config_t sensor_cfg = SCT013_CONFIG_DEFAULT();
    sensor_cfg.grid_voltage_rms = 220.0f;
    sensor_cfg.power_factor = 0.95f;
    ESP_ERROR_CHECK(sct013_init(&sensor_cfg));

    sct013_metrics_t metrics;

    while (1) {
            // // Leitura do sensor
            // sct013_get_metrics(&metrics);

            // // Obtém o timestamp atual do RTC do ESP
            // time_t now = 0;
            // time(&now);

            // // Monta o registro com timestamp e corrente RMS
            // measurement_record_t record = {
            //     .timestamp = (uint32_t)now,
            //     .current_rms = metrics.current_rms
            // };

            // // Salva o dado no buffer SPIFFS local
            // esp_err_t err = data_store_write_record(&record);
            // if (err == ESP_OK) {
            //     ESP_LOGI("MAIN", "Medição salva com sucesso!");
            // } else {
            //     ESP_LOGE("MAIN", "Falha ao salvar medição localmente");
            // }

            // // --- SEÇÃO DE CHECAGEM DOS DADOS SALVOS NO LOG ---
            // size_t total_pending = data_store_get_pending_count();
            // ESP_LOGI("CHECK", "========================================");
            // ESP_LOGI("CHECK", "Total de registros pendentes na Flash: %d", total_pending);

            // // Formata a data atual em string legível
            // struct tm timeinfo;
            // localtime_r(&now, &timeinfo);
            // char strftime_buf[64];
            // strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);

            // ESP_LOGI("CHECK", "Última Leitura -> Data/Hora: %s | Epoch: %u | Corrente: %.2f A", 
            //         strftime_buf, 
            //         record.timestamp, 
            //         record.current_rms);
            // ESP_LOGI("CHECK", "========================================\n");

            vTaskDelay(pdMS_TO_TICKS(3000));
        }
}


        // int i_f = (int)((metrics.current_rms - i_p) * 100);

        // int p_act = (int)metrics.power_active;
        // int kwh_p = (int)metrics.energy_kwh;
        // int kwh_f = (int)((metrics.energy_kwh - kwh_p) * 1000);

        // ESP_LOGI(TAG, "[Medição] Corrente: %d.%02d A | Potência Ativa: %d W | Consumo: %d.%03d kWh",
        //          i_p, (i_f < 0 ? -i_f : i_f),
        //          p_act,
        //          kwh_p, (kwh_f < 0 ? -kwh_f : kwh_f));