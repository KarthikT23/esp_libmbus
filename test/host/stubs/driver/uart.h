/* host-only stand-in for ESP-IDF driver/uart.h, just the types the config header needs */
#ifndef HOST_STUB_DRIVER_UART_H
#define HOST_STUB_DRIVER_UART_H

typedef int uart_port_t;
typedef int uart_word_length_t;
typedef int uart_parity_t;
typedef int uart_stop_bits_t;

#define UART_NUM_1          1
#define UART_PIN_NO_CHANGE  (-1)
#define UART_DATA_8_BITS    3
#define UART_PARITY_DISABLE 0
#define UART_PARITY_EVEN    2
#define UART_STOP_BITS_1    1

#endif
