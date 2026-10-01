/**
 *  文字宽度缓存的实现。整体说明见 TextWidthCache.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "TextWidthCache.h"

namespace balloonmain {

TextWidthCache::TextWidthCache(TextWidthMeasurer measurer, size_t maxEntries)
    : m_measurer(measurer)
    , m_maxEntries((maxEntries > 0) ? maxEntries : 1)
    , m_font(NULL)
{
}

int TextWidthCache::Width(HFONT font, LPCWSTR text)
{
    if (text == NULL || text[0] == L'\0' || m_measurer == NULL)
    {
        return 0;
    }

    //字体变了：旧字体下量出的宽度全部作废
    if (font != m_font)
    {
        m_widths.clear();
        m_font = font;
    }

    const std::wstring key(text);
    std::map<std::wstring, int>::const_iterator it = m_widths.find(key);
    if (it != m_widths.end())
    {
        return it->second;
    }

    const int width = m_measurer(font, text, (int)key.size());
    if (width <= 0)
    {
        return 0;   //测量失败多半是暂时的，不缓存，下次重新测量
    }

    //到达上限时整体清空再放入，保证条目数不超过上限
    if (m_widths.size() >= m_maxEntries)
    {
        m_widths.clear();
    }
    m_widths[key] = width;
    return width;
}

void TextWidthCache::Clear()
{
    m_widths.clear();
    m_font = NULL;
}

size_t TextWidthCache::Count() const
{
    return m_widths.size();
}

} // namespace balloonmain
