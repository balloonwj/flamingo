/**
 *  ChatCodeCardControl.cpp
 *
 *  代码片段卡片控件，见 ChatCodeCardControl.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖，与同目录 ChatFileCardControl.cpp 相同 ----
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

#include "ChatCodeCardControl.h"
#include "ChatMessageList.h"               // WM_CHAT_OPEN_CODE_VIEWER / CodeViewerOpenRequest / PutTextToClipboard
#include "../../Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"

namespace balloonchatwnd {

namespace {

//卡片的排版选项：超过折叠行数时只显示前面几行，底栏为「查看全部」、点了打开代码查看窗
//（列表的行高是预先按宽度算好的，不在卡片里展开）。
CodeBlockOptions CardOptions(bool copied)
{
    CodeBlockOptions options;
    options.m_footerOpensViewer = true;
    options.m_copied = copied;
    return options;
}

} // anonymous namespace

ChatCodeCardControl::ChatCodeCardControl()
    : m_layoutWidth(-1)
    , m_hoverPart(kCodePartNone)
    , m_copied(false)
{
}

SIZE ChatCodeCardControl::MeasureCard(const std::wstring& code, int lang, int maxWidth,
                                      const std::vector<codesnippet::CodeNote>& notes)
{
    SIZE size = { 0, 0 };
    const CodeBlockContent content = PrepareCodeBlockContent(code, lang, notes);
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return size;
    }
    CodeBlockLayout layout;
    LayoutCodeBlock(hdc, content, CardOptions(false), maxWidth, layout);
    ::ReleaseDC(NULL, hdc);
    return layout.m_size;
}

void ChatCodeCardControl::SetCode(const std::wstring& code, int lang, const std::string& key,
                                  const std::vector<codesnippet::CodeNote>& notes)
{
    m_content = PrepareCodeBlockContent(code, lang, notes);
    m_key = key;
    m_notes = notes;
    m_layoutWidth = -1;
    m_hoverPart = kCodePartNone;
    m_copied = false;
    Invalidate();
}

void ChatCodeCardControl::EnsureLayout(HDC hdc)
{
    const RECT& rc = GetRect();
    const int width = rc.right - rc.left;
    if (width == m_layoutWidth)
    {
        return;
    }
    LayoutCodeBlock(hdc, m_content, CardOptions(m_copied), width, m_layout);
    m_layoutWidth = width;
}

void ChatCodeCardControl::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    if (!IsVisible() || hdc == NULL)
    {
        return;
    }
    EnsureLayout(hdc);
    const RECT& rc = GetRect();
    PaintCodeBlock(hdc, m_layout, rc.left, rc.top, m_hoverPart);
}

int ChatCodeCardControl::PartAtPoint(POINT pt) const
{
    if (m_layoutWidth < 0)
    {
        return kCodePartNone;
    }
    const RECT& rc = GetRect();
    POINT local;
    local.x = pt.x - rc.left;
    local.y = pt.y - rc.top;
    return HitTestCodeBlock(m_layout, local);
}

bool ChatCodeCardControl::OnMouseMove(POINT pt, UINT /*mkFlags*/)
{
    int part = PartAtPoint(pt);
    //只有「复制」与底栏加悬停效果（下划线），正文不加
    if (part == kCodePartBody)
    {
        part = kCodePartNone;
    }
    if (part != m_hoverPart)
    {
        m_hoverPart = part;
        Invalidate();
    }
    return true;
}

bool ChatCodeCardControl::OnMouseLeave()
{
    bool changed = (m_hoverPart != kCodePartNone);
    m_hoverPart = kCodePartNone;
    //光标离开卡片：「已复制」恢复为「复制」
    if (m_copied)
    {
        m_copied = false;
        SetCodeBlockCopied(m_layout, false);
        changed = true;
    }
    if (changed)
    {
        Invalidate();
    }
    return true;
}

bool ChatCodeCardControl::OnSetCursor(POINT pt)
{
    if (PartAtPoint(pt) == kCodePartNone)
    {
        return false;
    }
    ::SetCursor(::LoadCursor(NULL, IDC_HAND));
    return true;
}

bool ChatCodeCardControl::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    const int part = PartAtPoint(pt);
    switch (part)
    {
    //「复制」：原文写进剪贴板
    case kCodePartCopy:
        CopyCode();
        return true;

    //底栏「查看全部」与卡片正文：打开代码查看窗
    case kCodePartFooter:
    case kCodePartBody:
        PostViewerOpen();
        return true;

    //卡片外：不处理，交给所在的行
    default:
        return false;
    }
}

bool ChatCodeCardControl::OnLButtonDblClk(POINT /*pt*/, UINT /*mkFlags*/)
{
    return true;
}

void ChatCodeCardControl::CopyCode()
{
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    //只写纯文字（与聊天窗相同）：Flamingo 自己的剪贴板格式会把正文里的 /f["N"] 当作表情还原
    const std::wstring text = CodeTextForClipboard(m_content.m_code);
    PutTextToClipboard(hwnd, text.c_str());
    m_copied = true;
    SetCodeBlockCopied(m_layout, true);
    Invalidate();
    LOG_INFO("code card: code snippet copied, key=%s, %u chars.", m_key.c_str(), (unsigned)text.size());
}

void ChatCodeCardControl::PostViewerOpen()
{
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    CodeViewerOpenRequest* pReq = new CodeViewerOpenRequest();
    pReq->m_code = m_content.m_code;
    pReq->m_lang = m_content.m_lang;
    pReq->m_key = m_key;
    pReq->m_notes = m_notes;
    if (!::PostMessage(hwnd, WM_CHAT_OPEN_CODE_VIEWER, 0, (LPARAM)pReq))
    {
        delete pReq;
        return;
    }
    LOG_INFO("code card: open code viewer requested, key=%s, lines=%d, notes=%u.", m_key.c_str(),
             m_content.m_lineCount, (unsigned)m_notes.size());
}

} // namespace balloonchatwnd
