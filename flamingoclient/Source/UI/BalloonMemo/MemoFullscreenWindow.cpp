/**
 *  备忘的全屏提醒窗口，见 MemoFullscreenWindow.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "MemoFullscreenWindow.h"

#include <memory>
#include <string>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM

#include "../../Common/Log/IULog.h"
#include "../../Common/Util/EncodeUtil.h"
#include "../../Feature/Memo/MemoClock.h"
#include "MemoCenter.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;
using balloonmemo::CMemoCenter;

namespace {

// ---- 控件 id（从 101 起，避开标题栏按钮的 1～3）----
const UINT kIdTitle  = 101;   // 标题
const UINT kIdTime   = 102;   // 时间与所属群
const UINT kIdNote   = 103;   // 备注
const UINT kIdMore   = 104;   // 「还有 N 条提醒」
const UINT kIdSnooze = 105;   // 「稍后提醒」
const UINT kIdKnown  = 106;   // 「知道了」
const UINT kIdCreator = 107;  // 创建人一行（群备忘才有）
const UINT kIdCaption = 108;  // 顶部小字「备忘提醒」（预览模式下为「备忘提醒（预览）」）

// ---- 定时器：取不与 DuiFrameWindow 内部定时器相同的号 ----
const UINT_PTR kTimerAnim    = 0x4D01;   // 淡入与滑入的逐帧动画
const UINT_PTR kTimerTimeout = 0x4D02;   // 检查是否已 60 秒无操作
const UINT kAnimFrameMs      = 16;       // 动画每帧的间隔（约 60 帧每秒）
const UINT kTimeoutCheckMs   = 1000;     // 无操作检查的间隔
const DWORD kAnimDurationMs  = 400;      // 淡入与滑入持续的时间

// ---- 外观 ----
const BYTE kTargetAlpha     = 235;                      // 淡入结束时的整窗不透明度（0～255），留一点透出桌面
const COLORREF kBg          = RGB(0x1C, 0x21, 0x2B);    // 深色背景
const COLORREF kCaptionFg   = RGB(0x9A, 0xA3, 0xB2);    // 「备忘提醒」与提示文字
const COLORREF kTitleFg     = RGB(0xFF, 0xFF, 0xFF);    // 标题
const COLORREF kTimeFg      = RGB(0xC9, 0xD1, 0xDE);    // 时间
const COLORREF kNoteFg      = RGB(0xE2, 0xE6, 0xEE);    // 备注
const int kCaptionPt   = 12;    // 「备忘提醒」磅值
const int kTitlePt     = 28;    // 标题磅值
const int kTimePt      = 13;    // 时间磅值
const int kNotePt      = 12;    // 备注磅值
const int kHintPt      = 10;    // 底部提示磅值
const int kCaptionH    = 28;    // 各行高（逻辑像素）
const int kTitleH      = 60;
const int kTimeH       = 30;
const int kCreatorH    = 26;    // 创建人一行的高
const int kNoteH       = 72;
const int kMoreH       = 24;
const int kButtonsH    = 40;
const int kHintH       = 24;
const int kButtonW     = 180;   // 两个按钮的宽（2026-10-04 由 140 加宽：「稍后提醒（10 分钟）」在 140 宽时首字被截掉一半）
const int kButtonGap   = 16;    // 两个按钮之间的间距
const int kRowGap      = 10;    // 各行之间的间距
const int kSlidePx     = 60;    // 标题滑入的距离
const int kTopPercent  = 30;    // 内容上沿距屏幕上沿的比例（百分比）
const int kSidePercent = 15;    // 左右各留出的宽度比例（百分比）
const int kPercent     = 100;

// 拼文字用的缓冲区大小（宽字符个数）
const size_t kTextBufSize = 64;

// m_pendingAction 的取值
const int kActionNone = 0;     // 没有待执行的动作
const int kActionKnown = 1;    // 「知道了」
const int kActionSnooze = 2;   // 「稍后提醒」
const int kActionClosePreview = 3;   // 预览模式下点了任一按钮：只关闭窗口

std::unique_ptr<DuiControl> MakeLabel(UINT id, LPCTSTR text, COLORREF color, int pt, bool bold, bool wrap)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    if (id != 0)
    {
        label->SetCtrlId(id);
    }
    label->SetText(text);
    label->SetTextColor(color);
    label->SetTextPointSize(pt, bold);
    if (wrap)
    {
        label->SetWordWrap(true);
        label->SetTextAlign(DT_CENTER | DT_TOP | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    else
    {
        label->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    return std::unique_ptr<DuiControl>(label.release());
}

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

CMemoFullscreenWindow::CMemoFullscreenWindow()
    : m_pendingAction(0)
    , m_animStart(0)
    , m_animating(false)
    , m_preview(false)
    , m_returnFocus(NULL)
    , m_pRoot(NULL)
    , m_pTitle(NULL)
    , m_pTime(NULL)
    , m_pCreator(NULL)
    , m_pNote(NULL)
    , m_pMore(NULL)
    , m_pCaption(NULL)
{
    m_lastMouse.x = 0;
    m_lastMouse.y = 0;
}

CMemoFullscreenWindow::~CMemoFullscreenWindow()
{
}

bool CMemoFullscreenWindow::EnsureWindow()
{
    if (IsWindow())
    {
        return true;
    }
    //工具窗口样式：不在任务栏上占一个按钮；分层窗口：淡入靠整窗透明度
    if (Create(NULL, CWindow::rcDefault, TR(L"备忘提醒"), WS_OVERLAPPEDWINDOW, WS_EX_TOOLWINDOW | WS_EX_LAYERED) == NULL)
    {
        LOG_ERROR("memo fullscreen window: Create failed.");
        return false;
    }
    SetTitle(TR(L"备忘提醒"));
    SetButtons(false, false, false);
    SetResizable(false);

    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kBg);
    root->SetGap(kRowGap);
    m_pRoot = root.get();
    root->AddChild(MakeLabel(kIdCaption, TR(L"备忘提醒"), kCaptionFg, kCaptionPt, false, false), Hint().Fixed(kCaptionH));
    root->AddChild(MakeLabel(kIdTitle, _T(""), kTitleFg, kTitlePt, true, false), Hint().Fixed(kTitleH));
    root->AddChild(MakeLabel(kIdTime, _T(""), kTimeFg, kTimePt, false, false), Hint().Fixed(kTimeH));
    root->AddChild(MakeLabel(kIdCreator, _T(""), kCaptionFg, kCaptionPt, false, false), Hint().Fixed(kCreatorH));
    root->AddChild(MakeLabel(kIdNote, _T(""), kNoteFg, kNotePt, false, true), Hint().Fixed(kNoteH));
    root->AddChild(MakeLabel(kIdMore, _T(""), kCaptionFg, kHintPt, false, false), Hint().Fixed(kMoreH));
    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetGap(kButtonGap);
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    buttons->AddChild(MakeButton(kIdSnooze, TR(L"稍后提醒（10 分钟）"), DuiButton::Variant::Default), Hint().Fixed(kButtonW));
    buttons->AddChild(MakeButton(kIdKnown, TR(L"知道了"), DuiButton::Variant::Primary), Hint().Fixed(kButtonW));
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonsH));
    root->AddChild(MakeLabel(0, TR(L"按 Esc 键等同于「知道了」"), kCaptionFg, kHintPt, false, false), Hint().Fixed(kHintH));
    root->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    SetClientContent(std::unique_ptr<DuiControl>(root.release()));

    DuiControl* content = GetClientContent();
    m_pTitle = dynamic_cast<DuiLabel*>(content->FindCtrlById(kIdTitle));
    m_pTime = dynamic_cast<DuiLabel*>(content->FindCtrlById(kIdTime));
    m_pCreator = dynamic_cast<DuiLabel*>(content->FindCtrlById(kIdCreator));
    m_pNote = dynamic_cast<DuiLabel*>(content->FindCtrlById(kIdNote));
    m_pMore = dynamic_cast<DuiLabel*>(content->FindCtrlById(kIdMore));
    m_pCaption = dynamic_cast<DuiLabel*>(content->FindCtrlById(kIdCaption));
    return true;
}

void CMemoFullscreenWindow::Enqueue(const balloonmemo::ReminderEntry& entry, const RECT& monitor)
{
    for (size_t i = 0; i < m_queue.size(); ++i)
    {
        if (m_queue[i].m_memoid == entry.m_memoid)
        {
            return;
        }
    }
    //本实例正在预览（只有编辑对话框持有的实例会预览，按理不会收到真实提醒）：先收起预览
    if (m_preview)
    {
        HideWindow();
    }
    m_queue.push_back(entry);
    if (IsWindow() && IsWindowVisible())
    {
        ShowFront();
        return;
    }
    if (!Present(monitor))
    {
        m_queue.clear();
        return;
    }
    LOG_INFO("memo fullscreen reminder shown: memoid=%lld, monitor=(%ld,%ld,%ld,%ld).", (long long)entry.m_memoid,
             monitor.left, monitor.top, monitor.right, monitor.bottom);
}

bool CMemoFullscreenWindow::ShowPreview(const balloonmemo::FullscreenReminderText& text, const std::wstring& creatorLine,
                                        const RECT& monitor, HWND returnFocus)
{
    if (!m_queue.empty())
    {
        LOG_WARNING("memo fullscreen preview refused: the window holds %u real reminders.", (uint32_t)m_queue.size());
        return false;
    }
    m_preview = true;
    m_previewText = text;
    m_previewCreator = creatorLine;
    m_returnFocus = returnFocus;
    if (IsWindow() && IsWindowVisible())
    {
        //已在预览：换上新内容，从头播放动画
        ShowFront();
        m_animStart = ::GetTickCount();
        m_animating = true;
        AnimateStep();
        SetTimer(kTimerAnim, kAnimFrameMs);
        m_timeout.Start(::GetTickCount());
        return true;
    }
    if (!Present(monitor))
    {
        m_preview = false;
        m_returnFocus = NULL;
        return false;
    }
    LOG_INFO("memo fullscreen preview shown: monitor=(%ld,%ld,%ld,%ld).", monitor.left, monitor.top, monitor.right,
             monitor.bottom);
    return true;
}

bool CMemoFullscreenWindow::Present(const RECT& monitor)
{
    if (!EnsureWindow())
    {
        return false;
    }

    //先把（还没显示的）窗口挪到目标显示器上，ToggleFullscreen 铺满的是窗口所在的那块显示器
    ::SetLayeredWindowAttributes(m_hWnd, 0, 0, LWA_ALPHA);
    //DuiFrameWindow 的全屏标志是 C++ 对象的成员，销毁 HWND 时不会复位。本对象每次收起都销毁窗口、
    //对象保留，于是第二次显示时新建的窗口上标志仍为 true，下面的 ToggleFullscreen 被跳过，窗口带着
    //标题栏显示、内容整体下移（2026-10-04 实机核对发现）。先按旧标志退出一次，使标志与新窗口一致；
    //此时整窗透明，退出时恢复旧位置的那一下看不到
    if (IsFullscreen())
    {
        ExitFullscreen();
    }
    ::SetWindowPos(m_hWnd, NULL, monitor.left, monitor.top, monitor.right - monitor.left,
                   monitor.bottom - monitor.top, SWP_NOZORDER | SWP_NOACTIVATE);
    if (!IsFullscreen())
    {
        ToggleFullscreen();
    }
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    ::SetForegroundWindow(m_hWnd);

    ShowFront();
    m_animStart = ::GetTickCount();
    m_animating = true;
    AnimateStep();
    SetTimer(kTimerAnim, kAnimFrameMs);
    m_timeout.Start(::GetTickCount());
    SetTimer(kTimerTimeout, kTimeoutCheckMs);
    ::GetCursorPos(&m_lastMouse);
    ScreenToClient(&m_lastMouse);
    return true;
}

void CMemoFullscreenWindow::ShowFront()
{
    if (m_pTitle == NULL || m_pTime == NULL || m_pNote == NULL || m_pMore == NULL)
    {
        return;
    }
    if (m_pCaption != NULL)
    {
        m_pCaption->SetText(m_preview ? TR(L"备忘提醒（预览）") : TR(L"备忘提醒"));
    }
    //预览：内容由编辑对话框给出，不查备忘本（还没保存的备忘本里没有）
    if (m_preview)
    {
        m_pTitle->SetText(m_previewText.m_title.c_str());
        m_pTime->SetText(m_previewText.m_time.c_str());
        if (m_pCreator != NULL)
        {
            m_pCreator->SetText(m_previewCreator.c_str());
        }
        m_pNote->SetText(m_previewText.m_note.c_str());
        m_pMore->SetText(_T(""));
        Invalidate();
        return;
    }
    if (m_queue.empty())
    {
        return;
    }
    const balloonmemo::ReminderEntry& entry = m_queue.front();
    const net::MemoItem* item = CMemoCenter::Instance().Book().Find(entry.m_memoid);
    if (item == NULL)
    {
        return;
    }
    const memo::LocalMinute now = memo::NowLocalMinute();
    const std::wstring groupName = (item->m_ctype == net::kMemoCtypeGroup)
                                       ? CMemoCenter::Instance().GroupNameOf(item->m_groupid) : std::wstring();
    const balloonmemo::FullscreenReminderText text = balloonmemo::BuildFullscreenReminderText(
        EncodeUtil::Utf8ToUnicode(item->m_title), EncodeUtil::Utf8ToUnicode(item->m_note), entry.m_event, now, groupName);
    m_pTitle->SetText(text.m_title.c_str());
    m_pTime->SetText(text.m_time.c_str());
    if (m_pCreator != NULL)
    {
        //群备忘：「张勇 · 10月3日 22:59 创建」；个人备忘为空串，这一行留空
        m_pCreator->SetText(CMemoCenter::Instance().CreatorLineOf(*item, now).c_str());
    }
    m_pNote->SetText(text.m_note.c_str());
    wchar_t buf[kTextBufSize] = { 0 };
    if (m_queue.size() > 1)
    {
        swprintf(buf, kTextBufSize, TR(L"还有 %u 条提醒"), (unsigned)(m_queue.size() - 1));
    }
    m_pMore->SetText(buf);
    Invalidate();
}

bool CMemoFullscreenWindow::AnimateStep()
{
    const DWORD elapsed = ::GetTickCount() - m_animStart;
    const DWORD clamped = (elapsed > kAnimDurationMs) ? kAnimDurationMs : elapsed;
    //缓出：进度 p 从 0 到 1，偏移按 (1-p)^2 收敛，开头快、结尾慢
    const int remain = (int)(kAnimDurationMs - clamped);
    const int offset = ::MulDiv(::MulDiv(kSlidePx, remain, (int)kAnimDurationMs), remain, (int)kAnimDurationMs);
    const BYTE alpha = (BYTE)::MulDiv(kTargetAlpha, (int)clamped, (int)kAnimDurationMs);
    ::SetLayeredWindowAttributes(m_hWnd, 0, alpha, LWA_ALPHA);

    if (m_pRoot != NULL)
    {
        RECT rc;
        GetClientRect(&rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        const int side = ::MulDiv(w, kSidePercent, kPercent);
        const int top = ::MulDiv(h, kTopPercent, kPercent) + offset;
        m_pRoot->SetPadding(side, top, side, 0);
        //上内边距变了而矩形没变：必须用 ForceLayout 才会重排（开发约定文档 UI 约定第 5 条）
        m_pRoot->ForceLayout(m_pRoot->GetRect());
        Invalidate();
    }
    return clamped >= kAnimDurationMs;
}

void CMemoFullscreenWindow::PopFront()
{
    if (!m_queue.empty())
    {
        m_queue.pop_front();
    }
    if (m_queue.empty())
    {
        HideWindow();
        return;
    }
    m_timeout.Start(::GetTickCount());
    ShowFront();
}

void CMemoFullscreenWindow::HideWindow()
{
    m_animating = false;
    if (m_preview)
    {
        //预览结束：先把前台交给编辑对话框，再销毁本窗口。若先销毁，系统会把激活交给同一线程里的
        //其它窗口（2026-10-04 实测为输入法工具条），之后再调 SetForegroundWindow 不会生效
        m_preview = false;
        const HWND back = m_returnFocus;
        m_returnFocus = NULL;
        BOOL returned = FALSE;
        if (::IsWindow(back))
        {
            returned = ::SetForegroundWindow(back);
        }
        LOG_INFO("memo fullscreen preview closed, focus returned=%d, foreground is dialog=%d.", returned ? 1 : 0,
                 (::GetForegroundWindow() == back) ? 1 : 0);
    }
    if (IsWindow())
    {
        KillTimer(kTimerAnim);
        KillTimer(kTimerTimeout);
        //下一次可能要盖另一块显示器，销毁后重建比退出全屏再挪窗口简单
        DestroyWindow();
    }
    m_pRoot = NULL;
    m_pTitle = NULL;
    m_pTime = NULL;
    m_pCreator = NULL;
    m_pNote = NULL;
    m_pMore = NULL;
    m_pCaption = NULL;
}

void CMemoFullscreenWindow::RemoveMemo(int64_t memoid)
{
    if (m_queue.empty())
    {
        return;
    }
    const bool front = (m_queue.front().m_memoid == memoid);
    for (std::deque<balloonmemo::ReminderEntry>::iterator it = m_queue.begin(); it != m_queue.end(); ++it)
    {
        if (it->m_memoid == memoid)
        {
            m_queue.erase(it);
            break;
        }
    }
    if (m_queue.empty())
    {
        HideWindow();
    }
    else if (front)
    {
        ShowFront();
    }
}

void CMemoFullscreenWindow::CloseWindow()
{
    m_queue.clear();
    HideWindow();
}

LRESULT CMemoFullscreenWindow::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam == kTimerAnim)
    {
        if (AnimateStep())
        {
            KillTimer(kTimerAnim);
            m_animating = false;
        }
        return 0;
    }
    if (wParam == kTimerTimeout)
    {
        //预览：60 秒无操作只关闭窗口，不交给中枢转为右下角弹窗
        if (m_preview && m_timeout.ShouldFallback(::GetTickCount()))
        {
            HideWindow();
            return 0;
        }
        if (m_timeout.ShouldFallback(::GetTickCount()))
        {
            const std::vector<balloonmemo::ReminderEntry> entries(m_queue.begin(), m_queue.end());
            m_queue.clear();
            HideWindow();
            LOG_INFO("memo fullscreen reminder timed out, %u entries moved to the popup.", (uint32_t)entries.size());
            CMemoCenter::Instance().OnFullscreenTimedOut(entries);
        }
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CMemoFullscreenWindow::OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    m_timeout.OnUserActivity(::GetTickCount());
    //预览：Esc 只关闭窗口
    if (wParam == VK_ESCAPE && m_preview)
    {
        HideWindow();
        return 0;
    }
    if (wParam == VK_ESCAPE && !m_queue.empty())
    {
        FinishFront(false);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CMemoFullscreenWindow::OnUserActivity(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    if (pt.x != m_lastMouse.x || pt.y != m_lastMouse.y)
    {
        m_lastMouse = pt;
        m_timeout.OnUserActivity(::GetTickCount());
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CMemoFullscreenWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL || pNotify->code != DUIN_CLICK || (m_queue.empty() && !m_preview))
    {
        bHandled = FALSE;
        return 0;
    }
    //队列空了会销毁本窗口，不能在点击通知里直接做：记下动作、投递给自己
    switch (pNotify->ctrlId)
    {
    //「知道了」：这一条处理完，换下一条；预览时只关闭窗口
    case kIdKnown:
        m_pendingAction = m_preview ? kActionClosePreview : kActionKnown;
        PostMessage(kMsgDeferredAction);
        return 0;

    //「稍后提醒」：10 分钟后再提醒这一条，换下一条；预览时只关闭窗口，不登记稍后提醒
    case kIdSnooze:
        m_pendingAction = m_preview ? kActionClosePreview : kActionSnooze;
        PostMessage(kMsgDeferredAction);
        return 0;

    //其它控件的点击：交给框架
    default:
        break;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CMemoFullscreenWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //Alt+F4：等同于对当前这一条点「知道了」
    if (!m_queue.empty())
    {
        FinishFront(false);
    }
    else
    {
        HideWindow();
    }
    return 0;
}

LRESULT CMemoFullscreenWindow::OnDeferredAction(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    const int action = m_pendingAction;
    m_pendingAction = kActionNone;
    if (action == kActionClosePreview)
    {
        if (m_preview)
        {
            HideWindow();
        }
        return 0;
    }
    if (action != kActionNone && !m_queue.empty())
    {
        FinishFront(action == kActionSnooze);
    }
    return 0;
}

void CMemoFullscreenWindow::FinishFront(bool snooze)
{
    if (m_queue.empty())
    {
        return;
    }
    const int64_t memoid = m_queue.front().m_memoid;
    const memo::LocalMinute event = m_queue.front().m_event;
    PopFront();
    if (snooze)
    {
        CMemoCenter::Instance().OnReminderSnoozed(memoid, event);
    }
    else
    {
        CMemoCenter::Instance().OnReminderDismissed(memoid);
    }
}
