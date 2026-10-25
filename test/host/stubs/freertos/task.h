#ifndef HOST_STUB_FREERTOS_TASK_H
#define HOST_STUB_FREERTOS_TASK_H

#include "FreeRTOS.h"

/* advances the virtual clock in host_port.c */
void vTaskDelay(TickType_t ticks);

#endif
