#include "se05x_bus_zephyr.h"

#include <errno.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(se05x_i2c, LOG_LEVEL_INF);

typedef struct {
	se05x_zephyr_i2c_config_t config;
	bool opened;
} se05x_zephyr_i2c_ctx_t;

uint8_t se05x_zephyr_i2c_default_address(void)
{
	return DT_REG_ADDR(SE05X_DT_NODE);
}

se05x_zephyr_i2c_config_t se05x_zephyr_i2c_default_config(void)
{
	se05x_zephyr_i2c_config_t config = {
		.i2c = I2C_DT_SPEC_GET(SE05X_DT_NODE),
		.timeout_ms = 1000U,
	};

	return config;
}

static int zephyr_i2c_open(se05x_bus_t *bus)
{
	if (bus == NULL || bus->ops.ctx == NULL) {
		return -EINVAL;
	}

	se05x_zephyr_i2c_ctx_t *ctx = (se05x_zephyr_i2c_ctx_t *)bus->ops.ctx;

	if (!i2c_is_ready_dt(&ctx->config.i2c)) {
		LOG_ERR("I2C device %s is not ready", ctx->config.i2c.bus->name);
		return -ENODEV;
	}

	ctx->opened = true;
	LOG_INF("SE05x I2C ready: bus=%s addr=0x%02x",
		ctx->config.i2c.bus->name, ctx->config.i2c.addr);
	return 0;
}

static void zephyr_i2c_close(se05x_bus_t *bus)
{
	if (bus == NULL || bus->ops.ctx == NULL) {
		return;
	}

	se05x_zephyr_i2c_ctx_t *ctx = (se05x_zephyr_i2c_ctx_t *)bus->ops.ctx;

	ctx->opened = false;
}

static int zephyr_i2c_write(se05x_bus_t *bus, uint8_t address,
			    const uint8_t *data, size_t data_len)
{
	if (bus == NULL || bus->ops.ctx == NULL || data == NULL || data_len == 0U) {
		return -EINVAL;
	}

	se05x_zephyr_i2c_ctx_t *ctx = (se05x_zephyr_i2c_ctx_t *)bus->ops.ctx;
	const uint16_t addr = address != 0U ? address : ctx->config.i2c.addr;

	return i2c_write(ctx->config.i2c.bus, data, data_len, addr);
}

static int zephyr_i2c_read(se05x_bus_t *bus, uint8_t address,
			   uint8_t *data, size_t data_len)
{
	if (bus == NULL || bus->ops.ctx == NULL || data == NULL || data_len == 0U) {
		return -EINVAL;
	}

	se05x_zephyr_i2c_ctx_t *ctx = (se05x_zephyr_i2c_ctx_t *)bus->ops.ctx;
	const uint16_t addr = address != 0U ? address : ctx->config.i2c.addr;

	return i2c_read(ctx->config.i2c.bus, data, data_len, addr);
}

static void zephyr_i2c_delay_ms(uint32_t delay_ms)
{
	k_msleep(delay_ms > 0U ? delay_ms : 1U);
}

int se05x_zephyr_i2c_bus_create(se05x_bus_t *bus,
				 const se05x_zephyr_i2c_config_t *config)
{
	if (bus == NULL) {
		return -EINVAL;
	}

	se05x_zephyr_i2c_ctx_t *ctx = calloc(1, sizeof(*ctx));

	if (ctx == NULL) {
		return -ENOMEM;
	}

	ctx->config = config != NULL ? *config : se05x_zephyr_i2c_default_config();

	bus->ops.open = zephyr_i2c_open;
	bus->ops.close = zephyr_i2c_close;
	bus->ops.write = zephyr_i2c_write;
	bus->ops.read = zephyr_i2c_read;
	bus->ops.delay_ms = zephyr_i2c_delay_ms;
	bus->ops.ctx = ctx;

	return 0;
}
