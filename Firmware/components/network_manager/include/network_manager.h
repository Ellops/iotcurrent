#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "esp_err.h"

/**
 * @brief Inicializa o Wi-Fi em modo STA e aguarda a conexão (bloqueante).
 */
void network_wifi_init(const char *ssid, const char *password);

/**
 * @brief Inicializa o serviço mDNS com o hostname e nome de instância informados.
 */
esp_err_t network_mdns_init(const char *hostname, const char *instance_name);

/**
 * @brief Inicializa o servidor HTTP com o endpoint /update para OTA.
 */
esp_err_t network_webserver_start(void);

/**
 * @brief Inicializa o sntp.
 */
void network_sntp_init(void);  

#endif // NETWORK_MANAGER_H