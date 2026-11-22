CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?= -Isrc
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
SCENES := single disjoint nested self
EXTRA := nested_same
REJECTS := reject_empty reject_open reject_empty_second reject_open_second
W := 256
H := 256
SS := 8
SAMPLES := 512

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

build/report: $(SRC) report.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) report.c -lm -o $@

# 四组场景 + 一组同向嵌套对照：渲染主模块 PGM、渲染参照 PGM、逐像素比对，
# 并把这些统计写入 build/report.txt；拒收样例单独跑一遍。
report: build/report
	@$(MAKE) -s -C ../gold
	@mkdir -p build/out
	@: > build/report.txt
	@for s in $(SCENES) $(EXTRA); do \
	  ../gold/build/ref fill scenes/$$s.d $(W) $(H) $(SS) auto build/out/$$s.ref.pgm || exit 1; \
	  ./build/report scenes/$$s.d build/out/$$s.pgm $(W) $(H) $(SS) $(SAMPLES) build/out/$$s.ref.pgm >> build/report.txt || exit 1; \
	done
	@for s in $(REJECTS); do \
	  ./build/report scenes/$$s.d build/out/$$s.pgm >> build/report.txt || true; \
	done
	@echo "--- report ---"
	@cat build/report.txt

# 逐字节比对主模块与参照的 PGM（每组场景都要求一致）
diff: report
	@fail=0; for s in $(SCENES) $(EXTRA); do \
	  if cmp -s build/out/$$s.pgm build/out/$$s.ref.pgm; then \
	    echo "IDENTICAL $$s"; \
	  else \
	    echo "DIFF $$s"; fail=1; \
	  fi; \
	done; exit $$fail

clean:
	rm -rf build *.pgm

.PHONY: all test demo report diff clean
