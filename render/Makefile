CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

clean:
	rm -rf build *.pgm

.PHONY: all test clean

build/check: $(SRC) check.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) check.c -lm -o $@

check: build/check
	./build/check

# 旧整数层路径的逐字节回归：golden/ 下的 PGM 由引入缩放层之前的
# 二进制生成，默认路径（无 --blend）必须与之一致。
golden: build/cli
	@fail=0; \
	for scene in square mixed; do \
	  for sc in 0.25 1 4 1.7; do \
	    ./build/cli golden/$$scene.txt build/$${scene}_$$sc.pgm 40 32 $$sc >/dev/null || exit 1; \
	    if cmp -s golden/$${scene}_$$sc.pgm build/$${scene}_$$sc.pgm; then \
	      echo "IDENTICAL $$scene $$sc"; \
	    else \
	      echo "DIFF $$scene $$sc"; fail=1; \
	    fi; \
	  done; \
	done; \
	exit $$fail

.PHONY: check golden
