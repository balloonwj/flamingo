/**
 *  主面板位置记忆与贴边自动隐藏开关的配置读写单元测试（MPC1～MPC4），MainPanelPlacementConfigTests.cpp
 *
 *  覆盖 CUserConfig 里与主面板相关的两组配置：
 *    · [MainPanel] 一节的主面板位置与尺寸：没保存过时视为未保存；旧版遗留的
 *      [MainDlg] MainDlgX 等键不再被读取；写进配置文件再读出（含负坐标）值不变；
 *    · [Settings] 一节的 DockAutoHide：缺键时默认开启，开 / 关两种取值都能原样读回。
 *  每条用例都用独立的临时配置文件，文件名带进程 id，两个测试进程同时跑也不会共用。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"
#include <windows.h>
#include <string>
#include "../Session/UserConfig.h"

namespace {

//生成本进程专用的临时配置文件路径，并删掉上一次残留的同名文件。
//  tag：区分同一进程内不同用例的文件名片段
std::wstring FreshIniPath(const wchar_t* tag)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t name[MAX_PATH] = { 0 };
    swprintf_s(name, L"%sflamingo_mainpanel_%s_%lu.ini", dir, tag, ::GetCurrentProcessId());
    ::DeleteFileW(name);
    return name;
}

bool SameRect(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

} // namespace

TEST_CASE("MPC1 配置文件里没有 [MainPanel] 时视为没有保存过主面板位置", "[mainpanelplace]")
{
    const std::wstring path = FreshIniPath(L"mpc1");
    CUserConfig cfg;
    REQUIRE(cfg.LoadConfig(path.c_str()));
    CHECK_FALSE(cfg.GetMainPanelPlacement().m_hasSaved);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("MPC2 只有旧版 [MainDlg] MainDlgX 等键时同样视为没有保存过", "[mainpanelplace]")
{
    //旧版 SaveConfig 每次都会把默认值写进这四个键，所以现有账号的配置文件里几乎都有它们。
    //若被当成"保存过"，主面板会跳到旧默认位置（屏幕右侧），因此新版不得再读取它们
    const std::wstring path = FreshIniPath(L"mpc2");
    ::WritePrivateProfileStringW(L"MainDlg", L"MainDlgX", L"1640", path.c_str());
    ::WritePrivateProfileStringW(L"MainDlg", L"MainDlgY", L"150", path.c_str());
    ::WritePrivateProfileStringW(L"MainDlg", L"MainDlgWidth", L"280", path.c_str());
    ::WritePrivateProfileStringW(L"MainDlg", L"MainDlgHeight", L"720", path.c_str());

    CUserConfig cfg;
    REQUIRE(cfg.LoadConfig(path.c_str()));
    CHECK_FALSE(cfg.GetMainPanelPlacement().m_hasSaved);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("MPC3 主面板位置写进配置文件再读出，值不变（含负坐标）", "[mainpanelplace]")
{
    const std::wstring path = FreshIniPath(L"mpc3");

    //副屏在主屏左边时，窗口的屏幕坐标为负数
    winplace::WindowPlacementPrefs saved;
    saved.m_hasSaved = true;
    saved.m_rect.left = -1500;
    saved.m_rect.top = 120;
    saved.m_rect.right = -1500 + 320;
    saved.m_rect.bottom = 120 + 760;

    CUserConfig writer;
    REQUIRE(writer.LoadConfig(path.c_str()));
    writer.SetMainPanelPlacement(saved);
    REQUIRE(writer.SaveConfig(path.c_str()));

    CUserConfig reader;
    REQUIRE(reader.LoadConfig(path.c_str()));
    const winplace::WindowPlacementPrefs& got = reader.GetMainPanelPlacement();
    CHECK(got.m_hasSaved);
    CHECK(SameRect(got.m_rect, saved.m_rect));
    ::DeleteFileW(path.c_str());
}

TEST_CASE("MPC4 贴边自动隐藏开关缺键时默认开启，开与关都能原样读回", "[mainpanelplace]")
{
    const std::wstring path = FreshIniPath(L"mpc4");

    SECTION("缺键时默认开启")
    {
        CUserConfig cfg;
        REQUIRE(cfg.LoadConfig(path.c_str()));
        CHECK(cfg.IsEnableDockAutoHide() != FALSE);
    }

    SECTION("关闭后写盘再读出仍为关闭，重新开启后读出为开启")
    {
        CUserConfig writer;
        REQUIRE(writer.LoadConfig(path.c_str()));
        writer.EnableDockAutoHide(FALSE);
        REQUIRE(writer.SaveConfig(path.c_str()));

        CUserConfig reader;
        REQUIRE(reader.LoadConfig(path.c_str()));
        CHECK(reader.IsEnableDockAutoHide() == FALSE);

        reader.EnableDockAutoHide(TRUE);
        REQUIRE(reader.SaveConfig(path.c_str()));
        CUserConfig again;
        REQUIRE(again.LoadConfig(path.c_str()));
        CHECK(again.IsEnableDockAutoHide() != FALSE);
    }

    ::DeleteFileW(path.c_str());
}
