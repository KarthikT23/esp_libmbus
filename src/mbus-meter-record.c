#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mbus-meter.h"
#include "mbus-meter-internal.h"

/* ------------------------------------------------------------------ */
/* names                                                               */
/* ------------------------------------------------------------------ */

static const char *const qty_names[] = {
    [MBUS_METER_QTY_UNKNOWN]               = "unknown",
    [MBUS_METER_QTY_ENERGY]                = "energy",
    [MBUS_METER_QTY_VOLUME]                = "volume",
    [MBUS_METER_QTY_MASS]                  = "mass",
    [MBUS_METER_QTY_ON_TIME]               = "on_time",
    [MBUS_METER_QTY_OPERATING_TIME]        = "operating_time",
    [MBUS_METER_QTY_POWER]                 = "power",
    [MBUS_METER_QTY_VOLUME_FLOW]           = "volume_flow",
    [MBUS_METER_QTY_MASS_FLOW]             = "mass_flow",
    [MBUS_METER_QTY_FLOW_TEMP]             = "flow_temp",
    [MBUS_METER_QTY_RETURN_TEMP]           = "return_temp",
    [MBUS_METER_QTY_TEMP_DIFF]             = "temp_diff",
    [MBUS_METER_QTY_EXTERNAL_TEMP]         = "external_temp",
    [MBUS_METER_QTY_PRESSURE]              = "pressure",
    [MBUS_METER_QTY_DATE]                  = "date",
    [MBUS_METER_QTY_TIME]                  = "time",
    [MBUS_METER_QTY_DATETIME]              = "datetime",
    [MBUS_METER_QTY_HCA_UNITS]             = "hca_units",
    [MBUS_METER_QTY_AVG_DURATION]          = "avg_duration",
    [MBUS_METER_QTY_ACTUALITY_DURATION]    = "actuality_duration",
    [MBUS_METER_QTY_FABRICATION_NO]        = "fabrication_no",
    [MBUS_METER_QTY_ENHANCED_ID]           = "enhanced_id",
    [MBUS_METER_QTY_BUS_ADDRESS]           = "bus_address",
    [MBUS_METER_QTY_VOLTAGE]               = "voltage",
    [MBUS_METER_QTY_CURRENT]               = "current",
    [MBUS_METER_QTY_FREQUENCY]             = "frequency",
    [MBUS_METER_QTY_REL_HUMIDITY]          = "rel_humidity",
    [MBUS_METER_QTY_CREDIT]                = "credit",
    [MBUS_METER_QTY_DEBIT]                 = "debit",
    [MBUS_METER_QTY_ACCESS_NO]             = "access_no",
    [MBUS_METER_QTY_MEDIUM]                = "medium",
    [MBUS_METER_QTY_MANUFACTURER]          = "manufacturer",
    [MBUS_METER_QTY_PARAMETER_SET_ID]      = "parameter_set_id",
    [MBUS_METER_QTY_MODEL_VERSION]         = "model_version",
    [MBUS_METER_QTY_HARDWARE_VERSION]      = "hardware_version",
    [MBUS_METER_QTY_FIRMWARE_VERSION]      = "firmware_version",
    [MBUS_METER_QTY_SOFTWARE_VERSION]      = "software_version",
    [MBUS_METER_QTY_CUSTOMER_LOCATION]     = "customer_location",
    [MBUS_METER_QTY_CUSTOMER]              = "customer",
    [MBUS_METER_QTY_ERROR_FLAGS]           = "error_flags",
    [MBUS_METER_QTY_ERROR_MASK]            = "error_mask",
    [MBUS_METER_QTY_DIGITAL_OUTPUT]        = "digital_output",
    [MBUS_METER_QTY_DIGITAL_INPUT]         = "digital_input",
    [MBUS_METER_QTY_BAUDRATE]              = "baudrate",
    [MBUS_METER_QTY_RESPONSE_DELAY]        = "response_delay",
    [MBUS_METER_QTY_RETRY]                 = "retry",
    [MBUS_METER_QTY_TARIFF_DURATION]       = "tariff_duration",
    [MBUS_METER_QTY_BATTERY_LIFE]          = "battery_life",
    [MBUS_METER_QTY_PLAIN_TEXT]            = "plain_text",
    [MBUS_METER_QTY_MANUFACTURER_SPECIFIC] = "manufacturer_specific",
    [MBUS_METER_QTY_ANY_VIF]               = "any_vif",
    [MBUS_METER_QTY_DIMENSIONLESS]         = "dimensionless",
    [MBUS_METER_QTY_ACCESS_CODE]           = "access_code",
    [MBUS_METER_QTY_PASSWORD]              = "password",
    [MBUS_METER_QTY_STORAGE_BLOCK]         = "storage_block",
    [MBUS_METER_QTY_STORAGE_INTERVAL]      = "storage_interval",
    [MBUS_METER_QTY_READOUT_DURATION]      = "readout_duration",
    [MBUS_METER_QTY_TARIFF_START]          = "tariff_start",
    [MBUS_METER_QTY_TARIFF_PERIOD]         = "tariff_period",
    [MBUS_METER_QTY_RESET_COUNTER]         = "reset_counter",
    [MBUS_METER_QTY_CUMULATION_COUNTER]    = "cumulation_counter",
    [MBUS_METER_QTY_CONTROL_SIGNAL]        = "control_signal",
    [MBUS_METER_QTY_DAY_OF_WEEK]           = "day_of_week",
    [MBUS_METER_QTY_WEEK_NUMBER]           = "week_number",
    [MBUS_METER_QTY_DAY_CHANGE_TIME]       = "day_change_time",
    [MBUS_METER_QTY_PARAM_ACTIVATION]      = "param_activation",
    [MBUS_METER_QTY_SUPPLIER_INFO]         = "supplier_info",
    [MBUS_METER_QTY_CUMULATION_DURATION]   = "cumulation_duration",
    [MBUS_METER_QTY_BATTERY_CHANGE_DATE]   = "battery_change_date",
    [MBUS_METER_QTY_TEMP_LIMIT]            = "temp_limit",
};

static const char *const unit_names[] = {
    [MBUS_METER_UNIT_NONE]               = "",
    [MBUS_METER_UNIT_WH]                 = "Wh",
    [MBUS_METER_UNIT_J]                  = "J",
    [MBUS_METER_UNIT_M3]                 = "m^3",
    [MBUS_METER_UNIT_KG]                 = "kg",
    [MBUS_METER_UNIT_W]                  = "W",
    [MBUS_METER_UNIT_J_PER_H]            = "J/h",
    [MBUS_METER_UNIT_M3_PER_H]           = "m^3/h",
    [MBUS_METER_UNIT_M3_PER_MIN]         = "m^3/min",
    [MBUS_METER_UNIT_M3_PER_S]           = "m^3/s",
    [MBUS_METER_UNIT_KG_PER_H]           = "kg/h",
    [MBUS_METER_UNIT_CELSIUS]            = "degC",
    [MBUS_METER_UNIT_KELVIN]             = "K",
    [MBUS_METER_UNIT_FAHRENHEIT]         = "degF",
    [MBUS_METER_UNIT_BAR]                = "bar",
    [MBUS_METER_UNIT_SECOND]             = "s",
    [MBUS_METER_UNIT_MINUTE]             = "min",
    [MBUS_METER_UNIT_HOUR]               = "h",
    [MBUS_METER_UNIT_DAY]                = "d",
    [MBUS_METER_UNIT_MONTH]              = "month",
    [MBUS_METER_UNIT_YEAR]               = "year",
    [MBUS_METER_UNIT_VOLT]               = "V",
    [MBUS_METER_UNIT_AMPERE]             = "A",
    [MBUS_METER_UNIT_HERTZ]              = "Hz",
    [MBUS_METER_UNIT_PERCENT]            = "%",
    [MBUS_METER_UNIT_BAUD]               = "Bd",
    [MBUS_METER_UNIT_BIT_TIME]           = "bit_time",
    [MBUS_METER_UNIT_FT3]                = "ft^3",
    [MBUS_METER_UNIT_US_GALLON]          = "gal",
    [MBUS_METER_UNIT_US_GALLON_PER_MIN]  = "gal/min",
    [MBUS_METER_UNIT_US_GALLON_PER_H]    = "gal/h",
    [MBUS_METER_UNIT_CURRENCY]           = "currency",
    [MBUS_METER_UNIT_OTHER]              = "?",
};

#define NAMES_LEN(t) (sizeof(t) / sizeof((t)[0]))

const char *
mbus_meter_qty_str(mbus_meter_qty_t qty)
{
    if ((unsigned)qty >= NAMES_LEN(qty_names) || qty_names[qty] == NULL)
        return "unknown";
    return qty_names[qty];
}

const char *
mbus_meter_unit_str(mbus_meter_unit_t unit)
{
    if ((unsigned)unit >= NAMES_LEN(unit_names) || unit_names[unit] == NULL)
        return "?";
    return unit_names[unit];
}

const char *
mbus_meter_strerror(int err)
{
    switch (err) {
    case MBUS_METER_OK:                 return "ok";
    case MBUS_METER_PARTIAL:            return "partial readout";
    case MBUS_METER_ERR_ARG:            return "bad argument";
    case MBUS_METER_ERR_NOMEM:          return "out of memory";
    case MBUS_METER_ERR_TOO_SOON:       return "min interval not elapsed";
    case MBUS_METER_ERR_ABORTED:        return "aborted";
    case MBUS_METER_ERR_TIMEOUT:        return "no answer";
    case MBUS_METER_ERR_LINK:           return "link error";
    case MBUS_METER_ERR_INVALID_FRAME:  return "invalid frame";
    case MBUS_METER_ERR_NO_ACK:         return "no ack";
    case MBUS_METER_ERR_COLLISION:      return "collision";
    case MBUS_METER_ERR_JAM:            return "bus jammed";
    case MBUS_METER_ERR_UNSUPPORTED_CI: return "unsupported CI";
    case MBUS_METER_ERR_APP_ERROR:      return "application error from slave";
    default:                            return "unknown error";
    }
}

const char *
mbus_meter_end_reason_str(mbus_meter_end_reason_t reason)
{
    switch (reason) {
    case MBUS_METER_END_NONE:        return "none";
    case MBUS_METER_END_LAST_FRAME:  return "last frame";
    case MBUS_METER_END_MAX_FRAMES:  return "max frames";
    case MBUS_METER_END_MAX_RECORDS: return "max records";
    case MBUS_METER_END_REPEAT:      return "repeated frame";
    case MBUS_METER_END_ERROR:       return "error";
    case MBUS_METER_END_ABORTED:     return "aborted";
    default:                         return "?";
    }
}

const char *
mbus_meter_medium_str(uint8_t medium)
{
    switch (medium) {
    case 0x00: return "other";
    case 0x01: return "oil";
    case 0x02: return "electricity";
    case 0x03: return "gas";
    case 0x04: return "heat (outlet)";
    case 0x05: return "steam";
    case 0x06: return "warm water";
    case 0x07: return "water";
    case 0x08: return "heat cost allocator";
    case 0x09: return "compressed air";
    case 0x0A: return "cooling (outlet)";
    case 0x0B: return "cooling (inlet)";
    case 0x0C: return "heat (inlet)";
    case 0x0D: return "heat/cooling";
    case 0x0E: return "bus/system";
    case 0x0F: return "unknown medium";
    case 0x10: return "irrigation water";
    case 0x11: return "water logger";
    case 0x12: return "gas logger";
    case 0x13: return "gas converter";
    case 0x14: return "calorific value";
    case 0x15: return "hot water (>90C)";
    case 0x16: return "cold water";
    case 0x17: return "dual water";
    case 0x18: return "pressure";
    case 0x19: return "A/D converter";
    case 0x1A: return "smoke detector";
    case 0x1B: return "ambient sensor";
    case 0x1C: return "gas detector";
    case 0x20: return "breaker (electricity)";
    case 0x21: return "valve";
    case 0x25: return "customer unit";
    case 0x28: return "waste water";
    case 0x29: return "garbage";
    case 0x30: return "service unit";
    case 0x31: return "gateway";
    case 0x32: return "repeater (uni)";
    case 0x33: return "repeater (bi)";
    case 0x36: return "radio control (system)";
    case 0x37: return "radio control (meter)";
    case 0x38: return "bus control (meter)";
    default:   return "reserved";
    }
}

/* ------------------------------------------------------------------ */
/* VIF / VIFE tables                                                   */
/* ------------------------------------------------------------------ */

static void
vset(mbus_meter_vdesc_t *d, mbus_meter_qty_t q, mbus_meter_unit_t u, int exp10)
{
    d->qty = q;
    d->unit = u;
    d->exp10 = (int8_t)exp10;
}

/* nn of the duration VIFs: s, min, h, d */
static mbus_meter_unit_t
unit_nn(int nn)
{
    switch (nn & 3) {
    case 0:  return MBUS_METER_UNIT_SECOND;
    case 1:  return MBUS_METER_UNIT_MINUTE;
    case 2:  return MBUS_METER_UNIT_HOUR;
    default: return MBUS_METER_UNIT_DAY;
    }
}

/* pp of the long duration VIFEs: h, d, month, year */
static mbus_meter_unit_t
unit_pp(int pp)
{
    switch (pp & 3) {
    case 0:  return MBUS_METER_UNIT_HOUR;
    case 1:  return MBUS_METER_UNIT_DAY;
    case 2:  return MBUS_METER_UNIT_MONTH;
    default: return MBUS_METER_UNIT_YEAR;
    }
}

/* primary VIF table, v = vif & 0x7F, not FB/FD/7C/7E/7F */
static void
resolve_primary(uint8_t v, mbus_meter_vdesc_t *d)
{
    if (v <= 0x07)       vset(d, MBUS_METER_QTY_ENERGY, MBUS_METER_UNIT_WH, (v & 7) - 3);
    else if (v <= 0x0F)  vset(d, MBUS_METER_QTY_ENERGY, MBUS_METER_UNIT_J, v & 7);
    else if (v <= 0x17)  vset(d, MBUS_METER_QTY_VOLUME, MBUS_METER_UNIT_M3, (v & 7) - 6);
    else if (v <= 0x1F)  vset(d, MBUS_METER_QTY_MASS, MBUS_METER_UNIT_KG, (v & 7) - 3);
    else if (v <= 0x23)  vset(d, MBUS_METER_QTY_ON_TIME, unit_nn(v & 3), 0);
    else if (v <= 0x27)  vset(d, MBUS_METER_QTY_OPERATING_TIME, unit_nn(v & 3), 0);
    else if (v <= 0x2F)  vset(d, MBUS_METER_QTY_POWER, MBUS_METER_UNIT_W, (v & 7) - 3);
    else if (v <= 0x37)  vset(d, MBUS_METER_QTY_POWER, MBUS_METER_UNIT_J_PER_H, v & 7);
    else if (v <= 0x3F)  vset(d, MBUS_METER_QTY_VOLUME_FLOW, MBUS_METER_UNIT_M3_PER_H, (v & 7) - 6);
    else if (v <= 0x47)  vset(d, MBUS_METER_QTY_VOLUME_FLOW, MBUS_METER_UNIT_M3_PER_MIN, (v & 7) - 7);
    else if (v <= 0x4F)  vset(d, MBUS_METER_QTY_VOLUME_FLOW, MBUS_METER_UNIT_M3_PER_S, (v & 7) - 9);
    else if (v <= 0x57)  vset(d, MBUS_METER_QTY_MASS_FLOW, MBUS_METER_UNIT_KG_PER_H, (v & 7) - 3);
    else if (v <= 0x5B)  vset(d, MBUS_METER_QTY_FLOW_TEMP, MBUS_METER_UNIT_CELSIUS, (v & 3) - 3);
    else if (v <= 0x5F)  vset(d, MBUS_METER_QTY_RETURN_TEMP, MBUS_METER_UNIT_CELSIUS, (v & 3) - 3);
    else if (v <= 0x63)  vset(d, MBUS_METER_QTY_TEMP_DIFF, MBUS_METER_UNIT_KELVIN, (v & 3) - 3);
    else if (v <= 0x67)  vset(d, MBUS_METER_QTY_EXTERNAL_TEMP, MBUS_METER_UNIT_CELSIUS, (v & 3) - 3);
    else if (v <= 0x6B)  vset(d, MBUS_METER_QTY_PRESSURE, MBUS_METER_UNIT_BAR, (v & 3) - 3);
    else if (v == 0x6C) {
        vset(d, MBUS_METER_QTY_DATE, MBUS_METER_UNIT_NONE, 0);
        d->tp = MBUS_METER_TP_DATE_G;
    } else if (v == 0x6D) {
        vset(d, MBUS_METER_QTY_DATETIME, MBUS_METER_UNIT_NONE, 0);
        d->tp = MBUS_METER_TP_DATETIME;
    }
    else if (v == 0x6E)  vset(d, MBUS_METER_QTY_HCA_UNITS, MBUS_METER_UNIT_NONE, 0);
    else if (v == 0x6F)  d->flags |= MBUS_METER_F_UNKNOWN_VIF;   /* reserved */
    else if (v <= 0x73)  vset(d, MBUS_METER_QTY_AVG_DURATION, unit_nn(v & 3), 0);
    else if (v <= 0x77)  vset(d, MBUS_METER_QTY_ACTUALITY_DURATION, unit_nn(v & 3), 0);
    else if (v == 0x78)  vset(d, MBUS_METER_QTY_FABRICATION_NO, MBUS_METER_UNIT_NONE, 0);
    else if (v == 0x79)  vset(d, MBUS_METER_QTY_ENHANCED_ID, MBUS_METER_UNIT_NONE, 0);
    else if (v == 0x7A)  vset(d, MBUS_METER_QTY_BUS_ADDRESS, MBUS_METER_UNIT_NONE, 0);
    else                 d->flags |= MBUS_METER_F_UNKNOWN_VIF;   /* 0x7B/0x7D without ext bit */
}

/* VIF 0xFB extension table, c = vife[0] & 0x7F. false = not in the table */
static bool
resolve_fb(uint8_t c, mbus_meter_vdesc_t *d)
{
    switch (c) {
    case 0x00: case 0x01: vset(d, MBUS_METER_QTY_ENERGY, MBUS_METER_UNIT_WH, 5 + (c & 1)); return true;   /* 0.1 MWh, MWh */
    case 0x08: case 0x09: vset(d, MBUS_METER_QTY_ENERGY, MBUS_METER_UNIT_J, 8 + (c & 1)); return true;    /* 0.1 GJ, GJ */
    case 0x10: case 0x11: vset(d, MBUS_METER_QTY_VOLUME, MBUS_METER_UNIT_M3, 2 + (c & 1)); return true;   /* 100 m3, 1000 m3 */
    case 0x18: case 0x19: vset(d, MBUS_METER_QTY_MASS, MBUS_METER_UNIT_KG, 5 + (c & 1)); return true;     /* 100 t, 1000 t */
    case 0x21: vset(d, MBUS_METER_QTY_VOLUME, MBUS_METER_UNIT_FT3, -1); return true;
    case 0x22: case 0x23: vset(d, MBUS_METER_QTY_VOLUME, MBUS_METER_UNIT_US_GALLON, (c & 1) - 1); return true;
    case 0x24: vset(d, MBUS_METER_QTY_VOLUME_FLOW, MBUS_METER_UNIT_US_GALLON_PER_MIN, -3); return true;
    case 0x25: vset(d, MBUS_METER_QTY_VOLUME_FLOW, MBUS_METER_UNIT_US_GALLON_PER_MIN, 0); return true;
    case 0x26: vset(d, MBUS_METER_QTY_VOLUME_FLOW, MBUS_METER_UNIT_US_GALLON_PER_H, 0); return true;
    case 0x28: case 0x29: vset(d, MBUS_METER_QTY_POWER, MBUS_METER_UNIT_W, 5 + (c & 1)); return true;     /* 0.1 MW, MW */
    case 0x30: case 0x31: vset(d, MBUS_METER_QTY_POWER, MBUS_METER_UNIT_J_PER_H, 8 + (c & 1)); return true;
    default: break;
    }

    if (c >= 0x58 && c <= 0x5B)      vset(d, MBUS_METER_QTY_FLOW_TEMP, MBUS_METER_UNIT_FAHRENHEIT, (c & 3) - 3);
    else if (c >= 0x5C && c <= 0x5F) vset(d, MBUS_METER_QTY_RETURN_TEMP, MBUS_METER_UNIT_FAHRENHEIT, (c & 3) - 3);
    else if (c >= 0x60 && c <= 0x63) vset(d, MBUS_METER_QTY_TEMP_DIFF, MBUS_METER_UNIT_FAHRENHEIT, (c & 3) - 3);
    else if (c >= 0x64 && c <= 0x67) vset(d, MBUS_METER_QTY_EXTERNAL_TEMP, MBUS_METER_UNIT_FAHRENHEIT, (c & 3) - 3);
    else if (c >= 0x70 && c <= 0x73) vset(d, MBUS_METER_QTY_TEMP_LIMIT, MBUS_METER_UNIT_FAHRENHEIT, (c & 3) - 3);
    else if (c >= 0x74 && c <= 0x77) vset(d, MBUS_METER_QTY_TEMP_LIMIT, MBUS_METER_UNIT_CELSIUS, (c & 3) - 3);
    else if (c >= 0x78)              vset(d, MBUS_METER_QTY_POWER, MBUS_METER_UNIT_W, (c & 7) - 3);       /* cumulative max power */
    else return false;
    return true;
}

/* VIF 0xFD extension table, c = vife[0] & 0x7F. false = not in the table */
static bool
resolve_fd(uint8_t c, mbus_meter_vdesc_t *d)
{
    const mbus_meter_unit_t none = MBUS_METER_UNIT_NONE;

    if (c <= 0x03) { vset(d, MBUS_METER_QTY_CREDIT, MBUS_METER_UNIT_CURRENCY, (c & 3) - 3); return true; }
    if (c <= 0x07) { vset(d, MBUS_METER_QTY_DEBIT, MBUS_METER_UNIT_CURRENCY, (c & 3) - 3); return true; }

    switch (c) {
    case 0x08: vset(d, MBUS_METER_QTY_ACCESS_NO, none, 0); return true;
    case 0x09: vset(d, MBUS_METER_QTY_MEDIUM, none, 0); return true;
    case 0x0A: vset(d, MBUS_METER_QTY_MANUFACTURER, none, 0); return true;
    case 0x0B: vset(d, MBUS_METER_QTY_PARAMETER_SET_ID, none, 0); return true;
    case 0x0C: vset(d, MBUS_METER_QTY_MODEL_VERSION, none, 0); return true;
    case 0x0D: vset(d, MBUS_METER_QTY_HARDWARE_VERSION, none, 0); return true;
    case 0x0E: vset(d, MBUS_METER_QTY_FIRMWARE_VERSION, none, 0); return true;
    case 0x0F: vset(d, MBUS_METER_QTY_SOFTWARE_VERSION, none, 0); return true;
    case 0x10: vset(d, MBUS_METER_QTY_CUSTOMER_LOCATION, none, 0); return true;
    case 0x11: vset(d, MBUS_METER_QTY_CUSTOMER, none, 0); return true;
    case 0x12: case 0x13: case 0x14: case 0x15:
               vset(d, MBUS_METER_QTY_ACCESS_CODE, none, 0); return true;
    case 0x16: vset(d, MBUS_METER_QTY_PASSWORD, none, 0); return true;
    case 0x17: vset(d, MBUS_METER_QTY_ERROR_FLAGS, none, 0); return true;
    case 0x18: vset(d, MBUS_METER_QTY_ERROR_MASK, none, 0); return true;
    case 0x1A: vset(d, MBUS_METER_QTY_DIGITAL_OUTPUT, none, 0); return true;
    case 0x1B: vset(d, MBUS_METER_QTY_DIGITAL_INPUT, none, 0); return true;
    case 0x1C: vset(d, MBUS_METER_QTY_BAUDRATE, MBUS_METER_UNIT_BAUD, 0); return true;
    case 0x1D: vset(d, MBUS_METER_QTY_RESPONSE_DELAY, MBUS_METER_UNIT_BIT_TIME, 0); return true;
    case 0x1E: vset(d, MBUS_METER_QTY_RETRY, none, 0); return true;
    case 0x20: case 0x21: case 0x22:
               vset(d, MBUS_METER_QTY_STORAGE_BLOCK, none, 0); return true;
    case 0x28: vset(d, MBUS_METER_QTY_STORAGE_INTERVAL, MBUS_METER_UNIT_MONTH, 0); return true;
    case 0x29: vset(d, MBUS_METER_QTY_STORAGE_INTERVAL, MBUS_METER_UNIT_YEAR, 0); return true;
    case 0x30:
        vset(d, MBUS_METER_QTY_TARIFF_START, none, 0);
        d->tp = MBUS_METER_TP_DATETIME;
        return true;
    case 0x38: vset(d, MBUS_METER_QTY_TARIFF_PERIOD, MBUS_METER_UNIT_MONTH, 0); return true;
    case 0x39: vset(d, MBUS_METER_QTY_TARIFF_PERIOD, MBUS_METER_UNIT_YEAR, 0); return true;
    case 0x3A: vset(d, MBUS_METER_QTY_DIMENSIONLESS, none, 0); return true;
    case 0x60: vset(d, MBUS_METER_QTY_RESET_COUNTER, none, 0); return true;
    case 0x61: vset(d, MBUS_METER_QTY_CUMULATION_COUNTER, none, 0); return true;
    case 0x62: vset(d, MBUS_METER_QTY_CONTROL_SIGNAL, none, 0); return true;
    case 0x63: vset(d, MBUS_METER_QTY_DAY_OF_WEEK, none, 0); return true;
    case 0x64: vset(d, MBUS_METER_QTY_WEEK_NUMBER, none, 0); return true;
    case 0x65: vset(d, MBUS_METER_QTY_DAY_CHANGE_TIME, none, 0); return true;
    case 0x66: vset(d, MBUS_METER_QTY_PARAM_ACTIVATION, none, 0); return true;
    case 0x67: vset(d, MBUS_METER_QTY_SUPPLIER_INFO, none, 0); return true;
    case 0x70:
        vset(d, MBUS_METER_QTY_BATTERY_CHANGE_DATE, none, 0);
        d->tp = MBUS_METER_TP_DATETIME;
        return true;
    default: break;
    }

    if (c >= 0x24 && c <= 0x27)      vset(d, MBUS_METER_QTY_STORAGE_INTERVAL, unit_nn(c & 3), 0);
    else if (c >= 0x2C && c <= 0x2F) vset(d, MBUS_METER_QTY_READOUT_DURATION, unit_nn(c & 3), 0);
    else if (c >= 0x31 && c <= 0x33) vset(d, MBUS_METER_QTY_TARIFF_DURATION, unit_nn(c & 3), 0);
    else if (c >= 0x34 && c <= 0x37) vset(d, MBUS_METER_QTY_TARIFF_PERIOD, unit_nn(c & 3), 0);
    else if (c >= 0x40 && c <= 0x4F) vset(d, MBUS_METER_QTY_VOLTAGE, MBUS_METER_UNIT_VOLT, (c & 0x0F) - 9);
    else if (c >= 0x50 && c <= 0x5F) vset(d, MBUS_METER_QTY_CURRENT, MBUS_METER_UNIT_AMPERE, (c & 0x0F) - 12);
    else if (c >= 0x68 && c <= 0x6B) vset(d, MBUS_METER_QTY_CUMULATION_DURATION, unit_pp(c & 3), 0);
    else if (c >= 0x6C && c <= 0x6F) vset(d, MBUS_METER_QTY_BATTERY_LIFE, unit_pp(c & 3), 0);
    else return false;
    return true;
}

void
mbus_meter_vib_resolve(uint8_t vif, const uint8_t *vife, uint8_t n_vife, mbus_meter_vdesc_t *d)
{
    uint8_t v = vif & 0x7F;
    uint8_t first = 0, i;
    int e;

    memset(d, 0, sizeof(*d));
    d->qty = MBUS_METER_QTY_UNKNOWN;
    d->unit = MBUS_METER_UNIT_NONE;

    if (vife == NULL)
        n_vife = 0;
    if (n_vife > MBUS_METER_VIFE_MAX)
        n_vife = MBUS_METER_VIFE_MAX;

    if (vif == 0xFB || vif == 0xFD) {
        bool ok;

        if (n_vife == 0) {                       /* extension byte missing */
            d->flags |= MBUS_METER_F_UNKNOWN_VIF;
            return;
        }
        ok = (vif == 0xFB) ? resolve_fb(vife[0] & 0x7F, d) : resolve_fd(vife[0] & 0x7F, d);
        if (!ok)
            d->flags |= MBUS_METER_F_UNKNOWN_VIFE;
        first = 1;                               /* vife[0] is the table index */
    } else if (v == 0x7C) {
        d->qty = MBUS_METER_QTY_PLAIN_TEXT;      /* unit text is in the record */
    } else if (v == 0x7F) {
        d->qty = MBUS_METER_QTY_MANUFACTURER_SPECIFIC;
        d->flags |= MBUS_METER_F_MFR_SPECIFIC;
        first = n_vife;                          /* VIFEs are vendor defined, keep raw */
    } else if (v == 0x7E) {
        d->qty = MBUS_METER_QTY_ANY_VIF;
    } else {
        resolve_primary(v, d);
    }

    /* combinable (orthogonal) VIFEs */
    e = d->exp10;
    for (i = first; i < n_vife; i++) {
        uint8_t c = vife[i] & 0x7F;

        if (c >= 0x70 && c <= 0x77) {
            e += (c & 7) - 6;                    /* multiplicative correction 10^(nnn-6) */
        } else if (c >= 0x78 && c <= 0x7B) {
            d->flags |= MBUS_METER_F_HAS_ADDITIVE;
            d->add_exp = (int8_t)((c & 3) - 3);  /* additive correction 10^(nn-3) */
        } else if (c == 0x7D) {
            e += 3;                              /* multiplicative correction 10^3 */
        } else if (c == 0x7F) {
            d->flags |= MBUS_METER_F_MFR_SPECIFIC;
            break;                               /* rest is vendor defined */
        } else {
            d->flags |= MBUS_METER_F_UNKNOWN_VIFE;
        }
    }
    if (e > 127) e = 127;
    if (e < -128) e = -128;
    d->exp10 = (int8_t)e;
}

/* ------------------------------------------------------------------ */
/* value decode                                                        */
/* ------------------------------------------------------------------ */

static uint64_t
le_u64(const uint8_t *d, size_t n)
{
    uint64_t u = 0;
    size_t i;

    for (i = n; i > 0; i--)
        u = (u << 8) | d[i - 1];
    return u;
}

/* BCD, little endian bytes, n <= 9. F in the top nibble = negative if allow_sign,
 * other nibbles above 9 count as 0 and set BCD_ERROR. */
static int64_t
bcd_to_i64(const uint8_t *d, size_t n, uint16_t *flags, bool allow_sign)
{
    int64_t v = 0;
    bool neg = false;
    size_t i;

    for (i = n; i > 0; i--) {
        uint8_t b = d[i - 1], hi = b >> 4, lo = b & 0x0F;

        if (i == n && allow_sign && hi == 0x0F) {
            neg = true;
            hi = 0;
        } else if (hi > 9) {
            *flags |= MBUS_METER_F_BCD_ERROR;
            hi = 0;
        }
        if (lo > 9) {
            *flags |= MBUS_METER_F_BCD_ERROR;
            lo = 0;
        }
        v = v * 100 + hi * 10 + lo;
    }
    return neg ? -v : v;
}

static int
store_int(const mbus_meter_profile_t *p, mbus_meter_record_t *rec, const uint8_t *d, size_t n)
{
    uint64_t u = le_u64(d, n);

    if (p != NULL && p->unsigned_ints) {
        rec->vtype = MBUS_METER_VT_UINT;
        rec->value.u = u;
        rec->flags |= MBUS_METER_F_UNSIGNED_FORCED;
    } else {
        if (n < 8 && (d[n - 1] & 0x80))
            u |= ~(uint64_t)0 << (8 * n);        /* sign extend */
        rec->vtype = MBUS_METER_VT_INT;
        rec->value.i = (int64_t)u;
    }
    return MBUS_METER_OK;
}

static int
store_bytes(mbus_meter_record_t *rec, const uint8_t *d, size_t n, bool as_text)
{
    uint8_t *buf = (uint8_t *)malloc(n + 1);
    size_t i;

    if (buf == NULL)
        return MBUS_METER_ERR_NOMEM;

    if (as_text) {
        for (i = 0; i < n; i++)
            buf[i] = d[n - 1 - i];               /* wire order is reversed */
    } else if (n > 0) {
        memcpy(buf, d, n);
    }
    buf[n] = 0;

    rec->vtype = as_text ? MBUS_METER_VT_TEXT : MBUS_METER_VT_BINARY;
    rec->value.bytes.data = buf;
    rec->value.bytes.len = (uint16_t)n;
    return MBUS_METER_OK;
}

static int
decode_date_g(mbus_meter_record_t *rec, const uint8_t *d)
{
    mbus_meter_datetime_t *t = &rec->value.dt;

    memset(&rec->value, 0, sizeof(rec->value));
    rec->vtype = MBUS_METER_VT_DATETIME;
    t->kind = MBUS_METER_DT_DATE;

    if (d[0] == 0xFF && d[1] == 0xFF) {          /* all ones = invalid */
        t->invalid = true;
        rec->flags |= MBUS_METER_F_TIME_INVALID;
        return MBUS_METER_OK;
    }
    t->day = d[0] & 0x1F;
    t->month = d[1] & 0x0F;
    t->year = (uint16_t)(2000 + (((d[0] & 0xE0) >> 5) | ((d[1] & 0xF0) >> 1)));
    return MBUS_METER_OK;
}

static int
decode_datetime_f(mbus_meter_record_t *rec, const uint8_t *d)
{
    mbus_meter_datetime_t *t = &rec->value.dt;
    int yy, hy;

    memset(&rec->value, 0, sizeof(rec->value));
    rec->vtype = MBUS_METER_VT_DATETIME;
    t->kind = MBUS_METER_DT_DATETIME;

    if (d[0] & 0x80) {                           /* IV */
        t->invalid = true;
        rec->flags |= MBUS_METER_F_TIME_INVALID;
        return MBUS_METER_OK;
    }
    yy = ((d[2] & 0xE0) >> 5) | ((d[3] & 0xF0) >> 1);
    hy = (d[1] & 0x60) >> 5;                     /* hundred-year bits */
    if (hy == 0 && yy <= 80)
        hy = 1;                                  /* old meters: 2 digit year, 2000+ */
    t->minute = d[0] & 0x3F;
    t->hour = d[1] & 0x1F;
    t->day = d[2] & 0x1F;
    t->month = d[3] & 0x0F;
    t->year = (uint16_t)(1900 + 100 * hy + yy);
    t->summer_time = (d[1] & 0x80) != 0;
    return MBUS_METER_OK;
}

static int
decode_datetime_i(mbus_meter_record_t *rec, const uint8_t *d)
{
    mbus_meter_datetime_t *t = &rec->value.dt;

    memset(&rec->value, 0, sizeof(rec->value));
    rec->vtype = MBUS_METER_VT_DATETIME;
    t->kind = MBUS_METER_DT_DATETIME;

    if (d[1] & 0x80) {                           /* IV */
        t->invalid = true;
        rec->flags |= MBUS_METER_F_TIME_INVALID;
        return MBUS_METER_OK;
    }
    t->second = d[0] & 0x3F;
    t->minute = d[1] & 0x3F;
    t->hour = d[2] & 0x1F;
    t->day = d[3] & 0x1F;
    t->month = d[4] & 0x0F;
    t->year = (uint16_t)(2000 + (((d[3] & 0xE0) >> 5) | ((d[4] & 0xF0) >> 1)));
    t->summer_time = (d[0] & 0x40) != 0;         /* bit position as in the reference parser */
    return MBUS_METER_OK;
}

static int
decode_time_j(mbus_meter_record_t *rec, const uint8_t *d)
{
    mbus_meter_datetime_t *t = &rec->value.dt;

    memset(&rec->value, 0, sizeof(rec->value));
    rec->vtype = MBUS_METER_VT_DATETIME;
    t->kind = MBUS_METER_DT_TIME;
    t->second = d[0] & 0x3F;
    t->minute = d[1] & 0x3F;
    t->hour = d[2] & 0x1F;
    return MBUS_METER_OK;
}

static int
store_raw_copy(mbus_meter_record_t *rec, const uint8_t *data, size_t len)
{
    rec->raw = (uint8_t *)malloc(len ? len : 1);
    if (rec->raw == NULL)
        return MBUS_METER_ERR_NOMEM;
    if (len)
        memcpy(rec->raw, data, len);
    rec->raw_len = (uint16_t)len;
    return MBUS_METER_OK;
}

int
mbus_meter_value_decode(const mbus_meter_profile_t *p, mbus_meter_record_t *rec,
                        mbus_meter_tp_t tp, uint8_t lvar, const uint8_t *data, size_t len)
{
    uint8_t code;
    size_t n;
    int rc;

    if (rec == NULL || (data == NULL && len > 0))
        return MBUS_METER_ERR_ARG;

    if (p != NULL && p->keep_raw) {
        rc = store_raw_copy(rec, data, len);
        if (rc != MBUS_METER_OK)
            return rc;
    }

    code = rec->dif & 0x0F;
    memset(&rec->value, 0, sizeof(rec->value));

    switch (code) {
    case 0x0:
    case 0x8:                                    /* no data, selection for readout */
        rec->vtype = MBUS_METER_VT_NONE;
        return MBUS_METER_OK;

    case 0x1: case 0x2: case 0x3: case 0x4: case 0x6: case 0x7:
        n = (code == 0x6) ? 6 : (code == 0x7) ? 8 : code;
        if (len < n)
            return MBUS_METER_ERR_INVALID_FRAME;
        if (tp == MBUS_METER_TP_DATE_G && n == 2)
            return decode_date_g(rec, data);
        if (tp == MBUS_METER_TP_DATETIME) {
            if (n == 3) return decode_time_j(rec, data);
            if (n == 4) return decode_datetime_f(rec, data);
            if (n == 6) return decode_datetime_i(rec, data);
        }
        return store_int(p, rec, data, n);

    case 0x5:                                    /* 32 bit real */
        if (len < 4)
            return MBUS_METER_ERR_INVALID_FRAME;
        {
            uint32_t u = (uint32_t)le_u64(data, 4);
            float f;

            memcpy(&f, &u, sizeof(f));
            rec->vtype = MBUS_METER_VT_REAL;
            rec->value.f = (double)f;
        }
        return MBUS_METER_OK;

    case 0x9: case 0xA: case 0xB: case 0xC: case 0xE:
        n = (code == 0xE) ? 6 : (size_t)(code - 8);
        if (len < n)
            return MBUS_METER_ERR_INVALID_FRAME;
        if (rec->key.function == MBUS_METER_FUNC_ERROR) {
            /* BCD under "value during error state" carries hex coded flags */
            rec->vtype = MBUS_METER_VT_UINT;
            rec->value.u = le_u64(data, n);
            rec->flags |= MBUS_METER_F_BCD_AS_HEX;
        } else {
            rec->vtype = MBUS_METER_VT_INT;
            rec->value.i = bcd_to_i64(data, n, &rec->flags, true);
        }
        return MBUS_METER_OK;

    case 0xD:                                    /* variable length */
        if (lvar <= 0xBF) {                      /* text */
            if (len < lvar)
                return MBUS_METER_ERR_INVALID_FRAME;
            return store_bytes(rec, data, lvar, true);
        }
        if (lvar >= 0xC0 && lvar <= 0xC9) {      /* positive BCD, bytes = lvar - 0xC0 */
            n = lvar - 0xC0u;
            if (len < n)
                return MBUS_METER_ERR_INVALID_FRAME;
            rec->vtype = n ? MBUS_METER_VT_INT : MBUS_METER_VT_NONE;
            rec->value.i = n ? bcd_to_i64(data, n, &rec->flags, false) : 0;
            return MBUS_METER_OK;
        }
        if (lvar >= 0xD0 && lvar <= 0xD9) {      /* negative BCD */
            n = lvar - 0xD0u;
            if (len < n)
                return MBUS_METER_ERR_INVALID_FRAME;
            rec->vtype = n ? MBUS_METER_VT_INT : MBUS_METER_VT_NONE;
            rec->value.i = n ? -bcd_to_i64(data, n, &rec->flags, false) : 0;
            return MBUS_METER_OK;
        }
        if (lvar >= 0xE0 && lvar <= 0xEF) {      /* binary number */
            n = lvar - 0xE0u;
            if (len < n)
                return MBUS_METER_ERR_INVALID_FRAME;
            if (n == 0) {
                rec->vtype = MBUS_METER_VT_NONE;
                return MBUS_METER_OK;
            }
            if (n <= 8)
                return store_int(p, rec, data, n);
            return store_bytes(rec, data, n, false);
        }
        return store_bytes(rec, data, len, false);   /* reals, reserved: keep raw */

    case 0xF:                                    /* special functions / vendor data */
    default:
        return store_bytes(rec, data, len, false);
    }
}

/* ------------------------------------------------------------------ */
/* readout container                                                   */
/* ------------------------------------------------------------------ */

void
mbus_meter_record_clear(mbus_meter_record_t *rec)
{
    if (rec == NULL)
        return;
    free(rec->raw);
    free(rec->unit_text);
    if (rec->vtype == MBUS_METER_VT_TEXT || rec->vtype == MBUS_METER_VT_BINARY)
        free(rec->value.bytes.data);
    memset(rec, 0, sizeof(*rec));
}

void
mbus_meter_readout_init(mbus_meter_readout_t *out)
{
    if (out != NULL)
        memset(out, 0, sizeof(*out));
}

void
mbus_meter_readout_free(mbus_meter_readout_t *out)
{
    size_t i;

    if (out == NULL)
        return;
    for (i = 0; i < out->n_records; i++)
        mbus_meter_record_clear(&out->records[i]);
    free(out->records);
    memset(out, 0, sizeof(*out));
}

mbus_meter_record_t *
mbus_meter_readout_add(mbus_meter_readout_t *out, uint16_t max_records, int *err)
{
    mbus_meter_record_t *rec;

    if (err != NULL)
        *err = MBUS_METER_OK;
    if (out == NULL) {
        if (err != NULL)
            *err = MBUS_METER_ERR_ARG;
        return NULL;
    }
    if (max_records != 0 && out->n_records >= max_records) {
        if (err != NULL)
            *err = MBUS_METER_PARTIAL;           /* cap reached, not a failure */
        return NULL;
    }
    if (out->n_records == out->n_alloc) {
        size_t n = out->n_alloc ? out->n_alloc * 2 : 16;
        mbus_meter_record_t *p;

        if (max_records != 0 && n > max_records)
            n = max_records;
        p = (mbus_meter_record_t *)realloc(out->records, n * sizeof(*p));
        if (p == NULL) {
            if (err != NULL)
                *err = MBUS_METER_ERR_NOMEM;
            return NULL;
        }
        out->records = p;
        out->n_alloc = n;
    }
    rec = &out->records[out->n_records++];
    memset(rec, 0, sizeof(*rec));
    return rec;
}

void
mbus_meter_readout_drop_last(mbus_meter_readout_t *out)
{
    if (out == NULL || out->n_records == 0)
        return;
    mbus_meter_record_clear(&out->records[--out->n_records]);
}

bool
mbus_meter_key_equal(const mbus_meter_key_t *a, const mbus_meter_key_t *b)
{
    uint8_t i;

    if (a == NULL || b == NULL)
        return false;
    if (a->vif != b->vif || a->n_vife != b->n_vife || a->function != b->function ||
        a->subunit != b->subunit || a->tariff != b->tariff || a->storage != b->storage)
        return false;
    if (a->n_vife > MBUS_METER_VIFE_MAX)
        return false;
    for (i = 0; i < a->n_vife; i++)
        if (a->vife[i] != b->vife[i])
            return false;
    return true;
}

const mbus_meter_record_t *
mbus_meter_readout_find(const mbus_meter_readout_t *readout, const mbus_meter_key_t *key,
                        size_t *from)
{
    size_t i = (from != NULL) ? *from : 0;

    if (readout == NULL || key == NULL)
        return NULL;
    for (; i < readout->n_records; i++) {
        if (mbus_meter_key_equal(&readout->records[i].key, key)) {
            if (from != NULL)
                *from = i + 1;
            return &readout->records[i];
        }
    }
    if (from != NULL)
        *from = readout->n_records;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* conversions                                                         */
/* ------------------------------------------------------------------ */

static double
scale10(double v, int e)
{
    double f = 1.0;
    int i;

    if (e >= 0) {
        for (i = 0; i < e; i++)
            f *= 10.0;
        return v * f;
    }
    for (i = 0; i < -e; i++)
        f *= 10.0;
    return v / f;                                /* divide: exact for |e| <= 22 */
}

double
mbus_meter_record_to_double(const mbus_meter_record_t *rec)
{
    if (rec == NULL)
        return 0.0;
    switch (rec->vtype) {
    case MBUS_METER_VT_INT:  return scale10((double)rec->value.i, rec->exp10);
    case MBUS_METER_VT_UINT: return scale10((double)rec->value.u, rec->exp10);
    case MBUS_METER_VT_REAL: return scale10(rec->value.f, rec->exp10);
    default:                 return 0.0;
    }
}

/* mantissa * 10^exp10 as exact decimal text */
static int
fmt_scaled(uint64_t mag, bool neg, int exp10, char *buf, size_t sz)
{
    char dig[24];
    char out[96];
    int nd = 0, n = 0, i;

    if (exp10 > 60 || exp10 < -60) {
        buf[0] = 0;
        return MBUS_METER_ERR_ARG;
    }
    do {
        dig[nd++] = (char)('0' + (int)(mag % 10));
        mag /= 10;
    } while (mag != 0);                          /* dig[] holds digits reversed */

    if (neg)
        out[n++] = '-';

    if (exp10 >= 0) {
        for (i = nd - 1; i >= 0; i--)
            out[n++] = dig[i];
        if (!(nd == 1 && dig[0] == '0'))
            for (i = 0; i < exp10; i++)
                out[n++] = '0';
    } else {
        int frac = -exp10;
        int total = (nd > frac) ? nd : frac + 1; /* at least one integer digit */
        int pad = total - nd;
        int int_digits = total - frac;

        for (i = 0; i < total; i++) {
            if (i == int_digits)
                out[n++] = '.';
            out[n++] = (i < pad) ? '0' : dig[nd - 1 - (i - pad)];
        }
    }
    out[n] = 0;

    if ((size_t)n + 1 > sz) {
        if (sz > 0)
            buf[0] = 0;
        return MBUS_METER_ERR_ARG;
    }
    memcpy(buf, out, (size_t)n + 1);
    return n;
}

int
mbus_meter_record_to_str(const mbus_meter_record_t *rec, char *buf, size_t len)
{
    int n;

    if (rec == NULL || buf == NULL || len == 0)
        return MBUS_METER_ERR_ARG;
    buf[0] = 0;

    switch (rec->vtype) {
    case MBUS_METER_VT_NONE:
        return 0;

    case MBUS_METER_VT_INT: {
        int64_t v = rec->value.i;
        bool neg = v < 0;
        uint64_t mag = neg ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;

        return fmt_scaled(mag, neg, rec->exp10, buf, len);
    }
    case MBUS_METER_VT_UINT:
        return fmt_scaled(rec->value.u, false, rec->exp10, buf, len);

    case MBUS_METER_VT_REAL:
        n = snprintf(buf, len, "%.9g", mbus_meter_record_to_double(rec));
        if (n < 0 || (size_t)n >= len) {
            buf[0] = 0;
            return MBUS_METER_ERR_ARG;
        }
        return n;

    case MBUS_METER_VT_TEXT:
        if (rec->value.bytes.data == NULL)
            return 0;
        if ((size_t)rec->value.bytes.len + 1 > len)
            return MBUS_METER_ERR_ARG;           /* caller can read value.bytes directly */
        memcpy(buf, rec->value.bytes.data, (size_t)rec->value.bytes.len + 1);
        return rec->value.bytes.len;

    case MBUS_METER_VT_BINARY: {
        size_t i, need = (size_t)rec->value.bytes.len * 2 + 1;

        if (need > len)
            return MBUS_METER_ERR_ARG;
        for (i = 0; i < rec->value.bytes.len; i++)
            snprintf(&buf[i * 2], 3, "%02X", rec->value.bytes.data[i]);
        return (int)(rec->value.bytes.len * 2);
    }
    case MBUS_METER_VT_DATETIME: {
        const mbus_meter_datetime_t *t = &rec->value.dt;

        if (t->invalid)
            return 0;
        if (t->kind == MBUS_METER_DT_DATE)
            n = snprintf(buf, len, "%04u-%02u-%02u", (unsigned)t->year, (unsigned)t->month,
                         (unsigned)t->day);
        else if (t->kind == MBUS_METER_DT_TIME)
            n = snprintf(buf, len, "%02u:%02u:%02u", (unsigned)t->hour, (unsigned)t->minute,
                         (unsigned)t->second);
        else
            n = snprintf(buf, len, "%04u-%02u-%02uT%02u:%02u:%02u", (unsigned)t->year,
                         (unsigned)t->month, (unsigned)t->day, (unsigned)t->hour,
                         (unsigned)t->minute, (unsigned)t->second);
        if (n < 0 || (size_t)n >= len) {
            buf[0] = 0;
            return MBUS_METER_ERR_ARG;
        }
        return n;
    }
    default:
        return MBUS_METER_ERR_ARG;
    }
}
