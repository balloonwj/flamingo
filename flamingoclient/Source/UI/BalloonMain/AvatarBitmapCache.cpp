/**
 *  头像位图缓存的实现。整体说明见 AvatarBitmapCache.h。
 *
 *  键的格式（首字段区分两类头像，可变长的内容一律放在最后，因此不会有两个不同的输入
 *  拼出同一个键）：
 *    字形头像  M|<底色，8 位十六进制>|<边长>|<首字>
 *    真图头像  I|<边长>|<文件路径>
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "AvatarBitmapCache.h"

#include <stdio.h>

namespace balloonmain {

namespace {

// 键里"底色 + 边长"那一段的缓冲区长度：前缀 2 + 底色 8 + 分隔 1 + 边长最多 11 + 分隔 1 + 结束符。
const int kKeyHeadBufLen = 32;

} // anonymous namespace

CAvatarBitmapCache::CAvatarBitmapCache(AvatarMonogramFactory monogramFactory,
                                       AvatarImageFactory imageFactory)
    : m_monogramFactory(monogramFactory)
    , m_imageFactory(imageFactory)
{
}

CAvatarBitmapCache::~CAvatarBitmapCache()
{
    Clear();
}

HBITMAP CAvatarBitmapCache::Lookup(const std::wstring& key) const
{
    std::map<std::wstring, HBITMAP>::const_iterator it = m_bitmaps.find(key);
    return (it != m_bitmaps.end()) ? it->second : NULL;
}

HBITMAP CAvatarBitmapCache::GetMonogram(LPCWSTR glyph, COLORREF color, int sizePx)
{
    if (sizePx <= 0 || m_monogramFactory == NULL)
    {
        return NULL;
    }

    //NULL 与空串都表示"只画底色圆"，归到同一个键
    const wchar_t* text = (glyph != NULL) ? glyph : L"";

    wchar_t head[kKeyHeadBufLen] = { 0 };
    ::swprintf_s(head, L"M|%08X|%d|", (unsigned int)color, sizePx);
    std::wstring key(head);
    key += text;

    HBITMAP hbm = Lookup(key);
    if (hbm != NULL)
    {
        return hbm;
    }

    //失败的结果不缓存：生成失败多半是暂时的（例如 GDI 对象一时耗尽），下次再试
    hbm = m_monogramFactory(text, color, sizePx);
    if (hbm != NULL)
    {
        m_bitmaps[key] = hbm;
    }
    return hbm;
}

HBITMAP CAvatarBitmapCache::GetImage(LPCWSTR path, int sizePx)
{
    if (path == NULL || path[0] == L'\0' || sizePx <= 0 || m_imageFactory == NULL)
    {
        return NULL;
    }

    wchar_t head[kKeyHeadBufLen] = { 0 };
    ::swprintf_s(head, L"I|%d|", sizePx);
    std::wstring key(head);
    key += path;

    HBITMAP hbm = Lookup(key);
    if (hbm != NULL)
    {
        return hbm;
    }

    //圆角半径取边长的一半，画成圆形，与字形头像的圆形一致
    hbm = m_imageFactory(path, sizePx, sizePx / 2);
    if (hbm != NULL)
    {
        m_bitmaps[key] = hbm;
    }
    return hbm;
}

void CAvatarBitmapCache::Clear()
{
    for (std::map<std::wstring, HBITMAP>::iterator it = m_bitmaps.begin(); it != m_bitmaps.end(); ++it)
    {
        if (it->second != NULL)
        {
            ::DeleteObject(it->second);
        }
    }
    m_bitmaps.clear();
}

size_t CAvatarBitmapCache::Count() const
{
    return m_bitmaps.size();
}

} // namespace balloonmain
