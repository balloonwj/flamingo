/**
 *  MarkdownEditorWindow.cpp
 *
 *  Markdown 编辑器窗口的实现，说明见 MarkdownEditorWindow.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "MarkdownEditorWindow.h"

#include <commdlg.h>
#include <vector>

#include "MarkdownEditorLayout.h"
#include "MarkdownEditorModel.h"
#include "MarkdownEditorManager.h"
#include "MarkdownExport.h"
#include "MarkdownFindReplace.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiSplitter.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../BalloonChatWnd/ChatMarkdownPreview.h"
#include "../BalloonChatWnd/ChatMarkdown.h"              // MarkdownTextStyleFromFont
#include "../BalloonChatWnd/MarkdownEditCommands.h"
#include "../../Feature/Markdown/MarkdownDocument.h"     // MarkdownToPlainTextUtf8
#include "../../Common/Util/FileDialogFilter.h"
#include "../../Common/I18n/I18n.h"
#include "../../Common/Log/IULog.h"

using namespace balloonwjui;

namespace balloonmd {

namespace {

//标题栏文字与按钮符号的颜色
const COLORREF kCaptionColor = RGB(0x55, 0x55, 0x55);

//编辑框字号（96 DPI 下的像素，按窗口 DPI 换算）
const int kEditorFontPx = 15;
const int kBaseDpi = 96;

//定时器：文字变化后延时刷新预览、状态文字过一会儿清除；编号取得较大，避开 balloonui 宿主窗口的定时器
const UINT_PTR kTimerPreview = 0xC1A1;
const UINT_PTR kTimerStatus = 0xC1A2;
const UINT kPreviewDelayMs = 300;   //停止输入多久后刷新预览（毫秒）
const UINT kStatusShowMs = 3000;    //状态文字显示多久（毫秒）

//多个编辑器相对屏幕中心错开的距离（像素）与循环周期（个）：超过周期后回到中心，避免窗口一直往右下走出屏幕
const int kCascadeOffsetPx = 28;
const int kCascadeWrap = 8;

//收起预览时给分隔条的位置：远大于任何窗口宽度，会被限制到最右边，窗口缩放后仍保持收起
const int kCollapsedSplitPx = 1 << 20;

//文件对话框里文件名缓冲区的长度（字符）
const DWORD kPathBufferChars = 1024;

//选择窗里显示的摘要最多多少个字符
const size_t kPickerDigestMaxChars = 120;

//格式命令没有选中文字时插入的占位文字（未翻译的原文），按 MarkdownEditCommand 的顺序
const wchar_t* const kPlaceholders[balloonchatwnd::kMarkdownEditCommandCount] =
{
    TR_NOOP(L"标题"),
    TR_NOOP(L"加粗文字"),
    TR_NOOP(L"斜体文字"),
    TR_NOOP(L"下划线文字"),
    TR_NOOP(L"删除的文字"),
    TR_NOOP(L"代码"),
    TR_NOOP(L"代码"),
    TR_NOOP(L"链接文字"),
    TR_NOOP(L"引用的文字"),
    TR_NOOP(L"列表项"),
    TR_NOOP(L"列表项"),
    TR_NOOP(L"待办事项"),
    L"",   //表格：插入的是带列名的模板，不用占位文字
    L"",   //分割线：插入的是「---」，不用占位文字
    TR_NOOP(L"警告框内容"),
};

//GetKeyState 返回值里表示「按下」的最高位
const SHORT kKeyDownMask = static_cast<SHORT>(0x8000);

//标题级别菜单：命令号为 kHeadingMenuIdBase + 级别（正文为级别 0；TrackPopup 用 0 表示没有选择，所以从 100 起）
const UINT kHeadingMenuIdBase = 100;

//标题级别菜单里的级别个数（一级 ~ 六级）
const int kHeadingLevelCount = 6;

//标题级别菜单各级的文字（一级 ~ 六级）；只做标记，使用处用 TR 取译文，快捷键「\tCtrl+n」在使用处拼上
const wchar_t* const kHeadingMenuTexts[kHeadingLevelCount] =
{
    TR_NOOP(L"一级标题"),
    TR_NOOP(L"二级标题"),
    TR_NOOP(L"三级标题"),
    TR_NOOP(L"四级标题"),
    TR_NOOP(L"五级标题"),
    TR_NOOP(L"六级标题"),
};

//打开查找条时，编辑框里选中的文字不超过这么多字符（且不跨行）才填进查找框
const long kFindPrefillMaxChars = 200;

//警告框类型菜单：命令号为 kAlertMenuIdBase + 类型（1 ~ 5；TrackPopup 用 0 表示没有选择）
const UINT kAlertMenuIdBase = 100;

/**
 *  警告框类型在界面上的名称，与 Markdown 模块宿主交给 DLL 的标题相同（MarkdownModuleHost.cpp，同一上下文，
 *  共用一条译文）。
 *  @param type 类型 1 ~ 5（enum MarkdownAlertType）
 *  @return 译文；越界时返回空串
 */
const wchar_t* AlertMenuTitle(int type)
{
    switch (type)
    {
    //提示 [!NOTE]
    case balloonchatwnd::md_edit_alert_note:
        return TRC(L"Markdown 警告框", L"提示");
    //技巧 [!TIP]
    case balloonchatwnd::md_edit_alert_tip:
        return TRC(L"Markdown 警告框", L"技巧");
    //重要 [!IMPORTANT]
    case balloonchatwnd::md_edit_alert_important:
        return TRC(L"Markdown 警告框", L"重要");
    //警告 [!WARNING]
    case balloonchatwnd::md_edit_alert_warning:
        return TRC(L"Markdown 警告框", L"警告");
    //注意 [!CAUTION]
    case balloonchatwnd::md_edit_alert_caution:
        return TRC(L"Markdown 警告框", L"注意");
    //越界的类型
    default:
        return L"";
    }
}

//WM_CHAR 里第一个可显示字符：小于它的是控制字符
const WPARAM kFirstPrintableChar = 0x20;

//UTF-8 转 UTF-16
std::wstring FromUtf8(const std::string& text)
{
    if (text.empty())
        return std::wstring();
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), NULL, 0);
    if (len <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], len);
    return out;
}

//当前宿主是否可以发送
bool IsSendReady()
{
    IMarkdownEditorHost* host = CMarkdownEditorManager::GetInstance().GetHost();
    return host != NULL && host->IsMarkdownSendReady();
}

} // namespace

CMarkdownEditorWindow::CMarkdownEditorWindow()
    : m_editorFont(NULL)
    , m_dirty(false)
    , m_settingText(false)
    , m_previewVisible(true)
    , m_savedSplitPx(0)
    , m_pickerOpen(false)
    , m_swallowCtrlChar(false)
    , m_imeComposing(false)
    , m_findVisible(false)
    , m_pEditor(NULL)
    , m_pPreview(NULL)
    , m_pPreviewScroll(NULL)
    , m_pSplitter(NULL)
    , m_pPreviewToggle(NULL)
    , m_pTargets(NULL)
    , m_pStatus(NULL)
    , m_pPick(NULL)
    , m_pSend(NULL)
    , m_pFindEdit(NULL)
    , m_pReplaceEdit(NULL)
    , m_pMatchCase(NULL)
    , m_pFindCount(NULL)
{
}

CMarkdownEditorWindow::~CMarkdownEditorWindow()
{
    if (m_editorFont != NULL)
    {
        ::DeleteObject(m_editorFont);
        m_editorFont = NULL;
    }
}

bool CMarkdownEditorWindow::Open(const std::string& sourceUtf8, const MarkdownSendTarget* target, int cascadeIndex)
{
    if (Create(NULL, CWindow::rcDefault, TR(L"Markdown 编辑器"), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("markdown editor: Create failed.");
        return false;
    }
    SetTitleBarHeight(kMdEdTitleBarH);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kCaptionColor);
    SetCaptionGlyphColor(kCaptionColor);
    SetButtons(true, true, true);
    SetResizable(true);
    //最小尺寸是整窗尺寸（含标题栏），见开发约定文档 UI 约定第 7 条
    SetMinSize(kMdEdMinWidth, kMdEdMinHeight);
    //标题栏程序图标（开发约定文档 UI 约定第 10 条）：标题栏透明，底色取根控件的底色；窗口没有所有者，会出现在任务栏
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, kMdWindowBackColor));
    m_programIcons.ApplyTaskIcons(m_hWnd);
    BuildClient();
    ResizeClient(kMdEdWindowWidth, kMdEdWindowHeight);
    //BuildClient 时已按建窗的默认尺寸排过一次版，分隔条的比例在那时就换算成了像素；
    //ResizeClient 触发的 WM_SIZE 已同步按设计尺寸排版，这里再按当前宽度左右各分一半
    ResetMarkdownEditorSplit(GetClientContent());
    CenterWindow(::GetDesktopWindow());
    //多个编辑器依次错开
    const int offset = (cascadeIndex % kCascadeWrap) * kCascadeOffsetPx;
    if (offset > 0)
    {
        RECT rc;
        GetWindowRect(&rc);
        SetWindowPos(NULL, rc.left + offset, rc.top + offset, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (target != NULL && target->m_id > 0)
        m_targets.push_back(*target);
    if (!sourceUtf8.empty())
        SetEditorText(MarkdownUtf8ToEditorText(sourceUtf8));
    SetDirty(false);
    RefreshSendArea();
    RefreshPreview();

    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    if (m_pEditor != NULL)
        SetDuiFocus(m_pEditor);
    LOG_INFO("markdown editor opened: bytes=%u, target=%lld, cascade=%d.", (unsigned)sourceUtf8.size(),
             (target != NULL) ? (long long)target->m_id : 0LL, cascadeIndex);
    return true;
}

bool CMarkdownEditorWindow::IsDirty() const
{
    return m_dirty;
}

std::wstring CMarkdownEditorWindow::DocumentName() const
{
    if (m_filePath.empty())
        return TR(L"未命名");
    const size_t slash = m_filePath.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? m_filePath : m_filePath.substr(slash + 1);
}

void CMarkdownEditorWindow::RefreshSendArea()
{
    const bool ready = IsSendReady();
    if (m_pTargets != NULL)
        m_pTargets->SetText(MarkdownSendTargetsText(ready, m_targets).c_str());
    if (m_pPick != NULL)
        m_pPick->SetEnabled(ready);
    if (m_pSend != NULL)
        m_pSend->SetEnabled(ready);
    if (m_pTargets != NULL)
        m_pTargets->Invalidate();
    if (m_pPick != NULL)
        m_pPick->Invalidate();
    if (m_pSend != NULL)
        m_pSend->Invalidate();
}

void CMarkdownEditorWindow::CloseWithoutPrompt()
{
    m_dirty = false;
    if (IsWindow())
        DestroyWindow();
}

void CMarkdownEditorWindow::OnMarkdownTargetsPicked(const std::vector<MarkdownSendTarget>& targets)
{
    m_pickerOpen = false;
    if (targets.empty())
        return;
    m_targets = targets;
    RefreshSendArea();
    ShowStatus(TR(L"已选择发送对象"));
    LOG_INFO("markdown editor: targets picked, count=%u.", (unsigned)targets.size());
}

void CMarkdownEditorWindow::OnFinalMessage(HWND /*hWnd*/)
{
    //管理器在这里 delete 本对象，之后不得再访问任何成员
    CMarkdownEditorManager::GetInstance().OnEditorDestroyed(this);
}

void CMarkdownEditorWindow::BuildClient()
{
    SetClientContent(BuildMarkdownEditorBody());
    DuiControl* root = GetClientContent();
    if (root == NULL)
        return;
    m_pEditor = dynamic_cast<DuiRichEdit*>(root->FindCtrlById(kMdEdIdEditor));
    m_pPreview = dynamic_cast<balloonchatwnd::ChatMarkdownPreview*>(root->FindCtrlById(kMdEdIdPreview));
    m_pPreviewScroll = dynamic_cast<DuiScrollView*>(root->FindCtrlById(kMdEdIdPreviewScroll));
    m_pSplitter = dynamic_cast<DuiSplitter*>(root->FindCtrlById(kMdEdIdSplitter));
    m_pPreviewToggle = dynamic_cast<DuiButton*>(root->FindCtrlById(kMdEdIdPreviewToggle));
    m_pTargets = dynamic_cast<DuiLabel*>(root->FindCtrlById(kMdEdIdTargets));
    m_pStatus = dynamic_cast<DuiLabel*>(root->FindCtrlById(kMdEdIdStatus));
    m_pPick = dynamic_cast<DuiButton*>(root->FindCtrlById(kMdEdIdPickTargets));
    m_pSend = dynamic_cast<DuiButton*>(root->FindCtrlById(kMdEdIdSend));
    m_pFindEdit = dynamic_cast<DuiEdit*>(root->FindCtrlById(kMdEdIdFindEdit));
    m_pReplaceEdit = dynamic_cast<DuiEdit*>(root->FindCtrlById(kMdEdIdReplaceEdit));
    m_pMatchCase = dynamic_cast<DuiButton*>(root->FindCtrlById(kMdEdIdFindMatchCase));
    m_pFindCount = dynamic_cast<DuiLabel*>(root->FindCtrlById(kMdEdIdFindCount));
    m_findVisible = false;

    //编辑框字体：界面字体，字号按窗口 DPI 换算
    if (m_pEditor != NULL)
    {
        if (m_editorFont != NULL)
        {
            ::DeleteObject(m_editorFont);
            m_editorFont = NULL;
        }
        const int fontPx = ::MulDiv(kEditorFontPx, GetDpi(), kBaseDpi);
        m_editorFont = ::CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, i18n::UiFontName());
        if (m_editorFont != NULL)
            m_pEditor->SetDefaultFontFromHFONT(m_editorFont);
    }
}

std::wstring CMarkdownEditorWindow::EditorText() const
{
    if (m_pEditor == NULL)
        return std::wstring();
    return std::wstring((LPCWSTR)m_pEditor->GetText());
}

std::string CMarkdownEditorWindow::SourceUtf8() const
{
    return EditorTextToMarkdownUtf8(EditorText());
}

void CMarkdownEditorWindow::SetEditorText(const std::wstring& text)
{
    if (m_pEditor == NULL)
        return;
    m_settingText = true;
    m_pEditor->SetText(text.c_str());
    m_settingText = false;
}

void CMarkdownEditorWindow::SetDirty(bool dirty)
{
    m_dirty = dirty;
    RefreshCaption();
}

void CMarkdownEditorWindow::RefreshCaption()
{
    if (IsWindow())
        SetTitle(MarkdownEditorCaption(m_filePath, m_dirty).c_str());
}

void CMarkdownEditorWindow::RefreshPreview()
{
    if (m_pPreview == NULL)
        return;
    int fontPx = 0;
    COLORREF color = 0;
    //预览与消息气泡、预览窗口使用同一套默认字号与颜色
    balloonchatwnd::MarkdownTextStyleFromFont(net::TextFont(), fontPx, color);
    m_pPreview->SetSource(SourceUtf8(), fontPx, color);
    if (m_pPreviewScroll != NULL)
    {
        m_pPreviewScroll->ForceLayout(m_pPreviewScroll->GetRect());
        m_pPreviewScroll->Invalidate();
    }
}

void CMarkdownEditorWindow::SetPreviewVisible(bool visible)
{
    if (m_pSplitter == NULL)
        return;
    if (visible == m_previewVisible)
        return;
    if (!visible)
    {
        //记下编辑区当前宽度，展开时恢复；收起时预览区的最小宽度改为 0，分隔条被限制到最右边
        DuiControl* left = m_pSplitter->GetPane(0);
        if (left != NULL)
            m_savedSplitPx = left->GetRect().right - left->GetRect().left;
        m_pSplitter->SetMinSizes(kMdEdPaneMinPx, 0);
        m_pSplitter->SetSplitPx(kCollapsedSplitPx);
    }
    else
    {
        m_pSplitter->SetMinSizes(kMdEdPaneMinPx, kMdEdPaneMinPx);
        if (m_savedSplitPx > 0)
            m_pSplitter->SetSplitPx(m_savedSplitPx);
        else
            m_pSplitter->SetSplitFraction(kMdEdInitialSplitFraction);
    }
    m_previewVisible = visible;
    m_pSplitter->ForceLayout(m_pSplitter->GetRect());
    m_pSplitter->Invalidate();
    if (m_pPreviewToggle != NULL)
    {
        //展开时按钮带描边表示「开」，收起时与左侧按钮相同
        m_pPreviewToggle->SetVariant(visible ? DuiButton::Variant::Outlined : DuiButton::Variant::Ghost);
        m_pPreviewToggle->Invalidate();
    }
    if (visible)
        RefreshPreview();
    LOG_INFO("markdown editor: preview %s.", visible ? "shown" : "hidden");
}

void CMarkdownEditorWindow::ApplyFormat(int command)
{
    if (m_pEditor == NULL || command < 0 || command >= balloonchatwnd::kMarkdownEditCommandCount)
        return;
    long selStart = 0;
    long selEnd = 0;
    m_pEditor->GetSel(selStart, selEnd);
    balloonchatwnd::MarkdownEditInput in;
    in.m_text = EditorText();
    in.m_selStart = (int)selStart;
    in.m_selEnd = (int)selEnd;
    //RichEdit 里段落之间是单个 \r
    in.m_lineBreak = L'\r';
    in.m_placeholder = TR(kPlaceholders[command]);
    in.m_tableHeader1 = TR(L"列1");
    in.m_tableHeader2 = TR(L"列2");
    ApplyEditResult(balloonchatwnd::ApplyMarkdownEditCommand(in, command));
    SetDuiFocus(m_pEditor);
}

void CMarkdownEditorWindow::ApplyEditResult(const balloonchatwnd::MarkdownEditResult& r)
{
    if (m_pEditor == NULL)
        return;
    if (r.m_replaceStart != r.m_replaceEnd || !r.m_replacement.empty())
    {
        //一次可撤销的替换：Ctrl+Z 一步撤回
        m_pEditor->SetSel(r.m_replaceStart, r.m_replaceEnd);
        m_pEditor->ReplaceSel(r.m_replacement.c_str(), true);
    }
    m_pEditor->SetSel(r.m_selStart, r.m_selEnd);
}

void CMarkdownEditorWindow::ClearStyle()
{
    if (m_pEditor == NULL)
        return;
    long selStart = 0;
    long selEnd = 0;
    m_pEditor->GetSel(selStart, selEnd);
    balloonchatwnd::MarkdownEditInput in;
    in.m_text = EditorText();
    in.m_selStart = (int)selStart;
    in.m_selEnd = (int)selEnd;
    //RichEdit 里段落之间是单个 \r
    in.m_lineBreak = L'\r';
    ApplyEditResult(balloonchatwnd::ClearMarkdownInlineStyle(in));
    SetDuiFocus(m_pEditor);
}

void CMarkdownEditorWindow::ApplyShortcut(int shortcut)
{
    //Ctrl+1 ~ Ctrl+6：标题级别
    if (shortcut >= balloonchatwnd::md_shortcut_heading1 && shortcut <= balloonchatwnd::md_shortcut_heading6)
    {
        SetHeadingLevel(shortcut - balloonchatwnd::md_shortcut_heading1 + 1);
        return;
    }
    switch (shortcut)
    {
    //Ctrl+B：加粗
    case balloonchatwnd::md_shortcut_bold:
        ApplyFormat(balloonchatwnd::md_edit_bold);
        break;
    //Ctrl+I：斜体
    case balloonchatwnd::md_shortcut_italic:
        ApplyFormat(balloonchatwnd::md_edit_italic);
        break;
    //Ctrl+U：下划线
    case balloonchatwnd::md_shortcut_underline:
        ApplyFormat(balloonchatwnd::md_edit_underline);
        break;
    //Ctrl+Shift+`：行内代码
    case balloonchatwnd::md_shortcut_code:
        ApplyFormat(balloonchatwnd::md_edit_code);
        break;
    //Alt+Shift+5：删除线
    case balloonchatwnd::md_shortcut_strike:
        ApplyFormat(balloonchatwnd::md_edit_strike);
        break;
    //Ctrl+K：超链接
    case balloonchatwnd::md_shortcut_link:
        ApplyFormat(balloonchatwnd::md_edit_link);
        break;
    //Ctrl+\：清除样式
    case balloonchatwnd::md_shortcut_clear_style:
        ClearStyle();
        break;
    //不是格式快捷键
    default:
        return;
    }
    LOG_INFO("markdown editor: shortcut %d applied.", shortcut);
}

void CMarkdownEditorWindow::DropPendingSysChar()
{
    MSG msg;
    ::PeekMessageW(&msg, m_hWnd, WM_SYSCHAR, WM_SYSCHAR, PM_REMOVE);
}

bool CMarkdownEditorWindow::ContinueListOnEnter()
{
    if (m_pEditor == NULL)
        return false;
    long selStart = 0;
    long selEnd = 0;
    m_pEditor->GetSel(selStart, selEnd);
    balloonchatwnd::MarkdownEditInput in;
    in.m_text = EditorText();
    in.m_selStart = (int)selStart;
    in.m_selEnd = (int)selEnd;
    //RichEdit 里段落之间是单个 \r
    in.m_lineBreak = L'\r';
    balloonchatwnd::MarkdownEditResult r;
    if (!balloonchatwnd::ContinueMarkdownListOnEnter(in, r))
        return false;
    //一次可撤销的替换：Ctrl+Z 一步撤回换行与标记
    ApplyEditResult(r);
    return true;
}

void CMarkdownEditorWindow::InsertPlainLineBreak()
{
    if (m_pEditor != NULL)
        m_pEditor->ReplaceSel(L"\r", true);
}

void CMarkdownEditorWindow::SetHeadingLevel(int level)
{
    if (m_pEditor == NULL)
        return;
    long selStart = 0;
    long selEnd = 0;
    m_pEditor->GetSel(selStart, selEnd);
    balloonchatwnd::MarkdownEditInput in;
    in.m_text = EditorText();
    in.m_selStart = (int)selStart;
    in.m_selEnd = (int)selEnd;
    //RichEdit 里段落之间是单个 \r
    in.m_lineBreak = L'\r';
    ApplyEditResult(balloonchatwnd::SetMarkdownHeadingLevel(in, level));
    SetDuiFocus(m_pEditor);
}

void CMarkdownEditorWindow::DoHeadingMenu()
{
    DuiControl* root = GetClientContent();
    DuiControl* button = (root != NULL)
                             ? root->FindCtrlById(kMdEdIdFormatFirst + (UINT)balloonchatwnd::md_edit_heading)
                             : NULL;
    if (button == NULL || m_pEditor == NULL)
        return;
    //当前行（有选区时为选区第一行）的级别打勾
    long selStart = 0;
    long selEnd = 0;
    m_pEditor->GetSel(selStart, selEnd);
    const int current = balloonchatwnd::MarkdownHeadingLevelAt(EditorText(), (int)selStart, (int)selEnd);
    DuiMenu menu;
    menu.AppendChecked(kHeadingMenuIdBase, TR(L"正文"), current == 0);
    for (int level = 1; level <= kHeadingLevelCount; ++level)
    {
        //「文字\t快捷键」：快捷键靠右显示在单独一列（DuiMenu）
        const std::wstring text = std::wstring(TR(kHeadingMenuTexts[level - 1])) + L"\tCtrl+" + std::to_wstring(level);
        menu.AppendChecked(kHeadingMenuIdBase + (UINT)level, text.c_str(), current == level);
    }
    //菜单弹在按钮正下方
    const RECT rc = button->GetRect();
    POINT anchor = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &anchor);
    const UINT chosen = menu.TrackPopup(anchor.x, anchor.y, m_hWnd);
    if (chosen < kHeadingMenuIdBase || chosen > kHeadingMenuIdBase + (UINT)kHeadingLevelCount)
        return;
    SetHeadingLevel((int)(chosen - kHeadingMenuIdBase));
    LOG_INFO("markdown editor: heading level %d chosen from menu.", (int)(chosen - kHeadingMenuIdBase));
}

void CMarkdownEditorWindow::InsertAlert(int type)
{
    if (m_pEditor == NULL)
        return;
    long selStart = 0;
    long selEnd = 0;
    m_pEditor->GetSel(selStart, selEnd);
    balloonchatwnd::MarkdownEditInput in;
    in.m_text = EditorText();
    in.m_selStart = (int)selStart;
    in.m_selEnd = (int)selEnd;
    //RichEdit 里段落之间是单个 \r
    in.m_lineBreak = L'\r';
    in.m_placeholder = TR(kPlaceholders[balloonchatwnd::md_edit_alert]);
    ApplyEditResult(balloonchatwnd::InsertMarkdownAlert(in, type));
    SetDuiFocus(m_pEditor);
}

void CMarkdownEditorWindow::DoAlertMenu()
{
    DuiControl* root = GetClientContent();
    DuiControl* button = (root != NULL)
                             ? root->FindCtrlById(kMdEdIdFormatFirst + (UINT)balloonchatwnd::md_edit_alert)
                             : NULL;
    if (button == NULL || m_pEditor == NULL)
        return;
    DuiMenu menu;
    for (int type = balloonchatwnd::md_edit_alert_note; type <= balloonchatwnd::md_edit_alert_caution; ++type)
    {
        //「名称\t类型名」：源文本里的写法（NOTE 等）靠右显示在单独一列
        const std::wstring text = std::wstring(AlertMenuTitle(type)) + L"\t" + balloonchatwnd::MarkdownAlertKeyword(type);
        menu.AppendItem(kAlertMenuIdBase + (UINT)type, text.c_str());
    }
    //菜单弹在按钮正下方
    const RECT rc = button->GetRect();
    POINT anchor = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &anchor);
    const UINT chosen = menu.TrackPopup(anchor.x, anchor.y, m_hWnd);
    if (chosen < kAlertMenuIdBase + (UINT)balloonchatwnd::md_edit_alert_note
        || chosen > kAlertMenuIdBase + (UINT)balloonchatwnd::md_edit_alert_caution)
        return;
    InsertAlert((int)(chosen - kAlertMenuIdBase));
    LOG_INFO("markdown editor: alert type %d chosen from menu.", (int)(chosen - kAlertMenuIdBase));
}

void CMarkdownEditorWindow::OpenFindBar(bool withReplace)
{
    DuiControl* root = GetClientContent();
    if (root == NULL || m_pFindEdit == NULL || m_pEditor == NULL)
        return;
    //编辑框里选中的是单行文字时，作为查找词填进查找框
    long selStart = 0;
    long selEnd = 0;
    m_pEditor->GetSel(selStart, selEnd);
    //GetText 去掉了末尾的空行，选区可能落在取到的文字之外，此时不预填
    const std::wstring text = EditorText();
    if (selEnd > selStart && selEnd - selStart <= kFindPrefillMaxChars && selEnd <= (long)text.size())
    {
        const std::wstring selected = text.substr((size_t)selStart, (size_t)(selEnd - selStart));
        if (selected.find_first_of(L"\r\n") == std::wstring::npos)
            m_pFindEdit->SetText(selected.c_str());
    }
    ShowMarkdownFindBar(root, true, withReplace);
    m_findVisible = true;
    SetDuiFocus(m_pFindEdit);
    m_pFindEdit->SelectAll();
    UpdateFindCount();
    LOG_INFO("markdown editor: find bar opened (replace %s).", withReplace ? "shown" : "hidden");
}

void CMarkdownEditorWindow::CloseFindBar()
{
    DuiControl* root = GetClientContent();
    if (root == NULL || !m_findVisible)
        return;
    ShowMarkdownFindBar(root, false, false);
    m_findVisible = false;
    if (m_pEditor != NULL)
        SetDuiFocus(m_pEditor);
    LOG_INFO("markdown editor: find bar closed.");
}

void CMarkdownEditorWindow::FindNext(bool forward)
{
    if (m_pEditor == NULL)
        return;
    const std::wstring query = FindQuery();
    if (query.empty())
    {
        //还没有查找词：打开查找条（已打开时保持替换行的显示状态）让用户输入
        const DuiControl* replaceRow = (GetClientContent() != NULL)
                                           ? GetClientContent()->FindCtrlById(kMdEdIdReplaceRow)
                                           : NULL;
        OpenFindBar(m_findVisible && replaceRow != NULL && replaceRow->IsVisible());
        return;
    }
    long selStart = 0;
    long selEnd = 0;
    m_pEditor->GetSel(selStart, selEnd);
    int start = 0;
    int end = 0;
    bool wrapped = false;
    if (!FindMarkdownText(EditorText(), query, forward ? (int)selEnd : (int)selStart, forward, FindMatchCase(), start,
                          end, &wrapped))
    {
        if (m_pFindCount != NULL)
            m_pFindCount->SetText(TR(L"无结果"));
        LOG_INFO("markdown editor: find %s, no match (query %u chars).", forward ? "next" : "previous",
                 (unsigned)query.size());
        return;
    }
    //先把起点滚到可见再选中，长匹配的终点随后也滚到可见
    m_pEditor->SetSel(start, start);
    m_pEditor->SendMessageToEngine(EM_SCROLLCARET, 0, 0);
    m_pEditor->SetSel(start, end);
    m_pEditor->SendMessageToEngine(EM_SCROLLCARET, 0, 0);
    if (wrapped)
        ShowStatus(forward ? TR(L"已从开头继续查找") : TR(L"已从末尾继续查找"));
}

void CMarkdownEditorWindow::ReplaceCurrent()
{
    if (m_pEditor == NULL || m_pReplaceEdit == NULL)
        return;
    const std::wstring query = FindQuery();
    if (query.empty())
    {
        if (m_pFindEdit != NULL)
            SetDuiFocus(m_pFindEdit);
        return;
    }
    long selStart = 0;
    long selEnd = 0;
    m_pEditor->GetSel(selStart, selEnd);
    balloonchatwnd::MarkdownEditResult r;
    bool foundNext = false;
    const bool replaced = ReplaceCurrentMarkdownText(EditorText(), (int)selStart, (int)selEnd, query,
                                                     std::wstring((LPCWSTR)m_pReplaceEdit->GetText()), FindMatchCase(),
                                                     r, &foundNext);
    ApplyEditResult(r);
    m_pEditor->SendMessageToEngine(EM_SCROLLCARET, 0, 0);
    UpdateFindCount();
    LOG_INFO("markdown editor: replace current, replaced %d, next match %d.", replaced ? 1 : 0, foundNext ? 1 : 0);
}

void CMarkdownEditorWindow::ReplaceAll()
{
    if (m_pEditor == NULL || m_pReplaceEdit == NULL)
        return;
    const std::wstring query = FindQuery();
    if (query.empty())
    {
        if (m_pFindEdit != NULL)
            SetDuiFocus(m_pFindEdit);
        return;
    }
    balloonchatwnd::MarkdownEditResult r;
    const int count = ReplaceAllMarkdownText(EditorText(), query, std::wstring((LPCWSTR)m_pReplaceEdit->GetText()),
                                             FindMatchCase(), r);
    if (count > 0)
    {
        ApplyEditResult(r);
        m_pEditor->SendMessageToEngine(EM_SCROLLCARET, 0, 0);
    }
    UpdateFindCount();
    ShowStatus((count > 0) ? TRF(L"已替换 {0} 处", count) : std::wstring(TR(L"无结果")));
    LOG_INFO("markdown editor: replace all, %d replaced.", count);
}

void CMarkdownEditorWindow::UpdateFindCount()
{
    if (m_pFindCount == NULL)
        return;
    const std::wstring query = FindQuery();
    std::wstring text;
    if (!query.empty())
    {
        const int count = CountMarkdownMatches(EditorText(), query, FindMatchCase());
        text = (count == 0) ? std::wstring(TR(L"无结果")) : TRF(L"共 {0} 处", count);
    }
    m_pFindCount->SetText(text.c_str());
    m_pFindCount->Invalidate();
}

std::wstring CMarkdownEditorWindow::FindQuery() const
{
    if (m_pFindEdit == NULL)
        return std::wstring();
    return std::wstring((LPCWSTR)m_pFindEdit->GetText());
}

bool CMarkdownEditorWindow::FindMatchCase() const
{
    return m_pMatchCase != NULL && m_pMatchCase->IsChecked();
}

bool CMarkdownEditorWindow::FindBarHasFocus() const
{
    //与 EditorHasKeyboardFocus 相同，以宿主的 DUI 焦点控件为准
    const DuiControl* focus = GetDuiFocus();
    return focus != NULL && (focus == m_pFindEdit || focus == m_pReplaceEdit);
}

bool CMarkdownEditorWindow::EditorHasKeyboardFocus() const
{
    return m_pEditor != NULL && GetDuiFocus() == m_pEditor;
}

void CMarkdownEditorWindow::DropPendingControlChar()
{
    //TranslateMessage 在派发按键消息之前已把对应的字符消息排进队列：紧挨着的是控制字符时取走
    MSG msg;
    if (::PeekMessageW(&msg, m_hWnd, WM_CHAR, WM_CHAR, PM_NOREMOVE) && msg.wParam < kFirstPrintableChar)
        ::PeekMessageW(&msg, m_hWnd, WM_CHAR, WM_CHAR, PM_REMOVE);
}

void CMarkdownEditorWindow::ShowStatus(const std::wstring& text)
{
    if (m_pStatus == NULL)
        return;
    m_pStatus->SetText(text.c_str());
    m_pStatus->Invalidate();
    KillTimer(kTimerStatus);
    if (!text.empty())
        SetTimer(kTimerStatus, kStatusShowMs, NULL);
}

void CMarkdownEditorWindow::DoOpen()
{
    if (!ConfirmDiscardChanges())
        return;
    std::vector<wchar_t> buffer(kPathBufferChars, L'\0');
    //过滤串中间夹着 \0，说明文字各自翻译后再拼（UI 约定第 8 条）；返回值须活到对话框关闭
    const std::wstring filter = filedlg::MakeFilter(TR(L"Markdown 文件"), L"*.md;*.markdown;*.txt",
                                                    TR(L"所有文件"), L"*.*");
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = &buffer[0];
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!::GetOpenFileNameW(&ofn))
        return;
    const std::wstring path(&buffer[0]);
    std::string bytes;
    bool tooLarge = false;
    if (!ReadWholeFile(path, kMarkdownOpenMaxBytes, bytes, &tooLarge))
    {
        const std::wstring tip = tooLarge ? std::wstring(TR(L"文件太大，无法打开。"))
                                          : std::wstring(TR(L"无法读取这个文件。"));
        ::MessageBoxW(m_hWnd, tip.c_str(), TR(L"Markdown 编辑器"), MB_OK | MB_ICONWARNING);
        return;
    }
    int encoding = md_file_utf8;
    SetEditorText(DecodeMarkdownFileBytes(bytes, &encoding));
    m_filePath = path;
    SetDirty(false);
    RefreshPreview();
    ShowStatus(TR(L"已打开"));
    LOG_INFO("markdown editor: opened file, bytes=%u, encoding=%d.", (unsigned)bytes.size(), encoding);
}

bool CMarkdownEditorWindow::DoSave(bool saveAs)
{
    std::wstring path = m_filePath;
    if (saveAs || path.empty())
    {
        std::vector<wchar_t> buffer(kPathBufferChars, L'\0');
        std::wstring name = MarkdownTitleFromSource(SourceUtf8());
        if (name.empty())
            name = TR(L"未命名");
        //文件名里不能出现的字符换成下划线
        const std::wstring bad = L"\\/:*?\"<>|";
        for (size_t i = 0; i < name.size(); ++i)
        {
            if (bad.find(name[i]) != std::wstring::npos)
                name[i] = L'_';
        }
        name += L".md";
        wcsncpy_s(&buffer[0], buffer.size(), name.c_str(), _TRUNCATE);
        const std::wstring filter = filedlg::MakeFilter(TR(L"Markdown 文件"), L"*.md", TR(L"所有文件"), L"*.*");
        OPENFILENAMEW ofn;
        memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hWnd;
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile = &buffer[0];
        ofn.nMaxFile = static_cast<DWORD>(buffer.size());
        ofn.lpstrDefExt = L"md";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
        if (!::GetSaveFileNameW(&ofn))
            return false;
        path.assign(&buffer[0]);
    }
    if (!WriteWholeFile(path, EncodeMarkdownForSave(EditorText())))
    {
        ::MessageBoxW(m_hWnd, TR(L"保存失败，请检查文件是否被占用或目录是否可写。"), TR(L"Markdown 编辑器"),
                      MB_OK | MB_ICONWARNING);
        return false;
    }
    m_filePath = path;
    SetDirty(false);
    ShowStatus(TR(L"已保存"));
    LOG_INFO("markdown editor: saved, chars=%u.", (unsigned)EditorText().size());
    return true;
}

void CMarkdownEditorWindow::DoExportMenu()
{
    DuiControl* root = GetClientContent();
    DuiControl* button = (root != NULL) ? root->FindCtrlById(kMdEdIdExport) : NULL;
    if (button == NULL)
        return;
    DuiMenu menu;
    for (int format = md_export_md; format <= md_export_count; ++format)
    {
        if (IsMarkdownExportAvailable(format))
            menu.AppendItem(static_cast<UINT>(format), MarkdownExportMenuText(format).c_str());
    }
    //菜单弹在按钮正下方
    const RECT rc = button->GetRect();
    POINT anchor = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &anchor);
    const UINT chosen = menu.TrackPopup(anchor.x, anchor.y, m_hWnd);
    if (chosen == 0)
        return;
    std::wstring name = MarkdownTitleFromSource(SourceUtf8());
    std::wstring error;
    if (ExportMarkdownWithDialog(m_hWnd, SourceUtf8(), name, static_cast<int>(chosen), &error))
        ShowStatus(TR(L"已导出"));
    else if (!error.empty())
        ::MessageBoxW(m_hWnd, error.c_str(), TR(L"Markdown 编辑器"), MB_OK | MB_ICONWARNING);
}

void CMarkdownEditorWindow::DoPickTargets()
{
    IMarkdownEditorHost* host = CMarkdownEditorManager::GetInstance().GetHost();
    if (host == NULL || !host->IsMarkdownSendReady())
    {
        RefreshSendArea();
        return;
    }
    const std::string source = SourceUtf8();
    //标题与摘要按卡片的规则取，选择窗里看到的与对方收到的卡片一致；摘要不含标题，否则标题会出现两次
    std::wstring title;
    std::wstring digest;
    if (!balloonchatwnd::MarkdownCardTextFromSource(source, title, digest))
    {
        //旧版 DLL 没有卡片接口：标题从源文本粗取，摘要用渲染后的纯文本
        title = MarkdownTitleFromSource(source);
        digest = FromUtf8(markdown::MarkdownToPlainTextUtf8(source, balloonchatwnd::MarkdownImageLinkPrefix()));
    }
    if (title.empty())
        title = TR(L"未命名");
    if (digest.size() > kPickerDigestMaxChars)
        digest = digest.substr(0, kPickerDigestMaxChars) + L"\x2026";
    m_pickerOpen = host->OpenMarkdownTargetPicker(title, digest, this);
    LOG_INFO("markdown editor: target picker %s.", m_pickerOpen ? "opened" : "not opened");
}

void CMarkdownEditorWindow::DoSend()
{
    IMarkdownEditorHost* host = CMarkdownEditorManager::GetInstance().GetHost();
    const bool ready = (host != NULL && host->IsMarkdownSendReady());
    const std::string source = SourceUtf8();
    const int check = CheckMarkdownSend(ready, source, m_targets.size());
    if (check == md_send_no_target)
    {
        //还没有选择对象：先打开选择窗，选好后再点「发送」
        DoPickTargets();
        ShowStatus(TR(L"选好发送对象后再点「发送」"));
        return;
    }
    if (check != md_send_ok)
    {
        ::MessageBoxW(m_hWnd, MarkdownSendCheckText(check).c_str(), TR(L"Markdown 编辑器"), MB_OK | MB_ICONWARNING);
        RefreshSendArea();
        return;
    }
    const int sent = host->SendMarkdownDocument(source, m_targets);
    if (sent > 0)
        ShowStatus(TRF(L"已发送给 {0} 个对象", sent));
    else
        ::MessageBoxW(m_hWnd, TR(L"发送失败，请检查网络连接后重试。"), TR(L"Markdown 编辑器"), MB_OK | MB_ICONWARNING);
    LOG_INFO("markdown editor: send, targets=%u, sent=%d, bytes=%u.", (unsigned)m_targets.size(), sent,
             (unsigned)source.size());
}

bool CMarkdownEditorWindow::ConfirmDiscardChanges()
{
    if (!m_dirty)
        return true;
    const std::wstring tip = TRF(L"文档「{0}」有未保存的修改，是否保存？", DocumentName().c_str());
    const int choice = ::MessageBoxW(m_hWnd, tip.c_str(), TR(L"Markdown 编辑器"), MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDYES)
        return DoSave(false);
    return choice == IDNO;
}

LRESULT CMarkdownEditorWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (!ConfirmDiscardChanges())
        return 0;
    DestroyWindow();
    return 0;
}

LRESULT CMarkdownEditorWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //编辑框文字变化：标记修改，停止输入一会儿后刷新预览
    if (pNotify->code == DUIN_VALUECHANGED && pNotify->ctrlId == kMdEdIdEditor)
    {
        if (!m_settingText)
            SetDirty(true);
        KillTimer(kTimerPreview);
        SetTimer(kTimerPreview, kPreviewDelayMs, NULL);
        //查找条开着时，正文变化后匹配个数随之刷新
        if (m_findVisible)
            UpdateFindCount();
        return 0;
    }
    //查找框文字或「区分大小写」变化：刷新匹配个数
    if (pNotify->code == DUIN_VALUECHANGED
        && (pNotify->ctrlId == kMdEdIdFindEdit || pNotify->ctrlId == kMdEdIdFindMatchCase))
    {
        UpdateFindCount();
        return 0;
    }
    //查找框里按回车：查找下一个（Shift+回车查找上一个）
    if (pNotify->code == (UINT)DuiEdit::DUIN_EDIT_ENTER && pNotify->ctrlId == kMdEdIdFindEdit)
    {
        FindNext((::GetKeyState(VK_SHIFT) & kKeyDownMask) == 0);
        return 0;
    }
    //替换框里按回车：替换当前并找下一个
    if (pNotify->code == (UINT)DuiEdit::DUIN_EDIT_ENTER && pNotify->ctrlId == kMdEdIdReplaceEdit)
    {
        ReplaceCurrent();
        return 0;
    }
    //查找框或替换框里按 Esc：收起查找条
    if (pNotify->code == (UINT)DuiEdit::DUIN_EDIT_ESCAPE
        && (pNotify->ctrlId == kMdEdIdFindEdit || pNotify->ctrlId == kMdEdIdReplaceEdit))
    {
        PostMessage(kMsgMarkdownEditorDeferred, md_editor_action_close_find);
        return 0;
    }
    if (pNotify->code == DUIN_CLICK)
    {
        const UINT id = pNotify->ctrlId;
        //格式按钮：「H」与「警告框」弹出菜单（等这次点击通知处理完再弹），其余直接执行
        if (id >= kMdEdIdFormatFirst && id < kMdEdIdFormatFirst + (UINT)balloonchatwnd::kMarkdownEditCommandCount)
        {
            const int command = static_cast<int>(id - kMdEdIdFormatFirst);
            if (command == balloonchatwnd::md_edit_heading)
                PostMessage(kMsgMarkdownEditorDeferred, md_editor_action_heading_menu);
            else if (command == balloonchatwnd::md_edit_alert)
                PostMessage(kMsgMarkdownEditorDeferred, md_editor_action_alert_menu);
            else
                ApplyFormat(command);
            return 0;
        }
        //「预览」开关
        if (id == kMdEdIdPreviewToggle)
        {
            SetPreviewVisible(!m_previewVisible);
            return 0;
        }
        //查找条：上一个、下一个、替换、全部替换
        if (id == kMdEdIdFindPrev || id == kMdEdIdFindNext)
        {
            FindNext(id == kMdEdIdFindNext);
            return 0;
        }
        if (id == kMdEdIdReplaceOne)
        {
            ReplaceCurrent();
            return 0;
        }
        if (id == kMdEdIdReplaceAll)
        {
            ReplaceAll();
            return 0;
        }
        //查找条的关闭按钮：等这次点击通知处理完再隐藏它所在的行
        if (id == kMdEdIdFindClose)
        {
            PostMessage(kMsgMarkdownEditorDeferred, md_editor_action_close_find);
            return 0;
        }
        //会弹出模态框或菜单的按钮：等这次点击通知处理完再执行
        WPARAM action = 0;
        if (id == kMdEdIdOpen)
            action = md_editor_action_open;
        else if (id == kMdEdIdSave)
            action = md_editor_action_save;
        else if (id == kMdEdIdExport)
            action = md_editor_action_export_menu;
        else if (id == kMdEdIdPickTargets)
            action = md_editor_action_pick;
        else if (id == kMdEdIdSend)
            action = md_editor_action_send;
        if (action != 0)
        {
            PostMessage(kMsgMarkdownEditorDeferred, action);
            return 0;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CMarkdownEditorWindow::OnKeyDownMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    const bool ctrl = (::GetKeyState(VK_CONTROL) & kKeyDownMask) != 0;
    const bool shift = (::GetKeyState(VK_SHIFT) & kKeyDownMask) != 0;
    const bool alt = (::GetKeyState(VK_MENU) & kKeyDownMask) != 0;
    //查找：Ctrl+F、Ctrl+H 打开查找条（随后的控制字符丢掉，Ctrl+H 产生的是退格符），F3 / Shift+F3 查找下一个 / 上一个
    switch (MarkdownFindKeyFor(static_cast<unsigned>(wParam), ctrl, shift, alt))
    {
    //Ctrl+F：只显示查找行
    case md_find_key_open_find:
        OpenFindBar(false);
        DropPendingControlChar();
        return 0;
    //Ctrl+H：显示查找行与替换行
    case md_find_key_open_replace:
        OpenFindBar(true);
        DropPendingControlChar();
        return 0;
    //F3：查找下一个
    case md_find_key_next:
        FindNext(true);
        return 0;
    //Shift+F3：查找上一个
    case md_find_key_prev:
        FindNext(false);
        return 0;
    //不是查找按键
    default:
        break;
    }
    //Esc：查找条开着时收起它（焦点在查找框里时由查找框发 DUIN_EDIT_ESCAPE，这里处理焦点在编辑框等其它控件的情况）
    if (wParam == VK_ESCAPE && !ctrl && !shift && !alt && m_findVisible && !FindBarHasFocus())
    {
        CloseFindBar();
        return 0;
    }
    //格式快捷键（标题、加粗、斜体、下划线、行内代码、链接、清除样式，组合见 MarkdownShortcutFor）。它们随后产生的
    //控制字符（Ctrl+I 是制表符、Ctrl+K 是 0x0B 等）丢掉，否则会插进编辑框。焦点在查找框或替换框里时不作用于编辑框
    const int shortcut = balloonchatwnd::MarkdownShortcutFor(static_cast<unsigned>(wParam), ctrl, shift, alt);
    if (shortcut != balloonchatwnd::md_shortcut_none && !FindBarHasFocus())
    {
        ApplyShortcut(shortcut);
        DropPendingControlChar();
        return 0;
    }
    if (ctrl && !shift && !alt)
    {
        switch (wParam)
        {
        //Ctrl+S：保存
        case 'S':
            m_swallowCtrlChar = true;
            PostMessage(kMsgMarkdownEditorDeferred, md_editor_action_save);
            return 0;
        //Ctrl+O：打开
        case 'O':
            m_swallowCtrlChar = true;
            PostMessage(kMsgMarkdownEditorDeferred, md_editor_action_open);
            return 0;
        //其余组合键交给 DuiHost
        default:
            break;
        }
    }
    //Tab：编辑框有焦点时不让 DuiHost 拿去切换焦点，随后的字符消息由编辑框插入制表符
    if (wParam == VK_TAB && !ctrl && EditorHasKeyboardFocus())
        return 0;
    //回车：Shift+回车只换行；不带修饰键时先尝试列表续项。处理了的话随后的字符消息（\r）要丢掉。
    //输入法组字期间的回车用来确认候选词，交给编辑框照常处理
    if (wParam == VK_RETURN && !ctrl && !alt && !m_imeComposing && EditorHasKeyboardFocus())
    {
        if (shift)
        {
            InsertPlainLineBreak();
            m_swallowCtrlChar = true;
            return 0;
        }
        if (ContinueListOnEnter())
        {
            m_swallowCtrlChar = true;
            return 0;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CMarkdownEditorWindow::OnCharMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //快捷键产生的控制字符丢掉（Ctrl+I 是制表符、Ctrl+B 是 0x02 等），已由列表续项或 Shift+回车处理的回车（\r）同样丢掉
    const WPARAM kFirstPrintable = 0x20;
    if (m_swallowCtrlChar && wParam < kFirstPrintable)
    {
        m_swallowCtrlChar = false;
        return 0;
    }
    m_swallowCtrlChar = false;
    bHandled = FALSE;
    return 0;
}

LRESULT CMarkdownEditorWindow::OnSysKeyDownMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //带 Alt 的格式快捷键（Alt+Shift+5 删除线）；随后的 WM_SYSCHAR 丢掉，免得系统按菜单助记符处理并发出提示音。
    //焦点在查找框或替换框里时不作用于编辑框
    const bool ctrl = (::GetKeyState(VK_CONTROL) & kKeyDownMask) != 0;
    const bool shift = (::GetKeyState(VK_SHIFT) & kKeyDownMask) != 0;
    const bool alt = (::GetKeyState(VK_MENU) & kKeyDownMask) != 0;
    const int shortcut = balloonchatwnd::MarkdownShortcutFor(static_cast<unsigned>(wParam), ctrl, shift, alt);
    if (shortcut != balloonchatwnd::md_shortcut_none && !FindBarHasFocus())
    {
        ApplyShortcut(shortcut);
        DropPendingSysChar();
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CMarkdownEditorWindow::OnImeCompositionMsg(UINT uMsg, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    //只记录组字状态，消息照常交给 DuiHost（它再转给编辑框定位候选窗）
    m_imeComposing = (uMsg == WM_IME_STARTCOMPOSITION);
    bHandled = FALSE;
    return 0;
}

LRESULT CMarkdownEditorWindow::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //停止输入后刷新预览
    if (wParam == kTimerPreview)
    {
        KillTimer(kTimerPreview);
        if (m_previewVisible)
            RefreshPreview();
        return 0;
    }
    //状态文字到时清除
    if (wParam == kTimerStatus)
    {
        KillTimer(kTimerStatus);
        if (m_pStatus != NULL)
        {
            m_pStatus->SetText(L"");
            m_pStatus->Invalidate();
        }
        return 0;
    }
    //balloonui 宿主窗口自己的定时器：交给框架
    bHandled = FALSE;
    return 0;
}

LRESULT CMarkdownEditorWindow::OnDeferred(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    switch (wParam)
    {
    //打开本地文件
    case md_editor_action_open:
        DoOpen();
        break;
    //保存
    case md_editor_action_save:
        DoSave(false);
        break;
    //导出菜单
    case md_editor_action_export_menu:
        DoExportMenu();
        break;
    //选择发送对象
    case md_editor_action_pick:
        DoPickTargets();
        break;
    //发送
    case md_editor_action_send:
        DoSend();
        break;
    //格式条「H」：标题级别菜单
    case md_editor_action_heading_menu:
        DoHeadingMenu();
        break;
    //格式条「警告框」：类型菜单
    case md_editor_action_alert_menu:
        DoAlertMenu();
        break;
    //查找条的关闭按钮或 Esc：收起查找条
    case md_editor_action_close_find:
        CloseFindBar();
        break;
    //不认识的取值：忽略
    default:
        break;
    }
    return 0;
}

LRESULT CMarkdownEditorWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    KillTimer(kTimerPreview);
    KillTimer(kTimerStatus);
    //选择窗还在为本窗口打开着：通知宿主不要再回调本窗口
    if (m_pickerOpen)
    {
        IMarkdownEditorHost* host = CMarkdownEditorManager::GetInstance().GetHost();
        if (host != NULL)
            host->CancelMarkdownTargetPicker(this);
        m_pickerOpen = false;
    }
    //控件树随窗口销毁，借用的指针一并作废；编辑框字体在析构函数里释放
    m_pEditor = NULL;
    m_pPreview = NULL;
    m_pPreviewScroll = NULL;
    m_pSplitter = NULL;
    m_pPreviewToggle = NULL;
    m_pTargets = NULL;
    m_pStatus = NULL;
    m_pPick = NULL;
    m_pSend = NULL;
    m_pFindEdit = NULL;
    m_pReplaceEdit = NULL;
    m_pMatchCase = NULL;
    m_pFindCount = NULL;
    LOG_INFO("markdown editor: window destroyed.");
    bHandled = FALSE;
    return 0;
}

} // namespace balloonmd
