/**
 *  LogCleanupPolicy.cpp
 *
 *  「启动时清空历史日志」开关的取值规则与读写实现。接口说明与设计取舍见
 *  LogCleanupPolicy.h。
 *
 *  balloonwj@qq.com   2026-08-17
 */

//本文件被 FlamingoUnit 测试工程直接编译，故<u>不</u>走主工程的预编译头，
//需要的头在这里自己引。
#include "LogCleanupPolicy.h"

#include "..\Util\IniFile.h"

namespace logcleanup {

const wchar_t* const kIniSection = L"app";
const wchar_t* const kIniKey     = L"clearexpirelog";

namespace {

//从配置里读一行值时给的缓冲区长度（宽字符个数）。本项的合法内容只有 "0" / "1"
//一两个字符，取 32 已经远远够用；配置被写坏时超长部分会被截断，而截断后的内容
//同样识别不出来，照样回落默认值，不影响结论。
const int kValueBufferChars = 32;

//写回配置时用的两种文本。与 ParseClearLogOnStart 认的两种写法必须一致。
const wchar_t* const kValueOn  = L"1";
const wchar_t* const kValueOff = L"0";

/**
 *  去掉首尾空白（空格、制表符、回车换行）。
 *    text：待处理文本，不为空。
 *    outBegin：[出参] 有效内容的起始位置。
 *    outEnd：  [出参] 有效内容的结束位置（不含）。
 */
void TrimSpan(const wchar_t* text, const wchar_t** outBegin, const wchar_t** outEnd)
{
    const wchar_t* begin = text;
    while (*begin == L' ' || *begin == L'\t' || *begin == L'\r' || *begin == L'\n')
    {
        ++begin;
    }

    const wchar_t* end = begin;
    while (*end != L'\0')
    {
        ++end;
    }
    while (end > begin)
    {
        const wchar_t ch = *(end - 1);
        if (ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n')
        {
            break;
        }
        --end;
    }

    *outBegin = begin;
    *outEnd   = end;
}

} // namespace

bool DefaultClearLogOnStart()
{
#ifdef _DEBUG
    //开发期要能翻前几次运行的日志，启动就清掉会很碍事。
    return false;
#else
    //用户机器上日志会一直堆积，没人会去手工清理。
    return true;
#endif
}

bool ParseClearLogOnStart(const wchar_t* rawValue, bool defaultOn)
{
    if (rawValue == NULL)
    {
        return defaultOn;   // 配置项不存在
    }

    const wchar_t* begin = NULL;
    const wchar_t* end   = NULL;
    TrimSpan(rawValue, &begin, &end);

    //只认单个字符的 "0" / "1"。其余一律当作没有有效取值 —— 配置被手工改坏时，
    //行为是可预期的默认值，而不是某个碰巧解析出来的结果。
    if (end - begin != 1)
    {
        return defaultOn;
    }
    if (*begin == L'0')
    {
        return false;
    }
    if (*begin == L'1')
    {
        return true;
    }
    return defaultOn;
}

bool ReadClearLogOnStart(const wchar_t* iniPath)
{
    const bool defaultOn = DefaultClearLogOnStart();
    if (iniPath == NULL || iniPath[0] == L'\0')
    {
        return defaultOn;
    }

    //用 ReadString 而不是 ReadInt：后者包的是 GetPrivateProfileInt，它分不清
    //"键不存在"和"值写着 0"，遇到非数字的脏值也会当 0 而不是回落默认值。
    wchar_t szValue[kValueBufferChars] = { 0 };
    CIniFile iniFile;
    iniFile.ReadString(kIniSection, kIniKey, L"", szValue, kValueBufferChars, iniPath);

    return ParseClearLogOnStart(szValue, defaultOn);
}

bool WriteClearLogOnStart(const wchar_t* iniPath, bool on)
{
    if (iniPath == NULL || iniPath[0] == L'\0')
    {
        return false;
    }

    CIniFile iniFile;
    return iniFile.WriteString(kIniSection, kIniKey,
                               on ? kValueOn : kValueOff, iniPath) != FALSE;
}

} // namespace logcleanup
