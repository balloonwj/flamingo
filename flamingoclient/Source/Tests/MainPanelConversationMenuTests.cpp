/**
 *  MainPanelConversationMenuTests.cpp
 *
 *  会话列表右键菜单（MainPanelConversationMenu）的测试：验证纯函数
 *  BuildConversationMenuModel() 产出的行模型 —— 条目数、顺序、分隔条位置、
 *  命令 id 的唯一性与非 0 约束、随会话状态切换的文案（查看资料 / 查看群资料、
 *  置顶 / 取消置顶、免打扰 / 取消免打扰）、未读为 0 时"标记已读"灰显。
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
#include "../UI/BalloonMain/StarredModel.h"     // GetMenuLabel（与本模型的特别关注项文案须一致）

using namespace balloonmain;

namespace {

// 造一个"单聊、未置顶、未免打扰、有 3 条未读"的常规状态，各用例按需改单个字段。
ConversationMenuState MakeState()
{
    ConversationMenuState st;
    st.m_isGroup = false;
    st.m_pinned  = false;
    st.m_muted   = false;
    st.m_unread  = 3;
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

    // 10 个功能项 + 2 条分隔条 = 12 行（MakeState 造的是单聊会话，故有「特别关注」）。
    REQUIRE(items.size() == 12);

    CHECK(items[0].m_id == kConvMenuOpen);
    CHECK(items[1].m_id == kConvMenuMarkRead);
    CHECK(items[2].m_id == kConvMenuHistory);
    CHECK(items[3].m_id == kConvMenuProfile);
    // 收藏项紧跟在"查看资料"之后，与联系人 / 群两个右键菜单同一位置。
    CHECK(items[4].m_id == kConvMenuFavorite);
    // 特别关注又紧跟收藏，同样与联系人右键菜单同一位置。
    CHECK(items[5].m_id == kConvMenuStarred);
    CHECK(items[6].m_isSeparator == true);
    CHECK(items[7].m_id == kConvMenuPin);
    CHECK(items[8].m_id == kConvMenuMute);
    CHECK(items[9].m_isSeparator == true);
    CHECK(items[10].m_id == kConvMenuDelete);
    CHECK(items[11].m_id == kConvMenuDeleteAll);

    // 两个删除项排在最末一组，与上面的日常动作被分隔条隔开，减少误点。
    CHECK(items[10].m_isSeparator == false);
    CHECK(items[11].m_isSeparator == false);
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
    CHECK(funcCount == 10);
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

    // 切类型只换文案，不增减菜单项。
    CHECK(groupItems.size() == 11);
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

TEST_CASE("CMENU4c 特别关注只在单聊会话出现，文案随状态取反", "[convmenu][starred]")
{
    //单聊：该项存在，未关注时显示「特别关注」
    ConversationMenuState single = MakeState();
    ConversationMenuEntry eOff =
        FindEntry(BuildConversationMenuModel(single), kConvMenuStarred);
    REQUIRE(eOff.m_id == kConvMenuStarred);
    REQUIRE(eOff.m_label != NULL);
    CHECK(wcscmp(eOff.m_label, L"特别关注") == 0);

    single.m_starred = true;
    ConversationMenuEntry eOn =
        FindEntry(BuildConversationMenuModel(single), kConvMenuStarred);
    REQUIRE(eOn.m_label != NULL);
    CHECK(wcscmp(eOn.m_label, L"取消特别关注") == 0);

    //与 StarredModel::GetMenuLabel 必须是同一套文案 —— 另外四处菜单走那个函数、
    //会话菜单走本模型，两边不一致用户会以为是两个不同的功能。
    CHECK(wcscmp(eOff.m_label, balloonmain::StarredModel::GetMenuLabel(false)) == 0);
    CHECK(wcscmp(eOn.m_label, balloonmain::StarredModel::GetMenuLabel(true)) == 0);

    //群会话：整项<u>不出现</u>，而不是灰显。群没有上下线可言，摆在那里只会让用户
    //以为能关注整个群。
    ConversationMenuState group = MakeState();
    group.m_isGroup = true;
    group.m_starred = true;   //即便状态位为真也不该冒出来
    const std::vector<ConversationMenuEntry> groupItems = BuildConversationMenuModel(group);
    for (size_t i = 0; i < groupItems.size(); ++i)
    {
        INFO("行 index = " << i);
        CHECK((groupItems[i].m_isSeparator || groupItems[i].m_id != kConvMenuStarred));
    }
    //群会话因此比单聊少一行
    CHECK(groupItems.size() == BuildConversationMenuModel(single).size() - 1);
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

TEST_CASE("CMENU6 免打扰项文案随当前免打扰状态取反", "[convmenu]")
{
    ConversationMenuState notMuted = MakeState();
    ConversationMenuEntry eOff =
        FindEntry(BuildConversationMenuModel(notMuted), kConvMenuMute);
    REQUIRE(eOff.m_label != NULL);
    CHECK(wcscmp(eOff.m_label, L"消息免打扰") == 0);

    ConversationMenuState muted = MakeState();
    muted.m_muted = true;
    ConversationMenuEntry eOn =
        FindEntry(BuildConversationMenuModel(muted), kConvMenuMute);
    REQUIRE(eOn.m_label != NULL);
    CHECK(wcscmp(eOn.m_label, L"取消免打扰") == 0);
}

// =====================================================================
// CMENU7 —— 未读为 0 时「标记已读」灰显
// =====================================================================

TEST_CASE("CMENU7 未读为 0 时标记已读灰显，有未读时可点", "[convmenu]")
{
    ConversationMenuState unreadZero = MakeState();
    unreadZero.m_unread = 0;
    ConversationMenuEntry eGrey =
        FindEntry(BuildConversationMenuModel(unreadZero), kConvMenuMarkRead);
    REQUIRE(eGrey.m_id == kConvMenuMarkRead);
    CHECK(eGrey.m_enabled == false);

    ConversationMenuState hasUnread = MakeState();
    hasUnread.m_unread = 1;
    ConversationMenuEntry eOn =
        FindEntry(BuildConversationMenuModel(hasUnread), kConvMenuMarkRead);
    REQUIRE(eOn.m_id == kConvMenuMarkRead);
    CHECK(eOn.m_enabled == true);

    // 灰显的只有"标记已读"一项，其余动作在任何状态下都可点。
    std::vector<ConversationMenuEntry> items = BuildConversationMenuModel(unreadZero);
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_isSeparator || items[i].m_id == kConvMenuMarkRead)
        {
            continue;
        }
        CHECK(items[i].m_enabled == true);
    }
}
