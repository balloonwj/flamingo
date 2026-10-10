/**
 *  MariaDB Connector/C 源码编译的守卫用例（Suite MDB），test_mariadbclient.cpp
 *
 *  自 2026-10-09 起，连接器以源码形式随仓库提交（mariadb-connector-c-3.4.9）：Windows 由
 *  windowsproj/mariadbclient.vcxproj、Linux 由 CMakeLists.txt 的 add_subdirectory 编出静态库，
 *  不再使用 deps/mariadb-connector 下的预编译库（docs/服务端依赖库源码编译方案.md 第 6 节）。
 *  本组用例不需要 MySQL 服务器，只检查链接进来的连接器就是这份源码、编译参数与此前的预编译库一致：
 *    MDB1  版本号为 3.4.9，且与编译时头文件里的版本号一致；
 *    MDB2  常用的认证插件都是静态编入的，不依赖从磁盘加载插件动态库；
 *    MDB3  TLS 后端：Windows 为 SChannel，Linux 为 OpenSSL。
 *
 *  只编进链接了连接器的测试程序：Windows 的 Tests.exe、Linux 的 flamingo_tests_mysql；
 *  Linux 的 flamingo_tests 不链接连接器，不包含本文件。
 *
 *  balloonwj@qq.com   2026-10-09
 */
#include "doctest.h"
#include <string>
//mysql/client_plugin.h（插件查找函数 mysql_client_find_plugin 的声明在这里）无条件包含上游的 ma_pvio.h、ma_compress.h
//等内部头文件，它们要用到 ssize_t、uint、NEAR 等只由内部总头文件 ma_global.h 提供的类型与宏。上游自己的单元测试
//（unittest/libmariadb/my_test.h）也是先包含 ma_global.h 再包含 mysql.h，这里照同样的顺序。
//ma_global.h 会给 C 语言 typedef 一个 bool，在 C++ 里非法；上游的判断条件是 (!defined(HAVE_BOOL) || !defined(__cplusplus))，
//C++ 代码先定义 HAVE_BOOL 即可跳过（HAVE_BOOL 在 ma_global.h 里只用于这一处）
#define HAVE_BOOL 1
#include <ma_global.h>
#include <mysql.h>
#include <mysql/client_plugin.h>

namespace {

//随仓库提交的连接器版本号，按 mysql_get_client_version() 的编码方式（主版本 * 10000 + 次版本 * 100 + 修订号）
//写成 3.4.9 → 30409。升级连接器时同步修改此处
const unsigned long kBundledConnectorVersion = 30409;

//chatserver 连 MySQL 8（默认 caching_sha2_password）与 MariaDB 时用得到的认证插件：前四个以前的预编译库
//用 CLIENT_PLUGIN_xxx=STATIC 编入，mysql_native_password 是连接器默认就静态编入的
const char* const kStaticAuthPlugins[] =
{
    "mysql_native_password",
    "caching_sha2_password",
    "sha256_password",
    "dialog",
    "mysql_clear_password"
};

//一个不存在的插件目录。插件查找在已登记的插件里找不到时，会到插件目录里加载动态库
//（上游 libmariadb/ma_client_plugin.c.in 的 mysql_client_find_plugin）；把目录指到这里，
//查找成功就只可能是静态编入的插件
const char* const kMissingPluginDir = "flamingo_mdb_no_such_plugin_dir";

//反向对照用的插件：client_ed25519 在两个平台上都被上游编成动态插件（CMake 输出的 Dynamic PLUGINS 里有它），
//chatserver 不携带它的动态库，所以同样的查找应当失败
const char* const kDynamicOnlyPlugin = "client_ed25519";

//TLS 后端报告的库名前缀。SChannel 是固定字符串（上游 libmariadb/secure/schannel.c）；
//OpenSSL 由 OpenSSL_version() 填写，形如 "OpenSSL 3.0.13"（上游 libmariadb/secure/openssl.c）
#ifdef WIN32
const char* const kExpectedTlsLibraryPrefix = "Schannel";
#else
const char* const kExpectedTlsLibraryPrefix = "OpenSSL";
#endif

} // namespace

//MDB1：mysql_get_client_version() 返回库实现里编进去的版本号，MARIADB_PACKAGE_VERSION_ID 是头文件里的版本号。
//两者一致，说明头文件与实现出自同一份源码；再等于 kBundledConnectorVersion，说明链接的是随仓库提交的 3.4.9
TEST_CASE("MDB1 链接的 MariaDB 连接器为随仓库提交的 3.4.9，且与头文件版本一致")
{
    CHECK(mysql_get_client_version() == static_cast<unsigned long>(MARIADB_PACKAGE_VERSION_ID));
    CHECK(mysql_get_client_version() == kBundledConnectorVersion);
}

//MDB2：mysql_init 会完成连接器的一次性初始化并登记全部静态插件。把插件目录指向不存在的目录后逐个查找，
//每个都能找到，说明它们是静态编入的，部署 chatserver 时不需要携带插件动态库
TEST_CASE("MDB2 常用认证插件都静态编入连接器，不依赖从磁盘加载")
{
    MYSQL* mysql = mysql_init(NULL);
    REQUIRE(mysql != NULL);
    REQUIRE(mysql_optionsv(mysql, MYSQL_PLUGIN_DIR, kMissingPluginDir) == 0);

    const size_t pluginCount = sizeof(kStaticAuthPlugins) / sizeof(kStaticAuthPlugins[0]);
    for (size_t i = 0; i < pluginCount; ++i)
    {
        struct st_mysql_client_plugin* plugin =
            mysql_client_find_plugin(mysql, kStaticAuthPlugins[i], MYSQL_CLIENT_AUTHENTICATION_PLUGIN);
        CHECK_MESSAGE(plugin != NULL, kStaticAuthPlugins[i]);
    }

    //反向对照：动态插件在同样的条件下查不到，才说明上面查到的那几个确实来自静态登记，
    //而不是从某个目录加载进来的；否则本用例即使插件不是静态编入的也会通过
    CHECK(mysql_client_find_plugin(mysql, kDynamicOnlyPlugin, MYSQL_CLIENT_AUTHENTICATION_PLUGIN) == NULL);

    mysql_close(mysql);
}

//MDB3：连接器的一次性初始化（由 mysql_init 触发，上游 mariadb_lib.c 的 mysql_once_init）会启动 TLS 后端
//并记下库名。Windows 应为系统自带的 SChannel，Linux 应为 OpenSSL，与此前预编译库的编译参数一致
TEST_CASE("MDB3 TLS 后端：Windows 为 SChannel，Linux 为 OpenSSL")
{
    MYSQL* mysql = mysql_init(NULL);
    REQUIRE(mysql != NULL);

    const char* tlsLibrary = NULL;
    REQUIRE(mariadb_get_infov(mysql, MARIADB_TLS_LIBRARY, &tlsLibrary) == 0);
    REQUIRE(tlsLibrary != NULL);

    const std::string library(tlsLibrary);
    CHECK_MESSAGE(library.find(kExpectedTlsLibraryPrefix) == 0, library);

    mysql_close(mysql);
}
