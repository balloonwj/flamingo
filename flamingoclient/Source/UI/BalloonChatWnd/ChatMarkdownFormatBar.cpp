/**
 *  ChatMarkdownFormatBar.cpp
 *
 *  Markdown 格式条横向布局的实现，说明见 ChatMarkdownFormatBar.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "ChatMarkdownFormatBar.h"

namespace balloonchatwnd {

ChatMarkdownFormatBar::ChatMarkdownFormatBar()
    : m_collapsibleCount(0)
    , m_hiddenCount(0)
{
}

void ChatMarkdownFormatBar::SetCollapsibleCount(int count)
{
    m_collapsibleCount = (count > 0) ? count : 0;
}

int ChatMarkdownFormatBar::GetHiddenCount() const
{
    return m_hiddenCount;
}

int ChatMarkdownFormatBar::FixedWidthOf(balloonwjui::DuiControl* child) const
{
    //与 DuiHBox 第一趟统计的规则一致：固定档取指定像素，自动档取子控件的期望宽度，按权重分配的只计外边距
    const Hint h = HintFor(child);
    int width = h.marginL + h.marginR;
    if (h.fixedMain >= 0)
    {
        width += h.fixedMain;
    }
    else if (h.fixedMain == Hint::kAutoMain)
    {
        const SIZE desired = child->GetDesiredSize();
        if (desired.cx > 0)
            width += (int)desired.cx;
    }
    return width;
}

void ChatMarkdownFormatBar::Layout(const RECT& rcAvail)
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    const int collapsible = (m_collapsibleCount < (int)kids.size()) ? m_collapsibleCount : (int)kids.size();

    //所有子控件都显示时需要的宽度：固定宽度之和加上子控件之间的间距
    int needed = 0;
    int visibleCount = 0;
    for (size_t i = 0; i < kids.size(); ++i)
    {
        //可隐藏的子控件按「全部显示」计算；其余子控件按调用方设置的显隐计算
        if ((int)i >= collapsible && !kids[i]->IsVisible())
            continue;
        needed += FixedWidthOf(kids[i].get());
        ++visibleCount;
    }
    if (visibleCount > 1)
        needed += m_gap * (visibleCount - 1);

    //放不下时从最后一个可隐藏的子控件往前隐藏，每隐藏一个省下它的宽度与一个间距
    const int innerW = (rcAvail.right - rcAvail.left) - m_padL - m_padR;
    int hidden = 0;
    while (needed > innerW && hidden < collapsible)
    {
        needed -= FixedWidthOf(kids[collapsible - 1 - hidden].get()) + m_gap;
        ++hidden;
    }

    //只在显隐确实变化时调用 SetVisible，避免每次排版都重绘
    for (int i = 0; i < collapsible; ++i)
    {
        const bool visible = (i < collapsible - hidden);
        if (kids[i]->IsVisible() != visible)
            kids[i]->SetVisible(visible);
    }
    m_hiddenCount = hidden;

    balloonwjui::DuiHBox::Layout(rcAvail);
}

} // namespace balloonchatwnd
