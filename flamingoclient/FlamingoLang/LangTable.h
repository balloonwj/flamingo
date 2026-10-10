/**
 *  一种语言的译文查找表。
 *
 *  由 .po 的解析结果建表，按「上下文 + 原文」查找译文。查找时直接对调用方传入的 wchar_t* 计算 64 位 FNV-1a
 *  哈希、按哈希查索引，命中后逐字比对上下文与原文，全程不分配内存（0b 步实测：Debug 1.2 微秒、Release 0.25 微秒，
 *  见 docs/多语言方案.md 第 5.5 节）。同一哈希值可以挂多条，哈希冲突不会导致查错。
 *
 *  建表之后表内容不再改变：Lookup 可以在多个线程并发调用，返回的指针在本对象销毁前一直有效。
 *
 *  典型用法：
 *      flamingolang::CLangTable table;
 *      table.Build(parsed);
 *      const wchar_t* text = table.Lookup(NULL, L"记住密码");   // NULL 表示没有译文
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <unordered_map>

#include "PoParser.h"

namespace flamingolang {

/**
 *  查找用的哈希函数类型。上下文为 NULL 与为空串必须得到相同的结果。
 *  @param context 上下文，可为 NULL
 *  @param source  原文，不为 NULL
 *  @return 64 位哈希值
 */
typedef uint64_t (*LangHashFn)(const wchar_t* context, const wchar_t* source);

/**
 *  默认哈希：64 位 FNV-1a。有上下文时先算上下文，再算分隔符 0x0004（与 gettext 拼接 msgctxt 的约定相同），
 *  最后算原文；没有上下文时只算原文。
 */
uint64_t DefaultLangHash(const wchar_t* context, const wchar_t* source);

/**
 *  一种语言的译文查找表。不可复制。
 */
class CLangTable
{
public:
    /**
     *  @param hashFn 哈希函数；传 NULL 时使用 DefaultLangHash。单元测试用它注入故意冲突的哈希
     */
    explicit CLangTable(LangHashFn hashFn = NULL);

    CLangTable(const CLangTable&) = delete;
    CLangTable& operator=(const CLangTable&) = delete;

    /**
     *  由解析结果建表。只能调用一次；再次调用会先清空已有内容。
     *  原文或译文不是合法 UTF-8 的条目跳过并计入错误数；同一「上下文 + 原文」出现多次时保留第一条，
     *  其余计入警告数。
     *  @param parsed ParsePo 的结果；其中的坏条目数与空译文数不计入本表
     */
    void Build(const PoParseResult& parsed);

    /**
     *  查找译文。
     *  @param context 上下文，可为 NULL；NULL 与空串等价
     *  @param source  原文；为 NULL 时返回 NULL
     *  @return 译文，归本对象所有；没有这条译文时返回 NULL
     */
    const wchar_t* Lookup(const wchar_t* context, const wchar_t* source) const;

    //有效译文条数。
    uint32_t GetEntryCount() const;

    //建表时跳过的条目数（UTF-8 编码错误）。
    uint32_t GetErrorCount() const;

    //建表时的警告数（重复条目）。
    uint32_t GetWarningCount() const;

private:
    //一条译文。三个字符串在建表后不再改变，Lookup 返回的是 m_text 的内部缓冲区。
    struct Item
    {
        std::wstring m_context;     //上下文，没有时为空串
        std::wstring m_source;      //原文
        std::wstring m_text;        //译文
    };

    //在已建好的条目里查找，找到返回下标，找不到返回 -1。
    int FindIndex(uint64_t hash, const wchar_t* context, const wchar_t* source) const;

    LangHashFn                                  m_hashFn;    //查找与建表共用的哈希函数，构造后不变
    std::vector<Item>                           m_items;     //全部译文；建表时预先分配容量，建表后不再增删
    std::unordered_multimap<uint64_t, uint32_t> m_index;     //哈希值 -> m_items 下标；同一哈希可对应多条
    uint32_t                                    m_errors;    //建表时跳过的条目数
    uint32_t                                    m_warnings;  //建表时的警告数
};

} // namespace flamingolang
