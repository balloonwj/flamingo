/**
 *  主面板贴边自动隐藏的收起 / 展开状态机单元测试（PDP1～PDP7），PanelDockPolicyTests.cpp
 *
 *  覆盖：鼠标离开满延时才收起、鼠标左键按着 / 有菜单开着 / 正在输入文字时不收起、
 *  从托盘唤出后鼠标没进入过主面板时不收起、收起状态下关掉开关立即退出贴边、
 *  收起状态下鼠标碰到细条时展开，以及 GetTickCount 回绕时的计时。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "catch_amalgamated.hpp"
#include <windows.h>
#include "../UI/BalloonMain/PanelDockPolicy.h"

using namespace balloonmain;

namespace {

//构造一份"开关开启、鼠标在外面、没有任何需要保持展开的情况"的输入
PanelDockInput Outside(DWORD now)
{
    PanelDockInput in;
    in.m_enabled = true;
    in.m_nowTick = now;
    return in;
}

//构造一份"鼠标在主面板上（收起时即在细条上）"的输入
PanelDockInput Over(DWORD now)
{
    PanelDockInput in = Outside(now);
    in.m_cursorOver = true;
    return in;
}

//把一个已贴上边、鼠标刚离开的状态机推进到收起状态，返回收起发生的时刻
DWORD DriveToHidden(CPanelDockPolicy& policy, DWORD start)
{
    policy.Dock(kPanelDockTop);
    REQUIRE(policy.Tick(Outside(start)) == kDockActionNone);
    REQUIRE(policy.Tick(Outside(start + kDockHideDelayMs)) == kDockActionHide);
    REQUIRE(policy.IsHidden());
    return start + kDockHideDelayMs;
}

} // namespace

TEST_CASE("PDP1 鼠标离开未满延时不收起，满延时后收起", "[paneldock]")
{
    SECTION("普通计时")
    {
        CPanelDockPolicy policy;
        policy.Dock(kPanelDockTop);
        CHECK(policy.Tick(Over(900)) == kDockActionNone);
        CHECK(policy.Tick(Outside(1000)) == kDockActionNone);
        CHECK(policy.Tick(Outside(1000 + kDockHideDelayMs - 1)) == kDockActionNone);
        CHECK_FALSE(policy.IsHidden());
        CHECK(policy.Tick(Outside(1000 + kDockHideDelayMs)) == kDockActionHide);
        CHECK(policy.IsHidden());
        CHECK(policy.IsDocked());
    }

    SECTION("中途鼠标回到主面板上，离开计时从头开始")
    {
        CPanelDockPolicy policy;
        policy.Dock(kPanelDockLeft);
        CHECK(policy.Tick(Outside(1000)) == kDockActionNone);
        CHECK(policy.Tick(Over(1300)) == kDockActionNone);
        CHECK(policy.Tick(Outside(1400)) == kDockActionNone);
        CHECK(policy.Tick(Outside(1000 + kDockHideDelayMs)) == kDockActionNone);
        CHECK(policy.Tick(Outside(1400 + kDockHideDelayMs)) == kDockActionHide);
    }

    SECTION("GetTickCount 回绕时照常计时")
    {
        CPanelDockPolicy policy;
        policy.Dock(kPanelDockRight);
        const DWORD start = 0xFFFFFF00;
        CHECK(policy.Tick(Outside(start)) == kDockActionNone);
        CHECK(policy.Tick(Outside(start + 0x80)) == kDockActionNone);
        CHECK(policy.Tick(Outside(start + kDockHideDelayMs)) == kDockActionHide);
    }

    SECTION("未贴边时什么都不做")
    {
        CPanelDockPolicy policy;
        CHECK(policy.Tick(Outside(1000)) == kDockActionNone);
        CHECK(policy.Tick(Outside(1000 + 10 * kDockHideDelayMs)) == kDockActionNone);
        CHECK_FALSE(policy.IsDocked());
    }
}

TEST_CASE("PDP2 鼠标左键按着时不收起，松开后重新计时", "[paneldock]")
{
    CPanelDockPolicy policy;
    policy.Dock(kPanelDockTop);
    PanelDockInput down = Outside(1000);
    down.m_mouseButtonDown = true;
    CHECK(policy.Tick(down) == kDockActionNone);
    down.m_nowTick = 1000 + 4 * kDockHideDelayMs;
    CHECK(policy.Tick(down) == kDockActionNone);
    CHECK_FALSE(policy.IsHidden());

    const DWORD released = 1000 + 5 * kDockHideDelayMs;
    CHECK(policy.Tick(Outside(released)) == kDockActionNone);
    CHECK(policy.Tick(Outside(released + kDockHideDelayMs - 1)) == kDockActionNone);
    CHECK(policy.Tick(Outside(released + kDockHideDelayMs)) == kDockActionHide);
}

TEST_CASE("PDP3 鼠标在弹出浮层上或有菜单开着时不收起", "[paneldock]")
{
    SECTION("鼠标停在主面板弹出的浮层上（调用方按在主面板上传入）")
    {
        CPanelDockPolicy policy;
        policy.Dock(kPanelDockTop);
        CHECK(policy.Tick(Over(1000)) == kDockActionNone);
        CHECK(policy.Tick(Over(1000 + 4 * kDockHideDelayMs)) == kDockActionNone);
        CHECK_FALSE(policy.IsHidden());
    }

    SECTION("菜单开着、鼠标在菜单和主面板之外")
    {
        CPanelDockPolicy policy;
        policy.Dock(kPanelDockTop);
        PanelDockInput menu = Outside(1000);
        menu.m_menuOpen = true;
        CHECK(policy.Tick(menu) == kDockActionNone);
        menu.m_nowTick = 1000 + 4 * kDockHideDelayMs;
        CHECK(policy.Tick(menu) == kDockActionNone);
        CHECK_FALSE(policy.IsHidden());

        //菜单关掉后重新计时
        const DWORD closed = 1000 + 5 * kDockHideDelayMs;
        CHECK(policy.Tick(Outside(closed)) == kDockActionNone);
        CHECK(policy.Tick(Outside(closed + kDockHideDelayMs)) == kDockActionHide);
    }
}

TEST_CASE("PDP4 正在主面板上输入文字（如搜索框）时不收起", "[paneldock]")
{
    CPanelDockPolicy policy;
    policy.Dock(kPanelDockRight);
    PanelDockInput editing = Outside(1000);
    editing.m_editingText = true;
    CHECK(policy.Tick(editing) == kDockActionNone);
    editing.m_nowTick = 1000 + 4 * kDockHideDelayMs;
    CHECK(policy.Tick(editing) == kDockActionNone);
    CHECK_FALSE(policy.IsHidden());
}

TEST_CASE("PDP5 从托盘唤出后鼠标没进入过主面板时不收起，进入再离开后按延时收起", "[paneldock]")
{
    CPanelDockPolicy policy;
    const DWORD hiddenAt = DriveToHidden(policy, 1000);

    //唤出前处于收起状态，调用方须把窗口移回吸附位置
    CHECK(policy.NotifyShownExternally());
    CHECK_FALSE(policy.IsHidden());
    CHECK(policy.IsDocked());

    //鼠标一直在托盘处，不收起
    CHECK(policy.Tick(Outside(hiddenAt + 100)) == kDockActionNone);
    CHECK(policy.Tick(Outside(hiddenAt + 100 + 10 * kDockHideDelayMs)) == kDockActionNone);
    CHECK_FALSE(policy.IsHidden());

    //鼠标进入主面板，再离开
    const DWORD entered = hiddenAt + 20 * kDockHideDelayMs;
    CHECK(policy.Tick(Over(entered)) == kDockActionNone);
    CHECK(policy.Tick(Outside(entered + 100)) == kDockActionNone);
    CHECK(policy.Tick(Outside(entered + 100 + kDockHideDelayMs)) == kDockActionHide);

    SECTION("展开状态下被唤出时返回 false，未贴边时同样返回 false")
    {
        CPanelDockPolicy shown;
        shown.Dock(kPanelDockTop);
        CHECK_FALSE(shown.NotifyShownExternally());
        CPanelDockPolicy none;
        CHECK_FALSE(none.NotifyShownExternally());
        CHECK_FALSE(none.IsDocked());
    }
}

TEST_CASE("PDP6 关掉开关时立即退出贴边状态", "[paneldock]")
{
    SECTION("收起状态")
    {
        CPanelDockPolicy policy;
        const DWORD hiddenAt = DriveToHidden(policy, 1000);
        PanelDockInput off = Outside(hiddenAt + 100);
        off.m_enabled = false;
        CHECK(policy.Tick(off) == kDockActionUndock);
        CHECK_FALSE(policy.IsDocked());
        CHECK_FALSE(policy.IsHidden());
        //退出后不再有任何动作
        CHECK(policy.Tick(Outside(hiddenAt + 100 + 10 * kDockHideDelayMs)) == kDockActionNone);
    }

    SECTION("展开状态")
    {
        CPanelDockPolicy policy;
        policy.Dock(kPanelDockLeft);
        PanelDockInput off = Over(1000);
        off.m_enabled = false;
        CHECK(policy.Tick(off) == kDockActionUndock);
        CHECK_FALSE(policy.IsDocked());
    }
}

TEST_CASE("PDP7 收起状态下鼠标碰到细条时展开", "[paneldock]")
{
    CPanelDockPolicy policy;
    const DWORD hiddenAt = DriveToHidden(policy, 1000);

    //鼠标不在细条上：保持收起
    CHECK(policy.Tick(Outside(hiddenAt + 100)) == kDockActionNone);
    CHECK(policy.IsHidden());

    //碰到细条：展开
    CHECK(policy.Tick(Over(hiddenAt + 200)) == kDockActionShow);
    CHECK_FALSE(policy.IsHidden());
    CHECK(policy.GetEdge() == kPanelDockTop);

    //展开后鼠标离开，按延时再次收起
    CHECK(policy.Tick(Outside(hiddenAt + 300)) == kDockActionNone);
    CHECK(policy.Tick(Outside(hiddenAt + 300 + kDockHideDelayMs)) == kDockActionHide);
}
