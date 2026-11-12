#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include <stdbool.h>
#include <stdint.h>
#include "unity.h"
#include "unity_test_runner.h"
#include "mbus.h"

#define TB_UART_PORT UART_NUM_1

/* test frames, hex with spaces */
extern const char TB_FRAME_SMALL[];   /* CI 0x78, addr 1, one volume record 305419.896 m^3 */
extern const char TB_FRAME_CI7A[];    /* CI 0x7A, addr 1, same record */
extern const char TB_FRAME_HDR[];     /* CI 0x72, addr 0, header + same record */
extern const char TB_FRAME_HDR_BAD[]; /* TB_FRAME_HDR with a wrong checksum */
extern const char TB_FRAME_BIG[];     /* 167 bytes, addr 0, 25 records */

#define TB_BIG_RECORDS 25
#define TB_HDR_ADDR    "24083345C5140004"   /* secondary address in TB_FRAME_HDR / BIG */

/* Opens a serial handle on the configured pins. Closed by tearDown at the latest. */
mbus_handle *tb_open(void);
void tb_close(void);
uint32_t tb_now_ms(void);

/* Scripted slave: raw bytes written onto the wire at fixed times after
 * tb_script_start(). The master sees them as replies (loopback). */
void tb_script_reset(mbus_handle *h);
void tb_script_add(const char *hex, uint32_t at_ms);
void tb_script_start(void);
bool tb_script_wait(uint32_t timeout_ms);   /* true when all steps were sent */

#endif
