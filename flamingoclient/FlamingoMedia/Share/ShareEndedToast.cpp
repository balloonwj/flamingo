/**
 *  屏幕共享结束的右下角提示，说明见 ShareEndedToast.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "ShareEndedToast.h"

#include <memory>
#include "ShareCornerToast.h"   // ShareToastButtonWidth：按钮按字宽算宽度
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/UI/BalloonMain/CornerPopupPlacement.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/DuiTheme.h"
#include "../../Source/Common/I18n/I18n.h"         // TR：界面文字的多语言查找
#include "../../Source/Common/I18n/I18nLayout.h"   // MeasureUiTextWidth：按字宽估算换行的行数

namespace screenshare {

namespace {

//整窗宽与标题栏高（像素），与 CShareCornerToast 一致
const int kToastW = 400;
const int kTitleBarH = 28;
//内容区内边距、行间距、按钮行高（像素）
const int kPadding = 16;
const int kGap = 6;
const int kButtonRowH = 34;
//一行文字的最小行高（像素）：字体自身的行高更大时按字体的
const int kMinLineH = 20;
//行高比字体行高多留的空白（像素）
const int kLineExtra = 4;
//分享人、参与者最多显示几行，超出的部分省略
const int kMaxSharerLines = 2;
const int kMaxParticipantLines = 4;
//次要文字的颜色（共享标题、时长、名单）
const COLORREF kDetailColor = RGB(96, 100, 108);
//标题栏文字
const wchar_t* const kToastTitle = TR_NOOP(L"屏幕共享");
//多行文字的绘制方式：按词换行（中文按字），最后一行放不下时省略
const UINT kWrapFlags = DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX;
//单行文字的绘制方式
const UINT kSingleFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;

//一行文字的行高（像素）
int lineHeight()
{
    const int font = i18n::MeasureUiLineHeight(i18n::DefaultFontPixelHeight(), false) + kLineExtra;
    return font > kMinLineH ? font : kMinLineH;
}

//按字宽估算一段文字在 widthPx 宽里要占几行，限制在 1 ~ maxLines
int linesFor(const std::wstring& text, int widthPx, int maxLines)
{
    if (widthPx <= 0)
        return 1;
    const int w = i18n::MeasureUiTextWidth(text.c_str(), i18n::DefaultFontPixelHeight(), false);
    int lines = (w + widthPx - 1) / widthPx;
    if (lines < 1)
        lines = 1;
    if (lines > maxLines)
        lines = maxLines;
    return lines;
}

std::unique_ptr<balloonwjui::DuiLabel> makeLabel(const std::wstring& text, UINT id, UINT flags, bool detail)
{
    std::unique_ptr<balloonwjui::DuiLabel> label(new balloonwjui::DuiLabel());
    label->SetText(text.c_str());
    label->SetCtrlId(id);
    label->SetTextAlign(flags);
    if (detail)
        label->SetTextColor(kDetailColor);
    return label;
}

} // namespace

CShareEndedToast::CShareEndedToast()
{
}

CShareEndedToast::~CShareEndedToast()
{
    Hide();
}

void CShareEndedToast::Show(const std::wstring& headline, const std::wstring& shareTitle, const ShareEndedTexts& texts)
{
    if (!IsWindow())
    {
        const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        if (Create(NULL, CWindow::rcDefault, TR(kToastTitle), WS_POPUP, exStyle) == NULL)
        {
            LOG_ERROR("screenshare: create ended toast failed.");
            return;
        }
        SetTitleBarHeight(kTitleBarH);
        //右下角提示不加最小化，只留关闭按钮
        SetButtons(false, false, true);
        SetResizable(false);
        SetIcon(m_icons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    }
    SetTitle(TR(kToastTitle));

    //按行数定窗口高度：说明、标题（有时）、时长各一行，分享人与参与者按字宽估算行数
    const int contentW = kToastW - 2 * kPadding;
    const int lineH = lineHeight();
    const int sharerLines = linesFor(texts.m_sharers, contentW, kMaxSharerLines);
    const int participantLines = linesFor(texts.m_participants, contentW, kMaxParticipantLines);
    int rows = 0;
    int textH = 0;
    //结束说明
    ++rows;
    textH += lineH;
    //共享标题
    if (!shareTitle.empty())
    {
        ++rows;
        textH += lineH;
    }
    //时长、分享人、参与者
    rows += 3;
    textH += lineH + sharerLines * lineH + participantLines * lineH;
    //各行之间与按钮行之前各一个间距
    const int height = kTitleBarH + 2 * kPadding + textH + rows * kGap + kButtonRowH;
    ResizeClient(kToastW, height);

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPadding);
    root->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiLabel> head = makeLabel(headline, kIdHeadline, kSingleFlags, false);
    head->SetTextPointSize(balloonwjui::DuiTheme::Inst().GetDefaultFontPt(), true);
    root->AddChild(std::move(head), balloonwjui::DuiLayout::Hint().Fixed(lineH));
    if (!shareTitle.empty())
        root->AddChild(makeLabel(shareTitle, kIdShareTitle, kSingleFlags, true), balloonwjui::DuiLayout::Hint().Fixed(lineH));
    root->AddChild(makeLabel(texts.m_duration, kIdDuration, kSingleFlags, true), balloonwjui::DuiLayout::Hint().Fixed(lineH));
    root->AddChild(makeLabel(texts.m_sharers, kIdSharers, kWrapFlags, true),
                   balloonwjui::DuiLayout::Hint().Fixed(sharerLines * lineH));
    root->AddChild(makeLabel(texts.m_participants, kIdParticipants, kWrapFlags, true),
                   balloonwjui::DuiLayout::Hint().Fixed(participantLines * lineH));
    root->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));
    //底部：「知道了」靠右
    std::unique_ptr<balloonwjui::DuiHBox> buttons(new balloonwjui::DuiHBox());
    buttons->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                      balloonwjui::DuiLayout::Hint().Weight(1));
    const std::wstring okText = TR(L"知道了");
    std::unique_ptr<balloonwjui::DuiButton> ok(new balloonwjui::DuiButton());
    ok->SetText(okText.c_str());
    ok->SetCtrlId(kIdOk);
    buttons->AddChild(std::move(ok), balloonwjui::DuiLayout::Hint().Fixed(ShareToastButtonWidth(okText)));
    root->AddChild(std::move(buttons), balloonwjui::DuiLayout::Hint().Fixed(kButtonRowH));
    SetClientContent(std::move(root));

    balloonmain::MoveWindowToWorkAreaCorner(m_hWnd, balloonmain::kCornerPopupMarginX,
                                            balloonmain::kCornerPopupMarginY);
    ShowWindow(SW_SHOWNA);
    LOG_INFO("screenshare: ended toast shown, sharer lines %d, participant lines %d.", sharerLines, participantLines);
}

void CShareEndedToast::Hide()
{
    if (IsWindow())
        DestroyWindow();
}

LRESULT CShareEndedToast::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    //「知道了」：关闭。等按钮的点击通知返回后再销毁窗口
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && n->ctrlId == static_cast<UINT>(kIdOk))
    {
        PostMessage(WM_CLOSE);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CShareEndedToast::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    Hide();
    return 0;
}

} // namespace screenshare
