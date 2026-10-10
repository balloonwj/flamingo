/**
 *  MainPanelSystemMenuTests.cpp
 *
 *  主面板系统菜单（左下角汉堡按钮弹出菜单）的测试：
 *    · 验证纯函数 BuildSystemMenuModel() 产出的行模型符合设计 —— 条目数、
 *      顺序、文本、分隔线与分组标题的位置、命令 id 的唯一性与非 0 约束、图标 / 可用态。
 *    · 验证 balloonui 新增的 DuiMenu::MeasureSize() —— 弹出前取尺寸用于"向上
 *      弹"定位，高度须随行数线性、分隔条比文本行更矮、空菜单为 {0,0}。
 *
 *  2026-10-05 起菜单按组加标题（docs/屏幕共享与录制方案.md 第 11.6 节 F8，布局经用户确认）：
 *      账号（个人信息、修改密码）─ 发起（发起语音通话、发起视频通话、发起屏幕共享、发起录制）
 *      ─ 消息与工具（聊天记录管理、公告与通知、消息盒子、备忘与提醒、文件共享、录制管理）
 *      ─ 设置（系统设置、网络设置）─ 登出与关于（登出、关于 FlamingoIM）
 *  组与组之间是一条分隔线加一行分组标题；模块不可用的项不出现，「发起」组一项都没有时整组不出现。
 *  用例 SYSH1~4 覆盖分组本身，原有 SYS / SYSC 用例按新布局改写。
 *  2026-10-07 起「关于」放在最后一行、「登出」在它之前（组标题随之改为「登出与关于」），「账号」组末尾在锁屏模块
 *  可用时多一项「锁屏」，「修改密码」改用钥匙图标；用例 SYSL1~SYSL3 覆盖这几处。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <algorithm>
#include <set>
#include <vector>
#include <cwchar>          // wcscmp —— 比对菜单项中文文本

#include "../UI/BalloonMain/MainPanelSystemMenu.h"
#include "../UI/BalloonMain/MainPanelIcons.h"   // IconKind（断言各项图标）
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"

using namespace balloonmain;

namespace {

//期望的一行：类型、命令 id（只对功能项有意义）、文字（分隔线为 NULL）
enum SysRowKind
{
    kRowFunc,     // 功能项
    kRowSep,      // 分隔线
    kRowHeader    // 分组标题
};

struct SysRow
{
    SysRowKind     kind;    // 行类型
    UINT           id;      // 功能项的命令 id；分隔线与标题取 0
    const wchar_t* label;   // 功能项与标题的文字；分隔线取 NULL
};

//功能项：既不是分隔线也不是分组标题
bool IsSysFunc(const SystemMenuEntry& e)
{
    return !e.isSeparator && !e.isHeader;
}

//某个命令 id 的功能项所在的行；没有时返回 items.size()
size_t IndexOfSysId(const std::vector<SystemMenuEntry>& items, UINT id)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (IsSysFunc(items[i]) && items[i].id == id)
            return i;
    }
    return items.size();
}

//某个文字的分组标题所在的行；没有时返回 items.size()
size_t IndexOfSysHeader(const std::vector<SystemMenuEntry>& items, const wchar_t* label)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].isHeader && items[i].label != NULL && ::wcscmp(items[i].label, label) == 0)
            return i;
    }
    return items.size();
}

//逐行比对行模型与期望的行序列
void CheckSysRows(const std::vector<SystemMenuEntry>& items, const SysRow* expected, size_t count)
{
    REQUIRE(items.size() == count);
    for (size_t i = 0; i < count; ++i)
    {
        INFO("行 index = " << i);
        const SystemMenuEntry& e = items[i];
        switch (expected[i].kind)
        {
        //分隔线：没有文字
        case kRowSep:
            CHECK(e.isSeparator);
            CHECK_FALSE(e.isHeader);
            break;

        //分组标题：文字相同
        case kRowHeader:
            CHECK(e.isHeader);
            CHECK_FALSE(e.isSeparator);
            REQUIRE(e.label != NULL);
            CHECK(::wcscmp(e.label, expected[i].label) == 0);
            break;

        //功能项：命令 id 与文字相同
        default:
            CHECK(IsSysFunc(e));
            CHECK(e.id == expected[i].id);
            REQUIRE(e.label != NULL);
            CHECK(::wcscmp(e.label, expected[i].label) == 0);
            break;
        }
    }
}

} // namespace

// =====================================================================
// SYS. 系统菜单行模型契约
// =====================================================================

TEST_CASE("SYS1 默认系统菜单共 10 个功能项 + 3 条分隔线 + 4 个分组标题 = 17 行", "[sysmenu]")
{
    // 默认参数：没有音视频、屏幕共享、录屏、文件共享模块，所以没有「发起」组，也没有「文件共享」「录制管理」。
    // 功能项、分隔线、标题与总行数是一起变的，改动时几个数都要跟上。
    std::vector<SystemMenuEntry> items = BuildSystemMenuModel();
    REQUIRE(items.size() == 17);

    int seps = 0;
    int headers = 0;
    int funcs = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].isSeparator)
        {
            ++seps;
        }
        else if (items[i].isHeader)
        {
            ++headers;
        }
        else
        {
            ++funcs;
        }
    }
    CHECK(seps == 3);
    CHECK(headers == 4);
    CHECK(funcs == 10);
    // 一行不能既是分隔线又是标题
    for (size_t i = 0; i < items.size(); ++i)
    {
        CHECK_FALSE((items[i].isSeparator && items[i].isHeader));
    }
}

TEST_CASE("SYS2 默认系统菜单的行顺序、文本、分隔线与分组标题位置", "[sysmenu]")
{
    const SysRow expected[] = {
        { kRowHeader, 0, L"账号" },
        { kRowFunc, kSysMenuProfile, L"个人信息" },
        { kRowFunc, kSysMenuChangePwd, L"修改密码" },
        { kRowSep, 0, NULL },
        { kRowHeader, 0, L"消息与工具" },
        { kRowFunc, kSysMenuHistoryManager, L"聊天记录管理" },
        { kRowFunc, kSysMenuAnnounce, L"公告与通知" },
        { kRowFunc, kSysMenuMsgBox, L"消息盒子" },
        { kRowFunc, kSysMenuMemo, L"备忘与提醒" },
        { kRowSep, 0, NULL },
        { kRowHeader, 0, L"设置" },
        { kRowFunc, kSysMenuSettings, L"系统设置" },
        { kRowFunc, kSysMenuNetSettings, L"网络设置" },
        { kRowSep, 0, NULL },
        { kRowHeader, 0, L"登出与关于" },
        { kRowFunc, kSysMenuLogout, L"登出" },
        { kRowFunc, kSysMenuAbout, L"关于 FlamingoIM" },
    };
    CheckSysRows(BuildSystemMenuModel(), expected, sizeof(expected) / sizeof(expected[0]));
}

TEST_CASE("SYS3 功能项命令 id 非 0 且互不重复", "[sysmenu]")
{
    // 所有模块都可用、「录制管理」也出现时功能项最多
    std::vector<SystemMenuEntry> items =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, true, true);

    std::vector<UINT> ids;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!IsSysFunc(items[i]))
        {
            continue;
        }
        INFO("功能项 index = " << i);
        CHECK(items[i].id != 0);   // 0 是 TrackPopup 的"未选中"返回值，不能占用
        ids.push_back(items[i].id);
    }
    // 默认 10 项 + 发起语音通话、发起视频通话、发起屏幕共享、发起录制、文件共享、录制管理
    REQUIRE(ids.size() == 16);

    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
}

TEST_CASE("SYS5 「关于」排在最后一行，「登出」紧挨在它之前，与「设置」组之间隔着分隔线与「登出与关于」标题", "[sysmenu][logout]")
{
    // 2026-10-07 起「关于」放在整张菜单的最后一行，「登出」在它之前（用户要求两者互换，组标题随之改为「登出与关于」）。
    // 登出是"结束当前会话"类操作，与上面的设置类操作之间仍用分隔线与分组标题隔开，降低误点概率。
    std::vector<SystemMenuEntry> items = BuildSystemMenuModel();
    REQUIRE(items.size() >= 4);
    const size_t n = items.size();

    const SystemMenuEntry& about = items[n - 1];
    CHECK(IsSysFunc(about));
    CHECK(about.id == kSysMenuAbout);
    CHECK(about.enabled);

    const SystemMenuEntry& logout = items[n - 2];
    CHECK(IsSysFunc(logout));
    CHECK(logout.id == kSysMenuLogout);
    CHECK(::wcscmp(logout.label, L"登出") == 0);
    CHECK(logout.enabled);

    REQUIRE(items[n - 3].isHeader);
    CHECK(::wcscmp(items[n - 3].label, L"登出与关于") == 0);
    CHECK(items[n - 4].isSeparator);
}

TEST_CASE("SYS6 登出项的图标与其它各项都不相同", "[sysmenu][logout]")
{
    // 登出用独立图标(kIconLogout)：与托盘菜单里紧挨着的"退出"(kIconExit)在视觉上必须
    // 能区分，也不能和菜单内其它项撞图标。
    std::vector<SystemMenuEntry> items =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, true, true);

    int logoutIcon = -1;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (IsSysFunc(items[i]) && items[i].id == kSysMenuLogout)
        {
            logoutIcon = items[i].iconKind;
        }
    }
    REQUIRE(logoutIcon == kIconLogout);
    CHECK(logoutIcon != kIconExit);

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!IsSysFunc(items[i]) || items[i].id == kSysMenuLogout)
        {
            continue;
        }
        INFO("功能项 index = " << i);
        CHECK(items[i].iconKind != logoutIcon);
    }
}

TEST_CASE("SYS7 文件共享模块已加载时「备忘与提醒」之后多一项「文件共享」", "[sysmenu][fileshare-entry]")
{
    const std::vector<SystemMenuEntry> without = BuildSystemMenuModel(false);
    const std::vector<SystemMenuEntry> with = BuildSystemMenuModel(true);
    REQUIRE(with.size() == without.size() + 1);
    //「备忘与提醒」紧跟在「消息盒子」之后，「文件共享」排在它后面，同属「消息与工具」组
    const size_t memoAt = IndexOfSysId(with, kSysMenuMemo);
    REQUIRE(memoAt + 1 < with.size());
    const SystemMenuEntry& share = with[memoAt + 1];
    CHECK(share.id == kSysMenuFileShare);
    CHECK(IsSysFunc(share));
    CHECK(std::wstring(share.label) == L"文件共享");
    CHECK(share.iconKind == kIconShare);
    CHECK(share.enabled);
    CHECK(IndexOfSysId(without, kSysMenuFileShare) == without.size());
    //命令 id 仍互不重复
    std::set<UINT> ids;
    for (size_t i = 0; i < with.size(); ++i)
    {
        if (IsSysFunc(with[i]))
            CHECK(ids.insert(with[i].id).second);
    }
}

TEST_CASE("SYSC1 音视频模块已加载时出现「发起」组：「发起语音通话」「发起视频通话」",
          "[sysmenu][callux]")
{
    const std::wstring aboutLabel = L"关于 FlamingoIM";
    const std::vector<SystemMenuEntry> without = BuildSystemMenuModel(false, kRecordMenuHidden, aboutLabel.c_str());
    const std::vector<SystemMenuEntry> with =
        BuildSystemMenuModel(false, kRecordMenuHidden, aboutLabel.c_str(), true);
    //多「发起」标题、两项与一条分隔线
    REQUIRE(with.size() == without.size() + 4);
    //「修改密码」之后先是分隔线，再是「发起」标题与两项，之后是分隔线与「消息与工具」标题
    const size_t pwdAt = IndexOfSysId(with, kSysMenuChangePwd);
    REQUIRE(pwdAt + 6 < with.size());
    CHECK(with[pwdAt + 1].isSeparator);
    REQUIRE(with[pwdAt + 2].isHeader);
    CHECK(::wcscmp(with[pwdAt + 2].label, L"发起") == 0);
    const SystemMenuEntry& voice = with[pwdAt + 3];
    const SystemMenuEntry& video = with[pwdAt + 4];
    CHECK(voice.id == kSysMenuStartVoiceCall);
    CHECK(std::wstring(voice.label) == L"发起语音通话");
    CHECK(voice.iconKind == kIconPhone);
    CHECK(voice.enabled);
    CHECK(video.id == kSysMenuStartVideoCall);
    CHECK(std::wstring(video.label) == L"发起视频通话");
    CHECK(video.iconKind == kIconVideoCall);
    CHECK(video.enabled);
    CHECK(with[pwdAt + 5].isSeparator);
    REQUIRE(with[pwdAt + 6].isHeader);
    CHECK(::wcscmp(with[pwdAt + 6].label, L"消息与工具") == 0);
    //模块没加载时没有这两项；命令 id 仍互不重复、都不为 0
    CHECK(IndexOfSysId(without, kSysMenuStartVoiceCall) == without.size());
    CHECK(IndexOfSysId(without, kSysMenuStartVideoCall) == without.size());
    std::set<UINT> ids;
    for (size_t i = 0; i < with.size(); ++i)
    {
        if (IsSysFunc(with[i]))
        {
            CHECK(with[i].id != 0u);
            CHECK(ids.insert(with[i].id).second);
        }
    }
}

TEST_CASE("SYS4 功能项都带文本 + 图标 + 可用；分隔条无文本无图标", "[sysmenu]")
{
    std::vector<SystemMenuEntry> items =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, true, true);

    for (size_t i = 0; i < items.size(); ++i)
    {
        INFO("行 index = " << i);
        if (items[i].isSeparator)
        {
            CHECK(items[i].label == NULL);
            CHECK(items[i].iconKind < 0);
        }
        else if (IsSysFunc(items[i]))
        {
            CHECK(items[i].label != NULL);
            CHECK(items[i].iconKind >= 0);
            CHECK(items[i].enabled);   // 当前各功能项均可用（无灰显项）
        }
    }
}

// =====================================================================
// MS. DuiMenu::MeasureSize() —— "弹出前取尺寸"契约
// =====================================================================

TEST_CASE("MS1 空菜单 MeasureSize 返回 {0,0}", "[sysmenu][measuresize]")
{
    balloonwjui::DuiMenu menu;
    SIZE sz = menu.MeasureSize();
    CHECK(sz.cx == 0);
    CHECK(sz.cy == 0);
}

TEST_CASE("MS2 菜单高度随行数线性，分隔条比文本行矮", "[sysmenu][measuresize]")
{
    // 用"增量法"反推单行高 / 单分隔高，避免在测试里硬编码 balloonui 内部常量。
    balloonwjui::DuiMenu m1;
    m1.AppendItem(1, L"甲");

    balloonwjui::DuiMenu m2;
    m2.AppendItem(1, L"甲");
    m2.AppendItem(2, L"乙");

    balloonwjui::DuiMenu m3;
    m3.AppendItem(1, L"甲");
    m3.AppendSeparator();
    m3.AppendItem(2, L"乙");

    long rowH = m2.MeasureSize().cy - m1.MeasureSize().cy;   // 一个文本行的高度
    long sepH = m3.MeasureSize().cy - m2.MeasureSize().cy;   // 一条分隔线的高度

    CHECK(rowH > 0);
    CHECK(sepH > 0);
    CHECK(sepH < rowH);                              // 分隔条比文本行矮
    CHECK(m1.MeasureSize().cy == rowH);              // 单项菜单高 = 一个文本行
    CHECK(m3.MeasureSize().cy == 2 * rowH + sepH);   // 2 文本 + 1 分隔
    CHECK(m1.MeasureSize().cx > 0);                  // 宽度恒为正
}

TEST_CASE("SYS8 录屏模块可用时「发起」组里多一项「发起录制」，录制中显示「停止录制」", "[sysmenu][record]")
{
    const std::vector<SystemMenuEntry> without = BuildSystemMenuModel(false, kRecordMenuHidden);
    const std::vector<SystemMenuEntry> start = BuildSystemMenuModel(false, kRecordMenuStart);
    const std::vector<SystemMenuEntry> stop = BuildSystemMenuModel(true, kRecordMenuStop, NULL, true, true);
    CHECK(IndexOfSysId(without, kSysMenuRecord) == without.size());
    //没有通话与屏幕共享时，「发起」组只有这一项：多出「发起」标题、这一项与一条分隔线
    REQUIRE(start.size() == without.size() + 3);
    const size_t recordAt = IndexOfSysId(start, kSysMenuRecord);
    REQUIRE(recordAt >= 1);
    REQUIRE(recordAt + 1 < start.size());
    REQUIRE(start[recordAt - 1].isHeader);
    CHECK(::wcscmp(start[recordAt - 1].label, L"发起") == 0);
    CHECK(start[recordAt + 1].isSeparator);
    CHECK(std::wstring(start[recordAt].label) == L"发起录制");
    CHECK(start[recordAt].iconKind == kIconRecord);
    CHECK(start[recordAt].enabled);
    //录制中：文字为「停止录制」，排在「发起屏幕共享」之后、「发起」组的最后
    const size_t stopAt = IndexOfSysId(stop, kSysMenuRecord);
    REQUIRE(stopAt < stop.size());
    CHECK(stopAt == IndexOfSysId(stop, kSysMenuScreenShare) + 1);
    REQUIRE(stopAt + 1 < stop.size());
    CHECK(stop[stopAt + 1].isSeparator);
    CHECK(std::wstring(stop[stopAt].label) == L"停止录制");
    std::set<UINT> ids;
    for (size_t i = 0; i < stop.size(); ++i)
    {
        if (IsSysFunc(stop[i]))
            CHECK(ids.insert(stop[i].id).second);
    }
}

TEST_CASE("SYS9 「关于」项的文字由调用方按产品名称传入，缺省为「关于 FlamingoIM」", "[sysmenu][brand]")
{
    //产品名称来自 flamingo.ini 的 [ui] apptitle，主面板把拼好的「关于 <产品名称>」传进来
    const wchar_t* const customLabel = L"关于 火烈鸟办公";
    std::vector<SystemMenuEntry> custom = BuildSystemMenuModel(false, kRecordMenuHidden, customLabel);
    const size_t customAt = IndexOfSysId(custom, kSysMenuAbout);
    REQUIRE(customAt < custom.size());
    CHECK(::wcscmp(custom[customAt].label, L"关于 火烈鸟办公") == 0);

    std::vector<SystemMenuEntry> def = BuildSystemMenuModel();
    const size_t defAt = IndexOfSysId(def, kSysMenuAbout);
    REQUIRE(defAt < def.size());
    CHECK(::wcscmp(def[defAt].label, L"关于 FlamingoIM") == 0);

    //传空指针时同样回退到缺省文字，不会让菜单项文字为空
    std::vector<SystemMenuEntry> nullLabel = BuildSystemMenuModel(false, kRecordMenuHidden, NULL);
    const size_t nullAt = IndexOfSysId(nullLabel, kSysMenuAbout);
    REQUIRE(nullAt < nullLabel.size());
    CHECK(::wcscmp(nullLabel[nullAt].label, L"关于 FlamingoIM") == 0);
}

TEST_CASE("SYS10 「公告与通知」之后紧跟「消息盒子」，命令 id 13、抽屉柜图标", "[sysmenu][msgbox]")
{
    //与会话列表里「消息盒子」那一行、头部消息盒子按钮同一去处，图标也与那一行的头像相同（2026-10-04 起）
    CHECK((UINT)kSysMenuMsgBox == 13u);
    std::vector<SystemMenuEntry> items = BuildSystemMenuModel(true, kRecordMenuStart);
    const size_t announceAt = IndexOfSysId(items, kSysMenuAnnounce);
    REQUIRE(announceAt + 1 < items.size());
    const SystemMenuEntry& box = items[announceAt + 1];
    CHECK(IsSysFunc(box));
    CHECK(box.id == kSysMenuMsgBox);
    CHECK(::wcscmp(box.label, L"消息盒子") == 0);
    CHECK(box.iconKind == kIconDrawer);
    CHECK(box.enabled);
}

// =====================================================================
// SYSH. 分组标题（2026-10-05 起，F8）
// =====================================================================

TEST_CASE("SYSH1 所有模块都可用时系统菜单的完整行序", "[sysmenu][sysheader]")
{
    const SysRow expected[] = {
        { kRowHeader, 0, L"账号" },
        { kRowFunc, kSysMenuProfile, L"个人信息" },
        { kRowFunc, kSysMenuChangePwd, L"修改密码" },
        { kRowSep, 0, NULL },
        { kRowHeader, 0, L"发起" },
        { kRowFunc, kSysMenuStartVoiceCall, L"发起语音通话" },
        { kRowFunc, kSysMenuStartVideoCall, L"发起视频通话" },
        { kRowFunc, kSysMenuScreenShare, L"发起屏幕共享" },
        { kRowFunc, kSysMenuRecord, L"发起录制" },
        { kRowSep, 0, NULL },
        { kRowHeader, 0, L"消息与工具" },
        { kRowFunc, kSysMenuHistoryManager, L"聊天记录管理" },
        { kRowFunc, kSysMenuAnnounce, L"公告与通知" },
        { kRowFunc, kSysMenuMsgBox, L"消息盒子" },
        { kRowFunc, kSysMenuMemo, L"备忘与提醒" },
        { kRowFunc, kSysMenuFileShare, L"文件共享" },
        { kRowFunc, kSysMenuRecordManager, L"录制管理" },
        { kRowSep, 0, NULL },
        { kRowHeader, 0, L"设置" },
        { kRowFunc, kSysMenuSettings, L"系统设置" },
        { kRowFunc, kSysMenuNetSettings, L"网络设置" },
        { kRowSep, 0, NULL },
        { kRowHeader, 0, L"登出与关于" },
        { kRowFunc, kSysMenuLogout, L"登出" },
        { kRowFunc, kSysMenuAbout, L"关于 FlamingoIM" },
    };
    CheckSysRows(BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, true, true), expected,
                 sizeof(expected) / sizeof(expected[0]));
}

TEST_CASE("SYSH2 分组标题不可用、id 为 0、没有图标，后面紧跟功能项；第一行是「账号」标题，每条分隔线后面都是标题",
          "[sysmenu][sysheader]")
{
    //几种模块组合都检查一遍：默认、全部可用、只有部分模块
    std::vector<std::vector<SystemMenuEntry> > menus;
    menus.push_back(BuildSystemMenuModel());
    menus.push_back(BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, true, true));
    menus.push_back(BuildSystemMenuModel(false, kRecordMenuStop, NULL, false, true, false));
    for (size_t m = 0; m < menus.size(); ++m)
    {
        INFO("菜单组合 = " << m);
        const std::vector<SystemMenuEntry>& items = menus[m];
        REQUIRE(!items.empty());
        REQUIRE(items[0].isHeader);
        CHECK(::wcscmp(items[0].label, L"账号") == 0);
        for (size_t i = 0; i < items.size(); ++i)
        {
            INFO("行 index = " << i);
            if (items[i].isHeader)
            {
                CHECK(items[i].id == 0u);
                CHECK_FALSE(items[i].enabled);
                CHECK(items[i].iconKind < 0);
                REQUIRE(items[i].label != NULL);
                CHECK(items[i].label[0] != L'\0');
                REQUIRE(i + 1 < items.size());
                CHECK(IsSysFunc(items[i + 1]));
            }
            if (items[i].isSeparator)
            {
                REQUIRE(i + 1 < items.size());
                CHECK(items[i + 1].isHeader);
            }
        }
    }
}

TEST_CASE("SYSH3 「发起」组：通话、屏幕共享、录制都不可用时整组不出现，部分可用时只列可用的项", "[sysmenu][sysheader]")
{
    //都不可用：没有「发起」标题，也没有为它留下的分隔线（不会出现两条相邻的分隔线）
    const std::vector<SystemMenuEntry> none = BuildSystemMenuModel(true, kRecordMenuHidden, NULL, false, false, true);
    CHECK(IndexOfSysHeader(none, L"发起") == none.size());
    for (size_t i = 0; i + 1 < none.size(); ++i)
    {
        CHECK_FALSE((none[i].isSeparator && none[i + 1].isSeparator));
    }

    //只有屏幕共享：「发起」标题之后只有「发起屏幕共享」一项，接着是分隔线
    const std::vector<SystemMenuEntry> shareOnly =
        BuildSystemMenuModel(false, kRecordMenuHidden, NULL, false, true, false);
    const size_t headerAt = IndexOfSysHeader(shareOnly, L"发起");
    REQUIRE(headerAt + 2 < shareOnly.size());
    CHECK(shareOnly[headerAt + 1].id == kSysMenuScreenShare);
    CHECK(shareOnly[headerAt + 2].isSeparator);
    CHECK(IndexOfSysId(shareOnly, kSysMenuStartVoiceCall) == shareOnly.size());
    CHECK(IndexOfSysId(shareOnly, kSysMenuRecord) == shareOnly.size());

    //只有通话与录制：「发起语音通话」「发起视频通话」「发起录制」三项
    const std::vector<SystemMenuEntry> callRecord =
        BuildSystemMenuModel(false, kRecordMenuStart, NULL, true, false, false);
    const size_t crAt = IndexOfSysHeader(callRecord, L"发起");
    REQUIRE(crAt + 4 < callRecord.size());
    CHECK(callRecord[crAt + 1].id == kSysMenuStartVoiceCall);
    CHECK(callRecord[crAt + 2].id == kSysMenuStartVideoCall);
    CHECK(callRecord[crAt + 3].id == kSysMenuRecord);
    CHECK(callRecord[crAt + 4].isSeparator);
}

TEST_CASE("SYSH4 「录制管理」：命令 id 17，排在「消息与工具」组最后，参数为假时不出现", "[sysmenu][sysheader]")
{
    CHECK((UINT)kSysMenuRecordManager == 17u);

    //有「文件共享」时排在它后面，之后是分隔线
    const std::vector<SystemMenuEntry> withShare =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, false, false, true);
    const size_t at = IndexOfSysId(withShare, kSysMenuRecordManager);
    REQUIRE(at + 1 < withShare.size());
    CHECK(withShare[at - 1].id == kSysMenuFileShare);
    CHECK(withShare[at + 1].isSeparator);
    CHECK(::wcscmp(withShare[at].label, L"录制管理") == 0);
    CHECK(withShare[at].enabled);
    //图标与「发起录制」不同，也不与登出相同
    CHECK(withShare[at].iconKind == kIconRecordList);
    CHECK(withShare[at].iconKind != kIconRecord);
    CHECK(withShare[at].iconKind != kIconLogout);

    //没有「文件共享」时排在「备忘与提醒」后面
    const std::vector<SystemMenuEntry> noShare =
        BuildSystemMenuModel(false, kRecordMenuStart, NULL, false, false, true);
    const size_t noShareAt = IndexOfSysId(noShare, kSysMenuRecordManager);
    REQUIRE(noShareAt < noShare.size());
    CHECK(noShare[noShareAt - 1].id == kSysMenuMemo);

    //参数为假（F9 的窗口接好之前主面板传假）：不出现，其余各项不变
    const std::vector<SystemMenuEntry> hidden =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, false, false, false);
    CHECK(IndexOfSysId(hidden, kSysMenuRecordManager) == hidden.size());
    CHECK(hidden.size() + 1 == withShare.size());
}

TEST_CASE("SYSMD1 「Markdown 编辑器」：命令 id 19，紧跟「图片文字识别」，没有识别时紧跟「备忘与提醒」", "[sysmenu][mdentry]")
{
    CHECK((UINT)kSysMenuMarkdownEditor == 19u);

    //有「图片文字识别」时排在它后面
    const std::vector<SystemMenuEntry> withOcr =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, false, false, true, true, true);
    const size_t at = IndexOfSysId(withOcr, kSysMenuMarkdownEditor);
    REQUIRE(at < withOcr.size());
    REQUIRE(at > 0);
    CHECK(withOcr[at - 1].id == kSysMenuOcr);
    CHECK(::wcscmp(withOcr[at].label, L"Markdown 编辑器") == 0);
    CHECK(withOcr[at].iconKind == kIconMarkdown);
    CHECK(withOcr[at].enabled);

    //没有「图片文字识别」时排在「备忘与提醒」后面
    const std::vector<SystemMenuEntry> noOcr =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, false, false, true, false, true);
    const size_t noOcrAt = IndexOfSysId(noOcr, kSysMenuMarkdownEditor);
    REQUIRE(noOcrAt < noOcr.size());
    CHECK(noOcr[noOcrAt - 1].id == kSysMenuMemo);
}

TEST_CASE("SYSMD2 Markdown 模块不可用时不出现，其余各项不变", "[sysmenu][mdentry]")
{
    const std::vector<SystemMenuEntry> shown =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, true, true, true, true);
    const std::vector<SystemMenuEntry> hidden =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, true, true, true, false);
    CHECK(IndexOfSysId(hidden, kSysMenuMarkdownEditor) == hidden.size());
    REQUIRE(hidden.size() + 1 == shown.size());
    //去掉 Markdown 那一行后两份完全相同
    size_t j = 0;
    for (size_t i = 0; i < shown.size(); ++i)
    {
        if (!shown[i].isSeparator && shown[i].id == kSysMenuMarkdownEditor)
            continue;
        REQUIRE(j < hidden.size());
        CHECK(shown[i].id == hidden[j].id);
        CHECK(shown[i].isSeparator == hidden[j].isSeparator);
        ++j;
    }
}

// =====================================================================
// SYSL. 锁屏与修改密码图标（2026-10-07 起）
// =====================================================================

TEST_CASE("SYSL1 锁屏模块可用时「账号」组在「修改密码」之后多一项「锁屏」：命令 id 20、挂锁图标", "[sysmenu][lockentry]")
{
    CHECK((UINT)kSysMenuLockScreen == 20u);

    const std::vector<SystemMenuEntry> with =
        BuildSystemMenuModel(false, kRecordMenuHidden, NULL, false, false, false, false, false, true);
    const size_t at = IndexOfSysId(with, kSysMenuLockScreen);
    REQUIRE(at < with.size());
    REQUIRE(at > 0);
    CHECK(with[at - 1].id == kSysMenuChangePwd);
    CHECK(::wcscmp(with[at].label, L"锁屏") == 0);
    CHECK(with[at].iconKind == kIconLock);
    CHECK(with[at].enabled);
    //「账号」组到此结束，后面是分隔线
    REQUIRE(at + 1 < with.size());
    CHECK(with[at + 1].isSeparator);
}

TEST_CASE("SYSL2 锁屏模块不可用时不出现「锁屏」，其余各项不变", "[sysmenu][lockentry]")
{
    const std::vector<SystemMenuEntry> shown =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, true, true, true, true, true);
    const std::vector<SystemMenuEntry> hidden =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, true, true, true, true, false);
    CHECK(IndexOfSysId(hidden, kSysMenuLockScreen) == hidden.size());
    REQUIRE(hidden.size() + 1 == shown.size());
    //去掉「锁屏」那一行后两份完全相同
    size_t j = 0;
    for (size_t i = 0; i < shown.size(); ++i)
    {
        if (IsSysFunc(shown[i]) && shown[i].id == kSysMenuLockScreen)
            continue;
        REQUIRE(j < hidden.size());
        CHECK(shown[i].id == hidden[j].id);
        CHECK(shown[i].isSeparator == hidden[j].isSeparator);
        CHECK(shown[i].isHeader == hidden[j].isHeader);
        ++j;
    }
}

TEST_CASE("SYSL3 「修改密码」用钥匙图标，与「锁屏」的挂锁图标不同；全部模块可用时最后一行仍是「关于」", "[sysmenu][lockentry]")
{
    const std::vector<SystemMenuEntry> items =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, true, true, true, true, true);
    const size_t pwdAt = IndexOfSysId(items, kSysMenuChangePwd);
    const size_t lockAt = IndexOfSysId(items, kSysMenuLockScreen);
    REQUIRE(pwdAt < items.size());
    REQUIRE(lockAt < items.size());
    CHECK(items[pwdAt].iconKind == kIconKey);
    CHECK(items[pwdAt].iconKind != items[lockAt].iconKind);
    REQUIRE(!items.empty());
    CHECK(items.back().id == kSysMenuAbout);
    CHECK(items[items.size() - 2].id == kSysMenuLogout);
}
