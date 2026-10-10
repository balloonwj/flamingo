/**
 *  FlamingoMarkdown.dll 字体缓存的实现，说明见 MdFontCache.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MdFontCache.h"

namespace flamingomd {

namespace {

//加粗字体的字重：与聊天气泡的正文加粗一致（ChatInlineText 的 CreateInlineFont 用 FW_SEMIBOLD），
//微软雅黑只有常规与粗体两档，600 会落到粗体
const int kBoldWeight = FW_SEMIBOLD;

//常规字体的字重
const int kNormalWeight = FW_NORMAL;

//GetGlyphIndicesW 在 GGI_MARK_NONEXISTING_GLYPHS 下给缺字形的字符填的字形号
const WORD kMissingGlyphIndex = 0xFFFF;

//代理对占两个 UTF-16 码元
const int kSurrogatePairUnits = 2;

} // namespace

bool MdFontKey::operator<(const MdFontKey& other) const
{
    if (m_px != other.m_px)
        return m_px < other.m_px;
    if (m_bold != other.m_bold)
        return m_bold < other.m_bold;
    if (m_italic != other.m_italic)
        return m_italic < other.m_italic;
    if (m_underline != other.m_underline)
        return m_underline < other.m_underline;
    if (m_strike != other.m_strike)
        return m_strike < other.m_strike;
    return m_face < other.m_face;
}

MdFontCache& MdFontCache::Instance()
{
    static MdFontCache s_instance;
    return s_instance;
}

MdFontCache::MdFontCache()
{
    ::InitializeSRWLock(&m_lock);
}

MdFontCache::~MdFontCache()
{
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i]->m_font != NULL)
            ::DeleteObject(m_entries[i]->m_font);
        delete m_entries[i];
    }
    m_entries.clear();
}

int MdFontCache::Acquire(HDC hdc, const MdFontKey& key)
{
    ::AcquireSRWLockExclusive(&m_lock);
    std::map<MdFontKey, int>::const_iterator it = m_index.find(key);
    if (it != m_index.end())
    {
        const int found = it->second;
        ::ReleaseSRWLockExclusive(&m_lock);
        return found;
    }

    HFONT font = ::CreateFontW(-key.m_px, 0, 0, 0,
                               key.m_bold ? kBoldWeight : kNormalWeight,
                               key.m_italic ? TRUE : FALSE,
                               key.m_underline ? TRUE : FALSE,
                               key.m_strike ? TRUE : FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, key.m_face.c_str());
    if (font == NULL)
    {
        ::ReleaseSRWLockExclusive(&m_lock);
        return -1;
    }

    Entry* entry = new Entry();
    entry->m_key = key;
    entry->m_font = font;

    HGDIOBJ old = ::SelectObject(hdc, font);
    TEXTMETRICW tm;
    if (::GetTextMetricsW(hdc, &tm))
    {
        entry->m_metrics.m_ascent = tm.tmAscent;
        entry->m_metrics.m_descent = tm.tmDescent;
        entry->m_metrics.m_height = tm.tmAscent + tm.tmDescent;
    }
    else
    {
        entry->m_metrics.m_ascent = key.m_px;
        entry->m_metrics.m_descent = 0;
        entry->m_metrics.m_height = key.m_px;
    }
    ::SelectObject(hdc, old);

    m_entries.push_back(entry);
    const int index = static_cast<int>(m_entries.size()) - 1;
    m_index[key] = index;
    ::ReleaseSRWLockExclusive(&m_lock);
    return index;
}

HFONT MdFontCache::Font(int index) const
{
    ::AcquireSRWLockShared(&m_lock);
    HFONT font = (index >= 0 && static_cast<size_t>(index) < m_entries.size()) ? m_entries[index]->m_font : NULL;
    ::ReleaseSRWLockShared(&m_lock);
    return font;
}

MdFontMetrics MdFontCache::Metrics(int index) const
{
    MdFontMetrics metrics;
    ::AcquireSRWLockShared(&m_lock);
    if (index >= 0 && static_cast<size_t>(index) < m_entries.size())
        metrics = m_entries[index]->m_metrics;
    ::ReleaseSRWLockShared(&m_lock);
    return metrics;
}

int MdFontCache::CharWidth(HDC hdc, int index, const wchar_t* text, int count)
{
    SIZE size = { 0, 0 };
    if (count != 1)
    {
        //代理对不缓存（数量少，且键不是单个码元）
        ::GetTextExtentPoint32W(hdc, text, (count == kSurrogatePairUnits) ? kSurrogatePairUnits : count, &size);
        return size.cx;
    }

    const wchar_t ch = text[0];
    ::AcquireSRWLockExclusive(&m_lock);
    if (index < 0 || static_cast<size_t>(index) >= m_entries.size())
    {
        ::ReleaseSRWLockExclusive(&m_lock);
        ::GetTextExtentPoint32W(hdc, text, 1, &size);
        return size.cx;
    }
    Entry* entry = m_entries[index];
    std::map<wchar_t, int>::const_iterator it = entry->m_widths.find(ch);
    if (it != entry->m_widths.end())
    {
        const int width = it->second;
        ::ReleaseSRWLockExclusive(&m_lock);
        return width;
    }
    ::GetTextExtentPoint32W(hdc, text, 1, &size);
    entry->m_widths[ch] = size.cx;
    ::ReleaseSRWLockExclusive(&m_lock);
    return size.cx;
}

bool MdFontCache::HasGlyph(HDC hdc, int index, wchar_t ch)
{
    ::AcquireSRWLockExclusive(&m_lock);
    if (index < 0 || static_cast<size_t>(index) >= m_entries.size())
    {
        ::ReleaseSRWLockExclusive(&m_lock);
        return true;
    }
    Entry* entry = m_entries[index];
    std::map<wchar_t, bool>::const_iterator it = entry->m_glyphs.find(ch);
    if (it != entry->m_glyphs.end())
    {
        const bool has = it->second;
        ::ReleaseSRWLockExclusive(&m_lock);
        return has;
    }
    WORD glyph = 0;
    bool has = true;
    if (::GetGlyphIndicesW(hdc, &ch, 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR)
        has = (glyph != kMissingGlyphIndex);
    entry->m_glyphs[ch] = has;
    ::ReleaseSRWLockExclusive(&m_lock);
    return has;
}

size_t MdFontCache::Count() const
{
    ::AcquireSRWLockShared(&m_lock);
    const size_t n = m_entries.size();
    ::ReleaseSRWLockShared(&m_lock);
    return n;
}

} // namespace flamingomd
