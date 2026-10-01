/**
 *  ChatFontUtil.cpp —— 聊天字体共享工具实现。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#include "stdafx.h"
#include "ChatFontUtil.h"

#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"

namespace balloonchatwnd {

HFONT ApplyChatFontToEdit(balloonwjui::DuiRichEdit* edit, const net::TextFont& font)
{
    if (edit == NULL)
    {
        return NULL;
    }
    // 翻译成 LOGFONT + 文字色；font.m_valid==false 时返回 false，此处即不改控件、
    // 返回 NULL，保留 RichEdit 默认外观（向后兼容未设字体的会话）。
    LOGFONT  lf;
    COLORREF color = 0;
    if (!ChatFontToLogFont(font, lf, color))
    {
        return NULL;
    }
    HFONT hFont = ::CreateFontIndirect(&lf);
    if (hFont == NULL)
    {
        return NULL;
    }
    // DuiRichEdit::SetDefaultFontFromHFONT 不接管 HFONT 所有权（见其头注释），
    // 故把句柄返回给调用方持有；RichEdit 的字色走 CHARFORMAT2，直接 SetTextColor 即可，
    // 不需要 EDIT 那套"设白底让文字色生效"的迂回。
    edit->SetDefaultFontFromHFONT(hFont);
    edit->SetTextColor(color);
    return hFont;
}

void ResetChatInputFont(balloonwjui::DuiRichEdit* edit, COLORREF defaultColor)
{
    if (edit == NULL)
    {
        return;
    }
    // 传 NULL 时控件改用 balloonui 的默认字体（见 DuiRichEdit::SetDefaultFontFromHFONT）。
    edit->SetDefaultFontFromHFONT(NULL);
    edit->SetTextColor(defaultColor);
}

} // namespace balloonchatwnd
