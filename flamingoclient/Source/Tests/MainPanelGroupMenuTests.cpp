/**
 *  MainPanelGroupMenuTests.cpp
 *
 *  主面板群列表右键菜单行模型（BuildGroupMenuModel）与群号格式化（FormatGroupNumber）的
 *  单元测试（标签 [groupmenu][copymenu]）：
 *
 *    CM1  行序与分隔条位置：复制群号、复制群名紧跟「查看历史消息」；「群管理」只在群主 / 管理员
 *         的菜单里出现；命令 id 非 0 且互不重复；置顶 / 免打扰 / 收藏文案随状态取反。
 *    CM4  群号格式化：十进制数字串，不带"群号"字样；群 id 不大于 0 时为空串。
 *    CM5  群主的菜单在「退出群聊」之后多出「解散群聊」；管理员与普通成员没有。
 *
 *  用 Catch2 v3（amalgamated 形态）。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <cwchar>          // wcscmp —— 比对菜单项中文文本
#include <set>
#include <vector>

#include "../UI/BalloonMain/MainPanelGroupMenu.h"
#include "../UI/BalloonMain/FavoritesModel.h"   // GetMenuLabel（收藏项文案须与本模型一致）

using namespace balloonmain;

namespace {

// 取某个命令 id 的显示文本；找不到返回 NULL。
const wchar_t* LabelOf(const std::vector<GroupMenuEntry>& model, UINT id)
{
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].m_isSeparator && model[i].m_id == id)
        {
            return model[i].m_label;
        }
    }
    return NULL;
}

} // anonymous namespace

TEST_CASE("CM1 群列表菜单：复制群号与复制群名紧跟查看历史消息，群管理只给群主和管理员", "[groupmenu][copymenu]")
{
    SECTION("群主 / 管理员：有群管理")
    {
        GroupMenuState st;
        st.m_canManage = true;
        const std::vector<GroupMenuEntry> m = BuildGroupMenuModel(st);
        // 11 个功能项 + 1 条分隔条
        REQUIRE(m.size() == 12);
        size_t i = 0;
        CHECK(m[i].m_id == kGroupMenuEnter);           ++i;
        CHECK(m[i].m_id == kGroupMenuViewInfo);        ++i;
        CHECK(m[i].m_id == kGroupMenuFavorite);        ++i;
        CHECK(m[i].m_id == kGroupMenuViewHistory);     ++i;
        CHECK(m[i].m_id == kGroupMenuCopyGroupNo);     ++i;
        CHECK(m[i].m_id == kGroupMenuCopyGroupName);   ++i;
        CHECK(m[i].m_id == kGroupMenuManage);          ++i;
        CHECK(m[i].m_id == kGroupMenuPin);             ++i;
        CHECK(m[i].m_id == kGroupMenuMute);            ++i;
        CHECK(m[i].m_isSeparator);                     ++i;
        CHECK(m[i].m_id == kGroupMenuClearMsgs);       ++i;
        CHECK(m[i].m_id == kGroupMenuQuit);            ++i;
        CHECK(i == m.size());
    }
    SECTION("普通成员：没有群管理，其余不变")
    {
        const std::vector<GroupMenuEntry> m = BuildGroupMenuModel(GroupMenuState());
        REQUIRE(m.size() == 11);
        CHECK(LabelOf(m, kGroupMenuManage) == NULL);
        CHECK(m[4].m_id == kGroupMenuCopyGroupNo);
        CHECK(m[5].m_id == kGroupMenuCopyGroupName);
        CHECK(m[6].m_id == kGroupMenuPin);
    }

    //文字
    const std::vector<GroupMenuEntry> m = BuildGroupMenuModel(GroupMenuState());
    REQUIRE(LabelOf(m, kGroupMenuCopyGroupNo) != NULL);
    REQUIRE(LabelOf(m, kGroupMenuCopyGroupName) != NULL);
    CHECK(wcscmp(LabelOf(m, kGroupMenuCopyGroupNo), L"复制群号") == 0);
    CHECK(wcscmp(LabelOf(m, kGroupMenuCopyGroupName), L"复制群名") == 0);

    //命令 id 非 0 且互不重复（含群管理那份）
    GroupMenuState manage;
    manage.m_canManage = true;
    const std::vector<GroupMenuEntry> all = BuildGroupMenuModel(manage);
    std::set<UINT> ids;
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].m_isSeparator)
        {
            CHECK(all[i].m_id == 0);
            CHECK(all[i].m_label == NULL);
            continue;
        }
        CHECK(all[i].m_id != 0);
        CHECK(all[i].m_label != NULL);
        CHECK(ids.insert(all[i].m_id).second);
    }

    //置顶 / 免打扰 / 收藏的文案随状态取反
    GroupMenuState on;
    on.m_pinned = true;
    on.m_muted = true;
    on.m_favorite = true;
    const std::vector<GroupMenuEntry> mOn = BuildGroupMenuModel(on);
    CHECK(wcscmp(LabelOf(m, kGroupMenuPin), L"置顶群聊") == 0);
    CHECK(wcscmp(LabelOf(mOn, kGroupMenuPin), L"取消置顶") == 0);
    CHECK(wcscmp(LabelOf(m, kGroupMenuMute), L"消息免打扰") == 0);
    CHECK(wcscmp(LabelOf(mOn, kGroupMenuMute), L"取消免打扰") == 0);
    CHECK(wcscmp(LabelOf(m, kGroupMenuFavorite), FavoritesModel::GetMenuLabel(false)) == 0);
    CHECK(wcscmp(LabelOf(mOn, kGroupMenuFavorite), FavoritesModel::GetMenuLabel(true)) == 0);
}

TEST_CASE("CM4 群号格式化：十进制数字串，群 id 不大于 0 时为空串", "[groupmenu][copymenu]")
{
    CHECK(FormatGroupNumber(268435463LL) == std::wstring(L"268435463"));
    CHECK(FormatGroupNumber(1LL) == std::wstring(L"1"));
    //64 位群 id 也完整输出，不截断
    CHECK(FormatGroupNumber(9007199254740993LL) == std::wstring(L"9007199254740993"));
    CHECK(FormatGroupNumber(0LL).empty());
    CHECK(FormatGroupNumber(-5LL).empty());
}

TEST_CASE("CM5 群主的菜单在退出群聊之后多出解散群聊，管理员和普通成员没有", "[groupmenu][groupexit]")
{
    //群主：既能管理，也是群主
    GroupMenuState owner;
    owner.m_canManage = true;
    owner.m_isOwner = true;
    const std::vector<GroupMenuEntry> mOwner = BuildGroupMenuModel(owner);
    //12 个功能项 + 1 条分隔条
    REQUIRE(mOwner.size() == 13);
    CHECK(mOwner[mOwner.size() - 2].m_id == kGroupMenuQuit);
    CHECK(mOwner[mOwner.size() - 1].m_id == kGroupMenuDismiss);
    REQUIRE(LabelOf(mOwner, kGroupMenuDismiss) != NULL);
    CHECK(wcscmp(LabelOf(mOwner, kGroupMenuDismiss), L"解散群聊") == 0);
    //群主也保留「退出群聊」：点了之后走"先转让群主再退出"的流程
    CHECK(wcscmp(LabelOf(mOwner, kGroupMenuQuit), L"退出群聊") == 0);

    //新命令 id 非 0，且与其余各项互不重复
    CHECK(kGroupMenuDismiss != 0);
    std::set<UINT> ids;
    for (size_t i = 0; i < mOwner.size(); ++i)
    {
        if (!mOwner[i].m_isSeparator)
        {
            CHECK(ids.insert(mOwner[i].m_id).second);
        }
    }

    //管理员：有群管理，没有解散
    GroupMenuState admin;
    admin.m_canManage = true;
    const std::vector<GroupMenuEntry> mAdmin = BuildGroupMenuModel(admin);
    CHECK(LabelOf(mAdmin, kGroupMenuDismiss) == NULL);
    CHECK(mAdmin.back().m_id == kGroupMenuQuit);

    //普通成员：没有解散
    const std::vector<GroupMenuEntry> mMember = BuildGroupMenuModel(GroupMenuState());
    CHECK(LabelOf(mMember, kGroupMenuDismiss) == NULL);
    CHECK(mMember.back().m_id == kGroupMenuQuit);
}
