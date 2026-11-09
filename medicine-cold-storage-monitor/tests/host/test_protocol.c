#include <string.h>

#include "check.h"
#include "protocol.h"

/* Feed a buffer one byte per millisecond starting at t0; returns frames accepted. */
static int feed(frame_parser_t *p, const uint8_t *buf, size_t n, uint32_t t0)
{
    int ok = 0;
    for (size_t i = 0; i < n; i++) {
        if (frame_parser_feed(p, buf[i], t0 + (uint32_t)i)) {
            ok++;
        }
    }
    return ok;
}

int main(void)
{
    /* CRC-8 standard check value. */
    CHECK_EQ(proto_crc8((const uint8_t *)"123456789", 9), 0xF4);

    /* Telemetry pack/unpack. */
    telemetry_t in = { 1042u, 1794096000u, 445, 6120, ALARM_STATE_NORMAL, 0, 0 };
    uint8_t wire[TELEMETRY_WIRE_LEN];
    CHECK_EQ(telemetry_pack(&in, wire), TELEMETRY_WIRE_LEN);
    telemetry_t out;
    CHECK(telemetry_unpack(wire, sizeof wire, &out));
    CHECK_EQ(out.seq, 1042);
    CHECK_EQ(out.epoch, 1794096000u);
    CHECK_EQ(out.temp_x100, 445);
    CHECK_EQ(out.rh_x100, 6120);

    telemetry_t neg = { 0xFFFFFFFFu, 0u, -1550, 10000, ALARM_STATE_ALARM, 0x0B, 1 };
    telemetry_pack(&neg, wire);
    CHECK(telemetry_unpack(wire, sizeof wire, &out));
    CHECK_EQ(out.temp_x100, -1550);
    CHECK_EQ(out.seq, 0xFFFFFFFFu);
    CHECK_EQ(out.fault, 0x0B);
    CHECK_EQ(out.muted, 1);

    CHECK(!telemetry_unpack(wire, sizeof wire - 1u, &out));
    wire[12] = 3;  /* invalid state */
    CHECK(!telemetry_unpack(wire, sizeof wire, &out));

    /* Encode, then parse: accepted exactly once, at the last byte. */
    telemetry_pack(&in, wire);
    uint8_t frame[FRAME_MAX_LEN];
    int flen = frame_encode(MSG_TELEMETRY, wire, TELEMETRY_WIRE_LEN, frame, sizeof frame);
    CHECK_EQ(flen, 20);
    CHECK_EQ(frame[0], FRAME_SOF);
    CHECK_EQ(frame[flen - 1], FRAME_EOF);

    printf("  golden frame:");
    for (int i = 0; i < flen; i++) {
        printf(" %02X", frame[i]);
    }
    printf("\n");

    frame_parser_t p;
    frame_parser_init(&p);
    CHECK_EQ(feed(&p, frame, (size_t)flen, 0), 1);
    CHECK_EQ(p.frame.msg_type, MSG_TELEMETRY);
    CHECK_EQ(p.frame.len, TELEMETRY_WIRE_LEN);
    CHECK(telemetry_unpack(p.frame.payload, p.frame.len, &out));
    CHECK_EQ(out.seq, 1042);

    /* One corrupted payload byte must be rejected and counted. */
    uint8_t bad[FRAME_MAX_LEN];
    memcpy(bad, frame, (size_t)flen);
    bad[6] ^= 0x10;
    frame_parser_init(&p);
    CHECK_EQ(feed(&p, bad, (size_t)flen, 0), 0);
    CHECK_EQ(p.crc_errors, 1);

    /* Garbage before three back-to-back frames. */
    uint8_t stream[200];
    size_t n = 0;
    const uint8_t junk[] = { 0x00, 0xFF, 0x13, 0x37 };
    memcpy(stream, junk, sizeof junk);
    n += sizeof junk;
    for (int i = 0; i < 3; i++) {
        memcpy(stream + n, frame, (size_t)flen);
        n += (size_t)flen;
    }
    frame_parser_init(&p);
    CHECK_EQ(feed(&p, stream, n, 0), 3);

    /* Noise turns the length byte into 16 (still legal). The parser would wait for bytes
     * that never come. Frames arrive one second apart, so the inter-byte timeout must
     * resynchronise and none of the 5 good frames may be lost. */
    uint8_t badlen[FRAME_MAX_LEN];
    memcpy(badlen, frame, (size_t)flen);
    badlen[2] = 16;
    frame_parser_init(&p);
    int accepted = feed(&p, badlen, (size_t)flen, 0);
    for (int i = 1; i <= 5; i++) {
        accepted += feed(&p, frame, (size_t)flen, (uint32_t)i * 1000u);
    }
    CHECK_EQ(accepted, 5);
    CHECK(p.resyncs >= 1u);

    /* A length byte above the maximum resets the parser immediately. */
    frame_parser_init(&p);
    const uint8_t over[] = { 0xA5, 0x01, 0xFF };
    CHECK_EQ(feed(&p, over, sizeof over, 0), 0);
    CHECK_EQ(feed(&p, frame, (size_t)flen, 10), 1);

    /* Empty-payload command frame. */
    uint8_t hb[FRAME_MAX_LEN];
    int hlen = frame_encode(MSG_CMD_HB, NULL, 0, hb, sizeof hb);
    CHECK_EQ(hlen, 5);
    frame_parser_init(&p);
    CHECK_EQ(feed(&p, hb, (size_t)hlen, 0), 1);
    CHECK_EQ(p.frame.msg_type, MSG_CMD_HB);
    CHECK_EQ(p.frame.len, 0);

    /* Encode limits. */
    uint8_t big[FRAME_MAX_PAYLOAD + 1] = { 0 };
    CHECK_EQ(frame_encode(MSG_TELEMETRY, big, FRAME_MAX_PAYLOAD + 1u, frame, sizeof frame), -1);
    CHECK_EQ(frame_encode(MSG_TELEMETRY, wire, TELEMETRY_WIRE_LEN, frame, 10), -1);

    FINISH("test_protocol");
}
