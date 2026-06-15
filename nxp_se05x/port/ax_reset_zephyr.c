#include "ax_reset.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "fsl_sss_ftr.h"
#include "se05x_const.h"
#include "sm_timer.h"

LOG_MODULE_REGISTER(se05x_reset, LOG_LEVEL_INF);

#define SE05X_RESET_NODE DT_ALIAS(se05x_reset)

#if DT_NODE_EXISTS(SE05X_RESET_NODE)
static const struct gpio_dt_spec reset_gpio = GPIO_DT_SPEC_GET(SE05X_RESET_NODE, gpios);
#endif

static bool reset_gpio_is_configured(void)
{
#if DT_NODE_EXISTS(SE05X_RESET_NODE)
	return gpio_is_ready_dt(&reset_gpio);
#else
	return false;
#endif
}

void axReset_HostConfigure(void)
{
#if DT_NODE_EXISTS(SE05X_RESET_NODE)
	if (!reset_gpio_is_configured()) {
		LOG_WRN("SE05x reset GPIO is not ready");
		return;
	}

	(void)gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_ACTIVE);
	axReset_PowerUp();
#endif
}

void axReset_HostUnconfigure(void)
{
#if DT_NODE_EXISTS(SE05X_RESET_NODE)
	if (reset_gpio_is_configured()) {
		(void)gpio_pin_configure_dt(&reset_gpio, GPIO_DISCONNECTED);
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
#if DT_NODE_EXISTS(SE05X_RESET_NODE)
	if (reset_gpio_is_configured()) {
		(void)gpio_pin_set_dt(&reset_gpio, !SE_RESET_LOGIC);
	}
#endif
}

void axReset_PowerUp(void)
{
#if DT_NODE_EXISTS(SE05X_RESET_NODE)
	if (reset_gpio_is_configured()) {
		(void)gpio_pin_set_dt(&reset_gpio, SE_RESET_LOGIC);
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
