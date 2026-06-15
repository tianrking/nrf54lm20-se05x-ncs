#include "i2c_a7.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "nxLog_smCom.h"
#include "sdkconfig.h"
#include "se05x_bus.h"
#include "se05x_bus_esp_idf.h"

typedef struct {
    se05x_bus_t bus;
    uint8_t address;
    bool owns_bus;
} se05x_i2c_conn_t;

static int s_backoff_ms;

static void backoff_delay(se05x_i2c_conn_t *conn)
{
    if (s_backoff_ms < 200) {
        s_backoff_ms += 1;
    }
    if (conn != NULL && conn->bus.ops.delay_ms != NULL) {
        conn->bus.ops.delay_ms((uint32_t)s_backoff_ms);
    }
}

static void reset_backoff(void)
{
    s_backoff_ms = 0;
}

static uint8_t parse_address(const char *connection_string)
{
    if (connection_string == NULL || strcmp(connection_string, "none") == 0) {
        return CONFIG_SE05X_I2C_ADDR;
    }

    const char *addr = strchr(connection_string, ':');
    if (addr == NULL) {
        addr = connection_string;
    }
    else {
        addr++;
    }

    char *end = NULL;
    unsigned long parsed = strtoul(addr, &end, 0);
    if (end != addr && parsed <= 0x7f) {
        return (uint8_t)parsed;
    }
    return CONFIG_SE05X_I2C_ADDR;
}

i2c_error_t axI2CInit(void **conn_ctx, const char *pDevName)
{
    se05x_i2c_conn_t *conn = calloc(1, sizeof(*conn));
    if (conn == NULL) {
        return I2C_FAILED;
    }

    conn->address = parse_address(pDevName);

    const se05x_bus_ops_t *registered_bus = se05x_bus_get_default();
    if (registered_bus != NULL) {
        conn->bus.ops = *registered_bus;
        conn->owns_bus = false;
    }
    else {
        se05x_esp_i2c_config_t config = se05x_esp_i2c_default_config();
        config.address = conn->address;
        if (se05x_esp_i2c_bus_create(&conn->bus, &config) != 0) {
            free(conn);
            return I2C_FAILED;
        }
        conn->owns_bus = true;
    }

    if (conn->bus.ops.open != NULL && conn->bus.ops.open(&conn->bus) != 0) {
        if (conn->owns_bus && conn->bus.ops.ctx != NULL) {
            free(conn->bus.ops.ctx);
        }
        free(conn);
        return I2C_FAILED;
    }

    if (conn_ctx != NULL) {
        *conn_ctx = conn;
    }
    return I2C_OK;
}

void axI2CTerm(void *conn_ctx, int mode)
{
    AX_UNUSED_ARG(mode);
    se05x_i2c_conn_t *conn = (se05x_i2c_conn_t *)conn_ctx;
    if (conn == NULL) {
        return;
    }

    if (conn->bus.ops.close != NULL) {
        conn->bus.ops.close(&conn->bus);
    }
    if (conn->owns_bus && conn->bus.ops.ctx != NULL) {
        free(conn->bus.ops.ctx);
    }
    free(conn);
}

void axI2CResetBackoffDelay(void)
{
    reset_backoff();
}

i2c_error_t axI2CWrite(void *conn_ctx, unsigned char bus, unsigned char addr, unsigned char *pTx, unsigned short txLen)
{
    AX_UNUSED_ARG(addr);
    se05x_i2c_conn_t *conn = (se05x_i2c_conn_t *)conn_ctx;
    if (conn == NULL || pTx == NULL || txLen == 0 || txLen > MAX_DATA_LEN) {
        return I2C_FAILED;
    }
    if (bus != I2C_BUS_0) {
        LOG_E("axI2CWrite on wrong bus %u", bus);
    }

    LOG_MAU8_D("TX (axI2CWrite) > ", pTx, txLen);
    if (conn->bus.ops.write(&conn->bus, conn->address, pTx, txLen) == 0) {
        reset_backoff();
        return I2C_OK;
    }
    backoff_delay(conn);
    return I2C_FAILED;
}

i2c_error_t axI2CRead(void *conn_ctx, unsigned char bus, unsigned char addr, unsigned char *pRx, unsigned short rxLen)
{
    AX_UNUSED_ARG(addr);
    se05x_i2c_conn_t *conn = (se05x_i2c_conn_t *)conn_ctx;
    if (conn == NULL || pRx == NULL || rxLen == 0 || rxLen > MAX_DATA_LEN) {
        return I2C_FAILED;
    }
    if (bus != I2C_BUS_0) {
        LOG_E("axI2CRead on wrong bus %u", bus);
    }

    if (conn->bus.ops.read(&conn->bus, conn->address, pRx, rxLen) == 0) {
        reset_backoff();
        LOG_MAU8_D("RX (axI2CRead) < ", pRx, rxLen);
        return I2C_OK;
    }
    backoff_delay(conn);
    return I2C_FAILED;
}
