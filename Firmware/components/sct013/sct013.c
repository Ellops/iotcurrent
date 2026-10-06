#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "driver/adc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sct013.h"

static const char *TAG = "SCT013_COMP";
#define ADC_RESOLUTION 1024.0f

static sct013_config_t g_config;
static sct013_metrics_t g_metrics = {0};
static SemaphoreHandle_t g_data_mutex = NULL;
static float g_calibration_factor = 35.71f;
static TickType_t g_last_update_tick = 0;

static float sct013_sample_rms(void)
{
    uint32_t number_of_samples = 0;
    double sum_voltage = 0.0;
    double sum_squared_voltage = 0.0;
    uint16_t adc_raw = 0;

    TickType_t start_tick = xTaskGetTickCount();
    TickType_t period_ticks = pdMS_TO_TICKS(g_config.sample_period_ms);

    // Passagem 1: Offset DC
    while ((xTaskGetTickCount() - start_tick) < period_ticks) {
        if (adc_read(&adc_raw) == ESP_OK) {
            float voltage_inst = ((float)adc_raw / ADC_RESOLUTION) * g_config.adc_ref_voltage;
            sum_voltage += voltage_inst;
            number_of_samples++;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (number_of_samples == 0) return 0.0f;
    float dynamic_dc_offset = (float)(sum_voltage / number_of_samples);

    // Passagem 2: AC e Variância
    start_tick = xTaskGetTickCount();
    uint32_t rms_samples = 0;

    while ((xTaskGetTickCount() - start_tick) < period_ticks) {
        if (adc_read(&adc_raw) == ESP_OK) {
            float voltage_inst = ((float)adc_raw / ADC_RESOLUTION) * g_config.adc_ref_voltage;
            float voltage_ac = voltage_inst - dynamic_dc_offset;
            sum_squared_voltage += (double)(voltage_ac * voltage_ac);
            rms_samples++;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (rms_samples == 0) return 0.0f;

    double mean_squared_voltage = sum_squared_voltage / (double)rms_samples;
    double clean_mean_squared = mean_squared_voltage - (double)g_config.noise_v_rms_sq;
    if (clean_mean_squared < 0.0) clean_mean_squared = 0.0;

    float v_rms = (float)sqrt(clean_mean_squared);
    float i_rms = v_rms * g_calibration_factor;

    if (i_rms < g_config.noise_floor_amps) {
        i_rms = 0.0f;
    }

    return i_rms;
}

static void sct013_task(void *pvParameters)
{
    g_last_update_tick = xTaskGetTickCount();

    while (1) {
        float current = sct013_sample_rms();
        TickType_t now = xTaskGetTickCount();
        float delta_seconds = (float)(now - g_last_update_tick) * portTICK_PERIOD_MS / 1000.0f;
        g_last_update_tick = now;

        float p_apparent = current * g_config.grid_voltage_rms;
        float p_active = p_apparent * g_config.power_factor;
        double energy_increment_kwh = (double)(p_active * delta_seconds) / 3600000.0;

        xSemaphoreTake(g_data_mutex, portMAX_DELAY);
        g_metrics.current_rms = current;
        g_metrics.power_apparent = p_apparent;
        g_metrics.power_active = p_active;
        g_metrics.energy_kwh += energy_increment_kwh;
        xSemaphoreGive(g_data_mutex);

        vTaskDelay(pdMS_TO_TICKS(g_config.update_interval_ms));
    }
}

esp_err_t sct013_init(const sct013_config_t *config)
{
    if (config == NULL) {
        sct013_config_t default_cfg = SCT013_CONFIG_DEFAULT();
        g_config = default_cfg;
    } else {
        g_config = *config;
    }

    g_calibration_factor = g_config.ct_ratio / g_config.burden_resistor_ohms;

    g_data_mutex = xSemaphoreCreateMutex();
    if (g_data_mutex == NULL) return ESP_FAIL;

    adc_config_t adc_config;
    adc_config.mode = ADC_READ_TOUT_MODE;
    adc_config.clk_div = 8;
    ESP_ERROR_CHECK(adc_init(&adc_config));

    if (xTaskCreate(sct013_task, "sct013_task", 3072, NULL, 5, NULL) != pdPASS) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

void sct013_get_metrics(sct013_metrics_t *metrics)
{
    if (metrics && g_data_mutex) {
        xSemaphoreTake(g_data_mutex, portMAX_DELAY);
        *metrics = g_metrics;
        xSemaphoreGive(g_data_mutex);
    }
}

void sct013_reset_energy(void)
{
    if (g_data_mutex) {
        xSemaphoreTake(g_data_mutex, portMAX_DELAY);
        g_metrics.energy_kwh = 0.0;
        xSemaphoreGive(g_data_mutex);
    }
}