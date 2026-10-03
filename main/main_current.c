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

// #include <stdio.h>
// #include <string.h>
// #include "esp_log.h"
// #include "tcpip_adapter.h"
// #include "esp_wifi.h"
// #include "esp_event.h"
// #include "esp_system.h"
// #include "esp_ota_ops.h"
// #include "esp_http_client.h"
// #include <esp_http_server.h>
// #include "ota_update.h"
// #include "esp_https_ota.h"
// #include "nvs_flash.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "freertos/event_groups.h"
// #include "lwip/opt.h"
// #include "lwip/ip_addr.h"
// #include "lwip/ip6_addr.h"
// #include "lwip/api.h"
// #include "sct013.h"
// #include "mdns.h"

// #define WIFI_SSID      "secret_lab"
// #define WIFI_PASS      "Osaxzp72"
// #define OTA_URL        "http://192.168.1.112:8070/iot_current_station.bin"


// void start_mdns_service(void)
// {
//     // Inicializa o serviço mDNS
//     ESP_ERROR_CHECK(mdns_init());
//     // Define o hostname da placa na rede -> esp8266.local
//     ESP_ERROR_CHECK(mdns_hostname_set("esp8266_000"));
//     // Define o nome de exibição/instância
//     ESP_ERROR_CHECK(mdns_instance_name_set("ESP8266 Power Monitor"));

//     ESP_LOGI("MDNS", "mDNS iniciado! Acesse via: http://esp8266.local");
// }


// static const char *TAG = "MAIN_APP";
// static EventGroupHandle_t s_wifi_event_group;
// const int WIFI_CONNECTED_BIT = BIT0;

// static esp_err_t event_handler(void *ctx, system_event_t *event)
// {
//     switch(event->event_id) {
//     case SYSTEM_EVENT_STA_START:
//         esp_wifi_connect();
//         break;
//     case SYSTEM_EVENT_STA_GOT_IP:
//         ESP_LOGI(TAG, "Wi-Fi Conectado. IP: " IPSTR, IP2STR(&event->event_info.got_ip.ip_info.ip));
//         xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
//         break;
//     case SYSTEM_EVENaT_STA_DISCONNECTED:
//         ESP_LOGW(TAG, "Wi-Fi desconectado, reconectando...");
//         esp_wifi_connect();
//         xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
//         break;
//     default:
//         break;
//     }
//     return ESP_OK;
// }

// void wifi_init_sta(void)
// {
//     s_wifi_event_group = xEventGroupCreate();

//     // Inicialização legada do TCP/IP Adapter no ESP8266
//     tcpip_adapter_init();

//     // Event Loop legado
//     ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));

//     wifi_config_t wifi_config = {
//         .sta = {
//             .ssid = WIFI_SSID,
//             .password = WIFI_PASS,
//         },
//     };

//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
//     ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
//     ESP_ERROR_CHECK(esp_wifi_start());

//     ESP_LOGI(TAG, "Aguardando conexão Wi-Fi...");
//     xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
// }

// static esp_err_t update_post_handler(httpd_req_t *req)
// {
//     char buf[128];
//     int ret, remaining = req->content_len;

//     if (remaining >= sizeof(buf)) {
//         httpd_resp_set_status(req, "400 Bad Request");
//         const char *err_msg = "Payload muito grande";
//         httpd_resp_send(req, err_msg, strlen(err_msg));
//         return ESP_FAIL;
//     }

//     ret = httpd_req_recv(req, buf, remaining);
//     if (ret <= 0) {
//         return ESP_FAIL;
//     }
//     buf[ret] = '\0';

//     ESP_LOGI("SERVER", "Recebida URL para OTA: %s", buf);

//     // Dispara a task OTA em background
//     esp_err_t err = ota_update_start(buf);
//     if (err == ESP_OK) {
//         const char *resp_str = "Processo de OTA iniciado com sucesso!";
//         httpd_resp_send(req, resp_str, strlen(resp_str));
//     } else {
//         httpd_resp_set_status(req, "500 Internal Server Error");
//         const char *err_msg = "Falha ao iniciar OTA";
//         httpd_resp_send(req, err_msg, strlen(err_msg));
//     }

//     return ESP_OK;
// }

// void start_webserver(void)
// {
//     httpd_handle_t server = NULL;
//     httpd_config_t config = HTTPD_DEFAULT_CONFIG();

//     if (httpd_start(&server, &config) == ESP_OK) {
//         httpd_uri_t update_uri = {
//             .uri      = "/update",
//             .method   = HTTP_POST,
//             .handler  = update_post_handler,
//             .user_ctx = NULL
//         };
//         httpd_register_uri_handler(server, &update_uri);
//     }
// }


// void app_main(void)
// {
//     // Inicializa NVS para salvar dados de Wi-Fi e OTA
//     esp_err_t ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         ret = nvs_flash_init();
//     }
//     ESP_ERROR_CHECK(ret);

//     // Inicializa Wi-Fi
//     wifi_init_sta();
    
//     start_mdns_service();
//     start_webserver();

//     // Inicializa Sensor SCT-013
//     sct013_config_t sensor_cfg = SCT013_CONFIG_DEFAULT();
//     sensor_cfg.grid_voltage_rms = 220.0f;
//     sensor_cfg.power_factor = 0.95f;
//     ESP_ERROR_CHECK(sct013_init(&sensor_cfg));

//     sct013_metrics_t metrics;


//     while (1) {
//         sct013_get_metrics(&metrics);

//         int i_p = (int)metrics.current_rms;
//         int i_f = (int)((metrics.current_rms - i_p) * 100);

//         int p_act = (int)metrics.power_active;
//         int kwh_p = (int)metrics.energy_kwh;
//         int kwh_f = (int)((metrics.energy_kwh - kwh_p) * 1000);

//         ESP_LOGI(TAG, "[Medição] Corrente: %d.%02d A | Potência Ativa: %d W | Consumo: %d.%03d kWh",
//                  i_p, (i_f < 0 ? -i_f : i_f),
//                  p_act,
//                  kwh_p, (kwh_f < 0 ? -kwh_f : kwh_f));

//         vTaskDelay(pdMS_TO_TICKS(3000));
//     }
// }