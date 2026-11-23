#!/usr/bin/env python3
"""逐条校验 manifest.json：依赖声明完整性、构建可复现性、渲染零差异。

退出码：
  0 全部通过
  1 存在失败项（清单与实际不符 / 未声明依赖 / 差异像素非零 / 构建失败）
用法：
  python3 tools/verify_manifest.py [--root REPO] [--json]
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

# 仓库里实际出现的系统头 -> 归属 apt 包（与 manifest toolchain 对齐）
HEADER_OWNER = {
    "math.h": "libc6-dev",
    "stdio.h": "libc6-dev",
    "stdlib.h": "libc6-dev",
    "string.h": "libc6-dev",
    "ctype.h": "libc6-dev",
}
# 实际用到的链接库 -> 归属包
LIB_OWNER = {"m": "libc6-dev"}


def run(cmd, cwd=None):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, shell=isinstance(cmd, str))
    return p.returncode, p.stdout + p.stderr


def scan_includes(path):
    """返回 (系统头集合, 本地头集合)。"""
    sys_h, loc_h = set(), set()
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r'\s*#\s*include\s*<([^>]+)>', line)
            if m:
                sys_h.add(os.path.basename(m.group(1)))
                continue
            m = re.match(r'\s*#\s*include\s*"([^"]+)"', line)
            if m:
                loc_h.add(os.path.basename(m.group(1)))
    return sys_h, loc_h


def main():
    args = sys.argv[1:]
    root = os.getcwd()
    as_json = False
    i = 0
    while i < len(args):
        if args[i] == "--root":
            root = os.path.abspath(args[i + 1]); i += 2
        elif args[i] == "--json":
            as_json = True; i += 1
        else:
            i += 1

    report = {"root": root, "checks": [], "module_reports": [], "ok": True}

    def check(name, passed, detail=""):
        report["checks"].append({"check": name, "pass": bool(passed), "detail": detail})
        if not passed:
            report["ok"] = False

    mf_path = os.path.join(root, "manifest.json")
    check("manifest.json exists", os.path.isfile(mf_path))
    if not os.path.isfile(mf_path):
        print(json.dumps(report, indent=2)); return 1
    with open(mf_path) as f:
        mf = json.load(f)

    declared_mods = {m["module"]: m for m in mf["modules"]}
    actual_dirs = sorted(
        d for d in os.listdir(root)
        if os.path.isfile(os.path.join(root, d, "Makefile"))
    )
    check("every Makefile module is declared",
          sorted(declared_mods) == actual_dirs,
          f"declared={sorted(declared_mods)} actual={actual_dirs}")

    # 工具链二进制
    for tool in ("gcc", "make", "cmp", "awk", "bash", "mkdir", "rm"):
        check(f"tool available: {tool}", shutil.which(tool) is not None,
              "missing" if not shutil.which(tool) else shutil.which(tool))

    total_steps = 0
    for mod in mf["modules"]:
        mdir = os.path.join(root, mod["path"])
        r = {"module": mod["module"], "group": mod["group"], "step_count": mod["step_count"]}
        total_steps += mod["step_count"]

        # 1) 依赖声明完整性：扫描真实源文件，核对系统头/本地头/库是否都已声明
        units = mod.get("compile_units", [])
        real_sys, real_loc = set(), set()
        for u in units:
            p = os.path.join(mdir, u)
            if not os.path.isfile(p):
                check(f"{mod['module']}: compile unit exists {u}", False, p); continue
            s, l = scan_includes(p)
            real_sys |= s; real_loc |= l
        undeclared_h = sorted(h for h in real_sys if h not in mod["headers_system"])
        check(f"{mod['module']}: no undeclared system header", not undeclared_h,
              f"undeclared={undeclared_h}")
        undeclared_loc = sorted(h for h in real_loc if h not in mod["headers_local"])
        check(f"{mod['module']}: no undeclared local header", not undeclared_loc,
              f"undeclared={undeclared_loc}")
        # 每个系统头的归属包必须在 toolchain 声明
        owners = {HEADER_OWNER.get(h) for h in real_sys}
        apt_pkgs = {p["apt"] for p in mf["toolchain"]["packages"]}
        bad_owner = sorted(o for o in owners if o and o not in apt_pkgs)
        check(f"{mod['module']}: header owner package declared", not bad_owner, str(bad_owner))
        # 库归属
        for lib in mod["libs"]:
            check(f"{mod['module']}: lib {lib} owner declared",
                  LIB_OWNER.get(lib) in apt_pkgs, lib)

        # 2) 干净构建（先 clean，模拟干净机器）
        rc, _ = run(["make", "-C", mdir, "clean"])
        rc, out = run(mod["build_test"].split(), cwd=root)
        r["build_test_rc"] = rc
        if mod.get("build_only"):
            # 纯参照实现（如 gold）：无 test 目标，只验证可构建
            check(f"{mod['module']}: clean build rc=0", rc == 0, out[-200:])
        else:
            check(f"{mod['module']}: clean build+test rc=0", rc == 0, out.strip().splitlines()[-1:] or [""])
            if rc == 0:
                mch = re.search(r'(\d+) checks?, (\d+) failures?', out)
                r["test_checks"], r["test_failures"] = (
                    (int(mch.group(1)), int(mch.group(2))) if mch else (None, None))
                check(f"{mod['module']}: 0 test failures",
                      mch is not None and mch.group(2) == "0",
                      mch.group(0) if mch else "no test summary")

        # 3) demo/cli 产物可生成（manifest 的 build_demo 是相对 root 的完整命令）
        if mod["build_demo"]:
            rc2, out2 = run(mod["build_demo"].split(), cwd=root)
            r["build_demo_rc"] = rc2
            check(f"{mod['module']}: demo/cli build rc=0", rc2 == 0, out2[-200:])
        for art in mod["artifacts"]:
            ap = os.path.join(mdir, art)
            check(f"{mod['module']}: artifact present {art}", os.path.exists(ap), ap)

        # 4) 缺 -lm 行为核对（仅对 manifest 标记会报错的模块强制验证）
        r["missing_libm_error_declared"] = mod.get("missing_libm_error")
        report["module_reports"].append(r)

    # 5) -lm 必要性实测：临时移除 -lm 编译，核对 manifest 标注
    for mod in mf["modules"]:
        mdir = os.path.join(root, mod["path"])
        units = [os.path.join(mdir, u) for u in mod.get("compile_units", []) if u.endswith(".c")]
        if not units:
            continue
        obj_dir = tempfile.mkdtemp(prefix="nolibm_")
        objs = []
        fail_compile = False
        for u in units:
            o = os.path.join(obj_dir, os.path.basename(u) + ".o")
            rc, out = run(["gcc", "-I" + os.path.join(mdir, "src"), "-O2", "-std=c11", "-c", u, "-o", o])
            if rc != 0:
                fail_compile = True; break
            objs.append(o)
        needs_lm = False
        if not fail_compile:
            rc, out = run(["gcc"] + objs + ["-o", os.path.join(obj_dir, "x")])
            needs_lm = rc != 0 and "undefined reference" in out
        shutil.rmtree(obj_dir, ignore_errors=True)
        declared_need = bool(mod.get("missing_libm_error"))
        check(f"{mod['module']}: -lm necessity matches manifest",
              needs_lm == declared_need,
              f"actual_needs_lm={needs_lm} declared={declared_need}")

    # 6) 渲染零差异端到端（先确保 gold/ref 与 render/cli 已构建）
    rv = mf["render_verification"]
    rc, out = run(["make", "-C", os.path.join(root, "gold")])
    check("build gold/ref rc=0", rc == 0, out)
    rc, out = run(["make", "-C", os.path.join(root, "render"), "build/cli"])
    check("build render/cli rc=0", rc == 0, out)
    with tempfile.TemporaryDirectory() as td:
        out_pgm = os.path.join(td, "out.pgm")
        gold_pgm = os.path.join(td, "gold.pgm")
        scene = os.path.join(root, rv["scene_input"])
        poly = os.path.join(root, rv["gold_input"])
        base = os.path.join(root, rv["baseline_image"])
        check("baseline image exists", os.path.isfile(base))
        cli = os.path.join(root, "render", "build", "cli")
        ref = os.path.join(root, "gold", "build", "ref")
        rc, out = run([cli, scene, out_pgm, str(rv["width"]), str(rv["height"]), str(rv["scale"])])
        check("render cli rc=0", rc == 0, out)
        with open(gold_pgm, "wb") as f:
            p = subprocess.run([ref, "pixels", poly, str(rv["width"]), str(rv["height"])],
                               stdout=f, stderr=subprocess.PIPE)
            rc_g = p.returncode
        check("gold ref rc=0", rc_g == 0, p.stderr.decode(errors="replace"))
        # 与受版本控制基线逐字节比对
        rc_d, out_d = run(["bash", os.path.join(root, "tools", "diff.sh"), out_pgm, base])
        check("diff.sh says IDENTICAL vs baseline", rc_d == 0, out_d.strip())
        # 独立像素统计（不依赖 diff.sh 结论）
        try:
            a = open(out_pgm, "rb").read()
            b = open(base, "rb").read()
            ia = a.index(b"255\n") + 4
            ib = b.index(b"255\n") + 4
            pa, pb = a[ia:], b[ib:]
            differing = sum(1 for x, y in zip(pa, pb) if x != y)
            total_px = rv["width"] * rv["height"]
            report["render"] = {"differing_pixels": differing, "total_pixels": total_px,
                                "byte_identical": a == b}
            check("render differing_pixels == 0", differing == 0,
                  f"{differing}/{total_px}")
            check("render byte-identical to baseline", a == b)
            # gold 现算结果也必须与基线一致（基线本身可信性）
            g = open(gold_pgm, "rb").read()
            check("regenerated gold == checked-in baseline", g == b)
        except FileNotFoundError as e:
            check("pixel comparison inputs present", False, str(e))

    report["total_shortest_steps"] = total_steps
    if as_json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    else:
        npass = sum(1 for c in report["checks"] if c["pass"])
        nfail = sum(1 for c in report["checks"] if not c["pass"])
        print(f"== manifest verification: {npass} passed, {nfail} failed ==")
        for c in report["checks"]:
            mark = "PASS" if c["pass"] else "FAIL"
            line = f"[{mark}] {c['check']}"
            if not c["pass"] and c["detail"]:
                line += f"  -> {c['detail']}"
            print(line)
        print("\n== per-module ==")
        for r in report["module_reports"]:
            print(f"  {r['module']:8s} group{r['group']} steps={r['step_count']} "
                  f"test_rc={r.get('build_test_rc')} checks={r.get('test_checks')} "
                  f"failures={r.get('test_failures')} needs-lm-declared={r.get('missing_libm_error_declared') is not None}")
        if "render" in report:
            print(f"\nrender: differing_pixels={report['render']['differing_pixels']} "
                  f"byte_identical={report['render']['byte_identical']}")
        print(f"\nRESULT: {'OK' if report['ok'] else 'FAILED'}")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
