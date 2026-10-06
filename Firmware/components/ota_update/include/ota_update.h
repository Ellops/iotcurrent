#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include "esp_err.h"

/**
 * @brief Inicia o processo de atualização OTA de forma assíncrona em uma Task FreeRTOS.
 * 
 * @param url String com a URL completa do arquivo .bin (ex: "http://192.168.1.100:8070/firmware.bin")
 * @return esp_err_t ESP_OK se a task foi disparada com sucesso, ESP_ERR_NO_MEM se falhar ao criar task.
 */
esp_err_t ota_update_start(const char *url);

/**
 * @brief Retorna se há um processo de OTA em andamento.
 */
bool ota_update_is_running(void);

#endif // OTA_UPDATE_H