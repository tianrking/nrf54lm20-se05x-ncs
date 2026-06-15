#include "sm_timer.h"

#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint32_t sm_initSleep(void)
{
    return 0;
}

void sm_sleep(uint32_t msec)
{
    vTaskDelay(pdMS_TO_TICKS(msec) > 0 ? pdMS_TO_TICKS(msec) : 1);
}

void sm_usleep(uint32_t microsec)
{
    esp_rom_delay_us(microsec);
}
