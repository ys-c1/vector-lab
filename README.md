# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值、固定 8 段细分与按弦高误差上界的自适应细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## bezier：反解容差的自适应细分

固定段数在弯处看得见折线；新逻辑按弦高误差上界自适应分段：

- 上界：一段贝塞尔到其端点弦的距离 `<= (3/4)·max(|P0−2P1+P2|, |P1−2P2+P3|)`。
- 反解：要求误差 `<= E` 时最大允许容差 `T = E/(3/4) = 4E/3`，
  `bezier_tol_for_error()` 再向下取 1 ulp，保证浮点下 `(3/4)·T <= E`。
- 细分：`bezier_flatten_path()` 用 de Casteljau 二分，每段满足三条充分判据之一
  （控制点垂距、二阶差分上界、控制点到线段距离）即停；输出保留每段的参数
  区间与来源段号。
- 验证：`bezier_verify_path()` 逐段采样弦高误差（峰值附近黄金分割细化），
  任一段超过 `E` 即 `pass = 0` 并给出超界段号、段内折线序号与参数区间。
- 拒绝：负/零/非有限容差返回 `BEZ_E_TOL`（`bad_seg = -1`，全局参数）；
  控制点非有限或控制多边形收缩成一点返回 `BEZ_E_DEGEN` 并指名段号（0 基）。

```sh
make -C bezier test     # 327 项检查
make -C bezier bench    # 缓弯/急弯/S 形/近直线：反解容差、实际误差、段数、耗时
make -C bezier verify   # 独立 Python 复算 (3/4) 上界
make -C bezier check    # 关掉新逻辑时，点序列与 PGM 与 baseline/ 逐字节一致
```

`make -C bezier check` 的三重比对：旧固定 8 段接口的点序列、`gold/ref.c`
栅格化的 PGM、以及模式开关 `BEZ_MODE_LEGACY_FIXED` 的输出，都与 `baseline/`
逐字节相同；基线由未改动的旧代码生成并随仓库保存。

