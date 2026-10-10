/**
 *  GroupQaModel.cpp
 *
 *  群聊窗「问答」页签的纯逻辑，说明见 GroupQaModel.h 与 docs/群问答方案.md。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "GroupQaModel.h"

#include <set>

#include "../../net/Protocol.h"            // group_role_admin
#include "../../Common/I18n/I18n.h"        // TR / TRF：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

// 全角空格（U+3000）
const wchar_t kFullWidthSpace = 0x3000;
// UTF-16 代理对的范围：高位代理 D800~DBFF，低位代理 DC00~DFFF
const wchar_t kHighSurrogateFirst = 0xD800;
const wchar_t kHighSurrogateLast = 0xDBFF;
// 第一个非 ASCII 字符：网址只认 ASCII，遇到中文字符、中文标点即结束
const wchar_t kFirstNonAscii = 0x80;

// 两种认可的网址前缀
const wchar_t* const kUrlPrefixes[] = { L"https://", L"http://" };

bool IsBlank(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == kFullWidthSpace;
}

wchar_t LowerAscii(wchar_t c)
{
    if (c >= L'A' && c <= L'Z')
        return static_cast<wchar_t>(c - L'A' + L'a');
    return c;
}

std::wstring LowerAsciiString(const std::wstring& s)
{
    std::wstring out(s);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = LowerAscii(out[i]);
    return out;
}

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

// 网址里允许出现的字符：ASCII 可见字符，去掉引号、尖括号与反引号
bool IsUrlChar(wchar_t c)
{
    if (c <= L' ' || c >= kFirstNonAscii)
        return false;
    return c != L'"' && c != L'\'' && c != L'<' && c != L'>' && c != L'`';
}

// 网址末尾不算在网址里的英文句末标点
bool IsTrailingPunct(wchar_t c)
{
    return c == L'.' || c == L',' || c == L';' || c == L':' || c == L'!' || c == L'?' || c == L')' || c == L']';
}

// 从 pos 开始是否是某个认可的网址前缀（不区分大小写）；是则返回前缀长度，否则返回 0
size_t MatchUrlPrefix(const std::wstring& text, size_t pos)
{
    for (size_t k = 0; k < sizeof(kUrlPrefixes) / sizeof(kUrlPrefixes[0]); ++k)
    {
        const std::wstring prefix(kUrlPrefixes[k]);
        if (pos + prefix.size() > text.size())
            continue;
        bool same = true;
        for (size_t i = 0; i < prefix.size() && same; ++i)
        {
            if (LowerAscii(text[pos + i]) != prefix[i])
                same = false;
        }
        if (same)
            return prefix.size();
    }
    return 0;
}

} // anonymous namespace

std::wstring TrimQaText(const std::wstring& text)
{
    size_t b = 0;
    size_t e = text.size();
    while (b < e && IsBlank(text[b]))
        ++b;
    while (e > b && IsBlank(text[e - 1]))
        --e;
    return text.substr(b, e - b);
}

size_t CountQaChars(const std::wstring& text)
{
    size_t count = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        //高位代理与紧随的低位代理合起来算一个字符
        if (text[i] >= kHighSurrogateFirst && text[i] <= kHighSurrogateLast && i + 1 < text.size())
            ++i;
        ++count;
    }
    return count;
}

std::vector<size_t> FilterGroupQa(const std::vector<net::GroupQaItem>& items, const std::wstring& keyword)
{
    std::vector<size_t> out;
    const std::wstring key = LowerAsciiString(TrimQaText(keyword));
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (key.empty())
        {
            out.push_back(i);
            continue;
        }
        const std::wstring q = LowerAsciiString(Utf8ToWide(items[i].m_question));
        const std::wstring a = LowerAsciiString(Utf8ToWide(items[i].m_answer));
        if (q.find(key) != std::wstring::npos || a.find(key) != std::wstring::npos)
            out.push_back(i);
    }
    return out;
}

std::vector<std::wstring> ExtractQaUrls(const std::wstring& text, size_t maxCount)
{
    std::vector<std::wstring> out;
    std::set<std::wstring> seen;
    size_t pos = 0;
    while (pos < text.size() && out.size() < maxCount)
    {
        const size_t prefixLen = MatchUrlPrefix(text, pos);
        if (prefixLen == 0)
        {
            ++pos;
            continue;
        }
        size_t end = pos + prefixLen;
        while (end < text.size() && IsUrlChar(text[end]))
            ++end;
        size_t trimmedEnd = end;
        while (trimmedEnd > pos + prefixLen && IsTrailingPunct(text[trimmedEnd - 1]))
            --trimmedEnd;
        //只有前缀、后面没有主机名的不算
        if (trimmedEnd > pos + prefixLen)
        {
            const std::wstring url = text.substr(pos, trimmedEnd - pos);
            if (seen.insert(url).second)
                out.push_back(url);
        }
        pos = end;
    }
    return out;
}

std::wstring BuildQaSendText(const std::wstring& question, const std::wstring& answer)
{
    return TRF(L"问：{0}\n答：{1}", TrimQaText(question), TrimQaText(answer));
}

int CheckQaInput(const std::wstring& question, const std::wstring& answer)
{
    const std::wstring q = TrimQaText(question);
    const std::wstring a = TrimQaText(answer);
    if (q.empty())
        return kQaInputEmptyQuestion;
    if (a.empty())
        return kQaInputEmptyAnswer;
    if (CountQaChars(q) > kQaQuestionMaxChars)
        return kQaInputQuestionTooLong;
    if (CountQaChars(a) > kQaAnswerMaxChars)
        return kQaInputAnswerTooLong;
    return kQaInputOk;
}

std::wstring QaInputCheckText(int check)
{
    switch (check)
    {
    //问题没填
    case kQaInputEmptyQuestion:
        return TR(L"请填写问题");
    //答案没填
    case kQaInputEmptyAnswer:
        return TR(L"请填写答案");
    //问题太长
    case kQaInputQuestionTooLong:
        return TRF(L"问题不能超过 {0} 个字", static_cast<int>(kQaQuestionMaxChars));
    //答案太长
    case kQaInputAnswerTooLong:
        return TRF(L"答案不能超过 {0} 个字", static_cast<int>(kQaAnswerMaxChars));
    //可以提交：没有提示
    default:
        return std::wstring();
    }
}

unsigned int QaCardCtrlId(int index, int part)
{
    return kQaCardIdBase + static_cast<unsigned int>(index) * kQaCardIdStride + static_cast<unsigned int>(part);
}

bool QaCardFromCtrlId(unsigned int ctrlId, int& index, int& part)
{
    const unsigned int end = kQaCardIdBase + static_cast<unsigned int>(kQaCardMaxCount) * kQaCardIdStride;
    if (ctrlId < kQaCardIdBase || ctrlId >= end)
        return false;
    index = static_cast<int>((ctrlId - kQaCardIdBase) / kQaCardIdStride);
    part = static_cast<int>((ctrlId - kQaCardIdBase) % kQaCardIdStride);
    return true;
}

bool CanEditQaItem(int myRole, int source)
{
    return CanAddQa(myRole) && source == net::kGroupQaSourceGroup;
}

bool CanAddQa(int myRole)
{
    return myRole >= group_role_admin;
}

std::wstring QaEmptyHintText(int state, size_t totalCount, size_t shownCount, bool hasKeyword)
{
    if (state == kQaPanelLoading)
        return TR(L"正在加载问答…");
    if (state == kQaPanelFailed)
        return TR(L"问答没有加载出来，请稍后再打开这个页签");
    if (totalCount == 0)
        return TR(L"本群还没有问答");
    if (shownCount == 0 && hasKeyword)
        return TR(L"没有找到匹配的问答");
    return std::wstring();
}

} // namespace balloonchatwnd
