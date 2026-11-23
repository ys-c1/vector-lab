CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
SCENES := s1_convex s2_concave s3_multi s4_compound

all: test

test: build/test
	./build/test

build/test: $(SRC) test.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) test.c -lm -o $@

build/cli: $(SRC) cli.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) cli.c -lm -o $@

# 四类场景各出一图 + 一份元数据，序号文件独立放在 build/demo/
demo: build/cli
	@mkdir -p build/demo
	@rm -f build/demo/render.seq build/demo/*.pgm build/demo/*.meta.json
	@i=1; for s in $(SCENES); do \
	  ./build/cli --seq-file build/demo/render.seq \
	    scenes/$$s.txt build/demo/$$s.pgm 64 64 1.0; \
	  ../tools/metacheck.sh scenes/$$s.txt build/demo/$$s.pgm \
	    build/demo/$$s.meta.json build/demo/render.seq $$i || exit 1; \
	  i=$$((i+1)); \
	done
	@echo "--- metadata ---"
	@for s in $(SCENES); do echo "== $$s =="; cat build/demo/$$s.meta.json; done

# 元数据开关关闭时，PGM 必须与开启时逐像素相同
bytecheck: build/cli
	@mkdir -p build/bc_on build/bc_off
	@rm -f build/bc_on/* build/bc_off/*
	@for s in $(SCENES); do \
	  ./build/cli --no-meta --seq-file build/bc_off/render.seq \
	    scenes/$$s.txt build/bc_off/$$s.pgm 64 64 1.0 >/dev/null; \
	  ./build/cli --seq-file build/bc_on/render.seq \
	    scenes/$$s.txt build/bc_on/$$s.pgm 64 64 1.0 >/dev/null; \
	  ../tools/diff.sh build/bc_off/$$s.pgm build/bc_on/$$s.pgm || exit 1; \
	done
	@test ! -e build/bc_off/s1_convex.meta.json
	@echo "bytecheck ok: --no-meta produces no metadata and identical PGM"

clean:
	rm -rf build *.pgm

.PHONY: all test demo bytecheck clean
