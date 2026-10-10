/**
 *  GroupQaCardList.cpp —— 群聊窗「问答」页签的卡片列表，说明见 GroupQaCardList.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "GroupQaCardList.h"

#include <memory>

#include "GroupQaModel.h"                     // QaCardCtrlId / kQaCardMaxCount / kQaMaxLinksPerCard
#include "../BalloonMemo/MemoCardList.h"      // CMemoCardScrollView：滚动视口与备忘页签共用
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../Common/I18n/I18n.h"           // TR：界面文字的多语言查找

using namespace balloonwjui;

namespace balloonchatwnd {

namespace {

// ---- 颜色：与「公告」「备忘」页签的卡片同一套浅底与描边 ----
const COLORREF kCardBg       = RGB(0xFA, 0xF9, 0xF6);   // 卡片浅底
const COLORREF kCardBorder   = RGB(0xEC, 0xEA, 0xE6);   // 卡片描边
const COLORREF kQuestionColor = RGB(0x2A, 0x2A, 0x2A);  // 问题
const COLORREF kAnswerColor  = RGB(0x55, 0x55, 0x55);   // 答案
const COLORREF kMetaColor    = RGB(0x8A, 0x8A, 0x8A);   // 来源与修改人
const COLORREF kLinkColor    = RGB(0x1E, 0x6F, 0xFF);   // 网址链接（品牌蓝）

// ---- 版式（逻辑像素）----
const int   kCardPadH    = 12;    // 卡片左右内边距
const int   kCardPadV    = 8;     // 卡片上下内边距
const int   kCardLineGap = 4;     // 卡片内各行之间的间距
const int   kCardRadius  = 8;     // 卡片圆角
const float kCardBorderW = 1.0f;  // 卡片描边线宽
const int   kMetaH       = 18;    // 来源与修改人一行的高
const int   kLinkH       = 20;    // 每个网址链接一行的高
const int   kActionsH    = 24;    // 底部按钮一行的高
const int   kSendW       = 64;    // 「发到群里」的宽
const int   kActionW     = 40;    // 「修改」「删除」各自的宽
const int   kActionGap   = 2;     // 按钮之间的间距
const int   kListGap     = 8;     // 卡片之间的间距
const int   kBothSides   = 2;     // 上下两侧（内边距按两侧计）
const int   kMinTextH    = 18;    // 问题、答案量出的高度不足一行时按一行算

// ---- 字号（磅）----
const int kQuestionPt = 10;   // 问题（加粗）
const int kLinePt     = 9;    // 其余各行与按钮

// 建一个折行的多行文字
std::unique_ptr<DuiLabel> MakeWrapLabel(UINT id, const std::wstring& text, COLORREF color, int pt, bool bold)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetCtrlId(id);
    label->SetText(text.c_str());
    label->SetTextColor(color);
    label->SetTextPointSize(pt, bold);
    label->SetWordWrap(true);
    label->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    return label;
}

// 建一行单行文字；放不下时末尾省略
std::unique_ptr<DuiLabel> MakeLine(UINT id, const std::wstring& text, COLORREF color)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetCtrlId(id);
    label->SetText(text.c_str());
    label->SetTextColor(color);
    label->SetTextPointSize(kLinePt, false);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    return label;
}

// 建一个文字按钮
std::unique_ptr<DuiControl> MakeAction(UINT id, LPCTSTR text)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(DuiButton::Variant::Text);
    button->SetCtrlId(id);
    button->SetText(text);
    button->SetTextPointSize(kLinePt);
    return std::unique_ptr<DuiControl>(button.release());
}

// 量一段折行文字在给定宽度下的高度；不足一行按一行算
int MeasureWrapped(const std::wstring& text, int pt, bool bold, int width)
{
    std::unique_ptr<DuiLabel> probe = MakeWrapLabel(0, text, kAnswerColor, pt, bold);
    const int h = probe->MeasureHeight(width);
    return (h < kMinTextH) ? kMinTextH : h;
}

// 卡片里可见的网址个数（最多 kQaMaxLinksPerCard 个）
size_t LinkCount(const QaCardText& card)
{
    return (card.m_links.size() < kQaMaxLinksPerCard) ? card.m_links.size() : kQaMaxLinksPerCard;
}

// 建第 index 张卡片
std::unique_ptr<DuiControl> BuildCard(int index, const QaCardText& card, int cardWidth)
{
    typedef DuiLayout::Hint Hint;
    const int textWidth = cardWidth - kCardPadH * kBothSides;

    std::unique_ptr<DuiVBox> body(new DuiVBox());
    body->SetCtrlId(QaCardCtrlId(index, kQaCardPartBody));
    body->SetBgColor(kCardBg);
    body->SetBorderColor(kCardBorder);
    body->SetBorderWidth(kCardBorderW);
    body->SetCornerRadius(kCardRadius);
    body->SetPadding(kCardPadH, kCardPadV, kCardPadH, kCardPadV);
    body->SetGap(kCardLineGap);

    body->AddChild(std::unique_ptr<DuiControl>(MakeWrapLabel(QaCardCtrlId(index, kQaCardPartQuestion), card.m_question,
                                                             kQuestionColor, kQuestionPt, true).release()),
                   Hint().Fixed(MeasureWrapped(card.m_question, kQuestionPt, true, textWidth)));
    if (!card.m_meta.empty())
    {
        body->AddChild(std::unique_ptr<DuiControl>(MakeLine(QaCardCtrlId(index, kQaCardPartMeta), card.m_meta,
                                                            kMetaColor).release()),
                       Hint().Fixed(kMetaH));
    }
    body->AddChild(std::unique_ptr<DuiControl>(MakeWrapLabel(QaCardCtrlId(index, kQaCardPartAnswer), card.m_answer,
                                                             kAnswerColor, kLinePt, false).release()),
                   Hint().Fixed(MeasureWrapped(card.m_answer, kLinePt, false, textWidth)));

    //答案里的网址：链接模式，点了用浏览器打开（ExtractQaUrls 只取 http / https）
    const size_t links = LinkCount(card);
    for (size_t k = 0; k < links; ++k)
    {
        std::unique_ptr<DuiLabel> link = MakeLine(QaCardCtrlId(index, kQaCardPartLinkFirst + static_cast<int>(k)),
                                                  card.m_links[k], kLinkColor);
        link->SetMode(DuiLabel::ModeLink);
        link->SetLinkColor(kLinkColor);
        link->SetUrl(card.m_links[k].c_str());
        link->SetAutoNavigate(true);
        body->AddChild(std::unique_ptr<DuiControl>(link.release()), Hint().Fixed(kLinkH));
    }

    //底部按钮：「发到群里」靠左；能修改的卡片在右侧再放「修改」「删除」
    std::unique_ptr<DuiHBox> actions(new DuiHBox());
    actions->SetCtrlId(QaCardCtrlId(index, kQaCardPartActions));
    actions->SetGap(kActionGap);
    actions->AddChild(MakeAction(QaCardCtrlId(index, kQaCardPartSend), TR(L"发到群里")), Hint().Fixed(kSendW));
    actions->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    if (card.m_editable)
    {
        actions->AddChild(MakeAction(QaCardCtrlId(index, kQaCardPartEdit), TR(L"修改")), Hint().Fixed(kActionW));
        actions->AddChild(MakeAction(QaCardCtrlId(index, kQaCardPartDelete), TR(L"删除")), Hint().Fixed(kActionW));
    }
    body->AddChild(std::unique_ptr<DuiControl>(actions.release()), Hint().Fixed(kActionsH));
    return std::unique_ptr<DuiControl>(body.release());
}

} // anonymous namespace

int QaCardHeight(const QaCardText& card, int cardWidth)
{
    const int textWidth = cardWidth - kCardPadH * kBothSides;
    int height = kCardPadV * kBothSides;
    int rows = 0;
    height += MeasureWrapped(card.m_question, kQuestionPt, true, textWidth);
    ++rows;
    if (!card.m_meta.empty())
    {
        height += kMetaH;
        ++rows;
    }
    height += MeasureWrapped(card.m_answer, kLinePt, false, textWidth);
    ++rows;
    const int links = static_cast<int>(LinkCount(card));
    height += links * kLinkH;
    rows += links;
    height += kActionsH;
    ++rows;
    //rows 行之间有 rows - 1 个间距
    height += (rows - 1) * kCardLineGap;
    return height;
}

int SetQaCards(DuiScrollView* scroll, const std::vector<QaCardText>& cards, COLORREF outerBg)
{
    if (scroll == NULL)
    {
        return -1;
    }
    const RECT rc = scroll->GetRect();
    //卡片宽度：视口宽度减去滚动条那一条（内容超出时 CMemoCardScrollView 会让出这一条，按让出后的宽度量高，
    //内容不超出时卡片会略宽一点，折行只会更少，高度不会不够）
    const int cardWidth = (rc.right - rc.left) - balloonmemo::kMemoScrollGutter;
    if (cardWidth <= 0)
    {
        return -1;
    }
    typedef DuiLayout::Hint Hint;
    balloonmemo::CMemoCardScrollView* memoScroll = dynamic_cast<balloonmemo::CMemoCardScrollView*>(scroll);
    if (memoScroll != NULL)
    {
        memoScroll->SetOuterBg(outerBg);
    }
    std::unique_ptr<DuiVBox> list(new DuiVBox());
    list->SetGap(kListGap);
    const size_t count = (cards.size() < (size_t)kQaCardMaxCount) ? cards.size() : (size_t)kQaCardMaxCount;
    for (size_t i = 0; i < count; ++i)
    {
        list->AddChild(BuildCard((int)i, cards[i], cardWidth), Hint().Fixed(QaCardHeight(cards[i], cardWidth)));
    }
    const int keepPos = scroll->GetScrollPos();
    scroll->SetContent(std::unique_ptr<DuiControl>(list.release()));
    scroll->ForceLayout(scroll->GetRect());
    //内容变短后原位置可能超出范围，由 SetScrollPos 收回到合法范围
    scroll->SetScrollPos(keepPos);
    scroll->Invalidate();
    return (int)count;
}

} // namespace balloonchatwnd
