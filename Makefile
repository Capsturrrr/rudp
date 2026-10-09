CC := gcc
CFLAGS := -Wall -Wextra -g -std=c11
SRC_DIR := src
BIN_DIR := bin

COMMON_SRC := $(SRC_DIR)/common.c

all: $(BIN_DIR)/server $(BIN_DIR)/client

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/server: $(SRC_DIR)/server.c $(COMMON_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN_DIR)/client: $(SRC_DIR)/client.c $(COMMON_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf $(BIN_DIR)

.PHONY: all clean

# Smart-RUDP (RL congestion control) -- needs -lm; server is built per port
$(BIN_DIR)/smart_client: $(SRC_DIR)/smart_client.c $(SRC_DIR)/rl_cc.h $(COMMON_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -D_GNU_SOURCE -o $@ $(SRC_DIR)/smart_client.c $(COMMON_SRC) -lm

smart: $(BIN_DIR)/server $(BIN_DIR)/smart_client
.PHONY: smart

# RUDP Chat demo (two-way messenger over RUDP packets)
$(BIN_DIR)/chat: $(SRC_DIR)/chat.c $(COMMON_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

chat: $(BIN_DIR)/chat
.PHONY: chat

# Selective-ACK server (buffers out-of-order packets, reports them in a bitmap)
$(BIN_DIR)/server_sack: $(SRC_DIR)/server.c $(COMMON_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -DRUDP_SACK -o $@ $(SRC_DIR)/server.c $(COMMON_SRC)

sack: $(BIN_DIR)/server_sack $(BIN_DIR)/smart_client
.PHONY: sack

# Run the whole test suite (wire format, end to end, transport modes, simulator)
test:
	bash tests/run_tests.sh
.PHONY: test

# Quick tour: build everything, run the tests, print how to start the demos
demo: test
	@echo; echo 'Demos: see docs/CHAT_ONLINE.txt and docs/RUDP_Demo_Runbook.html'
.PHONY: demo
