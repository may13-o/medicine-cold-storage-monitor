/*
 * ds3231.h - register <-> UTC epoch conversion for a DS3231-style RTC.
 *
 * ASSUMES the RTC module is a DS3231 (I2C 0x68). If yours is a different part
 * (for example DS1307 or PCF8523), only this file and the register constants change.
 * Time is stored as UTC, 24-hour mode, years 2000..2099.
 */
#ifndef DS3231_H
#define DS3231_H

#include <stdbool.h>
#include <stdint.h>

#define DS3231_I2C_ADDR   0x68u
#define DS3231_REG_TIME   0x00u   /* 7 registers: sec, min, hour, dow, date, month, year */
#define DS3231_REG_STATUS 0x0Fu
#define DS3231_STATUS_OSF 0x80u   /* oscillator stopped: time cannot be trusted */

/* Returns false if any field is out of range, a BCD digit is invalid, or 12-hour mode is set. */
bool ds3231_regs_to_epoch(const uint8_t regs[7], uint32_t *epoch);

/* Epochs outside 2000-01-01..2099-12-31 are clamped. */
void ds3231_epoch_to_regs(uint32_t epoch, uint8_t regs[7]);

#endif /* DS3231_H */
