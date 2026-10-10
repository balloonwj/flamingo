/**
 *  GroupManageBotsTests.cpp
 *
 *  群管理窗口「群机器人」页（添加与管理本群的群机器人，docs/机器人方案.md §14.9，标签 [bot][groupmanage]）。
 *  2026-10-08 用户确认：添加机器人的入口合入群管理面板，仍然只对群主可见，管理员与其他成员看不到。覆盖：
 *    GMB1 页面可见性：群主可见；管理员、普通成员不可见；企业关闭机器人或插件不可用时群主也不可见；
 *    GMB2 导航：满足条件时「设置」组里「群权限」之后出现「群机器人」，不满足时没有这一项；
 *    GMB3 页面内容：有机器人时每个一行（名字与标签与成员页签一致）并有「添加与管理机器人」按钮；没有机器人时显示
 *         「本群还没有机器人」与按钮；按设计尺寸排得下；
 *    GMB4 行数据换算（成员页签与本页共用）：名字、头像上的字与底色、标签、控件 id 与最多行数。
 *
 *  用 Catch2 v3，纯内存断言，不创建窗口。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../UI/BalloonGroupManage/GroupManageMapping.h"
#include "../UI/BalloonGroupManage/GroupManageModel.h"
#include "../UI/BalloonGroupManage/GroupManageLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../UI/BalloonChatWnd/GroupBotModel.h"
#include "../UI/BalloonMain/AvatarPalette.h"
#include "../Common/I18n/I18n.h"

using namespace balloongroupmanage;

namespace {

//一个 2801 下发的机器人
net::GroupBotInfo MakeBot(int64_t botId, int64_t userId, const char* name, bool isGroupBot)
{
    net::GroupBotInfo b;
    b.m_botId = botId;
    b.m_userId = userId;
    b.m_name = name;
    b.m_isGroupBot = isGroupBot;
    return b;
}

// 内容区的设计宽、高（像素），与群管理窗口右侧滚动视口的大小相当
const int kPageWidth = 640;
const int kPageHeight = 560;

// 群主、插件可用的模型，带两个机器人（一个企业机器人、一个群机器人）
GroupManageModel MakeOwnerModel(int loadState, bool withBots)
{
    GroupManageModel model;
    model.m_myRole = kRoleOwner;
    model.m_botsAvailable = true;
    model.m_botsState = static_cast<LoadState>(loadState);
    if (withBots)
    {
        std::vector<net::GroupBotInfo> bots;
        bots.push_back(MakeBot(5, 5401, u8"VPN助手", false));
        bots.push_back(MakeBot(2, 5402, u8"值班提醒", true));
        model.m_botRows = balloonchatwnd::BuildBotSectionRows(bots, kIdBotRowBase, 60);
    }
    return model;
}

// 控件在父容器里的下标；找不到返回 -1
int IndexOf(const balloonwjui::DuiControl* parent, UINT id)
{
    for (size_t i = 0; i < parent->Children().size(); ++i)
    {
        if (parent->Children()[i]->GetCtrlId() == id)
            return static_cast<int>(i);
    }
    return -1;
}

// 控件的矩形宽高都大于 0
bool HasArea(const balloonwjui::DuiControl* ctrl)
{
    if (ctrl == NULL)
        return false;
    const RECT rc = ctrl->GetRect();
    return rc.right > rc.left && rc.bottom > rc.top;
}

} // anonymous namespace

TEST_CASE("GMB1 群机器人页只对群主可见，且要求企业打开机器人、插件可用", "[bot][groupmanage]")
{
    //页面本身（kPageBots）：只有群主
    CHECK(IsPageVisibleForRole(kPageBots, kRoleOwner));
    CHECK_FALSE(IsPageVisibleForRole(kPageBots, kRoleAdmin));
    CHECK_FALSE(IsPageVisibleForRole(kPageBots, kRoleMember));

    //加上可用性：企业关闭机器人或插件没加载时群主也看不到
    CHECK(IsBotsPageVisible(kRoleOwner, true));
    CHECK_FALSE(IsBotsPageVisible(kRoleOwner, false));
    CHECK_FALSE(IsBotsPageVisible(kRoleAdmin, true));
    CHECK_FALSE(IsBotsPageVisible(kRoleMember, true));
}

TEST_CASE("GMB4 机器人行数据换算：名字、头像、标签与控件 id", "[bot][groupmanage]")
{
    i18n::ResetForTest();
    std::vector<net::GroupBotInfo> bots;
    net::GroupBotInfo ent = MakeBot(5, 5401, u8"VPN助手", false);
    ent.m_color = "#00897B";
    ent.m_glyph = "V";
    bots.push_back(ent);
    bots.push_back(MakeBot(2, 5402, u8"值班提醒", true));
    net::GroupBotInfo off = MakeBot(7, 5403, u8"IT助手", false);
    off.m_off = true;
    bots.push_back(off);

    const UINT kFirstId = 9000;
    const std::vector<balloonchatwnd::GroupBotSectionRow> rows = balloonchatwnd::BuildBotSectionRows(bots, kFirstId, 10);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].m_text == L"VPN助手");
    CHECK(rows[0].m_ctrlId == kFirstId);
    CHECK(rows[0].m_avatarText == L"V");
    CHECK(rows[0].m_avatarColor == RGB(0x00, 0x89, 0x7B));
    CHECK(rows[0].m_tag == L"企业");
    //群机器人：没有标签；没有设置底色时按账号从调色板取；头像上的字取名称第一个字
    CHECK(rows[1].m_ctrlId == kFirstId + 1);
    CHECK(rows[1].m_tag.empty());
    CHECK(rows[1].m_avatarText == L"值");
    CHECK(rows[1].m_avatarColor == balloonmain::AvatarColorForUser(5402));
    CHECK(rows[2].m_tag == L"本群已关闭");

    //最多行数
    CHECK(balloonchatwnd::BuildBotSectionRows(bots, kFirstId, 2).size() == 2);
    CHECK(balloonchatwnd::BuildBotSectionRows(std::vector<net::GroupBotInfo>(), kFirstId, 10).empty());
}

TEST_CASE("GMB2 导航：群主且可用时「群权限」之后有「群机器人」，否则没有", "[bot][groupmanage]")
{
    GroupManageModel model = MakeOwnerModel(kLoadDone, true);
    {
        std::unique_ptr<balloonwjui::DuiControl> nav = BuildNav(model);
        REQUIRE(nav);
        const int perms = IndexOf(nav.get(), kPagePerms);
        const int bots = IndexOf(nav.get(), kPageBots);
        const int danger = IndexOf(nav.get(), kPageDanger);
        REQUIRE(bots >= 0);
        CHECK(perms >= 0);
        CHECK(perms < bots);
        CHECK(bots < danger);
    }
    //管理员、普通成员：没有
    model.m_myRole = kRoleAdmin;
    CHECK(BuildNav(model)->FindCtrlById(kPageBots) == NULL);
    model.m_myRole = kRoleMember;
    CHECK(BuildNav(model)->FindCtrlById(kPageBots) == NULL);
    //群主，但企业关闭机器人或插件不可用：没有
    model.m_myRole = kRoleOwner;
    model.m_botsAvailable = false;
    CHECK(BuildNav(model)->FindCtrlById(kPageBots) == NULL);
}

TEST_CASE("GMB3 群机器人页：机器人列表、空提示、加载中与按钮", "[bot][groupmanage]")
{
    i18n::ResetForTest();
    const RECT rc = { 0, 0, kPageWidth, kPageHeight };

    //有两个机器人：两行（名字与成员页签一致）、没有空提示、有按钮，按钮在最后一行下面
    {
        std::unique_ptr<balloonwjui::DuiControl> page = BuildPage(kPageBots, MakeOwnerModel(kLoadDone, true));
        REQUIRE(page);
        page->Layout(rc);
        balloonwjui::DuiControl* row0 = page->FindCtrlById(kIdBotRowBase);
        balloonwjui::DuiControl* row1 = page->FindCtrlById(kIdBotRowBase + 1);
        balloonwjui::DuiControl* button = page->FindCtrlById(kIdBtnBotsManage);
        REQUIRE(HasArea(row0));
        REQUIRE(HasArea(row1));
        REQUIRE(HasArea(button));
        CHECK(row1->GetRect().top >= row0->GetRect().bottom);
        CHECK(button->GetRect().top >= row1->GetRect().bottom);
        CHECK(button->GetRect().bottom <= kPageHeight);
        REQUIRE(row0->Children().size() >= 2);
        balloonwjui::DuiLabel* name = dynamic_cast<balloonwjui::DuiLabel*>(row0->Children()[1].get());
        REQUIRE(name != NULL);
        CHECK(std::wstring(name->GetText()) == L"VPN助手");
        CHECK(page->FindCtrlById(kIdBotsEmpty) == NULL);
    }
    //本群没有机器人：空提示与按钮
    {
        std::unique_ptr<balloonwjui::DuiControl> page = BuildPage(kPageBots, MakeOwnerModel(kLoadDone, false));
        REQUIRE(page);
        page->Layout(rc);
        CHECK(HasArea(page->FindCtrlById(kIdBotsEmpty)));
        CHECK(HasArea(page->FindCtrlById(kIdBtnBotsManage)));
        CHECK(page->FindCtrlById(kIdBotRowBase) == NULL);
    }
    //2801 还没到：只显示「正在加载」，没有按钮
    {
        std::unique_ptr<balloonwjui::DuiControl> page = BuildPage(kPageBots, MakeOwnerModel(kLoadPending, false));
        REQUIRE(page);
        CHECK(page->FindCtrlById(kIdLoadPending) != NULL);
        CHECK(page->FindCtrlById(kIdBtnBotsManage) == NULL);
    }
}
