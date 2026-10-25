//------------------------------------------------------------------------------
// Copyright (C) 2011, Robert Johansson, Raditex AB
// All rights reserved.
//
// rSCADA
// http://www.rSCADA.se
// info@rscada.se
//
//------------------------------------------------------------------------------


#include <limits.h>
#include <sys/types.h>

#include <stdio.h>
#include <strings.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "mbus-serial.h"
#include "mbus-protocol-aux.h"
#include "mbus-protocol.h"

#define PACKET_BUFF_SIZE 512   /* max telegram is 261 bytes */

//------------------------------------------------------------------------------
/// Set up a serial connection handle.
//------------------------------------------------------------------------------
int
mbus_serial_connect(mbus_handle *handle)
{
    mbus_serial_data *sd;
    char error_str[128];

    if (handle == NULL || (sd = (mbus_serial_data *)handle->auxdata) == NULL)
        return -1;

    const uart_config_t uc = {
        .baud_rate  = sd->cfg.baudrate,
        .data_bits  = sd->cfg.data_bits,
        .parity     = sd->cfg.parity,
        .stop_bits  = sd->cfg.stop_bits,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_driver_install(sd->cfg.port, sd->cfg.rx_buf_size, 0, 0, NULL, 0) != ESP_OK ||
        uart_param_config(sd->cfg.port, &uc) != ESP_OK ||
        uart_set_pin(sd->cfg.port, sd->cfg.tx_pin, sd->cfg.rx_pin,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
    {
        snprintf(error_str, sizeof(error_str), "%s: UART setup failed\n", __PRETTY_FUNCTION__);
        mbus_error_str_set(error_str);
        if (uart_is_driver_installed(sd->cfg.port)) uart_driver_delete(sd->cfg.port);
        return -1;
    }
    /* default threshold is 120 bytes: a continuous frame stays in the hw fifo
       and uart_read_bytes sees nothing until it is half over. 1 = per byte. */
    uart_set_rx_full_threshold(sd->cfg.port, 1);

    sd->installed = true;
    uart_flush_input(sd->cfg.port);
    return 0;
}

//------------------------------------------------------------------------------
// Set baud rate for serial connection
//------------------------------------------------------------------------------
int
mbus_serial_set_baudrate(mbus_handle *handle, long baudrate)
{
    mbus_serial_data *sd;

    if (handle == NULL || (sd = (mbus_serial_data *)handle->auxdata) == NULL)
        return -1;

    if (uart_set_baudrate(sd->cfg.port, (uint32_t)baudrate) != ESP_OK)
        return -1;

    sd->cfg.baudrate = (int)baudrate;
    return 0;
}

int
mbus_serial_set_parity(mbus_handle *handle, uart_parity_t parity)
{
    mbus_serial_data *sd;

    if (handle == NULL || (sd = (mbus_serial_data *)handle->auxdata) == NULL || !sd->installed)
        return -1;

    if (uart_set_parity(sd->cfg.port, parity) != ESP_OK)
        return -1;

    sd->cfg.parity = parity;
    return 0;
}

/* raw bytes outside M-Bus framing, e.g. wake-up burst */
int
mbus_serial_write_raw(mbus_handle *handle, const void *data, size_t len)
{
    mbus_serial_data *sd;

    if (handle == NULL || data == NULL || len == 0 ||
        (sd = (mbus_serial_data *)handle->auxdata) == NULL || !sd->installed)
        return -1;

    if (uart_write_bytes(sd->cfg.port, data, len) != (int)len)
        return -1;

    /* 12 bit times per byte covers start, 8 data, parity, stop with margin */
    uint32_t wait_ms = (uint32_t)((uint64_t)len * 12 * 1000 / sd->cfg.baudrate) + 100;
    uart_wait_tx_done(sd->cfg.port, pdMS_TO_TICKS(wait_ms));
    return 0;
}

/* 0 = derive from cfg.rx_timeout_ms / baudrate. A non-zero first_byte_ms also makes
    * "no answer" cost one wait instead of three. */
int
mbus_serial_set_rx_timeouts(mbus_handle *handle, int first_byte_ms, int gap_ms)
{
    mbus_serial_data *sd;

    if (handle == NULL || (sd = (mbus_serial_data *)handle->auxdata) == NULL ||
        first_byte_ms < 0 || gap_ms < 0)
        return -1;

    sd->rx_first_ms = first_byte_ms;
    sd->rx_gap_ms = gap_ms;
    return 0;
}

int
mbus_serial_get_rx_timeouts(mbus_handle *handle, int *first_byte_ms, int *gap_ms)
{
    mbus_serial_data *sd;

    if (handle == NULL || (sd = (mbus_serial_data *)handle->auxdata) == NULL ||
        first_byte_ms == NULL || gap_ms == NULL)
        return -1;

    *first_byte_ms = sd->rx_first_ms;
    *gap_ms = sd->rx_gap_ms;
    return 0;
}

int
mbus_serial_flush_rx(mbus_handle *handle)
{
    mbus_serial_data *sd;

    if (handle == NULL || (sd = (mbus_serial_data *)handle->auxdata) == NULL || !sd->installed)
        return -1;

    return uart_flush_input(sd->cfg.port) == ESP_OK ? 0 : -1;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int
mbus_serial_disconnect(mbus_handle *handle)
{
    mbus_serial_data *sd;

    if (handle == NULL || (sd = (mbus_serial_data *)handle->auxdata) == NULL)
        return -1;

    if (sd->installed)
    {
        uart_driver_delete(sd->cfg.port);
        sd->installed = false;
    }
    return 0;
}

void
mbus_serial_data_free(mbus_handle *handle)
{
    if (handle)
    {
        mbus_serial_data *sd = (mbus_serial_data *)handle->auxdata;
        if (sd)
        {
            if (sd->installed) uart_driver_delete(sd->cfg.port);
            free(sd);
            handle->auxdata = NULL;
        }
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int
mbus_serial_send_frame(mbus_handle *handle, mbus_frame *frame)
{
    unsigned char buff[PACKET_BUFF_SIZE];
    int len, ret;
    mbus_serial_data *sd;

    if (handle == NULL || frame == NULL ||
        (sd = (mbus_serial_data *)handle->auxdata) == NULL || !sd->installed)
    {
        return -1;
    }

    if ((len = mbus_frame_pack(frame, buff, sizeof(buff))) < 0)
    {
        fprintf(stderr, "%s: mbus_frame_pack failed\n", __PRETTY_FUNCTION__);
        return -1;
    }

#ifdef MBUS_SERIAL_DEBUG
    // if debug, dump in HEX form to stdout what we write to the serial port
    printf("%s: Dumping M-Bus frame [%d bytes]: ", __PRETTY_FUNCTION__, len);
    int i;
    for (i = 0; i < len; i++)
    {
       printf("%.2X ", buff[i]);
    }
    printf("\n");
#endif

    uart_flush_input(sd->cfg.port);   /* drop stale bytes before a new request */
    if ((ret = uart_write_bytes(sd->cfg.port, buff, len)) == len)
    {
        //
        // call the send event function, if the callback function is registered
        //
        if (handle->send_event)
                handle->send_event(MBUS_HANDLE_TYPE_SERIAL, buff, len);
    }
    else
    {
        fprintf(stderr, "%s: Failed to write frame to UART (ret = %d)\n", __PRETTY_FUNCTION__, ret);
        return -1;
    }

    //
    // wait until complete frame has been transmitted
    //
    /* bound by line time (12 bit times per byte + 20 ms): a missed tx-done
     interrupt must not cost 500 ms */
    uart_wait_tx_done(sd->cfg.port,
                    pdMS_TO_TICKS((uint32_t)((uint64_t)len * 12 * 1000 / sd->cfg.baudrate) + 20));

    return 0;
}

static int
mbus_serial_timeout_ms(int baud)
{
    if (baud <= 300)  return 1300;
    if (baud <= 600)  return 800;
    if (baud <= 1200) return 500;
    if (baud <= 4800) return 300;
    return 200;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int
mbus_serial_recv_frame(mbus_handle *handle, mbus_frame *frame)
{
    char buff[PACKET_BUFF_SIZE];
    int remaining, timeouts;
    ssize_t len, nread;

    if (handle == NULL || frame == NULL)
    {
        fprintf(stderr, "%s: Invalid parameter.\n", __PRETTY_FUNCTION__);
        return MBUS_RECV_RESULT_ERROR;
    }

    mbus_serial_data *sd = (mbus_serial_data *)handle->auxdata;
    if (sd == NULL || !sd->installed)
    {
        fprintf(stderr, "%s: Serial connection is not available.\n", __PRETTY_FUNCTION__);
        return MBUS_RECV_RESULT_ERROR;
    }
    const int rd_ms = sd->cfg.rx_timeout_ms > 0 ? sd->cfg.rx_timeout_ms
                                                : mbus_serial_timeout_ms(sd->cfg.baudrate);
    const TickType_t first_timeout = pdMS_TO_TICKS(sd->rx_first_ms > 0 ? sd->rx_first_ms : rd_ms);
    const TickType_t gap_timeout = pdMS_TO_TICKS(sd->rx_gap_ms > 0 ? sd->rx_gap_ms : rd_ms);

    memset((void *)buff, 0, sizeof(buff));

    //
    // read data until a packet is received
    //
    remaining = 1; // start by reading 1 byte
    len = 0;
    timeouts = 0;

    do {
        if (len + remaining > PACKET_BUFF_SIZE)
        {
            // avoid out of bounds access
            return MBUS_RECV_RESULT_ERROR;
        }

        //printf("%s: Attempt to read %d bytes [len = %d]\n", __PRETTY_FUNCTION__, remaining, len);

        if ((nread =    uart_read_bytes(sd->cfg.port, &buff[len], remaining,
                                         len == 0 ? first_timeout : gap_timeout)) < 0)
        {
       //     fprintf(stderr, "%s: aborting recv frame (remaining = %d, len = %d, nread = %d)\n",
         //          __PRETTY_FUNCTION__, remaining, len, nread);
            return MBUS_RECV_RESULT_ERROR;
        }

//   printf("%s: Got %d byte [remaining %d, len %d]\n", __PRETTY_FUNCTION__, nread, remaining, len);

        if (nread == 0)
        {
            timeouts++;

        /* explicit first-byte wait: silence ends the receive at once */
        if (timeouts >= 3 || (len == 0 && sd->rx_first_ms > 0))
            {
                // abort to avoid endless loop
                fprintf(stderr, "%s: Timeout\n", __PRETTY_FUNCTION__);
                break;
            }
        }
        len += nread;

    } while ((remaining = mbus_parse(frame, buff, len)) > 0);

    if (len == 0)
    {
        // No data received
        return MBUS_RECV_RESULT_TIMEOUT;
    }

    //
    // call the receive event function, if the callback function is registered
    //
    if (handle->recv_event)
        handle->recv_event(MBUS_HANDLE_TYPE_SERIAL, buff, len);

    if (remaining != 0)
    {
        // Would be OK when e.g. scanning the bus, otherwise it is a failure.
        // printf("%s: M-Bus layer failed to receive complete data.\n", __PRETTY_FUNCTION__);
        return MBUS_RECV_RESULT_INVALID;
    }

    if (len == -1)
    {
        fprintf(stderr, "%s: M-Bus layer failed to parse data.\n", __PRETTY_FUNCTION__);
        return MBUS_RECV_RESULT_ERROR;
    }

    return MBUS_RECV_RESULT_OK;
}
