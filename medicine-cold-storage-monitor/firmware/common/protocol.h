/*
 * protocol.h - binary UART protocol shared by the MCXC444 node and RW612 gateway.
 *
 * Frame:  SOF(0xA5) | type | len | payload[len] | CRC8 | EOF(0x5A)
 * CRC-8 (poly 0x07, init 0x00, no reflection, no final XOR) covers type, len, payload.
 * All multi-byte fields are little-endian and serialised byte by byte, so there are
 * no packed structs and no unaligned access (which hard-faults on Cortex-M0+).
 *
 * See docs/protocol.md.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAME_SOF                  0xA5u
#define FRAME_EOF                  0x5Au
#define FRAME_MAX_PAYLOAD          16u
#define FRAME_OVERHEAD             5u   /* SOF, type, len, CRC, EOF */
#define FRAME_MAX_LEN              (FRAME_MAX_PAYLOAD + FRAME_OVERHEAD)
#define FRAME_INTERBYTE_TIMEOUT_MS 50u  /* a partial frame older than this is abandoned */

typedef enum {
    MSG_TELEMETRY = 0x01,  /* node -> gateway */
    MSG_CMD_TIME  = 0x10,  /* gateway -> node, payload: u32 UTC epoch */
    MSG_CMD_MUTE  = 0x11,  /* gateway -> node, no payload */
    MSG_CMD_HB    = 0x12   /* gateway -> node, no payload */
} msg_type_t;

/* Fault bitmask carried in telemetry. */
enum {
    FAULT_NONE       = 0x00,
    FAULT_SENSOR     = 0x01,  /* SHT40 read failure or bad CRC (debounced) */
    FAULT_RTC        = 0x02,  /* RTC invalid: power lost, oscillator stopped, or no reply */
    FAULT_OLED       = 0x04,  /* display not responding (non-critical) */
    FAULT_NO_GATEWAY = 0x08   /* gateway heartbeat missing */
};

typedef enum {
    ALARM_STATE_NORMAL  = 0,
    ALARM_STATE_WARNING = 1,
    ALARM_STATE_ALARM   = 2
} alarm_state_t;

#define TELEMETRY_WIRE_LEN 15u

typedef struct {
    uint32_t seq;        /* increments every frame; gateway detects gaps */
    uint32_t epoch;      /* UTC seconds from the RTC, 0 = time not valid */
    int16_t  temp_x100;  /* degrees C * 100; last good value if FAULT_SENSOR is set */
    uint16_t rh_x100;    /* % RH * 100 */
    uint8_t  state;      /* alarm_state_t */
    uint8_t  fault;      /* FAULT_* bitmask */
    uint8_t  muted;      /* 1 if the buzzer is muted */
} telemetry_t;

typedef struct {
    uint8_t msg_type;
    uint8_t len;
    uint8_t payload[FRAME_MAX_PAYLOAD];
} frame_t;

/* CRC-8, poly 0x07. */
uint8_t proto_crc8(const uint8_t *data, size_t len);

size_t telemetry_pack(const telemetry_t *t, uint8_t out[TELEMETRY_WIRE_LEN]);
bool   telemetry_unpack(const uint8_t *in, size_t len, telemetry_t *t);

/* Returns total frame length, or -1 if the payload is too long or the buffer too small. */
int frame_encode(uint8_t msg_type, const uint8_t *payload, uint8_t payload_len,
                 uint8_t *out, size_t out_max);

/* Byte-at-a-time parser with an inter-byte timeout so a corrupted length byte
 * cannot swallow the next frame. */
typedef struct {
    uint8_t  state;
    uint8_t  index;
    uint8_t  crc_rx;
    uint32_t last_byte_ms;
    frame_t  frame;         /* valid after frame_parser_feed() returns true */
    uint32_t frames_ok;
    uint32_t crc_errors;
    uint32_t resyncs;
} frame_parser_t;

void frame_parser_init(frame_parser_t *p);

/* Returns true when a complete, CRC-valid frame is available in p->frame. */
bool frame_parser_feed(frame_parser_t *p, uint8_t byte, uint32_t now_ms);

#endif /* PROTOCOL_H */
