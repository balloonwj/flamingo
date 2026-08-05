/**
 *  汉字 → 完整拼音（全拼）匹配实现，PinyinFull.cpp
 *
 *  查表数据在 PinyinTable.inc（由 tools/gen_pinyin_table.py 从 Unihan 生成），
 *  接口说明见 PinyinFull.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用，在前者里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#include "PinyinFull.h"

#include "PinyinTable.inc"

namespace pinyin {

namespace {

/**
 *  ASCII 字母 / 数字直接参与全拼匹配（视作长度 1 的音节），使"张三abc"这类
 *  中英混排的名字也能被 "zhangsanabc" 整串搜到。
 *  @param ch 待判字符
 *  @return 是字母或数字返回 true
 */
bool IsAsciiAlnum(wchar_t ch)
{
    return (ch >= L'0' && ch <= L'9')
        || (ch >= L'a' && ch <= L'z')
        || (ch >= L'A' && ch <= L'Z');
}

/**
 *  ASCII 字母转小写；非字母原样返回。
 */
char ToLowerAscii(wchar_t ch)
{
    if (ch >= L'A' && ch <= L'Z')
    {
        return static_cast<char>(ch - L'A' + L'a');
    }
    return static_cast<char>(ch);
}

} // namespace

std::string FullPinyinOf(wchar_t ch)
{
    if (ch < table::kFirstChar || ch > table::kLastChar)
    {
        return std::string();
    }

    const unsigned short idx = table::kSyllableIndex[ch - table::kFirstChar];
    if (idx == table::kNoSyllable)
    {
        return std::string();
    }
    return std::string(table::kSyllables[idx]);
}

std::vector<std::string> SyllablesOf(const std::wstring& text)
{
    std::vector<std::string> syllables;
    syllables.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t ch = text[i];
        if (IsAsciiAlnum(ch))
        {
            syllables.push_back(std::string(1, ToLowerAscii(ch)));
            continue;
        }

        //汉字取音节；表外字与标点、空格一律产出空音节占位，既不参与匹配，
        //也会打断跨越它的连续匹配（见头文件里对"张三 李四"的说明）
        syllables.push_back(FullPinyinOf(ch));
    }
    return syllables;
}

std::string InitialsOf(const std::wstring& text)
{
    //占位符：既占住 1:1 的位置，又不会与正常查询命中（与 PinyinInitials 同一约定）
    const char kPlaceholder = '\x01';

    std::string initials;
    initials.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t ch = text[i];
        if (IsAsciiAlnum(ch))
        {
            initials += ToLowerAscii(ch);
            continue;
        }

        const std::string syllable = FullPinyinOf(ch);
        initials += syllable.empty() ? kPlaceholder : syllable[0];
    }
    return initials;
}

bool MatchesFullPinyin(const std::wstring& text, const std::string& queryLower)
{
    if (queryLower.empty() || text.empty())
    {
        return false;
    }

    //查询词里但凡有非 ASCII 字符（如用户直接输入了汉字），走全拼没有意义，
    //交给调用方的原文子串匹配去处理
    for (size_t i = 0; i < queryLower.size(); ++i)
    {
        if (static_cast<unsigned char>(queryLower[i]) > 0x7F)
        {
            return false;
        }
    }

    const std::vector<std::string> syllables = SyllablesOf(text);

    //从每个音节的开头各试一次 —— 查询词允许"从中间某个字起头"（muchen 命中
    //苏沐辰），但不允许从音节内部起头（umuch 不命中）
    for (size_t start = 0; start < syllables.size(); ++start)
    {
        if (syllables[start].empty())
        {
            continue;   // 空音节（标点 / 表外字）不能作为匹配起点
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
                //查询词在这个音节中途用完：要求它是该音节的前缀（"much" 命中 chen）
                if (syllable.compare(0, remain, queryLower, matched, remain) == 0)
                {
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
                return true;   // 查询词正好在音节边界处用完
            }
        }
    }
    return false;
}

} // namespace pinyin
