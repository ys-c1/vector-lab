CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

all: build/pgmdiff

build/pgmdiff: pgmdiff.c
	@mkdir -p build
	$(CC) $(CFLAGS) pgmdiff.c -o $@

clean:
	rm -rf build

.PHONY: all clean
