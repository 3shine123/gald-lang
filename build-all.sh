#!/usr/bin/env bash
# build-all.sh — 一键交叉编译 ovicc 到 8 个目标平台
# 依赖：zig 0.14+（brew install zig），
#       rustup 管理的 Rust，
#       Windows 目标另需 cargo-zigbuild（cargo install cargo-zigbuild）
set -euo pipefail
cd "$(dirname "$0")"

# ── 参数解析：-jN 指定 cargo 并行度 ──
CARGO_JOBS=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        -j[0-9]*) CARGO_JOBS="-j ${1#-j}"; shift ;;
        -j) CARGO_JOBS="-j $2"; shift 2 ;;
        -h|--help) echo "usage: ./build-all.sh [-jN]"; exit 0 ;;
        *) echo "unknown option: $1 (try -jN)"; exit 1 ;;
    esac
done

source "$HOME/.cargo/env"
ZIG_CC="$(pwd)/zig-cc.sh"
ZIG_AR="$(pwd)/zig-ar.sh"

# ── 目标平台 ──
TARGETS=(
    "aarch64-apple-darwin"      # macOS Apple Silicon（宿主）
    "x86_64-apple-darwin"       # macOS Intel
    "x86_64-unknown-linux-musl" # Linux x86_64（静态链接）
    "aarch64-unknown-linux-musl" # Linux ARM64（静态链接）
    "x86_64-unknown-freebsd"    # FreeBSD x86_64
    "i686-unknown-freebsd"      # FreeBSD x86
    "x86_64-unknown-netbsd"     # NetBSD x86_64
    "x86_64-pc-windows-gnu"     # Windows x86_64 (MinGW, via cargo-zigbuild)
)

# ── 确保所有 target 已安装 ──
for t in "${TARGETS[@]}"; do
    rustup target add "$t" 2>&1 | grep -v "up to date" || true
done

# ── 配置 .cargo/config.toml ──
mkdir -p .cargo
cat > .cargo/config.toml <<'EOF'
[target.aarch64-apple-darwin]
linker = "clang"

[target.x86_64-apple-darwin]
linker = "clang"

[target.x86_64-unknown-linux-musl]
linker = "rust-lld"
rustflags = ["-C", "link-self-contained=yes"]

[target.aarch64-unknown-linux-musl]
linker = "rust-lld"
rustflags = ["-C", "link-self-contained=yes"]
EOF

# BSD 目标需要 zig cc 作为链接器，且每个目标有独立的 zig-cc wrapper
ZIG_CC_DIR="$(pwd)"
for bsd_target in x86_64-unknown-freebsd i686-unknown-freebsd x86_64-unknown-netbsd; do
    case "$bsd_target" in
        x86_64-unknown-freebsd) zig_linker="${ZIG_CC_DIR}/zig-cc-x86_64-freebsd.sh" ;;
        i686-unknown-freebsd)   zig_linker="${ZIG_CC_DIR}/zig-cc-i686-freebsd.sh" ;;
        x86_64-unknown-netbsd)  zig_linker="${ZIG_CC_DIR}/zig-cc-x86_64-netbsd.sh" ;;
    esac
    cat >> .cargo/config.toml <<EOF

[target.$bsd_target]
linker = "$zig_linker"
EOF
done

# ── 编译 ──
# 为 FreeBSD 目标创建 stub 共享库（Rust std 引用了一些 FreeBSD 特有库）
for _arch in x86_64 i386; do
    _dir="target/bsd-stubs"
    [ "$_arch" = "i386" ] && _dir="target/bsd-stubs-i386"
    mkdir -p "$_dir"
    for _lib in devstat procstat kvm memstat util rt execinfo; do
        _stub="$_dir/stub_$_lib.c"
        if [ ! -f "$_stub" ]; then
            echo "void ovic_stub_${_lib}(void) {}" > "$_stub"
            _ztarget="${_arch}-freebsd-none"
            [ "$_arch" = "i386" ] && _ztarget="x86-freebsd-none"
            zig cc -target "$_ztarget" -shared -fPIC -o "$_dir/lib$_lib.so" "$_stub" 2>/dev/null
        fi
    done
done

for t in "${TARGETS[@]}"; do
    echo "==> Building $t ..."
    # 非 Apple 目标需要用 zig cc 作为 C 编译器（编译 runtime.c）
    case "$t" in
        aarch64-apple-darwin|x86_64-apple-darwin)
            cargo build --release --target "$t" $CARGO_JOBS 2>&1 | tail -3
            ;;
        x86_64-unknown-freebsd|i686-unknown-freebsd)
            _stubdir="target/bsd-stubs"
            [ "$t" = "i686-unknown-freebsd" ] && _stubdir="target/bsd-stubs-i386"
            CC_ENV="CC_$(echo "$t" | tr '[:upper:]-' '[:lower:]_' | sed 's/\./_/g' | tr '-' '_')"
            export "$CC_ENV=$ZIG_CC"
            AR_ENV="AR_$(echo "$t" | tr '[:upper:]-' '[:lower:]_' | sed 's/\./_/g' | tr '-' '_')"
            export "$AR_ENV=$ZIG_AR"
            RUSTFLAGS="-C link-arg=-Wl,--allow-shlib-undefined -C link-arg=-L$(pwd)/$_stubdir" \
                cargo build --release --target "$t" $CARGO_JOBS 2>&1 | tail -3
            ;;
        x86_64-pc-windows-gnu)
            # Windows: 用 cargo-zigbuild（zig cc 作为 linker，自带 MinGW libc）。
            if ! command -v cargo-zigbuild >/dev/null 2>&1; then
                echo "  ⚠️  跳过 $t：未找到 cargo-zigbuild（请先 cargo install cargo-zigbuild）" >&2
                continue
            fi
            CC_ENV="CC_$(echo "$t" | tr '[:upper:]-' '[:lower:]_' | sed 's/\./_/g' | tr '-' '_')"
            export "$CC_ENV=$ZIG_CC"
            AR_ENV="AR_$(echo "$t" | tr '[:upper:]-' '[:lower:]_' | sed 's/\./_/g' | tr '-' '_')"
            export "$AR_ENV=$ZIG_AR"
            cargo zigbuild --release --target "$t" $CARGO_JOBS 2>&1 | tail -3
            ;;
        *)
            CC_ENV="CC_$(echo "$t" | tr '[:upper:]-' '[:lower:]_' | sed 's/\./_/g' | tr '-' '_')"
            export "$CC_ENV=$ZIG_CC"
            AR_ENV="AR_$(echo "$t" | tr '[:upper:]-' '[:lower:]_' | sed 's/\./_/g' | tr '-' '_')"
            export "$AR_ENV=$ZIG_AR"
            cargo build --release --target "$t" $CARGO_JOBS 2>&1 | tail -3
            ;;
    esac
    echo "==> $t done"
done

# ── 为每个平台生成自包含安装包（install.sh + 头文件） ──
for t in "${TARGETS[@]}"; do
    case "$t" in
        aarch64-apple-darwin) out="target/release" ;;   # 宿主目标
        *) out="target/$t/release" ;;
    esac
    # 全量头文件（libFire、ovic runtime 等）打包到 release 目录
    mkdir -p "$out/include"
    cp -r include/. "$out/include/"
    # 拷贝 install.sh
    cp install.sh "$out/install.sh"
    chmod +x "$out/install.sh"
    # 拷贝 Windows PowerShell 安装脚本
    cp install.ps1 "$out/install.ps1"
    # 拷贝 shell 补全脚本
    mkdir -p "$out/completions"
    cp -r completions/. "$out/completions/"
done

# ── 产物汇总 ──
echo ""
echo "========== Build artifacts =========="
for t in "${TARGETS[@]}"; do
    case "$t" in
        aarch64-apple-darwin)  bin="target/release/ovicc" ;;  # 宿主目标
        x86_64-pc-windows-gnu) bin="target/$t/release/ovicc.exe" ;;
        *) bin="target/$t/release/ovicc" ;;
    esac
    [ -f "$bin" ] && echo "$(ls -lh "$bin" | awk '{print $5}')  $bin" && file "$bin" | sed 's/.*: //' && echo ""
done

echo "========== Install bundles (binary + install.sh + headers) =========="
for t in "${TARGETS[@]}"; do
    case "$t" in
        aarch64-apple-darwin) out="target/release" ;;
        *) out="target/$t/release" ;;
    esac
    [ -f "$out/install.sh" ] && echo "  $out/  ($(du -sh "$out" | awk '{print $1}')) — 运行 ./install.sh 安装到 /opt/ovic"
done

# ── 打包成压缩包（放到 target/ 根目录） ──
pack_name() {
    case "$1" in
        aarch64-apple-darwin)       echo "ovic-aarch64-apple-darwin" ;;
        x86_64-apple-darwin)        echo "ovic-x86_64-apple-darwin" ;;
        x86_64-unknown-linux-musl)  echo "ovic-x86_64-unknown-linux-musl" ;;
        aarch64-unknown-linux-musl) echo "ovic-aarch64-unknown-linux-musl" ;;
        x86_64-unknown-freebsd)     echo "ovic-x86_64-unknown-freebsd" ;;
        i686-unknown-freebsd)       echo "ovic-i686-unknown-freebsd" ;;
        x86_64-unknown-netbsd)      echo "ovic-x86_64-unknown-netbsd" ;;
        x86_64-pc-windows-gnu)      echo "ovic-x86_64-pc-windows-gnu" ;;
        *) echo "ovic-$1" ;;
    esac
}

echo ""
echo "========== Packing into target/ =========="
for t in "${TARGETS[@]}"; do
    name="$(pack_name "$t")"
    case "$t" in
        aarch64-apple-darwin) out="target/release" ;;
        *) out="target/$t/release" ;;
    esac
    is_win=0
    case "$t" in x86_64-pc-windows-gnu) is_win=1 ;; esac
    staging="target/pack/$name"
    rm -rf "$staging"
    mkdir -p "$staging"
    # 只打包必要内容：二进制 + install.sh + 静态库 + 头文件 + 补全脚本
    if [ "$is_win" = 1 ]; then
        cp "$out/ovicc.exe" "$staging/ovicc.exe"
    else
        cp "$out/ovicc" "$staging/ovicc"
    fi
    cp "$out/install.sh" "$staging/"
    chmod +x "$staging/install.sh"
    cp "$out/install.ps1" "$staging/"
    [ -f "$out/libovic.a" ] && cp "$out/libovic.a" "$staging/"
    cp -r "$out/include" "$staging/include"
    [ -d "$out/completions" ] && cp -r "$out/completions" "$staging/completions"
    find "$staging" -name ".DS_Store" -delete

    if [ "$is_win" = 1 ]; then
        # Windows → .zip
        arch_path="target/${name}.zip"
        rm -f "$arch_path"
        ( cd target/pack && zip -qr "../${name}.zip" "$name" )
    else
        # Unix → tar.gz
        arch_path="target/${name}.tar.gz"
        tar -C target/pack -czf "$arch_path" "$name"
    fi
    echo "  $(ls -lh "$arch_path" | awk '{print $5}')  $arch_path"
    rm -rf "$staging"
done
rm -rf target/pack