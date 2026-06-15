#pragma once

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>

#include "se05x_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SE05X_DT_NODE DT_ALIAS(se05x)

typedef struct {
	struct i2c_dt_spec i2c;
	uint32_t timeout_ms;
} se05x_zephyr_i2c_config_t;

se05x_zephyr_i2c_config_t se05x_zephyr_i2c_default_config(void);
int se05x_zephyr_i2c_bus_create(se05x_bus_t *bus,
				 const se05x_zephyr_i2c_config_t *config);
uint8_t se05x_zephyr_i2c_default_address(void);

#ifdef __cplusplus
}
#endif
