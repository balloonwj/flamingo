/**
 *  帖子群文字处理的实现，见 ThreadText.h。
 *  表情编码的解析与转义规则照 Source/UI/BalloonChat/BalloonChatMsgFormat.cpp 的 TryParseFaceTag /
 *  EscapeTextForSend / ParseSendBody，两边必须一致，否则帖子与聊天消息里同一段正文显示不同。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadText.h"

#include <windows.h>
#include <stdio.h>

namespace threadgroup {

namespace {

//「[表情名]」里名字的最大长度（字符）：表情库里最长的名字不超过 8 个字，留出余量；超过这个长度的方括号内容不当表情
const size_t kMaxFaceNameChars = 16;

//64 位整数的十进制文本缓冲（含负号与结尾 0）
const int kInt64TextChars = 32;

///f[" 前缀的长度
const size_t kFaceTagPrefixLen = 4;

//判断 c 是不是空白：半角空格、制表、回车、换行、全角空格
bool IsSpaceChar(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x3000;
}

//判断是不是 UTF-16 低位代理（代理对的后半个）
bool IsLowSurrogate(wchar_t c)
{
    return c >= 0xDC00 && c <= 0xDFFF;
}

/**
 *  尝试从 text[i] 起解析一个 /f["<整数>"] 表情标签。
 *  @param outId   [出参] 表情编号
 *  @param outNext [出参] 标签之后的位置
 *  @return 命中返回 true
 */
bool TryParseFaceTag(const std::wstring& text, size_t i, int& outId, size_t& outNext)
{
    const size_t n = text.size();
    if (i + 3 >= n)
        return false;
    if (text[i] != L'/' || text[i + 1] != L'f' || text[i + 2] != L'[' || text[i + 3] != L'"')
        return false;
    size_t j = i + kFaceTagPrefixLen;
    int id = 0;
    bool hasDigit = false;
    const int kDecimalBase = 10;
    while (j < n && text[j] >= L'0' && text[j] <= L'9')
    {
        id = id * kDecimalBase + static_cast<int>(text[j] - L'0');
        hasDigit = true;
        ++j;
    }
    //数字后须紧跟 "]
    if (!hasDigit || j + 1 >= n)
        return false;
    if (text[j] != L'"' || text[j + 1] != L']')
        return false;
    outId = id;
    outNext = j + 2;
    return true;
}

//把一个表情编号写成线上编码 /f["n"]
std::wstring FaceTag(int faceId)
{
    wchar_t buf[kInt64TextChars];
    swprintf_s(buf, kInt64TextChars, L"/f[\"%d\"]", faceId);
    return std::wstring(buf);
}

} // namespace

//======================== FaceTable ========================

void FaceTable::Add(int faceId, const std::wstring& name)
{
    if (name.empty())
        return;
    std::map<int, std::wstring>::iterator old = m_names.find(faceId);
    if (old != m_names.end())
        m_ids.erase(old->second);
    m_names[faceId] = name;
    m_ids[name] = faceId;
}

bool FaceTable::NameOf(int faceId, std::wstring& name) const
{
    std::map<int, std::wstring>::const_iterator it = m_names.find(faceId);
    if (it == m_names.end())
        return false;
    name = it->second;
    return true;
}

bool FaceTable::IdOf(const std::wstring& name, int& faceId) const
{
    std::map<std::wstring, int>::const_iterator it = m_ids.find(name);
    if (it == m_ids.end())
        return false;
    faceId = it->second;
    return true;
}

size_t FaceTable::Count() const
{
    return m_names.size();
}

//======================== 编码转换 ========================

std::wstring Utf8ToWide(const std::string& text)
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

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
    if (len <= 0)
        return std::string();
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], len, NULL, NULL);
    return out;
}

int CountChars(const std::wstring& text)
{
    int count = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        //代理对的后半个不单独计数
        if (!IsLowSurrogate(text[i]))
            ++count;
    }
    return count;
}

//======================== 表情 ========================

bool ParseFaceTagAt(const std::wstring& text, size_t i, int& faceId, size_t& next)
{
    return TryParseFaceTag(text, i, faceId, next);
}

std::vector<TextSegment> ParseWire(const std::wstring& wire)
{
    std::vector<TextSegment> segs;
    std::wstring cur;
    size_t i = 0;
    while (i < wire.size())
    {
        const wchar_t c = wire[i];
        if (c == L'/')
        {
            //「//」只有紧跟一个表情标签时才是转义，还原成一个字面的 '/'
            if (i + 1 < wire.size() && wire[i + 1] == L'/')
            {
                int escId = 0;
                size_t escNext = 0;
                if (TryParseFaceTag(wire, i + 1, escId, escNext))
                {
                    cur += L'/';
                    i += 2;
                    continue;
                }
            }
            int faceId = 0;
            size_t next = 0;
            if (TryParseFaceTag(wire, i, faceId, next))
            {
                if (!cur.empty())
                {
                    TextSegment text;
                    text.m_text = cur;
                    segs.push_back(text);
                    cur.clear();
                }
                TextSegment face;
                face.m_face = true;
                face.m_faceId = faceId;
                segs.push_back(face);
                i = next;
                continue;
            }
        }
        cur += c;
        ++i;
    }
    if (!cur.empty())
    {
        TextSegment text;
        text.m_text = cur;
        segs.push_back(text);
    }
    return segs;
}

std::wstring WireToDisplay(const std::wstring& wire, const FaceTable& faces, const std::wstring& unknownFace)
{
    const std::vector<TextSegment> segs = ParseWire(wire);
    std::wstring out;
    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (!segs[i].m_face)
        {
            out += segs[i].m_text;
            continue;
        }
        std::wstring name;
        if (faces.NameOf(segs[i].m_faceId, name))
            out += L"[" + name + L"]";
        else
            out += unknownFace;
    }
    return out;
}

std::wstring DisplayToWire(const std::wstring& display, const FaceTable& faces)
{
    std::wstring out;
    size_t i = 0;
    while (i < display.size())
    {
        const wchar_t c = display[i];
        //换行统一为 LF：RichEdit 取出的文字以 CR 表示换行，粘贴来的文字可能是 CRLF（与 ChatCodeMessage 的 ToLf 口径一致）
        if (c == L'\r')
        {
            out += L'\n';
            i += (i + 1 < display.size() && display[i + 1] == L'\n') ? 2 : 1;
            continue;
        }
        if (c == L'[')
        {
            const size_t close = display.find(L']', i + 1);
            if (close != std::wstring::npos && close > i + 1 && close - i - 1 <= kMaxFaceNameChars)
            {
                int faceId = 0;
                if (faces.IdOf(display.substr(i + 1, close - i - 1), faceId))
                {
                    out += FaceTag(faceId);
                    i = close + 1;
                    continue;
                }
            }
        }
        else if (c == L'/')
        {
            //这个位置起是一个能被解析出来的表情标签：补一个 '/'，接收方还原成字面的 '/'
            int faceId = 0;
            size_t next = 0;
            if (TryParseFaceTag(display, i, faceId, next))
                out += L'/';
        }
        out += c;
        ++i;
    }
    return out;
}

//======================== 摘要 ========================

bool IsBlank(const std::wstring& text)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (!IsSpaceChar(text[i]))
            return false;
    }
    return true;
}

std::wstring TruncateChars(const std::wstring& text, int maxChars, bool& cut)
{
    cut = false;
    int chars = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (IsLowSurrogate(text[i]))
            continue;
        if (chars == maxChars)
        {
            cut = true;
            return text.substr(0, i);
        }
        ++chars;
    }
    return text;
}

std::wstring MakeDigest(const std::wstring& text, int maxChars, bool& cut)
{
    //先把空白折成单个空格，去掉首尾空白，再截取
    std::wstring folded;
    folded.reserve(text.size());
    bool lastSpace = true;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (IsSpaceChar(text[i]))
        {
            if (!lastSpace)
                folded.push_back(L' ');
            lastSpace = true;
            continue;
        }
        folded.push_back(text[i]);
        lastSpace = false;
    }
    while (!folded.empty() && folded[folded.size() - 1] == L' ')
        folded.erase(folded.size() - 1);
    return TruncateChars(folded, maxChars, cut);
}

std::string IdToString(int64_t id)
{
    char buf[kInt64TextChars];
    snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(id));
    return std::string(buf);
}

} // namespace threadgroup
