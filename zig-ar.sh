#!/bin/bash
# zig-ar.sh — 用 zig 的 ar 替代宿主 ar（macOS /usr/bin/ar 无法打包非 Mach-O 对象）
# 用于交叉编译 musl 等目标时打包 libnopa.a（cc-rs 通过 AR_<triple> 环境变量调用）
set -euo pipefail

exec zig ar "$@"
