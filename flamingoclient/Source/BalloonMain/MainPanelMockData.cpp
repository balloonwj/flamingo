/**
 *  MainPanelMockData.cpp
 *
 *  主面板 mock 数据的定义。数据逐字取自 design 的 data.js。
 *
 *  本 .cpp 故意<u>不</u> include Flamingo 的 stdafx.h —— 它要被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用，故自带 ATL/WTL 前置
 *  依赖。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-21
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>

#include "MainPanelMockData.h"
#include "MainPanelStatusMenu.h"    // GetPresenceVisual（状态 → 配色的唯一出处）
#include "../net/Protocol.h"        // presence_type（mock 状态换算的目标编号）

namespace balloonmain {

namespace {

// 按字段构造一个 MockContact（精简各处 mock 数据的填充代码）。
MockContact MakeContact(LPCTSTR id, LPCTSTR name, LPCTSTR remark,
                        LPCTSTR account, LPCTSTR avatarText, COLORREF avatarColor,
                        LPCTSTR signature, LPCTSTR title, int status, bool isService)
{
    MockContact c;
    c.id          = id;
    c.name        = name;
    c.remark      = remark;
    c.account     = account;
    c.avatarText  = avatarText;
    c.avatarColor = avatarColor;
    c.signature   = signature;
    c.title       = title;
    c.status      = status;
    c.isService   = isService;
    return c;
}

// 按字段构造一个 MockGroup。
MockGroup MakeGroup(LPCTSTR id, LPCTSTR name, LPCTSTR avatarText,
                    COLORREF avatarColor, int memberCount)
{
    MockGroup g;
    g.id          = id;
    g.name        = name;
    g.avatarText  = avatarText;
    g.avatarColor = avatarColor;
    g.memberCount = memberCount;
    return g;
}

// 按字段构造一个 MockConversation。
MockConversation MakeConversation(LPCTSTR title, LPCTSTR avatarText,
                                  COLORREF avatarColor, LPCTSTR lastMsg,
                                  LPCTSTR time, int unread, bool isGroup, int status)
{
    MockConversation c;
    c.title       = title;
    c.avatarText  = avatarText;
    c.avatarColor = avatarColor;
    c.lastMsg     = lastMsg;
    c.time        = time;
    c.unread      = unread;
    c.isGroup     = isGroup;
    c.status      = status;
    return c;
}

} // anonymous namespace

const MockContact& MockMe()
{
    static MockContact me = MakeContact(
        _T("me"), _T("林知夏"), _T(""), _T("88234561"),
        _T("L"), RGB(0xFF, 0x7A, 0x45),
        _T("想做一件让自己骄傲的事"), _T("资深设计师"), kMockOnline, false);
    return me;
}

int MockUnreadTotal()
{
    // design data.js 里 CONVERSATIONS 各项 unread 之和：3 + 1 + 2 = 6。
    return 6;
}

const std::vector<MockContact>& MockFavorites()
{
    // design MY_FAVORITES = ['c1','c2','c4']。
    static std::vector<MockContact> favs;
    if (favs.empty())
    {
        favs.push_back(MakeContact(_T("c1"), _T("苏沐辰"), _T(""), _T("72100345"),
            _T("苏"), RGB(0xFF, 0x7A, 0x45),
            _T("今天也要好好生活"), _T("资深设计师"), kMockOnline, false));
        favs.push_back(MakeContact(_T("c2"), _T("江屿白"), _T("设计组组长"), _T("10923847"),
            _T("江"), RGB(0x3B, 0x82, 0xF6),
            _T("在出差路上…"), _T("设计组组长"), kMockAway, false));
        favs.push_back(MakeContact(_T("c4"), _T("许清欢"), _T("前同事"), _T("33019823"),
            _T("许"), RGB(0xA8, 0x55, 0xF7),
            _T("喜欢有风的日子"), _T("用户研究员"), kMockOnline, false));
    }
    return favs;
}

const std::vector<MockContact>& MockServiceAccounts()
{
    // design SERVICE_ACCOUNTS（s1..s7）。
    static std::vector<MockContact> svc;
    if (svc.empty())
    {
        svc.push_back(MakeContact(_T("s1"), _T("系统通知"), _T(""), _T("10000"),
            _T("🔔"), RGB(0xE0, 0xF2, 0xFE), _T(""), _T("官方"), kMockOnline, true));
        svc.push_back(MakeContact(_T("s2"), _T("客服小助手"), _T(""), _T("10001"),
            _T("💬"), RGB(0xDC, 0xFC, 0xE7), _T(""), _T("7×24 在线"), kMockOnline, true));
        svc.push_back(MakeContact(_T("s3"), _T("公司公告"), _T(""), _T("10002"),
            _T("📢"), RGB(0xFE, 0xF3, 0xC7), _T(""), _T("HR 发布"), kMockOnline, true));
        svc.push_back(MakeContact(_T("s4"), _T("邮件助手"), _T(""), _T("10003"),
            _T("📧"), RGB(0xE0, 0xE7, 0xFF), _T(""), _T("每日邮件聚合"), kMockOnline, true));
        svc.push_back(MakeContact(_T("s5"), _T("日历提醒"), _T(""), _T("10004"),
            _T("📅"), RGB(0xFC, 0xE7, 0xF3), _T(""), _T("会议日程"), kMockOnline, true));
        svc.push_back(MakeContact(_T("s6"), _T("文件传输助手"), _T(""), _T("10005"),
            _T("📁"), RGB(0xED, 0xE9, 0xFE), _T(""), _T("传输助手"), kMockOnline, true));
        svc.push_back(MakeContact(_T("s7"), _T("FlamingoIM 团队"), _T(""), _T("10006"),
            _T("🦩"), RGB(0xFF, 0xE4, 0xD6), _T(""), _T("官方账号"), kMockOnline, true));
    }
    return svc;
}

const std::vector<MockGroup>& MockGroups()
{
    // design GROUPS（g1..g5）。
    static std::vector<MockGroup> groups;
    if (groups.empty())
    {
        groups.push_back(MakeGroup(_T("g1"), _T("设计组 · 周会"),
            _T("设"), RGB(0xFF, 0x7A, 0x45), 12));
        groups.push_back(MakeGroup(_T("g2"), _T("FlamingoIM 项目组"),
            _T("F"), RGB(0x3B, 0x82, 0xF6), 28));
        groups.push_back(MakeGroup(_T("g3"), _T("高中老友"),
            _T("友"), RGB(0x10, 0xB9, 0x81), 8));
        groups.push_back(MakeGroup(_T("g4"), _T("读书会"),
            _T("读"), RGB(0xA8, 0x55, 0xF7), 42));
        groups.push_back(MakeGroup(_T("g5"), _T("家庭群"),
            _T("家"), RGB(0xEF, 0x44, 0x44), 5));
    }
    return groups;
}

const std::vector<MockConversation>& MockConversations()
{
    // design CONVERSATIONS，会话引用的联系人 / 群显示字段已就地解析。
    static std::vector<MockConversation> convs;
    if (convs.empty())
    {
        convs.push_back(MakeConversation(_T("FlamingoIM 项目组"), _T("F"),
            RGB(0x3B, 0x82, 0xF6), _T("@林知夏 新版本的设计稿麻烦确认下"),
            _T("14:32"), 3, true, kMockOffline));
        convs.push_back(MakeConversation(_T("苏沐辰"), _T("苏"),
            RGB(0xFF, 0x7A, 0x45), _T("好的好的，等会发你"),
            _T("14:21"), 0, false, kMockOnline));
        convs.push_back(MakeConversation(_T("设计组组长"), _T("江"),
            RGB(0x3B, 0x82, 0xF6), _T("[图片]"),
            _T("13:58"), 1, false, kMockAway));
        convs.push_back(MakeConversation(_T("设计组 · 周会"), _T("设"),
            RGB(0xFF, 0x7A, 0x45), _T("宋微之: 我先来分享一下…"),
            _T("12:40"), 0, true, kMockOffline));
        convs.push_back(MakeConversation(_T("前同事"), _T("许"),
            RGB(0xA8, 0x55, 0xF7), _T("周末一起去逛展吗？"),
            _T("11:12"), 2, false, kMockOnline));
        convs.push_back(MakeConversation(_T("谢知行"), _T("谢"),
            RGB(0x0E, 0xA5, 0xE9), _T("[文件] PRD-v3.2.docx"),
            _T("昨天"), 0, false, kMockOnline));
        convs.push_back(MakeConversation(_T("高中老友"), _T("友"),
            RGB(0x10, 0xB9, 0x81), _T("苏沐辰: 收到[玫瑰]"),
            _T("昨天"), 0, true, kMockOffline));
        convs.push_back(MakeConversation(_T("妈妈"), _T("妈"),
            RGB(0xEF, 0x44, 0x44), _T("记得按时吃饭，下周回来吗？"),
            _T("昨天"), 0, false, kMockOnline));
        convs.push_back(MakeConversation(_T("温野"), _T("温"),
            RGB(0x10, 0xB9, 0x81), _T("明天再说"),
            _T("周一"), 0, false, kMockBusy));
        convs.push_back(MakeConversation(_T("读书会"), _T("读"),
            RGB(0xA8, 0x55, 0xF7), _T("许清欢: 这周读到第八章"),
            _T("周一"), 0, true, kMockOffline));
        convs.push_back(MakeConversation(_T("宋微之"), _T("宋"),
            RGB(0x14, 0xB8, 0xA6), _T("代码 review 通过啦"),
            _T("2/18"), 0, false, kMockOnline));
    }
    return convs;
}

int MockContactsTotalCount()
{
    // design ORG_TREE countMembers：ME + 14 名联系人（不含服务号）= 15。
    return 15;
}

int MockPresenceFromStatus(int status)
{
    switch (status)
    {
    case kMockOnline:
        return presence_online;
    case kMockAway:
        return presence_away;
    case kMockBusy:
        return presence_busy;
    default:
        // 离线、以及一切越界取值（含已作废的旧状态 3）一律归到离线 ——
        // 与 NormalizePresenceForMenu / GetPresenceBadgeIconKind 的兜底口径一致。
        return presence_offline;
    }
}

COLORREF MockStatusColor(int status)
{
    // 颜色不在这里另写一张表：mock 与真实数据必须长得一模一样，否则调试入口里核对过的
    // 观感到了登录后又变一副样子。改版前这里自留过一份 #22C55E / #F59E0B / #EF4444 /
    // #94A3B8，正式配色一调就对不上了。
    return GetPresenceVisual(MockPresenceFromStatus(status)).dotColor;
}

} // namespace balloonmain
