/**
 *  帖子群里输入文字用的富文本框，见 ThreadCommentEdit.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadCommentEdit.h"

using balloonwjui::DuiRichEdit;

namespace threadgroup {

namespace {

const UINT kPrevKeyDownFlag = 0x4000;         //WM_KEYDOWN 的 lParam 里「这是自动重复」的标志位（按键回调的 flags 取 lParam 高 16 位）
const SHORT kKeyDownMask = static_cast<SHORT>(0x8000);   //GetKeyState 返回值里「按下」的那一位
const wchar_t* const kNewLine = L"\r\n";      //换行时插入的文字
const TCHAR kLineFeedChar = 0x0A;             //Ctrl+Enter 产生的字符（换行符 LF）

} // namespace

CThreadCommentEdit::CThreadCommentEdit()
    : m_target(NULL)
    , m_msg(0)
    , m_wParam(0)
    , m_enterSends(true)
    , m_imeComposing(false)
{
}

void CThreadCommentEdit::SetSendTarget(HWND hwnd, UINT msg, WPARAM wParam)
{
    m_target = hwnd;
    m_msg = msg;
    m_wParam = wParam;
}

void CThreadCommentEdit::SetEnterSends(bool enterSends)
{
    m_enterSends = enterSends;
}

bool CThreadCommentEdit::OnKeyDown(UINT vk, UINT flags)
{
    //输入法合成期内不介入：那时的回车是候选词上屏，拦下来会打不出中文
    if (vk != VK_RETURN || m_imeComposing)
        return DuiRichEdit::OnKeyDown(vk, flags);
    const bool ctrlDown = (::GetKeyState(VK_CONTROL) & kKeyDownMask) != 0;
    //这次回车是不是「发送」：Enter 发送时不按 Ctrl，Ctrl+Enter 发送时按着 Ctrl
    const bool sendKey = (ctrlDown != m_enterSends);
    if (!sendKey || m_target == NULL || IsReadOnly())
    {
        if (!IsReadOnly())
            ReplaceSel(kNewLine);
    }
    else if ((flags & kPrevKeyDownFlag) == 0)
    {
        //发送；按住回车自动重复时不连发
        ::PostMessageW(m_target, m_msg, m_wParam, 0);
    }
    //无论发送还是换行都吞掉这次回车，放行的话排版引擎会再插一个换行
    return true;
}

bool CThreadCommentEdit::OnChar(TCHAR ch)
{
    if ((ch == VK_RETURN || ch == kLineFeedChar) && !m_imeComposing)
        return true;
    return DuiRichEdit::OnChar(ch);
}

bool CThreadCommentEdit::OnRawMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& lResult)
{
    //输入法合成期的起止：合成期内的回车放行
    if (uMsg == WM_IME_STARTCOMPOSITION)
        m_imeComposing = true;
    else if (uMsg == WM_IME_ENDCOMPOSITION)
        m_imeComposing = false;
    return DuiRichEdit::OnRawMessage(uMsg, wParam, lParam, lResult);
}

} // namespace threadgroup
