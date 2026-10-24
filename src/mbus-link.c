#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbus-link.h"
#include "mbus-serial.h"

int
mbus_link_wakeup(mbus_handle *handle, const mbus_link_config_t *cfg)
{
    mbus_serial_data *sd;
    uint8_t *burst;
    uart_parity_t saved;
    int ret = -1;

    if (handle == NULL || cfg == NULL || !handle->is_serial || cfg->wakeup_count == 0)
        return -1;

    sd = (mbus_serial_data *)handle->auxdata;
    if (sd == NULL)
        return -1;

    if ((burst = (uint8_t *)malloc(cfg->wakeup_count)) == NULL)
        return -1;
    memset(burst, cfg->wakeup_byte, cfg->wakeup_count);

    saved = sd->cfg.parity;

    /* burst goes out as 8N1 */
    if (mbus_serial_set_parity(handle, UART_PARITY_DISABLE) == 0 &&
        mbus_serial_write_raw(handle, burst, cfg->wakeup_count) == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(cfg->wakeup_pause_ms));
        ret = 0;
    }

    /* back to the frame format; the next send_frame flushes the burst echo */
    mbus_serial_set_parity(handle, saved);
    free(burst);
    return ret;
}

int
mbus_link_ping(mbus_handle *handle, int address,
               const mbus_link_config_t *cfg, mbus_link_mode_t mode)
{
    mbus_frame reply;
    int r;

    if (handle == NULL || cfg == NULL)
        return -1;

    if (mode == MBUS_LINK_MODE_WAKEUP && mbus_link_wakeup(handle, cfg) != 0)
        return -1;

    if (mbus_send_ping_frame(handle, address, 0) != 0)
        return -1;

    if (cfg->settle_ms)
        vTaskDelay(pdMS_TO_TICKS(cfg->settle_ms));

    /* recv drops our own echo (purge_first_frame) and returns the ACK */
    memset(&reply, 0, sizeof(reply));
    r = mbus_recv_frame(handle, &reply);

    return (r == MBUS_RECV_RESULT_OK && mbus_frame_type(&reply) == MBUS_FRAME_TYPE_ACK) ? 0 : -1;
}

mbus_link_mode_t
mbus_link_detect(mbus_handle *handle, int address, const mbus_link_config_t *cfg)
{
    if (mbus_link_ping(handle, address, cfg, MBUS_LINK_MODE_DIRECT) == 0)
        return MBUS_LINK_MODE_DIRECT;

    if (cfg != NULL)
        vTaskDelay(pdMS_TO_TICKS(cfg->detect_gap_ms));

    if (mbus_link_ping(handle, address, cfg, MBUS_LINK_MODE_WAKEUP) == 0)
        return MBUS_LINK_MODE_WAKEUP;

    return MBUS_LINK_MODE_UNKNOWN;
}