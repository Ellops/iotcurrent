#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "tcpip_adapter.h"

#include "lwip/opt.h"
#include "lwip/ip_addr.h"
#include "lwip/ip6_addr.h"
#include "mdns.h"

#include "esp_http_server.h"
#include "ota_update.h"
#include "network_manager.h"

static const char *TAG = "NET_MGR";
static EventGroupHandle_t s_wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "Wi-Fi Conectado. IP: " IPSTR, IP2STR(&event->event_info.got_ip.ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGW(TAG, "Wi-Fi desconectado, reconectando...");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

void network_wifi_init(const char *ssid, const char *password)
{
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Aguardando conexão Wi-Fi...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

esp_err_t network_mdns_init(const char *hostname, const char *instance_name)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar mDNS: %d", err);
        return err;
    }

    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_ERROR_CHECK(mdns_instance_name_set(instance_name));

    ESP_LOGI(TAG, "mDNS iniciado! Hostname: http://%s.local", hostname);
    return ESP_OK;
}

static esp_err_t update_post_handler(httpd_req_t *req)
{
    char buf[128];
    int remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_set_status(req, "400 Bad Request");
        const char *err_msg = "Payload muito grande";
        httpd_resp_send(req, err_msg, strlen(err_msg));
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    ESP_LOGI(TAG, "Recebida URL para OTA: %s", buf);

    esp_err_t err = ota_update_start(buf);
    if (err == ESP_OK) {
        const char *resp_str = "Processo de OTA iniciado com sucesso!";
        httpd_resp_send(req, resp_str, strlen(resp_str));
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        const char *err_msg = "Falha ao iniciar OTA";
        httpd_resp_send(req, err_msg, strlen(err_msg));
    }

    return ESP_OK;
}

esp_err_t network_webserver_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    esp_err_t err = httpd_start(&server, &config);
    if (err == ESP_OK) {
        httpd_uri_t update_uri = {
            .uri      = "/update",
            .method   = HTTP_POST,
            .handler  = update_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &update_uri);
        ESP_LOGI(TAG, "Servidor Web HTTP iniciado na porta %d", config.server_port);
    }
    return err;
}