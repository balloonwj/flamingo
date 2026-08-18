/**
 *  GroupChatHeaderInfo.cpp
 *
 *  群聊头部信息块的绘制实现。文字用 GDI DrawText（清晰）。群名 / 人数 / 公告
 *  标题三项由宿主窗口经 setter 写入，本控件只负责画。
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

namespace balloonchatwnd {

namespace {

// ---- 配色 / 字号 -------------------------------------------------------

const COLORREF kNameColor   = RGB(0x2A, 0x2A, 0x2A);   // 群名
const COLORREF kCountColor  = RGB(0x8A, 0x8A, 0x8A);   // 人数
const COLORREF kNoticeColor = RGB(0x8A, 0x8A, 0x8A);   // 公告

const int kNamePx   = 15;   // 群名字号
const int kCountPx  = 12;   // 人数字号
const int kNoticePx = 12;   // 公告字号

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
        L"Microsoft YaHei");
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

// 测量一段文字的像素宽度。
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight, bool bold)
{
    if (text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei");
    if (hFont == NULL)
    {
        return 0;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz.cx;
}

} // anonymous namespace

GroupChatHeaderInfo::GroupChatHeaderInfo()
    : m_memberCount(0)
{
}

void GroupChatHeaderInfo::SetGroupName(LPCTSTR name)
{
    m_groupName = (name != NULL) ? name : _T("");
    Invalidate();
}

void GroupChatHeaderInfo::SetMemberCount(int count)
{
    m_memberCount = count;
    Invalidate();
}

void GroupChatHeaderInfo::SetAnnouncementTitle(LPCTSTR title)
{
    m_announceTitle = (title != NULL) ? title : _T("");
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

    // 两行整体垂直居中；没有公告时只有一行，此时按单行居中，不让群名孤零零
    // 悬在上半区。
    const int kNameLineH   = 20;   // 群名行高
    const int kNoticeLineH = 17;   // 公告行高
    const int kLineGap     = 1;    // 两行之间的间距

    const bool hasNotice = !m_announceTitle.IsEmpty();
    const int  blockH    = hasNotice ? (kNameLineH + kLineGap + kNoticeLineH) : kNameLineH;
    const int  nameTop   = y + (h - blockH) / 2;
    const int  noticeTop = nameTop + kNameLineH + kLineGap;

    // ---- 群名 ----
    const int nameW = MeasureTextWidth(hdc, (LPCWSTR)m_groupName, kNamePx, true);
    RECT nameRc = { x, nameTop, x + nameW + 2, nameTop + kNameLineH };
    DrawGdiText(hdc, (LPCWSTR)m_groupName, nameRc, kNamePx, true, kNameColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    // ---- 人数 "(N 人)"（群名右侧）----
    // 成员列表还没回来时（count 为 0）不画这一段 —— 显示"(0 人)"会让人以为群空了。
    if (m_memberCount > 0)
    {
        CString countStr;
        countStr.Format(_T("(%d 人)"), m_memberCount);
        RECT countRc = { x + nameW + 8, nameTop, x + w, nameTop + kNameLineH };
        DrawGdiText(hdc, (LPCWSTR)countStr, countRc, kCountPx, false, kCountColor,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // ---- 群公告（📌 前缀）----
    if (hasNotice)
    {
        CString noticeLine = _T("📌 ");
        noticeLine += m_announceTitle;
        RECT noticeRc = { x, noticeTop, x + w, noticeTop + kNoticeLineH };
        DrawGdiText(hdc, (LPCWSTR)noticeLine, noticeRc, kNoticePx, false,
                    kNoticeColor,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

} // namespace balloonchatwnd
