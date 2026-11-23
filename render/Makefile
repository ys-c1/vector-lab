CC ?= gcc
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

# 优化选项：OPT=high（默认，等同旧默认 -O2）或 OPT=off（-O0）。
# 显式传 CFLAGS=... 时 CFLAGS 优先，OPT 仅作校验。
OPT ?= high
VALID_OPT := high off
ifeq ($(filter $(OPT),$(VALID_OPT)),)
$(error 非法 OPT='$(OPT)'，允许集合: $(VALID_OPT))
endif
OPT_CFLAGS_high := -O2 -Wall -Wextra -std=c11
OPT_CFLAGS_off  := -O0 -Wall -Wextra -std=c11
CFLAGS ?= $(OPT_CFLAGS_$(OPT))

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

# 双优化档产物逐像素比对：三组场景，报告像素差/耗时/告警数。
optcheck:
	./optcheck.sh

clean:
	rm -rf build *.pgm

.PHONY: all test optcheck clean
