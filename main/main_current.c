// /* Power save Example

//    This example code is in the Public Domain (or CC0 licensed, at your option.)

//    Unless required by applicable law or agreed to in writing, this
//    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//    CONDITIONS OF ANY KIND, either express or implied.
// */

// /*
//    this example shows how to use power save mode
//    set a router or a AP using the same SSID&PASSWORD as configuration of this example.
//    start esp8266 and when it connected to AP it will enter power save mode
// */
// #include <stdio.h>

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

// #include "esp_system.h"
// #include "esp_spi_flash.h"
// #include "esp_sleep.h"

// #include "nvs.h"
// #include "nvs_flash.h"

// #include "wifimod.h"

// #include "mqttmod.h"

// #include "curr.h"

// void makefloat(float number, char* buffer)
// {
//    int16_t number_n = (uint16_t)(number);
//    uint16_t number_v = 0;
//    if(number > 0)
//    {
//       number_v = (uint16_t)(number*1000.0f - (float)number_n*1000.0f);
//       if(number_v<100)
//       {
//          if(number_v<10)
//          {
//             sprintf(buffer, "%u.00%u", number_n,number_v);
//          }
//          else
//          {
//             sprintf(buffer, "%u.0%u", number_n,number_v);
//          }   
//       }
//       else
//       {
//          sprintf(buffer, "%u.%u", number_n,number_v);  
//       }
//    }
//    else
//    {
//       number_v = (uint16_t)((float)number_n*1000.0f - number*1000.0f);
//       if(number_v<100)
//       {
//          if(number_v<10)
//          {
//             sprintf(buffer, "-%u.00%u", number_n,number_v);
//          }
//          else
//          {
//             sprintf(buffer, "-%u.0%u", number_n,number_v);
//          }
//       }
//       else
//       {
//          sprintf(buffer, "-%u.%u", number_n,number_v);  
//       }
//    }
// }

// static const char *mTAG = "Main";

// void app_main(void)
// {
//    ESP_ERROR_CHECK(nvs_flash_init());
   
//    //ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
//    // wifi_init_sta();
   
//    curr_init();

//    //esp_sleep_enable_timer_wakeup(10000000);
//    //esp_wifi_stop();
//    //esp_power_consumption_info(true);

//    // esp_mqtt_client_handle_t mclient;
//    // mclient = mqtt_app_start();
//    int msg_id;
//    char buffer[8];
//    char pbuffer[8];


//    while(true)
//    {
//       float current = max_current*0.707;
//       makefloat(current,buffer);
//       float potency = current*220;
//       makefloat(potency,pbuffer);

//       // const char *msge = buffer;
//       // msg_id = esp_mqtt_client_publish(mclient, "/inside/table/current", msge, 0, 1, 0);

//       // const char *msge2 = pbuffer;
//       // msg_id = esp_mqtt_client_publish(mclient, "/inside/table/potency", msge2, 0, 1, 0);

//       ESP_LOGI(mTAG, "Corrente: %s",buffer);
//       ESP_LOGI(mTAG, "Potencia: %s",pbuffer);
      
//       vTaskDelay(pdMS_TO_TICKS(1000));
//       //printf("Entering Light Sleep Mode\n");
//       //esp_light_sleep_start();
//       //esp_power_consumption_info(false);
//    }
// }

#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "driver/adc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RMS_CALC";

// ============================================================================
// CONFIGURAÇÕES DO CIRCUITO
// ============================================================================
// Fator de Ganho = 2000 (Relação SCT-013-000) / R_burden
// Exemplo para R_burden = 56 Ω: 2000 / 56 = 35.71
// Exemplo para R_burden = 36.3 Ω: 2000 / 36.3 = 55.10
#define BURDEN_RESISTOR    33.0f
#define CT_RATIO           2000.0f
#define CALIBRATION_FACTOR (CT_RATIO / BURDEN_RESISTOR)

// Configurações do ADC do ESP8266 (0 a 3.3V com divisor interno de placa NodeMCU/Wemos)
#define ADC_VOLTAGE_REF    3.3f
#define ADC_RESOLUTION     1024.0f

/**
 * @brief Lê a corrente RMS acumulando amostras durante uma janela de tempo.
 * 
 * @param sample_period_ms Tempo total de amostragem em milissegundos (ex: 200 ms).
 * @return float Corrente RMS medida em Ampères.
 */
float read_current_rms(uint32_t sample_period_ms)
{
    uint32_t number_of_samples = 0;
    double sum_voltage = 0.0;
    double sum_squared_voltage = 0.0;
    uint16_t adc_raw = 0;

    TickType_t start_tick = xTaskGetTickCount();
    TickType_t period_ticks = pdMS_TO_TICKS(sample_period_ms);

    while ((xTaskGetTickCount() - start_tick) < period_ticks) 
    {
        if (adc_read(&adc_raw) == ESP_OK) 
        {
            float voltage_inst = ((float)adc_raw / ADC_RESOLUTION) * 3.22f;
            sum_voltage += voltage_inst;
            number_of_samples++;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (number_of_samples == 0) return 0.0f;

    float dynamic_dc_offset = (float)(sum_voltage / number_of_samples);

    start_tick = xTaskGetTickCount();
    uint32_t rms_samples = 0;

    while ((xTaskGetTickCount() - start_tick) < period_ticks) 
    {
        if (adc_read(&adc_raw) == ESP_OK) 
        {
            float voltage_inst = ((float)adc_raw / ADC_RESOLUTION) * 3.22f;
            float voltage_ac = voltage_inst - dynamic_dc_offset;

            sum_squared_voltage += (double)(voltage_ac * voltage_ac);
            rms_samples++;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (rms_samples == 0) return 0.0f;

    double mean_squared_voltage = sum_squared_voltage / (double)rms_samples;
    float v_rms = (float)sqrt(mean_squared_voltage);
    float i_rms = v_rms * CALIBRATION_FACTOR;

    return i_rms;
}

void rms_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Iniciando monitoramento de corrente RMS...");

    while (1) 
    {
      // Lê a corrente acumulando amostras durante 200ms
      float current_rms = read_current_rms(200);

      int integer_part = (int)current_rms;
      int fractional_part = (int)((current_rms - integer_part) * 100);

      // Garante que valores negativos no fracionário fiquem positivos para exibição
      if (fractional_part < 0) fractional_part = -fractional_part;

      ESP_LOGI(TAG, "Corrente RMS: %d.%02d A", integer_part, fractional_part);
      // Aguarda 1 segundo antes da próxima medição
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    // Inicialização do ADC
    adc_config_t adc_config;
    adc_config.mode = ADC_READ_TOUT_MODE;
    adc_config.clk_div = 8;
    ESP_ERROR_CHECK(adc_init(&adc_config));

    // Aumentado a pilha para 2048/3072 bytes por causa das operações de float e ESP_LOGI
    xTaskCreate(rms_task, "rms_task", 3072, NULL, 5, NULL);
}