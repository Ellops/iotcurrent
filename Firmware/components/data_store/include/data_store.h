#ifndef DATA_STORE_H
#define DATA_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Estrutura do registro de medição que será salvo em Flash
typedef struct {
    uint32_t timestamp;  // Epoch timestamp (ou contador sequencial)
    float current_rms;   // Valor da corrente RMS em Amperes
} measurement_record_t;

/**
 * @brief Inicializa a partição SPIFFS e monta o sistema de arquivos.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t data_store_init(void);

/**
 * @brief Salva um registro de medição no final do arquivo de buffer offline.
 * 
 * @param record Ponteiro para a estrutura com os dados de medição.
 * @return ESP_OK se o dado foi gravado com sucesso.
 */
esp_err_t data_store_write_record(const measurement_record_t *record);

/**
 * @brief Lê e remove o registro mais antigo do arquivo (FIFO / Ring Buffer).
 * 
 * @param record Ponteiro onde o dado lido será copiado.
 * @return ESP_OK se um registro foi lido com sucesso, ESP_ERR_NOT_FOUND se o buffer estiver vazio.
 */
esp_err_t data_store_pop_oldest_record(measurement_record_t *record);

/**
 * @brief Retorna a quantidade de registros pendentes no buffer local.
 * 
 * @return Número de registros salvos não enviados.
 */
size_t data_store_get_pending_count(void);

/**
 * @brief Limpa/Apaga todo o buffer de medições salvas.
 */
esp_err_t data_store_clear_all(void);

#endif // DATA_STORE_H