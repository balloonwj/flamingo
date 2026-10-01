/**
 *  头像位图缓存：主面板三个列表（会话、通讯录、群）的行头像按内容缓存、共用。
 *
 *  以前每个列表节点都单独生成一张头像位图：几千个节点就是几千次 GDI+ 绘制与几千个
 *  GDI 对象，一次会话列表重建有近一半时间花在这里，而且三个列表加起来的 GDI 对象数
 *  逼近每进程一万个的上限。实际上绝大多数头像是"首字 + 底色"画出来的字形头像，同一个
 *  首字、同一个底色、同一个边长画出来的位图完全一样，没有必要画第二次。
 *
 *  缓存的两类键：
 *    · 字形头像：首字 + 底色 + 边长；
 *    · 真图头像：本地缓存文件路径 + 边长（文件名即头像文件 id，文件内容不会变）。
 *
 *  <u>所有权</u>：缓存里的位图归本类所有，调用方只借用，<u>不得</u> DeleteObject，
 *  也不要放进任何"重建时统一释放"的池子里。位图在 Clear 或本对象析构时释放，因此
 *  本对象的生命周期必须长于所有引用这些位图的控件。
 *
 *  生成位图的函数由构造函数传入：正式代码传 MakeMonogramAvatar 与
 *  MakeRoundedAvatarFromFile，单元测试传会计数的替身。本文件因此不依赖 GDI+ 与
 *  stdafx.h，与 FlamingoUnit 测试工程共用同一份源码（见 Tests/AvatarBitmapCacheTests.cpp）。
 *
 *  典型用法：
 *      balloonmain::CAvatarBitmapCache cache(balloonmain::MakeMonogramAvatar,
 *                                            balloonmain::MakeRoundedAvatarFromFile);
 *      HBITMAP hbm = cache.GetMonogram(L"张", RGB(0x4A, 0x90, 0xE2), 36);
 *      tv->SetItemIcon(node, hbm);        // 只借用，不释放
 *
 *  本类不是线程安全的，只在 UI 线程使用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include <windows.h>
#include <map>
#include <string>

namespace balloonmain {

// 生成字形头像的函数：text 为首字（取前 1~2 个字符），bgColor 为底色，sizePx 为边长。
// 返回新建的位图，所有权交给调用方；失败返回 NULL。
typedef HBITMAP (*AvatarMonogramFactory)(LPCWSTR text, COLORREF bgColor, int sizePx);

// 从本地图片文件生成圆角头像的函数：path 为图片文件完整路径，sizePx 为边长，radiusPx 为
// 圆角半径。返回新建的位图，所有权交给调用方；失败返回 NULL。
typedef HBITMAP (*AvatarImageFactory)(LPCWSTR path, int sizePx, int radiusPx);

/**
 *  CAvatarBitmapCache —— 按内容缓存、共用的头像位图集合。说明见文件头。
 */
class CAvatarBitmapCache
{
public:
    /**
     *  构造。
     *    monogramFactory：生成字形头像的函数，不可为 NULL。
     *    imageFactory：   从图片文件生成圆形头像的函数，不可为 NULL。
     */
    CAvatarBitmapCache(AvatarMonogramFactory monogramFactory, AvatarImageFactory imageFactory);

    // 析构时释放全部缓存的位图（等同于 Clear）。
    ~CAvatarBitmapCache();

    /**
     *  取一张字形头像，未缓存时生成并缓存。
     *    glyph：  首字，可为 NULL 或空串（此时只画底色圆）。
     *    color：  底色。
     *    sizePx： 边长（像素），须大于 0。
     *  返回：缓存中的位图（归本类所有，调用方不得释放）；生成失败或参数非法时返回 NULL，
     *        失败的结果不缓存，下次再取会重新尝试生成。
     */
    HBITMAP GetMonogram(LPCWSTR glyph, COLORREF color, int sizePx);

    /**
     *  取一张由本地图片文件生成的圆形头像，未缓存时生成并缓存。
     *    path：   图片文件完整路径，不可为 NULL 或空串。
     *    sizePx： 边长（像素），须大于 0；圆角半径取边长的一半，即画成圆形。
     *  返回：缓存中的位图（归本类所有，调用方不得释放）；文件无法解码或参数非法时返回
     *        NULL，失败的结果不缓存。
     */
    HBITMAP GetImage(LPCWSTR path, int sizePx);

    /**
     *  释放全部缓存的位图并清空。调用前必须确保已没有控件在引用这些位图
     *  （例如各列表已 Clear），否则控件会画到已释放的句柄上。
     */
    void Clear();

    // 当前缓存的位图张数。
    size_t Count() const;

private:
    // 按完整键查缓存；未命中返回 NULL。
    HBITMAP Lookup(const std::wstring& key) const;

    // 禁止复制：位图所有权唯一，复制后两份析构会重复释放。
    CAvatarBitmapCache(const CAvatarBitmapCache&);
    CAvatarBitmapCache& operator=(const CAvatarBitmapCache&);

private:
    AvatarMonogramFactory          m_monogramFactory;   // 生成字形头像的函数，构造时传入
    AvatarImageFactory             m_imageFactory;      // 从图片文件生成头像的函数，构造时传入
    std::map<std::wstring, HBITMAP> m_bitmaps;          // 键 → 位图，位图归本类所有
};

} // namespace balloonmain
