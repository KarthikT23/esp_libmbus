#ifndef MBUS_SERIAL_CONFIG_H
#define MBUS_SERIAL_CONFIG_H

#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uart_port_t port;     /* e.g. UART_NUM_1 */
    int tx_pin;           /* GPIO number, or UART_PIN_NO_CHANGE */
    int rx_pin;           /* GPIO number, or UART_PIN_NO_CHANGE */
    int baudrate;         /* M-Bus default: 2400 */
    int rx_buf_size;      /* driver RX ring buffer, bytes (>= 256) */
    uart_word_length_t data_bits;  /* M-Bus: 8 */
    uart_parity_t parity;          /* M-Bus: even */
    uart_stop_bits_t stop_bits;    /* M-Bus: 1 */
    int rx_timeout_ms;             /* per-read timeout, 0 = derive from baudrate */
} mbus_serial_config_t;

#define MBUS_SERIAL_CONFIG_DEFAULT() { \
    .port = UART_NUM_1,                \
    .tx_pin = UART_PIN_NO_CHANGE,      \
    .rx_pin = UART_PIN_NO_CHANGE,      \
    .baudrate = 2400,                  \
    .rx_buf_size = 1024,               \
    .data_bits = UART_DATA_8_BITS,     \
    .parity = UART_PARITY_EVEN,        \
    .stop_bits = UART_STOP_BITS_1,     \
    .rx_timeout_ms = 0,                \
}

#ifdef __cplusplus
}
#endif

#endif /* MBUS_SERIAL_CONFIG_H */