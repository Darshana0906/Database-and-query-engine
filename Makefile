CC     ?= gcc
CFLAGS ?= -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Wpedantic -Iinclude
LIB    := src/common.c src/bptree.c src/hashidx.c src/trie.c src/itree.c \
          src/heap.c src/table.c src/sql.c src/engine.c
BIN    := build

.PHONY: all demo clean

all: $(BIN)/gatedb

$(BIN):
	mkdir -p $(BIN)

$(BIN)/gatedb: $(LIB) src/main.c | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@

demo: all
	$(BIN)/gatedb -f scripts/demo.sql

clean:
	rm -rf $(BIN)