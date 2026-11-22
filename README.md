# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## PGM 渲染与对拍

svg 模块把路径渲染成二进制 PGM（P5），每个像素用 8x8 子点按奇偶环绕算覆盖率，
坐标按路径包围盒映射，与 `gold/ref.c pixels` 的算法逐表达式对齐：

```
make -C svg build/demo && make -C gold build/ref && make -C tools
tools/render-check.sh          # 端到端：同尺寸两张 PGM、叠图、指标、非法尺寸
svg/build/demo render "<d>" <w> <h> out.pgm
svg/build/demo poly   "<d>" out.txt          # 导出首子路径多边形供 ref 消费
tools/build/pgmdiff a.pgm b.pgm overlay.pgm   # 差异像素数 + |a-b| 叠图
```

每次渲染记录一笔 `render <file> bytes <字节数> pixels <像素数> ms <毫秒>`，
汇总见 `svg/build/render/metrics.log`。宽或高为零/负立即退出（码 2）并指名
`param: width/height`。光栅化只分配 1 倍图像大小的缓冲（上限 3 倍），
`pgmdiff` 固定 8KB 分块流式比对，内存与图像尺寸无关。

