/**
 *  AboutDlgModelTests.cpp
 *
 *  "关于"对话框纯逻辑（AboutDlgModel.h）的测试：
 *    · 验证 BuildAboutTitle() 拼出的标题栏标题符合「关于 FlamingoIM [<pid>]」格式，
 *      尤其是用户特别要求的"标题栏带上进程 id"这一点 —— 不同 pid 拼出对应方括号串。
 *    · 顺带把固定展示文案常量（版本行 / 标语 / 版权 / 官网 URL 与显示文本）钉一遍，
 *      防止后续误改。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-05
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../BalloonMain/AboutDlgModel.h"

using namespace balloonmain;

// =====================================================================
// ABOUT. 标题栏标题拼装（含进程 id）
// =====================================================================

TEST_CASE("ABOUT1 标题栏标题形如「关于 FlamingoIM [pid]」", "[about]")
{
    REQUIRE(BuildAboutTitle(12345UL) == L"关于 FlamingoIM [12345]");
}

TEST_CASE("ABOUT2 不同进程 id 拼出对应方括号串", "[about]")
{
    REQUIRE(BuildAboutTitle(1UL) == L"关于 FlamingoIM [1]");
    REQUIRE(BuildAboutTitle(99999UL) == L"关于 FlamingoIM [99999]");
}

TEST_CASE("ABOUT3 标题以「关于 FlamingoIM 」开头、以右方括号结尾", "[about]")
{
    std::wstring title = BuildAboutTitle(42UL);

    // 前缀固定为「关于 」+ 应用名 +「 [」。
    std::wstring prefix = std::wstring(L"关于 ") + kAboutAppName + L" [";
    REQUIRE(title.compare(0, prefix.size(), prefix) == 0);

    // 末字符是右方括号，把进程 id 包住。
    REQUIRE(!title.empty());
    REQUIRE(title.back() == L']');
}

// =====================================================================
// ABOUT. 固定展示文案常量（对照 design/关于.png + 已确认决策）
// =====================================================================

TEST_CASE("ABOUT4 版本号写 2.0.0、版本行与 Build 对得上", "[about]")
{
    REQUIRE(std::wstring(kAboutVersion) == L"2.0.0");
    REQUIRE(std::wstring(kAboutVersionLine) == L"版本 2.0.0 (Build 20260518)");
}

TEST_CASE("ABOUT5 官网链接的默认 URL 是 GitHub 仓库、且带协议头", "[about]")
{
    // 官网地址已改为运行期从 flamingo.ini 的 [ui] websiteurl 读取，本常量是读不到
    // 时的兜底值，也是随包发布的 ini 里写的值 —— 两处必须一致，故在此钉住。
    REQUIRE(std::wstring(kAboutWebsiteUrl) == L"https://github.com/balloonwj/flamingo");

    // 显示文本就是 URL 原文，必须带协议头（ShellExecute 也直接用它打开浏览器，
    // 缺协议头会被当成本地路径）。
    std::wstring url(kAboutWebsiteUrl);
    REQUIRE(url.compare(0, 8, L"https://") == 0);
}
