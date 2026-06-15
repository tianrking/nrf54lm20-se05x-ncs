#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* NXP headers may define ARRAY_SIZE before pulling this RTOS shim in. */
#ifdef ARRAY_SIZE
#undef ARRAY_SIZE
#endif

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY K_FOREVER
#define pdMS_TO_TICKS(ms) (ms)

typedef int BaseType_t;
typedef uint32_t TickType_t;

typedef struct {
	struct k_mutex mutex;
} se05x_zephyr_mutex_t;

typedef se05x_zephyr_mutex_t *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
	SemaphoreHandle_t handle = (SemaphoreHandle_t)malloc(sizeof(*handle));

	if (handle != NULL) {
		k_mutex_init(&handle->mutex);
	}

	return handle;
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, k_timeout_t timeout)
{
	if (handle == NULL) {
		return pdFALSE;
	}

	return k_mutex_lock(&handle->mutex, timeout) == 0 ? pdTRUE : pdFALSE;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t handle)
{
	if (handle == NULL) {
		return pdFALSE;
	}

	return k_mutex_unlock(&handle->mutex) == 0 ? pdTRUE : pdFALSE;
}

static inline void vSemaphoreDelete(SemaphoreHandle_t handle)
{
	free(handle);
}

static inline void vTaskDelay(TickType_t ticks)
{
	k_msleep(ticks > 0U ? ticks : 1U);
}

#ifdef __cplusplus
}
#endif
