#include "sht40.h"

uint8_t sht40_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

bool sht40_parse(const uint8_t raw[6], int16_t *temp_x100, uint16_t *rh_x100)
{
    if (sht40_crc8(&raw[0], 2) != raw[2] || sht40_crc8(&raw[3], 2) != raw[5]) {
        return false;
    }

    uint32_t t_ticks  = ((uint32_t)raw[0] << 8) | raw[1];
    uint32_t rh_ticks = ((uint32_t)raw[3] << 8) | raw[4];

    /* T  = -45 + 175 * ticks / 65535  ->  x100: -4500 + 17500 * ticks / 65535
     * RH =  -6 + 125 * ticks / 65535  ->  x100:  -600 + 12500 * ticks / 65535
     * 65535 * 17500 fits in 32 bits. +32767 rounds to nearest. */
    int32_t t  = -4500 + (int32_t)((t_ticks * 17500u + 32767u) / 65535u);
    int32_t rh = -600 + (int32_t)((rh_ticks * 12500u + 32767u) / 65535u);

    if (rh < 0) {
        rh = 0;
    }
    if (rh > 10000) {
        rh = 10000;
    }

    *temp_x100 = (int16_t)t;
    *rh_x100 = (uint16_t)rh;
    return true;
}
