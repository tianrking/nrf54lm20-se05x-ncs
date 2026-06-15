#include "sm_timer.h"

#include <zephyr/kernel.h>

uint32_t sm_initSleep(void)
{
	return 0;
}

void sm_sleep(uint32_t msec)
{
	k_msleep(msec > 0U ? msec : 1U);
}

void sm_usleep(uint32_t microsec)
{
	k_busy_wait(microsec);
}
