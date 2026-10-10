/**
 *  帖子群窗口的位图缓存 CThreadBitmapCache：把本机图片文件（头像照片、表情、缩略图）读成指定边长与圆角的位图并缓存，
 *  同一张图同一种尺寸只读一次。位图归缓存所有，头像控件（DuiAvatar）只借用；窗口关闭、控件都销毁之后调 Clear 释放。
 *
 *  读图复用主程序的 balloonmain::MakeRoundedAvatarFromFile（居中裁成正方形、缩放、圆角，GDI+ 实现）。
 *
 *  典型用法：
 *      HBITMAP bmp = m_bitmaps.Get(path, kFacePx, 0);
 *      if (bmp != NULL) avatar->SetBitmap(bmp);
 *      ...窗口关闭后：m_bitmaps.Clear();
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <map>
#include <string>

namespace threadgroup {

/**
 *  CThreadBitmapCache —— 位图缓存。只在界面线程上使用。
 */
class CThreadBitmapCache
{
public:
    CThreadBitmapCache();
    ~CThreadBitmapCache();

    /**
     *  取一张图的位图，没有缓存时读文件生成。
     *  @param path     图片在本机的完整路径；为空时返回 NULL
     *  @param sizePx   边长，单位：像素，大于 0
     *  @param radiusPx 圆角半径，单位：像素；0 为直角
     *  @return 位图（归缓存所有，调用方不要释放）；文件读不出来时返回 NULL（同样记下，不再重复读）
     */
    HBITMAP Get(const std::wstring& path, int sizePx, int radiusPx);

    //释放全部位图。调用之前须确保借用它们的控件都已销毁
    void Clear();

    //缓存的条目数（含读失败的），测试用
    size_t Count() const;

private:
    CThreadBitmapCache(const CThreadBitmapCache&);
    CThreadBitmapCache& operator=(const CThreadBitmapCache&);

    //缓存的键：路径、边长、圆角
    struct Key
    {
        std::wstring m_path;
        int          m_size;
        int          m_radius;

        bool operator<(const Key& other) const
        {
            if (m_path != other.m_path)
                return m_path < other.m_path;
            if (m_size != other.m_size)
                return m_size < other.m_size;
            return m_radius < other.m_radius;
        }
    };

    std::map<Key, HBITMAP> m_bitmaps;   //已读过的图：键 → 位图（读失败的为 NULL）
};

} // namespace threadgroup
