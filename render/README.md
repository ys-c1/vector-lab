# render

场景光栅化管线与场景快照。

## 命令

```
# 原流程（不变）：scene.txt out.pgm w h scale，窗口取场景包围盒
./build/cli scene.txt out.pgm w h scale

# 快照：场景 + 变换 + 窗口 + 宽高写成一个文件
./build/cli snap scene.txt snap.bin w h scale [tx ty rot minx miny maxx maxy]

# 恢复：参数全部来自快照，直接出图，无需再记裁剪参数
./build/cli restore snap.bin out.pgm
```

## 快照格式（小端二进制，版本 1）

| 偏移 | 长度 | 字段 |
|---|---|---|
| 0 | 4 | 魔数 `RSNP` |
| 4 | 4 | 版本号 u32（当前 1，不符即拒） |
| 8 | 4 | 宽 i32 |
| 12 | 4 | 高 i32 |
| 16 | 32 | 变换：tx ty rot scale，4×f64 |
| 48 | 32 | 窗口：minx miny maxx maxy，4×f64 |
| 80 | 4 | 多边形数 npolys i32 |
| 84 | … | 每个多边形：n i32 + n×(x,y f64) |

拒绝条件均带文件字节偏移：魔数错（偏移 0）、版本不符（偏移 4）、
字段截断（起始偏移）、尾随字节（合法内容结束偏移）、NaN/零缩放/退化窗口。

## 验收

- `make test`：495 项检查，含往返、坏魔数、坏版本、逐长度截断、尾随字节
- `make bench`：三组场景报快照字节数 / 还原差异像素 / 往返毫秒，差异非零即失败
- `./verify.sh`：端到端验收，并与 git HEAD 基线 CLI 做 PGM 逐字节比对

## 功能关闭

`make build/cli-snapoff`（编译加 `-DRSNAP_DISABLE`）：快照代码退化为桩，
snap/restore 返回 `SNAP_EDISABLED`；原渲染流程产物与基线**逐字节相同**。
