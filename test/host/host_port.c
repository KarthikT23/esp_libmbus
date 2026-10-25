/* Host stand-ins for the parts of the component that need the target:
 * virtual clock (FreeRTOS delay, meter port) and the serial helpers the meter
 * and link layer call. The "serial" handle is a fake_slave bus (userdata) plus
 * a plain mbus_serial_data in auxdata. */
#include <string.h>
#include "fake_slave.h"
#include "mbus-serial.h"
#include "freertos/task.h"

uint32_t g_now_ms;

void vTaskDelay(TickType_t t)                { g_now_ms += (uint32_t)t; }
uint32_t mbus_meter_port_now_ms(void)        { return g_now_ms; }
void mbus_meter_port_delay_ms(uint32_t ms)   { g_now_ms += ms; }

static fs_bus *bus_of(mbus_handle *h)        { return (fs_bus *)h->userdata; }
static mbus_serial_data *sd_of(mbus_handle *h) { return (mbus_serial_data *)h->auxdata; }

int mbus_serial_set_parity(mbus_handle *h, uart_parity_t parity)
{
    sd_of(h)->cfg.parity = parity;
    return 0;
}

int mbus_serial_write_raw(mbus_handle *h, const void *data, size_t len)
{
    (void)data; (void)len;
    fs_wakeup(bus_of(h));
    return 0;
}

int mbus_serial_set_baudrate(mbus_handle *h, long baud)
{
    sd_of(h)->cfg.baudrate = (int)baud;
    bus_of(h)->baud = baud;
    return 0;
}

unsigned g_to_sets;
int      g_to_first_hist[16], g_to_gap_hist[16];

int mbus_serial_set_rx_timeouts(mbus_handle *h, int first, int gap)
{
    if (g_to_sets < 16) {
        g_to_first_hist[g_to_sets] = first;
        g_to_gap_hist[g_to_sets] = gap;
    }
    g_to_sets++;
    sd_of(h)->rx_first_ms = first;
    sd_of(h)->rx_gap_ms = gap;
    return 0;
}

int mbus_serial_get_rx_timeouts(mbus_handle *h, int *first, int *gap)
{
    *first = sd_of(h)->rx_first_ms;
    *gap = sd_of(h)->rx_gap_ms;
    return 0;
}

int mbus_serial_flush_rx(mbus_handle *h)
{
    fs_bus *b = bus_of(h);

    b->flushes++;
    b->rx_head = b->rx_tail;
    return 0;
}

/* handle that looks serial to the meter code */
mbus_handle *fs_serial_handle(fs_bus *b, mbus_serial_data *sd, long baud)
{
    mbus_handle *h = fs_handle(b);

    if (h == NULL)
        return NULL;
    memset(sd, 0, sizeof(*sd));
    sd->cfg.baudrate = (int)baud;
    sd->cfg.parity = UART_PARITY_EVEN;
    b->baud = baud;
    h->is_serial = 1;
    h->auxdata = sd;
    return h;
}
