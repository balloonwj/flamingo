/**
 *  ChatAvatarBitmapCache.h
 *
 *  聊天气泡发送者真图头像的位图缓存：同一张头像只解码、缩放一次，所有气泡共用。
 *
 *  原先每个气泡各自从磁盘解码一次发送者头像（256×256 的 PNG，平均 0.3 毫秒），并在每次
 *  绘制时用高质量插值把原图缩到气泡头像的尺寸（平均 1.1 毫秒，实测）。文字气泡有渲染缓存、
 *  只画一次，图片与文件气泡则每一帧都重画，滚动时这部分开销逐帧累加。
 *
 *  本缓存按「路径 + 边长 + 圆角半径」存一张已经缩放好、带圆角与预乘透明通道的 32 位位图
 *  （由 balloonmain::MakeRoundedAvatarFromFile 生成，与主面板头部头像同一套做法），气泡
 *  绘制时 1:1 贴上去即可。
 *
 *  存的是 GDI 位图（HBITMAP）而不是 GDI+ 的位图对象：后者依赖 GDI+ 的生命周期，进程退出或
 *  单元测试反复启停 GDI+ 之后再析构会出错；GDI 位图没有这个问题。
 *
 *  线程：只在 UI 线程使用（气泡都在 UI 线程绘制），内部不加锁。
 *
 *  典型用法：
 *      HBITMAP bmp = ChatAvatarBitmapCache::Inst().Get(path, kAvatarSize, kRadius);
 *      if (bmp != NULL)
 *          按预乘透明通道贴到目标位置;          // 位图归缓存所有，调用方不得删除
 *      //头像文件被重新下载覆盖（同一路径内容变了）时：
 *      ChatAvatarBitmapCache::Inst().Invalidate(path);
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include <windows.h>
#include <map>
#include <string>

namespace balloonchatwnd {

/**
 *  ChatAvatarBitmapCache —— 进程级的头像位图缓存（单例）。
 */
class ChatAvatarBitmapCache
{
public:
    // 进程级单例。
    static ChatAvatarBitmapCache& Inst();

    /**
     *  取 path 对应、缩放成 sizePx 见方、圆角半径 radiusPx 的头像位图。
     *
     *  第一次取时解码并缩放，之后直接返回同一张位图。解码失败（文件不存在、格式不认识）
     *  也会记下来，同一组参数之后直接返回 NULL、不再重试 —— 否则每一帧都要去碰一次磁盘。
     *  文件后来出现或被替换时，调用方用 Invalidate 让它重新解码。
     *
     *    path：    图片文件完整路径；空串直接返回 NULL。
     *    sizePx：  位图边长（像素，>0）。
     *    radiusPx：圆角半径（像素，>=0）。
     *  @return 32 位预乘透明通道的 DIB 位图，归缓存所有，调用方<u>不得</u>删除；
     *          解码失败返回 NULL。
     */
    HBITMAP Get(const std::wstring& path, int sizePx, int radiusPx);

    /**
     *  取同一张头像的灰度副本（发送者离线、或本端掉线时本人的头像，2026-10-03）。参数含义同 Get。
     *
     *  第一次取时先经 Get 取彩色位图，再由 balloonmain::MakeGrayAvatarBitmap 生成灰度副本并缓存，
     *  之后直接返回同一张；生成灰度副本不会再解码图片文件，DecodeCount 不因此增加。
     *  @return 32 位预乘透明通道的 DIB 位图，归缓存所有，调用方<u>不得</u>删除；彩色位图取不到
     *          （解码失败）或灰度副本生成失败时返回 NULL，调用方退回画彩色位图或字形。
     */
    HBITMAP GetGray(const std::wstring& path, int sizePx, int radiusPx);

    /**
     *  作废 path 对应的所有缓存（各种尺寸，彩色与灰度副本），下次 Get 时重新解码。
     *  用于同一路径的文件内容变了的场合：头像下载完成（原先不存在或是旧图）。
     *    path：图片文件完整路径。
     */
    void Invalidate(const std::wstring& path);

    // 清空全部缓存并释放位图。
    void Clear();

    // 自创建（或上次 Clear）以来实际解码图片文件的次数，供单元测试核对"没有重复解码"。
    int DecodeCount() const;

private:
    ChatAvatarBitmapCache();
    ~ChatAvatarBitmapCache();
    ChatAvatarBitmapCache(const ChatAvatarBitmapCache&);
    ChatAvatarBitmapCache& operator=(const ChatAvatarBitmapCache&);

    // 缓存键：路径 + 边长 + 圆角半径 + 是否灰度副本。
    struct Key
    {
        std::wstring m_path;       // 图片文件完整路径
        int          m_sizePx;     // 位图边长（像素）
        int          m_radiusPx;   // 圆角半径（像素）
        bool         m_gray;       // true 表示这一项是灰度副本（GetGray 生成），false 为彩色原图

        bool operator<(const Key& rhs) const;
    };

    // 已解码的位图；值为 NULL 表示解码失败（记住失败，避免每帧重试）。
    std::map<Key, HBITMAP> m_entries;

    // 实际解码次数，见 DecodeCount。
    int m_decodeCount;
};

} // namespace balloonchatwnd
