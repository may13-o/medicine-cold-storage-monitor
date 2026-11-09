CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O0 -g

COMMON = ../../firmware/common
NODE   = ../../firmware/mcxc444
GW     = ../../firmware/rw612
INC    = -I$(COMMON) -I$(NODE)/include -I$(GW)/include
BIN    = bin

NODE_LOGIC = $(NODE)/src/alarm.c $(NODE)/src/sht40.c $(NODE)/src/ds3231.c $(NODE)/src/node_app.c
GW_LOGIC   = $(GW)/src/gw_core.c $(GW)/src/record_queue.c
HEADERS    = $(wildcard $(COMMON)/*.h $(NODE)/include/*.h $(GW)/include/*.h) check.h

TESTS = test_protocol test_sensors test_alarm test_gateway test_node_sim

.PHONY: test smoke clean
test: $(addprefix $(BIN)/,$(TESTS))
	@set -e; for t in $(TESTS); do echo "== $$t"; $(BIN)/$$t; done
	@echo; echo "ALL HOST TESTS PASSED"

$(BIN):
	mkdir -p $(BIN)

$(BIN)/test_protocol: test_protocol.c $(COMMON)/protocol.c $(HEADERS) | $(BIN)
	$(CC) $(CFLAGS) $(INC) test_protocol.c $(COMMON)/protocol.c -o $@

$(BIN)/test_sensors: test_sensors.c $(NODE)/src/sht40.c $(NODE)/src/ds3231.c $(HEADERS) | $(BIN)
	$(CC) $(CFLAGS) $(INC) test_sensors.c $(NODE)/src/sht40.c $(NODE)/src/ds3231.c -lm -o $@

$(BIN)/test_alarm: test_alarm.c $(NODE)/src/alarm.c $(COMMON)/protocol.c $(HEADERS) | $(BIN)
	$(CC) $(CFLAGS) $(INC) test_alarm.c $(NODE)/src/alarm.c $(COMMON)/protocol.c -o $@

$(BIN)/test_gateway: test_gateway.c $(GW_LOGIC) $(COMMON)/protocol.c $(HEADERS) | $(BIN)
	$(CC) $(CFLAGS) $(INC) test_gateway.c $(GW_LOGIC) $(COMMON)/protocol.c -o $@

$(BIN)/test_node_sim: test_node_sim.c mock_hal.c mock_hal.h $(NODE_LOGIC) $(COMMON)/protocol.c $(HEADERS) | $(BIN)
	$(CC) $(CFLAGS) $(INC) -I. test_node_sim.c mock_hal.c $(NODE_LOGIC) $(COMMON)/protocol.c -o $@

# Compile-only check that the entry points and board stubs build against the headers.
smoke: | $(BIN)
	$(CC) $(CFLAGS) $(INC) -c $(NODE)/src/main.c -o $(BIN)/node_main.o
	$(CC) $(CFLAGS) $(INC) -c $(NODE)/src/hal_mcxc444.c -o $(BIN)/hal_mcxc444.o
	$(CC) $(CFLAGS) $(INC) -c $(GW)/src/main.c -o $(BIN)/gw_main.o
	$(CC) $(CFLAGS) $(INC) -c $(GW)/src/board_rw612.c -o $(BIN)/board_rw612.o
	@echo "smoke build OK"

clean:
	rm -rf $(BIN)
