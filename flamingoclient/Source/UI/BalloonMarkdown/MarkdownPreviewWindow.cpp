/**
 *  MarkdownPreviewWindow.cpp
 *
 *  Markdown 预览窗口的实现，说明见 MarkdownPreviewWindow.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "MarkdownPreviewWindow.h"

#include "MarkdownEditorLayout.h"
#include "MarkdownEditorManager.h"
#include "MarkdownExport.h"
#include "MarkdownClipboard.h"     // 「复制全文（带格式）」：纯文字加 CF_HTML
#include "MarkdownEditorModel.h"   // MarkdownTitleFromSource
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../BalloonChatWnd/ChatMarkdownPreview.h"
#include "../BalloonChatWnd/ChatMarkdown.h"   // MarkdownTextStyleFromFont
#include "../../Common/I18n/I18n.h"
#include "../../Common/Log/IULog.h"

using namespace balloonwjui;

namespace balloonmd {

namespace {

//标题栏文字与按钮符号的颜色
const COLORREF kCaptionColor = RGB(0x55, 0x55, 0x55);

//多个预览窗口相对屏幕中心错开的距离（像素）与循环周期（个）
const int kCascadeOffsetPx = 28;
const int kCascadeWrap = 8;

//复制方式菜单的命令号
const UINT kCopySource = 1;   //复制 Markdown 源文本
const UINT kCopyPlain  = 2;   //复制为纯文字
const UINT kCopyImage  = 3;   //复制为图片
const UINT kCopyRich   = 4;   //复制全文（带格式）：纯文字加 HTML

} // namespace

CMarkdownPreviewWindow::CMarkdownPreviewWindow()
    : m_pPreview(NULL)
    , m_pScroll(NULL)
{
}

bool CMarkdownPreviewWindow::Open(const MarkdownPreviewDoc& doc, int cascadeIndex)
{
    m_doc = doc;
    const std::wstring title = m_doc.m_title.empty() ? std::wstring(TR(L"Markdown 预览")) : m_doc.m_title;
    if (Create(NULL, CWindow::rcDefault, title.c_str(), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("markdown preview: Create failed.");
        return false;
    }
    SetTitle(title.c_str());
    SetTitleBarHeight(kMdPvTitleBarH);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kCaptionColor);
    SetCaptionGlyphColor(kCaptionColor);
    SetButtons(true, true, true);
    SetResizable(true);
    //最小尺寸是整窗尺寸（含标题栏），见开发约定文档 UI 约定第 7 条
    SetMinSize(kMdPvMinWidth, kMdPvMinHeight);
    //标题栏程序图标（开发约定文档 UI 约定第 10 条）：标题栏透明，底色取根控件的底色；窗口没有所有者，会出现在任务栏
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, kMdWindowBackColor));
    m_programIcons.ApplyTaskIcons(m_hWnd);
    SetClientContent(BuildMarkdownPreviewBody());
    DuiControl* root = GetClientContent();
    if (root != NULL)
    {
        m_pPreview = dynamic_cast<balloonchatwnd::ChatMarkdownPreview*>(root->FindCtrlById(kMdPvIdPreview));
        m_pScroll = dynamic_cast<DuiScrollView*>(root->FindCtrlById(kMdPvIdScroll));
        DuiLabel* meta = dynamic_cast<DuiLabel*>(root->FindCtrlById(kMdPvIdMeta));
        if (meta != NULL)
            meta->SetText(m_doc.m_meta.c_str());
    }
    ResizeClient(kMdPvWindowWidth, kMdPvWindowHeight);
    CenterWindow(::GetDesktopWindow());
    const int offset = (cascadeIndex % kCascadeWrap) * kCascadeOffsetPx;
    if (offset > 0)
    {
        RECT rc;
        GetWindowRect(&rc);
        SetWindowPos(NULL, rc.left + offset, rc.top + offset, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (m_pPreview != NULL)
    {
        int fontPx = 0;
        COLORREF color = 0;
        //与消息卡片、编辑器预览同一套默认字号与颜色
        balloonchatwnd::MarkdownTextStyleFromFont(net::TextFont(), fontPx, color);
        m_pPreview->SetSource(m_doc.m_sourceUtf8, fontPx, color);
    }
    if (m_pScroll != NULL)
        m_pScroll->ForceLayout(m_pScroll->GetRect());
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    if (m_pPreview != NULL)
        SetDuiFocus(m_pPreview);
    LOG_INFO("markdown preview opened: bytes=%u, key=%s.", (unsigned)m_doc.m_sourceUtf8.size(), m_doc.m_key.c_str());
    return true;
}

const std::string& CMarkdownPreviewWindow::Key() const
{
    return m_doc.m_key;
}

void CMarkdownPreviewWindow::Activate()
{
    if (!IsWindow())
        return;
    ShowWindow(::IsIconic(m_hWnd) ? SW_RESTORE : SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
}

void CMarkdownPreviewWindow::OnFinalMessage(HWND /*hWnd*/)
{
    //管理器在这里 delete 本对象，之后不得再访问任何成员
    CMarkdownEditorManager::GetInstance().OnPreviewDestroyed(this);
}

UINT CMarkdownPreviewWindow::TrackMenuBelow(UINT buttonId, DuiMenu& menu)
{
    DuiControl* root = GetClientContent();
    DuiControl* button = (root != NULL) ? root->FindCtrlById(buttonId) : NULL;
    if (button == NULL || menu.GetCount() == 0)
        return 0;
    const RECT rc = button->GetRect();
    POINT anchor = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &anchor);
    return menu.TrackPopup(anchor.x, anchor.y, m_hWnd);
}

void CMarkdownPreviewWindow::DoCopyMenu()
{
    DuiMenu menu;
    menu.AppendItem(kCopySource, TR(L"复制 Markdown 源文本"));
    menu.AppendItem(kCopyRich, TR(L"复制全文（带格式）"));
    menu.AppendItem(kCopyPlain, TR(L"复制为纯文字"));
    menu.AppendItem(kCopyImage, TR(L"复制为图片"));
    const UINT chosen = TrackMenuBelow(kMdPvIdCopy, menu);
    std::wstring error;
    bool ok = true;
    switch (chosen)
    {
    //源文本：可以粘进编辑器继续修改
    case kCopySource:
        ok = CopyMarkdownSourceToClipboard(m_hWnd, m_doc.m_sourceUtf8);
        break;
    //带格式：纯文字加 HTML，粘到 Word、邮件里保留标题、列表、表格与代码颜色
    case kCopyRich:
        ok = CopyMarkdownRichTextToClipboard(m_hWnd, m_doc.m_sourceUtf8);
        break;
    //纯文字：去掉标记
    case kCopyPlain:
        ok = CopyMarkdownPlainTextToClipboard(m_hWnd, m_doc.m_sourceUtf8);
        break;
    //图片：整篇文档画成一张图
    case kCopyImage:
        ok = CopyMarkdownImageToClipboard(m_hWnd, m_doc.m_sourceUtf8, &error);
        break;
    //没有选择
    default:
        return;
    }
    if (!ok)
    {
        const std::wstring tip = error.empty() ? std::wstring(TR(L"复制失败，请稍后重试。")) : error;
        ::MessageBoxW(m_hWnd, tip.c_str(), TR(L"Markdown 预览"), MB_OK | MB_ICONWARNING);
    }
    LOG_INFO("markdown preview: copy command %u, ok=%d.", chosen, ok ? 1 : 0);
}

void CMarkdownPreviewWindow::DoExportMenu()
{
    DuiMenu menu;
    for (int format = md_export_md; format <= md_export_count; ++format)
    {
        if (IsMarkdownExportAvailable(format))
            menu.AppendItem(static_cast<UINT>(format), MarkdownExportMenuText(format).c_str());
    }
    const UINT chosen = TrackMenuBelow(kMdPvIdExport, menu);
    if (chosen == 0)
        return;
    std::wstring name = m_doc.m_title.empty() ? MarkdownTitleFromSource(m_doc.m_sourceUtf8) : m_doc.m_title;
    std::wstring error;
    if (!ExportMarkdownWithDialog(m_hWnd, m_doc.m_sourceUtf8, name, static_cast<int>(chosen), &error) && !error.empty())
        ::MessageBoxW(m_hWnd, error.c_str(), TR(L"Markdown 预览"), MB_OK | MB_ICONWARNING);
}

LRESULT CMarkdownPreviewWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    DestroyWindow();
    return 0;
}

LRESULT CMarkdownPreviewWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify != NULL && pNotify->code == DUIN_CLICK)
    {
        //三个按钮都会弹出菜单或新窗口：等这次点击通知处理完再执行
        WPARAM action = 0;
        if (pNotify->ctrlId == kMdPvIdCopy)
            action = md_preview_action_copy_menu;
        else if (pNotify->ctrlId == kMdPvIdExport)
            action = md_preview_action_export_menu;
        else if (pNotify->ctrlId == kMdPvIdOpenEditor)
            action = md_preview_action_open_editor;
        if (action != 0)
        {
            PostMessage(kMsgMarkdownPreviewDeferred, action);
            return 0;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CMarkdownPreviewWindow::OnKeyDownMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //Esc：关闭窗口
    if (wParam == VK_ESCAPE)
    {
        PostMessage(WM_CLOSE);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CMarkdownPreviewWindow::OnDeferred(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    switch (wParam)
    {
    //复制方式菜单
    case md_preview_action_copy_menu:
        DoCopyMenu();
        break;
    //导出格式菜单
    case md_preview_action_export_menu:
        DoExportMenu();
        break;
    //在编辑器中打开：不预选发送对象，由用户在编辑器里选择
    case md_preview_action_open_editor:
        CMarkdownEditorManager::GetInstance().OpenEditor(m_doc.m_sourceUtf8, NULL);
        break;
    //不认识的取值：忽略
    default:
        break;
    }
    return 0;
}

LRESULT CMarkdownPreviewWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    m_pPreview = NULL;
    m_pScroll = NULL;
    LOG_INFO("markdown preview: window destroyed, key=%s.", m_doc.m_key.c_str());
    bHandled = FALSE;
    return 0;
}

} // namespace balloonmd
