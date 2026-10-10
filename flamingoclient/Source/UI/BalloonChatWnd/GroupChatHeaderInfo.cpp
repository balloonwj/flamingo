/**
 *  GroupChatHeaderInfo.cpp
 *
 *  群聊头部信息块的绘制实现。文字用 GDI DrawText（清晰）。群简介由宿主窗口经
 *  setter 写入，本控件只负责画（2026-10-03 起不再画群名与人数，2026-10-04 起由公告标题
 *  改为群简介，见头文件）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-22
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
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

#include "GroupChatHeaderInfo.h"
#include "../../Common/I18n/I18n.h"   // i18n::UiFontName：当前界面语言的界面字体

namespace balloonchatwnd {

namespace {

// ---- 配色 / 字号 -------------------------------------------------------

const COLORREF kIntroColor = RGB(0x8A, 0x8A, 0x8A);   // 群简介文字颜色（浅灰，作为标题栏下方的辅助信息）

const int kIntroPx    = 12;   // 群简介字号（像素）
const int kIntroLineH = 17;   // 群简介一行的高度（像素），这一行在信息块里垂直居中

const wchar_t kFullWidthSpace = 0x3000;   // 全角空格，整理群简介时与半角空格一样从首尾去掉

// 是否为需要折叠成一个空格的控制字符：换行、回车、制表符
bool IsIntroBreakChar(wchar_t ch)
{
    return ch == L'\r' || ch == L'\n' || ch == L'\t';
}

// 是否为需要从首尾去掉的空白：半角空格或全角空格
bool IsIntroTrimChar(wchar_t ch)
{
    return ch == L' ' || ch == kFullWidthSpace;
}

// 用 GDI 绘制文字 —— DrawText 经系统 ClearType 光栅化，清晰锐利。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        i18n::UiFontName());
    if (hFont == NULL)
    {
        return;
    }
    HFONT    hOldFont  = (HFONT)::SelectObject(hdc, hFont);
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, color);
    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r, dtFlags);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

} // anonymous namespace

CString MakeHeaderIntroLine(LPCTSTR intro)
{
    CString line;
    if (intro == NULL)
    {
        return line;
    }

    // 每一段连续的换行 / 回车 / 制表符只换成一个空格："第一行\r\n\r\n第二行" 得到 "第一行 第二行"
    bool inBreak = false;
    for (LPCTSTR p = intro; *p != _T('\0'); ++p)
    {
        if (IsIntroBreakChar(*p))
        {
            if (!inBreak)
            {
                line += _T(' ');
            }
            inBreak = true;
            continue;
        }
        inBreak = false;
        line += *p;
    }

    // 去掉首尾的半角 / 全角空格（包括上一步换出来的空格）
    int begin = 0;
    int end = line.GetLength();
    while (begin < end && IsIntroTrimChar(line[begin]))
    {
        ++begin;
    }
    while (end > begin && IsIntroTrimChar(line[end - 1]))
    {
        --end;
    }
    return line.Mid(begin, end - begin);
}

GroupChatHeaderInfo::GroupChatHeaderInfo()
{
}

void GroupChatHeaderInfo::SetGroupIntro(LPCTSTR intro)
{
    m_introLine = MakeHeaderIntroLine(intro);
    Invalidate();
}

void GroupChatHeaderInfo::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // 只有一行群简介，在信息块里垂直居中；没有群简介时整块空白
    if (m_introLine.IsEmpty())
    {
        return;
    }
    const int introTop = y + (h - kIntroLineH) / 2;

    // 文字矩形与本控件同宽：DrawText 在矩形内裁剪，DT_END_ELLIPSIS 让过长的简介以省略号结尾，
    // 因此不会画到右侧的图标按钮上。DT_NOPREFIX：简介是用户填写的文字，其中的 & 要原样显示，
    // 不能被当成快捷键前缀（否则 & 本身不显示，下一个字还会带下划线）。
    RECT introRc = { x, introTop, x + w, introTop + kIntroLineH };
    DrawGdiText(hdc, (LPCWSTR)m_introLine, introRc, kIntroPx, false,
                kIntroColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

} // namespace balloonchatwnd
