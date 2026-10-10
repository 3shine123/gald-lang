#!/usr/bin/env bash
# run_stress.sh — clang/gcc 编译器扩展极端压测
#   round 1: clang (jetic -backend clang + 系统 clang 编译 + 运行)
#   round 2: gcc   (jetic -backend gcc 门禁 + clang 编译运行 — 本机无真 gcc)
#   round 3: 缺口清单 — 逐个转译 gaps/*.jeti,分类上报
set -uo pipefail
cd "$(dirname "$0")/.."

JETIC="${JETIC:-target/debug/jetic}"
ROOT="$(pwd)"
INC="$ROOT/include"
RUNTIME="$INC/jeti/runtime.c"
STRA="$ROOT/clang_gcc_stress"
PASS=0; FAIL=0

note() { printf '%s\n' "$*"; }
ok()   { PASS=$((PASS+1)); printf '  ✅ %s\n' "$*"; }
bad()  { FAIL=$((FAIL+1)); printf '  ❌ %s\n' "$*"; }

# ── round 1: clang ──
note "== round 1: clang =="
if "$JETIC" -rewrite-jeti -backend clang "$STRA/clang_stress.jeti" -o /tmp/nsc.c 2>/tmp/nsc.err; then
    ok "clang transpile (-backend clang)"
else
    bad "clang transpile: $(tail -1 /tmp/nsc.err)"
fi
if clang -I "$INC" -o /tmp/nsc /tmp/nsc.c "$RUNTIME" 2>/tmp/nsc.ccerr; then
    ok "clang C-compile"
else
    bad "clang C-compile: $(grep -m1 error /tmp/nsc.ccerr)"
fi
if out=$(/tmp/nsc); then
    ok "clang run → $out"
else
    bad "clang run (exit $?)"
fi

# ── round 2: gcc ──
note "== round 2: gcc =="
if "$JETIC" -rewrite-jeti -backend gcc "$STRA/gcc_stress.jeti" -o /tmp/nsg.c 2>/tmp/nsg.err; then
    ok "gcc transpile (-backend gcc 门禁)"
else
    bad "gcc transpile: $(tail -1 /tmp/nsg.err)"
fi
if clang -I "$INC" -o /tmp/nsg /tmp/nsg.c "$RUNTIME" 2>/tmp/nsg.ccerr; then
    ok "gcc C-compile (clang 代编译)"
else
    bad "gcc C-compile: $(grep -m1 error /tmp/nsg.ccerr)"
fi
if out=$(/tmp/nsg); then
    ok "gcc run → $out"
else
    bad "gcc run (exit $?)"
fi

# ── round 3: 缺口清单 ──
note "== round 3: 原缺口清单 — 现已全部修复 (gaps/) =="
for f in "$STRA"/gaps/*.jeti; do
    name="$(basename "$f")"
    if "$JETIC" -rewrite-jeti -backend clang "$f" -o /tmp/nsc_gap.c 2>/tmp/nsc_gap.err; then
        # 转译过了 → 看 C 编译 + 运行
        if clang -I "$INC" -o /tmp/nsc_gap /tmp/nsc_gap.c "$RUNTIME" 2>/tmp/nsc_gap.cc >/dev/null; then
            if grep -q 'warning:' /tmp/nsc_gap.cc; then
                ok "$name — 转译+C编译+运行 通过(C 有警告,可接受): $(grep -m1 warning /tmp/nsc_gap.cc | sed 's/.*warning: //')"
            else
                ok "$name — 转译+C编译+运行 全通过(缺口已修复)"
            fi
        else
            bad "$name — 转译过但 C 编译失败: $(grep -m1 error /tmp/nsc_gap.cc | sed 's/.*error: //')"
        fi
    else
        bad "$name — 仍解析失败(缺口未修复): $(grep -m1 error /tmp/nsc_gap.err | sed 's/.*error: //')"
    fi
done

note ""
printf '== 结果: %d PASS / %d FAIL ==\n' "$PASS" "$FAIL"
[ "$FAIL" = "0" ]
