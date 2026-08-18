/**
 *  滚轮沿父链上冒的行为契约单元测试（Suite WHB），MouseWheelBubbleTests.cpp
 *
 *  背景：balloonui 的 DuiHost 原先只把滚轮投给鼠标底下最深的那个控件，该控件
 *  返回 false 就到此为止、不再往上传。于是客户端凡是往 DuiScrollView 里放可命中
 *  控件的地方，都各自覆写 OnMouseWheel 手工往父链转交。2026-08-14 库里补上了默认
 *  上冒（DuiHost::DispatchMouseWheel 从命中控件起沿 GetParent() 链逐层询问，谁
 *  返回 true 就停），那些手工转交随之全部删除。
 *
 *  本套件把「删除之后滚轮仍然到得了外层滚动视口」这条契约钉住，覆盖：
 *    WHB1 主面板列表行（MainPanelListItem）上的滚轮上冒到祖先滚动视口；
 *    WHB2 主面板分组标题（MainPanelSectionHeader）同上；
 *    WHB3 滚轮跨过多层布局容器（DuiVBox 嵌 DuiHBox）仍然到得了滚动视口；
 *    WHB4 聊天气泡（ChatBubble）上的滚轮上冒到消息列表并改变滚动位置；
 *    WHB5 父链上压根没有可滚容器时，派发如实返回 false（不崩、不误报已处理）；
 *    WHB6 内层滚动视口没有可滚范围时，滚轮穿过它落到外层滚动视口上。
 *
 *  这些用例走的都是 DuiHost::DispatchMouseWheel —— 产品里滚轮真正的派发入口
 *  （DuiHost::OnMouseWheel 先 HitTopMost 再调它）。直接调控件自己的 OnMouseWheel
 *  测不出上冒链路，删掉转交代码后那种写法必然失效。
 *
 *  框架 Catch2 v3。本工程不创建窗口，纯内存搭控件树 + 断言。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-14
 */

//stdafx.h 必须最先引入：下面几个头文件用到 CString 等 ATL/WTL 类型，
//按 balloonui 的头文件约定由调用方保证前置依赖。
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/MainPanelListItem.h"
#include "../UI/BalloonMain/MainPanelSectionHeader.h"
#include "../UI/BalloonMain/MainPanelScrollView.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"      // DuiVBox / DuiHBox
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView

using balloonwjui::DuiControl;
using balloonwjui::DuiHost;
using balloonwjui::DuiHBox;
using balloonwjui::DuiVBox;
using balloonwjui::DuiScrollView;

namespace {

//测试用滚动视口的视口尺寸（像素）。取一个宽松的方块，够放下几行即可。
const int kViewW = 240;
const int kViewH = 200;

//喂给滚动视口的内容总高（像素）。远大于视口高，保证滚动范围非零。
const int kContentH = 1000;

//滚轮派发时用的鼠标位置（host 客户区坐标）。落在视口内，取中心附近。
const POINT kWheelPt = { 100, 100 };

//聊天消息列表的测试宽高（像素）。宽度影响折行、高度决定视口高，
//两者都要足够小，追加十几条消息就能撑出滚动范围。
const int kChatListW = 400;
const int kChatListH = 300;

//聊天列表里追加的消息条数。二十条长文本稳稳超过 kChatListH。
const int kChatMsgCount = 20;

/**
 *  搭一棵「滚动视口 → 内容 VBox → 单个待测控件」的最小控件树，并排好版。
 *    child：待测控件，本函数拿走所有权（挂进内容 VBox）。
 *    outSv：接收滚动视口的裸指针（所有权留在返回值里）。
 *  返回：持有整棵树的滚动视口。
 */
std::unique_ptr<DuiScrollView> MakeScrollTree(std::unique_ptr<DuiControl> child,
                                              DuiScrollView** outSv)
{
    std::unique_ptr<DuiScrollView> sv(new DuiScrollView());
    std::unique_ptr<DuiVBox> vbox(new DuiVBox());
    vbox->AddChild(std::move(child));
    sv->SetContent(std::unique_ptr<DuiControl>(vbox.release()));

    RECT rcView = { 0, 0, kViewW, kViewH };
    sv->ForceLayout(rcView);
    //内容高必须在布局之后喂：Layout 会按当前内容高重算滚动条范围。
    sv->SetContentHeight(kContentH);

    if (outSv != NULL)
    {
        *outSv = sv.get();
    }
    return sv;
}

//往聊天列表追加 n 条长文本消息（自己 / 对方交替），文字够长以确保折行、撑高内容。
void AppendChatMessages(balloonchatwnd::ChatMessageList& list, int n)
{
    for (int i = 0; i < n; ++i)
    {
        CString t;
        t.Format(_T("第 %d 条用于撑高列表的测试消息，内容需要足够长才会折行。"), i);
        list.AppendMessage(balloonchatwnd::MakeTextMessage((i % 2) == 0, t));
    }
}

} // anonymous namespace

// =====================================================================
// WHB1 主面板列表行
// =====================================================================

TEST_CASE("WHB1 列表行上的滚轮上冒到祖先滚动视口", "[wheel][bubble][main]")
{
    //MainPanelListItem 继承 DuiButton，铺满滚动视口的整宽，滚轮几乎总落在它身上。
    //它自己不消化滚轮（不覆写 OnMouseWheel），事件必须靠上冒到达外层滚动视口。
    std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
    balloonmain::MainPanelListItem* raw = item.get();

    DuiScrollView* sv = NULL;
    std::unique_ptr<DuiScrollView> tree =
        MakeScrollTree(std::unique_ptr<DuiControl>(item.release()), &sv);
    REQUIRE(sv != NULL);
    REQUIRE(sv->GetScrollPos() == 0);

    //zDelta 取 -WHEEL_DELTA 表示向下滚一格。
    CHECK(DuiHost::DispatchMouseWheel(raw, kWheelPt, -WHEEL_DELTA, 0) == true);
    CHECK(sv->GetScrollPos() > 0);
}

// =====================================================================
// WHB2 主面板分组标题
// =====================================================================

TEST_CASE("WHB2 分组标题上的滚轮上冒到祖先滚动视口", "[wheel][bubble][main]")
{
    //分组标题与列表行同处一个列表里，同样铺满整宽，理由见 WHB1。
    std::unique_ptr<balloonmain::MainPanelSectionHeader> sec(
        new balloonmain::MainPanelSectionHeader());
    balloonmain::MainPanelSectionHeader* raw = sec.get();

    DuiScrollView* sv = NULL;
    std::unique_ptr<DuiScrollView> tree =
        MakeScrollTree(std::unique_ptr<DuiControl>(sec.release()), &sv);
    REQUIRE(sv != NULL);
    REQUIRE(sv->GetScrollPos() == 0);

    CHECK(DuiHost::DispatchMouseWheel(raw, kWheelPt, -WHEEL_DELTA, 0) == true);
    CHECK(sv->GetScrollPos() > 0);
}

// =====================================================================
// WHB3 跨多层布局容器
// =====================================================================

TEST_CASE("WHB3 滚轮跨过多层布局容器仍到得了滚动视口", "[wheel][bubble][main]")
{
    //公告列表行的形状就是这样：滚动视口 → 内容 VBox → 行 HBox → 文字 VBox → Label。
    //命中的是最里层那个控件，中间隔着三层纯布局容器 —— 布局容器不消化滚轮，
    //必须一层层传上去。这一条钉住的是"中间层数不限"，而非某个具体控件。
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    std::unique_ptr<DuiVBox> texts(new DuiVBox());
    std::unique_ptr<DuiControl> leaf(new DuiControl());
    DuiControl* rawLeaf = leaf.get();
    texts->AddChild(std::move(leaf));
    row->AddChild(std::unique_ptr<DuiControl>(texts.release()));

    DuiScrollView* sv = NULL;
    std::unique_ptr<DuiScrollView> tree =
        MakeScrollTree(std::unique_ptr<DuiControl>(row.release()), &sv);
    REQUIRE(sv != NULL);
    REQUIRE(sv->GetScrollPos() == 0);

    CHECK(DuiHost::DispatchMouseWheel(rawLeaf, kWheelPt, -WHEEL_DELTA, 0) == true);
    CHECK(sv->GetScrollPos() > 0);
}

// =====================================================================
// WHB4 聊天气泡
// =====================================================================

TEST_CASE("WHB4 聊天气泡上的滚轮上冒到消息列表", "[wheel][bubble][chat]")
{
    //ChatMessageList 是自绘的滚动容器（不是 DuiScrollView），气泡是它的直接子控件。
    //气泡铺满列表整宽，滚轮几乎总落在气泡上，同样要靠上冒才到得了列表。
    balloonchatwnd::ChatMessageList list;
    AppendChatMessages(list, kChatMsgCount);

    RECT rcList = { 0, 0, kChatListW, kChatListH };
    list.ForceLayout(rcList);
    //追加消息时列表会自动滚到底，此处的滚动位置即最大值。
    list.ScrollToBottom();

    const int posAtBottom = list.ScrollY();
    REQUIRE(posAtBottom > 0);   //内容确实溢出，才谈得上滚

    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    REQUIRE(!kids.empty());
    balloonchatwnd::ChatBubble* bubble =
        dynamic_cast<balloonchatwnd::ChatBubble*>(kids[0].get());
    REQUIRE(bubble != NULL);

    //已经在底部，只能向上滚：zDelta 取 +WHEEL_DELTA。
    CHECK(DuiHost::DispatchMouseWheel(bubble, kWheelPt, WHEEL_DELTA, 0) == true);
    CHECK(list.ScrollY() < posAtBottom);
}

// =====================================================================
// WHB5 父链上没有可滚容器
// =====================================================================

TEST_CASE("WHB5 父链上没有可滚容器时派发返回未处理", "[wheel][bubble]")
{
    //一路问到根都没人认领时必须如实返回 false，宿主才好把消息交回 DefWindowProc。
    //误报"已处理"会让外层窗口（如聊天窗整窗滚动）永远收不到滚轮。
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
    balloonmain::MainPanelListItem* raw = item.get();
    root->AddChild(std::unique_ptr<DuiControl>(item.release()));

    RECT rc = { 0, 0, kViewW, kViewH };
    root->ForceLayout(rc);

    CHECK(DuiHost::DispatchMouseWheel(raw, kWheelPt, -WHEEL_DELTA, 0) == false);

    //命中控件为空（鼠标不在任何控件上）时同样返回 false，且不得崩溃。
    CHECK(DuiHost::DispatchMouseWheel(NULL, kWheelPt, -WHEEL_DELTA, 0) == false);
}

// =====================================================================
// WHB6 内层视口滚不动时事件穿过去
// =====================================================================

TEST_CASE("WHB6 内层视口没有可滚范围时滚轮落到外层视口", "[wheel][bubble]")
{
    //搭一棵嵌套的树：
    //  外层 MainPanelScrollView（内容高远超视口，滚得动）
    //    └─ DuiVBox
    //        └─ 内层 DuiScrollView（内容高小于视口，滚不动）
    //            └─ DuiVBox
    //                └─ 待测控件
    //内层没有可滚范围时 DuiScrollBar::OnMouseWheel 返回 false，事件应当穿过内层
    //继续上冒到外层。内层若把事件拦下，用户看到的是"鼠标停在这一小块上整页就滚
    //不动了"——短列表嵌在长页面里是很常见的排布，这条必须成立。
    std::unique_ptr<balloonmain::MainPanelScrollView> outer(
        new balloonmain::MainPanelScrollView());
    std::unique_ptr<DuiVBox> outerBox(new DuiVBox());

    std::unique_ptr<DuiScrollView> inner(new DuiScrollView());
    std::unique_ptr<DuiVBox> innerBox(new DuiVBox());
    std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
    balloonmain::MainPanelListItem* raw = item.get();
    innerBox->AddChild(std::unique_ptr<DuiControl>(item.release()));
    inner->SetContent(std::unique_ptr<DuiControl>(innerBox.release()));
    DuiScrollView* innerRaw = inner.get();
    outerBox->AddChild(std::unique_ptr<DuiControl>(inner.release()));
    outer->SetContent(std::unique_ptr<DuiControl>(outerBox.release()));

    RECT rcView = { 0, 0, kViewW, kViewH };
    outer->ForceLayout(rcView);
    outer->SetContentHeight(kContentH);
    //内层内容比它自己的视口还矮，滚动条范围收成零宽。
    innerRaw->SetContentHeight(1);

    REQUIRE(outer->GetScrollPos() == 0);
    REQUIRE(innerRaw->GetScrollPos() == 0);

    CHECK(DuiHost::DispatchMouseWheel(raw, kWheelPt, -WHEEL_DELTA, 0) == true);
    CHECK(innerRaw->GetScrollPos() == 0);   //内层原地不动
    CHECK(outer->GetScrollPos() > 0);       //外层真的滚了
}
