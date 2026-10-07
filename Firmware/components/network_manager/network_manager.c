#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "tcpip_adapter.h"

#include "lwip/opt.h"
#include "lwip/ip_addr.h"
#include "lwip/ip6_addr.h"
#include "mdns.h"

#include <time.h>
#include <sys/time.h>
#include "lwip/apps/sntp.h"

#include "esp_http_server.h"
#include "ota_update.h"
#include "network_manager.h"

static const char *TAG = "NET_MGR";
static EventGroupHandle_t s_wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

#define MDNS_HEARTBEAT_INTERVAL_MS 30000


static void mdns_heartbeat_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Task mDNS Heartbeat iniciada.");

    while (1) {
        // 1. Desativa a economia de energia do rádio para transmissão limpa do pacote Multicast UDP 5353
        esp_wifi_set_ps(WIFI_PS_NONE);

        // 2. Anuncia o serviço mDNS no ar sem recriá-lo em memória
        mdns_service_port_set("_http", "_tcp", 80);

        // 3. Pequeno delay para o rádio escoar o buffer de TX
        vTaskDelay(pdMS_TO_TICKS(300));

        // 4. Retorna para o modo de economia de energia do Wi-Fi (Modem/Light Sleep do rádio)
        esp_wifi_set_ps(WIFI_PS_MODEM);

        // Aguarda até o próximo ciclo (30s)
        vTaskDelay(pdMS_TO_TICKS(MDNS_HEARTBEAT_INTERVAL_MS));
    }
}

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

    // Adiciona o serviço HTTP UMA ÚNICA VEZ durante o boot
    err = mdns_service_add(instance_name, "_http", "_tcp", 80, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao adicionar serviço HTTP mDNS: %d", err);
    }

    ESP_LOGI(TAG, "mDNS iniciado! Hostname: http://%s.local", hostname);

    // Criação da task com stack de 3KB para evitar estourar a memória da pilha
    xTaskCreate(
        mdns_heartbeat_task,
        "mdns_hb_task",
        3072,
        NULL,
        1,
        NULL
    );

    return ESP_OK;
}

static esp_err_t update_post_handler(httpd_req_t *req)
{
    // 1. Acorda o rádio imediatamente ao engatar o handler do /update
    esp_wifi_set_ps(WIFI_PS_NONE);

    char buf[128];
    int remaining = req->content_len;

    if (remaining <= 0 || remaining >= sizeof(buf)) {
        httpd_resp_set_status(req, "400 Bad Request");
        const char *err_msg = "Tamanho do payload inválido";
        httpd_resp_send(req, err_msg, strlen(err_msg));
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    ESP_LOGI(TAG, "Recebida URL para OTA: %s", buf);

    // Responde ao cliente web antes de travar no OTA/Reboot
    const char *resp_str = "Processo de OTA iniciado com sucesso!";
    httpd_resp_send(req, resp_str, strlen(resp_str));

    // Garante pequeno delay para enviar a resposta HTTP TCP antes da regravação de flash
    vTaskDelay(pdMS_TO_TICKS(500));

    esp_err_t err = ota_update_start(buf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao executar rotina de OTA: %d", err);
        // Restaura economia se o OTA falhar antes de reiniciar
        esp_wifi_set_ps(WIFI_PS_MODEM);
    }

    return err;
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

void network_sntp_init(void)
{
    ESP_LOGI(TAG, "Inicializando SNTP...");
    
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

    // --- BLOQUEIO ATÉ SINCRONIZAR HORÁRIO ---
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 30; // Aguarda até 30 segundos (30 x 1s)

    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry <= retry_count) {
        ESP_LOGI(TAG, "Aguardando sincronização do relógio do sistema (%d/%d)...", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Atualiza a variável com o horário corrente após sair do loop
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGW(TAG, "Falha ao obter horário via NTP (Timeout). O sistema continuará com timestamp desatualizado.");
    } else {
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "Horário sincronizado com sucesso: %s", strftime_buf);
    }
}

