/**
 *  敏感词匹配引擎：归一化、多模式匹配与内容摘要。SensitiveWordMatcher.h
 *
 *  只依赖标准库，不碰数据库与网络，便于单元测试。词库的装载、按企业缓存、写拦截记录在
 *  SensitiveWordManager 里；本类只负责"给定一组词条，判断一段文字有没有命中"。
 *
 *  匹配前，词条与待检查的文字都先按同一套规则归一化（规则见 Normalize 的说明，完整取舍见
 *  docs/敏感词方案.md 第 4.2 节）：统一 ASCII 大小写与全角半角，去掉空白、标点符号、各类符号
 *  与表情，再做子串匹配。匹配用 Aho-Corasick 自动机，对待检查的文字只扫描一遍，耗时与文字
 *  长度成正比、与词条数无关。
 *
 *  典型用法：
 *      SensitiveWordMatcher matcher;
 *      matcher.Build(entries);                 //entries 来自某个企业的词库
 *      SensitiveHit hit;
 *      if (matcher.Match(text, &hit))
 *      {
 *          std::string snippet = SensitiveWordMatcher::MakeSnippet(
 *              text, hit.m_beginChar, hit.m_endChar, SENSITIVE_SNIPPET_CONTEXT_CHARS);
 *      }
 *
 *  线程安全：Build 之后对象只读，多个线程可以同时调用 Match；Build 本身不能与 Match 并发。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "SensitiveWordDefs.h"

namespace chatserver {

//一条参与匹配的词条，通常取自 t_sensitive_word 的一行。
struct SensitiveWordEntry
{
    int64_t     m_id;         //词条ID（t_sensitive_word.f_id），命中时原样带回
    std::string m_word;       //词条原文（UTF-8）；Build 时自行归一化，归一化后为空的词条跳过
    int         m_category;   //分类，取值见 sensitive_word_category，命中时原样带回

    SensitiveWordEntry()
        : m_id(0)
        , m_category(sensitive_category_other)
    {
    }
};

//一次命中的结果。
struct SensitiveHit
{
    int64_t  m_wordId;      //命中的词条ID
    int      m_category;    //命中词条的分类，取值见 sensitive_word_category
    //命中位置在<u>原文</u>中的字符区间 [m_beginChar, m_endChar)，按 Unicode 字符计。区间覆盖
    //命中词第一个字到最后一个字之间的全部原文字符，包括被忽略的空白、符号与表情标记，
    //供截取内容摘要用。
    uint32_t m_beginChar;
    uint32_t m_endChar;

    SensitiveHit()
        : m_wordId(0)
        , m_category(sensitive_category_other)
        , m_beginChar(0)
        , m_endChar(0)
    {
    }
};

//CheckWord 的校验结果。
enum sensitive_word_check_result
{
    sensitive_word_check_ok       = 0,   //合格，可以入库
    sensitive_word_check_empty    = 1,   //为空，或去掉空白、符号与表情之后为空（如 "!!!"）
    sensitive_word_check_too_long = 2,   //去掉首尾空白后超过 SENSITIVE_WORD_MAX_CHARS 个字符
};

//敏感词匹配引擎。一个对象对应一份词库（一个企业），Build 之后只读。
class SensitiveWordMatcher
{
public:
    SensitiveWordMatcher();

    /**
     *  把一段 UTF-8 文字归一化成一串 Unicode 码点，规则依次为：
     *    1. 按 UTF-8 解码，非法字节逐个当作一个字符跳过（不中断、不报错）；
     *    2. 去掉表情标记 /f["数字"]；被转义的 //f[" 按字面文字处理；
     *    3. 全角 U+FF01～U+FF5E 转成对应的半角字符，全角空格 U+3000 视为空白；
     *    4. ASCII 大写字母转小写；
     *    5. 去掉空白、ASCII 中字母数字以外的字符、通用标点、CJK 符号与标点、各类符号与表情等
     *       （完整区间见实现文件 IsIgnoredCodepoint），其余字符（汉字、字母、数字、假名等）保留。
     *  @param utf8          待归一化的文字（UTF-8）
     *  @param codepoints    [出参] 归一化后的码点序列，调用前会被清空
     *  @param origCharIndex [出参，可为 NULL] 与 codepoints 等长，第 i 项是第 i 个码点在原文中的
     *                       字符下标（原文按字符计，非法字节各算一个字符）；调用前会被清空
     */
    static void Normalize(const std::string& utf8, std::vector<uint32_t>& codepoints,
                          std::vector<uint32_t>* origCharIndex);

    /**
     *  校验一个准备入库的词条。先去掉首尾空白再判断。
     *  @param utf8           管理员输入的词条（UTF-8）
     *  @param normalizedUtf8 [出参，可为 NULL] 合格时写入归一化后的形式（UTF-8），供判重与入库；
     *                        不合格时置为空串
     *  @return 取值见 sensitive_word_check_result
     */
    static int CheckWord(const std::string& utf8, std::string* normalizedUtf8);

    /**
     *  统计一段 UTF-8 文字的字符数，非法字节各算一个字符。
     *  @param utf8 文字（UTF-8）
     *  @return 字符数
     */
    static uint32_t CountChars(const std::string& utf8);

    /**
     *  用一组词条构建匹配器，原有内容整体替换。归一化后为空的词条跳过；归一化后相同的
     *  多个词条只保留先出现的那一个。
     *  @param words 词条列表
     *  @return 实际参与匹配的词条数
     */
    size_t Build(const std::vector<SensitiveWordEntry>& words);

    /**
     *  判断一段文字是否命中词库。有多个命中时，取在文字中最先结束的那个；结束位置相同时
     *  取最长的那个。
     *  @param utf8 待检查的文字（UTF-8），可为空串
     *  @param hit  [出参，可为 NULL] 命中时写入命中结果；未命中时不改动
     *  @return 命中返回 true；词库为空、文字为空或未命中返回 false
     */
    bool Match(const std::string& utf8, SensitiveHit* hit) const;

    /**
     *  按字符截取命中位置前后的内容摘要。两端被截断时分别加省略号 "…"；原文里的非法字节
     *  替换为 U+FFFD，保证结果是合法的 UTF-8。
     *  @param utf8         原文（UTF-8）
     *  @param beginChar    命中区间起点（字符下标，含），通常取自 SensitiveHit
     *  @param endChar      命中区间终点（字符下标，不含）；超出原文长度时按原文长度处理
     *  @param contextChars 前后各保留的字符数
     *  @return 摘要（UTF-8）
     */
    static std::string MakeSnippet(const std::string& utf8, uint32_t beginChar, uint32_t endChar,
                                   uint32_t contextChars);

    /**
     *  当前参与匹配的词条数（Build 的返回值）。
     *  @return 词条数
     */
    size_t WordCount() const;

private:
    //一条已入树的词条。
    struct WordInfo
    {
        int64_t  m_id;         //词条ID
        int      m_category;   //分类
        uint32_t m_length;     //归一化后的码点数，用来从结束位置推算起点

        WordInfo()
            : m_id(0)
            , m_category(sensitive_category_other)
            , m_length(0)
        {
        }
    };

    //在 node 的子节点里查找码点 cp 对应的子节点下标；没有时返回 -1。
    int FindChild(int node, uint32_t cp) const;

    //自动机按"结构数组"存放：节点的各项属性各占一个数组，下标即节点编号，0 为根节点；
    //全部出边存进一对连续数组，同一节点的出边按码点升序相邻存放，查找时二分。这样几十万个
    //节点不必各自分配一块内存，建树与查找都更快。各数组在 Build 时整体重建。
    std::vector<int>      m_fail;        //失败指针：节点所代表字符串的最长真后缀所在的节点；根节点为 0
    std::vector<int>      m_nodeWord;    //以本节点结尾的词条在 m_words 里的下标；没有时为 -1
    std::vector<int>      m_output;      //以本节点结尾的最长词条的下标（本节点自身或沿失败指针最近的词尾）；没有时为 -1
    std::vector<uint32_t> m_firstEdge;   //节点第一条出边在 m_edgeCp / m_edgeTarget 里的下标
    std::vector<uint32_t> m_edgeCount;   //节点的出边条数
    std::vector<uint32_t> m_edgeCp;      //出边上的码点
    std::vector<int>      m_edgeTarget;  //出边指向的子节点
    std::vector<WordInfo> m_words;       //已入树的词条，下标与 m_nodeWord / m_output 对应
};

} // namespace chatserver
