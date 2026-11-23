CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
# 串行 CLI 不编入并发实现，也不链接 pthread。
SERIAL_SRC := $(filter-out src/parallel.c,$(SRC))

all: test

test: build/test
	./build/test

verify: build/cli build/pcli
	../tools/verify_parallel.sh

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -pthread -o $@

build/cli: $(SERIAL_SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SERIAL_SRC) cli.c -lm -o $@

build/pcli: $(SRC) pcli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) pcli.c -lm -pthread -o $@

clean:
	rm -rf build *.pgm

.PHONY: all test verify clean
