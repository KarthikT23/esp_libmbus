#ifndef HOST_STUB_FREERTOS_TASK_H
#define HOST_STUB_FREERTOS_TASK_H

#include "FreeRTOS.h"

/* advances the virtual clock in host_port.c */
void vTaskDelay(TickType_t ticks);

/* declarations only, for the docs snippet check (never linked) */
typedef void *TaskHandle_t;
typedef int BaseType_t;
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
                       void *arg, unsigned prio, TaskHandle_t *out);
void vTaskDelete(TaskHandle_t task);

#endif
