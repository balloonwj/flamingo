/**
 *  BotCardLayoutTests.cpp
 *
 *  机器人资料卡小窗的布局（UI/BalloonChatWnd/BotCardLayout.h，标签 [bot]）。2026-10-08 资料卡由系统消息框改成
 *  应用风格的小窗（用户确认），窗口宽度固定、高度按内容定。覆盖：
 *    BCL1 常见内容（简介、两条用法、标签）：窗口高度按内容定、没有触到上下限，「关闭」按钮排得下，
 *         详情区不需要滚动，各段都有矩形；
 *    BCL2 内容很多：窗口高度停在上限，「关闭」按钮仍然排得下，详情区可以滚动；
 *    BCL3 内容很少（没有简介、没有可用的用法、没有标签）：显示「暂无可用的触发方式」，不建简介与标签，
 *         窗口不低于下限，「关闭」按钮排得下；
 *    BCL4 FitBotCardBody 按给定宽度重新量：窄的宽度量出来更高，换回原宽度后结果与第一次相同；
 *    BCL5 先排过版、再重新量（窗口挂上实际字体之后的情形）：RelayoutBotCardBody 之后滚动视图的内容高度
 *         与详情内容完整显示需要的高度一致（开发约定文档 UI 约定第 12 条）。
 *
 *  用 Catch2 v3，控件树在内存里排版，不创建窗口。
 *
 *  balloonwj@qq.com   2026-10-08
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <memory>
#include <string>

#include "LayoutFitCheck.h"   // layoutfit::MeasureAt：按设计尺寸排得下的检查
#include "../UI/BalloonChatWnd/BotCardLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView

using namespace balloonchatwnd;

namespace {

// 内容很多时的用法条数：足以让窗口高度超过上限
const int kManyUsageLines = 30;
// BCL4 用的窄宽度（像素）：比设计宽度窄得多，长简介会折成更多行
const int kNarrowWidth = 200;
// 长简介由同一句话重复这么多次拼成，在设计宽度下也要折成多行
const int kLongTextRepeat = 12;

// 常见内容：企业机器人，有简介与两条用法
BotCardView TypicalView()
{
    BotCardView view;
    view.m_name = L"IT助手";
    view.m_tag = L"企业";
    view.m_desc = L"回答 IT 常见问题，VPN、打印机、邮箱都可以问。";
    view.m_usageLines.push_back(L"在群里 @我 并输入问题，我会从群问答里找答案");
    view.m_usageLines.push_back(L"发送包含「VPN」「打印机」的消息");
    view.m_source = L"企业管理员配置";
    return view;
}

// 一段很长的简介，在设计宽度下也要折成多行
std::wstring LongText()
{
    std::wstring text;
    for (int i = 0; i < kLongTextRepeat; ++i)
    {
        text += L"这是一段很长的机器人简介，用来检查自动换行之后的高度。";
    }
    return text;
}

// 控件的矩形宽高都大于 0
bool HasArea(const balloonwjui::DuiControl* ctrl)
{
    if (ctrl == NULL)
    {
        return false;
    }
    const RECT rc = ctrl->GetRect();
    return rc.right > rc.left && rc.bottom > rc.top;
}

// 按设计宽度量出内容高度、定出窗口高度，再检查「关闭」按钮在这个窗口里排得下。返回窗口高度
int FitAndCheckClose(balloonwjui::DuiControl* root)
{
    const int bodyH = FitBotCardBody(root, kBotCardWindowWidth);
    REQUIRE(bodyH > 0);
    const int windowH = BotCardWindowHeight(bodyH);
    const int contentH = windowH - kBotCardTitleBarH;
    const layoutfit::FitResult r = layoutfit::MeasureAt(root, kBotCardIdClose, kBotCardWindowWidth, contentH);
    REQUIRE(r.m_found);
    CHECK(r.m_bottom <= contentH);
    CHECK(r.m_height == r.m_roomyHeight);
    CHECK(r.m_bottomGap == r.m_roomyBottomGap);
    RelayoutBotCardBody(root);
    return windowH;
}

// 详情区滚动视图
balloonwjui::DuiScrollView* ScrollOf(balloonwjui::DuiControl* root)
{
    return dynamic_cast<balloonwjui::DuiScrollView*>(root->FindCtrlById(kBotCardIdScroll));
}

// 滚动视图的可见高度
int ViewportHeight(const balloonwjui::DuiScrollView* scroll)
{
    const RECT rc = scroll->GetRect();
    return rc.bottom - rc.top;
}

} // anonymous namespace

TEST_CASE("BCL1 常见内容：高度按内容定，关闭按钮排得下，详情不需要滚动", "[bot]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = BuildBotCardBody(TypicalView());
    REQUIRE(root);
    const int windowH = FitAndCheckClose(root.get());
    CHECK(windowH > kBotCardMinWindowHeight);
    CHECK(windowH < kBotCardMaxWindowHeight);

    //头部：名字、「机器人」徽标与「企业」标签都排了版
    CHECK(HasArea(root->FindCtrlById(kBotCardIdName)));
    CHECK(HasArea(root->FindCtrlById(kBotCardIdBotBadge)));
    CHECK(HasArea(root->FindCtrlById(kBotCardIdTag)));
    //详情：简介、两条用法、来源都排了版，且没有超出滚动视图
    balloonwjui::DuiScrollView* scroll = ScrollOf(root.get());
    REQUIRE(scroll != NULL);
    CHECK(scroll->GetContentHeight() <= ViewportHeight(scroll));
    const UINT ids[] = { kBotCardIdDesc, kBotCardIdUsageFirst, kBotCardIdUsageFirst + 1, kBotCardIdSource };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        balloonwjui::DuiControl* ctrl = root->FindCtrlById(ids[i]);
        REQUIRE(HasArea(ctrl));
        CHECK(ctrl->GetRect().bottom <= scroll->GetRect().bottom);
    }
    CHECK(root->FindCtrlById(kBotCardIdNoUsage) == NULL);
}

TEST_CASE("BCL2 内容很多：高度停在上限，关闭按钮排得下，详情可以滚动", "[bot]")
{
    BotCardView view = TypicalView();
    view.m_desc = LongText();
    for (int i = 0; i < kManyUsageLines; ++i)
    {
        view.m_usageLines.push_back(L"每个工作日 09:00 发送消息");
    }
    std::unique_ptr<balloonwjui::DuiControl> root = BuildBotCardBody(view);
    REQUIRE(root);
    CHECK(FitAndCheckClose(root.get()) == kBotCardMaxWindowHeight);

    balloonwjui::DuiScrollView* scroll = ScrollOf(root.get());
    REQUIRE(scroll != NULL);
    CHECK(scroll->GetContentHeight() > ViewportHeight(scroll));
    CHECK(HasArea(root->FindCtrlById(kBotCardIdDesc)));
    CHECK(HasArea(root->FindCtrlById(kBotCardIdUsageFirst)));
}

TEST_CASE("BCL3 内容很少：显示暂无可用的触发方式，不低于下限", "[bot]")
{
    BotCardView view;
    view.m_name = L"值班提醒";
    view.m_source = L"群主配置";
    std::unique_ptr<balloonwjui::DuiControl> root = BuildBotCardBody(view);
    REQUIRE(root);
    CHECK(FitAndCheckClose(root.get()) >= kBotCardMinWindowHeight);

    CHECK(root->FindCtrlById(kBotCardIdDesc) == NULL);
    CHECK(root->FindCtrlById(kBotCardIdTag) == NULL);
    CHECK(root->FindCtrlById(kBotCardIdUsageFirst) == NULL);
    balloonwjui::DuiLabel* hint = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kBotCardIdNoUsage));
    REQUIRE(hint != NULL);
    CHECK(std::wstring(hint->GetText()) == L"暂无可用的触发方式");
    CHECK(HasArea(hint));
    CHECK(HasArea(root->FindCtrlById(kBotCardIdSource)));
}

TEST_CASE("BCL4 按给定宽度重新量：窄的更高，换回原宽度结果不变", "[bot]")
{
    BotCardView view = TypicalView();
    view.m_desc = LongText();
    std::unique_ptr<balloonwjui::DuiControl> root = BuildBotCardBody(view);
    REQUIRE(root);
    const int normal = FitBotCardBody(root.get(), kBotCardWindowWidth);
    const int narrow = FitBotCardBody(root.get(), kNarrowWidth);
    CHECK(narrow > normal);
    CHECK(FitBotCardBody(root.get(), kBotCardWindowWidth) == normal);
    //不是资料卡的控件树：返回 0
    CHECK(FitBotCardBody(NULL, kBotCardWindowWidth) == 0);
    balloonwjui::DuiVBox other;
    CHECK(FitBotCardBody(&other, kBotCardWindowWidth) == 0);
}

TEST_CASE("BCL5 先排过版再重新量：重排之后滚动范围跟着内容变", "[bot]")
{
    BotCardView view = TypicalView();
    view.m_desc = LongText();
    std::unique_ptr<balloonwjui::DuiControl> root = BuildBotCardBody(view);
    REQUIRE(root);
    //按上限高度排一次版（窗口建出、挂上客户区时的情形）
    const RECT rc = { 0, 0, kBotCardWindowWidth, kBotCardMaxWindowHeight - kBotCardTitleBarH };
    root->ForceLayout(rc);
    balloonwjui::DuiScrollView* scroll = ScrollOf(root.get());
    REQUIRE(scroll != NULL);
    const int before = scroll->GetContentHeight();

    //按更窄的宽度重新量：各段变高，根节点矩形不变
    FitBotCardBody(root.get(), kNarrowWidth);
    balloonwjui::DuiControl* detail = root->FindCtrlById(kBotCardIdDetail);
    REQUIRE(detail != NULL);
    const int needed = detail->GetDesiredSize().cy;
    REQUIRE(needed > before);
    RelayoutBotCardBody(root.get());
    CHECK(scroll->GetContentHeight() == needed);
    CHECK(detail->GetRect().bottom - detail->GetRect().top == needed);
}
