#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "se05x_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int port;
    int sda_gpio;
    int scl_gpio;
    uint8_t address;
    uint32_t clock_hz;
    uint32_t timeout_ms;
    bool enable_internal_pullups;
} se05x_esp_i2c_config_t;

se05x_esp_i2c_config_t se05x_esp_i2c_default_config(void);
int se05x_esp_i2c_bus_create(se05x_bus_t *bus, const se05x_esp_i2c_config_t *config);

#ifdef __cplusplus
}
#endif
