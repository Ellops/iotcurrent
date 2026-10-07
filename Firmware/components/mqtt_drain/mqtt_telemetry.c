#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"

#include "data_store.h"
#include "mqtt_telemetry.h"

static const char *TAG = "MQTT_TELEMETRY";

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_is_connected = false;

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Conectado ao Broker MQTT com sucesso!");
            s_is_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Desconectado do Broker MQTT.");
            s_is_connected = false;
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Erro de evento no cliente MQTT.");
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t mqtt_telemetry_init(const char *broker_uri)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = broker_uri ? broker_uri : MQTT_BROKER_URI,
        .event_handle = mqtt_event_handler,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Falha ao inicializar o cliente MQTT");
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_start(s_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar o loop do cliente MQTT (%d)", err);
        return err;
    }

    return ESP_OK;
}

bool mqtt_telemetry_is_connected(void)
{
    return s_is_connected;
}

esp_err_t mqtt_telemetry_flush_data_store(float battery_v)
{
    if (!s_is_connected) {
        ESP_LOGW(TAG, "Tentativa de envio cancelada: MQTT desconectado.");
        return ESP_FAIL;
    }

    size_t pending_count = data_store_get_pending_count();
    if (pending_count == 0) {
        ESP_LOGD(TAG, "Nenhum registro pendente para envio.");
        return ESP_ERR_NOT_FOUND;
    }

    // Limita o tamanho do lote para evitar estourar o heap na alocação do cJSON
    size_t items_to_send = (pending_count > MAX_BATCH_RECORDS) ? MAX_BATCH_RECORDS : pending_count;

    measurement_record_t records[MAX_BATCH_RECORDS];
    size_t actual_popped = 0;

    // Remove do SPIFFS os itens mais antigos
    for (size_t i = 0; i < items_to_send; i++) {
        if (data_store_pop_oldest_record(&records[i]) == ESP_OK) {
            actual_popped++;
        } else {
            break;
        }
    }

    if (actual_popped == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    // Construção da estrutura JSON
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Falha ao criar objeto raiz do cJSON");
        goto rollback;
    }

    cJSON_AddStringToObject(root, "device_id", "esp8266_000");
    cJSON_AddNumberToObject(root, "battery_v", battery_v);
    cJSON_AddNumberToObject(root, "count", actual_popped);

    cJSON *data_array = cJSON_CreateArray();
    if (!data_array) {
        cJSON_Delete(root);
        goto rollback;
    }

    for (size_t i = 0; i < actual_popped; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "ts", (double)records[i].timestamp);
        cJSON_AddNumberToObject(item, "current", records[i].current_rms);
        cJSON_AddItemToArray(data_array, item);
    }

    cJSON_AddItemToObject(root, "data", data_array);

    char *json_rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_rendered) {
        ESP_LOGE(TAG, "Falha ao renderizar string JSON");
        goto rollback;
    }

    ESP_LOGI(TAG, "Enviando lote MQTT (%d registros)...", actual_popped);

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_METRICS, json_rendered, 0, 1, 0);
    free(json_rendered);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Falha ao publicar mensagem via MQTT.");
        goto rollback;
    }

    ESP_LOGI(TAG, "Lote enviado com sucesso (MQTT msg_id: %d). Pendentes restantes: %d", 
             msg_id, data_store_get_pending_count());

    return ESP_OK;

rollback:
    // Se houve erro na montagem ou na transmissão, regrava os dados no SPIFFS para não perdê-los
    ESP_LOGW(TAG, "Restaurando %d registros no buffer SPIFFS devido a falha...", actual_popped);
    for (size_t i = 0; i < actual_popped; i++) {
        data_store_write_record(&records[i]);
    }
    return ESP_FAIL;
}