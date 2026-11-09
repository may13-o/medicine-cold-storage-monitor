#include "ds3231.h"

#define EPOCH_2000 946684800u
#define EPOCH_2100 4102444800u

static bool bcd_ok(uint8_t v)
{
    return (v & 0x0Fu) <= 9u && (v >> 4) <= 9u;
}

static uint8_t bcd_dec(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10u + (v & 0x0Fu));
}

static uint8_t dec_bcd(uint32_t v)
{
    return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}

/* Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm). */
static int32_t days_from_civil(int32_t y, int32_t m, int32_t d)
{
    y -= (m <= 2) ? 1 : 0;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int32_t z, int32_t *y, int32_t *m, int32_t *d)
{
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t yy = yoe + era * 400;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yy + ((*m <= 2) ? 1 : 0);
}

bool ds3231_regs_to_epoch(const uint8_t regs[7], uint32_t *epoch)
{
    uint8_t hour_reg = regs[2];
    if ((hour_reg & 0x40u) != 0u) {
        return false;  /* 12-hour mode: we always write 24-hour mode */
    }
    uint8_t month_reg = (uint8_t)(regs[5] & 0x7Fu);  /* bit 7 is the century flag */

    if (!bcd_ok(regs[0]) || !bcd_ok(regs[1]) || !bcd_ok((uint8_t)(hour_reg & 0x3Fu)) ||
        !bcd_ok(regs[4]) || !bcd_ok(month_reg) || !bcd_ok(regs[6])) {
        return false;
    }

    uint32_t sec = bcd_dec(regs[0]);
    uint32_t min = bcd_dec(regs[1]);
    uint32_t hour = bcd_dec((uint8_t)(hour_reg & 0x3Fu));
    uint32_t date = bcd_dec(regs[4]);
    uint32_t month = bcd_dec(month_reg);
    uint32_t year = 2000u + bcd_dec(regs[6]);

    if (sec > 59u || min > 59u || hour > 23u || date < 1u || date > 31u || month < 1u || month > 12u) {
        return false;
    }

    int32_t days = days_from_civil((int32_t)year, (int32_t)month, (int32_t)date);
    *epoch = (uint32_t)days * 86400u + hour * 3600u + min * 60u + sec;
    return true;
}

void ds3231_epoch_to_regs(uint32_t epoch, uint8_t regs[7])
{
    if (epoch < EPOCH_2000) {
        epoch = EPOCH_2000;
    }
    if (epoch >= EPOCH_2100) {
        epoch = EPOCH_2100 - 1u;
    }

    uint32_t days = epoch / 86400u;
    uint32_t rem = epoch % 86400u;

    int32_t y, m, d;
    civil_from_days((int32_t)days, &y, &m, &d);

    regs[0] = dec_bcd(rem % 60u);
    regs[1] = dec_bcd((rem / 60u) % 60u);
    regs[2] = dec_bcd(rem / 3600u);                  /* bit 6 = 0 -> 24-hour mode */
    regs[3] = (uint8_t)(((days + 4u) % 7u) + 1u);     /* 1970-01-01 was a Thursday; 1 = Sunday */
    regs[4] = dec_bcd((uint32_t)d);
    regs[5] = dec_bcd((uint32_t)m);
    regs[6] = dec_bcd((uint32_t)(y - 2000));
}
