/**
 *  MainPanelTrayMenuTests.cpp
 *
 *  系统托盘右键菜单（MainPanelTrayMenu）的测试：验证纯函数 BuildTrayMenuModel()
 *  产出的行模型符合 flamingo.rc 原托盘子菜单 —— 三种形态的条目数、顺序、文本、
 *  分隔线位置、命令 id 的唯一性与非 0 约束、静音项的可勾选 / 勾选态、图标列与
 *  勾选标记互斥（可勾选项不带图标），以及"用户当前所处的在线状态"在四个状态项里
 *  被正确标出（文字带"（当前）"后缀，每次至多一项；图标则恒为本行状态的徽标字形）。
 *
 *  TRAY13 起另加一组：托盘菜单必须与主面板状态胶囊同走 OnlineCapability 那套连接态
 *  规则 —— 掉线 / 重连中当前项显示"正在连接（当前）"而不是"在线（当前）"，且四个
 *  状态项一并灰显不可点。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <algorithm>
#include <vector>
#include <cwchar>          // wcscmp —— 比对菜单项中文文本

#include "../BalloonMain/MainPanelTrayMenu.h"
#include "../BalloonMain/MainPanelIcons.h"        // IconKind（断言静音项 / 当前状态项的图标）
#include "../BalloonMain/MainPanelStatusMenu.h"   // GetPresenceVisual（断言圆点配色仍与状态菜单同源）
#include "../BalloonMain/OnlineCapability.h"      // ConnectionUiState / GetConnectionMenuLabel
#include "../net/Protocol.h"                      // presence_type（构造各种"当前状态"入参）

using namespace balloonmain;

// 大部分既有用例只关心菜单结构、不关心当前状态标记，统一传"在线"作当前状态；
// 此时被标记的是第 1 行，其余三行文字保持纯状态名。
namespace {

const int kCurrentOnline = presence_online;

// 同理，这些用例也统一传"连接已就绪"作连接态 —— 断线态下的表现由 TRAY13 起的
// 那组用例专门覆盖，这里只验证菜单结构本身。
const int kConnOnline = (int)kConnUiOnline;

} // anonymous namespace

// 统计某菜单里功能项 / 分隔条数量的小工具。
namespace {

void CountRows(const std::vector<TrayMenuEntry>& items, int& funcs, int& seps)
{
    funcs = 0;
    seps  = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].isSeparator)
        {
            ++seps;
        }
        else
        {
            ++funcs;
        }
    }
}

} // anonymous namespace

// =====================================================================
// TRAY1 / TRAY2 —— 登录前 / 面板锁定：打开主面板 ── 退出（两态内容相同）
// =====================================================================

// 已作废的在线状态取值：4 曾经是「隐身」，本产品不提供该功能（见 net/Protocol.h）。
// 老服务端仍可能推上来，故保留这些用例，确认它被当作越界值归到"离线"。
static const int kRetiredPresence = 4;

TEST_CASE("TRAY1 登录前托盘菜单 = 打开主面板 / 分隔 / 退出（3 行）", "[traymenu]")
{
    std::vector<TrayMenuEntry> items = BuildTrayMenuModel(kTrayBeforeLogin, false, kCurrentOnline, kConnOnline);
    REQUIRE(items.size() == 3);

    int funcs = 0;
    int seps  = 0;
    CountRows(items, funcs, seps);
    CHECK(funcs == 2);
    CHECK(seps == 1);

    CHECK(items[0].id == kTrayMenuOpenPanel);
    CHECK(::wcscmp(items[0].label, L"打开主面板") == 0);
    CHECK(items[1].isSeparator);
    CHECK(items[2].id == kTrayMenuExit);
    CHECK(::wcscmp(items[2].label, L"退出") == 0);
}

TEST_CASE("TRAY2 面板锁定态托盘菜单与登录前完全一致（3 行）", "[traymenu]")
{
    // muted 传 true 也不应影响这两种形态（静音项只在正常菜单出现）。
    std::vector<TrayMenuEntry> locked = BuildTrayMenuModel(kTrayLocked, true, kCurrentOnline, kConnOnline);
    std::vector<TrayMenuEntry> before = BuildTrayMenuModel(kTrayBeforeLogin, false, kCurrentOnline, kConnOnline);

    REQUIRE(locked.size() == 3);
    REQUIRE(locked.size() == before.size());

    for (size_t i = 0; i < locked.size(); ++i)
    {
        INFO("行 index = " << i);
        CHECK(locked[i].id == before[i].id);
        CHECK(locked[i].isSeparator == before[i].isSeparator);
        if (!locked[i].isSeparator)
        {
            CHECK(::wcscmp(locked[i].label, before[i].label) == 0);
        }
    }
}

// =====================================================================
// TRAY3 —— 登录后正常菜单：顺序严格对照 flamingo.rc "系统托盘菜单二"
// =====================================================================

TEST_CASE("TRAY3 正常托盘菜单顺序对照设计（11 行：8 功能 + 3 分隔）", "[traymenu]")
{
    // "锁定 Flamingo" 已移除；在线状态扩成四态；末尾追加"登出"（与"退出"同组，登出在上）。
    //   在线 / 离开 / 忙碌 / 离线 ── 关闭所有声音 ── 打开主面板 ── 登出 / 退出，共 11 行。
    std::vector<TrayMenuEntry> items = BuildTrayMenuModel(kTrayNormal, false, kCurrentOnline, kConnOnline);
    REQUIRE(items.size() == 11);

    int funcs = 0;
    int seps  = 0;
    CountRows(items, funcs, seps);
    CHECK(funcs == 8);
    CHECK(seps == 3);

    // 当前状态传的是"在线"，故第 1 行文字带"（当前）"后缀，其余三行是纯状态名。
    CHECK(items[0].id == kTrayMenuOnline);
    CHECK(::wcscmp(items[0].label, L"在线（当前）") == 0);
    CHECK(items[1].id == kTrayMenuAway);
    CHECK(::wcscmp(items[1].label, L"离开") == 0);
    CHECK(items[2].id == kTrayMenuBusy);
    CHECK(::wcscmp(items[2].label, L"忙碌") == 0);
    CHECK(items[3].id == kTrayMenuOffline);
    CHECK(::wcscmp(items[3].label, L"离线") == 0);

    CHECK(items[4].isSeparator);

    CHECK(items[5].id == kTrayMenuMute);
    CHECK(::wcscmp(items[5].label, L"关闭所有声音") == 0);

    CHECK(items[6].isSeparator);

    CHECK(items[7].id == kTrayMenuOpenPanel);
    CHECK(::wcscmp(items[7].label, L"打开主面板") == 0);

    CHECK(items[8].isSeparator);

    CHECK(items[9].id == kTrayMenuLogout);
    CHECK(::wcscmp(items[9].label, L"登出") == 0);

    CHECK(items[10].id == kTrayMenuExit);
    CHECK(::wcscmp(items[10].label, L"退出") == 0);

    // 四个在线状态项带状态色圆点（dotColor 非 0）；其余功能项 dotColor 为 0（默认单色）。
    CHECK(items[0].dotColor != 0);
    CHECK(items[1].dotColor != 0);
    CHECK(items[2].dotColor != 0);
    CHECK(items[3].dotColor != 0);
    CHECK(items[5].dotColor == 0);   // 关闭所有声音：非状态项
}

// =====================================================================
// TRAY4 —— 静音项的文字与图标都随 muted 在 关闭/开启 之间切换
// =====================================================================

TEST_CASE("TRAY4 静音项文字与图标随 muted 切换；所有功能项都带图标", "[traymenu]")
{
    // 已静音：静音项（在四个状态项 + 1 分隔之后，位于第 6 行，index 5）显示"开启所有
    // 声音"+kIconMute（点一下恢复声音）。
    std::vector<TrayMenuEntry> on = BuildTrayMenuModel(kTrayNormal, true, kCurrentOnline, kConnOnline);
    REQUIRE(on.size() == 11);
    CHECK(on[5].id == kTrayMenuMute);
    CHECK(::wcscmp(on[5].label, L"开启所有声音") == 0);
    CHECK(on[5].iconKind == kIconMute);

    // 未静音：同一项显示"关闭所有声音"+kIconSound（普通喇叭，点一下转静音）。
    std::vector<TrayMenuEntry> off = BuildTrayMenuModel(kTrayNormal, false, kCurrentOnline, kConnOnline);
    REQUIRE(off.size() == 11);
    CHECK(off[5].id == kTrayMenuMute);
    CHECK(::wcscmp(off[5].label, L"关闭所有声音") == 0);
    CHECK(off[5].iconKind == kIconSound);

    // 所有功能项（含静音项）都带图标，没有 iconKind < 0 的功能项。
    for (size_t i = 0; i < on.size(); ++i)
    {
        if (on[i].isSeparator)
        {
            continue;
        }
        INFO("功能项 index = " << i);
        CHECK(on[i].iconKind >= 0);
    }
}

// =====================================================================
// TRAY5 —— 各形态功能项 id 非 0 且互不重复；分隔条无文本
// =====================================================================

TEST_CASE("TRAY5 功能项 id 非 0 且不重复，分隔条无文本", "[traymenu]")
{
    const TrayMenuKind kinds[] = { kTrayBeforeLogin, kTrayNormal, kTrayLocked };

    for (int k = 0; k < 3; ++k)
    {
        INFO("菜单形态 kind = " << (int)kinds[k]);
        std::vector<TrayMenuEntry> items = BuildTrayMenuModel(kinds[k], false, kCurrentOnline, kConnOnline);

        std::vector<UINT> ids;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].isSeparator)
            {
                CHECK(items[i].label == NULL);   // 分隔条无文本
                continue;
            }
            INFO("功能项 index = " << i);
            CHECK(items[i].id != 0);             // 0 是 TrackPopup 的"未选中"返回值，不能占用
            CHECK(items[i].label != NULL);
            ids.push_back(items[i].id);
        }

        std::sort(ids.begin(), ids.end());
        CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    }
}

// =====================================================================
// TRAY6 / TRAY7 —— 登出项：仅登录后出现，且与"退出"在 id 与图标上都能区分
// =====================================================================

TEST_CASE("TRAY6 登出项只在登录后出现，未登录 / 锁定形态都没有", "[traymenu][logout]")
{
    // 未登录时没有可登出的会话，菜单里出现"登出"会让人误以为已登录。
    const TrayMenuKind kindsWithout[] = { kTrayBeforeLogin, kTrayLocked };
    for (int k = 0; k < 2; ++k)
    {
        INFO("菜单形态 kind = " << (int)kindsWithout[k]);
        std::vector<TrayMenuEntry> items =
            BuildTrayMenuModel(kindsWithout[k], false, kCurrentOnline, kConnOnline);
        for (size_t i = 0; i < items.size(); ++i)
        {
            CHECK(items[i].id != kTrayMenuLogout);
        }
    }

    // 登录后正常形态必须有且只有一项登出。
    std::vector<TrayMenuEntry> normal = BuildTrayMenuModel(kTrayNormal, false, kCurrentOnline, kConnOnline);
    int logoutCount = 0;
    for (size_t i = 0; i < normal.size(); ++i)
    {
        if (!normal[i].isSeparator && normal[i].id == kTrayMenuLogout)
        {
            ++logoutCount;
        }
    }
    CHECK(logoutCount == 1);
}

TEST_CASE("TRAY7 登出与退出相邻，但命令 id 与图标都不相同", "[traymenu][logout]")
{
    // 两项紧挨着且语义相近（一个回登录界面、一个关掉程序），必须在 id 与图标两个
    // 维度上都能区分，否则既容易点错、分发也会串。
    std::vector<TrayMenuEntry> items = BuildTrayMenuModel(kTrayNormal, false, kCurrentOnline, kConnOnline);

    int logoutIdx = -1;
    int exitIdx   = -1;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].isSeparator)
        {
            continue;
        }
        if (items[i].id == kTrayMenuLogout)
        {
            logoutIdx = (int)i;
        }
        if (items[i].id == kTrayMenuExit)
        {
            exitIdx = (int)i;
        }
    }
    REQUIRE(logoutIdx >= 0);
    REQUIRE(exitIdx >= 0);

    CHECK(exitIdx == logoutIdx + 1);                              // 登出紧挨在退出上方
    CHECK(kTrayMenuLogout != kTrayMenuExit);                      // 命令 id 不同
    CHECK(items[logoutIdx].iconKind == kIconLogout);
    CHECK(items[exitIdx].iconKind == kIconExit);
    CHECK(items[logoutIdx].iconKind != items[exitIdx].iconKind);  // 图标不同
    CHECK(items[logoutIdx].dotColor == 0);                        // 非在线状态项，用默认单色图标
}

// =====================================================================
// TRAY8 ~ TRAY12 —— 当前所处的在线状态在菜单里被标出（图标 + 文字双通道）
//
// 这几条覆盖的是本次要修的问题：四个状态项原本长得一模一样，用户看不出自己
// 当前处在哪一个。
// =====================================================================

TEST_CASE("TRAY8 四种在线状态各自被标为当前项，且每份模型只标一项", "[traymenu][presence]")
{
    const int  presences[4] = { presence_online, presence_away, presence_busy, presence_offline };
    const UINT expectIds[4] = { kTrayMenuOnline, kTrayMenuAway, kTrayMenuBusy, kTrayMenuOffline };

    for (int p = 0; p < 4; ++p)
    {
        INFO("当前状态 presence = " << presences[p]);
        std::vector<TrayMenuEntry> items =
            BuildTrayMenuModel(kTrayNormal, false, presences[p], kConnOnline);

        // 整份模型里（含静音 / 打开主面板 / 登出 / 退出等非状态项与分隔条）
        // 有且仅有一项被标为当前，且必须是对应的那个状态项。
        int currentCount = 0;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].isCurrent)
            {
                ++currentCount;
                CHECK(items[i].id == expectIds[p]);
            }
        }
        CHECK(currentCount == 1);
    }
}

TEST_CASE("TRAY9 四个状态项各用各自状态的徽标字形，且四者两两不同", "[traymenu][presence]")
{
    // 当前状态取"离开"（第 2 行），便于同时验证前后两侧的行都没被误标。
    std::vector<TrayMenuEntry> items = BuildTrayMenuModel(kTrayNormal, false, presence_away, kConnOnline);
    REQUIRE(items.size() == 11);

    // 2026-08-12 状态图标改版后，图标认的是"本行是哪个状态"，不再随 isCurrent 变化；
    // "当前"由文字后缀 + 渲染期叠加的外环表达（见 TRAY10）。
    CHECK(items[0].iconKind == kIconPresenceOnline);
    CHECK(items[1].iconKind == kIconPresenceAway);
    CHECK(items[2].iconKind == kIconPresenceBusy);
    CHECK(items[3].iconKind == kIconPresenceOffline);

    // 本次改版的核心保证：四种字形两两不同 —— 即便四个圆点被渲染成同一种颜色
    // （灰度打印、色觉障碍），用户仍能靠形状认出谁是谁。
    for (int i = 0; i < 4; ++i)
    {
        for (int j = i + 1; j < 4; ++j)
        {
            INFO("比较状态项 " << i << " 与 " << j);
            CHECK(items[i].iconKind != items[j].iconKind);
        }
    }

    // 换图标不能把配色换掉：四行的圆点色仍与状态菜单 / 胶囊同源。
    const int presences[4] = { presence_online, presence_away, presence_busy, presence_offline };
    for (int i = 0; i < 4; ++i)
    {
        INFO("状态项 index = " << i);
        CHECK(items[i].dotColor == GetPresenceVisual(presences[i]).dotColor);
        // presence 字段是渲染期取徽标字形 / 判当前项的依据，必须逐行对上。
        CHECK(items[i].presence == presences[i]);
    }
}

TEST_CASE("TRAY10 当前项文字带\"（当前）\"后缀，其余项是纯状态名", "[traymenu][presence]")
{
    std::vector<TrayMenuEntry> items = BuildTrayMenuModel(kTrayNormal, false, presence_busy, kConnOnline);
    REQUIRE(items.size() == 11);

    CHECK(::wcscmp(items[0].label, L"在线") == 0);
    CHECK(::wcscmp(items[1].label, L"离开") == 0);
    CHECK(::wcscmp(items[2].label, L"忙碌（当前）") == 0);
    CHECK(::wcscmp(items[3].label, L"离线") == 0);

    // 非状态项的文字不受当前状态影响。
    CHECK(::wcscmp(items[5].label, L"关闭所有声音") == 0);
}

TEST_CASE("TRAY11 越界取值退化为\"离线\"项被标记", "[traymenu][presence]")
{
    // 约定：未知状态一律归到"离线"，与 GetPresenceVisual 的兜底口径一致 —— 否则会
    // 出现"胶囊显示离线、菜单里却没有任何一项被标记"的自相矛盾。
    std::vector<TrayMenuEntry> retired =
        BuildTrayMenuModel(kTrayNormal, false, kRetiredPresence, kConnOnline);
    REQUIRE(retired.size() == 11);
    CHECK(retired[3].isCurrent);
    CHECK(retired[3].iconKind == kIconPresenceOffline);
    CHECK(::wcscmp(retired[3].label, L"离线（当前）") == 0);

    std::vector<TrayMenuEntry> unknown = BuildTrayMenuModel(kTrayNormal, false, 999, kConnOnline);
    REQUIRE(unknown.size() == 11);
    CHECK(unknown[3].isCurrent);
    CHECK(!unknown[0].isCurrent);
}

TEST_CASE("TRAY12 未登录 / 锁定形态不受当前状态影响，也没有任何当前项", "[traymenu][presence]")
{
    const TrayMenuKind kinds[] = { kTrayBeforeLogin, kTrayLocked };

    for (int k = 0; k < 2; ++k)
    {
        INFO("菜单形态 kind = " << (int)kinds[k]);
        std::vector<TrayMenuEntry> a = BuildTrayMenuModel(kinds[k], false, presence_online, kConnOnline);
        std::vector<TrayMenuEntry> b = BuildTrayMenuModel(kinds[k], false, presence_busy, kConnOnline);
        REQUIRE(a.size() == b.size());

        for (size_t i = 0; i < a.size(); ++i)
        {
            INFO("行 index = " << i);
            CHECK(a[i].id == b[i].id);
            CHECK(a[i].iconKind == b[i].iconKind);
            CHECK(!a[i].isCurrent);
            CHECK(!b[i].isCurrent);
        }
    }
}

// =====================================================================
// TRAY13 ~ TRAY17 —— 托盘菜单与主面板状态胶囊同走连接态规则
//
// 这一组覆盖的是本次要修的问题：主面板胶囊按连接态显示"正在连接"时，托盘右键菜单
// 里"在线"那一行仍标着"（当前）"，而且四个状态项照样可点 —— 点了只是把改状态的帧
// 发进一条断掉的连接。判定统一收在 OnlineCapability（GetConnectionBadgePresence /
// GetConnectionMenuLabel / IsActionAllowedInState），本组用例锁的就是"菜单确实照它
// 的结果画"。
// =====================================================================

namespace {

// 四个状态项在模型里的固定行号（在线 / 离开 / 忙碌 / 离线），本组用例反复要用。
const int kRowOnline  = 0;
const int kRowAway    = 1;
const int kRowBusy    = 2;
const int kRowOffline = 3;

// 三种连接态与四种在线状态，供组合矩阵遍历。
const int kAllConnStates[3] = { (int)kConnUiOnline, (int)kConnUiConnecting, (int)kConnUiOffline };
const int kAllPresences[4]  =
{
    presence_online, presence_away, presence_busy, presence_offline
};

} // anonymous namespace

TEST_CASE("TRAY13 连接就绪时四个状态项可点，当前项仍按用户选的状态标出",
          "[traymenu][presence][conn]")
{
    // 连接就绪这一路的表现不能因为引入连接态就被改掉：当前项照旧是用户自己选的
    // 那个状态，四项都可点。
    const UINT expectIds[4] = { kTrayMenuOnline, kTrayMenuAway, kTrayMenuBusy, kTrayMenuOffline };

    for (int p = 0; p < 4; ++p)
    {
        INFO("presence = " << kAllPresences[p]);
        std::vector<TrayMenuEntry> items =
            BuildTrayMenuModel(kTrayNormal, false, kAllPresences[p], (int)kConnUiOnline);
        REQUIRE(items.size() == 11);

        for (int r = kRowOnline; r <= kRowOffline; ++r)
        {
            INFO("状态项行号 = " << r);
            CHECK(items[r].enabled);
        }

        int currentCount = 0;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].isCurrent)
            {
                ++currentCount;
                CHECK(items[i].id == expectIds[p]);
            }
        }
        CHECK(currentCount == 1);
    }
}

TEST_CASE("TRAY14 重连中：当前项落在\"离线\"行并显示\"正在连接（当前）\"",
          "[traymenu][presence][conn]")
{
    // 本次缺陷的核心用例。用户选的仍是在线 / 离开 / 忙碌，但连接没就绪，菜单里
    // 不能再有任何一行写着"在线（当前）"—— 那与主面板胶囊上的"正在连接"自相矛盾。
    for (int p = 0; p < 4; ++p)
    {
        INFO("presence = " << kAllPresences[p]);
        std::vector<TrayMenuEntry> items =
            BuildTrayMenuModel(kTrayNormal, false, kAllPresences[p], (int)kConnUiConnecting);
        REQUIRE(items.size() == 11);

        // 被标记的是"离线"那一行（与胶囊徽标同走 GetConnectionBadgePresence）。
        CHECK(!items[kRowOnline].isCurrent);
        CHECK(!items[kRowAway].isCurrent);
        CHECK(!items[kRowBusy].isCurrent);
        CHECK(items[kRowOffline].isCurrent);

        // 该行的文字按连接态取，与胶囊逐字一致。
        CHECK(::wcscmp(items[kRowOffline].label, L"正在连接（当前）") == 0);

        // 其余三行保持纯状态名，不带任何"（当前）"后缀。
        CHECK(::wcscmp(items[kRowOnline].label, L"在线") == 0);
        CHECK(::wcscmp(items[kRowAway].label,   L"离开") == 0);
        CHECK(::wcscmp(items[kRowBusy].label,   L"忙碌") == 0);
    }
}

TEST_CASE("TRAY15 用户主动离线：当前项是\"离线（当前）\"，不是\"正在连接\"",
          "[traymenu][presence][conn]")
{
    // 主动离线时用户并没有在等着连上，文字必须是"离线"而不是"正在连接"。
    std::vector<TrayMenuEntry> items =
        BuildTrayMenuModel(kTrayNormal, false, presence_offline, (int)kConnUiOffline);
    REQUIRE(items.size() == 11);

    CHECK(items[kRowOffline].isCurrent);
    CHECK(::wcscmp(items[kRowOffline].label, L"离线（当前）") == 0);

    // 越界的连接态按"离线"兜底，绝不能落回"在线（当前）"。
    std::vector<TrayMenuEntry> bad =
        BuildTrayMenuModel(kTrayNormal, false, presence_online, 999);
    REQUIRE(bad.size() == 11);
    CHECK(bad[kRowOffline].isCurrent);
    CHECK(::wcscmp(bad[kRowOffline].label, L"离线（当前）") == 0);
    CHECK(!bad[kRowOnline].isCurrent);
}

TEST_CASE("TRAY16 连接未就绪时四个状态项灰显，其余功能项照常可点",
          "[traymenu][presence][conn]")
{
    // 改状态要向服务端发一帧，连接未就绪时发不出去；静音 / 打开主面板 / 登出 / 退出
    // 都是本地动作，任何时候都不该被连带禁掉。
    const int kNotReady[3] = { (int)kConnUiConnecting, (int)kConnUiOffline, 999 };

    for (int s = 0; s < 3; ++s)
    {
        for (int p = 0; p < 4; ++p)
        {
            INFO("connUiState = " << kNotReady[s] << ", presence = " << kAllPresences[p]);
            std::vector<TrayMenuEntry> items =
                BuildTrayMenuModel(kTrayNormal, false, kAllPresences[p], kNotReady[s]);
            REQUIRE(items.size() == 11);

            for (size_t i = 0; i < items.size(); ++i)
            {
                if (items[i].isSeparator)
                {
                    continue;
                }
                INFO("功能项 index = " << i);
                // dotColor != 0 是"这一行是在线状态项"的判据（见 TrayMenuEntry 注释）。
                if (items[i].dotColor != 0)
                {
                    CHECK(!items[i].enabled);
                }
                else
                {
                    CHECK(items[i].enabled);
                }
            }
        }
    }
}

TEST_CASE("TRAY17 三种连接态 × 四个在线状态：当前项恒为一项，且文字与胶囊同源",
          "[traymenu][presence][conn]")
{
    // 这条是防回归用的：菜单的当前项文字将来只要有谁被单独改动、与 OnlineCapability
    // 的口径分叉了，本用例立刻失败。
    for (int s = 0; s < 3; ++s)
    {
        for (int p = 0; p < 4; ++p)
        {
            const int connUiState = kAllConnStates[s];
            const int presence    = kAllPresences[p];
            INFO("connUiState = " << connUiState << ", presence = " << presence);

            std::vector<TrayMenuEntry> items =
                BuildTrayMenuModel(kTrayNormal, false, presence, connUiState);
            REQUIRE(items.size() == 11);

            // 有且仅有一项被标为当前，且必须落在 GetConnectionBadgePresence 指定的那一行。
            const int expectPresence = GetConnectionBadgePresence(presence, connUiState);
            int currentCount = 0;
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (!items[i].isCurrent)
                {
                    continue;
                }
                ++currentCount;
                CHECK(items[i].presence == expectPresence);
                // 文字必须逐字等于 OnlineCapability 给出的那一串，菜单不得自己拼。
                CHECK(::wcscmp(items[i].label,
                               GetConnectionMenuLabel(presence, connUiState)) == 0);
            }
            CHECK(currentCount == 1);

            // 四项能不能点，与门禁层的答案必须一致。
            const bool bExpectEnabled =
                IsActionAllowedInState(connUiState, kActionChangePresence);
            for (int r = kRowOnline; r <= kRowOffline; ++r)
            {
                INFO("状态项行号 = " << r);
                CHECK(items[r].enabled == bExpectEnabled);
            }
        }
    }
}
