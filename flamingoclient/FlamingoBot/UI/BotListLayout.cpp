/**
 *  群机器人管理窗与「我的机器人」窗口共用的列表排版，见 BotListLayout.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BotListLayout.h"

using balloonwjui::DuiAvatar;
using balloonwjui::DuiButton;
using balloonwjui::DuiControl;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;

namespace flamingobot {

namespace {

typedef DuiLayout::Hint Hint;

//卡片里文字行之间的距离，单位：像素
const int kTextGap = 2;

} // namespace

void AddBotRow(DuiVBox* content, const BotRowView& view, HFONT font, int width)
{
    if (content == NULL)
        return;
    //右侧按钮：先算出总宽，中间文字的宽度据此算出
    std::vector<std::unique_ptr<DuiButton> > buttons;
    std::vector<int> widths;
    int buttonsWidth = 0;
    for (size_t i = 0; i < view.m_buttons.size(); ++i)
    {
        int w = 0;
        std::unique_ptr<DuiButton> button = MakeButton(view.m_buttons[i].m_text, view.m_buttons[i].m_id,
                                                       DuiButton::Variant::Text, font, w);
        button->SetEnabled(view.m_buttons[i].m_enabled);
        buttons.push_back(std::move(button));
        widths.push_back(w);
        buttonsWidth += w + kBotRowGap;
    }
    const int textWidth = width - 2 * kBotCardPad - kBotAvatarSize - kBotRowGap - buttonsWidth;

    //中间：名称与状态一行，简介、用法说明各自按宽度换行
    std::unique_ptr<DuiVBox> texts(new DuiVBox());
    texts->SetGap(kTextGap);
    int textHeight = kBotLabelHeight;
    std::unique_ptr<DuiHBox> titleRow(new DuiHBox());
    titleRow->SetGap(kBotRowGap);
    const int stateWidth = MeasureTextWidth(font, view.m_state);
    titleRow->AddChild(std::unique_ptr<DuiControl>(MakeLabel(view.m_name, kBotText, font,
                                                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS).release()),
                       Hint().Weight(1));
    titleRow->AddChild(std::unique_ptr<DuiControl>(MakeLabel(view.m_state, view.m_stateColor, font,
                                                             DT_RIGHT | DT_VCENTER | DT_SINGLELINE).release()),
                       Hint().Fixed(stateWidth));
    texts->AddChild(std::unique_ptr<DuiControl>(titleRow.release()), Hint().Fixed(kBotLabelHeight));
    if (!view.m_desc.empty())
    {
        const int h = MeasureWrappedHeight(font, view.m_desc, textWidth);
        texts->AddChild(std::unique_ptr<DuiControl>(MakeLabel(view.m_desc, kBotSubText, font,
                                                              DT_LEFT | DT_TOP | DT_WORDBREAK).release()),
                        Hint().Fixed(h));
        textHeight += kTextGap + h;
    }
    if (!view.m_detail.empty())
    {
        const int h = MeasureWrappedHeight(font, view.m_detail, textWidth);
        texts->AddChild(std::unique_ptr<DuiControl>(MakeLabel(view.m_detail, kBotFaint, font,
                                                              DT_LEFT | DT_TOP | DT_WORDBREAK).release()),
                        Hint().Fixed(h));
        textHeight += kTextGap + h;
    }

    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(kBotRowGap);
    std::unique_ptr<DuiAvatar> avatar = MakeBotAvatar(view.m_glyph, view.m_name, view.m_color);
    row->AddChild(std::unique_ptr<DuiControl>(avatar.release()),
                  Hint().Fixed(kBotAvatarSize, kBotAvatarSize).AlignC(DuiLayout::AlignNear));
    row->AddChild(std::unique_ptr<DuiControl>(texts.release()), Hint().Weight(1));
    for (size_t i = 0; i < buttons.size(); ++i)
    {
        row->AddChild(std::unique_ptr<DuiControl>(buttons[i].release()),
                      Hint().Fixed(widths[i], kBotButtonHeight).AlignC(DuiLayout::AlignNear));
    }
    int rowHeight = (textHeight > kBotAvatarSize) ? textHeight : kBotAvatarSize;
    if (rowHeight < kBotButtonHeight)
        rowHeight = kBotButtonHeight;
    std::unique_ptr<DuiVBox> card = MakeCard();
    card->AddChild(std::unique_ptr<DuiControl>(row.release()), Hint().Fixed(rowHeight));
    content->AddChild(std::unique_ptr<DuiControl>(card.release()), Hint().Fixed(rowHeight + 2 * kBotCardPad));
}

void AddListSection(DuiVBox* content, const std::wstring& title, HFONT font)
{
    if (content == NULL)
        return;
    content->AddChild(std::unique_ptr<DuiControl>(MakeSectionRow(title, std::wstring(), 0, font, NULL).release()),
                      Hint().Fixed(kBotSectionHeight));
}

void AddListNote(DuiVBox* content, const std::wstring& text, COLORREF color, HFONT font, int width)
{
    if (content == NULL || text.empty())
        return;
    const int height = MeasureWrappedHeight(font, text, width);
    content->AddChild(std::unique_ptr<DuiControl>(MakeLabel(text, color, font, DT_LEFT | DT_TOP | DT_WORDBREAK).release()),
                      Hint().Fixed(height));
}

} // namespace flamingobot
