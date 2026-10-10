/**
 *  品牌文字的读取与解码实现，BrandText.cpp。说明见 BrandText.h。
 *
 *  本文件被 Flamingo.vcxproj、FlamingoUnit.vcxproj 与 FlamingoUpdater.vcxproj 三个工程共用，
 *  只依赖 <windows.h>，不包含 stdafx.h；在 Flamingo.vcxproj 里须标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "BrandText.h"
#include "../Common/I18n/I18n.h"   // TR_NOOP：只做标记的宏，不引入链接依赖

#include <windows.h>
#include <stdio.h>
#include <vector>

std::wstring g_strAppTitle;
std::wstring g_strAppSlogan;
std::wstring g_strAppCopyright;
std::wstring g_strDefaultCompany;

namespace brandtext {

const wchar_t* const kDefaultAppTitle  = L"FlamingoIM";
//内置标语需要随界面语言翻译：这里只做标记（TR_NOOP），由主程序启动时换成译文（App/Startup.cpp）。
//常量在语言确定之前就已初始化，所以不能在这里直接调用 TR
const wchar_t* const kDefaultSlogan    = TR_NOOP(L"开源实用的企业即时通讯软件");
const wchar_t* const kDefaultCopyright = L"© 2024-2026 FlamingoIM Team · All Rights Reserved";
//公司全称是数据，不随界面语言翻译
const wchar_t* const kDefaultCompany   = L"FlamingoIM.org";

namespace {

// UTF-8 BOM 的三个字节。flamingo.ini 正常没有 BOM，被编辑器另存时可能带上。
const char kUtf8Bom[] = { '\xEF', '\xBB', '\xBF' };
const size_t kUtf8BomLen = sizeof(kUtf8Bom);

// UTF-16 LE BOM 的两个字节。文件被另存成 Unicode 时以它开头，系统按 UTF-16 解析这种文件。
const unsigned char kUtf16LeBom0 = 0xFF;
const unsigned char kUtf16LeBom1 = 0xFE;

// 用 GetPrivateProfileStringW 读取 UTF-16 文件时的缓冲区长度（字符数），足够容纳产品名称与标语。
const DWORD kProfileBufChars = 512;

// 传给 GetPrivateProfileStringW 的缺省值。配置项的值不会是这个串，读回它就说明没有这一项。
const wchar_t* const kMissingSentinel = L"<<brandtext: key is missing>>";

// 判断字符是否为 ini 行内需要去掉的空白（空格或制表符）
bool IsBlank(char ch)
{
    return ch == ' ' || ch == '\t';
}

// 去掉字节串首尾的空格与制表符
std::string TrimBlank(const std::string& s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && IsBlank(s[begin]))
        ++begin;
    while (end > begin && IsBlank(s[end - 1]))
        --end;
    return s.substr(begin, end - begin);
}

// 不区分大小写地比较两个 ASCII 字节串（只折叠 A～Z，其余字节按原值比较）
bool EqualsIgnoreCaseAscii(const std::string& a, const char* b)
{
    size_t lenB = strlen(b);
    if (a.size() != lenB)
        return false;

    for (size_t i = 0; i < lenB; ++i)
    {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return false;
    }
    return true;
}

// 把整个文件读成字节串。文件不存在或读取失败时返回 false。
bool ReadFileBytes(const wchar_t* path, std::string& outBytes)
{
    // 共享读写打开：客户端其它代码可能同时在写这个文件
    HANDLE hFile = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    bool ok = true;
    LARGE_INTEGER size;
    if (!::GetFileSizeEx(hFile, &size) || size.HighPart != 0)
    {
        ok = false;
    }
    else
    {
        outBytes.assign((size_t)size.LowPart, '\0');
        DWORD read = 0;
        if (size.LowPart > 0
            && (!::ReadFile(hFile, &outBytes[0], size.LowPart, &read, NULL) || read != size.LowPart))
        {
            ok = false;
        }
    }

    ::CloseHandle(hFile);
    return ok;
}

// 按指定代码页把字节串解码为宽字符串。flags 传 MB_ERR_INVALID_CHARS 时遇到非法字节返回 false。
bool DecodeWithCodePage(const std::string& raw, UINT codePage, DWORD flags, std::wstring& outText)
{
    int len = ::MultiByteToWideChar(codePage, flags, raw.data(), (int)raw.size(), NULL, 0);
    if (len <= 0)
        return false;

    std::vector<wchar_t> buf(len);
    if (::MultiByteToWideChar(codePage, flags, raw.data(), (int)raw.size(), &buf[0], len) != len)
        return false;

    outText.assign(&buf[0], len);
    return true;
}

// 把 ASCII 字节串转成宽字符串（节名、键名都是 ASCII，用于调用系统的 W 版读取函数）
std::wstring AsciiToWide(const char* s)
{
    std::wstring out;
    for (const char* p = s; *p != '\0'; ++p)
    {
        out.push_back((wchar_t)(unsigned char)*p);
    }
    return out;
}

// 去掉宽字符串首尾的空格与制表符
std::wstring TrimBlankWide(const std::wstring& s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (s[begin] == L' ' || s[begin] == L'\t'))
        ++begin;
    while (end > begin && (s[end - 1] == L' ' || s[end - 1] == L'\t'))
        --end;
    return s.substr(begin, end - begin);
}

} // namespace

bool FindIniValueBytes(const std::string& fileBytes, const char* section, const char* key,
                       std::string& outValue)
{
    size_t pos = 0;
    if (fileBytes.size() >= kUtf8BomLen && fileBytes.compare(0, kUtf8BomLen, kUtf8Bom, kUtf8BomLen) == 0)
        pos = kUtf8BomLen;

    bool inSection = false;
    while (pos < fileBytes.size())
    {
        size_t lineEnd = fileBytes.find('\n', pos);
        if (lineEnd == std::string::npos)
            lineEnd = fileBytes.size();

        std::string line = fileBytes.substr(pos, lineEnd - pos);
        pos = lineEnd + 1;

        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        line = TrimBlank(line);

        // 空行与注释行
        if (line.empty() || line[0] == ';')
            continue;

        // 节名行：[name]
        if (line[0] == '[')
        {
            size_t close = line.find(']');
            std::string name = (close == std::string::npos) ? line.substr(1) : line.substr(1, close - 1);
            inSection = EqualsIgnoreCaseAscii(TrimBlank(name), section);
            continue;
        }

        if (!inSection)
            continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        if (!EqualsIgnoreCaseAscii(TrimBlank(line.substr(0, eq)), key))
            continue;

        std::string value = TrimBlank(line.substr(eq + 1));
        // 整体被一对双引号包围时去掉引号，与系统读取 ini 的行为一致
        if (value.size() >= 2 && value[0] == '"' && value[value.size() - 1] == '"')
            value = value.substr(1, value.size() - 2);

        outValue = value;
        return true;
    }

    return false;
}

std::wstring DecodeIniValue(const std::string& raw)
{
    if (raw.empty())
        return std::wstring();

    std::wstring text;
    if (DecodeWithCodePage(raw, CP_UTF8, MB_ERR_INVALID_CHARS, text))
        return text;

    if (DecodeWithCodePage(raw, CP_ACP, 0, text))
        return text;

    return std::wstring();
}

bool ReadIniText(const wchar_t* iniPath, const char* section, const char* key, std::wstring& outText)
{
    if (iniPath == NULL || section == NULL || key == NULL)
        return false;

    std::string bytes;
    if (!ReadFileBytes(iniPath, bytes))
        return false;

    // 被另存成 UTF-16 的文件：系统读取函数本来就按 UTF-16 解码，直接交给它
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == kUtf16LeBom0 && (unsigned char)bytes[1] == kUtf16LeBom1)
    {
        std::vector<wchar_t> buf(kProfileBufChars, L'\0');
        ::GetPrivateProfileStringW(AsciiToWide(section).c_str(), AsciiToWide(key).c_str(), kMissingSentinel,
                                   &buf[0], kProfileBufChars, iniPath);
        std::wstring value(&buf[0]);
        if (value == kMissingSentinel)
            return false;

        outText = value;
        return true;
    }

    std::string raw;
    if (!FindIniValueBytes(bytes, section, key, raw))
        return false;

    outText = DecodeIniValue(raw);
    return true;
}

std::wstring ReadAppTitle(const wchar_t* iniPath)
{
    std::wstring title;
    if (!ReadIniText(iniPath, "ui", "apptitle", title))
        return kDefaultAppTitle;

    title = TrimBlankWide(title);
    if (title.empty())
        return kDefaultAppTitle;

    return title;
}

std::wstring ReadSlogan(const wchar_t* iniPath)
{
    std::wstring slogan;
    if (!ReadIniText(iniPath, "ui", "slogan", slogan))
        return kDefaultSlogan;

    // 配置了这一项但值为空：不显示标语
    return TrimBlankWide(slogan);
}

std::wstring ReadCopyright(const wchar_t* iniPath)
{
    std::wstring copyright;
    if (!ReadIniText(iniPath, "ui", "copyrighttext", copyright))
        return kDefaultCopyright;

    copyright = TrimBlankWide(copyright);
    if (copyright.empty())
        return kDefaultCopyright;

    return copyright;
}

std::wstring ReadDefaultCompany(const wchar_t* iniPath)
{
    std::wstring company;
    if (!ReadIniText(iniPath, "ui", "defaultcompany", company))
        return kDefaultCompany;

    company = TrimBlankWide(company);
    if (company.empty())
        return kDefaultCompany;

    return company;
}

} // namespace brandtext
