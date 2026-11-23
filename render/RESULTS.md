# 环境无关性：四环境逐像素验证

同一条命令、同一份二进制、同一份场景，在路径 / 时区 / 语言 / 掩码四组
刻意拉开的运行环境下各跑两次。八张 PGM 必须两两逐像素一致，并与基准图
一致；文件权限、字节数、SHA-256 也必须一致。

```sh
./render/build/cli <scene> <out.pgm> 160 120 1.25
```

## 四环境结果

| 环境 | 两个变体 | 差异像素 | 文件 | 字节 | 权限 | 耗时 |
|---|---|---|---|---|---|---|
| a-path | `work/场景 目录/sub dir`（空格+中文+两层）vs `plain/` | 0 | `runs/a-path/run{1,2}.pgm` | 19215 | 644/644 | 17–28 ms |
| b-time | `TZ=Pacific/Kiritimati`+时刻 1700000000 vs `TZ=Pacific/Pago_Pago`+时刻 2000000000 | 0 | `runs/b-time/run{1,2}.pgm` | 19215 | 644/644 | 12–28 ms |
| c-locale | `de_DE.UTF-8`（小数点 `,`）vs `C.UTF-8`，两者都先 `setlocale(LC_ALL,"")` | 0 | `runs/c-locale/run{1,2}.pgm` | 19215 | 644/644 | 13–34 ms |
| d-umask | `umask 077` vs `umask 027` | 0 | `runs/d-umask/run{1,2}.pgm` | 19215 | 644/644 | 12–21 ms |

八张图 SHA-256 全为

```
4b03d038903257cde04288b902c83c1fc28837927d309a9029fa17a0202b663b
```

基准图 `render/baseline/scene.pgm` 同一哈希（由改造前 HEAD 的二进制生成，
与改造后输出逐字节相同，回归成立）。八份 `run*.log` 也逐字节相同。
机器可读结果：`artifacts/determinism/report.tsv`、`files.tsv`、`verify.log`。

环境确实被拉开（`renderenv.sh` 每次运行都自证，不只是设了变量）：

- **路径**：其中一次在 `work/场景 目录/sub dir` 里用相对路径渲染。
- **时区**：同一瞬间 UTC 22 时 / UTC+14 12 时；第二个变体时钟停在 2033-05-17。
  `LD_PRELOAD` 的 `faketick.so` 钉住 `time/gettimeofday/clock_gettime/localtime…`，
  脚本先跑 `tickprobe` 确认 shim 生效，否则直接退出 2。
- **语言**：`locale -k LC_NUMERIC` 的小数点是 `,`。注意 C 程序默认就在 `"C"`
  locale，仅设置 `LANG` 不会改变行为；所以用 `adoptlocale.so` 在 `main` 之前执行
  `setlocale(LC_ALL,"")`，模拟“宿主已采用环境 locale”的真实集成方式，并先跑
  `localeprobe` 确认进程真的变成了逗号小数点。
- **掩码**：`umask 077`/`027` 下新建文件是 `600`/`640`，导出图仍是 `644`。

## 改造前后对照

改造前 HEAD 的二进制放进同一矩阵（`CLI=<旧二进制> OUT=<另一目录> ./render/renderenv.sh`，
结果留档在 `artifacts/determinism/before/`）：

| 环境 | 旧二进制 |
|---|---|
| a-path | 0 差异 |
| b-time | 0 差异 |
| c-locale | **变体 1 `rc=3`、无产物**（`de_DE` 下 `"10.5"` 只解析出 `10`） |
| d-umask | 0 差异，但权限 **600/640** |

各模块 demo 在“宿主采用环境 locale”后的输出差异（旧代码 → `80,00` 之类）：
arclen、bbox、bezier、clip、hull、stroke、xform、svg 八处不同，dash 无浮点输出故相同。
改造后九处全部与 `LC_ALL=C` 输出逐字节一致。

## 修复清单

| 位置 | 问题 | 处理 |
|---|---|---|
| `render/cli.c` | `fscanf("%lf")`/`atof` 受 `LC_NUMERIC` 影响；`atoi`/`atof` 静默截断（`1,25`→`1`、`160x`→`160`） | 入口 `setlocale(LC_ALL,"C")`；整串严格解析，坏输入报错 |
| `render/cli.c` | `strtod` 还接受 `inf`/`nan`/十六进制浮点，这类值进入坐标会引入实现相关行为 | 场景与尺度只接受 ASCII 十进制（`[-+]d[.d][e±d]`），其余拒绝 |
| `render/cli.c` | `isspace` 分类来自 `LC_CTYPE` | 显式 ASCII 空白判断 |
| `render/src/export.c` | `fopen` 继承进程 `umask`，图相同而权限随环境变 | 导出后 `chmod 0644`；写/关/改权限失败均返回错误 |
| `svg/src/svgpath.c` | `strtod` 与 `isspace` 依赖语言环境；旧实现在 `de_DE` 下解析失败（`rc=-4`） | 库内锁 `LC_NUMERIC`；空白判断改 ASCII |
| `gold/ref.c`、各 `demo.c` | `%.2f`/`%.6f` 输出在 `de_DE` 下变逗号，参照产物随之不同 | 入口锁 `setlocale(LC_ALL,"C")` |
| 各模块 `Makefile` | `$(wildcard)` 顺序随目录读序变，链接顺序与二进制随之变 | `$(sort …)`；`export LC_ALL=C` |

## 关于“绝对路径”和“语言排序”

两者都按“代码里出现即算没做完”处理，当前仓库结论：

- **绝对路径**：`scanenv.sh` 扫描所有 `*.c/*.h/Makefile/*.sh` 双引号字面量中的
  `/home`、`/tmp`、`/mnt`、`/Users`、盘符，**0 处**。改造前的源码树同样 0 处。
  验证脚本自身不含任何绝对路径：全部由 `$0` 推出根目录。唯一的绝对路径出现在
  `artifacts/determinism/*/report*.tsv` 这类“运行记录”里，那是产物不是代码。
  租户路径含空格时暴露的两个脚本缺陷（`cc` 参数未加引号、`LD_PRELOAD` 含空格
  时 shim 静默失效）已修复，并在 `render/renderenv.sh` 里对 shim 生效做了硬校验。
- **语言排序**：仓库里没有任何 `strcoll`/`strxfrm`/`nl_langinfo`/`setlocale(LC_*, "")`
  出现在产品代码里（`scanenv.sh` 报 0 违规）。`hull/src/hull.c` 用了 `qsort`，
  但比较器是 x/y 数值全序，不经过 locale。我一度按“相等键顺序不确定”改过它，
  随后用 8 个含重复点的 4096 组排列验证，改造前后结果完全一致（比较器只在坐标
  完全相同时返回 0，这类点互换不影响凸包），因此**撤回了该改动**——没测出差异
  就不该把它算作修复。

## 复现

```sh
make -C render build/cli build/pgmdiff
./render/mkenv.sh       # 离线 localedef、编译 faketick.so/adoptlocale.so/探针
./render/renderenv.sh   # 四环境 × 2、自证、28 对交叉比对、文件清单
./render/scanenv.sh     # 静态审查
```

`make -C render verify` 依次执行后三步，全过退出码 0。把整棵树拷到含空格和
中文的路径、用 `env -i` 去掉宿主环境后再跑 `make -C render verify`，结果完全相同。

## 验证脚本自身的可靠性

- 扫描器覆盖已跟踪与未跟踪文件（新文件不能因为没 `git add` 就漏审），
  并在改造前的源码树上跑过，确认会报 14 处违规、退出码 1。
- 负向对照：拿改造前二进制跑，`c-locale` 变体 1 必须失败、`d-umask` 必须报
  权限不符；`pgmdiff` 用翻转一个像素的副本验证会报 `diff 1/19200 first(x,y)`。

## 仍存在的、与本任务无关的限制

- 数值计算是 `double` + libm；换编译器/架构（x87、FMA 收缩、不同 `sqrt`）
  仍可能有末位差异。本次只验证同一台机器、同一编译器。
- 场景文件要求小数点必须是 `.`，这是明确契约（不合法即报错），不随环境变化。
