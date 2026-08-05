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
 *  balloonwj@qq.com   2026-06-01
 */
#pragma once

#include <string>

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

} // namespace contactsearch
