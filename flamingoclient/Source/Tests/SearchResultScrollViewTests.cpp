/**
 *  主面板搜索下拉结果滚动区的单元测试（Suite SRS），SearchResultScrollViewTests.cpp
 *
 *  覆盖 balloonsearch::SearchResultScrollView（2026-10-04 起直接使用 balloonui 统一的悬浮式细滑块）：
 *    SRS1  内容放得下时没有滑块，右侧也不认领点击；结果行用满整宽；
 *    SRS2  内容超出时滑块位置与聊天窗的算法（ComputeChatScrollThumb）完全一致，宽度为聊天窗滑块宽；
 *          命中带上的点击由视口的滚动条认领；
 *    SRS3  按住滑块向下拖动，滚动位置按聊天窗的换算（ChatScrollYFromThumbTop）变化；松开后
 *          不再跟随鼠标；
 *    SRS4  按在轨道空白处，滑块中心跳到点击处。
 *  不创建窗口，直接设矩形、调鼠标处理函数（没有宿主时 Capture 不做任何事）。框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonSearch/SearchResultScrollView.h"
#include "../UI/BalloonChatWnd/ChatScrollThumb.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox

#include <memory>

using balloonsearch::SearchResultScrollView;

namespace {

// 视口尺寸（与搜索下拉的宽度、最大内容高度同一量级）
const int kViewW = 338;
const int kViewH = 300;
// 内容放得下与超出时的内容高度
const int kShortContent = 200;
const int kLongContent = 1200;

// 建一个设好矩形与内容高度的滚动视口
void Setup(SearchResultScrollView& sv, int contentHeight)
{
    sv.SetAutoContentHeight(false);
    sv.SetContent(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiVBox()));
    sv.SetContentHeight(contentHeight);
    RECT rc = { 0, 0, kViewW, kViewH };
    sv.SetRect(rc);
}

// 矩形中心
POINT CenterOf(const RECT& r)
{
    POINT pt = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
    return pt;
}

} // namespace

TEST_CASE("SRS1 内容放得下时没有滑块，结果行用满整宽", "[globalsearch]")
{
    SearchResultScrollView sv;
    Setup(sv, kShortContent);
    const RECT thumb = sv.ThumbRect();
    CHECK(thumb.right == thumb.left);
    //右侧边缘不认领：没有滑块时命中结果与基类相同
    POINT edge = { kViewW - 2, kViewH / 2 };
    CHECK(sv.HitTest(edge) == sv.balloonwjui::DuiScrollView::HitTest(edge));
    //滚动条为悬浮式、不占内容宽度：内容控件的宽度等于视口宽度
    REQUIRE(sv.GetContent() != nullptr);
    const RECT content = sv.GetContent()->GetRect();
    CHECK(content.right - content.left == kViewW);
}

TEST_CASE("SRS2 内容超出时滑块与聊天窗算法一致，命中带由本控件认领", "[globalsearch]")
{
    SearchResultScrollView sv;
    Setup(sv, kLongContent);
    RECT view = { 0, 0, kViewW, kViewH };
    const RECT expected = balloonchatwnd::ComputeChatScrollThumb(view, kViewH, kLongContent, 0);
    const RECT thumb = sv.ThumbRect();
    REQUIRE(thumb.bottom > thumb.top);
    CHECK(thumb.left == expected.left);
    CHECK(thumb.top == expected.top);
    CHECK(thumb.right == expected.right);
    CHECK(thumb.bottom == expected.bottom);
    CHECK(thumb.right - thumb.left == balloonchatwnd::kChatScrollBarWidth);
    CHECK(sv.HitTest(CenterOf(thumb)) == sv.GetScrollBar());
    //离滑块很远的左侧不认领：命中结果与基类相同
    POINT left = { 10, kViewH / 2 };
    CHECK(sv.HitTest(left) == sv.balloonwjui::DuiScrollView::HitTest(left));

    //滚到一半后滑块跟着下移，仍与聊天窗算法一致
    sv.SetScrollPos((kLongContent - kViewH) / 2);
    const RECT moved = balloonchatwnd::ComputeChatScrollThumb(view, kViewH, kLongContent, sv.GetScrollPos());
    CHECK(sv.ThumbRect().top == moved.top);
    CHECK(moved.top > expected.top);
}

TEST_CASE("SRS3 按住滑块拖动按聊天窗的换算改变滚动位置", "[globalsearch]")
{
    //向下拖动的距离（像素）
    const int kDragDy = 80;
    SearchResultScrollView sv;
    Setup(sv, kLongContent);
    RECT view = { 0, 0, kViewW, kViewH };
    const RECT thumb = sv.ThumbRect();
    POINT down = CenterOf(thumb);
    balloonwjui::DuiScrollBar* bar = sv.GetScrollBar();
    REQUIRE(bar != nullptr);
    REQUIRE(sv.HitTest(down) == bar);
    REQUIRE(bar->OnLButtonDown(down, MK_LBUTTON));
    CHECK(sv.IsDraggingThumb());
    CHECK(sv.GetScrollPos() == 0);   //按在滑块上不跳

    POINT move = down;
    move.y += kDragDy;
    REQUIRE(bar->OnMouseMove(move, MK_LBUTTON));
    const int grab = down.y - thumb.top;
    const int expected = balloonchatwnd::ChatScrollYFromThumbTop(move.y - grab, view, kViewH, kLongContent);
    CHECK(expected > 0);
    CHECK(sv.GetScrollPos() == expected);

    REQUIRE(bar->OnLButtonUp(move, 0));
    CHECK_FALSE(sv.IsDraggingThumb());
    //松开之后鼠标再移动不再改滚动位置
    POINT later = move;
    later.y += kDragDy;
    bar->OnMouseMove(later, 0);
    CHECK(sv.GetScrollPos() == expected);
}

TEST_CASE("SRS4 按在轨道空白处，滑块中心跳到点击处", "[globalsearch]")
{
    SearchResultScrollView sv;
    Setup(sv, kLongContent);
    RECT view = { 0, 0, kViewW, kViewH };
    const RECT thumb = sv.ThumbRect();
    //点在滑块下方的轨道上
    POINT click = { (thumb.left + thumb.right) / 2, kViewH - 20 };
    REQUIRE(click.y > thumb.bottom);
    balloonwjui::DuiScrollBar* bar = sv.GetScrollBar();
    REQUIRE(sv.HitTest(click) == bar);
    REQUIRE(bar->OnLButtonDown(click, MK_LBUTTON));
    const int half = (thumb.bottom - thumb.top) / 2;
    const int expected = balloonchatwnd::ChatScrollYFromThumbTop(click.y - half, view, kViewH, kLongContent);
    CHECK(sv.GetScrollPos() == expected);
    CHECK(expected > 0);
    bar->OnLButtonUp(click, 0);
}
