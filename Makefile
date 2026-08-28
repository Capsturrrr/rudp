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
