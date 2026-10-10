/**
 *  GroupLinkLayout.cpp —— 「生成加群链接」窗口的客户区控件树，见 GroupLinkLayout.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用
 *  （布局单测按设计尺寸排版）。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-07
 */

// ---- ATL / WTL 头文件要求的前置依赖，与 MemoEditLayout.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <string>

#include "GroupLinkLayout.h"
#include "GroupLinkFlow.h"   // 有效期下拉的选项文字与默认项
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;

namespace balloonmain {

namespace {

// ---- 颜色 ----
const COLORREF kColorWhite = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorTitle = RGB(0x2A, 0x2A, 0x2A);   // 群名
const COLORREF kColorHint  = RGB(0x8A, 0x8A, 0x8A);   // 说明、「有效期」
const COLORREF kColorUrl   = RGB(0x1E, 0x6F, 0xFF);   // 链接文字（品牌蓝）
const COLORREF kColorUrlBg = RGB(0xF5, 0xF6, 0xFA);   // 链接文字的浅底

// ---- 版式（逻辑像素）----
const int kPadH       = 20;   // 客户区左右内边距
const int kPadTop     = 14;   // 客户区上内边距
const int kPadBottom  = 16;   // 客户区下内边距
const int kRootGap    = 8;    // 各行之间的间距
const int kNameH      = 22;   // 群名一行的高
const int kHintH      = 40;   // 说明（两行）的高
const int kRowH       = 32;   // 「有效期」一行与链接一行的高
const int kStatusH    = 20;   // 状态行的高
const int kButtonH    = 34;   // 按钮行的高
const int kLabelW     = 56;   // 「有效期」的宽
const int kComboW     = 120;  // 有效期下拉的宽
const int kCreateW    = 96;   // 「生成链接」的宽
const int kCopyW      = 96;   // 「复制链接」的宽
const int kCloseW     = 80;   // 「关闭」的宽
const int kRowGap     = 8;    // 行内控件之间的间距
const int kUrlPadH    = 8;    // 链接文字的左右内边距
const int kUrlRadius  = 4;    // 链接文字浅底的圆角
// 群名字号（磅），加粗
const int kNamePt     = 11;

// ---- 「我生成的链接」列表（逻辑像素）----
const int kSectionTitleH  = 20;   // 「我生成的链接」标题的高
const int kListRowH       = 56;   // 一行的高：上下内边距加两行文字
const int kListRowGap     = 6;    // 行与行之间的间距
const int kListRowPadH    = 10;   // 行的左右内边距
const int kListRowPadV    = 8;    // 行的上下内边距
const int kListRowRadius  = 4;    // 行浅底的圆角
const int kListTextGap    = 2;    // 行内两行文字之间的间距
const int kListUrlH       = 20;   // 行内链接文字的高
const int kListMetaH      = 18;   // 行内「生成时间 · 到期说明」的高
const int kListButtonW    = 56;   // 行内「复制」「作废」的宽
const int kListButtonH    = 28;   // 行内「复制」「作废」的高
const int kPlaceholderH   = 40;   // 占位文字的高（两行）

std::unique_ptr<DuiLabel> MakeLabel(UINT id, LPCTSTR text, COLORREF color, UINT align)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetCtrlId(id);
    label->SetText(text);
    label->SetTextColor(color);
    label->SetTextAlign(align);
    return label;
}

std::unique_ptr<DuiControl> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetCtrlId(id);
    button->SetText(text);
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(variant);
    return std::unique_ptr<DuiControl>(button.release());
}

// 造「我生成的链接」的第 index 行：左边链接文字与说明，右边「复制」「作废」。
// 浅底与圆角只有 DuiVBox 提供，所以外层是一个 DuiVBox 卡片，里面再放横排的一行（与群管理窗「加群链接」页的写法相同）
std::unique_ptr<DuiControl> MakeMyLinkRow(const MyGroupLinkRow& link, int index)
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> card(new DuiVBox());
    card->SetCtrlId(kGroupLinkIdRowBase + index);
    card->SetBgColor(kColorUrlBg);
    card->SetCornerRadius(kListRowRadius);
    card->SetPadding(kListRowPadH, kListRowPadV, kListRowPadH, kListRowPadV);
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(kRowGap);

    std::unique_ptr<DuiVBox> col(new DuiVBox());
    col->SetGap(kListTextGap);
    col->AddChild(std::unique_ptr<DuiControl>(MakeLabel(0, link.m_url.c_str(), kColorUrl,
                                                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX).release()),
                  Hint().Fixed(kListUrlH));
    col->AddChild(std::unique_ptr<DuiControl>(MakeLabel(0, link.m_metaText.c_str(), kColorHint,
                                                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX).release()),
                  Hint().Fixed(kListMetaH));
    row->AddChild(std::unique_ptr<DuiControl>(col.release()), Hint().Weight(1));

    row->AddChild(MakeButton(kGroupLinkIdRowCopyBase + index, TR(L"复制"), DuiButton::Variant::Default),
                  Hint().Fixed(kListButtonW, kListButtonH).AlignC(DuiLayout::AlignCenter));
    row->AddChild(MakeButton(kGroupLinkIdRowRevokeBase + index, TR(L"作废"), DuiButton::Variant::Default),
                  Hint().Fixed(kListButtonW, kListButtonH).AlignC(DuiLayout::AlignCenter));
    card->AddChild(std::unique_ptr<DuiControl>(row.release()), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(card.release());
}

// 造列表的内容：有链接时逐行排列，没有时只放一行占位文字。右边留出滚动条命中带的宽度，
// 滚动条出现时不会盖住行末的「作废」按钮（命中带内的点归滚动条）
std::unique_ptr<DuiControl> MakeMyLinkListContent(const MyGroupLinkList& list)
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> content(new DuiVBox());
    content->SetPadding(0, 0, DuiScrollBar::kOverlayBandPx, 0);
    content->SetGap(kListRowGap);
    const std::wstring placeholder = MyGroupLinksPlaceholderText(list);
    if (!placeholder.empty())
    {
        std::unique_ptr<DuiLabel> label = MakeLabel(kGroupLinkIdMyLinksPlaceholder, placeholder.c_str(), kColorHint,
                                                    DT_LEFT | DT_TOP | DT_WORDBREAK);
        label->SetWordWrap(true);
        content->AddChild(std::unique_ptr<DuiControl>(label.release()), Hint().Fixed(kPlaceholderH));
        return std::unique_ptr<DuiControl>(content.release());
    }
    for (size_t i = 0; i < list.m_rows.size() && i < (size_t)kMyGroupLinkRowMax; ++i)
    {
        content->AddChild(MakeMyLinkRow(list.m_rows[i], (int)i), Hint().Fixed(kListRowH));
    }
    return std::unique_ptr<DuiControl>(content.release());
}

} // anonymous namespace

int MyGroupLinkListContentHeight(size_t rowCount)
{
    if (rowCount == 0)
        return kPlaceholderH;
    const size_t rows = (rowCount < (size_t)kMyGroupLinkRowMax) ? rowCount : (size_t)kMyGroupLinkRowMax;
    return (int)rows * kListRowH + (int)(rows - 1) * kListRowGap;
}

void FillMyGroupLinkList(DuiControl* root, const MyGroupLinkList& list)
{
    DuiScrollView* scroll = (root != NULL) ? dynamic_cast<DuiScrollView*>(root->FindCtrlById(kGroupLinkIdMyLinksScroll)) : NULL;
    if (scroll == NULL)
        return;
    const size_t rows = MyGroupLinksPlaceholderText(list).empty() ? list.m_rows.size() : 0;
    const int contentH = MyGroupLinkListContentHeight(rows);
    scroll->SetContent(MakeMyLinkListContent(list));
    scroll->SetContentHeight(contentH);
    //SetContentHeight 只更新滚动范围，不重算内容的矩形；滚动视图自身的矩形没变时外层重排也不会让它重新排版，
    //新加的行会画得出来却点不到（开发约定文档 UI 约定 12）。还没排过版（矩形为空）时等第一次排版即可
    const RECT rc = scroll->GetRect();
    if (rc.right > rc.left && rc.bottom > rc.top)
    {
        scroll->ForceLayout(rc);
        //内容变少时（滚到底后作废了几条）原来的滚动位置可能超出新的范围，收回到最大可滚动位置，
        //否则列表会停在内容之外、显示成一片空白
        const int viewH = rc.bottom - rc.top;
        const int maxPos = (contentH > viewH) ? (contentH - viewH) : 0;
        if (scroll->GetScrollPos() > maxPos)
            scroll->SetScrollPos(maxPos);
    }
    scroll->Invalidate();
}

std::unique_ptr<DuiControl> BuildGroupLinkBody()
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPadH, kPadTop, kPadH, kPadBottom);
    root->SetGap(kRootGap);

    std::unique_ptr<DuiLabel> name = MakeLabel(kGroupLinkIdGroupName, _T(""), kColorTitle,
                                               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    name->SetTextPointSize(kNamePt, true);
    root->AddChild(std::unique_ptr<DuiControl>(name.release()), Hint().Fixed(kNameH));

    std::unique_ptr<DuiLabel> hint = MakeLabel(kGroupLinkIdHint,
        TR(L"同企业的同事点开链接即可加入本群；本群需要审批时，要等群主或管理员同意。"),
        kColorHint, DT_LEFT | DT_TOP | DT_WORDBREAK);
    hint->SetWordWrap(true);
    root->AddChild(std::unique_ptr<DuiControl>(hint.release()), Hint().Fixed(kHintH));

    //有效期一行：「有效期」[下拉] ……「生成链接」
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->SetGap(kRowGap);
        row->AddChild(std::unique_ptr<DuiControl>(MakeLabel(0, TR(L"有效期"), kColorHint,
                                                            DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                      Hint().Fixed(kLabelW));
        std::unique_ptr<DuiComboBox> combo(new DuiComboBox());
        combo->SetCtrlId(kGroupLinkIdValidity);
        for (int i = 0; i < kGroupLinkValidityChoiceCount; ++i)
        {
            combo->AddString(GroupLinkChoiceText(i).c_str());
        }
        combo->SetCurSel(kGroupLinkValidityDefaultIndex, false);
        row->AddChild(std::unique_ptr<DuiControl>(combo.release()), Hint().Fixed(kComboW));
        row->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
        row->AddChild(MakeButton(kGroupLinkIdCreate, TR(L"生成链接"), DuiButton::Variant::Primary), Hint().Fixed(kCreateW));
        root->AddChild(std::unique_ptr<DuiControl>(row.release()), Hint().Fixed(kRowH));
    }

    //链接文字：浅底单行，放不下时中间省略（随机码在末尾，省略中间仍看得出是哪一条）
    {
        std::unique_ptr<DuiVBox> urlBox(new DuiVBox());
        urlBox->SetBgColor(kColorUrlBg);
        urlBox->SetCornerRadius(kUrlRadius);
        urlBox->SetPadding(kUrlPadH, 0, kUrlPadH, 0);
        std::unique_ptr<DuiLabel> url = MakeLabel(kGroupLinkIdUrl, TR(L"还没有生成链接"), kColorUrl,
                                                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
        urlBox->AddChild(std::unique_ptr<DuiControl>(url.release()), Hint().Weight(1));
        root->AddChild(std::unique_ptr<DuiControl>(urlBox.release()), Hint().Fixed(kRowH));
    }

    root->AddChild(std::unique_ptr<DuiControl>(MakeLabel(kGroupLinkIdStatus, _T(""), kColorHint,
                                                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS).release()),
                   Hint().Fixed(kStatusH));

    //「我生成的链接」：标题与列表，列表占满余下的高度，按钮行贴底
    root->AddChild(std::unique_ptr<DuiControl>(MakeLabel(kGroupLinkIdMyLinksTitle, TR(L"我生成的链接"), kColorTitle,
                                                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS).release()),
                   Hint().Fixed(kSectionTitleH));
    {
        std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
        scroll->SetCtrlId(kGroupLinkIdMyLinksScroll);
        MyGroupLinkList loading;
        scroll->SetContent(MakeMyLinkListContent(loading));
        scroll->SetContentHeight(MyGroupLinkListContentHeight(0));
        root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));
    }

    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetCtrlId(kGroupLinkIdButtonRow);
    buttons->SetGap(kRowGap);
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    buttons->AddChild(MakeButton(kGroupLinkIdCopy, TR(L"复制链接"), DuiButton::Variant::Default), Hint().Fixed(kCopyW));
    buttons->AddChild(MakeButton(kGroupLinkIdClose, TR(L"关闭"), DuiButton::Variant::Default), Hint().Fixed(kCloseW));
    root->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonH));
    return std::unique_ptr<DuiControl>(root.release());
}

} // namespace balloonmain
