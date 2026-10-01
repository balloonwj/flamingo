/**
 *  文字测量结果缓存的实现，说明见 ChatTextExtentCache.h。
 *  balloonwj@qq.com   2026-10-01
 */

#include "ChatTextExtentCache.h"

namespace balloonchatwnd {

TextExtentFontKey::TextExtentFontKey()
    : m_nHeight(0)
    , m_nWidth(0)
    , m_nWeight(0)
    , m_bItalic(0)
    , m_bUnderline(0)
    , m_bStrikeOut(0)
    , m_nCharSet(0)
    , m_nQuality(0)
    , m_nMapMode(0)
{
}

TextExtentFontKey TextExtentFontKey::FromDc(HDC hdc)
{
    TextExtentFontKey key;
    if (hdc == NULL)
        return key;

    key.m_nMapMode = ::GetMapMode(hdc);

    HFONT hFont = (HFONT)::GetCurrentObject(hdc, OBJ_FONT);
    LOGFONTW lf;
    ::ZeroMemory(&lf, sizeof(lf));
    if (hFont == NULL || ::GetObjectW(hFont, sizeof(lf), &lf) == 0)
        return key;

    key.m_nHeight = lf.lfHeight;
    key.m_nWidth = lf.lfWidth;
    key.m_nWeight = lf.lfWeight;
    key.m_bItalic = lf.lfItalic;
    key.m_bUnderline = lf.lfUnderline;
    key.m_bStrikeOut = lf.lfStrikeOut;
    key.m_nCharSet = lf.lfCharSet;
    key.m_nQuality = lf.lfQuality;
    key.m_strFaceName = lf.lfFaceName;
    return key;
}

bool TextExtentFontKey::operator<(const TextExtentFontKey& rhs) const
{
    if (m_nHeight != rhs.m_nHeight)
        return m_nHeight < rhs.m_nHeight;
    if (m_nWidth != rhs.m_nWidth)
        return m_nWidth < rhs.m_nWidth;
    if (m_nWeight != rhs.m_nWeight)
        return m_nWeight < rhs.m_nWeight;
    if (m_bItalic != rhs.m_bItalic)
        return m_bItalic < rhs.m_bItalic;
    if (m_bUnderline != rhs.m_bUnderline)
        return m_bUnderline < rhs.m_bUnderline;
    if (m_bStrikeOut != rhs.m_bStrikeOut)
        return m_bStrikeOut < rhs.m_bStrikeOut;
    if (m_nCharSet != rhs.m_nCharSet)
        return m_nCharSet < rhs.m_nCharSet;
    if (m_nQuality != rhs.m_nQuality)
        return m_nQuality < rhs.m_nQuality;
    if (m_nMapMode != rhs.m_nMapMode)
        return m_nMapMode < rhs.m_nMapMode;
    return m_strFaceName < rhs.m_strFaceName;
}

ChatTextExtentCache& ChatTextExtentCache::Inst()
{
    static ChatTextExtentCache s_inst;
    return s_inst;
}

ChatTextExtentCache::ChatTextExtentCache()
    : m_bEnabled(true)
{
}

ChatTextExtentCache::FontBucket& ChatTextExtentCache::BucketFor(const TextExtentFontKey& key)
{
    std::map<TextExtentFontKey, FontBucket>::iterator it = m_buckets.find(key);
    if (it != m_buckets.end())
        return it->second;

    if (m_buckets.size() >= kMaxFontBuckets)
        m_buckets.clear();

    return m_buckets[key];
}

SIZE ChatTextExtentCache::TextExtent(HDC hdc, const TextExtentFontKey& key, LPCWSTR text, int len)
{
    SIZE size;
    size.cx = 0;
    size.cy = 0;
    if (len <= 0 || text == NULL)
    {
        //与直接调用 GDI 保持一致：空串也交给 GDI，它会给出高度为字体行高、宽度为 0 的结果
        ::GetTextExtentPoint32W(hdc, L"", 0, &size);
        return size;
    }

    const std::wstring strText(text, (size_t)len);
    bool bEnabled = false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        bEnabled = m_bEnabled;
        if (bEnabled)
        {
            FontBucket& bucket = BucketFor(key);
            std::unordered_map<std::wstring, SIZE>::const_iterator hit = bucket.m_extents.find(strText);
            if (hit != bucket.m_extents.end())
                return hit->second;
        }
    }

    //未命中：调用 GDI。不在持锁状态下调用，GDI 调用的耗时不计入锁的占用时间
    if (!::GetTextExtentPoint32W(hdc, text, len, &size))
    {
        size.cx = 0;
        size.cy = 0;
        return size;
    }

    if (bEnabled)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        FontBucket& bucket = BucketFor(key);
        if (bucket.m_extents.size() + bucket.m_nCharExtentCount >= kMaxEntriesPerFont)
        {
            bucket.m_extents.clear();
            bucket.m_charExtents.clear();
            bucket.m_nCharExtentCount = 0;
        }
        bucket.m_extents[strText] = size;
    }
    return size;
}

void ChatTextExtentCache::EmitCharExtents(const std::vector<INT>& dx, CharExtentSink sink, int base,
                                          std::vector<INT>* pCopy, std::vector<int>* pRights)
{
    if (sink == kSinkCopy)
    {
        *pCopy = dx;
        return;
    }
    for (size_t k = 0; k < dx.size(); ++k)
        pRights->push_back(base + dx[k]);
}

void ChatTextExtentCache::ReadCharExtents(HDC hdc, const TextExtentFontKey& key, LPCWSTR text, int len,
                                          int nMaxExtent, CharExtentSink sink, int base,
                                          std::vector<INT>* pCopy, std::vector<int>* pRights)
{
    const std::wstring strText(text, (size_t)len);
    bool bEnabled = false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        bEnabled = m_bEnabled;
        if (bEnabled)
        {
            FontBucket& bucket = BucketFor(key);
            std::map<int, std::unordered_map<std::wstring, std::vector<INT> > >::iterator level =
                bucket.m_charExtents.find(nMaxExtent);
            if (level != bucket.m_charExtents.end())
            {
                std::unordered_map<std::wstring, std::vector<INT> >::const_iterator hit =
                    level->second.find(strText);
                if (hit != level->second.end())
                {
                    EmitCharExtents(hit->second, sink, base, pCopy, pRights);
                    return;
                }
            }
        }
    }

    //未命中：调用 GDI。不在持锁状态下调用，GDI 调用的耗时不计入锁的占用时间
    std::vector<INT> dx((size_t)len, 0);
    INT fit = 0;
    SIZE size;
    ::ZeroMemory(&size, sizeof(size));
    const BOOL bOk = ::GetTextExtentExPointW(hdc, text, len, nMaxExtent, &fit, &dx[0], &size);
    if (!bOk)
        dx.assign((size_t)len, 0);

    if (bOk && bEnabled)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        FontBucket& bucket = BucketFor(key);
        if (bucket.m_extents.size() + bucket.m_nCharExtentCount >= kMaxEntriesPerFont)
        {
            bucket.m_extents.clear();
            bucket.m_charExtents.clear();
            bucket.m_nCharExtentCount = 0;
        }
        std::unordered_map<std::wstring, std::vector<INT> >& level = bucket.m_charExtents[nMaxExtent];
        if (level.find(strText) == level.end())
        {
            level[strText] = dx;
            ++bucket.m_nCharExtentCount;
        }
    }

    EmitCharExtents(dx, sink, base, pCopy, pRights);
}

void ChatTextExtentCache::CharExtents(HDC hdc, const TextExtentFontKey& key, LPCWSTR text, int len,
                                      int nMaxExtent, std::vector<INT>& outDx)
{
    if (len <= 0 || text == NULL)
    {
        outDx.clear();
        return;
    }
    ReadCharExtents(hdc, key, text, len, nMaxExtent, kSinkCopy, 0, &outDx, NULL);
}

void ChatTextExtentCache::AppendCharRights(HDC hdc, const TextExtentFontKey& key, LPCWSTR text, int len,
                                           int nMaxExtent, int base, std::vector<int>& outRights)
{
    if (len <= 0 || text == NULL)
        return;
    ReadCharExtents(hdc, key, text, len, nMaxExtent, kSinkAppendRights, base, NULL, &outRights);
}

void ChatTextExtentCache::SetEnabled(bool bEnabled)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_bEnabled = bEnabled;
}

void ChatTextExtentCache::Clear()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_buckets.clear();
}

size_t ChatTextExtentCache::EntryCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    size_t n = 0;
    for (std::map<TextExtentFontKey, FontBucket>::const_iterator it = m_buckets.begin();
         it != m_buckets.end(); ++it)
    {
        n += it->second.m_extents.size() + it->second.m_nCharExtentCount;
    }
    return n;
}

} // namespace balloonchatwnd
