/**
 *  ChatFontUtil.cpp —— 聊天字体共享工具实现。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#include "stdafx.h"
#include "ChatFontUtil.h"

#include "../IniFile.h"
#include "../UserSessionData.h"   // g_szHomePath
#include "../../balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "../../balloonui/balloonui/Controls/Input/DuiRichEditHost.h"

namespace balloonchatwnd {

namespace {

// flamingo.ini 里承载聊天字体的段名与各键名（全 ASCII）。
const TCHAR* const kSection = _T("chatfont");

// 字号上下限（与面板一致）。
const int kSizeMin = 8;
const int kSizeMax = 72;

// 拼出 flamingo.ini 的完整路径（与网络设置窗同口径）。
CString IniFilePath()
{
    CString path(g_szHomePath);
    path += _T("config\\flamingo.ini");
    return path;
}

} // anonymous namespace

net::TextFont LoadChatFont()
{
    net::TextFont f;
    const CString ini = IniFilePath();
    CIniFile io;

    // set==0（默认）表示从未设过字体：返回 m_valid=false。
    if ((int)io.ReadInt(kSection, _T("set"), 0, ini) == 0)
    {
        f.m_valid = false;
        return f;
    }

    f.m_valid     = true;
    f.m_size      = (int)io.ReadInt(kSection, _T("size"), 14, ini);
    f.m_bold      = io.ReadInt(kSection, _T("bold"),      0, ini) != 0;
    f.m_italic    = io.ReadInt(kSection, _T("italic"),    0, ini) != 0;
    f.m_underline = io.ReadInt(kSection, _T("underline"), 0, ini) != 0;
    f.m_strike    = io.ReadInt(kSection, _T("strike"),    0, ini) != 0;
    f.m_colorRgb  = (unsigned int)io.ReadInt(kSection, _T("color"), 0x1A1A1A, ini) & 0xFFFFFFu;

    if (f.m_size < kSizeMin)
    {
        f.m_size = kSizeMin;
    }
    if (f.m_size > kSizeMax)
    {
        f.m_size = kSizeMax;
    }
    return f;
}

void SaveChatFont(const net::TextFont& font)
{
    const CString ini = IniFilePath();
    CIniFile io;

    if (!font.m_valid)
    {
        io.WriteInt(kSection, _T("set"), 0, ini);
        return;
    }
    io.WriteInt(kSection, _T("set"),       1, ini);
    io.WriteInt(kSection, _T("size"),      font.m_size, ini);
    io.WriteInt(kSection, _T("bold"),      font.m_bold      ? 1 : 0, ini);
    io.WriteInt(kSection, _T("italic"),    font.m_italic    ? 1 : 0, ini);
    io.WriteInt(kSection, _T("underline"), font.m_underline ? 1 : 0, ini);
    io.WriteInt(kSection, _T("strike"),    font.m_strike    ? 1 : 0, ini);
    io.WriteInt(kSection, _T("color"),     (long)(font.m_colorRgb & 0xFFFFFFu), ini);
}

void ApplyChatFontToEdit(balloonwjui::DuiEditHost* edit, const net::TextFont& font)
{
    if (edit == NULL)
    {
        return;
    }
    // 未设过字体：不改动输入框，保留 balloonui 默认外观（微软雅黑 9pt）。
    if (!font.m_valid)
    {
        return;
    }
    edit->SetCtlFont(_T("Microsoft YaHei"), font.m_size,
                     font.m_bold, font.m_italic, font.m_underline, font.m_strike);
    // 先显式给 EDIT 设白底：Win32 EDIT 的 WM_CTLCOLOREDIT 只有在返回画刷时，
    // hdc 上的文字色才生效；若不设背景色（m_ctlBg=CLR_INVALID），HwndHostControl
    // 会 return nullptr 交给 DefWindowProc，文字色被覆盖。聊天输入区本就是白底，
    // 这里设白不改观感，只为让文字色落地。直接调 SetCtlBgColor（绕过 DuiEditHost
    // ::SetBgColor 的"同色即返回"短路，确保 m_ctlBg 被真正赋值）。
    edit->SetCtlBgColor(RGB(0xFF, 0xFF, 0xFF));
    const COLORREF c = RGB((font.m_colorRgb >> 16) & 0xFF,
                           (font.m_colorRgb >> 8) & 0xFF,
                           font.m_colorRgb & 0xFF);
    edit->SetCtlTextColor(c);
}

HFONT ApplyChatFontToEdit(balloonwjui::DuiRichEditHost* edit, const net::TextFont& font)
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
    // DuiRichEditHost::SetDefaultFontFromHFONT 不接管 HFONT 所有权（见其头注释），
    // 故把句柄返回给调用方持有；RichEdit 的字色走 CHARFORMAT2，直接 SetTextColor 即可，
    // 不需要 EDIT 那套"设白底让文字色生效"的迂回。
    edit->SetDefaultFontFromHFONT(hFont);
    edit->SetTextColor(color);
    return hFont;
}

} // namespace balloonchatwnd
