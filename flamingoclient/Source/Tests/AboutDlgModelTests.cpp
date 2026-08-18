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

#include "../UI/BalloonMain/AboutDlgModel.h"

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

TEST_CASE("ABOUT4 版本号取自 ClientVersion.h，版本行由它与 Build 拼成", "[about]")
{
    // 不再钉死具体数字：版本号每发一次版就变，钉字面量等于每次发版
    // 都要改测试。真正要钉住的是两件事：
    //   (1) 关于页的版本号与登录 / 检查更新上报的是同一个值——
    //       两者不一致会让用户看到“升完级关于页还是旧版本”；
    //   (2) 版本行确实是由版本号与 build 号拼出来的。
    REQUIRE(std::wstring(kAboutVersion) == std::wstring(FLAMINGO_CLIENT_VERSION_W));
    REQUIRE(std::wstring(kAboutBuild) == std::wstring(FLAMINGO_CLIENT_BUILD_W));

    const std::wstring expected = std::wstring(L"版本 ") + kAboutVersion
                                + L" (Build " + kAboutBuild + L")";
    REQUIRE(std::wstring(kAboutVersionLine) == expected);
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

// =====================================================================
// ABOUT. 功能行右侧链接按钮的宽度钳位
// =====================================================================

TEST_CASE("ABOUT6 文字比最小宽度短时按最小宽度走", "[about]")
{
    // 「查看」这类短文本：按钮不该缩到跟文字一样窄，否则各行右缘参差不齐。
    REQUIRE(ComputeLinkButtonWidth(30, 56, 292, 8) == 56);

    // 恰好等于最小宽度的边界：仍取最小宽度。
    REQUIRE(ComputeLinkButtonWidth(48, 56, 292, 8) == 56);

    // 无文字（传 0）也不会塌成 0。
    REQUIRE(ComputeLinkButtonWidth(0, 56, 292, 8) == 56);
}

TEST_CASE("ABOUT7 文字超过最小宽度时按文字宽度撑开、并带上留白", "[about]")
{
    // 这是修掉官网地址被截断的关键：按钮必须跟着文字变宽，而不是吃死传进来的宽度。
    REQUIRE(ComputeLinkButtonWidth(200, 56, 292, 8) == 208);

    // 留白确实计入：同样的文字宽度，pad 不同结果不同。
    REQUIRE(ComputeLinkButtonWidth(200, 56, 292, 0) == 200);
}

TEST_CASE("ABOUT8 文字超过上限时钳到上限", "[about]")
{
    // 地址长到放不下：宽度停在上限，剩下的交给 DT_END_ELLIPSIS 在尾部收省略号，
    // 绝不能让按钮继续变宽把左侧标题挤没。
    REQUIRE(ComputeLinkButtonWidth(400, 56, 292, 8) == 292);

    // 恰好卡在上限的边界：不越界。
    REQUIRE(ComputeLinkButtonWidth(284, 56, 292, 8) == 292);
    REQUIRE(ComputeLinkButtonWidth(283, 56, 292, 8) == 291);
}

TEST_CASE("ABOUT9 上限小于最小宽度时以上限为准", "[about]")
{
    // 参数本身矛盾的退化情况（对话框被改窄、或标题最小宽度调大后可能出现）：
    // 宁可按钮比预期窄，也不能超出行宽——超出会把左侧标题挤没，那是更糟的结果。
    REQUIRE(ComputeLinkButtonWidth(10, 56, 40, 8) == 40);
    REQUIRE(ComputeLinkButtonWidth(400, 56, 40, 8) == 40);
}
