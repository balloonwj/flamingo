/**
 *  帖子群窗口里卡片与详情共用的小控件建法（docs/帖子群方案.md 4.6）：文字标签、胶囊（圆角底色 + 居中文字）、
 *  互动标签一行（「赞 N」与表情图标 + 人数，本人加过的高亮），以及测量文字宽度。都由 balloonui 现成控件组合而成。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法：
 *      int width = 0;
 *      std::unique_ptr<balloonwjui::DuiVBox> pill = threadgroup::MakePill(L"置顶", bg, fg, font, radius, padding, width);
 *      threadgroup::AddReactionChips(footRow, chips, font);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

namespace threadgroup {

struct PostCardChip;

//互动标签的高度，单位：像素
const int kReactionChipHeight = 22;

//可点击胶囊发出的自定义通知码：鼠标进入、离开（ctrlId 为胶囊的控件 id）。另开取值段，避开其它控件从 DUIN_CUSTOM + 1
//起算的自定义码（UI 约定第 4 条）；单击仍发 DUIN_CLICK
const UINT kNotifyPillEnter = balloonwjui::DUIN_CUSTOM + 140;
const UINT kNotifyPillLeave = balloonwjui::DUIN_CUSTOM + 141;

/**
 *  可点击的胶囊：圆角底色的容器，整块接管命中；单击发 DUIN_CLICK，鼠标进入、离开分别发 kNotifyPillEnter、kNotifyPillLeave
 *  （窗口据此拉互动名单、显示提示）；悬停时底色加深一点并重画（UI 约定第 9 条）。只覆写命中与鼠标事件（子类化扩展，不改 balloonui）。
 */
class CThreadClickPill : public balloonwjui::DuiVBox
{
public:
    /**
     *  @param bg      平时的底色
     *  @param hoverBg 悬停时的底色
     */
    CThreadClickPill(COLORREF bg, COLORREF hoverBg);

    virtual balloonwjui::DuiControl* HitTest(POINT ptHostClient) override;
    virtual bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    virtual bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    virtual bool OnMouseEnter() override;
    virtual bool OnMouseLeave() override;

private:
    COLORREF m_bg;       //平时的底色
    COLORREF m_hoverBg;  //悬停时的底色
    bool     m_pressed;  //鼠标是否在胶囊上按下（还没抬起）
};

/**
 *  一段文字的宽度，单位：像素。
 *  @param font 文字所用的字体（借用）；为 NULL 时按设备上下文的默认字体测量
 */
int MeasureTextWidthPx(HFONT font, const std::wstring& text);

/**
 *  建一个单行或多行文字标签。
 *  @param font  字体（借用）；为 NULL 时用 balloonui 的默认字体
 *  @param align DrawText 对齐选项
 */
std::unique_ptr<balloonwjui::DuiLabel> MakeTextLabel(const std::wstring& text, COLORREF color, HFONT font, DWORD align);

/**
 *  建一个胶囊：圆角底色的容器里放一行居中的文字。
 *  @param width [出参] 正好放下文字的宽度（文字宽度加两侧内边距），调用方据此放进布局
 */
std::unique_ptr<balloonwjui::DuiVBox> MakePill(const std::wstring& text, COLORREF bg, COLORREF fg, HFONT font, int radius,
                                               int padding, int& width);

/**
 *  把互动标签逐个加进一行（每个标签高 kReactionChipHeight、垂直居中，标签之间的距离由这一行的 gap 决定）。
 *  「赞」显示为「赞 N」（没人点过时只写「赞」）；表情显示为图标 + 人数，图标读不出来时只显示人数。
 *  @param row   要加进去的那一行（借用）
 *  @param chips 互动标签，按显示顺序
 *  @param font  文字字体（借用）
 *  @param ids   各标签的控件 id（与 chips 同序）；为 NULL 或某一项为 0 时那个标签只显示、不能点击
 */
void AddReactionChips(balloonwjui::DuiHBox* row, const std::vector<PostCardChip>& chips, HFONT font,
                      const std::vector<UINT>* ids = NULL);

} // namespace threadgroup
