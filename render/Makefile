CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
# 原有管线只含 scene/raster/pipeline/export；winplan 是独立新增目标。
BASE_SRC := $(filter-out src/winplan.c,$(SRC))

all: test

test: build/test test-winplan
	./build/test
	./build/test_winplan

build/test: $(BASE_SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(BASE_SRC) test.c -lm -o $@

build/test_winplan: $(SRC) tests/test_winplan.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) tests/test_winplan.c -lm -o $@

test-winplan: build/test_winplan
	./build/test_winplan

build/cli: $(BASE_SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(BASE_SRC) cli.c -lm -o $@

build/winplan_cli: $(SRC) winplan_cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) winplan_cli.c -lm -o $@

verify-winplan: build/winplan_cli build/cli build/test_winplan
	./verify_winplan.sh

clean:
	rm -rf build *.pgm

.PHONY: all test test-winplan verify-winplan clean
