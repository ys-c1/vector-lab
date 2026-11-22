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

demo: build/demo
	./build/demo | tee demo.txt

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

# 新逻辑（按目标面积反解宽度）：独立开关，默认不参与 test/demo。
# 关闭时上面的 test/demo 与基线产物逐字节相同（make regress 核对）。
test-area: build/test-area
	./build/test-area

build/test-area: $(SRC) test_area.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSTROKE_AREA_FIT $(SRC) test_area.c -lm -o $@

demo-area: build/demo-area
	./build/demo-area

build/demo-area: $(SRC) demo_area.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSTROKE_AREA_FIT $(SRC) demo_area.c -lm -o $@

# 固定宽度位图产物：与旧 stroke.h 的固定宽度轮廓同源，新逻辑开关关闭时
# stroke_area.c 是空编译单元，figure.pgm 与 baseline/figure.pgm 逐字节相同。
figure: build/figure
	./build/figure figure.pgm

build/figure: $(SRC) figure.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) figure.c -lm -o $@

# 独立核验：逐像素光栅计数覆盖面积，与目标比对；偏差越大越说明反解错误
verify: build/verify
	./build/verify 1200 0.01 400

build/verify: $(SRC) verify_area.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSTROKE_AREA_FIT $(SRC) verify_area.c -lm -o $@

# 旧路径回归：新逻辑默认关闭，demo.txt / figure.pgm 必须与基线逐字节相同
regress: demo figure
	../tools/diff.sh demo.txt baseline/demo.txt
	../tools/diff.sh figure.pgm baseline/figure.pgm

clean:
	rm -rf build demo.txt figure.pgm

.PHONY: all test demo test-area demo-area figure verify regress clean
