/**
 *  联系人搜索匹配器（客户端搜索用）。
 *
 *  给定查询词与某联系人的昵称/账号/地区，判断是否命中，并在命中昵称（含拼音
 *  命中）时给出昵称内的高亮字符区间，供结果行黄底高亮。匹配规则：大小写不敏感
 *  子串；优先级 昵称 > 账号 > 地区 > 昵称拼音首字母 > 昵称全拼。
 *
 *  两档拼音的分工：首字母档让 smc 搜到"苏沐辰"，全拼档让 sumuchen / sumu /
 *  muchen 也搜到同一个人；两档都只对纯 ASCII 查询词生效（用户直接敲汉字时该由
 *  前面的原文子串匹配去处理）。
 *
 *  纯逻辑，只依赖 PinyinFull，不触及 UI，便于单元测试，见
 *  flamingoclient/Source/Tests/ContactMatchTests.cpp。
 *
 *  两套入口：
 *    · Match         —— 一次性匹配，每次调用都现算名字的小写形式、首字母串与音节序列，
 *                       适合候选不多、只匹配一两次的场合。
 *    · MatchPrepared —— 先用 PrepareQuery / PrepareName / ToLowerAscii 把查询词与候选
 *                       各自预处理好，再逐个匹配。候选的预处理结果只取决于候选本身，
 *                       数据不变就可以在多次按键之间复用，适合"几千个候选、每按一次键
 *                       都要全部重新匹配"的搜索框。两者的匹配档位、优先级与高亮区间
 *                       完全一致，由单元测试 CMP1 逐对比对保证。
 *
 *  balloonwj@qq.com   2026-06-01
 */
#pragma once

#include <string>
#include <vector>

namespace contactsearch {

//命中的字段类型。取值一经发布不再变动，新增档位一律追加在末尾。
enum MatchField
{
    MATCH_NONE        = 0,   //未命中
    MATCH_NAME        = 1,   //命中昵称子串
    MATCH_ACCOUNT     = 2,   //命中账号子串
    MATCH_REGION      = 3,   //命中地区子串
    MATCH_PINYIN      = 4,   //命中昵称的拼音首字母子串（如 smc 命中"苏沐辰"）
    MATCH_PINYIN_FULL = 5,   //命中昵称的全拼（如 sumuchen / sumu / muchen 命中"苏沐辰"）
};

//匹配结果。m_matched 为 true 时其余字段有效。
struct MatchResult
{
    bool m_matched;      //是否命中
    int  m_field;        //命中字段，取值见 MatchField
    int  m_nameHiStart;  //昵称内高亮起始字符下标（命中昵称/两档拼音时有效，否则 -1）
    int  m_nameHiLen;    //昵称内高亮字符长度（同上）

    MatchResult()
        : m_matched(false)
        , m_field(MATCH_NONE)
        , m_nameHiStart(-1)
        , m_nameHiLen(0)
    {
    }
};

/**
 *  判断查询词是否命中某联系人。
 *
 *  依次尝试五档，先命中者胜出：昵称子串 → 账号子串 → 地区子串 → 昵称拼音首字母
 *  子串 → 昵称全拼。全拼档的语义见 PinyinFull.h：查询词必须从某个字的音节开头起
 *  沿音节顺序连续匹配，允许在最后一个音节中途结束（"sumu" 命中"苏沐辰"），从音节
 *  内部起头的"umuch"则不命中。
 *
 *  @param query    查询词（UTF-16，调用方应已去首尾空白）；空串视为不命中
 *  @param nickname 昵称（UTF-16）
 *  @param account  账号/用户名（UTF-16）
 *  @param region   所在地区（UTF-16）
 *  @return 匹配结果；命中昵称或两档拼音时 m_nameHiStart/m_nameHiLen 给出昵称内高亮
 *          区间。首字母档的区间就是首字母串上的命中区间（首字母串与昵称 1:1 等长）；
 *          全拼档的区间按"查询词覆盖了从第几个字到第几个字"给，只被覆盖了半个音节
 *          的末字也算整字高亮（如 "much" 命中"苏沐辰"时高亮"沐辰"两个字）
 */
MatchResult Match(const std::wstring& query, const std::wstring& nickname,
                  const std::wstring& account, const std::wstring& region);

//预处理过的查询词。每次按键算一次，在全部候选上复用。
struct PreparedQuery
{
    std::wstring m_lower;   //查询词转小写（只把 ASCII 大写字母转小写，其余字符不变）
    std::string  m_ascii;   //查询词为纯 ASCII 时取 m_lower 的窄串形式；含非 ASCII 字符（如汉字）
                            //或查询词为空时为空串，此时拼音两档不参与匹配
};

//预处理过的名字（联系人显示名、群名等）。只取决于名字本身，名字不变就可以一直复用。
struct PreparedName
{
    std::wstring             m_lower;      //名字转小写（规则同 PreparedQuery::m_lower）
    std::string              m_initials;   //拼音首字母串，即 pinyin::InitialsOf(名字)，与名字 1:1 等长
    std::vector<std::string> m_syllables;  //音节序列，即 pinyin::SyllablesOf(名字)，与名字 1:1 等长
};

/**
 *  把宽字符串里的 ASCII 大写字母转小写，其余字符（含汉字）原样保留。
 *  预处理账号、地区时用它，结果作为 MatchPrepared 的 accountLower / regionLower。
 *  @param s 源串（UTF-16）
 *  @return 转换后的新串
 */
std::wstring ToLowerAscii(const std::wstring& s);

/**
 *  预处理查询词。
 *  @param query 查询词（UTF-16，调用方应已去首尾空白）
 *  @return 预处理结果；query 为空时两个字段都为空
 */
PreparedQuery PrepareQuery(const std::wstring& query);

/**
 *  预处理一个名字：转小写，并算好拼音首字母串与音节序列。
 *  @param name 名字（UTF-16），可以为空
 *  @return 预处理结果
 */
PreparedName PrepareName(const std::wstring& name);

/**
 *  用预处理好的查询词与候选做匹配。档位、优先级与高亮区间与 Match 完全一致：
 *  MatchPrepared(PrepareQuery(q), PrepareName(n), ToLowerAscii(a), ToLowerAscii(r))
 *  与 Match(q, n, a, r) 的返回值逐字段相同。本函数不分配内存、不查拼音表。
 *
 *  @param query        PrepareQuery 的结果
 *  @param name         候选名字的 PrepareName 结果
 *  @param accountLower 候选账号经 ToLowerAscii 转换后的结果；没有账号时传空串
 *  @param regionLower  候选地区经 ToLowerAscii 转换后的结果；没有地区时传空串
 *  @return 匹配结果，字段含义同 Match
 */
MatchResult MatchPrepared(const PreparedQuery& query, const PreparedName& name,
                          const std::wstring& accountLower, const std::wstring& regionLower);

} // namespace contactsearch
