/* In-memory M-Bus bus with scripted slaves, for host tests of the master side. */
#ifndef FAKE_SLAVE_H
#define FAKE_SLAVE_H

#include <stddef.h>
#include <stdint.h>
#include "mbus-protocol.h"
#include "mbus-protocol-aux.h"

#define FS_MAX_SLAVES  8
#define FS_MAX_TG      8
#define FS_TG_MAX      300
#define FS_RXQ_SIZE    4096
#define FS_LOG_MAX     2048

typedef enum {
    FS_FCB_LENIENT = 0,   /* answers by request count, ignores FCB */
    FS_FCB_STRICT         /* FCB != last -> next frame, FCB == last -> repeat */
} fs_fcb_mode;

typedef struct {
    uint8_t  tg[FS_MAX_TG][FS_TG_MAX];
    size_t   tg_len[FS_MAX_TG];
    int      n_tg;

    uint8_t  reduced[FS_TG_MAX];   /* answer to a "repeat" with nothing sent yet */
    size_t   reduced_len;

    int      primary;              /* -1 = secondary only */
    char     sec[17];              /* IIIIIIIIMMMMVVTT, "" = none */
    fs_fcb_mode fcb_mode;
    int      silent_requests;      /* ignore the next N REQ_UD2 */
    int      repeat_last;          /* stay on the last telegram instead of wrapping */
    int      select_resets_fcb;    /* default 1 */
    unsigned silent_after;         /* ignore REQ_UD2 once this many were answered, 0 = off */
    int      needs_wakeup;         /* deaf until a wake-up burst was seen */
    int      awake;

    /* state */
    int      idx;                  /* next telegram */
    int      last_fcb;
    int      sent_any;
    int      last_sent;            /* -1 = reduced */
    int      selected;
    unsigned requests;
} fs_slave;

typedef struct {
    uint8_t  type, control, address, ci;
} fs_log_entry;

typedef struct {
    fs_slave slave[FS_MAX_SLAVES];
    int      n_slaves;

    uint8_t  rxq[FS_RXQ_SIZE];
    size_t   rx_head, rx_tail;

    int      echo;                 /* converter echoes master frames */
    int      sec_bytewise;         /* id compared per byte only, a lone F nibble is no wildcard */
    long     jam_left;             /* endless 0xFF source, bytes left */
    int      corrupt_next;         /* 1 = bad checksum, 2 = truncated */

    fs_log_entry log[FS_LOG_MAX];
    int      n_log;
    unsigned recv_calls;

    long     baud;                 /* set by the serial stub */
    long     slave_baud;           /* slaves only hear this baud, 0 = any */
    unsigned wakeups;              /* wake-up bursts seen */
    unsigned flushes;              /* rx flush calls */
} fs_bus;

void   fs_init(fs_bus *b);
void   fs_wakeup(fs_bus *b);               /* wake-up burst arrived */
void   fs_sleep(fs_bus *b);                /* put wake-up slaves back to sleep */
int    fs_add_slave(fs_bus *b, int primary, const char *sec16, fs_fcb_mode mode);
void   fs_add_tg(fs_bus *b, int slave, const uint8_t *tg, size_t len);
void   fs_set_reduced(fs_bus *b, int slave, const uint8_t *tg, size_t len);

/* Long frame, C=0x08, from raw user data. Returns length. */
size_t fs_build_long(uint8_t *out, uint8_t addr, uint8_t ci, const uint8_t *ud, size_t n);

/* Variable data response. ci: 0x72 (12 byte header), 0x7A (4 byte), 0x78 (none).
   more: append MDH 0x1F. Returns length. */
size_t fs_build_var(uint8_t *out, uint8_t addr, uint8_t ci, const char *sec16,
                    uint8_t access, const uint8_t *rec, size_t rec_n, int more);

size_t fs_hex(uint8_t *dst, size_t max, const char *s);

/* handle bound to the bus; free with fs_free_handle() */
mbus_handle *fs_handle(fs_bus *b);
void         fs_free_handle(mbus_handle *h);

int fs_count_frames(mbus_frame *f);
int fs_req_control(const fs_bus *b, int n);     /* control byte of the n-th REQ_UD2, -1 if none */
int fs_count_req(const fs_bus *b);

#endif
