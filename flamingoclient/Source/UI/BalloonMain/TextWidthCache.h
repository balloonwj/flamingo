/**
 *  文字宽度缓存：按"字体 + 文字"记住量过的像素宽度，同样的文字不再重复测量。
 *
 *  主面板会话列表每个节点要量两三次右侧文字的宽度（时间"23:56"、占位用的空格等），
 *  每量一次都要取一次屏幕 DC、选入字体、测量、释放。3000 个会话的列表重建一次，这部分
 *  累计近一秒；而这些文字大量重复（同一分钟的时间、永远是同一个空格）。
 *
 *  作废规则：字体句柄变了（例如 DPI 变化后默认字体被重建）就整体作废；条目数超过上限时
 *  整体清空后继续工作，避免无限增长。量不出宽度（测量函数返回 0 或负数）时不缓存，下次
 *  再遇到同一段文字会重新测量 —— 失败多半是暂时的（例如一时取不到屏幕 DC）。
 *
 *  测量函数由构造函数传入：正式代码传用屏幕 DC 测量的函数，单元测试传会计数的替身。
 *  本文件不依赖 stdafx.h，与 FlamingoUnit 测试工程共用同一份源码
 *  （见 Tests/TextWidthCacheTests.cpp）。
 *
 *  典型用法：
 *      static balloonmain::TextWidthCache s_cache(MeasureWithScreenDc);
 *      int w = s_cache.Width(hFont, L"23:56");
 *
 *  本类不是线程安全的，只在 UI 线程使用。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

#include <windows.h>
#include <map>
#include <string>

namespace balloonmain {

// 缓存条目数的默认上限。会话列表里的时间文字最多约 1440 种（一天中的每一分钟）再加上
// "昨天""星期一"与日期，取 4096 足以装下，又不至于占用太多内存（每条几十字节）。
const size_t kTextWidthCacheMaxEntries = 4096;

// 测量函数：返回 text（长度 len 个字符）在 font 下的像素宽度；测量失败返回 0。
typedef int (*TextWidthMeasurer)(HFONT font, LPCWSTR text, int len);

/**
 *  TextWidthCache —— 按"字体 + 文字"缓存像素宽度。说明见文件头。
 */
class TextWidthCache
{
public:
    /**
     *  构造。
     *    measurer：   测量函数，不可为 NULL。
     *    maxEntries： 条目数上限，超过时整体清空；传 0 按 1 处理。
     */
    explicit TextWidthCache(TextWidthMeasurer measurer,
                            size_t maxEntries = kTextWidthCacheMaxEntries);

    /**
     *  取文字在指定字体下的像素宽度，未缓存时测量并缓存。
     *    font：字体句柄，可为 NULL（此时由测量函数决定用什么字体，缓存照常按 NULL 归组）。
     *    text：文字；NULL 或空串直接返回 0，不调用测量函数。
     *  返回：像素宽度；测量失败返回 0（且不缓存）。
     */
    int Width(HFONT font, LPCWSTR text);

    // 清空全部缓存。
    void Clear();

    // 当前缓存的条目数。
    size_t Count() const;

private:
    TextWidthMeasurer          m_measurer;     // 测量函数，构造时传入
    size_t                     m_maxEntries;   // 条目数上限，超过时整体清空
    HFONT                      m_font;         // 当前缓存条目所属的字体；字体变化时整体作废
    std::map<std::wstring, int> m_widths;      // 文字 → 像素宽度（只含 m_font 下的结果）
};

} // namespace balloonmain
