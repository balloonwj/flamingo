/**
 *  聊天窗图标按钮的数字角标单元测试（Suite CIB，标签 [chatwnd][sharebadge]），ChatIconButtonBadgeTests.cpp
 *
 *  头部的文件共享按钮用 ChatIconButton 的角标显示本会话里可用共享的个数（2026-10-03 起）。覆盖：
 *    - CIB1 角标的位置：贴按钮右上角、在按钮矩形之内；按钮挪动后跟着走；没有设置过数字时没有角标；
 *    - CIB2 数字：0 与负数不显示；点在角标上仍然是点这个按钮（角标不参与命中测试）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatIconButton.h"

namespace {

//测试用的按钮矩形：与头部按钮同宽（38），高 38
const RECT kButtonRect = { 100, 10, 138, 48 };

//按钮挪动后的矩形
const RECT kMovedRect = { 300, 10, 338, 48 };

//测试用的个数
const int kSomeCount = 3;

//负数个数（按 0 处理）
const int kNegativeCount = -5;

//图标边长的一半（ChatIconButton 的图标为 18 像素）
const int kIconHalf = 9;

//很矮的按钮：高度不足以让角标落在图标上沿上方
const RECT kLowRect = { 100, 10, 138, 30 };

} // namespace

TEST_CASE("CIB1 角标贴在按钮右上角并跟随按钮", "[chatwnd][sharebadge]")
{
    balloonchatwnd::ChatIconButton btn;
    btn.SetRect(kButtonRect);
    CHECK(btn.GetBadgeCount() == 0);
    const RECT none = btn.GetBadgeRect();
    CHECK(none.right == none.left);

    btn.SetBadgeCount(kSomeCount);
    CHECK(btn.GetBadgeCount() == kSomeCount);
    const RECT box = btn.GetBadgeRect();
    CHECK(box.right == kButtonRect.right);
    CHECK(box.left > kButtonRect.left);
    CHECK(box.top >= kButtonRect.top);
    CHECK(box.bottom < kButtonRect.bottom);
    //角标的垂直中心落在图标（18 像素，垂直居中）的上沿上
    const int iconTop = (kButtonRect.top + kButtonRect.bottom) / 2 - kIconHalf;
    CHECK((box.top + box.bottom) / 2 == iconTop);

    //按钮很矮时角标不越过按钮上沿
    balloonchatwnd::ChatIconButton low;
    low.SetRect(kLowRect);
    low.SetBadgeCount(kSomeCount);
    CHECK(low.GetBadgeRect().top == kLowRect.top);

    //按钮挪动：角标跟着走
    btn.SetRect(kMovedRect);
    const RECT moved = btn.GetBadgeRect();
    CHECK(moved.right == kMovedRect.right);
    CHECK(moved.right - moved.left == box.right - box.left);
}

TEST_CASE("CIB2 角标的数字与命中测试", "[chatwnd][sharebadge]")
{
    balloonchatwnd::ChatIconButton btn;
    btn.SetRect(kButtonRect);

    //0 与负数不显示，也不建角标
    btn.SetBadgeCount(0);
    CHECK(btn.GetBadgeCount() == 0);
    btn.SetBadgeCount(kNegativeCount);
    CHECK(btn.GetBadgeCount() == 0);

    btn.SetBadgeCount(kSomeCount);
    const RECT box = btn.GetBadgeRect();
    POINT center = { (box.left + box.right) / 2, (box.top + box.bottom) / 2 };
    //点在角标上仍是点这个按钮
    CHECK(btn.HitTest(center) == &btn);

    //清零：数字回到 0
    btn.SetBadgeCount(0);
    CHECK(btn.GetBadgeCount() == 0);
    CHECK(btn.HitTest(center) == &btn);
}
