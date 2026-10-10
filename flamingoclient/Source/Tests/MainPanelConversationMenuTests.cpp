/**
 *  MainPanelConversationMenuTests.cpp
 *
 *  会话列表右键菜单（MainPanelConversationMenu）的测试：验证纯函数
 *  BuildConversationMenuModel() 产出的行模型 —— 条目数、顺序、分隔条位置、
 *  命令 id 的唯一性与非 0 约束、随会话状态切换的文案（查看资料 / 查看群资料、
 *  置顶 / 取消置顶）、「消息提醒设置」二级菜单父项、未读为 0 时"标记已读"灰显。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <set>
#include <vector>
#include <cwchar>          // wcscmp —— 比对菜单项中文文本

#include "../UI/BalloonMain/MainPanelConversationMenu.h"
#include "../UI/BalloonMain/FavoritesModel.h"   // GetMenuLabel（与本模型的收藏项文案须一致）
#include "../UI/BalloonMain/NotifyModeMenu.h"   // GetNotifyModeMenuTitle（三处菜单共用的二级菜单父项文字）
#include "../net/Protocol.h"                    // enum conversation_notify_mode

using namespace balloonmain;

namespace {

// 造一个"单聊、未置顶、允许提醒、有 3 条未读"的常规状态，各用例按需改单个字段。
ConversationMenuState MakeState()
{
    ConversationMenuState st;
    st.m_isGroup    = false;
    st.m_pinned     = false;
    st.m_notifyMode = notify_mode_allow;
    st.m_unread     = 3;
    return st;
}

// 在行模型里找指定 id 的功能项；没有则返回一个全默认的项（m_id 为 0、m_label 为 NULL），
// 调用方先断言 m_id 再看别的字段。
//
// 这里刻意<u>返回值拷贝</u>而不是指向 items 内部的指针：调用点普遍写成
// FindEntry(BuildConversationMenuModel(st), id) 这种"就地取用临时 vector"的形式，
// 返回指针的话，整个表达式结束时临时 vector 就析构了，后面再解引用就是野指针（这个坑
// 已经踩过一次，CMENU4 当场 SIGSEGV）。
ConversationMenuEntry FindEntry(const std::vector<ConversationMenuEntry>& items, UINT id)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].m_isSeparator && items[i].m_id == id)
        {
            return items[i];
        }
    }
    return ConversationMenuEntry();
}

} // anonymous namespace

// =====================================================================
// CMENU1~CMENU2 —— 骨架：行序与分隔条位置
// =====================================================================

TEST_CASE("CMENU1 常规单聊会话的菜单行序与分隔条位置", "[convmenu]")
{
    std::vector<ConversationMenuEntry> items = BuildConversationMenuModel(MakeState());

    // 11 个功能项 + 5 条分隔条 = 16 行（MakeState 造的是单聊会话，故有「复制名字」）。
    // 2026-10-03 按统一的分组规则重排：打开 / 查看 / 标记与提醒 / 复制 / 清空 / 删除；
    // 同日「特别关注」「消息免打扰」两项合并成「消息提醒设置」二级菜单。
    REQUIRE(items.size() == 16);

    size_t i = 0;
    CHECK(items[i].m_id == kConvMenuOpen);          ++i;
    CHECK(items[i].m_id == kConvMenuMarkRead);      ++i;
    CHECK(items[i].m_isSeparator);                  ++i;
    CHECK(items[i].m_id == kConvMenuProfile);       ++i;
    CHECK(items[i].m_id == kConvMenuHistory);       ++i;
    CHECK(items[i].m_isSeparator);                  ++i;
    CHECK(items[i].m_id == kConvMenuFavorite);      ++i;
    CHECK(items[i].m_id == kConvMenuPin);           ++i;
    CHECK(items[i].m_id == kConvMenuNotifyMode);    ++i;
    CHECK(items[i].m_isSeparator);                  ++i;
    CHECK(items[i].m_id == kConvMenuCopyName);      ++i;
    CHECK(items[i].m_isSeparator);                  ++i;
    CHECK(items[i].m_id == kConvMenuClearHistory);  ++i;
    CHECK(items[i].m_isSeparator);                  ++i;
    CHECK(items[i].m_id == kConvMenuDelete);        ++i;
    CHECK(items[i].m_id == kConvMenuDeleteAll);     ++i;
    CHECK(i == items.size());

    CHECK(wcscmp(FindEntry(items, kConvMenuHistory).m_label, L"查看消息记录") == 0);
}

TEST_CASE("CMENU2 分隔条不带 id 与文本，功能项都有文本", "[convmenu]")
{
    std::vector<ConversationMenuEntry> items = BuildConversationMenuModel(MakeState());

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_isSeparator)
        {
            CHECK(items[i].m_id == 0);
            CHECK(items[i].m_label == NULL);
        }
        else
        {
            REQUIRE(items[i].m_label != NULL);
            CHECK(wcslen(items[i].m_label) > 0);
        }
    }
}

// =====================================================================
// CMENU3 —— id 约束：非 0 且互不重复
// =====================================================================

TEST_CASE("CMENU3 功能项 id 均非 0 且互不重复", "[convmenu]")
{
    // 0 是 DuiMenu::TrackPopup 的"未选中"返回值：功能项占用 0，用户按 ESC 关菜单
    // 就会被当成选中了那一项；id 重复则两项分不开，点 A 执行 B。
    std::vector<ConversationMenuEntry> items = BuildConversationMenuModel(MakeState());

    std::set<UINT> seen;
    int funcCount = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_isSeparator)
        {
            continue;
        }
        ++funcCount;
        CHECK(items[i].m_id != 0);
        CHECK(seen.count(items[i].m_id) == 0);
        seen.insert(items[i].m_id);
    }
    //单聊会话：10 个原有功能项 + 「复制名字」（2026-10-01）+ 「清空聊天记录」（2026-10-02），
    //2026-10-03「特别关注」「消息免打扰」合并成「消息提醒设置」一项
    CHECK(funcCount == 11);
}

TEST_CASE("CMENU3b 第一项就是「打开会话」，前面没有标题行", "[convmenu]")
{
    // 菜单顶部原先有一行会话名标题（禁用项），已经去掉 —— 现在弹出来第一眼看到的
    // 就是可点的功能项。标题若回归会顶在最前面，这条断言即时报警。
    std::vector<ConversationMenuEntry> items = BuildConversationMenuModel(MakeState());

    REQUIRE(!items.empty());
    CHECK_FALSE(items[0].m_isSeparator);
    CHECK(items[0].m_id == kConvMenuOpen);
    REQUIRE(items[0].m_label != NULL);
    CHECK(wcscmp(items[0].m_label, L"打开会话") == 0);
}

// =====================================================================
// CMENU4~CMENU6 —— 随会话状态切换的文案
// =====================================================================

TEST_CASE("CMENU4 群会话的资料项文案是「查看群资料」", "[convmenu]")
{
    ConversationMenuState single = MakeState();
    ConversationMenuEntry eSingle =
        FindEntry(BuildConversationMenuModel(single), kConvMenuProfile);
    REQUIRE(eSingle.m_id == kConvMenuProfile);
    REQUIRE(eSingle.m_label != NULL);
    CHECK(wcscmp(eSingle.m_label, L"查看资料") == 0);

    ConversationMenuState group = MakeState();
    group.m_isGroup = true;
    std::vector<ConversationMenuEntry> groupItems = BuildConversationMenuModel(group);
    ConversationMenuEntry eGroup = FindEntry(groupItems, kConvMenuProfile);
    REQUIRE(eGroup.m_id == kConvMenuProfile);
    REQUIRE(eGroup.m_label != NULL);
    CHECK(wcscmp(eGroup.m_label, L"查看群资料") == 0);

    // 群会话没有「复制名字」，换成「复制群号」「复制群名」两项，比单聊多一行（17 行，见 CMENU8）。
    CHECK(groupItems.size() == 17);
}

TEST_CASE("CMENU4b 收藏项文案随当前收藏状态取反", "[convmenu]")
{
    ConversationMenuState notFav = MakeState();
    ConversationMenuEntry eOff =
        FindEntry(BuildConversationMenuModel(notFav), kConvMenuFavorite);
    REQUIRE(eOff.m_id == kConvMenuFavorite);
    REQUIRE(eOff.m_label != NULL);
    CHECK(wcscmp(eOff.m_label, L"添加到收藏") == 0);

    ConversationMenuState fav = MakeState();
    fav.m_favorite = true;
    ConversationMenuEntry eOn =
        FindEntry(BuildConversationMenuModel(fav), kConvMenuFavorite);
    REQUIRE(eOn.m_id == kConvMenuFavorite);
    REQUIRE(eOn.m_label != NULL);
    CHECK(wcscmp(eOn.m_label, L"取消收藏") == 0);

    // 与 FavoritesModel::GetMenuLabel 必须是同一套文案 —— 联系人 / 群两个右键菜单
    // 走那个函数、会话菜单走本模型，两边不一致用户会以为是两个不同的功能。
    CHECK(wcscmp(eOff.m_label, balloonmain::FavoritesModel::GetMenuLabel(false)) == 0);
    CHECK(wcscmp(eOn.m_label, balloonmain::FavoritesModel::GetMenuLabel(true)) == 0);
}

TEST_CASE("CMENU4c 单聊与群聊会话都有「消息提醒设置」二级菜单父项，紧跟在置顶项之后", "[convmenu][notifymenu]")
{
    ConversationMenuState group = MakeState();
    group.m_isGroup = true;
    const ConversationMenuState states[] = { MakeState(), group };
    for (size_t k = 0; k < sizeof(states) / sizeof(states[0]); ++k)
    {
        INFO("isGroup = " << states[k].m_isGroup);
        const std::vector<ConversationMenuEntry> items = BuildConversationMenuModel(states[k]);
        size_t at = items.size();
        for (size_t i = 0; i < items.size(); ++i)
        {
            //只有这一项是二级菜单父项
            if (items[i].m_hasNotifySubMenu)
            {
                CHECK(at == items.size());
                at = i;
            }
        }
        REQUIRE(at < items.size());
        CHECK(items[at].m_id == kConvMenuNotifyMode);
        CHECK(items[at].m_enabled);
        REQUIRE(items[at].m_label != NULL);
        CHECK(wcscmp(items[at].m_label, balloonmain::GetNotifyModeMenuTitle()) == 0);
        CHECK(items[at - 1].m_id == kConvMenuPin);
        CHECK(items[at + 1].m_isSeparator);
    }
}

TEST_CASE("CMENU5 置顶项文案随当前置顶状态取反", "[convmenu]")
{
    ConversationMenuState notPinned = MakeState();
    ConversationMenuEntry eOff =
        FindEntry(BuildConversationMenuModel(notPinned), kConvMenuPin);
    REQUIRE(eOff.m_label != NULL);
    CHECK(wcscmp(eOff.m_label, L"置顶会话") == 0);

    ConversationMenuState pinned = MakeState();
    pinned.m_pinned = true;
    ConversationMenuEntry eOn =
        FindEntry(BuildConversationMenuModel(pinned), kConvMenuPin);
    REQUIRE(eOn.m_label != NULL);
    CHECK(wcscmp(eOn.m_label, L"取消置顶") == 0);
}

TEST_CASE("CMENU6 「消息提醒设置」父项的文字不随当前模式变化，当前模式只体现在子菜单的勾上", "[convmenu][notifymenu]")
{
    const int modes[] = { notify_mode_allow, notify_mode_silent, notify_mode_msgbox,
                          notify_mode_block, notify_mode_starred };
    for (size_t k = 0; k < sizeof(modes) / sizeof(modes[0]); ++k)
    {
        ConversationMenuState st = MakeState();
        st.m_isGroup    = true;
        st.m_notifyMode = modes[k];
        const ConversationMenuEntry e = FindEntry(BuildConversationMenuModel(st), kConvMenuNotifyMode);
        REQUIRE(e.m_label != NULL);
        CHECK(wcscmp(e.m_label, L"消息提醒设置") == 0);
    }
    //免打扰与特别关注两项已不存在：用字面量断言旧编号，防止日后被复用
    const std::vector<ConversationMenuEntry> items = BuildConversationMenuModel(MakeState());
    CHECK(FindEntry(items, 7).m_id == 0);
    CHECK(FindEntry(items, 11).m_id == 0);
}

// =====================================================================
// CMENU7 —— 未读为 0 时「标记已读」灰显
// =====================================================================

TEST_CASE("CMENU7 未读为 0 且没标记时第二项是标为未读，有未读时是标记已读，两者都可点", "[convmenu]")
{
    //2026-10-06 起（docs/会话标为未读方案.md）：原先未读为 0 时「标记已读」灰显，现在换成「标为未读」
    ConversationMenuState unreadZero = MakeState();
    unreadZero.m_unread = 0;
    std::vector<ConversationMenuEntry> items = BuildConversationMenuModel(unreadZero);
    REQUIRE(items.size() > 1);
    CHECK(items[1].m_id == kConvMenuMarkUnread);
    CHECK(FindEntry(items, kConvMenuMarkRead).m_id == 0);

    ConversationMenuState hasUnread = MakeState();
    hasUnread.m_unread = 1;
    ConversationMenuEntry eOn =
        FindEntry(BuildConversationMenuModel(hasUnread), kConvMenuMarkRead);
    REQUIRE(eOn.m_id == kConvMenuMarkRead);
    CHECK(eOn.m_enabled == true);

    // 任何状态下所有功能项都可点
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_isSeparator)
        {
            continue;
        }
        CHECK(items[i].m_enabled == true);
    }
}

// =====================================================================
// CMENU8 —— 复制项（2026-10-01）
// =====================================================================

TEST_CASE("CMENU8 复制项：单聊只有复制名字，群聊只有复制群号与复制群名，各自单独一组", "[convmenu][copymenu]")
{
    //单聊：「复制名字」单独一组，前后都是分隔条；没有群相关的复制项
    const std::vector<ConversationMenuEntry> single = BuildConversationMenuModel(MakeState());
    const ConversationMenuEntry copyName = FindEntry(single, kConvMenuCopyName);
    REQUIRE(copyName.m_id == kConvMenuCopyName);
    REQUIRE(copyName.m_label != NULL);
    CHECK(wcscmp(copyName.m_label, L"复制名字") == 0);
    CHECK(copyName.m_enabled);
    CHECK(FindEntry(single, kConvMenuCopyGroupNo).m_id == 0);
    CHECK(FindEntry(single, kConvMenuCopyGroupName).m_id == 0);

    //群聊：「标记与提醒」一组之后是复制群号、复制群名，前后都是分隔条；没有复制名字
    ConversationMenuState groupState = MakeState();
    groupState.m_isGroup = true;
    const std::vector<ConversationMenuEntry> group = BuildConversationMenuModel(groupState);
    REQUIRE(group.size() == 17);
    CHECK(group[8].m_id == kConvMenuNotifyMode);
    CHECK(group[9].m_isSeparator);
    CHECK(group[10].m_id == kConvMenuCopyGroupNo);
    CHECK(group[11].m_id == kConvMenuCopyGroupName);
    CHECK(group[12].m_isSeparator);
    REQUIRE(group[10].m_label != NULL);
    REQUIRE(group[11].m_label != NULL);
    CHECK(wcscmp(group[10].m_label, L"复制群号") == 0);
    CHECK(wcscmp(group[11].m_label, L"复制群名") == 0);
    CHECK(FindEntry(group, kConvMenuCopyName).m_id == 0);
}

TEST_CASE("CMENU9 清空聊天记录：单聊与群聊会话都有，单独一组排在两个删除项之前", "[convmenu][clearconv]")
{
    ConversationMenuState groupState = MakeState();
    groupState.m_isGroup = true;
    const ConversationMenuState states[] = { MakeState(), groupState };
    for (size_t k = 0; k < sizeof(states) / sizeof(states[0]); ++k)
    {
        INFO("isGroup = " << states[k].m_isGroup);
        const std::vector<ConversationMenuEntry> items = BuildConversationMenuModel(states[k]);
        size_t clearAt = items.size();
        size_t deleteAt = items.size();
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].m_id == kConvMenuClearHistory)
                clearAt = i;
            if (items[i].m_id == kConvMenuDelete)
                deleteAt = i;
        }
        REQUIRE(clearAt < items.size());
        //清空只影响本人的记录，删除会话则把会话从列表拿掉：两者之间隔一条分隔条
        CHECK(clearAt + 2 == deleteAt);
        CHECK(items[clearAt + 1].m_isSeparator);
        CHECK(items[clearAt].m_enabled);
        REQUIRE(items[clearAt].m_label != NULL);
        CHECK(wcscmp(items[clearAt].m_label, L"清空聊天记录") == 0);
        //前面是分隔条：与复制那一组分开
        CHECK(items[clearAt - 1].m_isSeparator);
    }
    CHECK(kConvMenuClearHistory != 0);
}

TEST_CASE("CMENU10 共享文件、共享屏幕：在标记已读之后，单聊写给TA、群聊写到本群，功能可用时才出现", "[convmenu][menushare]")
{
    //单聊、两项都可用：紧跟在「标记已读」之后，仍在第一组
    ConversationMenuState single = MakeState();
    single.m_canShareFile = true;
    single.m_canShareScreen = true;
    const std::vector<ConversationMenuEntry> items = BuildConversationMenuModel(single);
    REQUIRE(items.size() == 18);
    CHECK(items[0].m_id == kConvMenuOpen);
    CHECK(items[1].m_id == kConvMenuMarkRead);
    CHECK(items[2].m_id == kConvMenuShareFile);
    CHECK(items[3].m_id == kConvMenuShareScreen);
    CHECK(items[4].m_isSeparator);
    CHECK(std::wstring(FindEntry(items, kConvMenuShareFile).m_label) == L"共享文件给TA");
    CHECK(std::wstring(FindEntry(items, kConvMenuShareScreen).m_label) == L"共享屏幕给TA");
    CHECK(FindEntry(items, kConvMenuShareFile).m_enabled);
    CHECK(FindEntry(items, kConvMenuShareScreen).m_enabled);

    //群聊：文案是「…到本群」
    ConversationMenuState group = MakeState();
    group.m_isGroup = true;
    group.m_canShareFile = true;
    group.m_canShareScreen = true;
    const std::vector<ConversationMenuEntry> groupItems = BuildConversationMenuModel(group);
    CHECK(std::wstring(FindEntry(groupItems, kConvMenuShareFile).m_label) == L"共享文件到本群");
    CHECK(std::wstring(FindEntry(groupItems, kConvMenuShareScreen).m_label) == L"共享屏幕到本群");

    //只有屏幕共享可用：只出现共享屏幕
    ConversationMenuState screenOnly = MakeState();
    screenOnly.m_canShareScreen = true;
    const std::vector<ConversationMenuEntry> screenItems = BuildConversationMenuModel(screenOnly);
    CHECK(FindEntry(screenItems, kConvMenuShareFile).m_id == 0);
    CHECK(FindEntry(screenItems, kConvMenuShareScreen).m_id == kConvMenuShareScreen);

    //都不可用：两项都不出现
    const std::vector<ConversationMenuEntry> none = BuildConversationMenuModel(MakeState());
    CHECK(FindEntry(none, kConvMenuShareFile).m_id == 0);
    CHECK(FindEntry(none, kConvMenuShareScreen).m_id == 0);

    CHECK(kConvMenuShareFile != kConvMenuShareScreen);
    CHECK(kConvMenuShareFile > kConvMenuClearHistory);
}
