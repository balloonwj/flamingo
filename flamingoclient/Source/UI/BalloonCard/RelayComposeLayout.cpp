/**
 *  RelayComposeLayout.cpp —— 「发起接龙」窗口的客户区控件树，见 RelayComposeLayout.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用
 *  （布局单测按设计尺寸排版）。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// ---- ATL / WTL 头文件要求的前置依赖，与 RegisterLayout.cpp 相同 ----
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

#include <memory>

#include "RelayComposeLayout.h"
#include "../../net/CardProtocolClient.h"   // 标题、说明、每条接龙的长度上限
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSpinBox.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;

namespace ballooncard {

namespace {

// ---- 颜色 ----
const COLORREF kColorWhite = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorLabel = RGB(0x8A, 0x8A, 0x8A);   // 各输入框上方的小标题
const COLORREF kColorError = RGB(0xE5, 0x39, 0x35);   // 提示行

// ---- 版式（逻辑像素）----
const int kPadH         = 20;   // 客户区左右内边距
const int kPadTop       = 4;    // 客户区上内边距（标题栏下方）
const int kPadBottom    = 16;   // 客户区下内边距
const int kRootGap      = 6;    // 各行之间的间距
const int kLabelH       = 18;   // 小标题行高
const int kEditH        = 34;   // 单行输入框高
const int kNoteH        = 80;   // 说明输入框高（多行）
const int kSpinRowH     = 28;   // 截止时间每一行微调框的高
// 截止时间六个微调框统一的宽：放得下年份的四位数字与右侧的 ▲▼。六个框宽高一致，年月日与时分秒
// 两行的单位文字也等宽，两行因此按列对齐（布局单测 RCL4）
const int kSpinW        = 68;
const int kUnitW        = 18;   // 「年」「月」「日」「时」「分」「秒」文字宽
const int kSpinGap      = 4;    // 同一行里微调框与文字之间的间距
const int kErrorH       = 20;   // 提示行高
const int kButtonRowH   = 34;   // 底部按钮行高
const int kButtonGap    = 10;   // 底部按钮之间的间距
const int kCancelW      = 80;   // 「取消」宽
const int kSubmitW      = 88;   // 「发起」宽
// 截止时间微调框的取值范围。年的范围由窗口打开时按当前年份重设，这里只是建树时的初值
const int kYearMin   = 2000;
const int kYearMax   = 2099;
const int kMonthMin  = 1;
const int kMonthMax  = 12;
const int kDayMin    = 1;
const int kDayMax    = 31;
const int kHourMax   = 23;
const int kMinuteMax = 59;
const int kSecondMax = 59;

// 造一个输入框上方的小标题
std::unique_ptr<DuiControl> MakeCaption(LPCTSTR text)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetText(text);
    label->SetTextColor(kColorLabel);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return std::unique_ptr<DuiControl>(label.release());
}

// 造一个输入框
std::unique_ptr<DuiEdit> MakeEdit(UINT id, LPCTSTR placeholder, size_t maxChars)
{
    std::unique_ptr<DuiEdit> edit(new DuiEdit());
    edit->SetCtrlId(id);
    edit->SetPlaceholder(placeholder);
    edit->SetBgColor(kColorWhite);
    edit->SetMaxLength((int)maxChars);
    return edit;
}

// 造一个截止时间微调框
std::unique_ptr<DuiControl> MakeSpin(UINT id, int minV, int maxV)
{
    std::unique_ptr<DuiSpinBox> spin(new DuiSpinBox());
    spin->SetCtrlId(id);
    spin->SetRange(minV, maxV);
    return std::unique_ptr<DuiControl>(spin.release());
}

// 造微调框右侧的单位文字（年、月、日、时、分、秒）
std::unique_ptr<DuiControl> MakeUnit(LPCTSTR text)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetText(text);
    label->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return std::unique_ptr<DuiControl>(label.release());
}

// 造一行「[微调框]单位 [微调框]单位 [微调框]单位」，右侧留空。三个 id、单位、取值范围、宽度依次对应
std::unique_ptr<DuiControl> MakeSpinRow(const UINT ids[3], const LPCTSTR units[3],
                                        const int mins[3], const int maxs[3], const int widths[3])
{
    typedef DuiLayout::Hint Hint;
    const int kFieldsPerRow = 3;
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(kSpinGap);
    for (int i = 0; i < kFieldsPerRow; ++i)
    {
        row->AddChild(MakeSpin(ids[i], mins[i], maxs[i]), Hint().Fixed(widths[i]));
        row->AddChild(MakeUnit(units[i]), Hint().Fixed(kUnitW));
    }
    row->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(row.release());
}

// 造一个底部按钮
std::unique_ptr<DuiControl> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetText(text);
    return std::unique_ptr<DuiControl>(button.release());
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildRelayComposeBody()
{
    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPadH, kPadTop, kPadH, kPadBottom);
    root->SetGap(kRootGap);

    root->AddChild(MakeCaption(TR(L"标题")), Hint().Fixed(kLabelH));
    root->AddChild(std::unique_ptr<DuiControl>(
        MakeEdit(kRelayComposeIdTitle, TR(L"例如：周五团建报名"), net::kRelayTitleMaxChars).release()),
        Hint().Fixed(kEditH));

    root->AddChild(MakeCaption(TR(L"说明（选填）")), Hint().Fixed(kLabelH));
    std::unique_ptr<DuiEdit> note = MakeEdit(kRelayComposeIdNote, TR(L"例如：格式：姓名+是否带家属"),
                                             net::kRelayNoteMaxChars);
    note->SetMultiLine(true);
    note->SetWordWrap(true);
    root->AddChild(std::unique_ptr<DuiControl>(note.release()), Hint().Fixed(kNoteH));

    //截止时间必填，不能早于当前时间 + 5 分钟（点「发起」时检查）。用微调框而不是下拉框或日期弹层：
    //既能直接输入数字，也能点 ▲▼ 调整，不会再弹出一层浮层（与消息记录窗选日期同一做法）
    root->AddChild(MakeCaption(TR(L"截止时间（不能早于当前时间 5 分钟之后）")), Hint().Fixed(kLabelH));
    const UINT    dateIds[3]    = { kRelayComposeIdDeadlineYear, kRelayComposeIdDeadlineMonth,
                                   kRelayComposeIdDeadlineDay };
    const LPCTSTR dateUnits[3]  = { TR(L"年"), TR(L"月"), TR(L"日") };
    const int     dateMins[3]   = { kYearMin, kMonthMin, kDayMin };
    const int     dateMaxs[3]   = { kYearMax, kMonthMax, kDayMax };
    const int     dateWidths[3] = { kSpinW, kSpinW, kSpinW };
    root->AddChild(MakeSpinRow(dateIds, dateUnits, dateMins, dateMaxs, dateWidths), Hint().Fixed(kSpinRowH));
    const UINT    timeIds[3]    = { kRelayComposeIdDeadlineHour, kRelayComposeIdDeadlineMinute,
                                   kRelayComposeIdDeadlineSecond };
    const LPCTSTR timeUnits[3]  = { TR(L"时"), TR(L"分"), TR(L"秒") };
    const int     timeMins[3]   = { 0, 0, 0 };
    const int     timeMaxs[3]   = { kHourMax, kMinuteMax, kSecondMax };
    const int     timeWidths[3] = { kSpinW, kSpinW, kSpinW };
    root->AddChild(MakeSpinRow(timeIds, timeUnits, timeMins, timeMaxs, timeWidths), Hint().Fixed(kSpinRowH));

    root->AddChild(MakeCaption(TR(L"我的接龙（选填，发起后自动参与）")), Hint().Fixed(kLabelH));
    root->AddChild(std::unique_ptr<DuiControl>(
        MakeEdit(kRelayComposeIdEntry, TR(L"例如：本人+1"), net::kRelayTextMaxChars).release()),
        Hint().Fixed(kEditH));

    std::unique_ptr<DuiLabel> error(new DuiLabel());
    error->SetCtrlId(kRelayComposeIdError);
    error->SetTextColor(kColorError);
    error->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    root->AddChild(std::unique_ptr<DuiControl>(error.release()), Hint().Fixed(kErrorH));

    //余下的高度留空，按钮行贴底
    root->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));

    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetGap(kButtonGap);
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    buttons->AddChild(MakeButton(kRelayComposeIdCancel, TR(L"取消"), DuiButton::Variant::Default),
                      Hint().Fixed(kCancelW));
    buttons->AddChild(MakeButton(kRelayComposeIdSubmit, TR(L"发起"), DuiButton::Variant::Primary),
                      Hint().Fixed(kSubmitW));
    root->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonRowH));

    return std::unique_ptr<DuiControl>(root.release());
}

} // namespace ballooncard
