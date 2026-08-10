/**
 *  flamingoAdmin —— 汉字拼音首字母工具实现。详见 Util/Pinyin.h。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#include "stdafx.h"
#include "Util/Pinyin.h"

#include <cwctype>

namespace flamingoadmin {

namespace {

// GB2312 简体中文代码页。
const UINT kCodePageGBK = 936;

// 各拼音首字母在 GB2312 区位的"起始 GBK 码"边界(升序),末项为 Z 之后的终止
// 哨兵。GB2312 一级字按拼音大致有序,落在 [kBound[i], kBound[i+1]) 即首字母
// kLetter[i]。跳过 i / u / v(无以其开头的常用拼音首字母分区)。
const unsigned kBound[] = {
    0xB0A1, 0xB0C5, 0xB2C1, 0xB4EE, 0xB6EA, 0xB7A2, 0xB8C1, 0xB9FE,
    0xBBF7, 0xBFA6, 0xC0AC, 0xC2E8, 0xC4C3, 0xC5B6, 0xC5BE, 0xC6DA,
    0xC8BB, 0xC8F6, 0xCBFA, 0xCDDA, 0xCEF4, 0xD1B9, 0xD4D1, 0xD7FA
};
// 与 kBound 前 23 项一一对应的小写首字母。
const char kLetter[] = "abcdefghjklmnopqrstwxyz";
const int  kLetterCount = 23;   // = sizeof(kLetter) - 1

} // anonymous namespace

char PyInitialOf(unsigned gbkCode)
{
    if (gbkCode < kBound[0] || gbkCode >= kBound[kLetterCount])
    {
        return 0;   // 不在 GB2312 一级常用字拼音分区内
    }
    for (int i = 0; i < kLetterCount; ++i)
    {
        if (gbkCode >= kBound[i] && gbkCode < kBound[i + 1])
        {
            return kLetter[i];
        }
    }
    return 0;
}

CString PinyinInitials(const CString& text)
{
    CString out;
    for (int i = 0; i < text.GetLength(); ++i)
    {
        const wchar_t ch = static_cast<wchar_t>(text[i]);

        // ASCII:字母 / 数字保留(小写),其余跳过。
        if (ch < 128)
        {
            if (::iswalnum(ch) != 0)
            {
                out += static_cast<TCHAR>(::towlower(ch));
            }
            continue;
        }

        // 非 ASCII:转 GBK 双字节,按区间取拼音首字母;取不到则跳过。
        char gbk[4] = { 0 };
        const int n = ::WideCharToMultiByte(kCodePageGBK, 0, &ch, 1,
                                            gbk, static_cast<int>(sizeof(gbk)),
                                            nullptr, nullptr);
        if (n == 2)
        {
            const unsigned code =
                (static_cast<unsigned>(static_cast<unsigned char>(gbk[0])) << 8)
                | static_cast<unsigned>(static_cast<unsigned char>(gbk[1]));
            const char letter = PyInitialOf(code);
            if (letter != 0)
            {
                out += static_cast<TCHAR>(letter);
            }
        }
    }
    return out;
}

} // namespace flamingoadmin
