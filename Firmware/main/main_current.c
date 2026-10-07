#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_system.h"

#include "sct013.h"
#include "network_manager.h"
#include "data_store.h"
#include "mqtt_telemetry.h"



#define WIFI_SSID "secret_lab"
#define WIFI_PASS "Osaxzp72"

static const char *TAG = "MAIN_APP";


#define DEADBAND_THRESHOLD_AMPS  0.5f
#define HEARTBEAT_TIMEOUT_SEC    300
#define SAMPLE_INTERVAL_MS       2000
#define FLUSH_INTERVAL_MS       (3600 * 1000) // Intervalo de 1 hora (3.600.000 ms)
#define MAX_PENDING_THRESHOLD   20            // Dispara flush antecipado se houver 20+ registros
#define BATTERY_MOCK_VOLTAGE    3.82f         // Substitua pela leitura analógica real da bateria

// Variáveis de controle de estado
static float g_last_recorded_current = -1.0f;
static time_t g_last_recorded_time = 0;

/**
 * @brief Avalia se a medição atual deve ser gravada/enviada com base na Deadband ou Heartbeat.
 */
static bool should_record_measurement(float current_rms, time_t current_time){
    // 1. Primeira medição após boot: sempre grava
    if (g_last_recorded_time == 0) {
        return true;
    }

    // 2. Checa a variação absoluta da corrente (Deadband)
    float delta_I = fabsf(current_rms - g_last_recorded_current);
    if (delta_I >= DEADBAND_THRESHOLD_AMPS) {
        ESP_LOGI("TRIGGER", "Gatilho Deadband acionado! Delta = %.2f A (Anterior: %.2fA | Atual: %.2fA)", 
                 delta_I, g_last_recorded_current, current_rms);
        return true;
    }

    // 3. Checa se estourou o tempo limite sem atualizações (Heartbeat)
    if ((current_time - g_last_recorded_time) >= HEARTBEAT_TIMEOUT_SEC) {
        ESP_LOGI("TRIGGER", "Gatilho Heartbeat acionado! Tempo decorrido: %lds", 
                 (long)(current_time - g_last_recorded_time));
        return true;
    }

    // Nenhuma condição de disparo foi atingida
    return false;
}

static void telemetry_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Task de telemetria inicializada com sucesso.");

    while (1) {
        // Aguarda 1 hora em estado bloqueado (permitindo Light Sleep)
        vTaskDelay(pdMS_TO_TICKS(FLUSH_INTERVAL_MS));

        size_t pending = data_store_get_pending_count();

        if (pending == 0) {
            ESP_LOGD(TAG, "Nenhum dado pendente para envio.");
            continue;
        }

        ESP_LOGI(TAG, "Iniciando ciclo de envio (%d registros pendentes)...", pending);

        // Verifica se a conexão MQTT está estabelecida
        if (!mqtt_telemetry_is_connected()) {
            ESP_LOGW(TAG, "MQTT desconectado. O lote permanecerá no SPIFFS até o próximo ciclo.");
            continue;
        }

        // Drena todo o buffer em lotes
        while (data_store_get_pending_count() > 0 && mqtt_telemetry_is_connected()) {
            esp_err_t err = mqtt_telemetry_flush_data_store(BATTERY_MOCK_VOLTAGE);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Falha na transmissão do lote. Pausando tentativas.");
                break;
            }
            
            // Pequeno delay entre lotes para não sobrecarregar o buffer de sockets do ESP8266
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

void app_main(void){

    vTaskDelay(pdMS_TO_TICKS(100));

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(data_store_init());

    network_wifi_init(WIFI_SSID, WIFI_PASS);
    network_mdns_init("esp8266_000", "ESP8266 Power Monitor");
    network_webserver_start();
    network_sntp_init();

    ESP_LOGI(TAG, "Rede configurada! Iniciando loop principal de medição...");

    sct013_config_t sensor_cfg = SCT013_CONFIG_DEFAULT();   
    sensor_cfg.grid_voltage_rms = 220.0f;
    sensor_cfg.power_factor = 0.95f;
    ESP_ERROR_CHECK(sct013_init(&sensor_cfg));

    esp_err_t err = esp_wifi_set_ps(WIFI_PS_MODEM);
    if (err == ESP_OK) {
        ESP_LOGI("POWER", "Wi-Fi Light Sleep ativado com sucesso!");
    } else {
        ESP_LOGE("POWER", "Falha ao ativar Light Sleep: %d", err);
    }

    mqtt_telemetry_init("mqtt://192.168.1.112:1883");

    xTaskCreate(
        telemetry_task,        // Função da Task
        "telemetry_task",      // Nome para identificação
        4096,                  // Stack depth em bytes (4KB)
        NULL,                  // Parâmetros de entrada
        5,                     // Prioridade da Task (moderada/baixa)
        NULL                   // Handle da Task
    );

    ESP_LOGI(TAG, "FUNCIONOU O OTA...");

    ESP_LOGI(TAG, "Iniciando loop de amostragem de corrente (Deadband/Heartbeat)...");

    sct013_metrics_t metrics;
    
    while (1) {
        // 1. Realiza a leitura da corrente RMS no sensor
        sct013_get_metrics(&metrics);

        // 2. Obtém o timestamp do RTC
        time_t now = 0;
        time(&now);

        // 3. Avalia se atinge os critérios de Deadband ou Heartbeat
        if (should_record_measurement(metrics.current_rms, now)) {

            measurement_record_t record = {
                .timestamp = (uint32_t)now,
                .current_rms = metrics.current_rms
            };

            // Salva no buffer SPIFFS local
            esp_err_t err = data_store_write_record(&record);
            if (err == ESP_OK) {
                // Atualiza as variáveis de controle do estado
                g_last_recorded_current = metrics.current_rms;
                g_last_recorded_time = now;

                // Formatação para exibição no LOGI
                int i_part = (int)record.current_rms;
                int d_part = (int)((record.current_rms - i_part) * 100);
                if (d_part < 0) d_part = -d_part;

                struct tm timeinfo;
                localtime_r(&now, &timeinfo);
                char strftime_buf[32];
                strftime(strftime_buf, sizeof(strftime_buf), "%H:%M:%S", &timeinfo);

                ESP_LOGI("MAIN", "[REGISTRO SALVO] Hora: %s | Corrente: %d.%02d A | Pendentes: %d", 
                        strftime_buf, i_part, d_part, data_store_get_pending_count());
            } else {
                ESP_LOGE("MAIN", "Falha ao gravar medição localmente!");
            }
        } else {
            // Log apenas para acompanhamento de amostragem ignorada
            ESP_LOGD("MAIN", "Medição filtrada (sem alteração significativa).");
        }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }
}