/**
 *  帖子群窗口的位图缓存，见 ThreadBitmapCache.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadBitmapCache.h"

#include "../../Source/UI/BalloonMain/AvatarImageUtil.h"   // MakeRoundedAvatarFromFile：读图、裁成正方形、缩放与圆角

namespace threadgroup {

CThreadBitmapCache::CThreadBitmapCache()
{
}

CThreadBitmapCache::~CThreadBitmapCache()
{
    Clear();
}

HBITMAP CThreadBitmapCache::Get(const std::wstring& path, int sizePx, int radiusPx)
{
    if (path.empty() || sizePx <= 0)
        return NULL;
    Key key;
    key.m_path = path;
    key.m_size = sizePx;
    key.m_radius = (radiusPx > 0) ? radiusPx : 0;
    std::map<Key, HBITMAP>::iterator it = m_bitmaps.find(key);
    if (it != m_bitmaps.end())
        return it->second;
    HBITMAP bmp = balloonmain::MakeRoundedAvatarFromFile(path.c_str(), sizePx, key.m_radius);
    m_bitmaps[key] = bmp;
    return bmp;
}

void CThreadBitmapCache::Clear()
{
    for (std::map<Key, HBITMAP>::iterator it = m_bitmaps.begin(); it != m_bitmaps.end(); ++it)
    {
        if (it->second != NULL)
            ::DeleteObject(it->second);
    }
    m_bitmaps.clear();
}

size_t CThreadBitmapCache::Count() const
{
    return m_bitmaps.size();
}

} // namespace threadgroup
