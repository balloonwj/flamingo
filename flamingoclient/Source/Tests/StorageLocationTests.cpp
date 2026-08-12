/**
 *  本地数据根目录归一的单元测试（Suite STL），StorageLocationTests.cpp
 *
 *  覆盖 net::ResolveDataRoot 的全部归一规则：
 *    STL1  配置为空 —— 用默认根（绝大多数用户的情形）；
 *    STL2  绝对路径 —— 原样采用，并保证以反斜杠结尾；
 *    STL3  相对路径 —— 拒绝，回落默认（相对路径会随当前工作目录漂移）；
 *    STL4  含 ".." 的路径 —— 拒绝，回落默认（配置文件可能被手工改坏）；
 *    STL5  只给盘符（"D:"）—— 补成 "D:\"，否则拼出来是相对于该盘当前目录的路径；
 *    STL6  UNC 路径 —— 视为绝对路径，接受；
 *    STL7  默认根自身没带结尾反斜杠时，回落结果也要补上。
 *
 *  框架 Catch2 v3。本套件只验纯逻辑，不碰配置文件、不碰磁盘。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../net/StorageLocation.h"

namespace {

//默认根：程序主目录，按既有约定以反斜杠结尾（g_szHomePath 即是这个形式）。
const wchar_t* const kDefaultRoot = L"E:\\flamingo\\flamingoclient\\Bin\\";

} // namespace

TEST_CASE("STL1 配置为空时用默认根", "[storage]")
{
    CHECK(net::ResolveDataRoot(L"", kDefaultRoot) == std::wstring(kDefaultRoot));
}

TEST_CASE("STL2 绝对路径原样采用并补齐结尾反斜杠", "[storage]")
{
    //用户在选目录框里选出来的路径不带结尾反斜杠，这是最常见的形态。
    CHECK(net::ResolveDataRoot(L"D:\\FlamingoData", kDefaultRoot)
          == std::wstring(L"D:\\FlamingoData\\"));

    //已经带了就不重复添加。
    CHECK(net::ResolveDataRoot(L"D:\\FlamingoData\\", kDefaultRoot)
          == std::wstring(L"D:\\FlamingoData\\"));

    //中文路径同样处理（配置里的中文由程序写入、程序读出，实测可原样往返）。
    CHECK(net::ResolveDataRoot(L"D:\\我的数据\\聊天", kDefaultRoot)
          == std::wstring(L"D:\\我的数据\\聊天\\"));
}

TEST_CASE("STL3 相对路径一律拒绝并回落默认根", "[storage]")
{
    //相对路径的实际位置取决于进程当前工作目录，数据可能被写到任何地方。
    CHECK(net::ResolveDataRoot(L"Data\\Flamingo", kDefaultRoot) == std::wstring(kDefaultRoot));

    //单个反斜杠开头是"当前盘的根目录下"，盘符仍然随工作目录变，同样不接受。
    CHECK(net::ResolveDataRoot(L"\\FlamingoData", kDefaultRoot) == std::wstring(kDefaultRoot));
}

TEST_CASE("STL4 含上跳的路径一律拒绝", "[storage]")
{
    //配置文件是纯文本、可能被手工改坏，含 ".." 的路径不接受。
    CHECK(net::ResolveDataRoot(L"D:\\Data\\..\\..\\Windows", kDefaultRoot)
          == std::wstring(kDefaultRoot));
}

TEST_CASE("STL5 只给盘符时补成盘根", "[storage]")
{
    //不补的话后续拼出来是 "D:Users\\..."，那是相对于 D 盘当前目录的路径。
    CHECK(net::ResolveDataRoot(L"D:", kDefaultRoot) == std::wstring(L"D:\\"));
}

TEST_CASE("STL6 UNC 路径视为绝对路径", "[storage]")
{
    CHECK(net::ResolveDataRoot(L"\\\\nas\\share\\flamingo", kDefaultRoot)
          == std::wstring(L"\\\\nas\\share\\flamingo\\"));
}

TEST_CASE("STL7 默认根没带结尾反斜杠时回落结果也补齐", "[storage]")
{
    //调用方传进来的默认根理论上总带反斜杠，这里守住这个约定，免得拼出
    //"...BinUsers\\" 这种把两截黏在一起的路径。
    CHECK(net::ResolveDataRoot(L"", L"E:\\flamingo\\Bin")
          == std::wstring(L"E:\\flamingo\\Bin\\"));
}
