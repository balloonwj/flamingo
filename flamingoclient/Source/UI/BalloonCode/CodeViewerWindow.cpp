/**
 *  代码查看窗，见 CodeViewerWindow.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "CodeViewerWindow.h"

#include <map>
#include <string>
#include <vector>

#include "CodeViewerLayout.h"
#include "CodeViewerModel.h"
#include "CodeNotesPane.h"                      // 右侧只读的批注栏与分栏
#include "../../Common/Code/CodeLexer.h"
#include "../../Common/Code/CodeNotes.h"        // SanitizeCodeNotes / CodeNoteRangesOverlap
#include "../../Common/I18n/I18n.h"
#include "../../Common/Log/IULog.h"
#include "../BalloonChatWnd/ChatCodeMessage.h"   // SaveCodeWithDialog
#include "../BalloonChatWnd/ChatMessageList.h"   // PutTextToClipboard
#include "CodeComposeLayout.h"                    // CodeAnnotateStatusText：底部一行的状态文字
#include "CodeComposeWindow.h"                    // kMsgCodeComposeSend / CodeComposeResult：把追加了批注的代码交给聊天窗
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"

using namespace balloonwjui;

namespace ballooncode {

namespace {

// ---- 标题栏：透明、穿透显示客户区白底，与「提取图中文字」窗口一致 ----
const COLORREF kCaptionColor  = RGB(0x55, 0x55, 0x55);
// 标题栏程序图标的合成底色：标题栏透明，穿透显示的是客户区的白色
const COLORREF kTitleIconBackColor = RGB(0xFF, 0xFF, 0xFF);

// 「已复制」显示多久后恢复为「复制全部」（毫秒）
const UINT kCopiedShowMs = 1500;

// 定时器编号：取得较大，避开 balloonui 宿主窗口自己的定时器
const UINT_PTR kTimerCopied = 0xC0E1;

// 制表位宽度（列），与聊天里的代码卡片相同
const int kTabColumns = 4;

// 一英寸的缇数（RTF 的制表位以缇为单位）
const int kTwipsPerInch = 1440;

// 等宽字体名，用来量一列的宽度
const wchar_t* const kCodeFontName = L"Consolas";

// 底部状态文字的颜色：平时灰色，超限时红色（与发送代码窗口相同）
const COLORREF kAnnotateStatusColor = RGB(0x8A, 0x8A, 0x8A);
const COLORREF kAnnotateWarnColor   = RGB(0xE5, 0x4D, 0x42);

//宽字符串转 UTF-8（体积检查用）
std::string ToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    if (bytes <= 0)
        return std::string();
    std::string out((size_t)bytes, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], bytes, NULL, NULL);
    return out;
}

//去掉首尾的空白（空格、制表符、换行）
std::wstring TrimBlank(const std::wstring& text)
{
    const wchar_t* const kBlank = L" \t\r\n";
    const size_t first = text.find_first_not_of(kBlank);
    if (first == std::wstring::npos)
        return std::wstring();
    const size_t last = text.find_last_not_of(kBlank);
    return text.substr(first, last - first + 1);
}

//把换行 from 换成 to（批注说明在协议里用 \n，在输入框里用 \r）
std::wstring ReplaceNewlines(const std::wstring& text, wchar_t from, wchar_t to)
{
    std::wstring out = text;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] == from)
            out[i] = to;
    }
    return out;
}

/**
 *  已打开的查看窗登记表：消息标识 → 窗口。窗口在 OnFinalMessage 里把自己移除。
 *  只在 UI 线程访问。
 */
std::map<std::string, CCodeViewerWindow*>& Registry()
{
    static std::map<std::string, CCodeViewerWindow*> s_windows;
    return s_windows;
}

//按 DPI 量出查看窗正文一列的宽度，换算成制表位宽度（缇）；量不到时返回 0（引擎按默认的半英寸）
int MeasureTabTwips(int dpi)
{
    if (dpi <= 0)
        return 0;
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
        return 0;
    HFONT font = ::CreateFontW(-CodeLineGutter::FontPxForDpi(dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               FIXED_PITCH | FF_MODERN, kCodeFontName);
    HGDIOBJ old = ::SelectObject(hdc, font);
    SIZE size = { 0, 0 };
    const BOOL ok = ::GetTextExtentPoint32W(hdc, L"0", 1, &size);
    ::SelectObject(hdc, old);
    ::DeleteObject(font);
    ::ReleaseDC(NULL, hdc);
    if (!ok || size.cx <= 0)
        return 0;
    return ::MulDiv(kTabColumns * size.cx, kTwipsPerInch, dpi);
}

//语言下拉框里语言的显示名：纯文本经 TR 翻译，其余就是语言名
std::wstring LanguageLabel(int lang)
{
    const wchar_t* name = codesnippet::CodeLanguageDisplayName(lang);
    return (lang == codesnippet::code_lang_plain) ? std::wstring(TR(name)) : std::wstring(name);
}

} // anonymous namespace

bool CCodeViewerWindow::Show(HWND anchor, const std::wstring& code, int lang, const std::string& key,
                             const std::vector<codesnippet::CodeNote>& notes, const CodeViewerExtras& extras)
{
    if (!key.empty())
    {
        std::map<std::string, CCodeViewerWindow*>::iterator it = Registry().find(key);
        if (it != Registry().end() && it->second != NULL && it->second->IsWindow())
        {
            CCodeViewerWindow* existing = it->second;
            existing->ShowWindow(::IsIconic(existing->m_hWnd) ? SW_RESTORE : SW_SHOW);
            ::SetForegroundWindow(existing->m_hWnd);
            //聊天卡片右键「添加批注…」：已经打开的窗口也展开批注栏
            if (extras.m_openNotesPane && existing->CanAnnotate())
                existing->SetNotesPaneOpen(true);
            LOG_INFO("code viewer: reused the window of message %s.", key.c_str());
            return true;
        }
    }
    CCodeViewerWindow* wnd = new CCodeViewerWindow(code, lang, key, notes, extras);
    if (!wnd->CreateAndShow(anchor))
    {
        delete wnd;
        return false;
    }
    if (!key.empty())
        Registry()[key] = wnd;
    return true;
}

CCodeViewerWindow* CCodeViewerWindow::FindOpened(const std::string& key)
{
    if (key.empty())
        return NULL;
    std::map<std::string, CCodeViewerWindow*>::iterator it = Registry().find(key);
    if (it == Registry().end() || it->second == NULL || !it->second->IsWindow())
        return NULL;
    return it->second;
}

CCodeViewerWindow::CCodeViewerWindow(const std::wstring& code, int lang, const std::string& key,
                                     const std::vector<codesnippet::CodeNote>& notes, const CodeViewerExtras& extras)
    : m_code(code)
    , m_lang(lang)
    , m_key(key)
    , m_notes(notes)
    , m_paneGrownPx(0)
    , m_messageLang(lang)
    , m_extras(extras)
    , m_anchor(NULL)
    , m_swallowEnterChar(false)
    , m_closeConfirmed(false)
    , m_pLanguage(NULL)
    , m_pWrap(NULL)
    , m_pCopy(NULL)
    , m_pEdit(NULL)
    , m_pGutter(NULL)
    , m_pNotes(NULL)
    , m_pAnnotate(NULL)
    , m_pAnnotateBar(NULL)
    , m_pAnnotateStatus(NULL)
    , m_pAnnotateSend(NULL)
    , m_pSplit(NULL)
    , m_pNotesPane(NULL)
{
}

CCodeViewerWindow::~CCodeViewerWindow()
{
}

bool CCodeViewerWindow::CreateAndShow(HWND anchor)
{
    m_anchor = anchor;
    m_content = balloonchatwnd::PrepareCodeBlockContent(m_code, m_lang);
    //批注按代码行数整理（越界、相交的条目丢弃）
    m_notes = codesnippet::SanitizeCodeNotes(m_notes, m_content.m_lineCount);
    const std::wstring title = CodeViewerTitle(m_lang, m_content.m_lineCount);
    if (Create(NULL, CWindow::rcDefault, title.c_str(), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("code viewer: Create failed.");
        return false;
    }
    SetTitle(title.c_str());
    SetTitleBarHeight(kCodeViewerTitleBarH);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kCaptionColor);
    SetCaptionGlyphColor(kCaptionColor);
    SetButtons(true, true, true);
    SetResizable(true);
    //标题栏左侧与任务栏的程序图标：标题栏透明，底色取客户区的白色；窗口没有所有者，会在任务栏出现
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, kTitleIconBackColor));
    m_programIcons.ApplyTaskIcons(m_hWnd);
    //最小尺寸是整窗尺寸（含标题栏），见开发约定文档 UI 约定第 7 条
    SetMinSize(kCodeViewerMinWidth, kCodeViewerMinHeight);

    const int fontPx = CodeLineGutter::FontPxForDpi(GetDpi());
    SetClientContent(BuildCodeViewerBody(CodeLineGutter::WidthForLines(m_content.m_lineCount, fontPx)));
    DuiControl* root = GetClientContent();
    if (root != NULL)
    {
        m_pLanguage = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kCodeViewerIdLanguage));
        m_pWrap = dynamic_cast<DuiButton*>(root->FindCtrlById(kCodeViewerIdWrap));
        m_pCopy = dynamic_cast<DuiButton*>(root->FindCtrlById(kCodeViewerIdCopy));
        m_pEdit = dynamic_cast<CodeMarkedEdit*>(root->FindCtrlById(kCodeViewerIdEdit));
        m_pGutter = dynamic_cast<CodeLineGutter*>(root->FindCtrlById(kCodeViewerIdGutter));
        m_pNotes = dynamic_cast<DuiButton*>(root->FindCtrlById(kCodeViewerIdNotes));
        m_pAnnotate = dynamic_cast<DuiButton*>(root->FindCtrlById(kCodeViewerIdAnnotate));
        m_pAnnotateBar = root->FindCtrlById(kCodeViewerIdAnnotateBar);
        m_pAnnotateStatus = dynamic_cast<DuiLabel*>(root->FindCtrlById(kCodeViewerIdAnnotateStatus));
        m_pAnnotateSend = dynamic_cast<DuiButton*>(root->FindCtrlById(kCodeViewerIdAnnotateSend));
        m_pSplit = dynamic_cast<CodeNotesSplitter*>(root->FindCtrlById(kCodeViewerIdSplit));
        m_pNotesPane = dynamic_cast<CodeNotesPane*>(root->FindCtrlById(kCodeViewerIdNotesPane));
    }
    //选区变化时投给自己，留到消息里高亮光标所在行的批注；最后一行的底色按正文字体的行高确定
    if (m_pEdit != NULL)
    {
        m_pEdit->SetSelChangeMessage(kMsgCodeViewerSelChanged, 0);
        m_pEdit->SetLineHeight(CodeFontLineHeight(fontPx));
    }
    //能追加批注（docs/代码片段方案.md 第 12 节）：以这条消息的批注为底（作者补全）；没能发出、重新编辑时放回上次的批注。
    //批注栏改为可以编辑，只有本人的批注能修改、删除；这几项要在显示批注之前设好
    if (CanAnnotate())
    {
        m_originalNotes = codesnippet::FillCodeNoteAuthors(m_notes, m_extras.m_senderId);
        if (!m_extras.m_editedNotes.empty())
            m_notes = codesnippet::MergeAnnotationRound(m_originalNotes, m_extras.m_editedNotes, m_extras.m_selfId,
                                                        m_content.m_lineCount);
        if (m_pNotesPane != NULL)
        {
            m_pNotesPane->SetReadOnly(false);
            m_pNotesPane->SetSelfId(m_extras.m_selfId);
            m_pNotesPane->SetEmptyText(TR(L"选中代码行后点「添加批注」添加说明"));
            m_pNotesPane->SetAuthorNames(m_extras.m_authorNames);
            m_pNotesPane->SetNotes(m_notes);
        }
        //正文右键菜单「添加批注…」与底部一行（状态文字与「发送」）；显隐变化后对根容器重排（UI 约定第 5 条）
        if (m_pEdit != NULL)
            m_pEdit->AppendContextMenuItem(kCodeViewerMenuAddNote, TR(L"添加批注…"));
        if (m_pAnnotateBar != NULL)
        {
            m_pAnnotateBar->SetVisible(true);
            if (m_pAnnotateBar->GetParent() != NULL)
                m_pAnnotateBar->GetParent()->ForceLayout(m_pAnnotateBar->GetParent()->GetRect());
        }
    }
    //有批注：显示「批注 (N)」，批注栏载入批注（只读）；工具条显隐变化后重排（UI 约定第 5 条）
    if (!m_notes.empty())
    {
        if (m_pNotes != NULL)
        {
            m_pNotes->SetText(TRF(L"批注 ({0})", static_cast<unsigned int>(m_notes.size())).c_str());
            m_pNotes->SetVisible(true);
            if (m_pNotes->GetParent() != NULL)
                m_pNotes->GetParent()->ForceLayout(m_pNotes->GetParent()->GetRect());
        }
        if (m_pNotesPane != NULL)
        {
            //批注卡片写出作者（docs/代码片段方案.md 第 11.3 节）；名字表要在显示批注之前设好
            m_pNotesPane->SetAuthorNames(m_extras.m_authorNames);
            m_pNotesPane->SetNotes(m_notes);
        }
    }
    //能追加批注时显示「添加批注」；工具条显隐变化后重排（UI 约定第 5 条）
    if (m_extras.m_canAnnotate && m_pAnnotate != NULL)
    {
        m_pAnnotate->SetVisible(true);
        if (m_pAnnotate->GetParent() != NULL)
            m_pAnnotate->GetParent()->ForceLayout(m_pAnnotate->GetParent()->GetRect());
    }
    if (m_pLanguage != NULL)
    {
        for (int lang = 0; lang < codesnippet::code_lang_count; ++lang)
            m_pLanguage->AddString(LanguageLabel(lang).c_str());
        m_pLanguage->SetCurSel(m_lang, false);
    }

    ResizeClient(kCodeViewerWindowWidth, kCodeViewerWindowHeight);
    //有批注时批注栏默认展开，窗口在居中之前加宽（docs/代码片段方案.md 第 10.5 节）；聊天卡片右键「添加批注…」打开时
    //没有批注也展开（第 12 节）
    if (!m_notes.empty() || (CanAnnotate() && m_extras.m_openNotesPane))
        SetNotesPaneOpen(true);
    if (anchor != NULL && ::IsWindow(anchor))
        CenterWindow(anchor);
    else
        CenterWindow(::GetDesktopWindow());
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    LoadContent();
    //追加批注的消息：选中第一条「新」批注的行，对应的卡片高亮（批注按起始行排列，第一条就是行号最靠前的）
    int firstNew = -1;
    for (size_t i = 0; i < m_notes.size() && firstNew < 0; ++i)
    {
        if (m_notes[i].m_isNew)
            firstNew = (int)i;
    }
    if (firstNew >= 0)
        SelectNoteLines(firstNew);
    RefreshAnnotateBar();
    LOG_INFO("code viewer opened: key=%s, lang=%s, lines=%d, notes=%u, new notes=%d, can annotate=%d, edited=%u.",
             m_key.c_str(), codesnippet::CodeLanguageId(m_lang), m_content.m_lineCount, (unsigned)m_notes.size(),
             codesnippet::CountNewCodeNotes(m_notes), CanAnnotate() ? 1 : 0, (unsigned)m_extras.m_editedNotes.size());
    return true;
}

bool CCodeViewerWindow::CanAnnotate() const
{
    return m_extras.m_canAnnotate && m_extras.m_selfId != 0;
}

void CCodeViewerWindow::SelectedLines(int& from, int& to) const
{
    from = 1;
    to = 1;
    if (m_pEdit == NULL || m_lineStarts.empty())
        return;
    long selBegin = 0;
    long selEnd = 0;
    m_pEdit->GetSel(selBegin, selEnd);
    //选区的结束位置在行首时那一行不算；只有光标时为光标所在的一行
    from = LineOfPosition(m_lineStarts, selBegin) + 1;
    to = LineOfPosition(m_lineStarts, (selEnd > selBegin) ? selEnd - 1 : selBegin) + 1;
}

void CCodeViewerWindow::BeginAddNote()
{
    if (!CanAnnotate() || m_pNotesPane == NULL)
        return;
    //已经在编辑一条批注：把焦点交给它的输入框，不另起一条
    if (m_pNotesPane->IsEditing())
    {
        if (m_pNotesPane->EditBox() != NULL)
            SetDuiFocus(m_pNotesPane->EditBox());
        return;
    }
    int from = 1;
    int to = 1;
    SelectedLines(from, to);
    BeginAddNoteOnLines(from, to);
}

void CCodeViewerWindow::BeginAddNoteOnLines(int from, int to)
{
    if (!CanAnnotate() || m_pNotesPane == NULL || m_pNotesPane->IsEditing())
        return;
    //这几行已有本人的批注：改为修改那一条；只有别人的批注时新建一条
    const int existing = codesnippet::FindEditableCodeNoteInRange(m_notes, from, to, m_extras.m_selfId);
    if (existing >= 0)
    {
        BeginEditNote(existing);
        return;
    }
    if ((int)m_notes.size() >= codesnippet::kCodeNotesMax)
        return;
    SetNotesPaneOpen(true);
    m_pNotesPane->BeginEdit(from, to, std::wstring(), -1);
    EnterNoteEditing();
    LOG_INFO("code viewer: adding a note on lines %d-%d, key=%s.", from, to, m_key.c_str());
}

void CCodeViewerWindow::BeginEditNote(int index)
{
    if (!CanAnnotate() || m_pNotesPane == NULL || index < 0 || index >= (int)m_notes.size())
        return;
    //别人的批注不能修改
    if (!codesnippet::CanEditCodeNote(m_notes[(size_t)index], m_extras.m_selfId))
        return;
    //已经在编辑：先确定或取消那一条，不同时编辑两条
    if (m_pNotesPane->IsEditing())
    {
        if (m_pNotesPane->EditBox() != NULL)
            SetDuiFocus(m_pNotesPane->EditBox());
        return;
    }
    SetNotesPaneOpen(true);
    const codesnippet::CodeNote note = m_notes[(size_t)index];
    //说明里的换行在协议里是 \n，输入框里是 \r
    m_pNotesPane->BeginEdit(note.m_from, note.m_to, ReplaceNewlines(note.m_text, L'\n', L'\r'), index);
    EnterNoteEditing();
}

void CCodeViewerWindow::ConfirmNoteEdit()
{
    if (m_pNotesPane == NULL || !m_pNotesPane->IsEditing())
        return;
    const int index = m_pNotesPane->EditingIndex();
    const int from = m_pNotesPane->EditingFrom();
    const int to = m_pNotesPane->EditingTo();
    const std::wstring text = ReplaceNewlines(TrimBlank(m_pNotesPane->EditingText()), L'\r', L'\n');
    m_pNotesPane->EndEdit();
    LeaveNoteEditing();
    //说明为空：新增的放弃，修改的删除
    if (text.empty())
    {
        if (index >= 0)
            DeleteNote(index);
        return;
    }
    //新增的批注作者为 0（本机写的），由 MergedNotes 补成本人并标「新」
    if (index >= 0 && index < (int)m_notes.size())
        m_notes[(size_t)index].m_text = text;
    else
        m_notes.push_back(codesnippet::CodeNote(from, to, text));
    m_notes = MergedNotes();
    ApplyNotes();
    LOG_INFO("code viewer: note %s on lines %d-%d, %u chars, %u notes, %d unsent, key=%s.", index >= 0 ? "edited" : "added",
             from, to, (unsigned)text.size(), (unsigned)m_notes.size(), UnsentNoteCount(), m_key.c_str());
}

void CCodeViewerWindow::CancelNoteEdit()
{
    if (m_pNotesPane == NULL || !m_pNotesPane->IsEditing())
        return;
    m_pNotesPane->EndEdit();
    LeaveNoteEditing();
}

void CCodeViewerWindow::DeleteNote(int index)
{
    if (!CanAnnotate() || index < 0 || index >= (int)m_notes.size())
        return;
    //别人的批注不能删除；编辑期间不删除别的批注（批注栏在编辑期间不重建）
    if (!codesnippet::CanEditCodeNote(m_notes[(size_t)index], m_extras.m_selfId))
        return;
    if (m_pNotesPane != NULL && m_pNotesPane->IsEditing())
    {
        if (m_pNotesPane->EditBox() != NULL)
            SetDuiFocus(m_pNotesPane->EditBox());
        return;
    }
    const codesnippet::CodeNote removed = m_notes[(size_t)index];
    m_notes.erase(m_notes.begin() + index);
    m_notes = MergedNotes();
    ApplyNotes();
    LOG_INFO("code viewer: note on lines %d-%d deleted, %u left, key=%s.", removed.m_from, removed.m_to,
             (unsigned)m_notes.size(), m_key.c_str());
}

void CCodeViewerWindow::EnterNoteEditing()
{
    RefreshAnnotateBar();
    if (m_pNotesPane != NULL)
    {
        //编辑卡片在列表里对应的批注下标就是 EditingIndex（新增时为 -1）
        m_pNotesPane->ScrollToNote(m_pNotesPane->EditingIndex());
        if (m_pNotesPane->EditBox() != NULL)
            SetDuiFocus(m_pNotesPane->EditBox());
    }
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

void CCodeViewerWindow::LeaveNoteEditing()
{
    if (m_pEdit != NULL)
        SetDuiFocus(m_pEdit);
    RefreshAnnotateBar();
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

std::vector<codesnippet::CodeNote> CCodeViewerWindow::MergedNotes() const
{
    return codesnippet::MergeAnnotationRound(m_originalNotes, m_notes, m_extras.m_selfId, m_content.m_lineCount);
}

int CCodeViewerWindow::UnsentNoteCount() const
{
    if (!CanAnnotate())
        return 0;
    int count = codesnippet::CountNewCodeNotes(MergedNotes());
    //正在编辑、输入框里已经有文字的那一条也算
    if (m_pNotesPane != NULL && m_pNotesPane->IsEditing() && !TrimBlank(m_pNotesPane->EditingText()).empty())
        ++count;
    return count;
}

void CCodeViewerWindow::ApplyNotes()
{
    //批注栏：编辑期间批注不会变，不重建，免得丢掉输入框里的文字
    if (m_pNotesPane != NULL && !m_pNotesPane->IsEditing())
        m_pNotesPane->SetNotes(m_notes);
    //行号栏的橙色竖条与正文的浅黄底色：行号从 0 起
    std::vector<std::pair<int, int> > ranges;
    for (size_t i = 0; i < m_notes.size(); ++i)
        ranges.push_back(std::make_pair(m_notes[i].m_from - 1, m_notes[i].m_to - 1));
    if (m_pGutter != NULL)
        m_pGutter->SetMarkedRanges(ranges);
    if (m_pEdit != NULL)
        m_pEdit->SetMarkedRanges(ranges);
    //「批注 (N)」：有批注时显示；显隐变化后对工具条重排（UI 约定第 5 条）
    if (m_pNotes != NULL)
    {
        m_pNotes->SetText(TRF(L"批注 ({0})", static_cast<unsigned int>(m_notes.size())).c_str());
        const bool show = !m_notes.empty();
        if (m_pNotes->IsVisible() != show)
        {
            m_pNotes->SetVisible(show);
            if (m_pNotes->GetParent() != NULL)
                m_pNotes->GetParent()->ForceLayout(m_pNotes->GetParent()->GetRect());
        }
    }
    RefreshAnnotateBar();
    ::InvalidateRect(m_hWnd, NULL, FALSE);
    //按光标所在的行重新高亮卡片：留到消息里读取选区
    PostMessage(kMsgCodeViewerSelChanged, kCodeViewerDeferSelChanged);
}

void CCodeViewerWindow::RefreshAnnotateBar()
{
    if (!CanAnnotate())
        return;
    const std::vector<codesnippet::CodeNote> merged = MergedNotes();
    const int newCount = codesnippet::CountNewCodeNotes(merged);
    //体积按不带引用计；聊天窗发送前带上引用再查一次
    const bool tooLarge = balloonchatwnd::IsCodeTooLarge(ToUtf8(m_code), codesnippet::CodeLanguageId(m_messageLang),
                                                         net::ReplyQuote(),
                                                         balloonchatwnd::CodeNotesToData(merged, m_extras.m_selfId));
    if (m_pAnnotateStatus != NULL)
    {
        const std::wstring status = CodeAnnotateStatusText(m_extras.m_senderName, m_messageLang, m_content.m_lineCount,
                                                           newCount, tooLarge);
        m_pAnnotateStatus->SetText(status.c_str());
        m_pAnnotateStatus->SetTextColor(tooLarge ? kAnnotateWarnColor : kAnnotateStatusColor);
    }
    if (m_pAnnotateSend != NULL)
        m_pAnnotateSend->SetEnabled(newCount > 0 && !tooLarge);
    //正在编辑或批注已达上限时不能再添加
    const bool editing = (m_pNotesPane != NULL) && m_pNotesPane->IsEditing();
    const bool full = (int)m_notes.size() >= codesnippet::kCodeNotesMax;
    if (m_pAnnotate != NULL)
        m_pAnnotate->SetEnabled(!editing && !full);
    CodeViewerEdit* viewerEdit = dynamic_cast<CodeViewerEdit*>(m_pEdit);
    if (viewerEdit != NULL)
        viewerEdit->SetAddNoteEnabled(!editing && !full);
}

void CCodeViewerWindow::SendAnnotation()
{
    if (!CanAnnotate())
        return;
    //还有没确定的批注：先确定它，随代码一起发出去
    if (m_pNotesPane != NULL && m_pNotesPane->IsEditing())
        ConfirmNoteEdit();
    const std::vector<codesnippet::CodeNote> merged = MergedNotes();
    if (codesnippet::CountNewCodeNotes(merged) == 0
        || balloonchatwnd::IsCodeTooLarge(ToUtf8(m_code), codesnippet::CodeLanguageId(m_messageLang), net::ReplyQuote(),
                                          balloonchatwnd::CodeNotesToData(merged, m_extras.m_selfId)))
    {
        RefreshAnnotateBar();
        return;
    }
    //发起打开的聊天窗已经关了：请用户重新打开聊天窗再发
    if (m_anchor == NULL || !::IsWindow(m_anchor))
    {
        ::MessageBox(m_hWnd, TR(L"打开这段代码的聊天窗口已经关闭，请重新打开聊天窗口后再添加批注。"), TR(L"添加批注"),
                     MB_OK | MB_ICONINFORMATION);
        LOG_INFO("code viewer: send requested but the chat window is gone, key=%s.", m_key.c_str());
        return;
    }
    CodeComposeResult* result = new CodeComposeResult();
    result->m_code = m_code;
    result->m_lang = m_messageLang;
    result->m_manualLang = false;
    result->m_notes = merged;
    result->m_annotate.m_baseMsgId = m_extras.m_msgid;
    result->m_annotate.m_baseSenderId = m_extras.m_senderId;
    result->m_annotate.m_baseSenderName = m_extras.m_senderName;
    result->m_annotate.m_selfId = m_extras.m_selfId;
    result->m_annotate.m_authorNames = m_extras.m_authorNames;
    result->m_originalNotes = m_originalNotes;
    const int newCount = codesnippet::CountNewCodeNotes(merged);
    //先投递关闭、再把结果交给聊天窗：两条消息在同一个线程的队列里按投递顺序处理。聊天窗没能发出时会重新打开这条消息的
    //查看窗并放回批注，若关闭排在结果之后，刚重新打开的窗口会因同一个消息标识而被当作本窗口、随即被关掉
    m_closeConfirmed = true;
    PostMessage(WM_CLOSE);
    if (!::PostMessage(m_anchor, kMsgCodeComposeSend, 0, (LPARAM)result))
    {
        delete result;
        LOG_WARNING("code viewer: PostMessage to the chat window failed, error %u.", (unsigned)::GetLastError());
        return;
    }
    LOG_INFO("code viewer: annotation handed to the chat window, key=%s, notes=%u, new notes=%d.", m_key.c_str(),
             (unsigned)merged.size(), newCount);
}

void CCodeViewerWindow::LoadContent()
{
    m_content = balloonchatwnd::PrepareCodeBlockContent(m_code, m_lang, m_notes);
    const std::wstring title = CodeViewerTitle(m_lang, m_content.m_lineCount);
    SetTitle(title.c_str());
    ::SetWindowTextW(m_hWnd, title.c_str());
    if (m_pEdit == NULL)
        return;
    const std::string rtf = BuildCodeRtf(m_content, kCodeViewerFontHalfPoints, MeasureTabTwips(GetDpi()),
                                         i18n::UiFontName());
    if (!m_pEdit->LoadRTF(CStringA(rtf.c_str())))
        LOG_WARNING("code viewer: LoadRTF failed, rtf bytes=%u.", (unsigned)rtf.size());
    m_pEdit->SetSel(0, 0);
    m_pEdit->SetVScrollPos(0);
    BuildLineStartPositions(m_content.m_code, m_lineStarts);
    if (m_pGutter != NULL)
    {
        m_pGutter->SetLineStarts(m_lineStarts);
        m_pGutter->SetMarkedRanges(m_content.m_noteLines);
    }
    //被批注的行铺浅黄底色：按逻辑行的起点定位，开了自动换行也正确
    m_pEdit->SetMarkLineStarts(m_lineStarts);
    m_pEdit->SetMarkedRanges(m_content.m_noteLines);
}

void CCodeViewerWindow::SetNotesPaneOpen(bool open)
{
    if (m_pSplit == NULL || m_pSplit->IsCollapsed() != open)
        return;
    m_pSplit->SetCollapsed(!open);
    ResizeWindowForNotesPane(m_hWnd, open, m_pSplit->RightWidth() + m_pSplit->GetBarThickness(), m_paneGrownPx);
    //显隐变化不会自动重排（UI 约定第 5 条）：窗口尺寸没变时分栏的矩形也没变，要对它强制重排
    m_pSplit->ForceLayout(m_pSplit->GetRect());
    ::InvalidateRect(m_hWnd, NULL, FALSE);
    LOG_INFO("code viewer: notes pane %s, window grown by %d px.", open ? "opened" : "closed", m_paneGrownPx);
}

void CCodeViewerWindow::SelectNoteLines(int index)
{
    if (m_pEdit == NULL || index < 0 || index >= (int)m_notes.size() || m_lineStarts.empty())
        return;
    const int lineCount = (int)m_lineStarts.size();
    const codesnippet::CodeNote& note = m_notes[(size_t)index];
    if (note.m_from > lineCount)
        return;
    const int last = (note.m_to < lineCount) ? note.m_to : lineCount;
    const long begin = m_lineStarts[(size_t)(note.m_from - 1)];
    //末行的行尾：下一行起点减去换行；末行是最后一行时取全文末尾
    long end = 0;
    if (last < lineCount)
        end = m_lineStarts[(size_t)last] - 1;
    else
    {
        const size_t lastBreak = m_content.m_code.rfind(L'\n');
        const long lastLength = (long)((lastBreak == std::wstring::npos) ? m_content.m_code.size()
                                                                        : m_content.m_code.size() - lastBreak - 1);
        end = m_lineStarts.back() + lastLength;
    }
    //先把起始行滚到可见，再选中整段
    m_pEdit->SetSel(begin, begin);
    m_pEdit->SendMessageToEngine(EM_SCROLLCARET, 0, 0);
    m_pEdit->SetSel(begin, end);
    m_pEdit->SendMessageToEngine(EM_SCROLLCARET, 0, 0);
    SetDuiFocus(m_pEdit);
    if (m_pNotesPane != NULL)
    {
        m_pNotesPane->SetHighlight(index);
        m_pNotesPane->ScrollToNote(index);
    }
}

void CCodeViewerWindow::HighlightNoteAtCaret()
{
    if (m_pEdit == NULL || m_pNotesPane == NULL || m_notes.empty())
        return;
    long selBegin = 0;
    long selEnd = 0;
    m_pEdit->GetSel(selBegin, selEnd);
    //选区覆盖的逻辑行（从 1 起）；选区的结束位置在行首时那一行不算
    const int first = LineOfPosition(m_lineStarts, selBegin) + 1;
    const int last = LineOfPosition(m_lineStarts, (selEnd > selBegin) ? selEnd - 1 : selBegin) + 1;
    int found = -1;
    for (size_t i = 0; i < m_notes.size(); ++i)
    {
        if (codesnippet::CodeNoteRangesOverlap(first, last, m_notes[i].m_from, m_notes[i].m_to))
        {
            found = (int)i;
            break;
        }
    }
    if (found == m_pNotesPane->Highlight())
        return;
    m_pNotesPane->SetHighlight(found);
    if (found >= 0)
        m_pNotesPane->ScrollToNote(found);
}

void CCodeViewerWindow::CopyAll()
{
    const std::wstring text = balloonchatwnd::CodeTextForClipboard(m_content.m_code);
    balloonchatwnd::PutTextToClipboard(m_hWnd, text.c_str());
    if (m_pCopy != NULL)
    {
        m_pCopy->SetText(TR(L"已复制"));
        SetTimer(kTimerCopied, kCopiedShowMs, NULL);
    }
    LOG_INFO("code viewer: copied %u chars.", (unsigned)text.size());
}

LRESULT CCodeViewerWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //换了语言：只改本窗口的着色
    if (pNotify->code == DUIN_VALUECHANGED && pNotify->ctrlId == kCodeViewerIdLanguage)
    {
        if (m_pLanguage != NULL && m_pLanguage->GetCurSel() >= 0)
        {
            m_lang = m_pLanguage->GetCurSel();
            LoadContent();
        }
        return 0;
    }
    //正文右键菜单的「添加批注…」（docs/代码片段方案.md 第 12 节）
    if (pNotify->code == (UINT)DuiRichEdit::DUIN_RICHTEXT_MENUCOMMAND && pNotify->ctrlId == kCodeViewerIdEdit
        && (UINT)pNotify->extra == kCodeViewerMenuAddNote)
    {
        BeginAddNote();
        return 0;
    }
    //开关自动换行：行号按逻辑行画，换行后要重画
    if (pNotify->code == DUIN_VALUECHANGED && pNotify->ctrlId == kCodeViewerIdWrap)
    {
        if (m_pEdit != NULL && m_pWrap != NULL)
            m_pEdit->SetWordWrap(m_pWrap->IsChecked());
        if (m_pGutter != NULL)
            m_pGutter->Invalidate();
        return 0;
    }
    if (pNotify->code == DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        //「复制全部」：复制代码原文
        case kCodeViewerIdCopy:
            CopyAll();
            return 0;

        //「另存为…」：把代码写进用户选的文件
        case kCodeViewerIdSave:
            balloonchatwnd::SaveCodeWithDialog(m_hWnd, m_content.m_code, m_lang);
            return 0;

        //「批注 (N)」：展开或收起批注栏
        case kCodeViewerIdNotes:
            SetNotesPaneOpen(m_pSplit != NULL && m_pSplit->IsCollapsed());
            return 0;

        //「添加批注」：为选中的行添加批注（docs/代码片段方案.md 第 12 节）
        case kCodeViewerIdAnnotate:
            BeginAddNote();
            return 0;

        //底部一行的「发送」：把带批注的新代码片段交给聊天窗
        case kCodeViewerIdAnnotateSend:
            SendAnnotation();
            return 0;

        //编辑卡片的「确定」「取消」：会重建卡片列表（按钮自己就在列表里），等点击通知处理完再做（UI 约定第 13 条）
        case kCodeNotesIdEditOk:
            PostMessage(kMsgCodeViewerSelChanged, kCodeViewerDeferConfirmNote);
            return 0;

        //编辑卡片的「取消」：同上，延后执行
        case kCodeNotesIdEditCancel:
            PostMessage(kMsgCodeViewerSelChanged, kCodeViewerDeferCancelNote);
            return 0;

        //批注栏顶部的「»」：收起批注栏
        case kCodeNotesIdCollapse:
            SetNotesPaneOpen(false);
            return 0;

        //批注卡片上的「第 3～4 行」「修改」「删除」；其余控件的点击（含标题栏按钮）交给框架
        default:
        {
            int index = -1;
            int part = -1;
            if (!CodeNotesIdToNote(pNotify->ctrlId, index, part))
                break;
            //「第 x～y 行」只改卡片的高亮，可以直接做；「修改」「删除」会重建卡片列表，延后执行
            if (part == kCodeNotesPartRange)
                SelectNoteLines(index);
            else if (part == kCodeNotesPartEdit)
                PostMessage(kMsgCodeViewerSelChanged, kCodeViewerDeferEditNote, (LPARAM)index);
            else if (part == kCodeNotesPartDelete)
                PostMessage(kMsgCodeViewerSelChanged, kCodeViewerDeferDeleteNote, (LPARAM)index);
            return 0;
        }
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CCodeViewerWindow::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //「已复制」显示时间到：恢复按钮文字
    if (wParam == kTimerCopied)
    {
        KillTimer(kTimerCopied);
        if (m_pCopy != NULL)
            m_pCopy->SetText(TR(L"复制全部"));
        return 0;
    }
    //balloonui 宿主窗口自己的定时器：交给框架
    bHandled = FALSE;
    return 0;
}

LRESULT CCodeViewerWindow::OnSelChanged(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    switch (wParam)
    {
    //正文的选区变了，或批注变了：高亮光标所在行的批注
    case kCodeViewerDeferSelChanged:
        HighlightNoteAtCaret();
        break;

    //编辑卡片的「确定」，或批注输入框里按了 Ctrl+Enter
    case kCodeViewerDeferConfirmNote:
        m_swallowEnterChar = false;
        ConfirmNoteEdit();
        break;

    //编辑卡片的「取消」
    case kCodeViewerDeferCancelNote:
        CancelNoteEdit();
        break;

    //卡片上的「修改」
    case kCodeViewerDeferEditNote:
        BeginEditNote((int)lParam);
        break;

    //卡片上的「删除」
    case kCodeViewerDeferDeleteNote:
        DeleteNote((int)lParam);
        break;

    //不认识的动作：忽略
    default:
        break;
    }
    return 0;
}

LRESULT CCodeViewerWindow::OnKeyDownMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool inNoteBox = (m_pNotesPane != NULL && m_pNotesPane->EditBox() != NULL
                            && m_pNotesPane->EditBox()->IsFocused());
    switch (wParam)
    {
    //批注输入框里按 Ctrl+Enter：确定这条批注，等这次按键的字符消息处理完再做，换行不会被插进去
    case VK_RETURN:
        if (ctrl && inNoteBox)
        {
            m_swallowEnterChar = true;
            PostMessage(kMsgCodeViewerSelChanged, kCodeViewerDeferConfirmNote);
            return 0;
        }
        break;

    //Esc：正在编辑批注时放弃这条批注
    case VK_ESCAPE:
        if (m_pNotesPane != NULL && m_pNotesPane->IsEditing())
        {
            CancelNoteEdit();
            return 0;
        }
        break;

    //其余按键：交给 DuiHost 转给焦点控件
    default:
        break;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CCodeViewerWindow::OnCharMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //Ctrl+Enter 产生的换行字符丢掉
    if (m_swallowEnterChar && (wParam == L'\n' || wParam == L'\r'))
    {
        m_swallowEnterChar = false;
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CCodeViewerWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //有没发出的批注：先问是否放弃（docs/代码片段方案.md 第 12.1 节）；发出之后不再问
    if (!m_closeConfirmed)
    {
        const int unsent = UnsentNoteCount();
        if (unsent > 0)
        {
            const std::wstring ask = TRF(L"有 {0} 条批注还没发出，关闭后会丢弃。要关闭吗？", static_cast<unsigned int>(unsent));
            if (::MessageBox(m_hWnd, ask.c_str(), TR(L"添加批注"), MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
            {
                LOG_INFO("code viewer: close cancelled, %d unsent notes, key=%s.", unsent, m_key.c_str());
                return 0;
            }
            LOG_INFO("code viewer: closed with %d unsent notes discarded, key=%s.", unsent, m_key.c_str());
        }
    }
    KillTimer(kTimerCopied);
    //控件树随窗口销毁，借用的指针一并作废
    m_pLanguage = NULL;
    m_pWrap = NULL;
    m_pCopy = NULL;
    m_pEdit = NULL;
    m_pGutter = NULL;
    m_pNotes = NULL;
    m_pAnnotate = NULL;
    m_pAnnotateBar = NULL;
    m_pAnnotateStatus = NULL;
    m_pAnnotateSend = NULL;
    m_pSplit = NULL;
    m_pNotesPane = NULL;
    DestroyWindow();
    return 0;
}

void CCodeViewerWindow::OnFinalMessage(HWND /*hWnd*/)
{
    if (!m_key.empty())
    {
        std::map<std::string, CCodeViewerWindow*>::iterator it = Registry().find(m_key);
        if (it != Registry().end() && it->second == this)
            Registry().erase(it);
    }
    LOG_INFO("code viewer closed: key=%s.", m_key.c_str());
    delete this;
}

} // namespace ballooncode
