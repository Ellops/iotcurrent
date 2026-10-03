#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "ota_update.h"

#define BUFFSIZE 1024
static const char *TAG = "OTA_UPDATE";
static bool s_ota_in_progress = false;

typedef struct {
    char url[128];
} ota_args_t;

bool ota_update_is_running(void)
{
    return s_ota_in_progress;
}

static void ota_task(void *pvParameter)
{
    ota_args_t *args = (ota_args_t *)pvParameter;
    char ota_write_data[BUFFSIZE + 1] = { 0 };

    ESP_LOGI(TAG, "Iniciando OTA a partir da URL: %s", args->url);

    esp_http_client_config_t config = {
        .url = args->url,
        .timeout_ms = 8000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Falha ao inicializar o cliente HTTP");
        goto exit;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao abrir conexão HTTP: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        goto exit;
    }

    esp_http_client_fetch_headers(client);

    // Valida o status da resposta HTTP para evitar gravar páginas 404/HTML
    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "Servidor retornou erro HTTP Status: %d", status_code);
        esp_http_client_cleanup(client);
        goto exit;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Partição de destino OTA não encontrada");
        esp_http_client_cleanup(client);
        goto exit;
    }

    ESP_LOGI(TAG, "Gravando partição sub-tipo %d no offset 0x%x",
             update_partition->subtype, update_partition->address);

    esp_ota_handle_t update_handle = 0;
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin falhou (%s)", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        goto exit;
    }

    int binary_file_len = 0;
    while (1) {
        int data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
        if (data_read < 0) {
            ESP_LOGE(TAG, "Erro na leitura dos dados HTTP");
            esp_ota_end(update_handle);
            esp_http_client_cleanup(client);
            goto exit;
        } else if (data_read == 0) {
            break; // Fim do arquivo
        }

        err = esp_ota_write(update_handle, (const void *)ota_write_data, data_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao gravar partição OTA (%s)", esp_err_to_name(err));
            esp_ota_end(update_handle);
            esp_http_client_cleanup(client);
            goto exit;
        }
        binary_file_len += data_read;
    }

    ESP_LOGI(TAG, "Total de bytes de firmware recebidos: %d", binary_file_len);

    if (esp_ota_end(update_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end falhou!");
        esp_http_client_cleanup(client);
        goto exit;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition falhou (%s)", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        goto exit;
    }

    ESP_LOGI(TAG, "Sucesso! Reiniciando em 2 segundos...");
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

exit:
    free(args);
    s_ota_in_progress = false;
    vTaskDelete(NULL);
}

esp_err_t ota_update_start(const char *url)
{
    if (s_ota_in_progress) {
        ESP_LOGW(TAG, "Aviso: Atualização OTA já está em andamento");
        return ESP_ERR_INVALID_STATE;
    }

    ota_args_t *args = malloc(sizeof(ota_args_t));
    if (!args) {
        return ESP_ERR_NO_MEM;
    }

    strncpy(args->url, url, sizeof(args->url) - 1);
    args->url[sizeof(args->url) - 1] = '\0';

    s_ota_in_progress = true;

    BaseType_t ret = xTaskCreate(ota_task, "ota_update_task", 8192, args, 5, NULL);
    if (ret != pdPASS) {
        free(args);
        s_ota_in_progress = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}