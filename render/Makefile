CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
# 原有管线只含 scene/raster/pipeline/export，窗口旋转是新加的独立目标。
BASE_SRC := $(filter-out src/winrot.c,$(SRC))

all: test

test: build/test
	./build/test

test-winrot: build/test_winrot
	./build/test_winrot

verify-winrot: build/winrot_cli build/cli build/cli_zero build/test_winrot
	./verify_winrot.sh

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

build/test_winrot: $(SRC) tests/test_winrot.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) tests/test_winrot.c -lm -o $@

build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

# 零旋转基线：只编原有管线，产出的 PGM 必须与 winrot_cli 的零旋转分支逐字节相同。
build/cli_zero: $(BASE_SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(BASE_SRC) cli.c -lm -o $@

build/winrot_cli: $(SRC) winrot_cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) winrot_cli.c -lm -o $@

clean:
	rm -rf build *.pgm

.PHONY: all test test-winrot verify-winrot clean
