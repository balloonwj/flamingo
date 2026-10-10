/**
 *  屏幕共享的右下角提示，ShareCornerToast.cpp
 *  说明见 ShareCornerToast.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareCornerToast.h"

#include <memory>
#include "ShareMessages.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/UI/BalloonMain/CornerPopupPlacement.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Source/Common/I18n/I18nLayout.h"   // MeasureUiTextWidth：按界面字体量按钮文字宽度

namespace screenshare {

namespace {

//整窗尺寸与标题栏高（像素）。2026-10-05 用户要求加大：由 320×128 改为 400×152
const int kToastW = 400;
const int kToastH = 152;
const int kTitleBarH = 28;
//内容区内边距与间距、按钮的最小宽度与按钮行高
const int kPadding = 16;
const int kGap = 12;
const int kButtonW = 84;
const int kButtonRowH = 34;
//按钮文字左右留白合计（像素）：按钮宽度 = 文字宽度 + 该值，且不小于 kButtonW
const int kButtonTextPadPx = 28;
//多行文字时每多一行窗口加高的量（第 14 期）
const int kTextLineH = 22;
//没有操作时自动关闭：一分钟（第 15 期起标题后显示倒计时，定时器每秒刷新一次标题）；定时器 id 避开 balloonui
//基类可能占用的小 id
const UINT_PTR kCountdownTimerId = 301;
const int64_t kAutoCloseMs = 60 * 1000;
//标题栏文字（倒计时接在它后面）
const wchar_t* const kToastTitle = TR_NOOP(L"屏幕共享");

} // namespace

int ShareToastButtonWidth(const std::wstring& text)
{
    //用按钮实际绘制的默认字体量：字高随系统 DPI 换算，与 DuiButton 绘制时一致
    const int need = i18n::MeasureUiTextWidth(text.c_str(), i18n::DefaultFontPixelHeight(), false) + kButtonTextPadPx;
    return (need > kButtonW) ? need : kButtonW;
}

int ShareToastWindowWidth(int ignoreWidth, int acceptWidth)
{
    //按钮行：左侧弹性留白、次按钮、主按钮，三者之间各一个间距；两边各一个内边距
    const int need = 2 * kPadding + 2 * kGap + ignoreWidth + acceptWidth;
    return (need > kToastW) ? need : kToastW;
}

ShareToastTexts::ShareToastTexts()
    : m_title(TR(kToastTitle))
    , m_accept(TR(L"加入"))
    , m_ignore(TR(L"忽略"))
    , m_message(kMsgShareToastCommand)
{
}

CShareCornerToast::CShareCornerToast()
    : m_notifyWnd(NULL)
    , m_token(0)
{
}

CShareCornerToast::~CShareCornerToast()
{
    Hide();
}

void CShareCornerToast::Show(const std::wstring& text, LPARAM token, HWND notifyWnd, int textLines)
{
    Show(text, token, notifyWnd, textLines, ShareToastTexts());
}

void CShareCornerToast::Show(const std::wstring& text, LPARAM token, HWND notifyWnd, int textLines,
                             const ShareToastTexts& texts)
{
    if (textLines < 1)
        textLines = 1;
    m_notifyWnd = notifyWnd;
    m_token = token;
    m_texts = texts;
    if (!IsWindow())
    {
        const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        if (Create(NULL, CWindow::rcDefault, m_texts.m_title.c_str(), WS_POPUP, exStyle) == NULL)
        {
            LOG_ERROR("screenshare: create corner toast failed.");
            return;
        }
        SetTitleBarHeight(kTitleBarH);
        //会自动关闭的提示不加最小化，只留关闭按钮（第 15 期）
        SetButtons(false, false, true);
        SetResizable(false);
        SetIcon(m_icons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    }
    //每次弹出都从头倒计时
    m_countdown.Start(kAutoCloseMs, static_cast<int64_t>(::GetTickCount64()));
    refreshTitle();
    //每次按行数重设高度、按按钮文字重设宽度（已弹出时文字可能变了）
    const int ignoreW = ShareToastButtonWidth(m_texts.m_ignore);
    const int acceptW = ShareToastButtonWidth(m_texts.m_accept);
    ResizeClient(ShareToastWindowWidth(ignoreW, acceptW), kToastH + (textLines - 1) * kTextLineH);

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPadding);
    root->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiLabel> label(new balloonwjui::DuiLabel());
    label->SetText(text.c_str());
    //多行时自动换行，单行时超长省略
    label->SetTextAlign(textLines > 1 ? (DT_LEFT | DT_TOP | DT_WORDBREAK)
                                      : (DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS));
    root->AddChild(std::move(label), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiHBox> buttons(new balloonwjui::DuiHBox());
    buttons->SetGap(kGap);
    buttons->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                      balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiButton> ignore(new balloonwjui::DuiButton());
    ignore->SetText(m_texts.m_ignore.c_str());
    ignore->SetVariant(balloonwjui::DuiButton::Variant::Default);
    ignore->SetCtrlId(kIdIgnore);
    buttons->AddChild(std::move(ignore), balloonwjui::DuiLayout::Hint().Fixed(ignoreW));
    std::unique_ptr<balloonwjui::DuiButton> join(new balloonwjui::DuiButton());
    join->SetText(m_texts.m_accept.c_str());
    join->SetCtrlId(kIdJoin);
    buttons->AddChild(std::move(join), balloonwjui::DuiLayout::Hint().Fixed(acceptW));
    root->AddChild(std::move(buttons), balloonwjui::DuiLayout::Hint().Fixed(kButtonRowH));
    SetClientContent(std::move(root));

    balloonmain::MoveWindowToWorkAreaCorner(m_hWnd, balloonmain::kCornerPopupMarginX,
                                            balloonmain::kCornerPopupMarginY);
    ShowWindow(SW_SHOWNA);
    SetTimer(kCountdownTimerId, balloonmain::kCornerCountdownTickMs, NULL);
}

void CShareCornerToast::refreshTitle()
{
    if (!IsWindow())
        return;
    const int64_t remaining = m_countdown.RemainingMs(static_cast<int64_t>(::GetTickCount64()));
    SetTitle(balloonmain::MakeCornerCountdownTitle(m_texts.m_title.c_str(), remaining,
                                                   balloonmain::corner_countdown_close).c_str());
}

void CShareCornerToast::Hide()
{
    m_countdown.Stop();
    m_token = 0;
    m_notifyWnd = NULL;
    if (IsWindow())
        DestroyWindow();
}

LPARAM CShareCornerToast::Token() const
{
    return m_token;
}

void CShareCornerToast::send(int command)
{
    HWND notify = m_notifyWnd;
    const LPARAM token = m_token;
    const UINT message = m_texts.m_message;
    Hide();
    if (notify != NULL && ::IsWindow(notify))
        ::PostMessage(notify, message, static_cast<WPARAM>(command), token);
}

LRESULT CShareCornerToast::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        //加入
        if (n->ctrlId == static_cast<UINT>(kIdJoin))
        {
            send(kCmdJoin);
            return 0;
        }
        //忽略
        if (n->ctrlId == static_cast<UINT>(kIdIgnore))
        {
            send(kCmdIgnore);
            return 0;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CShareCornerToast::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (wParam != kCountdownTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    //到时按「忽略」处理，否则刷新标题里的剩余秒数
    if (m_countdown.Expired(static_cast<int64_t>(::GetTickCount64())))
        send(kCmdIgnore);
    else
        refreshTitle();
    return 0;
}

LRESULT CShareCornerToast::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    send(kCmdIgnore);
    return 0;
}

} // namespace screenshare
