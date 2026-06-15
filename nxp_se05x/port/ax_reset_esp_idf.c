#include "ax_reset.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "fsl_sss_ftr.h"
#include "sdkconfig.h"
#include "sm_timer.h"

#ifndef CONFIG_SE05X_RST_GPIO
#define CONFIG_SE05X_RST_GPIO -1
#endif

static bool reset_gpio_is_configured(void)
{
#if CONFIG_SE05X_RST_GPIO >= 0
    return CONFIG_SE05X_RST_GPIO >= 0;
#else
    return false;
#endif
}

void axReset_HostConfigure(void)
{
#if CONFIG_SE05X_RST_GPIO >= 0
    if (!reset_gpio_is_configured()) {
        return;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << CONFIG_SE05X_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&config);
    axReset_PowerUp();
#endif
}

void axReset_HostUnconfigure(void)
{
#if CONFIG_SE05X_RST_GPIO >= 0
    if (reset_gpio_is_configured()) {
        gpio_reset_pin(CONFIG_SE05X_RST_GPIO);
    }
#endif
}

void axReset_ResetPluseDUT(void)
{
    axReset_PowerDown();
    sm_usleep(2000);
    axReset_PowerUp();
}

void axReset_PowerDown(void)
{
#if CONFIG_SE05X_RST_GPIO >= 0
    if (reset_gpio_is_configured()) {
        gpio_set_level(CONFIG_SE05X_RST_GPIO, !SE_RESET_LOGIC);
    }
#endif
}

void axReset_PowerUp(void)
{
#if CONFIG_SE05X_RST_GPIO >= 0
    if (reset_gpio_is_configured()) {
        gpio_set_level(CONFIG_SE05X_RST_GPIO, SE_RESET_LOGIC);
    }
#endif
}

#if SSS_HAVE_APPLET_SE05X_IOT || SSS_HAVE_APPLET_LOOPBACK
void se05x_ic_reset(void)
{
    axReset_ResetPluseDUT();
    sm_usleep(3000);
}
#endif
