/*
 * FRDM-MCXC444 cold-storage node - entry point.
 * All behaviour lives in node_app.c; all hardware access lives behind hal.h.
 */
#include "hal.h"
#include "node_app.h"

int main(void)
{
    static node_t node;

    hal_init();       /* must also start the watchdog (suggest 2 to 4 s timeout) */
    node_init(&node);

    for (;;) {
        node_step(&node);
        hal_delay_ms(5u);
    }
}
