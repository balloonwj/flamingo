/**
 *  机器人管理界面共用的小控件，见 BotWidgets.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BotWidgets.h"

#include <ctype.h>
#include <string.h>

#include "../Core/BotStrings.h"
#include "../../Source/Common/I18n/I18n.h"   // TR_NOOP：预设颜色的名字
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"

using balloonwjui::DuiAvatar;
using balloonwjui::DuiButton;
using balloonwjui::DuiComboBox;
using balloonwjui::DuiControl;
using balloonwjui::DuiEdit;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiLayout;
using balloonwjui::DuiScrollView;
using balloonwjui::DuiVBox;

namespace flamingobot {

namespace {

typedef DuiLayout::Hint Hint;

//标准 DPI（窗口还没建出来时按它取字体）
const int kDefaultDpi = 96;

//头像上的字的颜色
const COLORREF kAvatarInitialsColor = RGB(0xFF, 0xFF, 0xFF);

//多行文字测量用的 DrawText 选项
const UINT kWrapFlags = DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT;

//分组标题的字号（磅）
const int kSectionPointSize = 10;

//头像底色的预设项：第一项是默认色（与 DuiAvatar 的品牌蓝一致）
const BotColorPreset kColorPresets[] =
{
    { "#2D6CDF", TR_NOOP(L"蓝色") },
    { "#43A047", TR_NOOP(L"绿色") },
    { "#FB8C00", TR_NOOP(L"橙色") },
    { "#E53935", TR_NOOP(L"红色") },
    { "#8E24AA", TR_NOOP(L"紫色") },
    { "#00ACC1", TR_NOOP(L"青色") },
    { "#546E7A", TR_NOOP(L"灰色") },
    { "#6D4C41", TR_NOOP(L"棕色") },
};

//一行文字的高度（像素）
int LineHeight(HDC hdc)
{
    TEXTMETRICW tm;
    ::GetTextMetricsW(hdc, &tm);
    return tm.tmHeight;
}

//两个颜色文字是否相同（不区分大小写）
bool SameHex(const std::string& a, const char* b)
{
    if (a.size() != strlen(b))
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const char x = static_cast<char>(toupper(static_cast<unsigned char>(a[i])));
        const char y = static_cast<char>(toupper(static_cast<unsigned char>(b[i])));
        if (x != y)
            return false;
    }
    return true;
}

} // namespace

const BotColorPreset* ColorPresets(size_t& count)
{
    count = sizeof(kColorPresets) / sizeof(kColorPresets[0]);
    return kColorPresets;
}

int ColorPresetIndex(const std::string& hex)
{
    size_t count = 0;
    const BotColorPreset* presets = ColorPresets(count);
    for (size_t i = 0; i < count; ++i)
    {
        if (SameHex(hex, presets[i].m_hex))
            return static_cast<int>(i);
    }
    return 0;
}

HFONT WindowFont(int dpi)
{
    return balloonwjui::DuiResMgr::Inst().GetDefaultFontForDpi(dpi > 0 ? dpi : kDefaultDpi);
}

int MeasureTextWidth(HFONT font, const std::wstring& text)
{
    HDC hdc = ::GetDC(NULL);
    HGDIOBJ old = (font != NULL) ? ::SelectObject(hdc, font) : NULL;
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    if (old != NULL)
        ::SelectObject(hdc, old);
    ::ReleaseDC(NULL, hdc);
    return size.cx;
}

int MeasureWrappedHeight(HFONT font, const std::wstring& text, int width)
{
    HDC hdc = ::GetDC(NULL);
    HGDIOBJ old = (font != NULL) ? ::SelectObject(hdc, font) : NULL;
    int height = LineHeight(hdc);
    if (!text.empty() && width > 0)
    {
        RECT rc = { 0, 0, width, 0 };
        ::DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rc, kWrapFlags);
        height = rc.bottom - rc.top;
    }
    if (old != NULL)
        ::SelectObject(hdc, old);
    ::ReleaseDC(NULL, hdc);
    return height;
}

std::unique_ptr<DuiLabel> MakeLabel(const std::wstring& text, COLORREF color, HFONT font, DWORD align)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetText(text.c_str());
    label->SetTextColor(color);
    label->SetTextAlign(align | DT_NOPREFIX);
    if ((align & DT_WORDBREAK) != 0)
        label->SetWordWrap(true);
    if (font != NULL)
        label->SetFont(font);
    return label;
}

std::unique_ptr<DuiButton> MakeButton(const std::wstring& text, UINT id, DuiButton::Variant variant, HFONT font,
                                      int& width)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetText(text.c_str());
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetTabStop(false);
    width = MeasureTextWidth(font, text) + 2 * kBotButtonTextPad;
    if (width < kBotMinButtonWidth)
        width = kBotMinButtonWidth;
    return button;
}

std::unique_ptr<DuiButton> MakeCheckBox(const std::wstring& text, UINT id, bool checked, HFONT font, int& width)
{
    std::unique_ptr<DuiButton> box(new DuiButton());
    box->SetButtonType(DuiButton::StyleCheckbox);
    //透明底、无边框，与客户端其他复选框（如 HistoryExportLayout）同一外观；不设时按默认样式画出按钮边框（2026-10-08 实机核对）
    box->SetVariant(DuiButton::Variant::Ghost);
    box->SetText(text.c_str());
    box->SetCtrlId(id);
    box->SetTextColor(kBotText);
    box->SetCheck(checked, false);
    box->SetTabStop(false);
    width = MeasureTextWidth(font, text) + kBotCheckGlyphWidth;
    return box;
}

std::unique_ptr<DuiEdit> MakeEdit(UINT id, const std::wstring& placeholder, const std::wstring& text, HFONT font,
                                  bool multiLine, int maxChars)
{
    std::unique_ptr<DuiEdit> edit(new DuiEdit());
    edit->SetCtrlId(id);
    if (multiLine)
    {
        edit->SetMultiLine(true);
        edit->SetWordWrap(true);
    }
    else
    {
        edit->SetVerticalCenter(true);
    }
    edit->SetTextColor(kBotText);
    if (font != NULL)
        edit->SetDefaultFontFromHFONT(font);
    if (maxChars > 0)
        edit->SetMaxLength(maxChars);
    edit->SetPlaceholder(placeholder.c_str());
    edit->SetTextNoNotify(text.c_str());
    return edit;
}

std::unique_ptr<DuiComboBox> MakeCombo(UINT id, const std::vector<std::wstring>& items, int sel)
{
    std::unique_ptr<DuiComboBox> combo(new DuiComboBox());
    combo->SetCtrlId(id);
    for (size_t i = 0; i < items.size(); ++i)
        combo->AddString(items[i].c_str());
    if (sel < 0 || sel >= static_cast<int>(items.size()))
        sel = 0;
    if (!items.empty())
        combo->SetCurSel(sel, false);
    return combo;
}

std::wstring AvatarInitials(const std::wstring& glyph, const std::wstring& name)
{
    const std::wstring g = TrimWide(glyph);
    if (g.empty())
    {
        const std::wstring n = TrimWide(name);
        return n.empty() ? std::wstring(L"?") : n.substr(0, 1);
    }
    //两个字时中间加空格：DuiAvatar 对多个词取首、末两个词的首字，这样两个字都画出来
    if (WideCharCount(g) >= 2 && g.size() >= 2)
        return g.substr(0, 1) + L" " + g.substr(g.size() - 1, 1);
    return g;
}

std::unique_ptr<DuiAvatar> MakeBotAvatar(const std::wstring& glyph, const std::wstring& name, const std::wstring& color)
{
    std::unique_ptr<DuiAvatar> avatar(new DuiAvatar());
    avatar->SetShape(DuiAvatar::ShapeRoundRect);
    avatar->SetCornerRadius(kBotAvatarCorner);
    avatar->SetInitialsColor(kAvatarInitialsColor);
    ApplyBotAvatar(avatar.get(), glyph, name, color);
    return avatar;
}

void ApplyBotAvatar(DuiAvatar* avatar, const std::wstring& glyph, const std::wstring& name, const std::wstring& color)
{
    if (avatar == NULL)
        return;
    uint32_t rgb = 0;
    const COLORREF bg = ParseHexColor(WideToUtf8(color), rgb) ? static_cast<COLORREF>(rgb) : kBotDefaultAvatar;
    avatar->SetFallbackBgColor(bg);
    avatar->SetName(AvatarInitials(glyph, name).c_str());
    avatar->Invalidate();
}

std::unique_ptr<DuiVBox> MakeCard()
{
    std::unique_ptr<DuiVBox> card(new DuiVBox());
    card->SetBgColor(kBotCardBg);
    card->SetBorderColor(kBotCardBorder);
    card->SetCornerRadius(kBotCardRadius);
    card->SetPadding(kBotCardPad);
    return card;
}

std::unique_ptr<DuiHBox> MakeSectionRow(const std::wstring& title, const std::wstring& buttonText, UINT buttonId,
                                        HFONT font, DuiButton** button)
{
    if (button != NULL)
        *button = NULL;
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(kBotRowGap);
    std::unique_ptr<DuiLabel> label = MakeLabel(title, kBotText, NULL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    label->SetTextPointSize(kSectionPointSize, true);
    row->AddChild(std::unique_ptr<DuiControl>(label.release()), Hint().Weight(1));
    if (!buttonText.empty())
    {
        int width = 0;
        std::unique_ptr<DuiButton> btn = MakeButton(buttonText, buttonId, DuiButton::Variant::Outlined, font, width);
        if (button != NULL)
            *button = btn.get();
        row->AddChild(std::unique_ptr<DuiControl>(btn.release()),
                      Hint().Fixed(width, kBotButtonHeight).AlignC(DuiLayout::AlignCenter));
    }
    return row;
}

std::unique_ptr<DuiHBox> MakeFieldRow(const std::wstring& label, HFONT font, int labelWidth,
                                      std::unique_ptr<DuiControl> field)
{
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(kBotRowGap);
    row->AddChild(std::unique_ptr<DuiControl>(MakeLabel(label, kBotSubText, font,
                                                        DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                  Hint().Fixed(labelWidth));
    row->AddChild(std::move(field), Hint().Weight(1));
    return row;
}

int FieldLabelWidth(HFONT font, const std::vector<std::wstring>& labels)
{
    int width = kBotFieldLabelWidth;
    for (size_t i = 0; i < labels.size(); ++i)
    {
        const int w = MeasureTextWidth(font, labels[i]);
        if (w > width)
            width = w;
    }
    return width;
}

std::unique_ptr<DuiVBox> BuildScrollFrame(HFONT font, const std::vector<FooterButtonSpec>& buttons,
                                          ScrollFrameControls& out)
{
    out = ScrollFrameControls();
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kBotBg);
    root->SetPadding(kBotPadX, kBotPadTop, kBotPadX, kBotPadBottom);
    root->SetGap(kBotRowGap);

    std::unique_ptr<DuiVBox> content(new DuiVBox());
    content->SetGap(kBotRowGap);
    content->SetPadding(0, 0, kBotScrollGutter, 0);
    out.m_content = content.get();
    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    scroll->SetContent(std::unique_ptr<DuiControl>(content.release()));
    scroll->SetAutoContentHeight(true);
    out.m_scroll = scroll.get();
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));

    std::unique_ptr<DuiHBox> footer(new DuiHBox());
    footer->SetGap(kBotRowGap);
    std::unique_ptr<DuiLabel> status = MakeLabel(std::wstring(), kBotSubText, font,
                                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    out.m_status = status.get();
    footer->AddChild(std::unique_ptr<DuiControl>(status.release()), Hint().Weight(1));
    for (size_t i = 0; i < buttons.size(); ++i)
    {
        int width = 0;
        std::unique_ptr<DuiButton> button = MakeButton(buttons[i].m_text, buttons[i].m_id, buttons[i].m_variant, font,
                                                       width);
        out.m_buttons.push_back(button.get());
        footer->AddChild(std::unique_ptr<DuiControl>(button.release()), Hint().Fixed(width, kBotButtonHeight));
    }
    root->AddChild(std::unique_ptr<DuiControl>(footer.release()), Hint().Fixed(kBotButtonHeight));
    return root;
}

void RelayoutScroll(DuiScrollView* scroll)
{
    if (scroll == NULL)
        return;
    DuiControl* content = scroll->GetContent();
    if (content != NULL)
        content->Invalidate();
    scroll->ForceLayout(scroll->GetRect());
    //滚动视图排版时对内容调的是 SetRect，内容的矩形没变时（如重建后的列表总高度不变）直接返回、不会给新建的子控件排版，
    //窗口里就什么都画不出来（2026-10-08 实机联调：群机器人管理窗点「在本群关闭」后整片空白；用例 BOTL4）。
    //所以再对内容 ForceLayout 一次（开发约定文档 UI 约定第 12 条）
    if (content != NULL)
    {
        content->ForceLayout(content->GetRect());
        content->Invalidate();
    }
    scroll->Invalidate();
}

void SetStatusText(DuiLabel* status, const std::wstring& text, bool isError)
{
    if (status == NULL)
        return;
    status->SetTextColor(isError ? kBotError : kBotSubText);
    status->SetText(text.c_str());
}

void RemoveAllChildren(DuiControl* box)
{
    if (box == NULL)
        return;
    while (!box->Children().empty())
        box->RemoveChild(box->Children().back().get());
}

std::wstring EditText(const DuiEdit* edit)
{
    if (edit == NULL)
        return std::wstring();
    const CString text = edit->GetText();
    return std::wstring(static_cast<LPCWSTR>(text));
}

} // namespace flamingobot
