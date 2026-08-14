#!/bin/sh
# 编译 libsodium 的 Linux 静态库并复制到 deps 下，同时更新公共头文件。
# 在 WSL 或任意 Linux 上跑；源码目录是只读使用，构建放在临时目录里，不污染源码树。
set -e

VERSION="1.0.22-RELEASE"
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
DEST_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../../.." && pwd)
SRC="$REPO_ROOT/third-party/libsodium-$VERSION"

BUILD_DIR="${TMPDIR:-/tmp}/libsodium-build-$VERSION"
INSTALL_DIR="${TMPDIR:-/tmp}/libsodium-install-$VERSION"

if [ ! -x "$SRC/configure" ]; then
    echo "找不到 libsodium 源码或 configure：$SRC" >&2
    exit 1
fi

rm -rf "$BUILD_DIR" "$INSTALL_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# --with-pic：静态库要链进可执行文件，编成位置无关代码更通用
"$SRC/configure" --enable-static --disable-shared --with-pic --prefix="$INSTALL_DIR"
make -j"$(nproc)"
make install

mkdir -p "$DEST_ROOT/lib/linux"
cp "$INSTALL_DIR/lib/libsodium.a" "$DEST_ROOT/lib/linux/"

# 头文件与平台无关，Windows 侧也用这一份（含 configure 生成的 version.h）
rm -rf "$DEST_ROOT/include"
mkdir -p "$DEST_ROOT/include"
cp -r "$INSTALL_DIR/include/." "$DEST_ROOT/include/"

echo "已更新：$DEST_ROOT/lib/linux/libsodium.a 与 $DEST_ROOT/include"
