CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra -fPIC
LDFLAGS ?= -shared -ldl

ROOT := $(CURDIR)
OUT := $(ROOT)/bin/libvaapi_probe.so
SRC := $(ROOT)/src/vaapi_probe.c

.PHONY: all clean

all: $(OUT)

$(OUT): $(SRC)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

clean:
	rm -f $(OUT)
