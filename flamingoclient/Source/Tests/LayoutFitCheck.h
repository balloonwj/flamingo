/**
 *  布局单测共用的「排得下」检查：控件树按给定的内容区尺寸排版后，读取指定控件的下边、高度与
 *  下方留白，并与「空间充裕时」排出的结果比较，用来判断窗口的设计尺寸能否完整容下内容。
 *
 *  空间不够时有三种表现，都要能查出来：固定高度的控件被压扁；控件越过内容区下缘；控件越过所在
 *  容器的下内边距 —— 最后这种最隐蔽，控件仍在内容区之内，只是离窗口下缘变近（注册窗按精确尺寸
 *  设置时，提交按钮离下缘只剩 4 像素，设计是 16 像素）。适用于底部按钮上方有弹性占位、空间
 *  充裕时按钮贴着下内边距的布局：此时「下方留白与空间充裕时相同」即说明排得下。
 *
 *  典型用法（内容区高度 = 窗口高度 - 标题栏高度）：
 *      const layoutfit::FitResult r = layoutfit::MeasureAt(root.get(), kIdBtnSave, width, contentH);
 *      REQUIRE(r.m_found);
 *      CHECK(r.m_bottom <= contentH);
 *      CHECK(r.m_height == r.m_roomyHeight);
 *      CHECK(r.m_bottomGap == r.m_roomyBottomGap);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace layoutfit {

// 排参照布局时在内容区高度上额外加的高度（像素）。只要足够让所有固定高度的控件按原尺寸排下即可，
// 取值无特殊含义
const int kRoomyExtraHeight = 1000;

/**
 *  FitResult —— MeasureAt 的测量结果。
 */
struct FitResult
{
    bool m_found;        // 是否找到了指定 id 的控件；为 false 时其余字段无意义
    int  m_bottom;       // 按给定尺寸排版后该控件的下边（像素，相对内容区顶端）
    int  m_height;       // 按给定尺寸排版后该控件的高度（像素）
    int  m_roomyHeight;  // 内容区高度加上 kRoomyExtraHeight 后排版出的高度（像素），作为没有被压缩时的参照
    int  m_bottomGap;    // 按给定尺寸排版后该控件下边到内容区下缘的距离（像素）；越界时为负
    int  m_roomyBottomGap; // 空间充裕时排版出的同一距离（像素），作为下内边距没有被占用时的参照

    FitResult()
        : m_found(false)
        , m_bottom(0)
        , m_height(0)
        , m_roomyHeight(0)
        , m_bottomGap(0)
        , m_roomyBottomGap(0)
    {
    }
};

/**
 *  先按 width ×（height + kRoomyExtraHeight）排版一次读取参照高度，再按 width × height 排版读取
 *  下边与高度。最后一次排版用的是给定尺寸，调用方之后读到的控件矩形与本函数的测量一致。
 *    root：         控件树根（借用，不转移所有权），须非空。
 *    ctrlId：       要检查的控件 id，通常是窗口最底部的按钮。
 *    width / height：内容区宽、高（像素），即窗口尺寸扣掉标题栏之后的部分。
 *  @return 测量结果；找不到控件时 m_found 为 false。
 */
inline FitResult MeasureAt(balloonwjui::DuiControl* root, UINT ctrlId, int width, int height)
{
    FitResult result;
    RECT roomy = { 0, 0, width, height + kRoomyExtraHeight };
    root->ForceLayout(roomy);
    balloonwjui::DuiControl* ctrl = root->FindCtrlById(ctrlId);
    if (ctrl == nullptr)
    {
        return result;
    }
    const RECT rcRoomy = ctrl->GetRect();
    result.m_roomyHeight = rcRoomy.bottom - rcRoomy.top;
    result.m_roomyBottomGap = roomy.bottom - rcRoomy.bottom;

    RECT real = { 0, 0, width, height };
    root->ForceLayout(real);
    const RECT rc = ctrl->GetRect();
    result.m_found = true;
    result.m_bottom = rc.bottom;
    result.m_height = rc.bottom - rc.top;
    result.m_bottomGap = height - rc.bottom;
    return result;
}

} // namespace layoutfit
