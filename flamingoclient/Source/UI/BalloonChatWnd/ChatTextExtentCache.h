/**
 *  文字测量结果缓存：缓存 GetTextExtentPoint32W 与 GetTextExtentExPointW 两个 GDI 调用的返回值。
 *
 *  聊天气泡与消息记录的「文字 + 内联表情」排版（ChatInlineText.cpp）是逐个词元测宽的：
 *  中文每个字单独成一个词元，一条消息几十个字就是几十次 GDI 调用；选区模型还要再逐段
 *  调一次 GetTextExtentExPointW。实测打开聊天窗时 50 条消息的首次测量约 46 毫秒，其中
 *  纯折行排版约 26 毫秒（FlamingoUnit "CPF4*"），而建字体、取屏幕 DC 合计只占约 1.3 毫秒。
 *  同一字体下同一段文字的测量结果是确定的，常用汉字与常见单词又高度重复，缓存命中后
 *  省掉的就是这些 GDI 调用。
 *
 *  缓存的是 GDI 调用的原始返回值，键为「当前选入设备上下文的字体 + 映射模式 + 文本」，
 *  因此命中与未命中得到的结果完全相同，排版结果不因缓存而改变（ChatTextExtentCacheTests 验证）。
 *
 *  典型用法（调用前已把目标字体选入 hdc）：
 *      const TextExtentFontKey key = TextExtentFontKey::FromDc(hdc);   // 每次排版取一次
 *      SIZE sz = ChatTextExtentCache::Inst().TextExtent(hdc, key, text, len);
 *
 *  <u>本文件被 FlamingoUnit 测试工程直接编译</u>，故 .cpp 不走主工程的预编译头。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

#include <windows.h>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace balloonchatwnd {

/**
 *  文字测量缓存的字体身份。取自当前选入设备上下文的字体的 LOGFONT，外加设备上下文的
 *  映射模式（映射模式不同时逻辑单位不同，测出的宽度也不同）。
 */
struct TextExtentFontKey
{
    //字体高度（LOGFONT::lfHeight，负数表示按字符高度取像素）
    LONG         m_nHeight;
    //平均字符宽度（LOGFONT::lfWidth，0 表示按高度自动取）
    LONG         m_nWidth;
    //字重（LOGFONT::lfWeight）
    LONG         m_nWeight;
    //是否斜体
    BYTE         m_bItalic;
    //是否下划线
    BYTE         m_bUnderline;
    //是否删除线
    BYTE         m_bStrikeOut;
    //字符集
    BYTE         m_nCharSet;
    //输出质量（ClearType 等）
    BYTE         m_nQuality;
    //设备上下文的映射模式（GetMapMode）
    int          m_nMapMode;
    //字体名
    std::wstring m_strFaceName;

    TextExtentFontKey();

    /**
     *  取 hdc 当前选入的字体与映射模式作为缓存键。
     *  @param hdc 已选好目标字体的设备上下文
     *  @return 缓存键；取不到字体信息时各字段保持默认值（同样可用作键，只是不同字体会共用它，
     *          实际上选入字体后不会取不到）
     */
    static TextExtentFontKey FromDc(HDC hdc);

    bool operator<(const TextExtentFontKey& rhs) const;
};

/**
 *  文字测量结果的进程级缓存，按字体分桶。所有方法线程安全（内部加锁），
 *  实际只在 UI 线程上排版时使用。
 */
class ChatTextExtentCache
{
public:
    //每个字体桶最多缓存的条目数（两种测量合计）。超出后清空该桶重新积累。
    //常用汉字约三千五百个，再加上常见单词与标点，一万六千条足以覆盖日常会话，
    //单条约几十字节，一个桶的上限约一兆字节
    static const size_t kMaxEntriesPerFont = 16384;
    //最多保留的字体桶数。超出后清空全部缓存。气泡正文的字号、粗体、斜体组合有限，
    //实际用到的字体远少于这个数
    static const size_t kMaxFontBuckets = 64;

    //取进程唯一的实例
    static ChatTextExtentCache& Inst();

    /**
     *  等价于 ::GetTextExtentPoint32W(hdc, text, len, &size)，命中缓存时不调用 GDI。
     *  @param hdc   已选入与 key 对应字体的设备上下文
     *  @param key   TextExtentFontKey::FromDc(hdc) 的结果
     *  @param text  待测文字，不要求以 '\0' 结尾；len 为 0 时可以为 NULL
     *  @param len   字符数
     *  @return 文字的宽高（逻辑单位）；GDI 调用失败时为 {0, 0}，且不写入缓存
     */
    SIZE TextExtent(HDC hdc, const TextExtentFontKey& key, LPCWSTR text, int len);

    /**
     *  等价于 ::GetTextExtentExPointW(hdc, text, len, nMaxExtent, &fit, dx, &size) 得到的
     *  逐字符累计宽度数组 dx（dx[k] 为前 k+1 个字符的总宽），命中缓存时不调用 GDI。
     *  @param hdc        已选入与 key 对应字体的设备上下文
     *  @param key        TextExtentFontKey::FromDc(hdc) 的结果
     *  @param text       待测文字
     *  @param len        字符数
     *  @param nMaxExtent 传给 GDI 的最大宽度；只有放得下的字符会被回填，其余保持 0
     *  @param outDx      输出，长度为 len；GDI 调用失败时全为 0，且不写入缓存
     */
    void CharExtents(HDC hdc, const TextExtentFontKey& key, LPCWSTR text, int len,
                     int nMaxExtent, std::vector<INT>& outDx);

    /**
     *  与 CharExtents 取同一份结果，但不产出中间数组，而是把每个字符的右边界
     *  （base + dx[k]）直接追加到 outRights 末尾。选区模型逐段调用它，省掉每段一次的
     *  临时数组分配（在调试版运行库下这类小块分配的开销与 GDI 调用相当）。
     *  @param base      本段左边界，追加的每个值都加上它
     *  @param outRights 追加 len 个值；GDI 调用失败时追加 len 个 base（与逐字符宽度全为 0 时一致）
     *  其余参数同 CharExtents。
     */
    void AppendCharRights(HDC hdc, const TextExtentFontKey& key, LPCWSTR text, int len,
                          int nMaxExtent, int base, std::vector<int>& outRights);

    /**
     *  打开或关闭缓存。关闭时两个测量函数直接调用 GDI、不读不写缓存。
     *  供单元测试对比开关前后的排版结果；产品代码不调用，默认打开。
     */
    void SetEnabled(bool bEnabled);

    //清空全部缓存
    void Clear();

    //当前缓存的条目总数（全部字体桶、两种测量合计），供单元测试核对上限
    size_t EntryCount() const;

private:
    ChatTextExtentCache();
    ChatTextExtentCache(const ChatTextExtentCache&);
    ChatTextExtentCache& operator=(const ChatTextExtentCache&);

    //同一字体下的两种测量结果
    struct FontBucket
    {
        //文字 -> GetTextExtentPoint32W 的结果
        std::unordered_map<std::wstring, SIZE>             m_extents;
        //最大宽度 -> (文字 -> GetTextExtentExPointW 的逐字符累计宽度)。最大宽度放在外层而不是
        //拼进文字键里，是为了让单个汉字这样的短文字键不超出 std::wstring 的小字符串长度，
        //查找时不必分配内存。实际只用到一种最大宽度，外层只有一项
        std::map<int, std::unordered_map<std::wstring, std::vector<INT> > > m_charExtents;
        //m_charExtents 里的条目总数（各个最大宽度合计），用于判断是否超出上限
        size_t m_nCharExtentCount;

        FontBucket() : m_nCharExtentCount(0) {}
    };

    //逐字符累计宽度交给调用方的方式
    enum CharExtentSink
    {
        //原样复制到 pCopy（CharExtents 用）
        kSinkCopy,
        //每项加上 base 后追加到 pRights 末尾（AppendCharRights 用）
        kSinkAppendRights
    };

    //按 sink 把一份逐字符累计宽度交给调用方
    static void EmitCharExtents(const std::vector<INT>& dx, CharExtentSink sink, int base,
                                std::vector<INT>* pCopy, std::vector<int>* pRights);

    //查找或测量 text 的逐字符累计宽度，并按 sink 交给调用方。命中缓存时在持锁状态下
    //直接从缓存里读出，不复制中间结果，也不会读到被其它线程同时清掉的数据
    void ReadCharExtents(HDC hdc, const TextExtentFontKey& key, LPCWSTR text, int len,
                         int nMaxExtent, CharExtentSink sink, int base,
                         std::vector<INT>* pCopy, std::vector<int>* pRights);

    //取 key 对应的桶，不存在时新建；桶数超过上限时先清空全部。调用方须已持有 m_mutex
    FontBucket& BucketFor(const TextExtentFontKey& key);

    //按字体分桶的缓存，受 m_mutex 保护
    std::map<TextExtentFontKey, FontBucket> m_buckets;
    //保护 m_buckets 与 m_bEnabled
    mutable std::mutex                      m_mutex;
    //缓存是否打开，默认打开
    bool                                    m_bEnabled;
};

} // namespace balloonchatwnd
