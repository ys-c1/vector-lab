CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
ART := ../artifacts

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

demo: build/demo
	./build/demo

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

# 多窗口顺序裁决驱动
multi: build/multi
	@mkdir -p $(ART)/multi
	./build/multi --all $(ART)/multi

build/multi: $(SRC) multi.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) multi.c -lm -o $@

# 关闭新逻辑（CLIP_NO_MULTI）构建，验证单窗裁剪逐字节不变
legacy: build/legacy_test build/legacy_demo

build/legacy_test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCLIP_NO_MULTI src/clip.c test.c -lm -o $@

build/legacy_demo: src/clip.c demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCLIP_NO_MULTI src/clip.c demo.c -lm -o $@

# 新旧单窗路径逐字节比对：同一组输入，fwrite 裸 double 坐标与面积
bytecheck: build/probe build/legacy_probe legacy
	@mkdir -p $(ART)/bytecheck
	./build/probe        > $(ART)/bytecheck/new.bin
	./build/legacy_probe > $(ART)/bytecheck/legacy.bin
	cmp $(ART)/bytecheck/new.bin $(ART)/bytecheck/legacy.bin && \
		echo "BYTE-IDENTICAL single-window path"
	./build/legacy_test > $(ART)/bytecheck/legacy_test.out
	cmp $(ART)/bytecheck/baseline/test.out $(ART)/bytecheck/legacy_test.out && \
		echo "BYTE-IDENTICAL legacy test stdout vs baseline"
	./build/legacy_demo > $(ART)/bytecheck/legacy_demo.out
	cmp $(ART)/bytecheck/baseline/demo.out $(ART)/bytecheck/legacy_demo.out && \
		echo "BYTE-IDENTICAL legacy demo stdout vs baseline"

build/probe: src/clip.c probe.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/clip.c probe.c -lm -o $@

build/legacy_probe: src/clip.c probe.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCLIP_NO_MULTI src/clip.c probe.c -lm -o $@

clean:
	rm -rf build demo.txt

.PHONY: all test demo multi legacy bytecheck clean
