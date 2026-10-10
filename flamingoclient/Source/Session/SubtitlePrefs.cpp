/**
 *  SubtitlePrefs.cpp
 *
 *  聊天字幕模式偏好设置的数据模型实现，说明见 SubtitlePrefs.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#include "stdafx.h"

#include "SubtitlePrefs.h"

#include <stdlib.h>   // _tcstoi64 / _tcstoui64
#include <tchar.h>

namespace subtitle {

namespace {

//位置文本的字段数：左、上、宽、是否锁定
const int kPlacementFieldCount = 4;
//位置文本里字段之间的分隔符
const TCHAR kFieldSeparator = _T(',');
//会话键里会话类型与会话ID之间的分隔符
const TCHAR kKeySeparator = _T('_');

//把一段只含十进制数字的文本转成无符号整数；含其它字符或为空时返回 false
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

//把一段十进制整数文本（可以带一个前导负号）转成整数；格式不对时返回 false
bool ParseSigned(const CString& text, long long& out)
{
    if (text.IsEmpty())
        return false;
    const bool negative = (text[0] == _T('-'));
    unsigned long long magnitude = 0;
    if (!ParseUnsigned(negative ? text.Mid(1) : text, magnitude))
        return false;
    out = negative ? -(long long)magnitude : (long long)magnitude;
    return true;
}

//解析会话键「会话类型_会话ID」；两部分都必须是正整数
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
    cid = (int64_t)id;
    return true;
}

//把 value 限制在 [lo, hi] 之内
int ClampInt(int value, int lo, int hi)
{
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

} // anonymous namespace

CString EncodePlacement(const SubtitlePlacement& pl)
{
    CString text;
    text.Format(_T("%d,%d,%d,%d"), pl.m_left, pl.m_top, pl.m_width, pl.m_locked ? 1 : 0);
    return text;
}

bool DecodePlacement(LPCTSTR text, SubtitlePlacement& out)
{
    if (text == NULL)
        return false;
    const CString s(text);
    long long fields[kPlacementFieldCount] = { 0 };
    int start = 0;
    for (int i = 0; i < kPlacementFieldCount; ++i)
    {
        const int sep = s.Find(kFieldSeparator, start);
        const bool last = (i == kPlacementFieldCount - 1);
        //最后一个字段之后不能再有分隔符，其余字段之后必须有
        if (last != (sep < 0))
            return false;
        const CString part = last ? s.Mid(start) : s.Mid(start, sep - start);
        if (!ParseSigned(part, fields[i]))
            return false;
        start = sep + 1;
    }
    //宽度必须为正数；锁定标志只认 0 和 1
    const int kWidthField = 2;
    const int kLockedField = 3;
    if (fields[kWidthField] <= 0)
        return false;
    if (fields[kLockedField] != 0 && fields[kLockedField] != 1)
        return false;
    out.m_left = (int)fields[0];
    out.m_top = (int)fields[1];
    out.m_width = (int)fields[kWidthField];
    out.m_locked = (fields[kLockedField] == 1);
    return true;
}

CString MakeConversationKey(int ctype, int64_t cid)
{
    CString key;
    key.Format(_T("%d%c%I64d"), ctype, kKeySeparator, (long long)cid);
    return key;
}

SubtitleAppearance NormalizeAppearance(const SubtitleAppearance& in)
{
    SubtitleAppearance out = in;
    out.m_fontSize = ClampInt(in.m_fontSize, kMinFontSize, kMaxFontSize);
    out.m_maxLines = ClampInt(in.m_maxLines, kMinMaxLines, kMaxMaxLines);
    return out;
}

const SubtitleAppearance& SubtitlePrefs::GetAppearance() const
{
    return m_appearance;
}

void SubtitlePrefs::SetAppearance(const SubtitleAppearance& appearance)
{
    m_appearance = NormalizeAppearance(appearance);
}

bool SubtitlePrefs::GetPlacement(int ctype, int64_t cid, SubtitlePlacement& out) const
{
    std::map<std::pair<int, int64_t>, SubtitlePlacement>::const_iterator it =
        m_placements.find(std::make_pair(ctype, cid));
    if (it == m_placements.end())
        return false;
    out = it->second;
    return true;
}

void SubtitlePrefs::SetPlacement(int ctype, int64_t cid, const SubtitlePlacement& pl)
{
    if (pl.m_width <= 0)
        return;
    m_placements[std::make_pair(ctype, cid)] = pl;
}

void SubtitlePrefs::ExportPlacements(std::vector<std::pair<CString, CString> >& out) const
{
    out.clear();
    //std::map 按 (会话类型, 会话ID) 升序遍历，导出顺序因而稳定
    for (std::map<std::pair<int, int64_t>, SubtitlePlacement>::const_iterator it = m_placements.begin();
         it != m_placements.end(); ++it)
    {
        out.push_back(std::make_pair(MakeConversationKey(it->first.first, it->first.second),
                                     EncodePlacement(it->second)));
    }
}

void SubtitlePrefs::ImportPlacements(const std::vector<std::pair<CString, CString> >& entries)
{
    m_placements.clear();
    for (size_t i = 0; i < entries.size(); ++i)
    {
        int ctype = 0;
        int64_t cid = 0;
        SubtitlePlacement pl;
        if (!ParseKey(entries[i].first, ctype, cid) || !DecodePlacement(entries[i].second, pl))
            continue;
        m_placements[std::make_pair(ctype, cid)] = pl;
    }
}

} // namespace subtitle
