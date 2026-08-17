/**
 *  ChatInputRichEdit 的实现。设计背景与消息协议见 ChatInputRichEdit.h。
 *  balloonwj@qq.com   2026-08-16
 */
#include "stdafx.h"
#include "ChatInputRichEdit.h"

#include "../CustomMsgDef.h"   // WM_CHAT_FOCUS_AREA / WM_CHAT_COPY_SELECTION

namespace balloonchatwnd {

namespace {

// 按键按下消息里表示「自动重复」的那一位：为 1 说明这个键在本次消息之前
// 就已经按着了。按住回车不该连发多条消息，发送路径据此忽略重复。
const UINT kPrevKeyDownFlag = 0x4000;

// 判断某个虚拟键当前是否处于按下状态。
const SHORT kKeyDownMask = (SHORT)0x8000;

// Ctrl+回车在光标处插入的换行串。用回车加换行两个字符，与排版引擎内部的
// 换行表示一致。
const TCHAR* const kNewLine = _T("\r\n");

// 「@提醒」浮层要接管的那几个导航键。
bool IsMentionNavKey(UINT vk)
{
    return vk == VK_UP || vk == VK_DOWN
        || vk == VK_RETURN || vk == VK_TAB || vk == VK_ESCAPE;
}

} // 匿名命名空间

ChatInputRichEdit::ChatInputRichEdit()
    : m_hNotifyTarget(NULL)
    , m_nSendOnEnterMsg(0)
    , m_nTypingChangedMsg(0)
    , m_nMentionChangedMsg(0)
    , m_nMentionNavMsg(0)
    , m_nMentionWheelMsg(0)
    , m_bImeComposing(false)
    , m_pfnSendQuery(NULL)
    , m_pSendQueryCtx(NULL)
{
    // 关掉基类自带的拖放，且<u>必须在构造时就关</u>。
    //
    // 操作系统的拖放目标是按窗口注册的，一个窗口只能注册一个。基类在第一次
    // 布局时会替自己向宿主申请开启拖放分发（DuiRichEdit::Layout →
    // DuiHost::EnableDropDispatch），那一步就把聊天窗的窗口拖放目标占走了；
    // 随后聊天窗自己的 ChatDropController::Attach 再去注册必然失败，症状是
    // 拖任何文件进聊天窗都没有反应，界面上没有任何提示。
    //
    // 聊天窗的投放区是整个窗口（拖文件进来就发送），不需要输入框单独收一份，
    // 所以这里直接关掉。关的时机是关键：等控件挂进控件树之后再关就晚了 ——
    // 挂进去那一下就会走布局，申请已经发出去了，而这个开关只翻排版引擎的一个
    // 属性位，并不会把宿主已经做的注册撤销。
    //
    // 守卫用例：Source/Tests/ChatDropRegistrationTests.cpp（Suite CDR）。
    SetDragDropEnabled(false);
}

void ChatInputRichEdit::SetNotifyTarget(HWND hWnd)
{
    m_hNotifyTarget = hWnd;
}

void ChatInputRichEdit::SetSendOnEnterMsg(UINT nMsg)
{
    m_nSendOnEnterMsg = nMsg;
}

void ChatInputRichEdit::SetSendOnEnterQuery(SendOnEnterQueryFn fn, void* pCtx)
{
    m_pfnSendQuery   = fn;
    m_pSendQueryCtx  = pCtx;
}

void ChatInputRichEdit::SetTypingChangedMsg(UINT nMsg)
{
    m_nTypingChangedMsg = nMsg;
}

void ChatInputRichEdit::SetMentionMsgs(UINT nChangedMsg, UINT nNavMsg, UINT nWheelMsg)
{
    m_nMentionChangedMsg = nChangedMsg;
    m_nMentionNavMsg     = nNavMsg;
    m_nMentionWheelMsg   = nWheelMsg;
}

bool ChatInputRichEdit::HasTarget() const
{
    return m_hNotifyTarget != NULL && ::IsWindow(m_hNotifyTarget);
}

void ChatInputRichEdit::NotifyFocusInput()
{
    if (!HasTarget())
    {
        return;
    }
    ::PostMessage(m_hNotifyTarget, WM_CHAT_FOCUS_AREA,
                  (WPARAM)CHAT_FOCUS_INPUT, 0);
}

void ChatInputRichEdit::NotifyTyping(bool bKillFocus)
{
    if (m_nTypingChangedMsg == 0 || !HasTarget())
    {
        return;
    }
    ::PostMessage(m_hNotifyTarget, m_nTypingChangedMsg,
                  bKillFocus ? 1 : 0, 0);
}

bool ChatInputRichEdit::OnKeyDown(UINT vk, UINT flags)
{
    // ---- Ctrl+C：先问一句这一下归谁 ----
    //
    // 焦点在接收框（用户刚在气泡上拖选完）就由消息区复制并吞掉按键；焦点在
    // 发送框则不拦，输入框照常复制自己的选中文字。焦点归属由聊天窗维护。
    //
    // 用**同步**发送而不是投递：要拿它的返回值当场决定这次按键吞不吞。
    if (vk == 'C' && HasTarget()
        && (::GetKeyState(VK_CONTROL) & kKeyDownMask) != 0)
    {
        const LRESULT lCopied = ::SendMessage(m_hNotifyTarget,
                                              WM_CHAT_COPY_SELECTION, 0, 0);
        if (lCopied != 0)
        {
            return true;   // 已由消息区复制，吞掉这次按键
        }
    }

    // ---- @提醒浮层的导航键 ----
    //
    // 浮层正显示时，方向键 / 回车 / Tab / Esc 归它用来移动高亮或提交选中。
    // 同样必须同步发送 —— 吞不吞得当场知道。注意这一段要排在回车处理**之前**：
    // 浮层显示期间的回车是「选中当前候选」，不是「发送消息」。
    if (m_nMentionNavMsg != 0 && IsMentionNavKey(vk) && HasTarget())
    {
        const LRESULT lTaken = ::SendMessage(m_hNotifyTarget, m_nMentionNavMsg,
                                             (WPARAM)vk, 0);
        if (lTaken != 0)
        {
            return true;   // 浮层消费掉了这次按键
        }
    }

    // ---- 回车：发送还是换行 ----
    //
    // 输入法合成期内不介入 —— 那时的回车是「候选词上屏」，拦下来会让用户
    // 打不出中文。
    if (vk == VK_RETURN && !m_bImeComposing)
    {
        const bool bCtrlDown = (::GetKeyState(VK_CONTROL) & kKeyDownMask) != 0;
        // 这次回车是发送还是换行，交给调用方注入的回调判断（它知道用户选的是
        // 哪种发送模式）。没注册回调时按最常见的约定：不按 Ctrl 即发送。
        const bool bSendNow = (m_pfnSendQuery != NULL)
                            ? m_pfnSendQuery(bCtrlDown, m_pSendQueryCtx)
                            : !bCtrlDown;

        if (!bSendNow || m_nSendOnEnterMsg == 0)
        {
            // 换行：在当前选区 / 光标处插入换行符。
            ReplaceSel(kNewLine);
        }
        else if ((flags & kPrevKeyDownFlag) == 0)
        {
            // 发送。忽略自动重复，避免按住回车连发。
            if (HasTarget())
            {
                ::PostMessage(m_hNotifyTarget, m_nSendOnEnterMsg, 0, 0);
            }
        }
        // 无论发送还是换行，都吞掉这次回车 —— 放行的话排版引擎会再插一个换行。
        return true;
    }

    return DuiRichEdit::OnKeyDown(vk, flags);
}

bool ChatInputRichEdit::OnChar(TCHAR ch)
{
    // 回车产生的字符一并吞掉：按键按下那一步已经处理过了，放行会让引擎再插
    // 一个换行。合成期不拦，交回输入法处理。
    if ((ch == VK_RETURN || ch == 0x0A) && !m_bImeComposing)
    {
        return true;
    }

    // 真的在输入框里打字，也算「回到发送框」。
    //
    // **只认可见字符**：Ctrl+C 之类的控制键同样会产生字符消息（Ctrl+C 是 0x03），
    // 若一并算上，按一次 Ctrl+C 就会把焦点标记改成「发送框」，紧接着再按一次
    // 就复制不到气泡了。
    const TCHAR kFirstPrintable = 0x20;
    if (ch >= kFirstPrintable)
    {
        NotifyFocusInput();
    }

    return DuiRichEdit::OnChar(ch);
}

bool ChatInputRichEdit::OnSetFocus()
{
    NotifyFocusInput();
    return DuiRichEdit::OnSetFocus();
}

bool ChatInputRichEdit::OnKillFocus()
{
    // 失焦时让宿主立即给「正在输入」收尾，否则对端会一直显示「对方正在输入」。
    NotifyTyping(/*bKillFocus=*/true);
    return DuiRichEdit::OnKillFocus();
}

bool ChatInputRichEdit::OnLButtonDown(POINT pt, UINT mkFlags)
{
    // 点输入框同样算「回到发送框」。
    //
    // 光靠获得焦点那条路不够：点消息区时输入框**并没有**失去焦点，再点回
    // 输入框自然也就不会有获得焦点这一步。
    NotifyFocusInput();
    return DuiRichEdit::OnLButtonDown(pt, mkFlags);
}

bool ChatInputRichEdit::OnMouseWheel(POINT pt, short zDelta, UINT mkFlags)
{
    // @提醒浮层正显示时，滚轮归它滚候选列表。
    if (m_nMentionWheelMsg != 0 && HasTarget())
    {
        const LRESULT lTaken = ::SendMessage(m_hNotifyTarget, m_nMentionWheelMsg,
                                             (WPARAM)zDelta, 0);
        if (lTaken != 0)
        {
            return true;   // 浮层消费掉了这次滚轮，输入框自身不滚
        }
    }
    return DuiRichEdit::OnMouseWheel(pt, zDelta, mkFlags);
}

bool ChatInputRichEdit::OnRawMessage(UINT uMsg, WPARAM wParam, LPARAM lParam,
                                     LRESULT& lResult)
{
    // 输入法合成期的起止。记这个状态是为了让回车在合成期内放行 ——
    // 那时的回车是候选词上屏，拦下来用户就打不出中文了。
    if (uMsg == WM_IME_STARTCOMPOSITION)
    {
        m_bImeComposing = true;
    }
    else if (uMsg == WM_IME_ENDCOMPOSITION)
    {
        m_bImeComposing = false;
    }
    return DuiRichEdit::OnRawMessage(uMsg, wParam, lParam, lResult);
}

HRESULT ChatInputRichEdit::TxSiteNotify(DWORD iNotify, void* pv)
{
    // 内容变化：一路通知出去。两个接收方各有各的用途 ——
    // 「正在输入」用来决定要不要给对端发提示，@提醒用来重算光标前的 @ 前缀。
    if (iNotify == EN_CHANGE)
    {
        NotifyTyping(/*bKillFocus=*/false);
        if (m_nMentionChangedMsg != 0 && HasTarget())
        {
            ::PostMessage(m_hNotifyTarget, m_nMentionChangedMsg, 0, 0);
        }
    }
    return DuiRichEdit::TxSiteNotify(iNotify, pv);
}

} // namespace balloonchatwnd
