#!/bin/sh
# 编译 MariaDB Connector/C 的 Linux 静态库并复制到 deps 下。
# 在 WSL 或任意 Linux 上跑；源码目录只读使用，构建放在临时目录里，不污染源码树。
# 编译选项与同目录下 build_mariadb_connector_windows.ps1 保持对应，差别只在 TLS 后端：
# Windows 用系统自带的 SCHANNEL，Linux 这边见下面 WITH_SSL 的说明。
set -e

VERSION="3.4.9"
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
DEST_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
SERVER_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)          # flamingoserver
SRC="$SERVER_ROOT/mariadb-connector-c-$VERSION"

BUILD_DIR="${TMPDIR:-/tmp}/mariadb-connector-build-$VERSION"
INSTALL_DIR="${TMPDIR:-/tmp}/mariadb-connector-install-$VERSION"

if [ ! -f "$SRC/CMakeLists.txt" ]; then
    echo "找不到连接器源码：$SRC" >&2
    exit 1
fi

# TLS 后端。连接器要求必须选一个，给 OFF 会在 configure 阶段直接报
# "Invalid TLS/SSL option"，所以没有"不带 TLS"这个选项。
# Linux 取 OPENSSL，与 Windows 侧取 SCHANNEL 相对应 —— 两个平台都支持与 MySQL 之间
# 的 TLS 连接，各用各平台上最通用的那套实现。
# 编译前提：libssl-dev（Debian/Ubuntu）或 openssl-devel（CentOS）。
# 链接后果：静态库自身不含 OpenSSL，chatserver 链接时要带上 -lssl -lcrypto，
# 见 flamingoserver/CMakeLists.txt 里 MYSQL_LIB 那一段。
# 想换 GnuTLS 的话装好 libgnutls28-dev 后用环境变量覆盖：
#   MARIADB_SSL=GNUTLS ./build_mariadb_connector_linux.sh
SSL_BACKEND="${MARIADB_SSL:-OPENSSL}"

rm -rf "$BUILD_DIR" "$INSTALL_DIR"
mkdir -p "$BUILD_DIR"

# -fPIC：静态库要链进可执行文件，编成位置无关代码更通用（与 libsodium 那份一致）
cmake -S "$SRC" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DWITH_SSL="$SSL_BACKEND" \
    -DWITH_UNIT_TESTS=OFF \
    -DWITH_CURL=OFF \
    -DWITH_EXTERNAL_ZLIB=OFF \
    -DWITH_MYSQLCOMPAT=OFF \
    -DINSTALL_INCLUDEDIR=include/mysql \
    -DCLIENT_PLUGIN_CACHING_SHA2_PASSWORD=STATIC \
    -DCLIENT_PLUGIN_SHA256_PASSWORD=STATIC \
    -DCLIENT_PLUGIN_DIALOG=STATIC \
    -DCLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD=STATIC

cmake --build "$BUILD_DIR" --target mariadbclient -j "$(nproc)"

# 静态库产物名在各版本间叫过 libmariadbclient.a / libmariadb.a，两个都找一下
LIB=$(find "$BUILD_DIR" -name 'libmariadbclient.a' -o -name 'libmariadb.a' | head -1)
if [ -z "$LIB" ]; then
    echo "构建完成但没找到静态库产物" >&2
    exit 1
fi

mkdir -p "$DEST_ROOT/lib/linux"
cp "$LIB" "$DEST_ROOT/lib/linux/libmariadbclient.a"

echo "已更新：$DEST_ROOT/lib/linux/libmariadbclient.a（WITH_SSL=$SSL_BACKEND）"
echo "头文件不在此更新：include/mysql 下那份由 Windows 脚本产出，两个平台共用同一份。"
