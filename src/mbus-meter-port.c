#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbus-meter-internal.h"

/* wraps after ~49 days, callers only use unsigned differences */
uint32_t
mbus_meter_port_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void
mbus_meter_port_delay_ms(uint32_t ms)
{
    if (ms == 0)
        return;
    vTaskDelay(pdMS_TO_TICKS(ms));
}
