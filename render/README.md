# render

场景、光栅化、渲染管线与图层依赖排序。

## 图层依赖 DAG（新逻辑）

图层之间的遮挡关系建模为有向图：`edge A B` 表示 A 在 B 之下、必须先渲染。
`src/dag.*` 负责图算法，`src/lscene.*` 负责场景解析与 painter 覆盖渲染。

- 环检测：DFS 三色标记，有环即拒绝并输出环上节点（首尾同点闭合）
- 渲染顺序：先算每个节点的最早层级 `ee`（最长前驱链长度），Kahn 每步在
  就绪节点中取 `(ee, 下标)` 最小者；并列结果确定性可复现
- 关键路径：节点权为 1 的最长路，决定渲染深度的下界
- 孤立图层：入度、出度均为 0，单独列出，在排序中按规则尽早渲染

### 场景文件格式

```
win  minx miny maxx maxy                 # 可选，缺省取所有图层包围盒
layer <名称> <灰度0-255> x1 y1 x2 y2 ... # 至少 3 个顶点
edge  <下层名> <上层名>                   # 下层先渲染，上层遮挡下层
order <名称> ...                          # 手排顺序（旧逻辑），必须列全且无重复
```

### 命令

```
make -C render layers          # 四组夹具的完整报告（关键路径/顺序/差异像素/耗时）
make -C render dagtest         # DAG 单元测试
make -C render test            # 全部单元测试

layercli auto   <scene> <out.pgm> <w> <h>   # 新逻辑：DAG 定序渲染，有环退出码 4
layercli manual <scene> <out.pgm> <w> <h>   # 旧逻辑：按 order 手排渲染
layercli diff   <a.pgm> <b.pgm>             # 逐像素差异计数
layercli dot    <scene>                     # Graphviz 有向图，关键路径标红、孤立节点虚线
```

`gold/ref layers` 是独立参照实现（多趟松弛求层级、位掩码就绪集定序），
与主代码的 Kahn/单趟松弛是两套算法，渲染结果必须逐字节相同，由
`run_layers.sh` 与 `tools/diff.sh` 双重校验。
