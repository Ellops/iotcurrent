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

    size_t items_to_send = (pending_count > MAX_BATCH_RECORDS) ? MAX_BATCH_RECORDS : pending_count;

    measurement_record_t records[MAX_BATCH_RECORDS];
    size_t actual_popped = 0;

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

    // 1. Sanitize & Converte Bateria para Partes Inteira e Decimal (Evita promover float em varargs)
    if (battery_v < 0.0f || battery_v > 15.0f || battery_v != battery_v) { 
        battery_v = 0.0f; // Proteção contra NaN / valores absurdos
    }
    int bat_int = (int)battery_v;
    int bat_dec = (int)((battery_v - bat_int) * 100.0f);
    if (bat_dec < 0) bat_dec = -bat_dec;

    // 2. Buffer local seguro na stack
    char json_rendered[1024];
    size_t buf_size = sizeof(json_rendered);
    int offset = 0;

    // 3. Cabeçalho formatado com inteiros (%d.%02d) para não desalinhar a pilha Xtensa
    int written = snprintf(json_rendered, buf_size,
                           "{\"device_id\":\"esp8266_000\",\"battery_v\":%d.%02d,\"count\":%u,\"data\":[",
                           bat_int, bat_dec, (unsigned int)actual_popped);

    if (written < 0 || (size_t)written >= buf_size) {
        ESP_LOGE(TAG, "Buffer estourado na escrita do cabeçalho JSON (written=%d)", written);
        goto rollback;
    }
    offset += written;

    // 4. Array de registros formatando Corrente em Inteiros
    for (size_t i = 0; i < actual_popped; i++) {
        float cur = records[i].current_rms;
        if (cur < 0.0f || cur != cur) cur = 0.0f; // Proteção contra NaN
        
        int cur_int = (int)cur;
        int cur_dec = (int)((cur - cur_int) * 100.0f);
        if (cur_dec < 0) cur_dec = -cur_dec;

        written = snprintf(json_rendered + offset, buf_size - (size_t)offset,
                           "%s{\"ts\":%lu,\"current\":%d.%02d}",
                           (i > 0) ? "," : "",
                           (unsigned long)records[i].timestamp,
                           cur_int, cur_dec);

        if (written < 0 || (size_t)(offset + written) >= buf_size) {
            ESP_LOGE(TAG, "Buffer estourado ao adicionar item %u (written=%d)", (unsigned int)i, written);
            goto rollback;
        }
        offset += written;
    }

    // 5. Fechamento da estrutura JSON
    written = snprintf(json_rendered + offset, buf_size - (size_t)offset, "]}");
    if (written < 0 || (size_t)(offset + written) >= buf_size) {
        ESP_LOGE(TAG, "Buffer estourado ao fechar JSON (written=%d)", written);
        goto rollback;
    }

    ESP_LOGI(TAG, "Enviando lote MQTT (%u registros)...", (unsigned int)actual_popped);

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_METRICS, json_rendered, 0, 1, 0);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Falha ao publicar mensagem via MQTT.");
        goto rollback;
    }

    ESP_LOGI(TAG, "Lote enviado com sucesso (MQTT msg_id: %d). Pendentes restantes: %u", 
             msg_id, (unsigned int)data_store_get_pending_count());

    return ESP_OK;

rollback:
    ESP_LOGW(TAG, "Restaurando %u registros no buffer SPIFFS devido a falha...", (unsigned int)actual_popped);
    for (size_t i = 0; i < actual_popped; i++) {
        data_store_write_record(&records[i]);
    }
    return ESP_FAIL;
}