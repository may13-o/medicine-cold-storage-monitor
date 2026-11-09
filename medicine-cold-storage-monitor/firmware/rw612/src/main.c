/*
 * FRDM-RW612 gateway - entry point.
 * Logic lives in gw_core.c; hardware and network live in board_rw612.c.
 */
#include "board_rw612.h"
#include "gw_core.h"

int main(void)
{
    static gw_core_t gw;

    gw_config_t cfg;
    gw_config_default(&cfg);
    /* cfg.node_id = "node1";            set per device */
    /* cfg.format  = GW_FMT_THINGSBOARD; if the cloud platform is ThingsBoard */

    gw_io_t io = {
        .net_is_up   = board_net_is_up,
        .net_publish = board_net_publish,
        .uart_write  = board_uart_write,
        .utc_now     = board_utc_now,
        .millis      = board_millis
    };

    board_init();
    gw_core_init(&gw, &cfg, &io);

    for (;;) {
        uint8_t b;
        while (board_uart_read_byte(&b)) {
            gw_core_on_byte(&gw, b);
        }
        gw_core_poll(&gw);
        board_idle();
    }
}
