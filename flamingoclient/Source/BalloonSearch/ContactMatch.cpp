/**
 *  联系人搜索匹配器实现，ContactMatch.cpp
 *
 *  匹配档位与各档的高亮口径见 ContactMatch.h，拼音两档的语义见 PinyinFull.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用，在前者里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-06-01
 */
#include "ContactMatch.h"
#include "PinyinFull.h"

#include <vector>

namespace contactsearch {

//ASCII 码点上界（含）。查询词里出现比它大的字符，说明用户直接敲了汉字之类的内容，
//两档拼音路径都不适用，该由前面的原文子串匹配去处理。
static const wchar_t kAsciiMaxCodePoint = 0x7F;

//把宽字符串里的 ASCII 大写字母转小写（CJK 等非 ASCII 不变），用于大小写不敏感匹配。
static std::wstring ToLowerAscii(const std::wstring& s)
{
    std::wstring out = s;
    for (size_t i = 0; i < out.size(); ++i)
    {
        wchar_t c = out[i];
        if (c >= L'A' && c <= L'Z')
        {
            out[i] = (wchar_t)(c - L'A' + L'a');
        }
    }
    return out;
}

/**
 *  在昵称的音节序列上定位全拼命中区间，供高亮使用。
 *
 *  匹配语义与 pinyin::MatchesFullPinyin 保持一致（须从某个音节的开头起沿音节顺序
 *  连续匹配，允许在最后一个音节中途结束），区别是本函数还要回报"查询词覆盖了从第
 *  几个字到第几个字"。pinyin::SyllablesOf 产出的音节数组与昵称逐字符一一对应，故
 *  音节下标可直接当作昵称的字符下标用。查询词在某个音节中途用完时，那个字只被覆盖
 *  了一半，仍按整字计入高亮 —— 高亮的最小单位是字，切不开。
 *
 *  @param nickname   昵称（UTF-16）
 *  @param queryLower 查询词，须为已转小写的纯 ASCII 窄串
 *  @param hiStart    出参，命中时填入昵称内的高亮起始字符下标
 *  @param hiLen      出参，命中时填入高亮覆盖的字符个数
 *  @return 找到命中区间返回 true；返回 false 时两个出参保持原值不动
 */
static bool FindFullPinyinRange(const std::wstring& nickname, const std::string& queryLower,
                                int& hiStart, int& hiLen)
{
    const std::vector<std::string> syllables = pinyin::SyllablesOf(nickname);

    //从每个音节的开头各试一次：允许"从中间某个字起头"，但不允许从音节内部起头
    for (size_t start = 0; start < syllables.size(); ++start)
    {
        if (syllables[start].empty())
        {
            continue;   // 空音节（标点 / 空格 / 表外字）不能作为匹配起点
        }

        size_t matched = 0;   // 查询词已匹配掉的字符数
        for (size_t i = start; i < syllables.size(); ++i)
        {
            const std::string& syllable = syllables[i];
            if (syllable.empty())
            {
                break;   // 遇到占位符：连续匹配到此中断
            }

            const size_t remain = queryLower.size() - matched;
            if (remain < syllable.size())
            {
                //查询词在这个音节中途用完：要求它是该音节的前缀，第 i 个字整字高亮
                if (syllable.compare(0, remain, queryLower, matched, remain) == 0)
                {
                    hiStart = (int)start;
                    hiLen   = (int)(i - start + 1);
                    return true;
                }
                break;
            }

            //整个音节都要对得上，否则这个起点不成立
            if (queryLower.compare(matched, syllable.size(), syllable) != 0)
            {
                break;
            }
            matched += syllable.size();
            if (matched == queryLower.size())
            {
                //查询词正好在音节边界处用完，覆盖 start 到 i 这几个字
                hiStart = (int)start;
                hiLen   = (int)(i - start + 1);
                return true;
            }
        }
    }
    return false;
}

MatchResult Match(const std::wstring& query, const std::wstring& nickname,
                  const std::wstring& account, const std::wstring& region)
{
    MatchResult r;

    //空查询：不命中（由调用方决定空查询时展示什么）。
    if (query.empty())
    {
        return r;
    }

    std::wstring q = ToLowerAscii(query);

    //1. 昵称子串（命中即给出昵称内高亮区间）。
    std::wstring nick = ToLowerAscii(nickname);
    std::wstring::size_type pos = nick.find(q);
    if (pos != std::wstring::npos)
    {
        r.m_matched     = true;
        r.m_field       = MATCH_NAME;
        r.m_nameHiStart = (int)pos;
        r.m_nameHiLen   = (int)q.size();
        return r;
    }

    //2. 账号子串（不在昵称上高亮）。
    if (ToLowerAscii(account).find(q) != std::wstring::npos)
    {
        r.m_matched = true;
        r.m_field   = MATCH_ACCOUNT;
        return r;
    }

    //3. 地区子串。
    if (ToLowerAscii(region).find(q) != std::wstring::npos)
    {
        r.m_matched = true;
        r.m_field   = MATCH_REGION;
        return r;
    }

    //把查询词转成窄串（仅取其 ASCII 字节）。含非 ASCII 时两档拼音都无意义，
    //到此为止，直接按不命中返回。
    std::string qn;
    qn.reserve(q.size());
    bool queryAsciiOnly = true;
    for (size_t i = 0; i < q.size(); ++i)
    {
        wchar_t c = q[i];
        if (c > kAsciiMaxCodePoint)
        {
            queryAsciiOnly = false;
            break;
        }
        qn.push_back((char)c);
    }
    if (!queryAsciiOnly || qn.empty())
    {
        return r;
    }

    //4. 昵称拼音首字母子串。首字母串与昵称 1:1 等长，故命中下标即昵称字符下标。
    //   这里用 PinyinFull 的 InitialsOf 而不是 PinyinInitials 的 Initials —— 后者只
    //   覆盖 GB2312 一级汉字，二级字取不到首字母（"苏沐辰"的"沐"区位码 6769 就在覆盖
    //   之外，会让 smc 搜不到这个人）；前者走 Unihan 全表没有这个缺口，且同样保持
    //   1:1 等长，所以下面的区间映射方式不用变。InitialsOf 的产出已是小写，无需再转。
    std::string initials = pinyin::InitialsOf(nickname);
    std::string::size_type ppos = initials.find(qn);
    if (ppos != std::string::npos)
    {
        r.m_matched     = true;
        r.m_field       = MATCH_PINYIN;
        r.m_nameHiStart = (int)ppos;
        r.m_nameHiLen   = (int)qn.size();
        return r;
    }

    //5. 昵称全拼。规则见 PinyinFull.h：查询词须从某个字的音节开头起连续匹配，允许
    //   在最后一个音节中途结束（sumuchen / sumu / muchen 都命中"苏沐辰"，而从音节
    //   内部起头的 umuch 不命中）。命中与否以 MatchesFullPinyin 为准，高亮区间再由
    //   FindFullPinyinRange 在同样的音节序列上算出来。
    if (pinyin::MatchesFullPinyin(nickname, qn))
    {
        r.m_matched = true;
        r.m_field   = MATCH_PINYIN_FULL;
        FindFullPinyinRange(nickname, qn, r.m_nameHiStart, r.m_nameHiLen);
        return r;
    }

    return r;
}

} // namespace contactsearch
