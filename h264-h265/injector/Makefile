CC ?= cc
CFLAGS ?= -O2 -g -fPIC -Wall -Wextra
LDFLAGS ?= -shared -ldl

ROOT := $(abspath ..)
BIN := bin
SCOUT_TARGET := $(BIN)/libchrome_hevc_delegate_scout.so
TRAMPOLINE_TARGET := $(BIN)/libchrome_hevc_trampoline_probe.so

.PHONY: all clean

all: $(SCOUT_TARGET) $(TRAMPOLINE_TARGET)

$(BIN):
	mkdir -p $(BIN)

$(SCOUT_TARGET): src/injector_scout.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(TRAMPOLINE_TARGET): src/trampoline_probe.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -rf $(BIN)
