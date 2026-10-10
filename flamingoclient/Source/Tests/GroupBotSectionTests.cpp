/**
 *  GroupBotSectionTests.cpp
 *
 *  群聊窗成员页签「机器人 N」区域的重建与重排（UI/BalloonChatWnd/GroupBotSection.h，标签 [bot]）。覆盖：
 *    GBS1 第一次显示一行：区域可见、高度按行数算，那一行有矩形；
 *    GBS2 先排过版、再换一批行数相同的内容（群主「在本群关闭」之后重拉 2801）：新的那一行仍然有矩形、文字是新的。
 *         2026-10-08 实机联调时这一行显示不出来，原因见 GroupBotSection.h 与开发约定文档 UI 约定第 5 条；
 *    GBS3 隐藏之后再显示两行：两行都有矩形，第二行在第一行下面；
 *    GBS4 隐藏：区域不可见，各行摘掉；
 *    GBS5 行的样式与命中（2026-10-08 改成与成员行同一样式）：行高与成员行一致，头像、名字、标签都排了版，
 *         名字与标签的字高与成员行相同（13 / 10 像素，不随 DPI 放大），点在头像或标签上命中的都是整行
 *         （整行发点击通知，群聊窗据此弹资料卡）；
 *    GBS6 这一栏只用来查看（2026-10-08 起添加与管理的入口在群管理窗口）：本群没有机器人时谁都不显示（群主也一样），
 *         企业关闭机器人或 2801 还没到达时不显示；标题行里只有「机器人 N」，没有「管理」。
 *
 *  用 Catch2 v3，控件树由 BuildGroupChatWindowBody 建出，在内存里排版，不创建窗口。
 *
 *  balloonwj@qq.com   2026-10-08
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <memory>
#include <string>

#include "../UI/BalloonChatWnd/GroupBotSection.h"
#include "../UI/BalloonChatWnd/GroupBotModel.h"
#include "../UI/BalloonChatWnd/GroupChatWindowLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

using namespace balloonchatwnd;

namespace {

//群聊窗的设计尺寸（整窗，像素），与 BalloonGroupChatWindow 打开时的大小一致
const int kWindowW = 1000;
const int kWindowH = 720;

//建出群聊窗的控件树并按设计尺寸排一次版
std::unique_ptr<balloonwjui::DuiControl> BuildLaidOutBody()
{
    std::unique_ptr<balloonwjui::DuiControl> root = BuildGroupChatWindowBody(false);
    if (root)
    {
        const RECT rc = { 0, 0, kWindowW, kWindowH };
        root->ForceLayout(rc);
    }
    return root;
}

//一行机器人的显示内容：名字与标签
GroupBotSectionView OneRow(const std::wstring& text, const std::wstring& tag = L"企业")
{
    GroupBotSectionView view;
    view.m_show = true;
    view.m_title = L"机器人 1";
    GroupBotSectionRow row(text, kGIdBotRowFirst);
    row.m_tag = tag;
    view.m_rows.push_back(row);
    return view;
}

//行里的名字标签（第二个子控件）
balloonwjui::DuiLabel* RowNameLabel(balloonwjui::DuiControl* row)
{
    if (row == NULL || row->Children().size() < 2)
    {
        return NULL;
    }
    return dynamic_cast<balloonwjui::DuiLabel*>(row->Children()[1].get());
}

//标签控件所用字体的字高（像素，取 LOGFONT 的 lfHeight 绝对值）；取不到字体时返回 0
int FontPixelHeight(const balloonwjui::DuiLabel* label)
{
    if (label == NULL || label->GetFont() == NULL)
    {
        return 0;
    }
    LOGFONTW lf;
    if (::GetObjectW(label->GetFont(), sizeof(lf), &lf) == 0)
    {
        return 0;
    }
    return (lf.lfHeight < 0) ? -lf.lfHeight : lf.lfHeight;
}

//控件的矩形宽高都大于 0
bool HasArea(const balloonwjui::DuiControl* ctrl)
{
    if (ctrl == NULL)
    {
        return false;
    }
    const RECT rc = ctrl->GetRect();
    return rc.right > rc.left && rc.bottom > rc.top;
}

} // anonymous namespace

TEST_CASE("GBS1 第一次显示一行：区域可见，那一行有矩形", "[bot]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = BuildLaidOutBody();
    REQUIRE(root);
    REQUIRE(ApplyGroupBotSection(root.get(), OneRow(L"IT助手（企业）")));

    balloonwjui::DuiControl* section = root->FindCtrlById(kGIdBotSection);
    REQUIRE(section != NULL);
    CHECK(section->IsVisible());
    const RECT rcSection = section->GetRect();
    CHECK(rcSection.bottom - rcSection.top == GroupBotSectionHeight(1));
    CHECK(HasArea(root->FindCtrlById(kGIdBotRowFirst)));
}

TEST_CASE("GBS2 行数不变时换一批内容：新的那一行仍然排了版", "[bot]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = BuildLaidOutBody();
    REQUIRE(root);
    REQUIRE(ApplyGroupBotSection(root.get(), OneRow(L"IT助手", L"企业")));
    REQUIRE(ApplyGroupBotSection(root.get(), OneRow(L"IT助手", L"本群已关闭")));

    balloonwjui::DuiControl* row = root->FindCtrlById(kGIdBotRowFirst);
    REQUIRE(row != NULL);
    CHECK(HasArea(row));
    balloonwjui::DuiLabel* name = RowNameLabel(row);
    REQUIRE(name != NULL);
    CHECK(HasArea(name));
    CHECK(std::wstring(name->GetText()) == L"IT助手");
    REQUIRE(row->Children().size() == 3);
    CHECK(HasArea(row->Children()[2].get()));
}

TEST_CASE("GBS3 隐藏之后再显示两行：两行都排了版", "[bot]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = BuildLaidOutBody();
    REQUIRE(root);
    REQUIRE(ApplyGroupBotSection(root.get(), OneRow(L"IT助手（企业）")));
    REQUIRE(ApplyGroupBotSection(root.get(), GroupBotSectionView()));

    GroupBotSectionView two = OneRow(L"IT助手（企业）");
    two.m_title = L"机器人 2";
    two.m_rows.push_back(GroupBotSectionRow(L"值班提醒", kGIdBotRowFirst + 1));
    REQUIRE(ApplyGroupBotSection(root.get(), two));

    balloonwjui::DuiControl* first = root->FindCtrlById(kGIdBotRowFirst);
    balloonwjui::DuiControl* second = root->FindCtrlById(kGIdBotRowFirst + 1);
    REQUIRE(HasArea(first));
    REQUIRE(HasArea(second));
    CHECK(second->GetRect().top >= first->GetRect().bottom);
    balloonwjui::DuiControl* section = root->FindCtrlById(kGIdBotSection);
    REQUIRE(section != NULL);
    CHECK(section->GetRect().bottom - section->GetRect().top == GroupBotSectionHeight(2));
}

TEST_CASE("GBS4 隐藏：区域不可见，各行摘掉", "[bot]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = BuildLaidOutBody();
    REQUIRE(root);
    REQUIRE(ApplyGroupBotSection(root.get(), OneRow(L"IT助手（企业）")));
    REQUIRE(ApplyGroupBotSection(root.get(), GroupBotSectionView()));

    balloonwjui::DuiControl* section = root->FindCtrlById(kGIdBotSection);
    REQUIRE(section != NULL);
    CHECK_FALSE(section->IsVisible());
    CHECK(root->FindCtrlById(kGIdBotRowFirst) == NULL);
    //找不到区域时什么都不做
    CHECK_FALSE(ApplyGroupBotSection(NULL, OneRow(L"IT助手")));
}

TEST_CASE("GBS5 行与成员行同一样式，点在头像或标签上命中的都是整行", "[bot]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = BuildLaidOutBody();
    REQUIRE(root);
    REQUIRE(ApplyGroupBotSection(root.get(), OneRow(L"IT助手", L"企业")));

    balloonwjui::DuiControl* row = root->FindCtrlById(kGIdBotRowFirst);
    REQUIRE(row != NULL);
    REQUIRE(row->Children().size() == 3);
    const RECT rcRow = row->GetRect();
    CHECK(rcRow.bottom - rcRow.top == kBotSectionRowHeight);
    //头像、名字、标签都排了版
    for (size_t i = 0; i < row->Children().size(); ++i)
    {
        CHECK(HasArea(row->Children()[i].get()));
    }
    //点在头像中心、标签中心：命中的都是整行
    const RECT rcAvatar = row->Children()[0]->GetRect();
    const POINT ptAvatar = { (rcAvatar.left + rcAvatar.right) / 2, (rcAvatar.top + rcAvatar.bottom) / 2 };
    CHECK(root->HitTest(ptAvatar) == row);
    const RECT rcTag = row->Children()[2]->GetRect();
    const POINT ptTag = { (rcTag.left + rcTag.right) / 2, (rcTag.top + rcTag.bottom) / 2 };
    CHECK(root->HitTest(ptTag) == row);

    //字高与成员行（GroupMemberItem.cpp 的 kNamePx / kBadgePx）一致
    const int kMemberNamePx = 13;
    const int kMemberBadgePx = 10;
    CHECK(FontPixelHeight(RowNameLabel(row)) == kMemberNamePx);
    balloonwjui::DuiControl* tag = row->Children()[2].get();
    REQUIRE(tag->Children().size() == 1);
    CHECK(FontPixelHeight(dynamic_cast<balloonwjui::DuiLabel*>(tag->Children()[0].get())) == kMemberBadgePx);

    //没有标签的行（本群的群机器人）只有头像与名字
    GroupBotSectionView noTag = OneRow(L"值班提醒", L"");
    REQUIRE(ApplyGroupBotSection(root.get(), noTag));
    row = root->FindCtrlById(kGIdBotRowFirst);
    REQUIRE(row != NULL);
    CHECK(row->Children().size() == 2);
}

TEST_CASE("GBS6 机器人一栏只用来查看：没有机器人时不显示，标题行没有「管理」", "[bot]")
{
    //本群没有机器人：不显示（群主也一样）；企业关闭机器人或 2801 还没到达：不显示
    CHECK_FALSE(ShouldShowBotSection(true, true, 0));
    CHECK_FALSE(ShouldShowBotSection(false, true, 2));
    CHECK_FALSE(ShouldShowBotSection(true, false, 2));
    CHECK(ShouldShowBotSection(true, true, 1));

    //标题行里只有标题一个控件
    std::unique_ptr<balloonwjui::DuiControl> root = BuildLaidOutBody();
    REQUIRE(root);
    REQUIRE(ApplyGroupBotSection(root.get(), OneRow(L"IT助手")));
    balloonwjui::DuiControl* title = root->FindCtrlById(kGIdBotTitle);
    REQUIRE(title != NULL);
    REQUIRE(title->GetParent() != NULL);
    CHECK(title->GetParent()->Children().size() == 1);
}
