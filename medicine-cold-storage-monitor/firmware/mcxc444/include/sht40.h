/*
 * sht40.h - pure conversion helpers for the Sensirion SHT40.
 * The I2C transfer itself lives in node_app.c via the HAL.
 */
#ifndef SHT40_H
#define SHT40_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHT40_I2C_ADDR         0x44u
#define SHT40_CMD_MEASURE_HIGH 0xFDu   /* high-precision measurement */
#define SHT40_MEASURE_WAIT_MS  10u     /* datasheet maximum is about 8.3 ms */

/* Sensirion CRC-8: poly 0x31, init 0xFF. */
uint8_t sht40_crc8(const uint8_t *data, size_t len);

/* raw = T_hi, T_lo, T_crc, RH_hi, RH_lo, RH_crc. Returns false if either CRC fails.
 * temp in degrees C * 100; RH in % * 100, clamped to 0..10000. */
bool sht40_parse(const uint8_t raw[6], int16_t *temp_x100, uint16_t *rh_x100);

#endif /* SHT40_H */
