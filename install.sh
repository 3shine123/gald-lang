#!/usr/bin/env bash
# install.sh — 本平台安装脚本（由 build-all.sh 自动复制到每个平台的 target/<triple>/release/ 下）
# 用法：./install.sh [PREFIX] [SYSTEM_INC]
#   默认目录：Linux/Darwin → /opt/ovel ；系统头文件 → /usr/local/include
# 检测系统语言，中文环境显示中文，其他环境显示英语
set -euo pipefail

BUNDLE="$(cd "$(dirname "$0")" && pwd)"

# ── 语言检测（中文 → zh，其他 → en） ──
LANG_OK="$(printf '%s\n%s\n%s\n' "${LC_ALL:-}" "${LC_MESSAGES:-}" "${LANG:-}")"
if printf '%s' "$LANG_OK" | grep -qiE 'zh|cn|chinese'; then
    I18N="zh"
else
    I18N="en"
fi

# ── 二进制名 ──
BIN="ovelc"

# ── 默认安装前缀 ──
PREFIX="${1:-/opt/ovel}"
SYSTEM_INC="${2:-/usr/local/include}"

# ── 文案 ──
if [ "$I18N" = "zh" ]; then
    MSG_TITLE="Ovel 安装包"
    MSG_INSTALL_DIR="安装目录"
    MSG_LIB="静态库"
    MSG_INC="头文件"
    MSG_SYS_INC="系统头文件（Foundation / ovel runtime）"
    MSG_SYS_INC_SKIP="无写入权限，跳过系统头文件安装（可用 sudo 重试）"
    MSG_DONE="安装完成"
    MSG_PATH="如需在 PATH 使用"
    MSG_UNINSTALL="卸载"
    MSG_COMP="Shell 补全（zsh/bash/fish）"
    MSG_COMP_DIR="补全脚本目录"
    MSG_COMP_ZSH="zsh 用户请把以下加到 ~/.zshrc"
    MSG_COMP_BASH="bash 用户请把以下加到 ~/.bashrc"
    MSG_COMP_FISH="fish 用户请把以下加到 ~/.config/fish/config.fish"
    MSG_AUTOCOMP="自动安装 Shell 补全"
    MSG_COMP_SKIP="跳过补全安装"
else
    MSG_TITLE="Ovel Installer"
    MSG_INSTALL_DIR="Install directory"
    MSG_LIB="library"
    MSG_INC="headers"
    MSG_SYS_INC="System headers (Foundation / ovel runtime)"
    MSG_SYS_INC_SKIP="no write permission, skipped (retry with sudo)"
    MSG_DONE="Installation complete"
    MSG_PATH="To use from PATH, run"
    MSG_UNINSTALL="Uninstall"
    MSG_COMP="Shell completions (zsh/bash/fish)"
    MSG_COMP_DIR="completion scripts directory"
    MSG_COMP_ZSH="zsh: add the following to ~/.zshrc"
    MSG_COMP_BASH="bash: add the following to ~/.bashrc"
    MSG_COMP_FISH="fish: add the following to ~/.config/fish/config.fish"
    MSG_AUTOCOMP="Auto-install shell completions"
    MSG_COMP_SKIP="completion install skipped"
fi

echo "==> ${MSG_TITLE} (${BIN})"
echo "    ${MSG_INSTALL_DIR}: $PREFIX"
echo "    ${MSG_SYS_INC}: $SYSTEM_INC"
echo ""

install -d "$PREFIX/bin" "$PREFIX/lib" "$PREFIX/include"

# 二进制
install -m 755 "$BUNDLE/$BIN" "$PREFIX/bin/ovelc"

# 静态库
if [ -f "$BUNDLE/libovel.a" ]; then
    install -m 644 "$BUNDLE/libovel.a" "$PREFIX/lib/libovel.a"
    echo "    ${MSG_LIB}:  $PREFIX/lib/libovel.a"
fi

# 头文件（libVec<...>、runtime.h 等）
cp -r "$BUNDLE/include/." "$PREFIX/include/"
echo "    ${MSG_INC}:   $PREFIX/include/"

# 系统头文件：Foundation + ovel runtime → 系统 include 目录
if [ -d "$BUNDLE/include/Foundation" ] && [ -d "$BUNDLE/include/ovel" ]; then
    if mkdir -p "$SYSTEM_INC/Foundation" "$SYSTEM_INC/ovel" 2>/dev/null; then
        cp -r "$BUNDLE/include/Foundation/." "$SYSTEM_INC/Foundation/"
        cp -r "$BUNDLE/include/ovel/." "$SYSTEM_INC/ovel/"
        echo "    ${MSG_SYS_INC}: $SYSTEM_INC/{Foundation,ovel}/"
    else
        echo "    ${MSG_SYS_INC}: ${MSG_SYS_INC_SKIP}"
    fi
fi

echo ""
echo "========== ${MSG_DONE} =========="
echo "  binary:  $PREFIX/bin/ovelc"
echo "  headers: $PREFIX/include/"
echo "  system:  $SYSTEM_INC/{Foundation,ovel}/"

# ── Shell 补全：复制到 $PREFIX/share/ovelc/completions/ ──
if [ -d "$BUNDLE/completions" ]; then
    install -d "$PREFIX/share/ovelc/completions"
    cp -r "$BUNDLE/completions/." "$PREFIX/share/ovelc/completions/"
    echo "  ${MSG_COMP}: $PREFIX/share/ovelc/completions/"
fi

# ── 自动把补全脚本装进当前用户的 shell 并注册 ──
if [ -d "$PREFIX/share/ovelc/completions" ]; then
    COMP_DIR="$PREFIX/share/ovelc/completions"
    printf "\n==> %s\n" "$MSG_AUTOCOMP"
    if [ "$I18N" = "zh" ]; then
        printf "  是否自动安装补全到你的 shell？[Y/n] "
    else
        printf "  Auto-install completions into your shell? [Y/n] "
    fi
    read -r ans || true

    case "${ans:-y}" in
    y|Y|yes)
        # zsh：装进 ~/.zsh/completions/ 并写入 fpath
        if command -v zsh >/dev/null 2>&1; then
            ZCOMP="$HOME/.zsh/completions"
            mkdir -p "$ZCOMP"
            cp -f "$COMP_DIR/_ovelc" "$ZCOMP/_ovelc"
            # 在 ~/.zshrc 里注册 fpath（若未注册）
            if [ -f "$HOME/.zshrc" ]; then
                LINE="fpath=($ZCOMP \$fpath)"
                if ! grep -qF "$ZCOMP" "$HOME/.zshrc"; then
                    {
                        echo ""
                        echo "# ovelc completion (install.sh auto-added)"
                        echo "$LINE"
                    } >> "$HOME/.zshrc"
                fi
            fi
            echo "  zsh: $ZCOMP/_ovelc  (restart zsh or run: source ~/.zshrc)"
        fi

        # fish
        if command -v fish >/dev/null 2>&1; then
            FDIR="$HOME/.config/fish/completions.d"
            mkdir -p "$FDIR"
            cp -f "$COMP_DIR/ovelc.fish" "$FDIR/ovelc.fish"
            echo "  fish: $FDIR/ovelc.fish"
        fi

        # bash
        if [ -n "${BASH_VERSION:-}" ] || command -v bash >/dev/null 2>&1; then
            BCOMP="$HOME/.bash_completion"
            mkdir -p "$BCOMP"
            cp -f "$COMP_DIR/ovelc.bash" "$BCOMP/ovelc.bash"
            if [ -f "$HOME/.bashrc" ]; then
                if ! grep -qF "ovelc.bash" "$HOME/.bashrc"; then
                    echo "source \"$BCOMP/ovelc.bash\"" >> "$HOME/.bashrc"
                fi
            fi
            echo "  bash: $BCOMP/ovelc.bash"
        fi
        ;;
    *) echo "  ${MSG_COMP_SKIP}" ;;
    esac
fi

echo ""
echo "  ${MSG_PATH}: export PATH=\"$PREFIX/bin:\$PATH\""
echo "  ${MSG_UNINSTALL}: rm -rf $PREFIX"