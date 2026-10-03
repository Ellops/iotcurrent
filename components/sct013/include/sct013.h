#ifndef SCT013_H
#define SCT013_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float burden_resistor_ohms;  // ex: 56.0f
    float ct_ratio;               // ex: 2000.0f
    float adc_ref_voltage;        // ex: 3.22f
    float noise_floor_amps;       // ex: 0.30f
    float noise_v_rms_sq;         // ex: 0.000031f
    float grid_voltage_rms;       // ex: 220.0f
    float power_factor;           // ex: 0.95f (estimativa para cálculo de W)
    uint32_t sample_period_ms;    // ex: 200
    uint32_t update_interval_ms;  // ex: 1000
} sct013_config_t;

#define SCT013_CONFIG_DEFAULT() { \
    .burden_resistor_ohms = 56.0f, \
    .ct_ratio = 2000.0f, \
    .adc_ref_voltage = 3.22f, \
    .noise_floor_amps = 0.30f, \
    .noise_v_rms_sq = 0.000031f, \
    .grid_voltage_rms = 220.0f, \
    .power_factor = 0.95f, \
    .sample_period_ms = 200, \
    .update_interval_ms = 1000 \
}

typedef struct {
    float current_rms;    // A
    float power_apparent; // VA
    float power_active;   // W
    double energy_kwh;    // kWh acumulado
} sct013_metrics_t;

esp_err_t sct013_init(const sct013_config_t *config);
void sct013_get_metrics(sct013_metrics_t *metrics);
void sct013_reset_energy(void);

#ifdef __cplusplus
}
#endif

#endif // SCT013_H