/**
 *  「发送代码」窗口，见 CodeComposeWindow.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "CodeComposeWindow.h"

#include <string>

#include "CodeComposeLayout.h"
#include "CodeNotesPane.h"                      // 右侧的批注栏与分栏
#include "../../Common/Code/CodeLexer.h"
#include "../../Common/Code/CodeFormatter.h"   // FormatCode / FormatCodeSelection
#include "../../Common/Code/CodeNotes.h"       // 批注的整理、平移与重新定位
#include "../../Common/I18n/I18n.h"
#include "../../Common/Log/IULog.h"
#include "../BalloonChatWnd/ChatCodeBlock.h"     // NormalizeCodeNewlines
#include "../BalloonChatWnd/ChatCodeMessage.h"   // IsCodeTooLarge
#include "../../App/FlamingoClient.h"             // 用户配置：是否显示行号
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"

using namespace balloonwjui;

namespace ballooncode {

namespace {

// ---- 标题栏：透明、穿透显示客户区白底，与「提取图中文字」窗口一致 ----
const COLORREF kCaptionColor = RGB(0x55, 0x55, 0x55);
// 标题栏程序图标的合成底色：标题栏透明，穿透显示的是客户区的白色
const COLORREF kTitleIconBackColor = RGB(0xFF, 0xFF, 0xFF);

// 状态文字的颜色：平时灰色，超限时红色
const COLORREF kStatusColor     = RGB(0x8A, 0x8A, 0x8A);
const COLORREF kStatusWarnColor = RGB(0xE5, 0x4D, 0x42);

// 编辑框字号按 96 DPI 计（kCodeComposeFontPx），实际按窗口 DPI 换算
const int kBaseDpi = 96;
const wchar_t* const kEditorFontName = L"Consolas";

// 编辑框文字变化后多久刷新状态文字（毫秒）：连续输入时只刷新一次
const UINT kStatusDelayMs = 300;

// 定时器编号：取得较大，避开 balloonui 宿主窗口自己的定时器
const UINT_PTR kTimerStatus = 0xC0F1;

//宽字符串转 UTF-8
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

//代码有几行（规范化后的代码，空代码为 0）
int CountLines(const std::wstring& code)
{
    if (code.empty())
        return 0;
    int lines = 1;
    for (size_t i = 0; i < code.size(); ++i)
    {
        if (code[i] == L'\n')
            ++lines;
    }
    return lines;
}

//当前登录账号的用户配置；调试入口没有登录态时为 NULL
CUserConfig* CurrentUserConfig()
{
    CFlamingoClient* client = CFlamingoClient::GetInstance();
    return (client != NULL) ? &client->m_UserConfig : NULL;
}

//两组批注是否完全相同（行范围、说明、作者与「新」都相同）
bool SameNotes(const std::vector<codesnippet::CodeNote>& a, const std::vector<codesnippet::CodeNote>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].m_from != b[i].m_from || a[i].m_to != b[i].m_to || a[i].m_text != b[i].m_text
            || a[i].m_authorId != b[i].m_authorId || a[i].m_isNew != b[i].m_isNew)
            return false;
    }
    return true;
}

//把换行统一成 from 换成 to（批注说明在协议里用 \n，在输入框里用 \r）
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

//text 里 [0, pos) 之间有几个换行（\r），即 pos 所在的行号减一
int LineIndexOf(const std::wstring& text, long pos)
{
    int line = 0;
    const size_t end = (pos < 0) ? 0 : ((size_t)pos < text.size() ? (size_t)pos : text.size());
    for (size_t i = 0; i < end; ++i)
    {
        if (text[i] == L'\r')
            ++line;
    }
    return line;
}

} // anonymous namespace

CCodeComposeWindow::CCodeComposeWindow()
    : m_owner(NULL)
    , m_editorFont(NULL)
    , m_fontPx(kCodeComposeFontPx)
    , m_swallowEnterChar(false)
    , m_hasDeletedNote(false)
    , m_paneGrownPx(0)
    , m_pLanguage(NULL)
    , m_pStatus(NULL)
    , m_pEditor(NULL)
    , m_pCodeEdit(NULL)
    , m_pSend(NULL)
    , m_pLineNumbers(NULL)
    , m_pGutter(NULL)
    , m_pNotesButton(NULL)
    , m_pUndoLink(NULL)
    , m_pSplit(NULL)
    , m_pNotesPane(NULL)
{
}

CCodeComposeWindow::~CCodeComposeWindow()
{
    if (m_editorFont != NULL)
    {
        ::DeleteObject(m_editorFont);
        m_editorFont = NULL;
    }
}

bool CCodeComposeWindow::CreateFrame(HWND owner, const wchar_t* title)
{
    if (Create(owner, CWindow::rcDefault, title, WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("code compose: Create failed.");
        return false;
    }
    SetTitle(title);
    SetTitleBarHeight(kCodeComposeTitleBarH);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kCaptionColor);
    SetCaptionGlyphColor(kCaptionColor);
    SetButtons(false, true, true);
    SetResizable(true);
    //标题栏左侧的程序图标：标题栏透明，底色取客户区的白色。窗口有所有者，不在任务栏出现
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, kTitleIconBackColor));
    //最小尺寸是整窗尺寸（含标题栏），见开发约定文档 UI 约定第 7 条
    SetMinSize(kCodeComposeMinWidth, kCodeComposeMinHeight);
    //新窗口：上一次打开时的批注与状态不再沿用
    m_notes.clear();
    m_notesText.clear();
    m_hasDeletedNote = false;
    m_paneGrownPx = 0;
    m_statusNote.clear();
    m_statusNoteText.clear();
    BuildClient();
    ResizeClient(kCodeComposeWindowWidth, kCodeComposeWindowHeight);
    return true;
}

bool CCodeComposeWindow::Open(HWND owner, const std::wstring& initialCode, const std::string& initialLangId,
                              const std::vector<codesnippet::CodeNote>& initialNotes)
{
    m_owner = owner;
    const bool existed = (IsWindow() != FALSE);
    if (!existed && !CreateFrame(owner, TR(L"发送代码")))
        return false;

    //预选语言：认识的语言标识选中它，否则选「自动识别」
    if (m_pLanguage != NULL && (!existed || !initialLangId.empty()))
    {
        int index = 0;
        if (!initialLangId.empty())
        {
            const int lang = codesnippet::CodeLanguageFromId(initialLangId);
            if (lang != codesnippet::code_lang_plain || initialLangId == codesnippet::CodeLanguageId(lang))
                index = lang + 1;
        }
        m_pLanguage->SetCurSel(index, false);
    }
    if (m_pEditor != NULL && !initialCode.empty())
    {
        //换了代码：正在编辑的批注放弃，批注换成随代码带来的那一组（行号指 initialCode 的行）
        if (m_pNotesPane != NULL && m_pNotesPane->IsEditing())
            CancelNoteEdit();
        //先清空旧批注：写入文字时引擎报告内容变化，不要把旧批注按新文字重新定位
        m_notes.clear();
        m_pEditor->SetText(initialCode.c_str());
        const int lines = (int)m_pEditor->SendMessageToEngine(EM_GETLINECOUNT, 0, 0);
        m_notes = codesnippet::SanitizeCodeNotes(initialNotes, lines);
        m_notesText = EditorText();
        m_hasDeletedNote = false;
        ShowUndoLink(false);
        ApplyNotes();
        //有批注时按用户上次的选择展开批注栏（没有登录态时展开），没有批注时收起
        if (!m_notes.empty())
        {
            const CUserConfig* config = CurrentUserConfig();
            SetNotesPaneOpen(config == NULL || config->IsCodeNotesPaneOpen() != FALSE, false);
        }
        else
            SetNotesPaneOpen(false, false);
    }
    else if (!existed)
        m_notesText = EditorText();
    UpdateGutterWidth();

    //新窗口在批注栏展开、窗口随之加宽之后再居中
    if (!existed)
    {
        if (owner != NULL && ::IsWindow(owner))
            CenterWindow(owner);
        else
            CenterWindow(::GetDesktopWindow());
    }
    ShowWindow(::IsIconic(m_hWnd) ? SW_RESTORE : SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    if (m_pEditor != NULL)
        SetDuiFocus(m_pEditor);
    RefreshStatus();
    LOG_INFO("code compose opened: reused=%d, initial chars=%u, lang=%s, notes=%u.", existed ? 1 : 0,
             (unsigned)initialCode.size(), initialLangId.c_str(), (unsigned)m_notes.size());
    return true;
}

void CCodeComposeWindow::Close()
{
    if (IsWindow())
        DestroyWindow();
}

void CCodeComposeWindow::BuildClient()
{
    SetClientContent(BuildCodeComposeBody());
    DuiControl* root = GetClientContent();
    if (root == NULL)
        return;
    m_pLanguage = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kCodeComposeIdLanguage));
    m_pStatus = dynamic_cast<DuiLabel*>(root->FindCtrlById(kCodeComposeIdStatus));
    m_pEditor = dynamic_cast<DuiRichEdit*>(root->FindCtrlById(kCodeComposeIdEditor));
    m_pCodeEdit = dynamic_cast<CodeComposeEdit*>(m_pEditor);
    m_pSend = dynamic_cast<DuiButton*>(root->FindCtrlById(kCodeComposeIdSend));
    m_pLineNumbers = dynamic_cast<DuiButton*>(root->FindCtrlById(kCodeComposeIdLineNumbers));
    m_pGutter = dynamic_cast<CodeLineGutter*>(root->FindCtrlById(kCodeComposeIdGutter));
    m_pNotesButton = dynamic_cast<DuiButton*>(root->FindCtrlById(kCodeComposeIdNotes));
    m_pUndoLink = dynamic_cast<DuiLabel*>(root->FindCtrlById(kCodeComposeIdUndoNote));
    m_pSplit = dynamic_cast<CodeNotesSplitter*>(root->FindCtrlById(kCodeComposeIdSplit));
    m_pNotesPane = dynamic_cast<CodeNotesPane*>(root->FindCtrlById(kCodeComposeIdNotesPane));
    //选区变化时投给自己，留到消息里高亮光标所在行的批注
    if (m_pCodeEdit != NULL)
        m_pCodeEdit->SetSelChangeMessage(kMsgCodeComposeDeferred, kCodeComposeDeferSelChanged);

    //语言下拉框：第 0 项「自动识别」，第 i 项为语言 i - 1
    if (m_pLanguage != NULL)
    {
        m_pLanguage->AddString(TR(L"自动识别"));
        for (int lang = 0; lang < codesnippet::code_lang_count; ++lang)
        {
            const wchar_t* name = codesnippet::CodeLanguageDisplayName(lang);
            m_pLanguage->AddString(lang == codesnippet::code_lang_plain ? TR(name) : name);
        }
        m_pLanguage->SetCurSel(0, false);
    }
    //编辑框用等宽字体，字号按窗口 DPI 换算
    if (m_pEditor != NULL)
    {
        //窗口关掉再打开时会重新搭控件树：上一个编辑框已随旧窗口销毁，旧字体在这里释放
        if (m_editorFont != NULL)
        {
            ::DeleteObject(m_editorFont);
            m_editorFont = NULL;
        }
        m_fontPx = ::MulDiv(kCodeComposeFontPx, GetDpi(), kBaseDpi);
        m_editorFont = ::CreateFontW(-m_fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, kEditorFontName);
        if (m_editorFont != NULL)
        {
            m_pEditor->SetDefaultFontFromHFONT(m_editorFont);
            //被批注的行是最后一行时，按这个行高确定浅黄底色的下边
            if (m_pCodeEdit != NULL)
                m_pCodeEdit->SetLineHeight(CodeFontLineHeight(m_fontPx));
        }
    }
    //行号栏与编辑框同一字号；是否显示取用户配置（没有登录态时显示）
    if (m_pGutter != NULL)
        m_pGutter->SetFontPx(m_fontPx);
    const CUserConfig* config = CurrentUserConfig();
    ApplyLineNumbers(config == NULL || config->IsCodeComposeLineNumbers() != FALSE);
    UpdateGutterWidth();
}

void CCodeComposeWindow::ApplyLineNumbers(bool show)
{
    if (m_pLineNumbers != NULL && m_pLineNumbers->IsChecked() != show)
        m_pLineNumbers->SetCheck(show, false);
    if (m_pGutter == NULL || m_pGutter->IsVisible() == show)
        return;
    m_pGutter->SetVisible(show);
    DuiControl* parent = m_pGutter->GetParent();
    if (parent != NULL)
        parent->ForceLayout(parent->GetRect());
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

void CCodeComposeWindow::UpdateGutterWidth()
{
    if (m_pGutter == NULL || m_pEditor == NULL)
        return;
    DuiLayout* parent = dynamic_cast<DuiLayout*>(m_pGutter->GetParent());
    if (parent == NULL)
        return;
    const int lines = (int)m_pEditor->SendMessageToEngine(EM_GETLINECOUNT, 0, 0);
    const int width = CodeComposeGutterWidth(lines, m_fontPx);
    DuiLayout::Hint hint = parent->GetHint(m_pGutter);
    if (hint.fixedMain == width)
        return;
    hint.fixedMain = width;
    parent->SetHint(m_pGutter, hint);
    parent->ForceLayout(parent->GetRect());
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

std::wstring CCodeComposeWindow::EditorText() const
{
    if (m_pEditor == NULL)
        return std::wstring();
    return std::wstring((LPCWSTR)m_pEditor->GetText());
}

int CCodeComposeWindow::ChosenLanguage(const std::wstring& code, bool& autoDetect) const
{
    const int index = (m_pLanguage != NULL) ? m_pLanguage->GetCurSel() : 0;
    autoDetect = (index <= 0 || index > codesnippet::code_lang_count);
    if (autoDetect)
        return codesnippet::DetectCodeLanguage(code);
    return index - 1;
}

void CCodeComposeWindow::RefreshStatus()
{
    const std::wstring code = balloonchatwnd::NormalizeCodeNewlines(EditorText());
    bool autoDetect = true;
    const int lang = ChosenLanguage(code, autoDetect);
    //体积按不带引用计、计入批注；带引用时宿主发送前还会再查一次
    const bool tooLarge = !code.empty()
        && balloonchatwnd::IsCodeTooLarge(ToUtf8(code), codesnippet::CodeLanguageId(lang), net::ReplyQuote(),
                                          balloonchatwnd::CodeNotesToData(NotesForSending()));
    if (m_pStatus != NULL)
    {
        std::wstring status = CodeComposeStatusText(autoDetect, lang, CountLines(code), tooLarge);
        //格式化、删除批注等操作之后、内容没有再改动之前，状态文字显示操作的结果（超限的提示优先）
        if (!m_statusNote.empty())
        {
            if (EditorText() != m_statusNoteText)
            {
                m_statusNote.clear();
                m_statusNoteText.clear();
                //「撤销」只在「已删除 1 条批注」显示期间有效
                if (m_hasDeletedNote)
                {
                    m_hasDeletedNote = false;
                    ShowUndoLink(false);
                }
            }
            else if (!tooLarge)
                status = m_statusNote;
        }
        m_pStatus->SetText(status.c_str());
        m_pStatus->SetTextColor(tooLarge ? kStatusWarnColor : kStatusColor);
    }
    if (m_pSend != NULL)
        m_pSend->SetEnabled(!code.empty() && !tooLarge);
}

void CCodeComposeWindow::DoFormat()
{
    //编辑批注期间代码编辑框只读，不格式化（批注的行范围在编辑期间不能变）
    if (m_pEditor == NULL || (m_pNotesPane != NULL && m_pNotesPane->IsEditing()))
        return;
    const std::wstring text = EditorText();
    if (text.empty())
        return;
    bool autoDetect = true;
    const int lang = ChosenLanguage(balloonchatwnd::NormalizeCodeNewlines(text), autoDetect);

    //有选区时只处理它覆盖的整行，否则处理全文
    long selBegin = 0;
    long selEnd = 0;
    m_pEditor->GetSel(selBegin, selEnd);
    const bool selection = (selEnd > selBegin);
    long replaceBegin = 0;
    long replaceEnd = (long)text.size();
    codesnippet::CodeFormatResult result;
    if (selection)
    {
        ExpandSelectionToLines(text, selBegin, selEnd, replaceBegin, replaceEnd);
        result = codesnippet::FormatCodeSelection(
            text.substr((size_t)replaceBegin, (size_t)(replaceEnd - replaceBegin)), lang);
    }
    else
        result = codesnippet::FormatCode(text, lang);

    std::wstring note;
    if (result.m_jsonInvalid)
        note = TR(L"不是合法的 JSON，只做了基本整理");
    if (result.m_changedLines > 0)
    {
        //编辑框里的换行是 \r；用可撤销的替换写回，Ctrl+Z 一步撤回
        std::wstring replacement = result.m_text;
        for (size_t i = 0; i < replacement.size(); ++i)
        {
            if (replacement[i] == L'\n')
                replacement[i] = L'\r';
        }
        m_pEditor->SetSel(replaceBegin, replaceEnd);
        m_pEditor->ReplaceSel(replacement.c_str(), true);
        if (selection)
            m_pEditor->SetSel(replaceBegin, replaceBegin + (long)replacement.size());
        else
            m_pEditor->SetSel(0, 0);
        if (note.empty())
            note = TRF(L"已格式化，调整了 {0} 行", static_cast<unsigned int>(result.m_changedLines));
    }
    else if (note.empty())
        note = TR(L"没有需要调整的地方");
    //批注随格式化重新定位（格式化通常只改缩进，按行比较时忽略行首行尾空白，批注留在原来的行上）
    RemapNotes();
    m_statusNote = note;
    m_statusNoteText = EditorText();
    m_hasDeletedNote = false;
    ShowUndoLink(false);
    UpdateGutterWidth();
    KillTimer(kTimerStatus);
    RefreshStatus();
    LOG_INFO("code compose: formatted, lang=%s, selection=%d, changed lines=%d, json invalid=%d.",
             codesnippet::CodeLanguageId(lang), selection ? 1 : 0, result.m_changedLines, result.m_jsonInvalid ? 1 : 0);
}

void CCodeComposeWindow::DoSend()
{
    //还有没确定的批注：先确定它，随代码一起发出去
    if (m_pNotesPane != NULL && m_pNotesPane->IsEditing())
        ConfirmNoteEdit();
    const std::wstring code = balloonchatwnd::NormalizeCodeNewlines(EditorText());
    bool autoDetect = true;
    const int lang = ChosenLanguage(code, autoDetect);
    const std::vector<codesnippet::CodeNote> notes = NotesForSending();
    if (code.empty()
        || balloonchatwnd::IsCodeTooLarge(ToUtf8(code), codesnippet::CodeLanguageId(lang), net::ReplyQuote(),
                                          balloonchatwnd::CodeNotesToData(notes)))
    {
        RefreshStatus();
        return;
    }
    if (m_owner == NULL || !::IsWindow(m_owner))
    {
        LOG_WARNING("code compose: the owner window is gone, nothing sent.");
        return;
    }
    CodeComposeResult* result = new CodeComposeResult();
    result->m_code = code;
    result->m_lang = lang;
    result->m_manualLang = !autoDetect;
    result->m_notes = notes;
    //先投递关闭、再把结果交给宿主：两条消息在同一个线程的队列里按投递顺序处理。宿主没能发出（离线等）时会
    //重新打开本窗口并放回代码，若关闭排在结果之后，刚重新打开的窗口会随即被关掉
    PostMessage(WM_CLOSE);
    if (!::PostMessage(m_owner, kMsgCodeComposeSend, 0, (LPARAM)result))
    {
        delete result;
        LOG_WARNING("code compose: PostMessage to the owner failed, error %u.", (unsigned)::GetLastError());
        return;
    }
    LOG_INFO("code compose: handed %u chars to the chat window, lang=%s, manual=%d, notes=%u.", (unsigned)code.size(),
             codesnippet::CodeLanguageId(lang), autoDetect ? 0 : 1, (unsigned)notes.size());
}

std::vector<codesnippet::CodeNote> CCodeComposeWindow::NotesForSending() const
{
    //发送的代码去掉了首尾只含空白的行，批注随之平移、截断
    return codesnippet::CodeNotesForSending(m_notes, EditorText());
}

void CCodeComposeWindow::ApplyNotes()
{
    //批注栏：内容变了才重建卡片（重建会重置卡片列表的滚动位置）。编辑期间批注不会变，不重建，免得丢掉输入框里的文字
    if (m_pNotesPane != NULL && !m_pNotesPane->IsEditing() && !SameNotes(m_pNotesPane->Notes(), m_notes))
        m_pNotesPane->SetNotes(m_notes);
    //行号栏的橙色竖条与编辑框的浅黄底色：行号从 0 起
    std::vector<std::pair<int, int> > ranges;
    for (size_t i = 0; i < m_notes.size(); ++i)
        ranges.push_back(std::make_pair(m_notes[i].m_from - 1, m_notes[i].m_to - 1));
    if (m_pGutter != NULL)
        m_pGutter->SetMarkedRanges(ranges);
    if (m_pCodeEdit != NULL)
        m_pCodeEdit->SetMarkedRanges(ranges);
    UpdateNotesButton();
    ::InvalidateRect(m_hWnd, NULL, FALSE);
    //按光标所在的行重新高亮卡片：可能是在引擎的回调里被调用的，留到消息里读取选区
    PostMessage(kMsgCodeComposeDeferred, kCodeComposeDeferSelChanged);
}

void CCodeComposeWindow::UpdateNotesButton()
{
    const bool collapsed = (m_pSplit == NULL) || m_pSplit->IsCollapsed();
    const bool editing = (m_pNotesPane != NULL) && m_pNotesPane->IsEditing();
    const bool full = (int)m_notes.size() >= codesnippet::kCodeNotesMax;
    if (m_pNotesButton != NULL)
    {
        const std::wstring text = (collapsed && !m_notes.empty())
            ? TRF(L"批注 ({0})", static_cast<unsigned int>(m_notes.size()))
            : std::wstring(TR(L"批注"));
        m_pNotesButton->SetText(text.c_str());
        //批注栏收起且有批注时，点它展开；否则点它为选中的行添加批注，达到上限或正在编辑时不可用
        m_pNotesButton->SetEnabled((collapsed && !m_notes.empty()) || (!full && !editing));
    }
    if (m_pCodeEdit != NULL)
        m_pCodeEdit->SetAddNoteEnabled(!full && !editing);
    //编辑批注期间代码只读，「格式化」（按钮与右键菜单）不可用
    DuiControl* root = GetClientContent();
    DuiButton* format = (root != NULL) ? dynamic_cast<DuiButton*>(root->FindCtrlById(kCodeComposeIdFormat)) : NULL;
    if (format != NULL)
        format->SetEnabled(!editing);
    if (m_pCodeEdit != NULL)
        m_pCodeEdit->SetFormatEnabled(!editing);
}

void CCodeComposeWindow::RemapNotes()
{
    const std::wstring text = EditorText();
    if (text == m_notesText)
        return;
    if (m_notes.empty())
    {
        m_notesText = text;
        return;
    }
    int removed = 0;
    const size_t before = m_notes.size();
    m_notes = codesnippet::RemapCodeNotes(m_notes, m_notesText, text, removed);
    m_notesText = text;
    ApplyNotes();
    if (removed > 0)
    {
        m_statusNote = TRF(L"有 {0} 条批注所在的行已被删除", static_cast<unsigned int>(removed));
        m_statusNoteText = text;
        m_hasDeletedNote = false;
        ShowUndoLink(false);
        RefreshStatus();
        LOG_INFO("code compose: %d of %u notes removed with their lines.", removed, (unsigned)before);
    }
}

void CCodeComposeWindow::SelectedLines(int& from, int& to) const
{
    from = 1;
    to = 1;
    if (m_pEditor == NULL)
        return;
    long selBegin = 0;
    long selEnd = 0;
    m_pEditor->GetSel(selBegin, selEnd);
    const std::wstring text = EditorText();
    long lineBegin = 0;
    long lineEnd = 0;
    ExpandSelectionToLines(text, selBegin, selEnd, lineBegin, lineEnd);
    from = LineIndexOf(text, lineBegin) + 1;
    to = LineIndexOf(text, lineEnd) + 1;
}

void CCodeComposeWindow::BeginAddNote()
{
    if (m_pNotesPane == NULL || m_pEditor == NULL)
        return;
    //已经在编辑一条批注：把焦点交给它的输入框，不另起一条
    if (m_pNotesPane->IsEditing())
    {
        if (m_pNotesPane->EditBox() != NULL)
            SetDuiFocus(m_pNotesPane->EditBox());
        return;
    }
    if (TrimBlank(EditorText()).empty())
        return;
    int from = 1;
    int to = 1;
    SelectedLines(from, to);
    BeginAddNoteOnLines(from, to);
}

void CCodeComposeWindow::BeginAddNoteOnLines(int from, int to)
{
    if (m_pNotesPane == NULL || m_pEditor == NULL || m_pNotesPane->IsEditing())
        return;
    //选中的行已有批注：改为修改那一条
    for (size_t i = 0; i < m_notes.size(); ++i)
    {
        if (codesnippet::CodeNoteRangesOverlap(from, to, m_notes[i].m_from, m_notes[i].m_to))
        {
            BeginEditNote((int)i);
            return;
        }
    }
    if ((int)m_notes.size() >= codesnippet::kCodeNotesMax)
    {
        m_statusNote = TRF(L"每段代码最多 {0} 条批注", static_cast<unsigned int>(codesnippet::kCodeNotesMax));
        m_statusNoteText = EditorText();
        RefreshStatus();
        return;
    }
    SetNotesPaneOpen(true, true);
    m_pNotesPane->BeginEdit(from, to, std::wstring(), -1);
    EnterNoteEditing();
    LOG_INFO("code compose: adding a note on lines %d-%d.", from, to);
}

void CCodeComposeWindow::BeginEditNote(int index)
{
    if (m_pNotesPane == NULL || index < 0 || index >= (int)m_notes.size())
        return;
    //已经在编辑：先确定或取消那一条，不同时编辑两条
    if (m_pNotesPane->IsEditing())
    {
        if (m_pNotesPane->EditBox() != NULL)
            SetDuiFocus(m_pNotesPane->EditBox());
        return;
    }
    SetNotesPaneOpen(true, true);
    const codesnippet::CodeNote note = m_notes[(size_t)index];
    //说明里的换行在协议里是 \n，输入框里是 \r
    m_pNotesPane->BeginEdit(note.m_from, note.m_to, ReplaceNewlines(note.m_text, L'\n', L'\r'), index);
    EnterNoteEditing();
}

void CCodeComposeWindow::EnterNoteEditing()
{
    //编辑批注期间代码编辑框只读：批注的行范围在编辑期间不会变
    if (m_pEditor != NULL)
        m_pEditor->SetReadOnly(true);
    UpdateNotesButton();
    if (m_pNotesPane != NULL)
    {
        //编辑卡片在列表里对应的批注下标就是 EditingIndex（新增时为 -1）
        m_pNotesPane->ScrollToNote(m_pNotesPane->EditingIndex());
        if (m_pNotesPane->EditBox() != NULL)
            SetDuiFocus(m_pNotesPane->EditBox());
    }
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

void CCodeComposeWindow::LeaveNoteEditing()
{
    if (m_pEditor != NULL)
    {
        m_pEditor->SetReadOnly(false);
        SetDuiFocus(m_pEditor);
    }
    UpdateNotesButton();
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

void CCodeComposeWindow::ConfirmNoteEdit()
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
    if (index >= 0 && index < (int)m_notes.size())
        m_notes[(size_t)index].m_text = text;
    else
        m_notes.push_back(codesnippet::CodeNote(from, to, text));
    const int lines = (m_pEditor != NULL) ? (int)m_pEditor->SendMessageToEngine(EM_GETLINECOUNT, 0, 0) : 0;
    m_notes = codesnippet::SanitizeCodeNotes(m_notes, lines);
    m_notesText = EditorText();
    m_hasDeletedNote = false;
    ShowUndoLink(false);
    ApplyNotes();
    RefreshStatus();
    LOG_INFO("code compose: note %s on lines %d-%d, %u chars, %u notes in total.", index >= 0 ? "edited" : "added",
             from, to, (unsigned)text.size(), (unsigned)m_notes.size());
}

void CCodeComposeWindow::CancelNoteEdit()
{
    if (m_pNotesPane == NULL || !m_pNotesPane->IsEditing())
        return;
    m_pNotesPane->EndEdit();
    LeaveNoteEditing();
}

void CCodeComposeWindow::DeleteNote(int index)
{
    if (index < 0 || index >= (int)m_notes.size())
        return;
    //编辑期间不删除别的批注（批注栏在编辑期间不重建）：先把焦点交回输入框
    if (m_pNotesPane != NULL && m_pNotesPane->IsEditing())
    {
        if (m_pNotesPane->EditBox() != NULL)
            SetDuiFocus(m_pNotesPane->EditBox());
        return;
    }
    m_deletedNote = m_notes[(size_t)index];
    m_hasDeletedNote = true;
    m_notes.erase(m_notes.begin() + index);
    ApplyNotes();
    m_statusNote = TR(L"已删除 1 条批注");
    m_statusNoteText = EditorText();
    ShowUndoLink(true);
    RefreshStatus();
    LOG_INFO("code compose: note on lines %d-%d deleted, %u left.", m_deletedNote.m_from, m_deletedNote.m_to,
             (unsigned)m_notes.size());
}

void CCodeComposeWindow::UndoDeleteNote()
{
    if (!m_hasDeletedNote)
        return;
    //编辑期间批注栏不重建：先把焦点交回输入框，确定或取消之后再撤销
    if (m_pNotesPane != NULL && m_pNotesPane->IsEditing())
    {
        if (m_pNotesPane->EditBox() != NULL)
            SetDuiFocus(m_pNotesPane->EditBox());
        return;
    }
    m_hasDeletedNote = false;
    ShowUndoLink(false);
    //删除之后又在这几行加了批注，或批注已达上限：无法恢复
    bool conflict = (int)m_notes.size() >= codesnippet::kCodeNotesMax;
    for (size_t i = 0; i < m_notes.size() && !conflict; ++i)
    {
        if (codesnippet::CodeNoteRangesOverlap(m_deletedNote.m_from, m_deletedNote.m_to, m_notes[i].m_from, m_notes[i].m_to))
            conflict = true;
    }
    if (conflict)
    {
        m_statusNote = TR(L"这几行已有别的批注，无法恢复");
        m_statusNoteText = EditorText();
        RefreshStatus();
        return;
    }
    m_notes.push_back(m_deletedNote);
    const int lines = (m_pEditor != NULL) ? (int)m_pEditor->SendMessageToEngine(EM_GETLINECOUNT, 0, 0) : 0;
    m_notes = codesnippet::SanitizeCodeNotes(m_notes, lines);
    ApplyNotes();
    m_statusNote.clear();
    m_statusNoteText.clear();
    RefreshStatus();
    LOG_INFO("code compose: deleted note on lines %d-%d restored.", m_deletedNote.m_from, m_deletedNote.m_to);
}

void CCodeComposeWindow::ShowUndoLink(bool show)
{
    if (m_pUndoLink == NULL || m_pUndoLink->IsVisible() == show)
        return;
    m_pUndoLink->SetVisible(show);
    //显隐变化不会自动重排（UI 约定第 5 条）：对所在的工具行重排
    DuiControl* parent = m_pUndoLink->GetParent();
    if (parent != NULL)
        parent->ForceLayout(parent->GetRect());
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

void CCodeComposeWindow::SelectNoteLines(int index)
{
    if (m_pEditor == NULL || index < 0 || index >= (int)m_notes.size())
        return;
    const codesnippet::CodeNote& note = m_notes[(size_t)index];
    const int lineCount = (int)m_pEditor->SendMessageToEngine(EM_GETLINECOUNT, 0, 0);
    if (note.m_from > lineCount)
        return;
    const int last = ((note.m_to < lineCount) ? note.m_to : lineCount) - 1;
    const long begin = (long)m_pEditor->SendMessageToEngine(EM_LINEINDEX, (WPARAM)(note.m_from - 1), 0);
    const long lastStart = (long)m_pEditor->SendMessageToEngine(EM_LINEINDEX, (WPARAM)last, 0);
    const long lastLength = (long)m_pEditor->SendMessageToEngine(EM_LINELENGTH, (WPARAM)lastStart, 0);
    //先把起始行滚到可见，再选中整段（长批注的末行随后也滚到可见）
    m_pEditor->SetSel(begin, begin);
    m_pEditor->SendMessageToEngine(EM_SCROLLCARET, 0, 0);
    m_pEditor->SetSel(begin, lastStart + lastLength);
    m_pEditor->SendMessageToEngine(EM_SCROLLCARET, 0, 0);
    SetDuiFocus(m_pEditor);
    if (m_pNotesPane != NULL)
    {
        m_pNotesPane->SetHighlight(index);
        m_pNotesPane->ScrollToNote(index);
    }
}

void CCodeComposeWindow::HighlightNoteAtCaret()
{
    if (m_pEditor == NULL || m_pNotesPane == NULL)
        return;
    long selBegin = 0;
    long selEnd = 0;
    m_pEditor->GetSel(selBegin, selEnd);
    //选区覆盖的行（从 1 起）；选区的结束位置在行首时那一行不算
    const int first = (int)m_pEditor->SendMessageToEngine(EM_EXLINEFROMCHAR, 0, selBegin) + 1;
    const int last = (int)m_pEditor->SendMessageToEngine(EM_EXLINEFROMCHAR, 0, (selEnd > selBegin) ? selEnd - 1 : selBegin) + 1;
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

void CCodeComposeWindow::SetNotesPaneOpen(bool open, bool saveConfig)
{
    if (m_pSplit == NULL)
        return;
    if (saveConfig)
    {
        CUserConfig* config = CurrentUserConfig();
        if (config != NULL)
            config->SetCodeNotesPaneOpen(open ? TRUE : FALSE);
    }
    if (m_pSplit->IsCollapsed() != open)
    {
        UpdateNotesButton();
        return;
    }
    if (!open)
        CancelNoteEdit();
    m_pSplit->SetCollapsed(!open);

    //窗口随批注栏加宽或缩回
    ResizeWindowForNotesPane(m_hWnd, open, m_pSplit->RightWidth() + m_pSplit->GetBarThickness(), m_paneGrownPx);
    //显隐变化不会自动重排（UI 约定第 5 条）：窗口尺寸没变时分栏的矩形也没变，要对它强制重排
    m_pSplit->ForceLayout(m_pSplit->GetRect());
    UpdateNotesButton();
    ::InvalidateRect(m_hWnd, NULL, FALSE);
    LOG_INFO("code compose: notes pane %s, window grown by %d px.", open ? "opened" : "closed", m_paneGrownPx);
}

LRESULT CCodeComposeWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //编辑框文字变了：行号栏宽度立即跟上（行数的位数可能变了），批注立即重新定位（只比较改动过的行，不费时），
    //状态文字延时刷新，连续输入时只刷新一次
    if (pNotify->code == DUIN_VALUECHANGED && pNotify->ctrlId == kCodeComposeIdEditor)
    {
        UpdateGutterWidth();
        RemapNotes();
        SetTimer(kTimerStatus, kStatusDelayMs, NULL);
        return 0;
    }
    //「行号」勾选框：显示或隐藏行号栏，并记进用户配置
    if (pNotify->code == DUIN_VALUECHANGED && pNotify->ctrlId == kCodeComposeIdLineNumbers)
    {
        const bool show = (m_pLineNumbers != NULL) && m_pLineNumbers->IsChecked();
        ApplyLineNumbers(show);
        CUserConfig* config = CurrentUserConfig();
        if (config != NULL)
            config->SetCodeComposeLineNumbers(show ? TRUE : FALSE);
        LOG_INFO("code compose: line numbers %s.", show ? "shown" : "hidden");
        return 0;
    }
    //编辑框右键菜单的「格式化」
    if (pNotify->code == (UINT)DuiRichEdit::DUIN_RICHTEXT_MENUCOMMAND && pNotify->ctrlId == kCodeComposeIdEditor
        && (UINT)pNotify->extra == kCodeComposeMenuFormat)
    {
        DoFormat();
        return 0;
    }
    //编辑框右键菜单的「添加批注…」
    if (pNotify->code == (UINT)DuiRichEdit::DUIN_RICHTEXT_MENUCOMMAND && pNotify->ctrlId == kCodeComposeIdEditor
        && (UINT)pNotify->extra == kCodeComposeMenuAddNote)
    {
        BeginAddNote();
        return 0;
    }
    //换了语言：立即刷新状态
    if (pNotify->code == DUIN_VALUECHANGED && pNotify->ctrlId == kCodeComposeIdLanguage)
    {
        RefreshStatus();
        return 0;
    }
    if (pNotify->code == DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        //「发送」：交给宿主聊天窗
        case kCodeComposeIdSend:
            DoSend();
            return 0;

        //「格式化」：处理选区或全文
        case kCodeComposeIdFormat:
            DoFormat();
            return 0;

        //「取消」：关闭窗口，不发送
        case kCodeComposeIdCancel:
            PostMessage(WM_CLOSE);
            return 0;

        //「批注」：批注栏收起且有批注时展开，否则为选中的行添加批注
        case kCodeComposeIdNotes:
            if (m_pSplit != NULL && m_pSplit->IsCollapsed() && !m_notes.empty())
                SetNotesPaneOpen(true, true);
            else
                BeginAddNote();
            return 0;

        //批注栏顶部的「»」：收起批注栏
        case kCodeNotesIdCollapse:
            SetNotesPaneOpen(false, true);
            return 0;

        //编辑中的卡片的「确定」：会重建卡片列表（按钮自己就在列表里），等点击通知处理完再做
        case kCodeNotesIdEditOk:
            PostMessage(kMsgCodeComposeDeferred, kCodeComposeDeferConfirmNote);
            return 0;

        //编辑中的卡片的「取消」：同上，延后执行
        case kCodeNotesIdEditCancel:
            PostMessage(kMsgCodeComposeDeferred, kCodeComposeDeferCancelNote);
            return 0;

        //删除批注之后的「撤销」
        case kCodeComposeIdUndoNote:
            UndoDeleteNote();
            return 0;

        //批注卡片上的「第 3～4 行」「修改」「删除」，其余控件的点击（含标题栏按钮）交给框架
        default:
        {
            int index = -1;
            int part = -1;
            if (!CodeNotesIdToNote(pNotify->ctrlId, index, part))
                break;
            //「第 x～y 行」只改卡片的高亮，可以直接做；「修改」「删除」会重建卡片列表（链接自己就在列表里），延后执行
            if (part == kCodeNotesPartRange)
                SelectNoteLines(index);
            else if (part == kCodeNotesPartEdit)
                PostMessage(kMsgCodeComposeDeferred, kCodeComposeDeferEditNote, (LPARAM)index);
            else if (part == kCodeNotesPartDelete)
                PostMessage(kMsgCodeComposeDeferred, kCodeComposeDeferDeleteNote, (LPARAM)index);
            return 0;
        }
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CCodeComposeWindow::OnKeyDownMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    switch (wParam)
    {
    //Tab：编辑框有焦点时不让 DuiHost 拿去切换焦点，随后的字符消息由编辑框插入制表符
    case VK_TAB:
        if (m_pEditor != NULL && m_pEditor->IsFocused())
            return 0;
        break;

    //Ctrl+Enter：批注输入框有焦点时确定这条批注，否则发送。都等这次按键的字符消息处理完再做，换行不会被插进去
    case VK_RETURN:
        if (ctrl)
        {
            const bool inNoteBox = (m_pNotesPane != NULL && m_pNotesPane->EditBox() != NULL
                                    && m_pNotesPane->EditBox()->IsFocused());
            m_swallowEnterChar = true;
            PostMessage(kMsgCodeComposeDeferred, inNoteBox ? kCodeComposeDeferConfirmNote : kCodeComposeDeferSend);
            return 0;
        }
        break;

    //Ctrl+Shift+F：格式化
    case 'F':
        if (ctrl && shift)
        {
            DoFormat();
            return 0;
        }
        break;

    //Esc：正在编辑批注时放弃这条批注，否则关闭窗口、不发送
    case VK_ESCAPE:
        if (m_pNotesPane != NULL && m_pNotesPane->IsEditing())
        {
            CancelNoteEdit();
            return 0;
        }
        PostMessage(WM_CLOSE);
        return 0;

    //其余按键：交给 DuiHost 转给焦点控件
    default:
        break;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CCodeComposeWindow::OnCharMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
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

LRESULT CCodeComposeWindow::OnDeferred(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    switch (wParam)
    {
    //按下 Ctrl+Enter 之后发送
    case kCodeComposeDeferSend:
        m_swallowEnterChar = false;
        DoSend();
        break;

    //批注输入框里按下 Ctrl+Enter、或点了编辑卡片的「确定」之后确定这条批注
    case kCodeComposeDeferConfirmNote:
        m_swallowEnterChar = false;
        ConfirmNoteEdit();
        break;

    //代码编辑框的选区变了，或批注变了：高亮光标所在行的批注
    case kCodeComposeDeferSelChanged:
        HighlightNoteAtCaret();
        break;

    //编辑卡片的「取消」
    case kCodeComposeDeferCancelNote:
        CancelNoteEdit();
        break;

    //卡片上的「修改」
    case kCodeComposeDeferEditNote:
        BeginEditNote((int)lParam);
        break;

    //卡片上的「删除」
    case kCodeComposeDeferDeleteNote:
        DeleteNote((int)lParam);
        break;

    //不认识的动作：忽略
    default:
        break;
    }
    return 0;
}

LRESULT CCodeComposeWindow::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //编辑框文字变化后的延时刷新
    if (wParam == kTimerStatus)
    {
        KillTimer(kTimerStatus);
        RefreshStatus();
        return 0;
    }
    //balloonui 宿主窗口自己的定时器：交给框架
    bHandled = FALSE;
    return 0;
}

LRESULT CCodeComposeWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    Close();
    return 0;
}

LRESULT CCodeComposeWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    KillTimer(kTimerStatus);
    //控件树随窗口销毁，借用的指针一并作废；编辑框字体在析构函数里释放
    m_pLanguage = NULL;
    m_pStatus = NULL;
    m_pEditor = NULL;
    m_pCodeEdit = NULL;
    m_pSend = NULL;
    m_pLineNumbers = NULL;
    m_pGutter = NULL;
    m_pNotesButton = NULL;
    m_pUndoLink = NULL;
    m_pSplit = NULL;
    m_pNotesPane = NULL;
    m_swallowEnterChar = false;
    bHandled = FALSE;
    return 0;
}

} // namespace ballooncode
