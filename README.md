# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## render：场景渲染与导出元数据

`scene → pipeline → raster → export` 把多边形场景渲染成 PGM，每次导出
强制附带一份元数据文件（`<图名>.meta.json`），字段顺序与格式固定：

```json
{
  "width": 64,
  "height": 64,
  "vertices": 4,
  "sequence": 1,
  "elapsed_ms": 1
}
```

- 画幅 `width/height`、顶点总数 `vertices`、生成序号 `sequence`、
  耗时 `elapsed_ms` 均为整数；写入前与读回时都做字段完整性和类型校验，
  缺字段、字段顺序错、浮点/字符串等非整数一律拒绝（`RMETA_E_*` 错误码）。
- 序号持久化在序号文件（默认 `render.seq`，两行：当前号 + 历史高水位）。
  每次取号必须为上一号 +1，严格递增；回退、跳号、文件损坏、到达上限
  （2^53，杜绝回绕）一律拒绝，且文件不被改写。
- `--no-meta` 关闭元数据：不产生任何元数据/序号文件，PGM 与开启时
  逐字节相同（`make -C render bytecheck` 用 `tools/diff.sh` 验证）。
- `tools/metacheck.sh` 用独立来源（PGM 头、场景文件、序号文件）逐项
  核对元数据，报告每个字段与实际值的偏差；`make -C render demo`
  对四类场景（凸/凹/多多边形/复合）各出一图并自动核对，零偏差才通过。

