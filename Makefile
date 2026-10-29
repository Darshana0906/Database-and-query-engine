CC     ?= gcc
CFLAGS ?= -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Wpedantic -Iinclude
LIB    := src/common.c src/bptree.c src/hashidx.c src/trie.c src/itree.c \
          src/heap.c src/table.c src/sql.c src/engine.c
BIN    := build

.PHONY: all demo clean seed

all: $(BIN)/gatedb $(BIN)/gen

$(BIN):
	mkdir -p $(BIN)

$(BIN)/gatedb: $(LIB) src/main.c | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN)/gen: tools/gen.c | $(BIN)
	$(CC) $(CFLAGS) $< -o $@

seed: $(BIN)/gen
	$(BIN)/gen data/demo 2500 15000

demo: all
	$(BIN)/gatedb -f scripts/demo.sql

clean:
	rm -rf $(BIN)