/*
 * FRDM-MCXC444: SHT40 (0x44) + DS3231 RTC (0x68) on the mikroBUS I2C bus
 * I2C1: PTC1 = SCL, PTC2 = SDA (ALT2). Polling mode, 100 kHz.
 * Both sensors share the same two wires; only the slave address differs.
 */
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "fsl_clock.h"
#include "fsl_i2c.h"
#include "fsl_lpuart.h"
#include "fsl_port.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define APP_I2C_BASE        I2C1
#define APP_I2C_CLK_FREQ    CLOCK_GetFreq(I2C1_CLK_SRC)
#define APP_I2C_BAUDRATE    100000U

/* SHT40 */
#define SHT40_I2C_ADDR              0x44U
#define SHT40_CMD_SOFT_RESET        0x94U
#define SHT40_CMD_MEASURE_HIGH_PREC 0xFDU

/* DS3231 */
#define DS3231_I2C_ADDR     0x68U
#define DS3231_REG_TIME     0x00U
#define DS3231_REG_STATUS   0x0FU
#define DS3231_OSF_MASK     0x80U

/*******************************************************************************
 * Shared I2C helpers (one bus, address passed per call)
 ******************************************************************************/
static void App_InitI2CPins(void)
{
    CLOCK_EnableClock(kCLOCK_PortC);
    PORT_SetPinMux(PORTC, 1U, kPORT_MuxAlt2); /* I2C1_SCL */
    PORT_SetPinMux(PORTC, 2U, kPORT_MuxAlt2); /* I2C1_SDA */
}

static status_t App_I2CWrite(uint8_t addr, uint8_t reg, bool useReg, const uint8_t *data, size_t len)
{
    i2c_master_transfer_t xfer;
    memset(&xfer, 0, sizeof(xfer));

    xfer.slaveAddress   = addr;
    xfer.direction      = kI2C_Write;
    xfer.subaddress     = reg;
    xfer.subaddressSize = useReg ? 1U : 0U;
    xfer.data           = (uint8_t *)data;
    xfer.dataSize       = len;
    xfer.flags          = kI2C_TransferDefaultFlag;

    return I2C_MasterTransferBlocking(APP_I2C_BASE, &xfer);
}

static status_t App_I2CRead(uint8_t addr, uint8_t reg, bool useReg, uint8_t *data, size_t len)
{
    i2c_master_transfer_t xfer;
    memset(&xfer, 0, sizeof(xfer));

    xfer.slaveAddress   = addr;
    xfer.direction      = kI2C_Read;
    xfer.subaddress     = reg;
    xfer.subaddressSize = useReg ? 1U : 0U;
    xfer.data           = data;
    xfer.dataSize       = len;
    xfer.flags          = kI2C_TransferDefaultFlag;

    return I2C_MasterTransferBlocking(APP_I2C_BASE, &xfer);
}

/*******************************************************************************
 * SHT40
 ******************************************************************************/
/* Sensirion CRC-8: poly 0x31, init 0xFF */
static uint8_t SHT40_Crc8(const uint8_t *d, size_t n)
{
    uint8_t crc = 0xFFU;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x31U) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* Returns temperature and humidity in hundredths (e.g. 2534 = 25.34) */
static status_t SHT40_Read(int32_t *tempX100, int32_t *humX100)
{
    uint8_t rx[6] = {0};
    uint8_t cmd   = SHT40_CMD_MEASURE_HIGH_PREC;
    status_t st;

    st = App_I2CWrite(SHT40_I2C_ADDR, 0U, false, &cmd, 1U);
    if (st != kStatus_Success)
    {
        return st;
    }

    SDK_DelayAtLeastUs(10000U, SystemCoreClock); /* high precision: 8.3 ms max */

    st = App_I2CRead(SHT40_I2C_ADDR, 0U, false, rx, sizeof(rx));
    if (st != kStatus_Success)
    {
        return st;
    }

    if (SHT40_Crc8(&rx[0], 2U) != rx[2] || SHT40_Crc8(&rx[3], 2U) != rx[5])
    {
        return kStatus_Fail; /* CRC error */
    }

    uint32_t rawT = ((uint32_t)rx[0] << 8) | rx[1];
    uint32_t rawH = ((uint32_t)rx[3] << 8) | rx[4];

    /* T = -45 + 175*raw/65535 ; RH = -6 + 125*raw/65535 (integer math, x100) */
    *tempX100 = -4500 + (int32_t)((17500UL * rawT) / 65535UL);
    *humX100  = -600 + (int32_t)((12500UL * rawH) / 65535UL);

    if (*humX100 > 10000) *humX100 = 10000;
    if (*humX100 < 0)     *humX100 = 0;

    return kStatus_Success;
}

/*******************************************************************************
 * DS3231
 ******************************************************************************/
/* Set to 1, build and flash ONCE to load the PC's build time into the RTC,
 * then set back to 0 and flash again (otherwise every reset rewrites the time). */
#define SET_RTC_FROM_BUILD  0

static uint8_t bcd2dec(uint8_t v) { return (uint8_t)(((v >> 4) * 10U) + (v & 0x0FU)); }
static uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10U) << 4) | (v % 10U)); }

/* SDK PRINTF pads "%02d" with spaces, so zero-pad by hand */
static void Print2(unsigned v)
{
    if (v < 10U)
    {
        PRINTF("0");
    }
    PRINTF("%u", v);
}

#if SET_RTC_FROM_BUILD
static void DS3231_SetFromBuildTime(void)
{
    const char *t = __TIME__; /* "HH:MM:SS" */
    uint8_t h = (uint8_t)((t[0] - '0') * 10 + (t[1] - '0'));
    uint8_t m = (uint8_t)((t[3] - '0') * 10 + (t[4] - '0'));
    uint8_t s = (uint8_t)((t[6] - '0') * 10 + (t[7] - '0'));
    uint8_t buf[3] = {dec2bcd(s), dec2bcd(m), dec2bcd(h)}; /* 24h mode */

    if (App_I2CWrite(DS3231_I2C_ADDR, DS3231_REG_TIME, true, buf, 3U) == kStatus_Success)
    {
        PRINTF("[INFO] RTC set to build time %s\r\n", __TIME__);
    }
    else
    {
        PRINTF("[FAIL] RTC time write failed\r\n");
    }
}
#endif

static status_t DS3231_Init(void)
{
    uint8_t statusReg = 0;
    status_t st = App_I2CRead(DS3231_I2C_ADDR, DS3231_REG_STATUS, true, &statusReg, 1U);
    if (st != kStatus_Success)
    {
        PRINTF("[FAIL] DS3231 not found at 0x68 (status %d). Check SDA/SCL/3V3/GND.\r\n", (int)st);
        return st;
    }
    PRINTF("[PASS] DS3231 detected. Status reg: 0x%X\r\n", statusReg);

    if (statusReg & DS3231_OSF_MASK)
    {
        PRINTF("[WARN] Oscillator stopped (OSF set). Setting time to 12:00:00.\r\n");
        uint8_t initTime[3] = {dec2bcd(0), dec2bcd(0), dec2bcd(12)}; /* sec, min, hour (24h) */
        (void)App_I2CWrite(DS3231_I2C_ADDR, DS3231_REG_TIME, true, initTime, 3U);

        statusReg &= (uint8_t)~DS3231_OSF_MASK;
        (void)App_I2CWrite(DS3231_I2C_ADDR, DS3231_REG_STATUS, true, &statusReg, 1U);
    }
    else
    {
        PRINTF("[PASS] RTC oscillator running.\r\n");
    }
    return kStatus_Success;
}

static status_t DS3231_ReadTime(uint8_t *hour, uint8_t *min, uint8_t *sec)
{
    uint8_t raw[3] = {0};
    status_t st = App_I2CRead(DS3231_I2C_ADDR, DS3231_REG_TIME, true, raw, 3U);
    if (st == kStatus_Success)
    {
        *sec  = bcd2dec(raw[0] & 0x7FU);
        *min  = bcd2dec(raw[1] & 0x7FU);
        *hour = bcd2dec(raw[2] & 0x3FU);
    }
    return st;
}

/*******************************************************************************
 * Main
 ******************************************************************************/
/*******************************************************************************
 * LPUART1 -> ESP32 (mikroBUS TX = PTE0, RX = PTE1, ALT3), TX only.
 * 115200 8N1. Sends one JSON line per reading, e.g.
 *   {"t":29.45,"h":81.30,"ts":"13:40:01"}\n
 * LPUART0 stays on the debug console (PTA1/PTA2).
 ******************************************************************************/
#define ESP_UART        LPUART1
#define ESP_UART_BAUD   115200U

static void App_InitUart(void)
{
    lpuart_config_t cfg;

    CLOCK_EnableClock(kCLOCK_PortE);
    PORT_SetPinMux(PORTE, 0U, kPORT_MuxAlt3); /* LPUART1_TX */
    PORT_SetPinMux(PORTE, 1U, kPORT_MuxAlt3); /* LPUART1_RX (not used yet) */

    /* LPUART1 needs its own clock source selected. Use the same selection value
     * the board uses for the debug console's LPUART0, and the same frequency. */
    CLOCK_SetLpuart1Clock(0x1U);

    LPUART_GetDefaultConfig(&cfg);
    cfg.baudRate_Bps = ESP_UART_BAUD;
    cfg.enableTx     = true;
    cfg.enableRx     = false;
    LPUART_Init(ESP_UART, &cfg, BOARD_DEBUG_UART_CLK_FREQ);
}

static void App_UartSend(const char *s)
{
    LPUART_WriteBlocking(ESP_UART, (const uint8_t *)s, strlen(s));
}

/* Tiny string builders (no printf, so no padding/format surprises) */
static char *PutStr(char *p, const char *s)
{
    while (*s)
    {
        *p++ = *s++;
    }
    return p;
}

static char *PutUInt(char *p, unsigned v)
{
    char tmp[6];
    int n = 0;
    do
    {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v && n < 5);
    while (n)
    {
        *p++ = tmp[--n];
    }
    return p;
}

static char *Put2(char *p, unsigned v)
{
    *p++ = (char)('0' + ((v / 10U) % 10U));
    *p++ = (char)('0' + (v % 10U));
    return p;
}

static char *PutX100(char *p, int32_t v)
{
    if (v < 0)
    {
        *p++ = '-';
        v    = -v;
    }
    p    = PutUInt(p, (unsigned)(v / 100));
    *p++ = '.';
    return Put2(p, (unsigned)(v % 100));
}

static void App_SendJson(int32_t t, int32_t rh, bool rtcOk, uint8_t h, uint8_t m, uint8_t s)
{
    char buf[64];
    char *p = buf;

    p = PutStr(p, "{\"t\":");
    p = PutX100(p, t);
    p = PutStr(p, ",\"h\":");
    p = PutX100(p, rh);
    p = PutStr(p, ",\"ts\":");
    if (rtcOk)
    {
        *p++ = '"';
        p    = Put2(p, h);
        *p++ = ':';
        p    = Put2(p, m);
        *p++ = ':';
        p    = Put2(p, s);
        *p++ = '"';
    }
    else
    {
        p = PutStr(p, "null");
    }
    p  = PutStr(p, "}\n");
    *p = '\0';

    App_UartSend(buf);
}

static void PrintX100(int32_t v)
{
    const char *sign = (v < 0) ? "-" : "";
    if (v < 0) v = -v;
    PRINTF("%s%d.", sign, (int)(v / 100));
    Print2((unsigned)(v % 100));
}

int main(void)
{
    i2c_master_config_t cfg;

    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
    App_InitI2CPins(); /* after BootPins so our I2C mux is the final setting */

    PRINTF("\r\nFRDM-MCXC444: SHT40 + DS3231 on mikroBUS I2C1\r\n");

    I2C_MasterGetDefaultConfig(&cfg);
    cfg.baudRate_Bps = APP_I2C_BAUDRATE;
    I2C_MasterInit(APP_I2C_BASE, &cfg, APP_I2C_CLK_FREQ);
    App_InitUart();

    /* SHT40 soft reset (needs >= 1 ms afterwards) */
    uint8_t rst = SHT40_CMD_SOFT_RESET;
    if (App_I2CWrite(SHT40_I2C_ADDR, 0U, false, &rst, 1U) == kStatus_Success)
    {
        PRINTF("[PASS] SHT40 acknowledged reset.\r\n");
    }
    else
    {
        PRINTF("[FAIL] SHT40 NACK at 0x44. Check wiring.\r\n");
    }
    SDK_DelayAtLeastUs(2000U, SystemCoreClock);

    (void)DS3231_Init(); /* keeps running even if RTC is missing */
#if SET_RTC_FROM_BUILD
    DS3231_SetFromBuildTime();
#endif

    while (1)
    {
        uint8_t h = 0, m = 0, s = 0;
        int32_t t = 0, rh = 0;
        bool rtcOk = (DS3231_ReadTime(&h, &m, &s) == kStatus_Success);

        if (rtcOk)
        {
            Print2(h);
            PRINTF(":");
            Print2(m);
            PRINTF(":");
            Print2(s);
            PRINTF(" | ");
        }
        else
        {
            PRINTF("--:--:-- (RTC err) | ");
        }

        if (SHT40_Read(&t, &rh) == kStatus_Success)
        {
            PRINTF("Temp: ");
            PrintX100(t);
            PRINTF(" C | RH: ");
            PrintX100(rh);
            PRINTF(" %%\r\n");
            App_SendJson(t, rh, rtcOk, h, m, s); /* forward to ESP32 */
        }
        else
        {
            PRINTF("SHT40 read error\r\n");
        }

        SDK_DelayAtLeastUs(1000000U, SystemCoreClock);
    }
}