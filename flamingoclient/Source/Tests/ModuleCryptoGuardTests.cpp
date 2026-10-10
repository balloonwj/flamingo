/**
 *  插件 DLL 初始化自己那一份 libsodium 的守卫用例（标签 [modulecrypto]），ModuleCryptoGuardTests.cpp
 *  背景：FlamingoMedia.dll 与 FlamingoFileShare.dll 各自静态链接了一份 libsodium，每个模块有自己的全局状态，
 *  exe 启动时的 sodium_init 只初始化 exe 那一份。DLL 不调用 sodium_init 时，随机数源与 ChaCha20、Poly1305 等算法的实现
 *  没有按 CPU 特性选择，中转加解密慢一倍多。两个模块总控依赖窗口与宿主，不在单元测试工程里，本用例在运行时读取源文件，检查：
 *    - MCG1 CMediaModule::Init 调用 sodium_init()，并把返回值写进日志；
 *    - MCG2 CFileShareModule::Init 同上。
 *  日志里的返回值是运行期的证据：0 表示这一次才完成初始化（此前确实没有初始化过），1 表示此前已初始化，-1 表示失败。
 *  源文件位置：首选 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>

namespace {

//读文件时每次读的字节数
const size_t kMcgReadChunk = 4096;
//本文件在 flamingoclient 下的层数（Source\Tests\本文件）：去掉这么多级就是 flamingoclient
const int kMcgDepth = 3;

//读一个源文件的全部内容；读不到时返回空串
std::string McgReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kMcgReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

//本测试文件所在目录往上两级就是 flamingoclient（Source\Tests\本文件）
std::string McgClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kMcgDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

//取一个成员函数的函数体：从定义开始到下一个同类成员函数定义（或文件末尾）
std::string McgFunctionBody(const std::string& source, const std::string& signature, const std::string& classPrefix)
{
    const size_t begin = source.find(signature);
    if (begin == std::string::npos)
        return std::string();
    const size_t next = source.find("\n" + classPrefix, begin + signature.size());
    return source.substr(begin, next == std::string::npos ? std::string::npos : next - begin);
}

/**
 *  检查一个 Init 函数体：调用了 sodium_init()，并且返回值写进了日志（日志里带 "sodium_init returned"）。
 *  @param body Init 的函数体文本
 */
void McgCheckInitBody(const std::string& body)
{
    REQUIRE_FALSE(body.empty());
    CHECK(body.find("sodium_init()") != std::string::npos);
    CHECK(body.find("sodium_init returned") != std::string::npos);
}

} // namespace

TEST_CASE("MCG1 FlamingoMedia initializes its own libsodium", "[modulecrypto]")
{
    const std::string root = McgClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string source = McgReadFile(root + "\\FlamingoMedia\\Host\\MediaModule.cpp");
    REQUIRE_FALSE(source.empty());
    McgCheckInitBody(McgFunctionBody(source, "bool CMediaModule::Init(", "bool CMediaModule::"));
}

TEST_CASE("MCG2 FlamingoFileShare initializes its own libsodium", "[modulecrypto]")
{
    const std::string root = McgClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string source = McgReadFile(root + "\\FlamingoFileShare\\FileShareModule.cpp");
    REQUIRE_FALSE(source.empty());
    McgCheckInitBody(McgFunctionBody(source, "bool CFileShareModule::Init(", "bool CFileShareModule::"));
}
