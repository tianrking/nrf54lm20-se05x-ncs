#include "se05x_bus_esp_idf.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

typedef struct {
    se05x_esp_i2c_config_t config;
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t device_handle;
} se05x_esp_i2c_ctx_t;

static const char *TAG = "se05x_i2c";

#ifdef CONFIG_SE05X_I2C_INTERNAL_PULLUPS
#define SE05X_ENABLE_INTERNAL_PULLUPS true
#else
#define SE05X_ENABLE_INTERNAL_PULLUPS false
#endif

static int esp_i2c_open(se05x_bus_t *bus)
{
    if (bus == NULL || bus->ops.ctx == NULL) {
        return -1;
    }

    se05x_esp_i2c_ctx_t *ctx = (se05x_esp_i2c_ctx_t *)bus->ops.ctx;
    if (ctx->device_handle != NULL) {
        return 0;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = ctx->config.port,
        .sda_io_num = ctx->config.sda_gpio,
        .scl_io_num = ctx->config.scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = ctx->config.enable_internal_pullups,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &ctx->bus_handle);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ctx->config.address,
        .scl_speed_hz = ctx->config.clock_hz,
    };

    ret = i2c_master_bus_add_device(ctx->bus_handle, &device_config, &ctx->device_handle);
    if (ret != ESP_OK) {
        i2c_del_master_bus(ctx->bus_handle);
        ctx->bus_handle = NULL;
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ESP_LOGI(TAG,
        "SE05x I2C ready: port=%d sda=%d scl=%d addr=0x%02x hz=%" PRIu32,
        ctx->config.port,
        ctx->config.sda_gpio,
        ctx->config.scl_gpio,
        ctx->config.address,
        ctx->config.clock_hz);
    return 0;
}

static void esp_i2c_close(se05x_bus_t *bus)
{
    if (bus == NULL || bus->ops.ctx == NULL) {
        return;
    }

    se05x_esp_i2c_ctx_t *ctx = (se05x_esp_i2c_ctx_t *)bus->ops.ctx;
    if (ctx->device_handle != NULL) {
        i2c_master_bus_rm_device(ctx->device_handle);
        ctx->device_handle = NULL;
    }
    if (ctx->bus_handle != NULL) {
        i2c_del_master_bus(ctx->bus_handle);
        ctx->bus_handle = NULL;
    }
}

static int esp_i2c_write(se05x_bus_t *bus, uint8_t address, const uint8_t *data, size_t data_len)
{
    (void)address;
    if (bus == NULL || bus->ops.ctx == NULL || data == NULL || data_len == 0) {
        return -1;
    }

    se05x_esp_i2c_ctx_t *ctx = (se05x_esp_i2c_ctx_t *)bus->ops.ctx;
    esp_err_t ret = i2c_master_transmit(ctx->device_handle, data, data_len, (int)ctx->config.timeout_ms);
    return ret == ESP_OK ? 0 : -1;
}

static int esp_i2c_read(se05x_bus_t *bus, uint8_t address, uint8_t *data, size_t data_len)
{
    (void)address;
    if (bus == NULL || bus->ops.ctx == NULL || data == NULL || data_len == 0) {
        return -1;
    }

    se05x_esp_i2c_ctx_t *ctx = (se05x_esp_i2c_ctx_t *)bus->ops.ctx;
    esp_err_t ret = i2c_master_receive(ctx->device_handle, data, data_len, (int)ctx->config.timeout_ms);
    return ret == ESP_OK ? 0 : -1;
}

static void esp_i2c_delay_ms(uint32_t delay_ms)
{
    vTaskDelay(pdMS_TO_TICKS(delay_ms) > 0 ? pdMS_TO_TICKS(delay_ms) : 1);
}

se05x_esp_i2c_config_t se05x_esp_i2c_default_config(void)
{
    se05x_esp_i2c_config_t config = {
        .port = CONFIG_SE05X_I2C_PORT,
        .sda_gpio = CONFIG_SE05X_I2C_SDA_GPIO,
        .scl_gpio = CONFIG_SE05X_I2C_SCL_GPIO,
        .address = CONFIG_SE05X_I2C_ADDR,
        .clock_hz = CONFIG_SE05X_I2C_FREQ_HZ,
        .timeout_ms = CONFIG_SE05X_I2C_TIMEOUT_MS,
        .enable_internal_pullups = SE05X_ENABLE_INTERNAL_PULLUPS,
    };
    return config;
}

int se05x_esp_i2c_bus_create(se05x_bus_t *bus, const se05x_esp_i2c_config_t *config)
{
    if (bus == NULL) {
        return -1;
    }

    se05x_esp_i2c_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return -1;
    }

    ctx->config = config != NULL ? *config : se05x_esp_i2c_default_config();
    bus->ops.open = esp_i2c_open;
    bus->ops.close = esp_i2c_close;
    bus->ops.write = esp_i2c_write;
    bus->ops.read = esp_i2c_read;
    bus->ops.delay_ms = esp_i2c_delay_ms;
    bus->ops.ctx = ctx;
    return 0;
}
