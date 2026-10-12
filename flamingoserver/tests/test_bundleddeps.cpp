/**
 *  随仓库源码编译的第三方库的守卫用例（Suite SOD / OSL），test_bundleddeps.cpp
 *
 *  docs/服务端依赖库源码编译方案.md 第 12 节。Linux 上 libsodium 与（默认情况下的）OpenSSL 在 cmake 配置阶段
 *  从 flamingoserver/libsodium-1.0.22、flamingoserver/openssl-3.5.9 编出静态库（cmake/FlamingoDeps.cmake）；
 *  Windows 仍链接 deps/libsodium 下的预编译库，且不使用 OpenSSL。本组用例检查链接进来的就是这些库：
 *    SOD1  libsodium 运行时报告的版本为 1.0.22，且与编译时头文件里的版本一致（Linux、Windows 都检查）；
 *    OSL1  OpenSSL 运行时的版本与头文件一致：使用自带 OpenSSL 时都必须是 3.5.9；使用系统 OpenSSL
 *          （-DWITH_SYSTEM_OPENSSL=ON）时只比较主、次版本，因为动态库随发行版升级后补丁号可以比头文件新；
 *    OSL2  使用自带 OpenSSL 时，OPENSSLDIR 是 CMake 设定的那个目录（FLAMINGO_OPENSSL_DIR），
 *          确认不会去读取系统里别的版本的 openssl.cnf。
 *  OSL1、OSL2 只在定义了 FLAMINGO_HAS_OPENSSL（Linux 上链接了 OpenSSL）时编译。
 *
 *  balloonwj@qq.com   2026-10-10
 */
#include <string>

#include "doctest.h"
#include <sodium.h>

#ifdef FLAMINGO_HAS_OPENSSL
#include <openssl/crypto.h>
#include <openssl/opensslv.h>
#endif

namespace {

//随仓库提交的 libsodium 版本。升级时同步修改此处与 CMakeLists.txt 里的版本号
const char* const kBundledSodiumVersion = "1.0.22";

#ifdef FLAMINGO_HAS_OPENSSL
//随仓库提交的 OpenSSL 版本，按 3.x 的 OPENSSL_VERSION_NUMBER 编码（0xMNN00PP0L）写成 3.5.9 → 0x30500090。
//升级时同步修改此处与 CMakeLists.txt 里的版本号
const unsigned long kBundledOpenSslVersion = 0x30500090UL;

//只保留版本号里的主、次版本两部分的掩码。1.0.x（0xMNNFFPPSL）与 3.x（0xMNN00PP0L）的编码里，
//高 12 位都是主版本（4 位）加次版本（8 位）
const unsigned long kMajorMinorMask = 0xFFF00000UL;

//运行时的 OpenSSL 版本号：1.1.0 起为 OpenSSL_version_num，之前为 SSLeay
unsigned long RuntimeOpenSslVersion()
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    return ::SSLeay();
#else
    return ::OpenSSL_version_num();
#endif
}
#endif

} // anonymous namespace

TEST_CASE("SOD1 链接的 libsodium 为随仓库提交的 1.0.22，且与头文件版本一致")
{
    const std::string runtime(::sodium_version_string());
    CHECK(runtime == SODIUM_VERSION_STRING);
    CHECK(runtime == kBundledSodiumVersion);
}

#ifdef FLAMINGO_HAS_OPENSSL
TEST_CASE("OSL1 OpenSSL 运行时版本与头文件一致，自带 OpenSSL 时为 3.5.9")
{
    const unsigned long runtime = RuntimeOpenSslVersion();
    const unsigned long header = OPENSSL_VERSION_NUMBER;
#ifdef FLAMINGO_BUNDLED_OPENSSL
    CHECK(header == kBundledOpenSslVersion);
    CHECK(runtime == kBundledOpenSslVersion);
#else
    CHECK((runtime & kMajorMinorMask) == (header & kMajorMinorMask));
#endif
}

#ifdef FLAMINGO_BUNDLED_OPENSSL
TEST_CASE("OSL2 自带 OpenSSL 的 OPENSSLDIR 为 CMake 设定的目录")
{
    //OpenSSL_version(OPENSSL_DIR) 的格式为 OPENSSLDIR: "<目录>"（OpenSSL crypto/cversion.c）
    const std::string expected = std::string("OPENSSLDIR: \"") + FLAMINGO_OPENSSL_DIR + "\"";
    const std::string actual(::OpenSSL_version(OPENSSL_DIR));
    CHECK(actual == expected);
}
#endif
#endif
