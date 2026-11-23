# render

场景光栅化与（字节序无关的）场景序列化。

## 构建

    make            # 构建并运行单元测试 + 序列化往返验证
    make build/cli  # 命令行：cli scene.txt out.pgm w h scale

## 线格式 v1（显式按字节，字节序无关）

| 偏移 | 长度 | 字段 | 编码 |
|---|---|---|---|
| 0 | 4 | magic | `RSLE`（载荷小端）/ `RSBE`（载荷大端） |
| 4 | 2 | version | uint16，当前 = 1 |
| 8 | 4 | npolys | uint32，0..32 |
| 每多边形 | 4 | `poly[k].n` | uint32，3..64 |
| 每顶点 | 8+8 | `pts[i].x`, `pts[i].y` | IEEE-754 double，位模式逐字节搬运 |

写出恒为小端（RSLE）；读入同时接受 RSLE/RSBE。所有多字节字段均显式
按字节拼拆（移位 + 掩码），不依赖宿主机字节序、结构体填充或 double 的
内存布局，因此可跨架构往返。

字段数 = 3（magic/version/npolys）+ Σ(1 + 2·n_k)，由
`serio_field_count()` 给出。

## 拒读规则（`scene_load_ex` 返回错误码并在 `SerioErr.field` 报字段名）

| 情形 | 错误码 | field 示例 |
|---|---|---|
| magic 不对 | `SERIO_E_MAGIC` | `magic` |
| 版本不对 | `SERIO_E_VERSION` | `version` |
| 计数越界 / 尾部多余字节（长度不符） | `SERIO_E_LENGTH` | `poly[0].n`、`npolys`、`payload` |
| 文件提前结束（漏字段） | `SERIO_E_MISSING` | `poly[0].pts[2].x` |

## 模式开关

- 默认（或 `RENDER_SERIO=1`/任意非 raw 值，或 `serio_set_mode(1)`）：
  显式按字节序列化，跨机可移植。
- `RENDER_SERIO=raw` / `RENDER_SERIO=0`，或 `serio_set_mode(0)`：
  保留的老内存直拷路径（blob 恰为 `sizeof(RScene)`，同机往返）。
  两条路径的渲染输出逐字节相同，开关只改变落盘表示。

## 往返验收（roundtrip.c）

三个场景（单四边形 / 3 多边形 / 16×16 边形网格）各自：
存盘 → 读回 → 重渲染，与直接渲染逐像素比较，差异像素必须为 0；
报表打印字段数、差异像素与 50 次 save+load 的平均耗时。
另含 RSBE 大端载荷零差异、全部拒读用例、老直拷路径校验。
