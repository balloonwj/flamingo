/**
 *  ChatMarkdownPreview.cpp
 *
 *  Markdown 文档显示控件的实现，说明见 ChatMarkdownPreview.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "ChatMarkdownPreview.h"

#include <shellapi.h>

#include "../../../../third-party/balloonui/balloonui/DuiHost.h"   // GetHost()->m_hWnd：剪贴板与菜单的属主窗口
#include "ChatMarkdown.h"                    // MakePreviewMarkdownStyle / MarkdownImageLinkPrefix
#include "ChatLinkDetect.h"                  // IsSafeLinkUrl：只在浏览器里打开 http、https
#include "../BalloonMarkdown/MarkdownClipboard.h"   // 复制选区：纯文字加 CF_HTML
#include "../../Common/I18n/I18n.h"          // TR / UiFontName
#include "../../Common/Log/IULog.h"

namespace balloonchatwnd {

namespace {

//控件底色：与外面套着的 DuiScrollView 的底色相同（balloonui DuiScrollBar.cpp 的 kScrollViewBg），
//内容不满一屏时，预览控件下方露出的滚动视图与正文区域颜色一致（2026-10-06 实机核对时发现上白下灰）
const COLORREF kPreviewBack = RGB(0xFC, 0xFC, 0xFC);

//空文档提示的颜色与字号（像素）
const COLORREF kEmptyTipColor = RGB(0xA8, 0xA6, 0xA2);
const int kEmptyTipPx = 13;

//文档四周的留白（像素）
const int kPadH = 4;
const int kPadV = 4;

//空文档时报告的内容高度（像素）：一行提示文字加上下留白
const int kEmptyAreaHeight = kEmptyTipPx * 2 + kPadV * 2;

//右键菜单的命令号
const UINT kMenuCopy     = 1;   //复制选中的文字
const UINT kMenuSelAll   = 2;   //全选
const UINT kMenuCopyLink = 3;   //复制链接地址（右键点在链接上时出现）

//GetKeyState 返回值里表示「按下」的最高位
const SHORT kKeyDownMask = static_cast<SHORT>(0x8000);

//判断 UTF-8 文本是否全是空白
bool IsBlank(const std::string& s)
{
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            return false;
    }
    return true;
}

//把一段文字写进剪贴板（CF_UNICODETEXT）
void CopyTextToClipboard(HWND owner, const std::wstring& text)
{
    if (!::OpenClipboard(owner))
        return;
    ::EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem != NULL)
    {
        void* p = ::GlobalLock(mem);
        if (p != NULL)
        {
            memcpy(p, text.c_str(), bytes);
            ::GlobalUnlock(mem);
            if (::SetClipboardData(CF_UNICODETEXT, mem) == NULL)
                ::GlobalFree(mem);
        }
        else
        {
            ::GlobalFree(mem);
        }
    }
    ::CloseClipboard();
}

} // namespace

ChatMarkdownPreview::ChatMarkdownPreview()
    : m_fontPx(0)
    , m_textColor(0)
    , m_hasContent(false)
    , m_docWidth(0)
    , m_docHeight(0)
    , m_hoverCode(-1)
    , m_hoverCopy(-1)
    , m_hoverLink(-1)
    , m_selAnchor(-1)
    , m_selCaret(-1)
    , m_selecting(false)
    , m_pressLink(-1)
{
}

void ChatMarkdownPreview::SetSource(const std::string& utf8Source, int fontPx, COLORREF textColor)
{
    m_source = utf8Source;
    m_fontPx = fontPx;
    m_textColor = textColor;
    m_hoverCode = -1;
    m_hoverCopy = -1;
    m_hoverLink = -1;
    m_selAnchor = -1;
    m_selCaret = -1;
    m_pressLink = -1;
    m_docWidth = 0;
    m_docHeight = 0;
    m_hasContent = !IsBlank(utf8Source) && m_doc.Parse(utf8Source, MarkdownImageLinkPrefix());
    if (!m_hasContent)
        m_doc.Reset();
    Invalidate();
}

const std::string& ChatMarkdownPreview::Source() const
{
    return m_source;
}

bool ChatMarkdownPreview::HasContent() const
{
    return m_hasContent;
}

bool ChatMarkdownPreview::HasSelection() const
{
    return m_hasContent && m_selAnchor >= 0 && m_selCaret >= 0 && m_selAnchor != m_selCaret;
}

std::wstring ChatMarkdownPreview::GetSelectedText() const
{
    if (!HasSelection())
        return std::wstring();
    return m_doc.GetText(m_selAnchor, m_selCaret);
}

void ChatMarkdownPreview::SelectAll()
{
    if (!m_hasContent)
        return;
    m_selAnchor = 0;
    m_selCaret = m_doc.GetTextLength();
    Invalidate();
}

bool ChatMarkdownPreview::CopySelection()
{
    if (!HasSelection())
        return false;
    //纯文字加这一段的 HTML（旧版 DLL 只有纯文字），一次写进剪贴板
    balloonmd::MarkdownClipboardData data;
    if (!balloonmd::BuildMarkdownSelectionClipboard(m_doc, m_selAnchor, m_selCaret, data))
        return false;
    const bool ok = balloonmd::PutMarkdownClipboard(OwnerHwnd(), data);
    LOG_INFO("markdown preview: selection copied, %u chars, html %u bytes, ok=%d.", (unsigned)data.m_text.size(),
             (unsigned)data.m_cfHtml.size(), ok ? 1 : 0);
    return true;
}

int ChatMarkdownPreview::ContentWidth() const
{
    const balloonwjui::DuiControl* parent = GetParent();
    const RECT rc = (parent != NULL) ? parent->GetRect() : m_rcItem;
    return (rc.right - rc.left) - kPadH * 2;
}

int ChatMarkdownPreview::LayoutAt(int width) const
{
    if (!m_hasContent || width <= 0)
        return 0;
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
        return 0;
    const flamingomd::MdStyle style = MakePreviewMarkdownStyle(m_fontPx, m_textColor);
    SIZE size = { 0, 0 };
    const bool ok = m_doc.Layout(hdc, style, width, size);
    ::ReleaseDC(NULL, hdc);
    if (!ok)
        return 0;
    m_docWidth = size.cx;
    m_docHeight = size.cy;
    return size.cy;
}

SIZE ChatMarkdownPreview::GetDesiredSize() const
{
    SIZE size = { 0, 0 };
    const int width = ContentWidth();
    const int h = LayoutAt(width);
    size.cx = width + kPadH * 2;
    //空文档也要留出一行提示文字的高度，否则提示画不出来
    size.cy = (h > 0) ? h + kPadV * 2 : kEmptyAreaHeight;
    return size;
}

void ChatMarkdownPreview::OnPaint(HDC hdc, const RECT& rcDirty)
{
    HBRUSH back = ::CreateSolidBrush(kPreviewBack);
    ::FillRect(hdc, &m_rcItem, back);
    ::DeleteObject(back);

    if (!m_hasContent)
    {
        //空文档：在左上角给出提示
        HFONT font = ::CreateFontW(-kEmptyTipPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                   i18n::UiFontName());
        HGDIOBJ old = ::SelectObject(hdc, font);
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, kEmptyTipColor);
        RECT rc = { m_rcItem.left + kPadH, m_rcItem.top + kPadV, m_rcItem.right - kPadH, m_rcItem.bottom - kPadV };
        ::DrawTextW(hdc, TR(L"没有可预览的内容"), -1, &rc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        ::SelectObject(hdc, old);
        ::DeleteObject(font);
        return;
    }

    LayoutAt(ContentWidth());
    flamingomd::MdPaintState state;
    memset(&state, 0, sizeof(state));
    state.m_cbSize = sizeof(state);
    state.m_selBegin = HasSelection() ? m_selAnchor : -1;
    state.m_selEnd = HasSelection() ? m_selCaret : -1;
    state.m_hoverLink = m_hoverLink;
    state.m_hoverCodeBlock = m_hoverCode;
    state.m_hoverCopyButton = m_hoverCopy;
    state.m_clip = rcDirty;
    m_doc.Paint(hdc, m_rcItem.left + kPadH, m_rcItem.top + kPadV, &state);
}

bool ChatMarkdownPreview::HitAt(POINT pt, flamingomd::MdHitResult& out) const
{
    memset(&out, 0, sizeof(out));
    out.m_cbSize = sizeof(out);
    out.m_linkIndex = -1;
    out.m_codeBlockIndex = -1;
    if (!m_hasContent)
        return false;
    return m_doc.HitTest(pt.x - (m_rcItem.left + kPadH), pt.y - (m_rcItem.top + kPadV), out);
}

HWND ChatMarkdownPreview::OwnerHwnd() const
{
    balloonwjui::DuiHost* host = GetHost();
    return (host != NULL) ? host->m_hWnd : NULL;
}

void ChatMarkdownPreview::OpenLink(int index)
{
    const std::wstring url = m_doc.GetLinkUrl(index);
    if (url.empty() || !IsSafeLinkUrl(url))
    {
        LOG_INFO("markdown preview: link %d not opened (empty or not http/https).", index);
        return;
    }
    ::ShellExecuteW(OwnerHwnd(), L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    LOG_INFO("markdown preview: link %d opened.", index);
}

bool ChatMarkdownPreview::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    SetFocus();
    flamingomd::MdHitResult hit;
    if (!HitAt(pt, hit))
        return false;
    //代码块的「复制」按钮：复制原文，不开始拖选
    if (hit.m_kind == flamingomd::md_hit_code_copy)
    {
        const std::wstring code = m_doc.GetCodeBlockText(hit.m_codeBlockIndex);
        if (!code.empty())
        {
            CopyTextToClipboard(OwnerHwnd(), code);
            LOG_INFO("markdown preview: code block %d copied, %u chars.", hit.m_codeBlockIndex, (unsigned)code.size());
        }
        return true;
    }
    //开始拖选；按在链接上时记下，松开时没有选出文字就打开它
    m_pressLink = (hit.m_kind == flamingomd::md_hit_link) ? hit.m_linkIndex : -1;
    m_selAnchor = hit.m_charPos;
    m_selCaret = hit.m_charPos;
    m_selecting = true;
    Capture();
    Invalidate();
    return true;
}

bool ChatMarkdownPreview::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    if (!m_selecting)
        return false;
    m_selecting = false;
    ReleaseCapture();
    const int pressLink = m_pressLink;
    m_pressLink = -1;
    if (pressLink >= 0 && !HasSelection())
    {
        flamingomd::MdHitResult hit;
        if (HitAt(pt, hit) && hit.m_kind == flamingomd::md_hit_link && hit.m_linkIndex == pressLink)
            OpenLink(pressLink);
    }
    return true;
}

bool ChatMarkdownPreview::OnLButtonDblClk(POINT /*pt*/, UINT /*mkFlags*/)
{
    SelectAll();
    return m_hasContent;
}

bool ChatMarkdownPreview::OnRButtonDown(POINT pt, UINT /*mkFlags*/)
{
    HWND owner = OwnerHwnd();
    if (!m_hasContent || !::IsWindow(owner))
        return false;
    flamingomd::MdHitResult hit;
    const int link = (HitAt(pt, hit) && hit.m_kind == flamingomd::md_hit_link) ? hit.m_linkIndex : -1;
    HMENU menu = ::CreatePopupMenu();
    if (menu == NULL)
        return true;
    if (link >= 0)
        ::AppendMenuW(menu, MF_STRING, kMenuCopyLink, TR(L"复制链接地址"));
    ::AppendMenuW(menu, MF_STRING | (HasSelection() ? 0 : MF_GRAYED), kMenuCopy, TR(L"复制"));
    ::AppendMenuW(menu, MF_STRING, kMenuSelAll, TR(L"全选"));
    POINT scr = pt;
    ::ClientToScreen(owner, &scr);
    const UINT cmd = (UINT)::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, scr.x, scr.y, 0,
                                            owner, NULL);
    ::DestroyMenu(menu);
    switch (cmd)
    {
    //复制选中的文字
    case kMenuCopy:
        CopySelection();
        break;
    //全选
    case kMenuSelAll:
        SelectAll();
        break;
    //复制链接地址（已补全 http:// 或 mailto:）
    case kMenuCopyLink:
        CopyTextToClipboard(owner, m_doc.GetLinkUrl(link));
        break;
    //没有选择任何一项
    default:
        break;
    }
    return true;
}

bool ChatMarkdownPreview::OnMouseMove(POINT pt, UINT /*mkFlags*/)
{
    flamingomd::MdHitResult hit;
    const bool hitOk = HitAt(pt, hit);
    //拖选：选区终点跟着鼠标走（DLL 对文档外的点给出最近的字符边界）
    if (m_selecting)
    {
        if (hitOk && hit.m_charPos != m_selCaret)
        {
            m_selCaret = hit.m_charPos;
            Invalidate();
        }
        return true;
    }
    int code = -1;
    int copy = -1;
    int link = -1;
    if (hitOk)
    {
        code = hit.m_codeBlockIndex;
        if (hit.m_kind == flamingomd::md_hit_code_copy)
            copy = hit.m_codeBlockIndex;
        if (hit.m_kind == flamingomd::md_hit_link)
            link = hit.m_linkIndex;
    }
    if (code != m_hoverCode || copy != m_hoverCopy || link != m_hoverLink)
    {
        m_hoverCode = code;
        m_hoverCopy = copy;
        m_hoverLink = link;
        Invalidate();
    }
    return false;
}

bool ChatMarkdownPreview::OnMouseLeave()
{
    if (m_hoverCode != -1 || m_hoverCopy != -1 || m_hoverLink != -1)
    {
        m_hoverCode = -1;
        m_hoverCopy = -1;
        m_hoverLink = -1;
        Invalidate();
    }
    return false;
}

bool ChatMarkdownPreview::OnSetCursor(POINT pt)
{
    flamingomd::MdHitResult hit;
    if (!HitAt(pt, hit))
        return false;
    if (hit.m_kind == flamingomd::md_hit_code_copy || hit.m_kind == flamingomd::md_hit_link)
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    if (hit.m_kind == flamingomd::md_hit_text)
    {
        ::SetCursor(::LoadCursor(NULL, IDC_IBEAM));
        return true;
    }
    return false;
}

bool ChatMarkdownPreview::OnKeyDown(UINT vk, UINT /*flags*/)
{
    const bool ctrl = (::GetKeyState(VK_CONTROL) & kKeyDownMask) != 0;
    if (!ctrl)
        return false;
    //Ctrl+C：复制选区
    if (vk == 'C')
        return CopySelection();
    //Ctrl+A：全选
    if (vk == 'A')
    {
        SelectAll();
        return m_hasContent;
    }
    return false;
}

} // namespace balloonchatwnd
