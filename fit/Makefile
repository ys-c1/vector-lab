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

# 新逻辑（线性方程组）演示，确定性产物写到 build/demo-linear.txt
demo: build/demo
	./build/demo build/demo-linear.txt

build/demo: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) demo.c -lm -o $@

# 关闭新逻辑、回退到旧的逐个试：同一 demo 源码用 -DFIT_NO_LINEAR 编译
demo-trial: build/demo-trial
	./build/demo-trial build/demo-trial.txt

build/demo-trial: $(SRC) demo.c $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFIT_NO_LINEAR $(SRC) demo.c -lm -o $@

# 两种模式比对：状态/冲突逐字节一致，良态数值按容差一致
bytecheck: demo demo-trial
	../tools/fitcheck.sh build/demo-linear.txt build/demo-trial.txt 1e-6

# 回归保证：fit 是新增独立模块，不改动 render 任何源码（git 工作区中 render/ 无改动），
# 且 render 测试与 CLI 产物照常可复现——新逻辑开关只在 fit/ 内，关掉它图逐字节不变。
render-regress:
	@git -C .. diff --quiet -- render/ && echo "render/ 源码零改动" || \
	  (echo "render/ 被改动，回归失败"; exit 1)
	$(MAKE) -C ../render test >/dev/null
	$(MAKE) -C ../render build/cli >/dev/null
	@printf 'p 0 0 10 0 10 10 0 10\n' > build/scene.txt
	../render/build/cli build/scene.txt build/a.pgm 32 32 1.0 >/dev/null
	../render/build/cli build/scene.txt build/b.pgm 32 32 1.0 >/dev/null
	../tools/diff.sh build/a.pgm build/b.pgm

clean:
	rm -rf build

.PHONY: all test demo demo-trial bytecheck render-regress clean
