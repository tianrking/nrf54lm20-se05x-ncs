#include "se05x_bus.h"

#include <stdbool.h>
#include <string.h>

static se05x_bus_ops_t s_default_bus;
static bool s_default_bus_registered;

int se05x_bus_register_default(const se05x_bus_ops_t *ops)
{
    if (ops == NULL || ops->write == NULL || ops->read == NULL) {
        return -1;
    }

    s_default_bus = *ops;
    s_default_bus_registered = true;
    return 0;
}

void se05x_bus_clear_default(void)
{
    memset(&s_default_bus, 0, sizeof(s_default_bus));
    s_default_bus_registered = false;
}

const se05x_bus_ops_t *se05x_bus_get_default(void)
{
    return s_default_bus_registered ? &s_default_bus : NULL;
}
