#ifndef MQTT_TELEMETRY_H
#define MQTT_TELEMETRY_H

#include <stdint.h>
#include "esp_err.h"

// Configurações Padrão
#define MQTT_BROKER_URI     "mqtt://192.168.1.100:1883"  // SUBSTITUA pelo IP do seu Ubuntu
#define MQTT_TOPIC_METRICS  "energy/esp8266_000/metrics"
#define MAX_BATCH_RECORDS   24                            // Máximo de itens por payload JSON

/**
 * @brief Inicializa o cliente MQTT (no ESP8266 RTOS SDK utiliza mqtt_client do ESP-IDF)
 */
esp_err_t mqtt_telemetry_init(const char *broker_uri);

/**
 * @brief Verifica se o cliente MQTT está conectado ao broker.
 */
bool mqtt_telemetry_is_connected(void);

/**
 * @brief Drena até MAX_BATCH_RECORDS do data_store, monta o payload JSON e envia via MQTT.
 * 
 * @param battery_v Tensão atual da bateria (ex: 3.82)
 * @return esp_err_t ESP_OK se o lote foi enviado com sucesso, ESP_FAIL ou ESP_ERR_NOT_FOUND se não houver registros.
 */
esp_err_t mqtt_telemetry_flush_data_store(float battery_v);

#endif // MQTT_TELEMETRY_H