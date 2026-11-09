#include <math.h>
#include <string.h>

#include "check.h"
#include "ds3231.h"
#include "sht40.h"

static void make_raw(uint16_t t_ticks, uint16_t rh_ticks, uint8_t raw[6])
{
    raw[0] = (uint8_t)(t_ticks >> 8);
    raw[1] = (uint8_t)(t_ticks & 0xFF);
    raw[2] = sht40_crc8(&raw[0], 2);
    raw[3] = (uint8_t)(rh_ticks >> 8);
    raw[4] = (uint8_t)(rh_ticks & 0xFF);
    raw[5] = sht40_crc8(&raw[3], 2);
}

static void check_epoch_roundtrip(uint32_t epoch)
{
    uint8_t regs[7];
    uint32_t back = 0;
    ds3231_epoch_to_regs(epoch, regs);
    CHECK(ds3231_regs_to_epoch(regs, &back));
    CHECK_EQ(back, epoch);
}

int main(void)
{
    /* ---- SHT40 ---- */
    const uint8_t beef[] = { 0xBE, 0xEF };
    CHECK_EQ(sht40_crc8(beef, 2), 0x92);  /* Sensirion datasheet example */

    int16_t t;
    uint16_t rh;
    uint8_t raw[6];

    /* 25.00 C and 50 %RH, computed with floating point as the oracle. */
    uint16_t t_ticks = (uint16_t)lround((25.0 + 45.0) / 175.0 * 65535.0);
    uint16_t rh_ticks = (uint16_t)lround((50.0 + 6.0) / 125.0 * 65535.0);
    make_raw(t_ticks, rh_ticks, raw);
    CHECK(sht40_parse(raw, &t, &rh));
    CHECK(t >= 2499 && t <= 2501);
    CHECK(rh >= 4999 && rh <= 5001);

    /* Fridge temperature, 5.00 C. */
    make_raw((uint16_t)lround((5.0 + 45.0) / 175.0 * 65535.0), rh_ticks, raw);
    CHECK(sht40_parse(raw, &t, &rh));
    CHECK(t >= 499 && t <= 501);

    /* Sub-zero. */
    make_raw((uint16_t)lround((-10.0 + 45.0) / 175.0 * 65535.0), rh_ticks, raw);
    CHECK(sht40_parse(raw, &t, &rh));
    CHECK(t >= -1001 && t <= -999);

    /* RH clamps instead of wrapping. */
    make_raw(t_ticks, 0, raw);
    CHECK(sht40_parse(raw, &t, &rh));
    CHECK_EQ(rh, 0);
    make_raw(t_ticks, 65535, raw);
    CHECK(sht40_parse(raw, &t, &rh));
    CHECK_EQ(rh, 10000);

    /* Bad CRC on either half is rejected. */
    make_raw(t_ticks, rh_ticks, raw);
    raw[2] ^= 0x01;
    CHECK(!sht40_parse(raw, &t, &rh));
    make_raw(t_ticks, rh_ticks, raw);
    raw[5] ^= 0x01;
    CHECK(!sht40_parse(raw, &t, &rh));

    /* ---- DS3231 ---- */
    uint8_t regs[7];
    uint32_t e;

    /* 2000-01-01 00:00:00 UTC was a Saturday (register 7 when Sunday = 1). */
    ds3231_epoch_to_regs(946684800u, regs);
    CHECK_EQ(regs[0], 0x00);
    CHECK_EQ(regs[3], 7);
    CHECK_EQ(regs[4], 0x01);
    CHECK_EQ(regs[5], 0x01);
    CHECK_EQ(regs[6], 0x00);

    /* 2024-02-29 12:34:56 UTC (leap day). */
    ds3231_epoch_to_regs(1709210096u, regs);
    CHECK_EQ(regs[0], 0x56);
    CHECK_EQ(regs[1], 0x34);
    CHECK_EQ(regs[2], 0x12);
    CHECK_EQ(regs[4], 0x29);
    CHECK_EQ(regs[5], 0x02);
    CHECK_EQ(regs[6], 0x24);
    CHECK(ds3231_regs_to_epoch(regs, &e));
    CHECK_EQ(e, 1709210096u);

    /* Round trip across 2000..2099 at an awkward stride, plus boundaries. */
    for (uint32_t ep = 946684800u; ep < 4102444800u; ep += 86399u * 7u + 13u) {
        check_epoch_roundtrip(ep);
    }
    check_epoch_roundtrip(951782400u);   /* 2000-02-29 */
    check_epoch_roundtrip(4102444799u);  /* 2099-12-31 23:59:59 */
    check_epoch_roundtrip(1794096000u);  /* the date used in the docs */

    /* Invalid register contents. */
    ds3231_epoch_to_regs(1709210096u, regs);
    uint8_t save = regs[0];
    regs[0] = 0x7F;
    CHECK(!ds3231_regs_to_epoch(regs, &e));
    regs[0] = save;
    regs[2] |= 0x40;  /* 12-hour mode */
    CHECK(!ds3231_regs_to_epoch(regs, &e));
    regs[2] &= (uint8_t)~0x40;
    regs[4] = 0x00;   /* day 0 */
    CHECK(!ds3231_regs_to_epoch(regs, &e));

    /* Out-of-range epochs clamp rather than corrupt. */
    ds3231_epoch_to_regs(0u, regs);
    CHECK(ds3231_regs_to_epoch(regs, &e));
    CHECK_EQ(e, 946684800u);

    FINISH("test_sensors");
}
