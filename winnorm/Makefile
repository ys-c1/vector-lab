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

build/run: $(SRC) run.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) run.c -lm -o $@

build/fuzz: $(SRC) tests/fuzz_arrange.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) tests/fuzz_arrange.c -lm -o $@

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

# 关掉新逻辑的基线：直接用 render 模块的旧代码，不经过 winnorm。
RENDER_SRC := $(wildcard ../render/src/*.c)
RENDER_HDR := $(wildcard ../render/src/*.h)
build/legacy: baseline/legacy.c $(RENDER_SRC) $(RENDER_HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -I../render/src baseline/legacy.c $(RENDER_SRC) -lm -o $@

data:
	python3 -I tests/gen_windows.py data

run: build/run
	@echo "驱动就绪：./build/run report|render ..."

demo: build/demo build/legacy data
	@mkdir -p run-out
	./build/demo | tee demo.txt

verify: build/run build/legacy build/test build/fuzz data
	./build/test
	./build/fuzz
	./verify.sh

clean:
	rm -rf build demo.txt data run-out

.PHONY: all test run demo data verify fuzz clean
