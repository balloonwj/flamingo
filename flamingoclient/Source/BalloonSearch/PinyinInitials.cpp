/**
 *  汉字 → 拼音首字母 转换实现，PinyinInitials.cpp
 *  balloonwj@qq.com   2026-06-01
 */
#include "PinyinInitials.h"
#include <windows.h>

namespace pinyin {

//GBK/GB2312 代码页
static const UINT kCodePageGBK = 936;
//GB2312 双字节字符的两个字节都 >= 0xA1
static const unsigned char kGbLeadMin = 0xA1;
//区位码偏移基准：区位码 = (高字节-0xA0)*100 + (低字节-0xA0)
static const int kGbByteBase = 0xA0;
//一级常用汉字区位码上界（"座"约 5589），超出视为未覆盖
static const int kMaxLevel1Quwei = 5589;
//未覆盖/无首字母时的占位符（不会与正常查询命中）
static const char kPlaceholder = '\x01';

//GB2312 一级汉字按拼音首字母分段的区位码起点表（23 个字母，无 i/u/v 开头拼音）。
//每个字母的区位码区间为 [kQuweiStart[i], kQuweiStart[i+1]-1]，最后一个到 kMaxLevel1Quwei。
//这是汉语拼音排序的标准边界表（公开资料），数据本身即含义，不可拆成"魔数"。
static const int kQuweiStart[] =
{
    1601, 1637, 1833, 2078, 2274, 2302, 2433, 2594, 2787, 3106,
    3212, 3472, 3635, 3722, 3730, 3858, 4027, 4086, 4390, 4558,
    4684, 4925, 5249
};
static const char kInitialLetter[] =
{
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'j', 'k',
    'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'w',
    'x', 'y', 'z'
};
static const int kLetterCount = (int)(sizeof(kInitialLetter) / sizeof(kInitialLetter[0]));

//把一个 GBK 双字节汉字映射成拼音首字母；未覆盖返回占位符。
static char HanziInitial(unsigned char b0, unsigned char b1)
{
    if (b0 < kGbLeadMin || b1 < kGbLeadMin)
    {
        return kPlaceholder;
    }
    int quwei = (b0 - kGbByteBase) * 100 + (b1 - kGbByteBase);
    if (quwei < kQuweiStart[0] || quwei > kMaxLevel1Quwei)
    {
        return kPlaceholder;
    }
    for (int i = 0; i < kLetterCount; ++i)
    {
        int end = (i + 1 < kLetterCount) ? (kQuweiStart[i + 1] - 1) : kMaxLevel1Quwei;
        if (quwei >= kQuweiStart[i] && quwei <= end)
        {
            return kInitialLetter[i];
        }
    }
    return kPlaceholder;
}

std::string Initials(const std::wstring& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        wchar_t wc = text[i];

        //ASCII：字母转小写，其余原样（数字/标点保持 1:1，不参与字母匹配）
        if (wc < 0x80)
        {
            char c = (char)wc;
            if (c >= 'A' && c <= 'Z')
            {
                c = (char)(c - 'A' + 'a');
            }
            out.push_back(c);
            continue;
        }

        //非 ASCII：转 GBK 取字节，双字节按区位码映射首字母，否则占位。
        char buf[8] = { 0 };
        int n = ::WideCharToMultiByte(kCodePageGBK, 0, &wc, 1, buf, (int)sizeof(buf), NULL, NULL);
        if (n == 2)
        {
            out.push_back(HanziInitial((unsigned char)buf[0], (unsigned char)buf[1]));
        }
        else
        {
            out.push_back(kPlaceholder);
        }
    }
    return out;
}

} // namespace pinyin
