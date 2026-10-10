/**
 *  FlamingoMarkdown.dll 的字体缓存：整个 DLL 共用一份，按字体描述复用 HFONT 与字体度量。
 *
 *  为什么要全局共用：消息列表里每条 Markdown 消息各有一个文档，一个文档会用到正文、加粗、斜体、代码、各级标题
 *  等多种字体。如果字体随文档创建与释放，消息一多就会很快耗尽进程的 GDI 句柄配额（默认 10000 个）。字体按描述
 *  去重后，整个进程通常只有几十个，缓存里的字体到 DLL 卸载（即进程退出）时才释放。
 *
 *  另外缓存两类逐字符的测量结果，减少 GDI 调用：单个字符的宽度（中文按字排版，同一个字反复出现），以及等宽字体
 *  是否缺少某个字符的字形（Consolas 没有中文字形，而系统字体链接里没有它的条目，GDI 不会自动换字体）。
 *
 *  所有函数都是线程安全的（内部加锁）；按约定只在界面线程上排版，锁几乎不会发生争用。
 *
 *  典型用法：
 *
 *      MdFontKey key(L"Microsoft YaHei", 14, true, false, false, false);
 *      int fontIndex = MdFontCache::Instance().Acquire(hdc, key);
 *      MdFontMetrics m = MdFontCache::Instance().Metrics(fontIndex);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <map>
#include <string>
#include <vector>

namespace flamingomd {

/**
 *  字体描述，也是缓存的键。
 */
struct MdFontKey
{
    std::wstring m_face;        //字体名
    int          m_px;          //字符高度（像素），即 CreateFontW 的 -lfHeight
    bool         m_bold;        //是否加粗
    bool         m_italic;      //是否斜体
    bool         m_underline;   //是否带下划线（链接）
    bool         m_strike;      //是否带删除线

    MdFontKey()
        : m_px(0), m_bold(false), m_italic(false), m_underline(false), m_strike(false)
    {
    }

    MdFontKey(const std::wstring& face, int px, bool bold, bool italic, bool underline, bool strike)
        : m_face(face), m_px(px), m_bold(bold), m_italic(italic), m_underline(underline), m_strike(strike)
    {
    }

    //排序比较，供 std::map 使用
    bool operator<(const MdFontKey& other) const;
};

/**
 *  字体度量（像素）。
 */
struct MdFontMetrics
{
    int m_ascent;    //基线以上的高度
    int m_descent;   //基线以下的高度
    int m_height;    //m_ascent + m_descent

    MdFontMetrics()
        : m_ascent(0), m_descent(0), m_height(0)
    {
    }
};

/**
 *  全局字体缓存。用 Instance() 取唯一实例。
 */
class MdFontCache
{
public:
    /**
     *  取进程内唯一的实例（函数内静态对象，首次调用时构造）。
     */
    static MdFontCache& Instance();

    /**
     *  取字体在缓存中的序号；没有时创建字体并用 hdc 测量度量。
     *  @param hdc 用于测量的设备上下文，不可为 NULL；调用前后其中选中的对象不变
     *  @param key 字体描述
     *  @return 序号（从 0 起），在 DLL 卸载前一直有效；创建字体失败时返回 -1
     */
    int Acquire(HDC hdc, const MdFontKey& key);

    /**
     *  取字体句柄。
     *  @param index Acquire 返回的序号
     *  @return 字体句柄，归缓存所有，调用方不得释放；序号非法时返回 NULL
     */
    HFONT Font(int index) const;

    /**
     *  取字体度量。
     *  @param index Acquire 返回的序号
     *  @return 度量；序号非法时各值为 0
     */
    MdFontMetrics Metrics(int index) const;

    /**
     *  测量一个字符（或一对代理项）的宽度，结果按字体缓存。
     *  @param hdc   设备上下文，其中须已选入该字体
     *  @param index 字体序号
     *  @param text  字符所在位置，不可为 NULL
     *  @param count 字符个数：1，或代理对的 2
     *  @return 宽度（像素）
     */
    int CharWidth(HDC hdc, int index, const wchar_t* text, int count);

    /**
     *  判断字体有没有某个 BMP 字符的字形，结果按字体缓存。
     *  @param hdc   设备上下文，其中须已选入该字体
     *  @param index 字体序号
     *  @param ch    字符
     *  @return 有字形时返回 true
     */
    bool HasGlyph(HDC hdc, int index, wchar_t ch);

    //当前缓存的字体个数，单元测试用
    size_t Count() const;

private:
    MdFontCache();
    ~MdFontCache();
    MdFontCache(const MdFontCache&);
    MdFontCache& operator=(const MdFontCache&);

    /**
     *  缓存里的一种字体。
     */
    struct Entry
    {
        MdFontKey               m_key;       //字体描述
        HFONT                   m_font;      //字体句柄，析构时释放
        MdFontMetrics           m_metrics;   //度量
        std::map<wchar_t, int>  m_widths;    //单个 BMP 字符的宽度
        std::map<wchar_t, bool> m_glyphs;    //BMP 字符是否有字形
    };

    mutable SRWLOCK            m_lock;      //保护以下全部字段
    std::vector<Entry*>        m_entries;   //全部字体，按序号排列，析构时释放
    std::map<MdFontKey, int>   m_index;     //字体描述 → 序号
};

} // namespace flamingomd
