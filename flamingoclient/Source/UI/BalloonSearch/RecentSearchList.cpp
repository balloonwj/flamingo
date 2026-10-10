/**
 *  主面板搜索框「最近搜索」列表的实现。整体说明见 RecentSearchList.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "RecentSearchList.h"

namespace balloonsearch {

namespace {

// 是否为要去掉的首尾空白（半角空格、全角空格与控制字符）。
bool IsTrimChar(wchar_t ch)
{
    const wchar_t kIdeographicSpace = 0x3000;   // 全角空格
    return ch <= L' ' || ch == kIdeographicSpace;
}

// 去掉首尾空白。
std::wstring TrimKeyword(const std::wstring& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && IsTrimChar(s[b]))
    {
        ++b;
    }
    while (e > b && IsTrimChar(s[e - 1]))
    {
        --e;
    }
    return s.substr(b, e - b);
}

// 两个关键字是否相同：英文字母不区分大小写（与搜索本身的匹配规则一致），其余逐字比较。
bool SameKeyword(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        wchar_t x = a[i];
        wchar_t y = b[i];
        if (x >= L'A' && x <= L'Z')
        {
            x = (wchar_t)(x - L'A' + L'a');
        }
        if (y >= L'A' && y <= L'Z')
        {
            y = (wchar_t)(y - L'A' + L'a');
        }
        if (x != y)
        {
            return false;
        }
    }
    return true;
}

} // namespace

RecentSearchList::RecentSearchList()
{
}

bool RecentSearchList::Add(const std::wstring& keyword)
{
    std::wstring k = TrimKeyword(keyword);
    if (k.size() > kRecentSearchMaxChars)
    {
        k = TrimKeyword(k.substr(0, kRecentSearchMaxChars));
    }
    if (k.empty())
    {
        return false;
    }
    if (!m_items.empty() && m_items[0] == k)
    {
        return false;
    }

    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (SameKeyword(m_items[i], k))
        {
            m_items.erase(m_items.begin() + static_cast<std::vector<std::wstring>::difference_type>(i));
            break;
        }
    }
    m_items.insert(m_items.begin(), k);
    if (m_items.size() > kRecentSearchCapacity)
    {
        m_items.resize(kRecentSearchCapacity);
    }
    return true;
}

bool RecentSearchList::Remove(const std::wstring& keyword)
{
    const std::wstring k = TrimKeyword(keyword);
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (SameKeyword(m_items[i], k))
        {
            m_items.erase(m_items.begin() + static_cast<std::vector<std::wstring>::difference_type>(i));
            return true;
        }
    }
    return false;
}

void RecentSearchList::Clear()
{
    m_items.clear();
}

std::wstring RecentSearchList::Serialize() const
{
    std::wstring out;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (i > 0)
        {
            out.push_back(kRecentSearchSeparator);
        }
        out += m_items[i];
    }
    return out;
}

void RecentSearchList::Deserialize(const std::wstring& text)
{
    m_items.clear();
    //配置里是最近的在前；逐个 Add 会把后加的排到前面，所以从后往前放回，顺序保持不变。
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t sep = text.find(kRecentSearchSeparator, start);
        const size_t end = (sep == std::wstring::npos) ? text.size() : sep;
        parts.push_back(text.substr(start, end - start));
        if (sep == std::wstring::npos)
        {
            break;
        }
        start = sep + 1;
    }
    for (size_t i = parts.size(); i > 0; --i)
    {
        Add(parts[i - 1]);
    }
}

} // namespace balloonsearch
