#ifndef MBUS_LINK_H
#define MBUS_LINK_H

#include <stdint.h>
#include "mbus-protocol-aux.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MBUS_LINK_MODE_UNKNOWN = 0,
    MBUS_LINK_MODE_DIRECT,    /* slave answers a plain ping */
    MBUS_LINK_MODE_WAKEUP     /* slave needs the wake-up burst first */
} mbus_link_mode_t;

typedef struct {
    uint8_t  wakeup_byte;      /* usually 0x55 */
    uint16_t wakeup_count;     /* keep below the UART rx_buf_size if the transceiver echoes */
    uint32_t wakeup_pause_ms;  /* wait after the burst, before the ping */
    uint32_t settle_ms;        /* ping TX -> RX delay, for slow transceivers */
    uint32_t detect_gap_ms;    /* pause between the two attempts in detect */
} mbus_link_config_t;

#define MBUS_LINK_CONFIG_DEFAULT() { \
    .wakeup_byte = 0x55,             \
    .wakeup_count = 530,             \
    .wakeup_pause_ms = 1500,         \
    .settle_ms = 50,                 \
    .detect_gap_ms = 500,            \
}

/* Serial handles only. One bus per task, the library is not thread-safe. */

/* 8N1 wake-up burst, then back to the configured frame format. 0 = ok. */
int mbus_link_wakeup(mbus_handle *handle, const mbus_link_config_t *cfg);

/* Ping (SND_NKE), optionally preceded by the burst. 0 = ACK received. */
int mbus_link_ping(mbus_handle *handle, int address,
                   const mbus_link_config_t *cfg, mbus_link_mode_t mode);

/* Direct first, then with wake-up. UNKNOWN if neither gets an ACK. */
mbus_link_mode_t mbus_link_detect(mbus_handle *handle, int address,
                                  const mbus_link_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* MBUS_LINK_H */