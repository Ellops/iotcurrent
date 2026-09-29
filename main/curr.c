#include "curr.h"

float max_current = 0.0f; 
float current_value = 0.0f;

void curr_task()
{
    ESP_LOGI(currTAG , "Task Started\n");
    uint16_t index = 0;
    while (1) {
        current_value = curr_read();
        
        if(current_value >= max_current)
        {
            max_current = current_value;
            index = 0;
        }else{index++;}

        if (index > 512)
        {
            max_current = -999.0f; 
            index = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void curr_init()
{
    adc_config_t adc_config;

    adc_config.mode = ADC_READ_TOUT_MODE;
    adc_config.clk_div = 8; 
    ESP_ERROR_CHECK(adc_init(&adc_config));
    ESP_LOGI(currTAG , "ADC started\n");
    xTaskCreate(curr_task, "curr_task", 2048, NULL, 5, NULL);
}

double curr_read()
{
    uint16_t adc_data[10];
    float current = 0.0;

    if (ESP_OK == adc_read(&adc_data[0])) 
    {
        float tension = (float)(adc_data[0] - 33)/1024.0f * 3.3f;

        current = 6.0607 * (tension - 1.65);
    }
    return current;
}