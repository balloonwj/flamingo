/**
 *  MainPanelStatusMenuTests.cpp
 *
 *  主面板"状态胶囊"弹出菜单（在线状态选择器）纯数据模型的测试：
 *    · BuildStatusMenuModel() 产出的行模型 —— 恰好 4 项、顺序为 在线/离开/勿扰/离线、
 *      命令 id 非 0 且互不重复、各项 presence 对应正确，并<u>不含"隐身"</u>。
 *    · GetPresenceVisual() 的"在线状态 → 文字 + 圆点色"映射，以及与菜单行 dotColor 的一致性。
 *    · "当前所处状态"的标记 —— isCurrent / iconKind / label 后缀三者一致、每份模型至多标
 *      一项、未知取值的兜底（归到"离线"），以及登录窗专用的三项模型
 *      BuildLoginStatusMenuModel()（不含"离线"）。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-05
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <algorithm>
#include <vector>
#include <cwchar>          // wcscmp —— 比对菜单项中文文本

#include "../BalloonMain/MainPanelStatusMenu.h"
#include "../BalloonMain/MainPanelIcons.h"   // IconKind（断言当前项的图标类型）
#include "../net/Protocol.h"                 // presence_type（断言各项对应的在线状态值）

using namespace balloonmain;

// 只关心菜单结构、不关心当前项标记的既有用例统一传"在线"作当前状态；此时被标记的
// 是第 1 行（在线），其余各行保持纯状态名。
namespace {

const int kCurrentOnline = presence_online;

} // anonymous namespace

// =====================================================================
// STM. 状态菜单行模型契约（对照 design/主面板状态菜单.png，去掉"隐身"）
// =====================================================================

TEST_CASE("STM1 状态菜单恰好 4 项：在线/离开/勿扰/离线", "[statusmenu]")
{
    std::vector<StatusMenuEntry> items = BuildStatusMenuModel(kCurrentOnline);
    REQUIRE(items.size() == 4);

    // 当前状态传的是"在线"，故第 1 行文字带"（当前）"后缀，其余三行是纯状态名。
    CHECK(items[0].id == kStatusMenuOnline);
    CHECK(::wcscmp(items[0].label, L"在线（当前）") == 0);
    CHECK(items[0].presence == presence_online);

    CHECK(items[1].id == kStatusMenuAway);
    CHECK(::wcscmp(items[1].label, L"离开") == 0);
    CHECK(items[1].presence == presence_away);

    CHECK(items[2].id == kStatusMenuBusy);
    CHECK(::wcscmp(items[2].label, L"忙碌") == 0);
    CHECK(items[2].presence == presence_busy);

    CHECK(items[3].id == kStatusMenuOffline);
    CHECK(::wcscmp(items[3].label, L"离线") == 0);
    CHECK(items[3].presence == presence_offline);
}

TEST_CASE("STM2 菜单不含\"隐身\"（presence_invisible 不出现）", "[statusmenu]")
{
    std::vector<StatusMenuEntry> items = BuildStatusMenuModel(kCurrentOnline);
    for (size_t i = 0; i < items.size(); ++i)
    {
        INFO("行 index = " << i);
        CHECK(items[i].presence != presence_invisible);
        CHECK(::wcscmp(items[i].label, L"隐身") != 0);
    }
}

TEST_CASE("STM3 菜单命令 id 非 0 且互不重复", "[statusmenu]")
{
    std::vector<StatusMenuEntry> items = BuildStatusMenuModel(kCurrentOnline);

    std::vector<UINT> ids;
    for (size_t i = 0; i < items.size(); ++i)
    {
        INFO("行 index = " << i);
        CHECK(items[i].id != 0);   // 0 是 TrackPopup 的"未选中"返回值，不能占用
        ids.push_back(items[i].id);
    }
    REQUIRE(ids.size() == 4);

    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
}

TEST_CASE("STM4 \"离线\"项的菜单 id 不等于 presence_offline(0)", "[statusmenu]")
{
    // presence_offline 恰好是 0，与 TrackPopup 的"未选中"返回值冲突，故"离线"项的
    // 菜单 id 必须另立非 0 值，真正的 presence 单独放在 entry.presence 里。
    std::vector<StatusMenuEntry> items = BuildStatusMenuModel(kCurrentOnline);
    const StatusMenuEntry& offline = items[3];
    CHECK(offline.presence == presence_offline);   // presence 仍是 0
    CHECK(offline.presence == 0);
    CHECK(offline.id != 0);                         // 但菜单 id 非 0
}

// =====================================================================
// STM.VISUAL. GetPresenceVisual 映射契约
// =====================================================================

TEST_CASE("STM5 GetPresenceVisual 四态映射正确", "[statusmenu][visual]")
{
    CHECK(::wcscmp(GetPresenceVisual(presence_online).text,  L"在线") == 0);
    CHECK(::wcscmp(GetPresenceVisual(presence_away).text,    L"离开") == 0);
    CHECK(::wcscmp(GetPresenceVisual(presence_busy).text,    L"忙碌") == 0);
    CHECK(::wcscmp(GetPresenceVisual(presence_offline).text, L"离线") == 0);
}

TEST_CASE("STM6 GetPresenceVisual 圆点色与菜单行 dotColor 一致", "[statusmenu][visual]")
{
    // 胶囊与菜单共用同一份配色，二者必须逐项相等。
    std::vector<StatusMenuEntry> items = BuildStatusMenuModel(kCurrentOnline);
    for (size_t i = 0; i < items.size(); ++i)
    {
        INFO("行 index = " << i << " presence = " << items[i].presence);
        CHECK(GetPresenceVisual(items[i].presence).dotColor == items[i].dotColor);
    }
}

TEST_CASE("STM7 GetPresenceVisual 兜底：隐身/未知值退化为\"离线\"灰点", "[statusmenu][visual]")
{
    PresenceVisual offlineV = GetPresenceVisual(presence_offline);

    // 隐身（本菜单不可选）与越界值都退化到"离线"的文字与颜色。
    PresenceVisual invisibleV = GetPresenceVisual(presence_invisible);
    CHECK(::wcscmp(invisibleV.text, L"离线") == 0);
    CHECK(invisibleV.dotColor == offlineV.dotColor);

    PresenceVisual unknownV = GetPresenceVisual(999);
    CHECK(::wcscmp(unknownV.text, L"离线") == 0);
    CHECK(unknownV.dotColor == offlineV.dotColor);
}

// =====================================================================
// STM.CURRENT. "当前所处状态"的标记契约
//
// 覆盖本次要修的问题：菜单各项原本长得一模一样，用户看不出自己当前处在哪一个。
// 标记走图标 + 文字双通道（DuiMenu 的勾选标记与左侧图标互斥，圆点必须保留，
// 故用不了勾选标记）。
// =====================================================================

TEST_CASE("STM8 四种状态各自被标为当前项，且每份模型只标一项", "[statusmenu][current]")
{
    const int  presences[4] = { presence_online, presence_away, presence_busy, presence_offline };
    const UINT expectIds[4] = { kStatusMenuOnline, kStatusMenuAway, kStatusMenuBusy, kStatusMenuOffline };

    for (int p = 0; p < 4; ++p)
    {
        INFO("当前状态 presence = " << presences[p]);
        std::vector<StatusMenuEntry> items = BuildStatusMenuModel(presences[p]);
        REQUIRE(items.size() == 4);

        int currentCount = 0;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].isCurrent)
            {
                ++currentCount;
                CHECK(items[i].id == expectIds[p]);
                CHECK(items[i].presence == presences[p]);
            }
        }
        CHECK(currentCount == 1);
    }
}

TEST_CASE("STM9 当前项 isCurrent / iconKind / label 后缀三者一致", "[statusmenu][current]")
{
    // 三种表达必须同步：只要 isCurrent 为真，图标就得是 kIconStatusCurrent、文字就得
    // 带"（当前）"后缀；反之亦然。任一处漏改都会让菜单自相矛盾。
    const int presences[4] = { presence_online, presence_away, presence_busy, presence_offline };

    for (int p = 0; p < 4; ++p)
    {
        INFO("当前状态 presence = " << presences[p]);
        std::vector<StatusMenuEntry> items = BuildStatusMenuModel(presences[p]);

        for (size_t i = 0; i < items.size(); ++i)
        {
            INFO("行 index = " << i);
            const StatusMenuEntry& e = items[i];

            CHECK(e.iconKind == (e.isCurrent ? kIconStatusCurrent : kIconOnline));
            CHECK(::wcscmp(e.label, GetPresenceMenuLabel(e.presence, e.isCurrent)) == 0);

            // 换图标 / 加后缀都不能把配色改掉：仍与胶囊同源。
            CHECK(e.dotColor == GetPresenceVisual(e.presence).dotColor);
        }
    }
}

TEST_CASE("STM10 NormalizePresenceForMenu：四态原样返回，其余归到\"离线\"", "[statusmenu][current]")
{
    CHECK(NormalizePresenceForMenu(presence_online)  == presence_online);
    CHECK(NormalizePresenceForMenu(presence_away)    == presence_away);
    CHECK(NormalizePresenceForMenu(presence_busy)    == presence_busy);
    CHECK(NormalizePresenceForMenu(presence_offline) == presence_offline);

    // 隐身与越界值统一归到"离线"，与 GetPresenceVisual 的兜底口径一致。
    CHECK(NormalizePresenceForMenu(presence_invisible) == presence_offline);
    CHECK(NormalizePresenceForMenu(999)                == presence_offline);
    CHECK(NormalizePresenceForMenu(-1)                 == presence_offline);
}

TEST_CASE("STM11 隐身 / 越界取值下，被标记的是\"离线\"项", "[statusmenu][current]")
{
    std::vector<StatusMenuEntry> invisible = BuildStatusMenuModel(presence_invisible);
    REQUIRE(invisible.size() == 4);
    CHECK(invisible[3].isCurrent);                                    // 第 4 行即"离线"
    CHECK(::wcscmp(invisible[3].label, L"离线（当前）") == 0);
    CHECK(!invisible[0].isCurrent);

    std::vector<StatusMenuEntry> unknown = BuildStatusMenuModel(999);
    REQUIRE(unknown.size() == 4);
    CHECK(unknown[3].isCurrent);
}

TEST_CASE("STM12 GetPresenceMenuLabel：非当前项是纯状态名，当前项带后缀", "[statusmenu][current]")
{
    // 非当前项的文字必须与胶囊文字逐字相同（同一份"状态 → 文字"映射）。
    CHECK(::wcscmp(GetPresenceMenuLabel(presence_online,  false), L"在线") == 0);
    CHECK(::wcscmp(GetPresenceMenuLabel(presence_away,    false), L"离开") == 0);
    CHECK(::wcscmp(GetPresenceMenuLabel(presence_busy,    false), L"忙碌") == 0);
    CHECK(::wcscmp(GetPresenceMenuLabel(presence_offline, false), L"离线") == 0);

    CHECK(::wcscmp(GetPresenceMenuLabel(presence_online,  true), L"在线（当前）") == 0);
    CHECK(::wcscmp(GetPresenceMenuLabel(presence_away,    true), L"离开（当前）") == 0);
    CHECK(::wcscmp(GetPresenceMenuLabel(presence_busy,    true), L"忙碌（当前）") == 0);
    CHECK(::wcscmp(GetPresenceMenuLabel(presence_offline, true), L"离线（当前）") == 0);

    // 未知取值同样走"离线"兜底。
    CHECK(::wcscmp(GetPresenceMenuLabel(999, false), L"离线") == 0);
    CHECK(::wcscmp(GetPresenceMenuLabel(999, true),  L"离线（当前）") == 0);
}

// =====================================================================
// STM.LOGIN. 登录窗状态菜单（三项，不含"离线"）
// =====================================================================

TEST_CASE("STM13 登录窗状态菜单恰好 3 项：在线/离开/忙碌，不含离线", "[statusmenu][login]")
{
    std::vector<StatusMenuEntry> items = BuildLoginStatusMenuModel(presence_online);
    REQUIRE(items.size() == 3);

    CHECK(items[0].id == kStatusMenuOnline);
    CHECK(items[0].presence == presence_online);
    CHECK(items[1].id == kStatusMenuAway);
    CHECK(items[1].presence == presence_away);
    CHECK(items[2].id == kStatusMenuBusy);
    CHECK(items[2].presence == presence_busy);

    // 登录时选"离线"没有意义，本菜单不给这一项。
    for (size_t i = 0; i < items.size(); ++i)
    {
        INFO("行 index = " << i);
        CHECK(items[i].presence != presence_offline);
        CHECK(items[i].id != kStatusMenuOffline);
        CHECK(items[i].id != 0);   // 0 是 TrackPopup 的"未选中"返回值，不能占用
    }
}

TEST_CASE("STM14 登录窗菜单同样标出当前项；当前状态是离线时一项都不标", "[statusmenu][login]")
{
    std::vector<StatusMenuEntry> away = BuildLoginStatusMenuModel(presence_away);
    REQUIRE(away.size() == 3);
    CHECK(!away[0].isCurrent);
    CHECK(away[1].isCurrent);
    CHECK(away[1].iconKind == kIconStatusCurrent);
    CHECK(::wcscmp(away[1].label, L"离开（当前）") == 0);
    CHECK(!away[2].isCurrent);

    // 本菜单没有"离线"行可标，故三行都不是当前项 —— 这是预期行为，不是漏标。
    std::vector<StatusMenuEntry> offline = BuildLoginStatusMenuModel(presence_offline);
    REQUIRE(offline.size() == 3);
    for (size_t i = 0; i < offline.size(); ++i)
    {
        INFO("行 index = " << i);
        CHECK(!offline[i].isCurrent);
        CHECK(offline[i].iconKind == kIconOnline);
    }
}
