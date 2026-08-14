MariaDB Connector/C 3.4.9（编译产物，Windows 专用）
====================================================

用途：chatserver 在 dbtype=mysql 时连接 MySQL / MariaDB 服务端。
它取代了原先仓库自带的 mysql-5.1.73-win32（那份只有 32 位库，且太老，
不支持 MySQL 8 默认的 caching_sha2_password 认证），旧包已从仓库删除。

来源：flamingoserver\mariadb-connector-c-3.4.9，官方 release 源码，未做改动。

产物清单
--------

    include\mysql\*.h                          公共头文件
    lib\Win32\Debug\mariadbclient.lib           静态库，/MTd
    lib\Win32\Release\mariadbclient.lib         静态库，/MT
    lib\x64\Debug\mariadbclient.lib             静态库，/MTd
    lib\x64\Release\mariadbclient.lib           静态库，/MT

头文件目录之所以叫 include\mysql，是为了让代码里既有的 #include <mysql/mysql.h>
原样可用；vcxproj 里同时把 include 与 include\mysql 两级都加进包含目录，
后者是给连接器头文件之间的平铺互相引用（如 #include <mariadb_com.h>）用的。

编译参数（要点）
----------------

    -DWITH_SSL=SCHANNEL          用 Windows 自带的 TLS，不引入 OpenSSL 依赖
    -DWITH_EXTERNAL_ZLIB=OFF     用连接器自带的 zlib
    -DWITH_CURL=OFF -DWITH_MSI=OFF -DWITH_SIGNCODE=OFF -DWITH_UNIT_TESTS=OFF
    -DCLIENT_PLUGIN_CACHING_SHA2_PASSWORD=STATIC
    -DCLIENT_PLUGIN_SHA256_PASSWORD=STATIC
    -DCLIENT_PLUGIN_DIALOG=STATIC
    -DCLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD=STATIC
        认证插件全部编成静态，连 MySQL 8 也不需要额外携带插件 DLL
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>
    -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
        静态 CRT，与 flamingoserver 各工程一致

静态链接它时，调用方还要自己带上这些系统库（连接器 configure 输出的 SYSTEM_LIBS）：

    ws2_32.lib shlwapi.lib crypt32.lib bcrypt.lib secur32.lib

Linux 侧同样用这里的产物（2026-08-14 起）：lib/linux/libmariadbclient.a，头文件与
Windows 共用 include/mysql 那一份。CMakeLists.txt 按完整路径链接它，不再 find_library
去系统里找，因此机器上不装 mysql-devel / mariadb-devel 也能编。仍然可以用
cmake -DWITH_MYSQL=OFF . 关掉 MySQL 后端只跑 SQLite（联调常用）。

Linux 侧的 TLS 后端选的是 OPENSSL（Windows 是 SCHANNEL）—— 连接器不接受
WITH_SSL=OFF，必须选一个。由此带来两个要求：

    编译该库时：装 libssl-dev（Debian/Ubuntu）或 openssl-devel（CentOS）
    链接 chatserver 时：带上 -lssl -lcrypto -ldl（已写进 CMakeLists.txt 的 MYSQL_LIB）

重新编译
--------

Windows：

    build\build_mariadb_connector_windows.ps1

Linux / WSL：

    build/build_mariadb_connector_linux.sh

Linux 脚本只产出 lib/linux/libmariadbclient.a，不动 include —— 头文件两个平台共用，
由 Windows 那份脚本产出。换连接器版本时，改两个脚本顶部的版本号。
