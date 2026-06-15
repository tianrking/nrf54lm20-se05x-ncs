#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct se05x_bus se05x_bus_t;

typedef struct {
    int (*open)(se05x_bus_t *bus);
    void (*close)(se05x_bus_t *bus);
    int (*write)(se05x_bus_t *bus, uint8_t address, const uint8_t *data, size_t data_len);
    int (*read)(se05x_bus_t *bus, uint8_t address, uint8_t *data, size_t data_len);
    void (*delay_ms)(uint32_t delay_ms);
    void *ctx;
} se05x_bus_ops_t;

struct se05x_bus {
    se05x_bus_ops_t ops;
};

int se05x_bus_register_default(const se05x_bus_ops_t *ops);
void se05x_bus_clear_default(void);
const se05x_bus_ops_t *se05x_bus_get_default(void);

#ifdef __cplusplus
}
#endif
