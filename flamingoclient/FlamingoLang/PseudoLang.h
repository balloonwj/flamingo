/**
 *  伪语言（qps-ploc）：把原文加上括号并拉长，用来在界面上发现漏标记与被截断的文字。
 *
 *  伪译文的格式为「«原文 + 填充»」：原文中每个宽字符（码位 >= U+2E80，汉字、假名、谚文与全角符号都在此列）
 *  补两个 '~'，使渲染宽度约为原文的 2 倍，与 0a 步实测的英文平均宽度相当（docs/多语言方案.md 第 8.5.2 节）。
 *  原文本身不做任何改动，因此 {0} 占位符与 & 助记符原样保留。
 *  界面上没有括号的文字就是漏标记的，右括号看不见的就是被截断的。
 *
 *  伪译文在第一次查找时生成并缓存，同一原文再次查找返回同一指针，指针在本对象销毁前一直有效。
 *  Lookup 可以在多个线程并发调用（内部加锁）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace flamingolang {

/**
 *  伪语言的译文生成与缓存。不可复制。
 */
class CPseudoLang
{
public:
    CPseudoLang();
    ~CPseudoLang();

    CPseudoLang(const CPseudoLang&) = delete;
    CPseudoLang& operator=(const CPseudoLang&) = delete;

    /**
     *  取伪译文。上下文不影响结果。
     *  @param context 上下文，可为 NULL（不使用）
     *  @param source  原文；为 NULL 时返回 NULL
     *  @return 伪译文，归本对象所有
     */
    const wchar_t* Lookup(const wchar_t* context, const wchar_t* source);

    //已生成的伪译文条数。
    uint32_t GetEntryCount() const;

    /**
     *  清空缓存。<u>只供单元测试使用</u>：调用后此前返回的指针全部失效，调用时不得有其它线程正在查找。
     */
    void Clear();

    /**
     *  按规则生成一条伪译文，不缓存。
     *  @param source 原文，不为 NULL
     *  @return 伪译文
     */
    static std::wstring MakePseudoText(const wchar_t* source);

private:
    mutable CRITICAL_SECTION                                        m_lock;     //保护 m_cache，构造时初始化、析构时删除
    std::unordered_map<std::wstring, std::unique_ptr<std::wstring> > m_cache;   //原文 -> 伪译文；伪译文单独分配，地址不随哈希表扩容改变
};

} // namespace flamingolang
