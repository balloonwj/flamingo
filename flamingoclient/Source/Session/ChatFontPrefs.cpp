/**
 *  ChatFontPrefs.cpp
 *
 *  聊天字体偏好纯数据模型的实现，说明见 ChatFontPrefs.h。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "ChatFontPrefs.h"

#include <stdlib.h>   // _tcstoui64 / _tcstoi64
#include <tchar.h>

namespace chatfont {

namespace {

// 已设置的字体编码后的字段数：是否设置、字号、粗体、斜体、下划线、删除线、颜色。
const int kFieldCount = 7;

// 字号上下限（像素），与「AF」字体浮层一致。
const int kSizeMin = 8;
const int kSizeMax = 72;

// 颜色只取低 24 位（0xRRGGBB）。
const uint32_t kColorMask = 0xFFFFFFu;

// 会话键里会话类型与会话ID之间的分隔符。
const TCHAR kKeySeparator = _T('_');

// 把一段只含十进制数字的文本转成无符号整数；含其它字符或为空时返回 false。
bool ParseUnsigned(const CString& text, unsigned long long& out)
{
    if (text.IsEmpty())
        return false;
    for (int i = 0; i < text.GetLength(); ++i)
    {
        if (text[i] < _T('0') || text[i] > _T('9'))
            return false;
    }
    out = _tcstoui64(text, NULL, 10);
    return true;
}

// 拼出会话键"会话类型_会话ID"。
CString MakeKey(int ctype, int64_t cid)
{
    CString key;
    key.Format(_T("%d%c%I64d"), ctype, kKeySeparator, (long long)cid);
    return key;
}

// 解析会话键；会话类型或会话ID 不是正整数时返回 false。
bool ParseKey(const CString& key, int& ctype, int64_t& cid)
{
    const int sep = key.Find(kKeySeparator);
    if (sep <= 0)
        return false;

    unsigned long long type = 0;
    unsigned long long id = 0;
    if (!ParseUnsigned(key.Left(sep), type) || !ParseUnsigned(key.Mid(sep + 1), id))
        return false;
    if (type == 0 || id == 0)
        return false;

    ctype = (int)type;
    cid   = (int64_t)id;
    return true;
}

} // anonymous namespace

CString EncodeFont(const net::TextFont& font)
{
    if (!font.m_valid)
        return CString(_T("0"));

    CString text;
    text.Format(_T("1,%d,%d,%d,%d,%d,%u"),
                font.m_size,
                font.m_bold ? 1 : 0,
                font.m_italic ? 1 : 0,
                font.m_underline ? 1 : 0,
                font.m_strike ? 1 : 0,
                (unsigned int)(font.m_colorRgb & kColorMask));
    return text;
}

bool DecodeFont(LPCTSTR text, net::TextFont& out)
{
    const CString all = (text != NULL) ? text : _T("");
    if (all == _T("0"))
    {
        out = net::TextFont();   //未设置：各项回到默认值，m_valid 为 false
        return true;
    }

    //按逗号拆成字段，字段数必须恰好是 kFieldCount，每个字段都必须是非负整数
    std::vector<unsigned long long> fields;
    int start = 0;
    while (true)
    {
        const int comma = all.Find(_T(','), start);
        const CString part = (comma < 0) ? all.Mid(start) : all.Mid(start, comma - start);
        unsigned long long value = 0;
        if (!ParseUnsigned(part, value))
            return false;
        fields.push_back(value);
        if (comma < 0)
            break;
        start = comma + 1;
    }
    if ((int)fields.size() != kFieldCount || fields[0] != 1)
        return false;

    net::TextFont f;
    f.m_valid = true;
    long long size = (long long)fields[1];
    if (size < kSizeMin)
        size = kSizeMin;
    if (size > kSizeMax)
        size = kSizeMax;
    f.m_size      = (int)size;
    f.m_bold      = (fields[2] != 0);
    f.m_italic    = (fields[3] != 0);
    f.m_underline = (fields[4] != 0);
    f.m_strike    = (fields[5] != 0);
    f.m_colorRgb  = (uint32_t)(fields[6] & kColorMask);
    out = f;
    return true;
}

const net::TextFont& ChatFontPrefs::GetDefault() const
{
    return m_default;
}

void ChatFontPrefs::SetDefault(const net::TextFont& font)
{
    m_default = font.m_valid ? font : net::TextFont();
}

bool ChatFontPrefs::HasOverride(int ctype, int64_t cid) const
{
    return m_overrides.count(std::make_pair(ctype, cid)) != 0;
}

bool ChatFontPrefs::GetOverride(int ctype, int64_t cid, net::TextFont& out) const
{
    std::map<std::pair<int, int64_t>, net::TextFont>::const_iterator it =
        m_overrides.find(std::make_pair(ctype, cid));
    if (it == m_overrides.end())
        return false;
    out = it->second;
    return true;
}

void ChatFontPrefs::SetOverride(int ctype, int64_t cid, const net::TextFont& font)
{
    if (!font.m_valid)
    {
        RemoveOverride(ctype, cid);
        return;
    }
    m_overrides[std::make_pair(ctype, cid)] = font;
}

bool ChatFontPrefs::RemoveOverride(int ctype, int64_t cid)
{
    return m_overrides.erase(std::make_pair(ctype, cid)) != 0;
}

net::TextFont ChatFontPrefs::Resolve(int ctype, int64_t cid) const
{
    net::TextFont font;
    if (GetOverride(ctype, cid, font))
        return font;
    return m_default;
}

void ChatFontPrefs::ExportOverrides(std::vector<std::pair<CString, CString> >& out) const
{
    out.clear();
    //std::map 按 (会话类型, 会话ID) 升序遍历，导出顺序因而稳定
    for (std::map<std::pair<int, int64_t>, net::TextFont>::const_iterator it = m_overrides.begin();
         it != m_overrides.end(); ++it)
    {
        out.push_back(std::make_pair(MakeKey(it->first.first, it->first.second),
                                     EncodeFont(it->second)));
    }
}

void ChatFontPrefs::ImportOverrides(const std::vector<std::pair<CString, CString> >& entries)
{
    m_overrides.clear();
    for (size_t i = 0; i < entries.size(); ++i)
    {
        int ctype = 0;
        int64_t cid = 0;
        net::TextFont font;
        if (!ParseKey(entries[i].first, ctype, cid) || !DecodeFont(entries[i].second, font))
            continue;
        if (!font.m_valid)
            continue;
        m_overrides[std::make_pair(ctype, cid)] = font;
    }
}

} // namespace chatfont
