/**
 *  帖子群窗口左栏的帖子列表，见 ThreadFeedList.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadFeedList.h"

#include <memory>

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

using balloonwjui::DuiControl;
using balloonwjui::DuiLabel;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;

namespace threadgroup {

namespace {

const int kListPadX = 10;          //卡片与列表左右两边的距离，单位：像素
const int kListPadBottom = 10;     //最后一行与列表底边的距离
const int kCardSpacing = 8;        //相邻两张卡片之间的距离
const int kFooterHeight = 28;      //最下面状态文字那一行的高
const int kNearBottomPx = 60;      //离内容底部不到这么多像素就算「滚到接近底部」
const COLORREF kFooterColor = RGB(0x9A, 0x9A, 0x9A);   //状态文字的颜色

} // namespace

CThreadFeedList::CThreadFeedList()
    : m_nearBottom(NULL)
    , m_nearUser(NULL)
    , m_contentH(0)
{
}

void CThreadFeedList::SetNearBottomCallback(NearBottomFn fn, void* user)
{
    m_nearBottom = fn;
    m_nearUser = user;
}

int CThreadFeedList::CardInnerWidth(int listWidth)
{
    const int width = listWidth - 2 * kListPadX - 2 * kCardPadX;
    return (width > 1) ? width : 1;
}

void CThreadFeedList::Rebuild(const std::vector<PostCardData>& cards, HFONT bodyFont, HFONT titleFont, int innerWidth,
                              int64_t selectedId, const std::wstring& footer)
{
    typedef DuiLayout::Hint Hint;
    const int scrollPos = GetScrollPos();
    const RECT rc = GetRect();

    std::unique_ptr<DuiVBox> content(new DuiVBox());
    content->SetPadding(kListPadX, 0, kListPadX, kListPadBottom);
    content->SetGap(kCardSpacing);
    m_cards.clear();
    int height = kListPadBottom;
    for (size_t i = 0; i < cards.size(); ++i)
    {
        std::unique_ptr<CThreadPostCard> card(new CThreadPostCard(static_cast<int>(i)));
        card->Build(cards[i], bodyFont, titleFont, innerWidth);
        card->SetSelected(cards[i].m_postId == selectedId && selectedId > 0);
        m_cards.push_back(card.get());
        const int cardHeight = cards[i].m_metrics.m_height;
        content->AddChild(std::unique_ptr<DuiControl>(card.release()), Hint().Fixed(cardHeight));
        height += cardHeight + ((i > 0) ? kCardSpacing : 0);
    }
    if (!footer.empty())
    {
        std::unique_ptr<DuiLabel> label(new DuiLabel());
        label->SetText(footer.c_str());
        label->SetTextColor(kFooterColor);
        label->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        content->AddChild(std::unique_ptr<DuiControl>(label.release()), Hint().Fixed(kFooterHeight));
        height += kFooterHeight + (cards.empty() ? 0 : kCardSpacing);
    }
    m_contentH = height;
    SetContent(std::unique_ptr<DuiControl>(content.release()));
    SetContentHeight(height);
    //内容换了一批：对自己按当前矩形重排，内容控件的矩形才会跟着新高度走（UI 约定第 12 条）
    ForceLayout(rc);
    SetScrollPos(scrollPos);
    Invalidate();
}

void CThreadFeedList::SetSelected(int64_t postId)
{
    for (size_t i = 0; i < m_cards.size(); ++i)
        m_cards[i]->SetSelected(postId > 0 && m_cards[i]->PostId() == postId);
}

int64_t CThreadFeedList::PostIdAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_cards.size()))
        return 0;
    return m_cards[index]->PostId();
}

int CThreadFeedList::CardCount() const
{
    return static_cast<int>(m_cards.size());
}

void CThreadFeedList::OnPaint(HDC hdc, const RECT& rcDirty)
{
    DuiScrollView::OnPaint(hdc, rcDirty);
    if (m_nearBottom == NULL || m_cards.empty() || !IsVisible())
        return;
    const RECT rc = GetRect();
    const int viewH = rc.bottom - rc.top;
    //内容比视口矮时也算到底：第一页没填满列表而后面还有帖子时，接着拉下一页
    if (GetScrollPos() + viewH >= m_contentH - kNearBottomPx)
        m_nearBottom(m_nearUser);
}

} // namespace threadgroup
