#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_spiffs.h"
#include "data_store.h"

static const char *TAG = "DATA_STORE";

#define STORAGE_BASE_PATH "/spiffs"
#define DATA_FILE_PATH    "/spiffs/offline_records.bin"

esp_err_t data_store_init(void)
{
    ESP_LOGI(TAG, "Inicializando e montando o SPIFFS...");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = STORAGE_BASE_PATH,     // "/spiffs"
        .partition_label = "storage",      // Nome exato na partitions.csv
        .max_files = 5,
        .format_if_mount_failed = true     // Formata automaticamente na primeira execução
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Falha ao montar ou formatar a partição SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Partição 'storage' não encontrada na Partition Table!");
        } else {
            ESP_LOGE(TAG, "Erro ao registrar SPIFFS (%d)", ret);
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS montado com sucesso! Total: %d bytes, Usado: %d bytes", total, used);
    } else {
        ESP_LOGE(TAG, "Falha ao obter informações do SPIFFS (%d)", ret);
    }

    return ESP_OK;
}

esp_err_t data_store_write_record(const measurement_record_t *record)
{
    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(DATA_FILE_PATH, "ab"); // Append em modo binário
    if (f == NULL) {
        ESP_LOGE(TAG, "Erro ao abrir o arquivo para escrita");
        return ESP_FAIL;
    }

    size_t written = fwrite(record, sizeof(measurement_record_t), 1, f);
    fclose(f);

    if (written != 1) {
        ESP_LOGE(TAG, "Falha ao gravar registro na Flash");
        return ESP_FAIL;
    }

    int i_part = (int)record->current_rms;
    int d_part = (int)((record->current_rms - i_part) * 100);
    if (d_part < 0) d_part = -d_part;

    ESP_LOGI(TAG, "Registro salvo localmente (Corrente: %d.%02dA)", i_part, d_part);
    return ESP_OK;
}

size_t data_store_get_pending_count(void)
{
    struct stat st;
    if (stat(DATA_FILE_PATH, &st) != 0) {
        return 0; // Arquivo não existe ainda
    }

    return (size_t)(st.st_size / sizeof(measurement_record_t));
}

esp_err_t data_store_pop_oldest_record(measurement_record_t *record)
{
    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t pending = data_store_get_pending_count();
    if (pending == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    FILE *f = fopen(DATA_FILE_PATH, "rb");
    if (f == NULL) {
        return ESP_FAIL;
    }

    // Lê o primeiro elemento (mais antigo)
    size_t read_bytes = fread(record, sizeof(measurement_record_t), 1, f);
    if (read_bytes != 1) {
        fclose(f);
        return ESP_FAIL;
    }

    // Se só havia 1 elemento, basta remover o arquivo
    if (pending == 1) {
        fclose(f);
        remove(DATA_FILE_PATH);
        return ESP_OK;
    }

    // Se havia mais de 1, reescreve o arquivo omitindo o primeiro registro lido
    FILE *temp_f = fopen("/spiffs/temp.bin", "wb");
    if (temp_f == NULL) {
        fclose(f);
        return ESP_FAIL;
    }

    measurement_record_t temp_rec;
    while (fread(&temp_rec, sizeof(measurement_record_t), 1, f) == 1) {
        fwrite(&temp_rec, sizeof(measurement_record_t), 1, temp_f);
    }

    fclose(f);
    fclose(temp_f);

    remove(DATA_FILE_PATH);
    rename("/spiffs/temp.bin", DATA_FILE_PATH);

    return ESP_OK;
}

esp_err_t data_store_clear_all(void)
{
    remove(DATA_FILE_PATH);
    ESP_LOGI(TAG, "Buffer local limpo com sucesso.");
    return ESP_OK;
}

