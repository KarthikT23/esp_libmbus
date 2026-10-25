/* host-only stand-in, just what mbus-link.c needs */
#ifndef HOST_STUB_FREERTOS_H
#define HOST_STUB_FREERTOS_H

typedef unsigned int TickType_t;
#define pdMS_TO_TICKS(ms)  ((TickType_t)(ms))

#endif
