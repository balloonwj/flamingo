/**
 *  ConfigTextCodec.cpp
 *
 *  配置文字编码的实现，用途与用法见 ConfigTextCodec.h。只做内存里的字符串转换，不读写文件。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// 本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
// （工程里对应的 PrecompiledHeader 设为 NotUsing）。
#include "ConfigTextCodec.h"

#include <windows.h>   // WideCharToMultiByte / MultiByteToWideChar（UTF-8 与 UTF-16 互转）
#include <vector>

namespace cfgtext {

namespace {

// 十六进制数字表，下标即数值
const wchar_t kHexDigits[] = L"0123456789abcdef";

// 一个字节拆成两位十六进制时，高四位右移的位数与低四位的掩码
const int          kNibbleBits = 4;
const unsigned int kNibbleMask = 0x0F;

// 十进制数字之后的十六进制数值起点：'a' / 'A' 对应 10
const int kHexLetterBase = 10;

//把一位十六进制字符转成数值；不是十六进制字符时返回 -1
int HexValue(wchar_t ch)
{
    if (ch >= L'0' && ch <= L'9')
    {
        return ch - L'0';
    }
    if (ch >= L'a' && ch <= L'f')
    {
        return ch - L'a' + kHexLetterBase;
    }
    if (ch >= L'A' && ch <= L'F')
    {
        return ch - L'A' + kHexLetterBase;
    }
    return -1;
}

} // anonymous namespace

std::wstring EncodeConfigText(const std::wstring& text)
{
    if (text.empty())
    {
        return std::wstring();
    }
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    if (bytes <= 0)
    {
        return std::wstring();
    }
    std::vector<char> utf8((size_t)bytes);
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], bytes, NULL, NULL);

    std::wstring out;
    out.reserve(utf8.size() * 2);
    for (size_t i = 0; i < utf8.size(); ++i)
    {
        const unsigned int b = (unsigned char)utf8[i];
        out.push_back(kHexDigits[(b >> kNibbleBits) & kNibbleMask]);
        out.push_back(kHexDigits[b & kNibbleMask]);
    }
    return out;
}

bool DecodeConfigText(const std::wstring& encoded, std::wstring& out)
{
    out.clear();
    if (encoded.empty())
    {
        return true;
    }
    if (encoded.size() % 2 != 0)
    {
        return false;
    }

    std::vector<char> utf8;
    utf8.reserve(encoded.size() / 2);
    for (size_t i = 0; i < encoded.size(); i += 2)
    {
        const int hi = HexValue(encoded[i]);
        const int lo = HexValue(encoded[i + 1]);
        if (hi < 0 || lo < 0)
        {
            return false;
        }
        utf8.push_back((char)((hi << kNibbleBits) | lo));
    }

    //MB_ERR_INVALID_CHARS：字节序列不是合法 UTF-8 时整体失败，而不是悄悄换成替代字符
    const int chars = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, &utf8[0], (int)utf8.size(), NULL, 0);
    if (chars <= 0)
    {
        return false;
    }
    std::vector<wchar_t> wide((size_t)chars);
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, &utf8[0], (int)utf8.size(), &wide[0], chars);
    out.assign(wide.begin(), wide.end());
    return true;
}

} // namespace cfgtext
