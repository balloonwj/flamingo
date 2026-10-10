/**
 *  ChatCodeMessage.cpp —— 聊天「代码片段」消息与界面之间的纯逻辑，见 ChatCodeMessage.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖，与同目录 ChatBubble.cpp 相同 ----
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

#include <commdlg.h>   // GetSaveFileNameW

#include <memory>

#include "ChatCodeMessage.h"
#include "ChatCodeBlock.h"                       // CodeTextForClipboard：写文件时换行统一为 CRLF
#include "../../Common/Code/CodeLexer.h"         // CodeLanguageFromId / CodeLanguageFileExtension
#include "../../Common/I18n/I18n.h"              // TR：默认文件名与过滤串说明
#include "../../Common/Log/IULog.h"
#include "../../Common/Util/FileDialogFilter.h"  // filedlg::MakeFilter
#include "../../net/MessageProtocolClient.h"     // net::text_format_code、textContentBytes
#include "../../net/Protocol.h"                  // MESSAGE_CONTENT_MAX_BYTES
#include "../../third-party/jsoncpp-1.9.0/json.h" // 读本地库 content 列里的 format 与 lang

namespace balloonchatwnd {

namespace {

// ``` 写法的围栏
const wchar_t* const kCodeFence = L"```";
const size_t kCodeFenceLen = 3;

// 摘要里第一行最多保留的字符数，超过时截断并加省略号
const size_t kCodeDigestMaxChars = 40;

// 去掉首尾的空格、Tab 与换行
std::wstring TrimBlank(const std::wstring& s)
{
    const wchar_t* const kBlank = L" \t\r\n";
    const size_t b = s.find_first_not_of(kBlank);
    if (b == std::wstring::npos)
        return std::wstring();
    const size_t e = s.find_last_not_of(kBlank);
    return s.substr(b, e - b + 1);
}

// 换行与 Tab 换成空格，摘要只占一行
std::wstring ToOneLine(const std::wstring& s)
{
    std::wstring out = s;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] == L'\r' || out[i] == L'\n' || out[i] == L'\t')
            out[i] = L' ';
    }
    return out;
}

// 过长时截断到 kCodeDigestMaxChars 个字并加省略号
std::wstring TruncateCodeDigest(const std::wstring& s)
{
    if (s.size() > kCodeDigestMaxChars)
        return s.substr(0, kCodeDigestMaxChars) + L"\x2026";
    return s;
}

// 换行统一为 LF
std::wstring ToLf(const std::wstring& s)
{
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == L'\r')
        {
            out += L'\n';
            if (i + 1 < s.size() && s[i + 1] == L'\n')
                ++i;
            continue;
        }
        out += s[i];
    }
    return out;
}

} // anonymous namespace

bool ParseCodeFence(const std::wstring& text, std::wstring& code, int& lang)
{
    const std::wstring s = TrimBlank(ToLf(text));
    if (s.compare(0, kCodeFenceLen, kCodeFence) != 0)
        return false;
    const size_t firstNl = s.find(L'\n');
    const size_t lastNl = s.rfind(L'\n');
    //至少要有开头一行、结尾一行，中间还要有内容
    if (firstNl == std::wstring::npos || lastNl == firstNl)
        return false;
    if (TrimBlank(s.substr(lastNl + 1)) != kCodeFence)
        return false;
    const std::wstring info = TrimBlank(s.substr(kCodeFenceLen, firstNl - kCodeFenceLen));
    if (info.find(L'`') != std::wstring::npos)
        return false;
    const std::wstring body = s.substr(firstNl + 1, lastNl - firstNl - 1);
    //中间还有以 ``` 开头的行：是几段代码块混着文字，不当作一整段代码
    size_t begin = 0;
    while (begin <= body.size())
    {
        size_t end = body.find(L'\n', begin);
        if (end == std::wstring::npos)
            end = body.size();
        if (TrimBlank(body.substr(begin, end - begin)).compare(0, kCodeFenceLen, kCodeFence) == 0)
            return false;
        begin = end + 1;
    }
    if (TrimBlank(body).empty())
        return false;

    code = body;
    if (info.empty())
    {
        lang = codesnippet::DetectCodeLanguage(code);
        return true;
    }
    //语言名只认 ASCII，含其它字符时按纯文本
    std::string id;
    for (size_t i = 0; i < info.size(); ++i)
    {
        if (info[i] >= 0x80)
        {
            id.clear();
            break;
        }
        id += static_cast<char>(info[i]);
    }
    lang = id.empty() ? codesnippet::code_lang_plain : codesnippet::CodeLanguageFromId(id);
    return true;
}

bool TryCodeFenceSend(const std::wstring& text, bool markdownMode, std::wstring& code, int& lang)
{
    //Markdown 模式下 ``` 是 Markdown 的代码块语法，整条照常按 Markdown 发出
    if (markdownMode)
        return false;
    return ParseCodeFence(text, code, lang);
}

bool ShouldOfferCodePaste(bool inputWasEmpty, bool markdownMode, const std::wstring& inputText)
{
    //输入框里原本就有内容时，粘进来的只是其中一部分，不提示；Markdown 模式下 ``` 代码块已能表达代码
    if (!inputWasEmpty || markdownMode)
        return false;
    return codesnippet::LooksLikeCode(ToLf(inputText));
}

namespace {

//content 里文本格式的键名，与 MessageProtocolClient.cpp 的 kTextFormatKey 相同
const char* const kContentFormatKey = "format";

//UTF-8 转 UTF-16；空串或转换失败时返回空串
std::wstring Utf8ToWideText(const std::string& utf8)
{
    if (utf8.empty())
        return std::wstring();
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), NULL, 0);
    if (len <= 0)
        return std::wstring();
    std::wstring out(len, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &out[0], len);
    return out;
}

//UTF-16 转 UTF-8；空串或转换失败时返回空串
std::string WideToUtf8Text(const std::wstring& wide)
{
    if (wide.empty())
        return std::string();
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), NULL, 0, NULL, NULL);
    if (len <= 0)
        return std::string();
    std::string out(len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), &out[0], len, NULL, NULL);
    return out;
}

} // anonymous namespace

std::string CodeDigestFromUtf8(const std::string& utf8Code, const std::vector<net::CodeNoteData>& notes)
{
    return WideToUtf8Text(CodeDigestFromCode(Utf8ToWideText(utf8Code), CodeNotesFromData(notes, 0)));
}

bool CodeLangFromContentJson(int msgtype, const std::string& contentJson, int& lang,
                             std::vector<codesnippet::CodeNote>* notes, long long senderId)
{
    if (msgtype != ::message_type_text || contentJson.empty())
        return false;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    std::string errors;
    if (!reader->parse(contentJson.data(), contentJson.data() + contentJson.size(), &root, &errors)
        || !root.isObject())
        return false;
    const Json::Value& format = root[kContentFormatKey];
    if (!format.isString() || format.asString() != net::kTextFormatCodeValue)
        return false;
    const Json::Value& id = root[net::kCodeLangKey];
    lang = codesnippet::CodeLanguageFromId(id.isString() ? id.asString() : std::string());
    //批注：按 content.text 的行数整理
    if (notes != NULL)
    {
        const Json::Value& text = root["text"];
        const int lines = text.isString() ? CodeLineCount(Utf8ToWideText(text.asString())) : 0;
        *notes = CodeNotesFromData(net::MessageProtocolClient::readCodeNotesFromContent(root), lines, senderId);
    }
    return true;
}

int CodeLineCount(const std::wstring& code)
{
    const std::wstring normalized = NormalizeCodeNewlines(code);
    if (normalized.empty())
        return 0;
    int lines = 1;
    for (size_t i = 0; i < normalized.size(); ++i)
    {
        if (normalized[i] == L'\n')
            ++lines;
    }
    return lines;
}

std::vector<codesnippet::CodeNote> CodeNotesFromData(const std::vector<net::CodeNoteData>& data, int lineCount,
                                                     long long senderId)
{
    std::vector<codesnippet::CodeNote> notes;
    for (size_t i = 0; i < data.size(); ++i)
    {
        //协议里没写作者的，作者就是这条消息的发送者
        const long long author = (data[i].m_userId != 0) ? (long long)data[i].m_userId : senderId;
        notes.push_back(codesnippet::CodeNote(data[i].m_from, data[i].m_to, Utf8ToWideText(data[i].m_text),
                                              author, data[i].m_isNew));
    }
    return codesnippet::SanitizeCodeNotes(notes, lineCount);
}

std::vector<net::CodeNoteData> CodeNotesToData(const std::vector<codesnippet::CodeNote>& notes, long long senderId)
{
    std::vector<net::CodeNoteData> data;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        net::CodeNoteData item;
        item.m_from = notes[i].m_from;
        item.m_to = notes[i].m_to;
        item.m_text = WideToUtf8Text(notes[i].m_text);
        //作者是发送者本人时缺省不写，第 10 节已有的发送格式因此不变
        if (notes[i].m_authorId != 0 && notes[i].m_authorId != senderId)
            item.m_userId = notes[i].m_authorId;
        item.m_isNew = notes[i].m_isNew;
        data.push_back(item);
    }
    return data;
}

std::wstring CodeDigestFromCode(const std::wstring& code, const std::vector<codesnippet::CodeNote>& notes)
{
    //追加批注的消息：「[批注] 」加按行号排在最前的那条新批注的说明（整理之后按起始行排序）
    const std::vector<codesnippet::CodeNote> sorted = codesnippet::SanitizeCodeNotes(notes, 0);
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        if (!sorted[i].m_isNew)
            continue;
        const std::wstring text = TrimBlank(ToOneLine(sorted[i].m_text));
        if (text.empty())
            continue;
        std::wstring digest = TR(L"[批注]");
        digest += L" ";
        digest += TruncateCodeDigest(text);
        return digest;
    }

    std::wstring digest = TR(L"[代码]");
    const std::wstring s = ToLf(code);
    size_t begin = 0;
    while (begin < s.size())
    {
        size_t end = s.find(L'\n', begin);
        if (end == std::wstring::npos)
            end = s.size();
        std::wstring line = TrimBlank(s.substr(begin, end - begin));
        if (!line.empty())
        {
            digest += L" ";
            digest += TruncateCodeDigest(line);
            break;
        }
        begin = end + 1;
    }
    return digest;
}

bool IsCodeTooLarge(const std::string& codeUtf8, const std::string& langId, const net::ReplyQuote& quote,
                    const std::vector<net::CodeNoteData>& notes)
{
    const size_t bytes = net::MessageProtocolClient::textContentBytes(
        codeUtf8, net::TextFont(), quote, std::vector<MentionSpan>(), false, net::text_format_code, langId, notes);
    return bytes > MESSAGE_CONTENT_MAX_BYTES;
}

void ApplyCodeFields(ChatMessage& msg, int textFormat, const std::string& codeLang,
                     const std::vector<net::CodeNoteData>& notes, long long senderId)
{
    msg.isCode = (textFormat == net::text_format_code);
    msg.codeLang = msg.isCode ? codesnippet::CodeLanguageFromId(codeLang) : codesnippet::code_lang_plain;
    msg.codeNotes.clear();
    if (msg.isCode && !notes.empty())
        msg.codeNotes = CodeNotesFromData(notes, CodeLineCount(std::wstring((LPCWSTR)msg.text)), senderId);
}

bool CanAnnotateCodeMessage(const ChatMessage& msg)
{
    return msg.isCode && msg.msgid > 0;
}

std::vector<codesnippet::CodeNote> BaselineNotesForReedit(const std::vector<codesnippet::CodeNote>& notes)
{
    std::vector<codesnippet::CodeNote> baseline;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (!notes[i].m_isNew)
            baseline.push_back(notes[i]);
    }
    return baseline;
}

std::wstring CodeSaveFileName(int lang)
{
    std::wstring name = TR(L"代码片段");
    name += L".";
    name += codesnippet::CodeLanguageFileExtension(lang);
    return name;
}

bool SaveCodeWithDialog(HWND owner, const std::wstring& code, int lang)
{
    //默认文件名写进路径缓冲区，对话框在其基础上让用户修改
    wchar_t path[MAX_PATH] = { 0 };
    const std::wstring defaultName = CodeSaveFileName(lang);
    wcsncpy_s(path, defaultName.c_str(), _TRUNCATE);

    const std::wstring ext = codesnippet::CodeLanguageFileExtension(lang);
    const std::wstring pattern = L"*." + ext;
    //过滤串由几段以空字符分隔的文字组成，须活到对话框关闭
    const std::wstring filter = filedlg::MakeFilter(TR(L"代码文件"), pattern.c_str(), TR(L"所有文件"), L"*.*");

    OPENFILENAMEW ofn;
    ::ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = ext.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!::GetSaveFileNameW(&ofn))
        return false;

    //UTF-8、不带 BOM、CRLF 换行：记事本与各种编辑器都能直接打开
    const std::wstring text = CodeTextForClipboard(code);
    std::string utf8;
    if (!text.empty())
    {
        const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
        if (bytes > 0)
        {
            utf8.resize((size_t)bytes);
            ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], bytes, NULL, NULL);
        }
    }
    HANDLE file = ::CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        LOG_ERROR("code snippet: save as failed to create the file, error %u.", (unsigned)::GetLastError());
        return false;
    }
    DWORD written = 0;
    const BOOL ok = utf8.empty() || ::WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, NULL);
    ::CloseHandle(file);
    if (!ok || written != (DWORD)utf8.size())
    {
        LOG_ERROR("code snippet: save as failed to write %u bytes, error %u.", (unsigned)utf8.size(),
                  (unsigned)::GetLastError());
        return false;
    }
    LOG_INFO("code snippet: saved %u bytes, lang=%s.", (unsigned)utf8.size(), codesnippet::CodeLanguageId(lang));
    return true;
}

} // namespace balloonchatwnd
