/**
 *  MainPanelContactMenuTests.cpp
 *
 *  通讯录联系人右键菜单（MainPanelContactMenu）的测试：验证纯函数
 *  BuildContactMenuModel() 产出的行模型 —— 右键同事时的条目数 / 顺序 /
 *  分隔条位置（防回归），右键自己时砍成只剩"查看资料"，命令 id 的唯一性与
 *  非 0 约束，以及随状态切换的文案（收藏 / 免打扰）。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <set>
#include <vector>
#include <cwchar>          // wcscmp —— 比对菜单项中文文本

#include "../UI/BalloonMain/MainPanelContactMenu.h"
#include "../UI/BalloonMain/FavoritesModel.h"   // GetMenuLabel（收藏项文案须与本模型一致）

using namespace balloonmain;

namespace {

// 造一份"右键的是同事"的状态。
ContactMenuState MakePeerState()
{
    ContactMenuState st;
    st.m_isSelf   = false;
    st.m_muted    = false;
    st.m_favorite = false;
    return st;
}

// 造一份"右键的是自己"的状态。
ContactMenuState MakeSelfState()
{
    ContactMenuState st;
    st.m_isSelf = true;
    return st;
}

// 模型里是否含有某个命令 id 的功能项。
bool HasId(const std::vector<ContactMenuEntry>& model, UINT id)
{
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].m_isSeparator && model[i].m_id == id)
        {
            return true;
        }
    }
    return false;
}

// 取某个命令 id 的显示文本；找不到返回 NULL。
const wchar_t* LabelOf(const std::vector<ContactMenuEntry>& model, UINT id)
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

TEST_CASE("CTM1 右键同事：条目顺序与分隔条位置保持不变", "[contactmenu]")
{
    const std::vector<ContactMenuEntry> model = BuildContactMenuModel(MakePeerState());

    // 9 个功能项 + 2 条分隔线 = 11 行。这条断言是防回归的锚：菜单形态一个字
    // 都不该悄悄变。企业版取消好友关系后，原先的"移动到分组""加入黑名单"
    // "删除好友"三项连同末尾那条分隔线一并去掉了，故由 14 行降到 10 行；
    // 2026-08-12 加了「特别关注」一项，回到 11 行；2026-10-01 加了「复制联系人名」，12 行。
    REQUIRE(model.size() == 12);

    size_t i = 0;
    CHECK(model[i].m_id == kContactMenuSendMsg);        ++i;
    CHECK(model[i].m_id == kContactMenuVoice);          ++i;
    CHECK(model[i].m_id == kContactMenuVideo);          ++i;
    CHECK(model[i].m_isSeparator);                      ++i;
    CHECK(model[i].m_id == kContactMenuViewProfile);    ++i;
    CHECK(model[i].m_id == kContactMenuFavorite);       ++i;
    CHECK(model[i].m_id == kContactMenuStarred);        ++i;
    CHECK(model[i].m_id == kContactMenuViewHistory);    ++i;
    CHECK(model[i].m_id == kContactMenuRemark);         ++i;
    CHECK(model[i].m_id == kContactMenuCopyName);       ++i;
    CHECK(model[i].m_isSeparator);                      ++i;
    CHECK(model[i].m_id == kContactMenuMute);           ++i;
    CHECK(i == model.size());   // 逐项对完，正好走到末尾，没有漏检的行
}

TEST_CASE("CTM1b 好友相关的三项已彻底移除", "[contactmenu]")
{
    // 企业版通讯录就是企业组织架构，联系人的归属由部门表达，既没有好友分组，
    // 也谈不上"删掉一个同事"。这三项不是灰显、是整项不存在。
    // 用字面量而不是枚举值来断言 —— 枚举项本身已经删了，这里要防的是"日后
    // 有人复用 7 / 9 / 10 这三个编号"。
    const std::vector<ContactMenuEntry> peer = BuildContactMenuModel(MakePeerState());
    CHECK_FALSE(HasId(peer, 7));    // 曾经的"移动到分组"
    CHECK_FALSE(HasId(peer, 9));    // 曾经的"加入黑名单"
    CHECK_FALSE(HasId(peer, 10));   // 曾经的"删除好友"
}

TEST_CASE("CTM2 右键自己：只剩查看资料一项", "[contactmenu]")
{
    const std::vector<ContactMenuEntry> model = BuildContactMenuModel(MakeSelfState());

    REQUIRE(model.size() == 1);
    CHECK_FALSE(model[0].m_isSeparator);
    CHECK(model[0].m_id == kContactMenuViewProfile);
    REQUIRE(model[0].m_label != NULL);
    CHECK(wcscmp(model[0].m_label, L"查看资料") == 0);
}

TEST_CASE("CTM3 右键自己：那些对自己无意义的项一个都不出现", "[contactmenu]")
{
    const std::vector<ContactMenuEntry> model = BuildContactMenuModel(MakeSelfState());

    // 与自己不能聊天，也谈不上给自己改备注。这些项必须整项不出现，而不是灰显
    // —— 灰显仍会让用户以为"本来可以，只是现在不行"。
    CHECK_FALSE(HasId(model, kContactMenuSendMsg));
    CHECK_FALSE(HasId(model, kContactMenuVoice));
    CHECK_FALSE(HasId(model, kContactMenuVideo));
    CHECK_FALSE(HasId(model, kContactMenuRemark));
    CHECK_FALSE(HasId(model, kContactMenuMute));
    CHECK_FALSE(HasId(model, kContactMenuFavorite));
    CHECK_FALSE(HasId(model, kContactMenuViewHistory));
    //复制自己的名字没有意义，自己那份菜单里同样不出现
    CHECK_FALSE(HasId(model, kContactMenuCopyName));

    // 只有一项时不该还留着分隔条 —— 没有东西可分。
    for (size_t i = 0; i < model.size(); ++i)
    {
        CHECK_FALSE(model[i].m_isSeparator);
    }
}

TEST_CASE("CTM4 命令 id 唯一且非 0（两种状态都验）", "[contactmenu]")
{
    // 0 是 DuiMenu::TrackPopup 的"未选中"返回值：被功能项占用的话，用户按 ESC
    // 关掉菜单会被当成选了那一项。
    ContactMenuState states[2];
    states[0] = MakePeerState();
    states[1] = MakeSelfState();

    for (size_t s = 0; s < 2; ++s)
    {
        const std::vector<ContactMenuEntry> model = BuildContactMenuModel(states[s]);
        std::set<UINT> seen;
        for (size_t i = 0; i < model.size(); ++i)
        {
            if (model[i].m_isSeparator)
            {
                CHECK(model[i].m_id == 0);
                CHECK(model[i].m_label == NULL);
                continue;
            }
            CHECK(model[i].m_id != 0);
            CHECK(model[i].m_label != NULL);
            CHECK(seen.insert(model[i].m_id).second);   // 未重复出现过
        }
    }
}

TEST_CASE("CTM5 免打扰与收藏的文案随当前状态取反", "[contactmenu]")
{
    SECTION("未免打扰 / 未收藏")
    {
        ContactMenuState st = MakePeerState();
        st.m_muted    = false;
        st.m_favorite = false;
        const std::vector<ContactMenuEntry> model = BuildContactMenuModel(st);

        const wchar_t* mute = LabelOf(model, kContactMenuMute);
        REQUIRE(mute != NULL);
        CHECK(wcscmp(mute, L"消息免打扰") == 0);

        // 收藏项文案与会话右键菜单共用 FavoritesModel 那一份，避免措辞漂移。
        const wchar_t* fav = LabelOf(model, kContactMenuFavorite);
        REQUIRE(fav != NULL);
        CHECK(wcscmp(fav, FavoritesModel::GetMenuLabel(false)) == 0);
    }

    SECTION("已免打扰 / 已收藏")
    {
        ContactMenuState st = MakePeerState();
        st.m_muted    = true;
        st.m_favorite = true;
        const std::vector<ContactMenuEntry> model = BuildContactMenuModel(st);

        const wchar_t* mute = LabelOf(model, kContactMenuMute);
        REQUIRE(mute != NULL);
        CHECK(wcscmp(mute, L"取消免打扰") == 0);

        const wchar_t* fav = LabelOf(model, kContactMenuFavorite);
        REQUIRE(fav != NULL);
        CHECK(wcscmp(fav, FavoritesModel::GetMenuLabel(true)) == 0);
    }
}

TEST_CASE("CTM6 是自己时 muted / favorite 不影响结果", "[contactmenu]")
{
    // 自己那份菜单里根本没有免打扰与收藏两项，故这两个状态位取什么值都应产出
    // 同一份模型。这条防的是"以后有人给自己那份也加了状态相关项却忘了想清楚"。
    ContactMenuState a = MakeSelfState();
    a.m_muted    = false;
    a.m_favorite = false;

    ContactMenuState b = MakeSelfState();
    b.m_muted    = true;
    b.m_favorite = true;

    const std::vector<ContactMenuEntry> ma = BuildContactMenuModel(a);
    const std::vector<ContactMenuEntry> mb = BuildContactMenuModel(b);

    REQUIRE(ma.size() == mb.size());
    for (size_t i = 0; i < ma.size(); ++i)
    {
        CHECK(ma[i].m_id == mb[i].m_id);
        CHECK(ma[i].m_isSeparator == mb[i].m_isSeparator);
        if (ma[i].m_label != NULL && mb[i].m_label != NULL)
        {
            CHECK(wcscmp(ma[i].m_label, mb[i].m_label) == 0);
        }
    }
}
