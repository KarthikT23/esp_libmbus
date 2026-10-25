#ifndef MBUS_METER_INTERNAL_H
#define MBUS_METER_INTERNAL_H

/* private to the component, not installed */

#include "mbus-meter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* how the data bytes of a record are interpreted besides plain numbers */
typedef enum {
    MBUS_METER_TP_NONE = 0,
    MBUS_METER_TP_DATE_G,      /* VIF 0x6C, 2 bytes */
    MBUS_METER_TP_DATETIME     /* VIF 0x6D / FD 0x30 / FD 0x70: 3 (J), 4 (F), 6 (I) bytes */
} mbus_meter_tp_t;

typedef struct {
    mbus_meter_qty_t  qty;
    mbus_meter_unit_t unit;
    int8_t            exp10;
    int8_t            add_exp;
    uint16_t          flags;   /* MBUS_METER_F_* subset set by the VIB */
    mbus_meter_tp_t   tp;
} mbus_meter_vdesc_t;

/* VIF + VIFE (ext bit may be set or not) -> quantity, unit, exponent, flags */
void mbus_meter_vib_resolve(uint8_t vif, const uint8_t *vife, uint8_t n_vife,
                            mbus_meter_vdesc_t *d);

/* Decode data bytes into rec->vtype/value/flags. rec->dif and rec->key.function
 * must be set. lvar is only used for DIF low nibble 0xD, data/len exclude the
 * LVAR byte. May leave heap members allocated on error: caller drops the record. */
int mbus_meter_value_decode(const mbus_meter_profile_t *profile, mbus_meter_record_t *rec,
                            mbus_meter_tp_t tp, uint8_t lvar,
                            const uint8_t *data, size_t len);

/* Append a zeroed record. NULL on failure, *err = ERR_NOMEM or PARTIAL (cap hit).
 * max_records 0 = no limit. Pointers to earlier records are invalid afterwards. */
mbus_meter_record_t *mbus_meter_readout_add(mbus_meter_readout_t *out, uint16_t max_records,
                                            int *err);

/* Remove the last record and free its heap members. */
void mbus_meter_readout_drop_last(mbus_meter_readout_t *out);

/* Free heap members of one record and zero it. */
void mbus_meter_record_clear(mbus_meter_record_t *rec);

/* time and delay, ms; FreeRTOS in the component, virtual clock in host tests */
uint32_t mbus_meter_port_now_ms(void);
void     mbus_meter_port_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* MBUS_METER_INTERNAL_H */
