/**
 *  ChatAvatarBitmapCache.cpp
 *
 *  聊天气泡头像位图缓存的实现，见 ChatAvatarBitmapCache.h。
 *
 *  本文件与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h（Flamingo.vcxproj 里
 *  对本文件把预编译头设为 NotUsing）。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "ChatAvatarBitmapCache.h"

#include "../BalloonMain/AvatarImageUtil.h"   // MakeRoundedAvatarFromFile：解码 + 居中裁方 + 缩放 + 圆角

namespace balloonchatwnd {

namespace {

// 缓存条目数上限。键是"发送者头像文件 + 尺寸"，一个进程里打开的聊天窗涉及的发送者
// 通常只有几十个；超过上限时整体清空重来，避免异常情况下（例如头像路径不断变化）
// 位图无限增长。每张 34 见方的位图约 4.5KB，上限对应约 1MB。
const size_t kMaxEntries = 256;

} // anonymous namespace

bool ChatAvatarBitmapCache::Key::operator<(const Key& rhs) const
{
    if (m_sizePx != rhs.m_sizePx)
    {
        return m_sizePx < rhs.m_sizePx;
    }
    if (m_radiusPx != rhs.m_radiusPx)
    {
        return m_radiusPx < rhs.m_radiusPx;
    }
    return m_path < rhs.m_path;
}

ChatAvatarBitmapCache& ChatAvatarBitmapCache::Inst()
{
    static ChatAvatarBitmapCache s_inst;
    return s_inst;
}

ChatAvatarBitmapCache::ChatAvatarBitmapCache()
    : m_decodeCount(0)
{
}

ChatAvatarBitmapCache::~ChatAvatarBitmapCache()
{
    Clear();
}

HBITMAP ChatAvatarBitmapCache::Get(const std::wstring& path, int sizePx, int radiusPx)
{
    if (path.empty() || sizePx <= 0)
    {
        return NULL;
    }

    Key key;
    key.m_path     = path;
    key.m_sizePx   = sizePx;
    key.m_radiusPx = radiusPx;
    std::map<Key, HBITMAP>::const_iterator it = m_entries.find(key);
    if (it != m_entries.end())
    {
        return it->second;   //含解码失败记下的 NULL：不再重试
    }

    if (m_entries.size() >= kMaxEntries)
    {
        Clear();
    }

    ++m_decodeCount;
    HBITMAP bmp = balloonmain::MakeRoundedAvatarFromFile(path.c_str(), sizePx, radiusPx);
    m_entries[key] = bmp;
    return bmp;
}

void ChatAvatarBitmapCache::Invalidate(const std::wstring& path)
{
    std::map<Key, HBITMAP>::iterator it = m_entries.begin();
    while (it != m_entries.end())
    {
        if (it->first.m_path == path)
        {
            if (it->second != NULL)
            {
                ::DeleteObject(it->second);
            }
            it = m_entries.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void ChatAvatarBitmapCache::Clear()
{
    for (std::map<Key, HBITMAP>::iterator it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (it->second != NULL)
        {
            ::DeleteObject(it->second);
        }
    }
    m_entries.clear();
    m_decodeCount = 0;
}

int ChatAvatarBitmapCache::DecodeCount() const
{
    return m_decodeCount;
}

} // namespace balloonchatwnd
