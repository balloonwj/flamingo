/**
 *  FolderManifest.cpp
 *
 *  单聊「发送文件夹」本地目录清单的实现：纯逻辑层（路径规范化 / 安全校验 / 编码转换 /
 *  统计汇总）与目录遍历层（ScanFolderTree）。接口说明与设计取舍见 FolderManifest.h。
 *
 *  balloonwj@qq.com   2026-08-13
 */

//本文件被 FlamingoUnit 测试工程直接编译，故<u>不</u>走主工程的预编译头（两处工程文件里
//都须登记为 PrecompiledHeader=NotUsing），需要的系统头在这里自己引。
#include "FolderManifest.h"

#include <windows.h>
#include <algorithm>
#include <string.h>

namespace foldertransfer {

//长路径前缀。四个字符依次是反斜杠、反斜杠、问号、反斜杠，在 C++ 字符串里各要转义一次。
const wchar_t* const kWindowsLongPathPrefix = L"\\\\?\\";

namespace {

// ---------------------------------------------------------------------------
// 纯逻辑层用到的常量
// ---------------------------------------------------------------------------

//Windows 文件名里不允许出现的可见字符。反斜杠与正斜杠不列在这里 —— 它们是路径分隔符，
//在逐段判定之前就已经被切开了。冒号单独出现在段内也算非法（盘符前缀另有专门的判定）。
const char* const kWindowsIllegalNameChars = "<>:\"|?*\\/";

//ASCII 控制字符的上界与 DEL。文件名里出现它们多半是构造出来的畸形路径，一律拒绝。
const unsigned char kAsciiControlCharMax = 0x1F;
const unsigned char kAsciiDeleteChar     = 0x7F;

//Windows 保留设备名里不带编号的四个。以这些名字（或 "CON.txt" 这类带扩展名的形式）
//建文件会被系统当成设备而不是文件，故一律拒绝。
const char* const kReservedDeviceNames[] =
{
    "CON",
    "PRN",
    "AUX",
    "NUL"
};

//带编号的保留设备名前缀：COM1~COM9 与 LPT1~LPT9。编号只有 1~9 是保留的，COM0 与
//COM10 不是设备名。
const char* const kNumberedDeviceNames[] =
{
    "COM",
    "LPT"
};

//带编号的保留设备名的前缀长度（"COM" / "LPT" 都是 3 个字符）。
const size_t kNumberedDeviceNamePrefixLen = 3;

//带编号的保留设备名的总长度（前缀 3 个字符 + 1 位编号）。
const size_t kNumberedDeviceNameTotalLen = 4;

//UTF-8 编码的分段阈值：码点落在各区间时分别占 1 / 2 / 3 / 4 个字节。
const uint32_t kUtf8OneByteMax   = 0x7F;
const uint32_t kUtf8TwoByteMax   = 0x7FF;
const uint32_t kUtf8ThreeByteMax = 0xFFFF;

//一个基本平面字符转成 UTF-8 后最多占的字节数，仅用于预留缓冲、不参与判定。
const size_t kUtf8MaxBytesPerBmpChar = 3;

//UTF-8 续字节（10xxxxxx）的判定掩码、标志位与有效位数。
const unsigned char kUtf8ContinuationTagMask   = 0xC0;
const unsigned char kUtf8ContinuationTag       = 0x80;
const unsigned char kUtf8ContinuationValueMask = 0x3F;
const int           kUtf8ContinuationBits      = 6;

//UTF-8 两 / 三 / 四字节首字节（110xxxxx / 1110xxxx / 11110xxx）的判定掩码、标志位与有效位掩码。
const unsigned char kUtf8TwoByteTagMask     = 0xE0;
const unsigned char kUtf8TwoByteTag         = 0xC0;
const unsigned char kUtf8TwoByteValueMask   = 0x1F;
const unsigned char kUtf8ThreeByteTagMask   = 0xF0;
const unsigned char kUtf8ThreeByteTag       = 0xE0;
const unsigned char kUtf8ThreeByteValueMask = 0x0F;
const unsigned char kUtf8FourByteTagMask    = 0xF8;
const unsigned char kUtf8FourByteTag        = 0xF0;
const unsigned char kUtf8FourByteValueMask  = 0x07;

//UTF-16 代理对的区间边界、辅助平面起始码点与拼接时的移位量。
const uint32_t kUtf16HighSurrogateFirst = 0xD800;
const uint32_t kUtf16HighSurrogateLast  = 0xDBFF;
const uint32_t kUtf16LowSurrogateFirst  = 0xDC00;
const uint32_t kUtf16LowSurrogateLast   = 0xDFFF;
const uint32_t kUtf16SupplementaryBase  = 0x10000;
const int      kUtf16SurrogateShiftBits = 10;

//Unicode 允许的最大码点。超过它的值不是合法字符，一律换成替换字符。
const uint32_t kUnicodeMaxCodePoint = 0x10FFFF;

//替换字符 U+FFFD。转换遇到非法序列时用它占位，使调用方拿到的仍是一条合法字符串。
const uint32_t kUnicodeReplacementChar = 0xFFFD;

//UNC 路径开头那两个反斜杠的长度，拼长路径前缀时要跳过它们。
const size_t kUncPrefixChars = 2;

//盘符前缀的长度（一个字母加一个冒号，如 "C:"）。
const size_t kDrivePrefixChars = 2;

//盘符绝对路径（形如 "D:\"）的最短长度。
const size_t kDriveAbsolutePathMinChars = 3;

// ---------------------------------------------------------------------------
// 目录遍历层用到的常量
// ---------------------------------------------------------------------------

//默认允许递归进入的最大层数，取值依据见 FolderManifest.h 里 FolderScanOptions 的注释。
const int kDefaultMaxScanDepth = 64;

//WIN32_FIND_DATAW 里文件大小分高低两个 32 位字段，拼成 64 位时高位要左移这么多位。
const int kFileSizeHighShiftBits = 32;

// ---------------------------------------------------------------------------
// 纯逻辑层的内部小工具
// ---------------------------------------------------------------------------

//是否是 ASCII 字母。不用 isalpha 是因为它受区域设置影响，对高位字节的行为不确定。
bool IsAsciiAlpha(wchar_t ch)
{
    return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
}

//把 ASCII 小写字母转成大写，其余字节原样返回（保留设备名比对只需要 ASCII 口径）。
char ToAsciiUpper(char ch)
{
    if (ch >= 'a' && ch <= 'z')
        return static_cast<char>(ch - 'a' + 'A');

    return ch;
}

//数一条 UTF-8 串里的码点个数：续字节（10xxxxxx）不计数，其余每个字节起一个码点。
//长度上限按码点判而不是按字节判，否则一个 200 字的中文名会被误判成超长。
size_t CountUtf8CodePoints(const std::string& text)
{
    size_t count = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        if ((b & kUtf8ContinuationTagMask) != kUtf8ContinuationTag)
            ++count;
    }

    return count;
}

//把一个码点按 UTF-8 追加进 out。非法码点（超出 Unicode 上界或落在代理区）换成替换字符。
void AppendUtf8CodePoint(uint32_t code, std::string& out)
{
    if (code > kUnicodeMaxCodePoint
        || (code >= kUtf16HighSurrogateFirst && code <= kUtf16LowSurrogateLast))
    {
        code = kUnicodeReplacementChar;
    }

    if (code <= kUtf8OneByteMax)
    {
        out.push_back(static_cast<char>(code));
        return;
    }

    if (code <= kUtf8TwoByteMax)
    {
        out.push_back(static_cast<char>(kUtf8TwoByteTag | (code >> kUtf8ContinuationBits)));
        out.push_back(static_cast<char>(kUtf8ContinuationTag | (code & kUtf8ContinuationValueMask)));
        return;
    }

    if (code <= kUtf8ThreeByteMax)
    {
        out.push_back(static_cast<char>(kUtf8ThreeByteTag | (code >> (kUtf8ContinuationBits * 2))));
        out.push_back(static_cast<char>(kUtf8ContinuationTag | ((code >> kUtf8ContinuationBits) & kUtf8ContinuationValueMask)));
        out.push_back(static_cast<char>(kUtf8ContinuationTag | (code & kUtf8ContinuationValueMask)));
        return;
    }

    out.push_back(static_cast<char>(kUtf8FourByteTag | (code >> (kUtf8ContinuationBits * 3))));
    out.push_back(static_cast<char>(kUtf8ContinuationTag | ((code >> (kUtf8ContinuationBits * 2)) & kUtf8ContinuationValueMask)));
    out.push_back(static_cast<char>(kUtf8ContinuationTag | ((code >> kUtf8ContinuationBits) & kUtf8ContinuationValueMask)));
    out.push_back(static_cast<char>(kUtf8ContinuationTag | (code & kUtf8ContinuationValueMask)));
}

//把一个码点按 UTF-16 追加进 out：基本平面直接放，辅助平面拆成一对代理项。
void AppendUtf16CodePoint(uint32_t code, std::wstring& out)
{
    if (code > kUnicodeMaxCodePoint
        || (code >= kUtf16HighSurrogateFirst && code <= kUtf16LowSurrogateLast))
    {
        code = kUnicodeReplacementChar;
    }

    if (code < kUtf16SupplementaryBase)
    {
        out.push_back(static_cast<wchar_t>(code));
        return;
    }

    const uint32_t rest = code - kUtf16SupplementaryBase;
    const uint32_t high = kUtf16HighSurrogateFirst + (rest >> kUtf16SurrogateShiftBits);
    const uint32_t low  = kUtf16LowSurrogateFirst + (rest & ((1u << kUtf16SurrogateShiftBits) - 1u));
    out.push_back(static_cast<wchar_t>(high));
    out.push_back(static_cast<wchar_t>(low));
}

//一个路径段是不是 Windows 保留设备名。判定口径：取第一个点号之前的部分（"CON.txt" 与
//"CON" 同样是设备），去掉尾部的空格与点号（系统建文件时也会把它们去掉），再按 ASCII
//大小写不敏感比对。
bool IsReservedDeviceName(const std::string& segment)
{
    std::string base = segment;

    const size_t dot = base.find('.');
    if (dot != std::string::npos)
        base = base.substr(0, dot);

    while (!base.empty() && (base[base.size() - 1] == ' ' || base[base.size() - 1] == '.'))
        base.erase(base.size() - 1);

    for (size_t i = 0; i < base.size(); ++i)
        base[i] = ToAsciiUpper(base[i]);

    for (size_t i = 0; i < sizeof(kReservedDeviceNames) / sizeof(kReservedDeviceNames[0]); ++i)
    {
        if (base == kReservedDeviceNames[i])
            return true;
    }

    if (base.size() != kNumberedDeviceNameTotalLen)
        return false;

    const char digit = base[kNumberedDeviceNamePrefixLen];
    if (digit < '1' || digit > '9')
        return false;

    const std::string prefix = base.substr(0, kNumberedDeviceNamePrefixLen);
    for (size_t i = 0; i < sizeof(kNumberedDeviceNames) / sizeof(kNumberedDeviceNames[0]); ++i)
    {
        if (prefix == kNumberedDeviceNames[i])
            return true;
    }

    return false;
}

//校验单个路径段。整条路径的判定由 CheckFolderRelativePath 逐段调用本函数完成。
FolderPathVerdict CheckPathSegment(const std::string& segment)
{
    if (segment.empty())
        return kFolderPathEmpty;

    //"." 与 ".." 都不允许：前者是无意义的冗余段，后者能穿越到接收端选定目录之外
    if (segment == "." || segment == "..")
        return kFolderPathDotSegment;

    if (CountUtf8CodePoints(segment) > kMaxFolderPathSegmentChars)
        return kFolderPathSegmentTooLong;

    for (size_t i = 0; i < segment.size(); ++i)
    {
        const unsigned char b = static_cast<unsigned char>(segment[i]);

        //控制字符与 DEL：正常文件名里不会出现，出现即是构造出来的
        if (b <= kAsciiControlCharMax || b == kAsciiDeleteChar)
            return kFolderPathIllegalChar;

        //非 ASCII 字节属于中文等多字节字符的一部分，照常放行
        if (b > kAsciiDeleteChar)
            continue;

        if (::strchr(kWindowsIllegalNameChars, static_cast<char>(b)) != NULL)
            return kFolderPathIllegalChar;
    }

    if (IsReservedDeviceName(segment))
        return kFolderPathReservedName;

    return kFolderPathOk;
}

// ---------------------------------------------------------------------------
// 目录遍历层的内部小工具
// ---------------------------------------------------------------------------

/**
 *  广度优先队列里的一项：一个还没有枚举过的目录。
 *  用广度优先而不是递归，一来父目录必定先于子目录进清单（接收端顺序建目录即可），
 *  二来极深的目录树不会把调用栈压爆。
 */
struct PendingDir
{
    std::wstring m_relativeW;   // 相对根目录的路径（宽字符、反斜杠分隔），根目录自身为空串
    int          m_depth;       // 相对根目录的层数，根目录为 0，其直接子目录为 1

    PendingDir()
        : m_depth(0)
    {
    }
};

//把宽字符相对路径转成清单里的写法（UTF-8 + 正斜杠）。转换失败返回 false。
bool ToManifestPath(const std::wstring& relativeW, std::string& out)
{
    out.clear();

    std::string utf8;
    if (!Utf16ToUtf8(relativeW, utf8))
        return false;

    out = NormalizeFolderRelativePath(utf8);
    return true;
}

//清单排序用的比较函数（按相对路径升序）。不用 lambda，符合本仓库的代码规范。
bool CompareManifestFile(const FolderManifestFile& left, const FolderManifestFile& right)
{
    return left.m_relativePath < right.m_relativePath;
}

//跳过项排序用的比较函数（按相对路径升序），使结果与磁盘返回顺序无关、可复现。
bool CompareManifestSkipped(const FolderManifestSkipped& left, const FolderManifestSkipped& right)
{
    return left.m_relativePath < right.m_relativePath;
}

/**
 *  把扫描结果置为失败并清空清单，统一各失败出口的收尾动作。
 *  @param out         [出参] 要填的扫描结果
 *  @param error       失败类型，取值见 FolderScanError
 *  @param win32Error  对应的 GetLastError() 原值，无系统错误时传 0
 *  @param errorPath   出错处的相对路径（UTF-8），根目录自身出错时传空串
 *  @return 恒为 false，供调用处写成 "return FailScan(...);"
 */
bool FailScan(FolderScanResult& out, int error, unsigned long win32Error, const std::string& errorPath)
{
    out.m_manifest = FolderManifest();
    out.m_stats = FolderManifestStats();
    out.m_error = error;
    out.m_win32Error = win32Error;
    out.m_errorPath = errorPath;
    return false;
}

} // namespace

// ======================== 第一部分：纯逻辑 ========================

std::string NormalizeFolderRelativePath(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());

    //起始置 true，使开头的分隔符（一个或多个）被一并吞掉
    bool lastWasSeparator = true;

    for (size_t i = 0; i < raw.size(); ++i)
    {
        const char ch = raw[i];
        if (ch == '\\' || ch == '/')
        {
            if (!lastWasSeparator)
            {
                out.push_back(kFolderPathSeparator);
                lastWasSeparator = true;
            }
            continue;
        }

        out.push_back(ch);
        lastWasSeparator = false;
    }

    //上面的合并逻辑可能在末尾留下一个分隔符（原串以分隔符结尾时），去掉它
    if (!out.empty() && out[out.size() - 1] == kFolderPathSeparator)
        out.erase(out.size() - 1);

    return out;
}

FolderPathVerdict CheckFolderRelativePath(const std::string& relativePath)
{
    if (relativePath.empty())
        return kFolderPathEmpty;

    if (CountUtf8CodePoints(relativePath) > kMaxFolderRelativePathChars)
        return kFolderPathTooLong;

    //以分隔符开头即是绝对路径（UNC 的 "\\server\share" 也在此被挡下）
    const char first = relativePath[0];
    if (first == '/' || first == '\\')
        return kFolderPathRooted;

    //盘符前缀形如 "C:"，无论后面跟不跟分隔符都不是相对路径
    if (relativePath.size() >= kDrivePrefixChars
        && IsAsciiAlpha(static_cast<wchar_t>(first))
        && relativePath[1] == ':')
    {
        return kFolderPathDrive;
    }

    const std::string normalized = NormalizeFolderRelativePath(relativePath);
    if (normalized.empty())
        return kFolderPathEmpty;

    size_t begin = 0;
    while (begin <= normalized.size())
    {
        size_t end = normalized.find(kFolderPathSeparator, begin);
        if (end == std::string::npos)
            end = normalized.size();

        const std::string segment = normalized.substr(begin, end - begin);
        const FolderPathVerdict verdict = CheckPathSegment(segment);
        if (verdict != kFolderPathOk)
            return verdict;

        if (end == normalized.size())
            break;

        begin = end + 1;
    }

    return kFolderPathOk;
}

bool IsSafeFolderRelativePath(const std::string& relativePath)
{
    return CheckFolderRelativePath(relativePath) == kFolderPathOk;
}

const char* FolderPathVerdictText(FolderPathVerdict verdict)
{
    switch (verdict)
    {
    //通过校验，可安全落地
    case kFolderPathOk:
        return "ok";

    //空串或只由分隔符组成
    case kFolderPathEmpty:
        return "empty path";

    //含 "." 或 ".." 路径段
    case kFolderPathDotSegment:
        return "dot segment";

    //以分隔符开头的绝对路径
    case kFolderPathRooted:
        return "rooted path";

    //带盘符前缀
    case kFolderPathDrive:
        return "drive prefix";

    //含 Windows 文件名非法字符或控制字符
    case kFolderPathIllegalChar:
        return "illegal char";

    //某一段是 Windows 保留设备名
    case kFolderPathReservedName:
        return "reserved device name";

    //某一段过长
    case kFolderPathSegmentTooLong:
        return "segment too long";

    //整条路径过长
    case kFolderPathTooLong:
        return "path too long";

    //将来新增判定值却漏改本函数时走到这里，只影响日志文字，不影响判定
    default:
        return "unknown";
    }
}

bool Utf16ToUtf8(const std::wstring& in, std::string& out)
{
    out.clear();
    out.reserve(in.size() * kUtf8MaxBytesPerBmpChar);

    bool ok = true;
    for (size_t i = 0; i < in.size(); ++i)
    {
        uint32_t code = static_cast<uint32_t>(static_cast<unsigned short>(in[i]));

        if (code >= kUtf16HighSurrogateFirst && code <= kUtf16HighSurrogateLast)
        {
            //高位代理项后面必须紧跟一个低位代理项，两者合起来才是一个辅助平面字符
            uint32_t low = 0;
            if (i + 1 < in.size())
                low = static_cast<uint32_t>(static_cast<unsigned short>(in[i + 1]));

            if (low >= kUtf16LowSurrogateFirst && low <= kUtf16LowSurrogateLast)
            {
                code = kUtf16SupplementaryBase
                       + ((code - kUtf16HighSurrogateFirst) << kUtf16SurrogateShiftBits)
                       + (low - kUtf16LowSurrogateFirst);
                ++i;
            }
            else
            {
                code = kUnicodeReplacementChar;
                ok = false;
            }
        }
        else if (code >= kUtf16LowSurrogateFirst && code <= kUtf16LowSurrogateLast)
        {
            //落单的低位代理项：前面没有高位代理项与它配对，是坏数据
            code = kUnicodeReplacementChar;
            ok = false;
        }

        AppendUtf8CodePoint(code, out);
    }

    return ok;
}

bool Utf8ToUtf16(const std::string& in, std::wstring& out)
{
    out.clear();
    out.reserve(in.size());

    bool ok = true;
    size_t i = 0;
    while (i < in.size())
    {
        const unsigned char b0 = static_cast<unsigned char>(in[i]);
        uint32_t code = 0;
        size_t extra = 0;

        if (b0 <= kUtf8OneByteMax)
        {
            code = b0;
            extra = 0;
        }
        else if ((b0 & kUtf8TwoByteTagMask) == kUtf8TwoByteTag)
        {
            code = static_cast<uint32_t>(b0 & kUtf8TwoByteValueMask);
            extra = 1;
        }
        else if ((b0 & kUtf8ThreeByteTagMask) == kUtf8ThreeByteTag)
        {
            code = static_cast<uint32_t>(b0 & kUtf8ThreeByteValueMask);
            extra = 2;
        }
        else if ((b0 & kUtf8FourByteTagMask) == kUtf8FourByteTag)
        {
            code = static_cast<uint32_t>(b0 & kUtf8FourByteValueMask);
            extra = 3;
        }
        else
        {
            //续字节或 0xF8 以上的字节出现在首字节位置，是坏数据：占位一个替换字符后跳过它
            AppendUtf16CodePoint(kUnicodeReplacementChar, out);
            ++i;
            ok = false;
            continue;
        }

        if (i + extra >= in.size())
        {
            //末尾的序列被截断了，剩下的字节凑不出一个完整码点
            AppendUtf16CodePoint(kUnicodeReplacementChar, out);
            ok = false;
            break;
        }

        bool broken = false;
        for (size_t k = 1; k <= extra; ++k)
        {
            const unsigned char bx = static_cast<unsigned char>(in[i + k]);
            if ((bx & kUtf8ContinuationTagMask) != kUtf8ContinuationTag)
            {
                broken = true;
                break;
            }

            code = (code << kUtf8ContinuationBits) | static_cast<uint32_t>(bx & kUtf8ContinuationValueMask);
        }

        if (broken)
        {
            //该跟续字节的位置上不是续字节：只跳过首字节，让后面的字节还有机会被正确解出来
            AppendUtf16CodePoint(kUnicodeReplacementChar, out);
            ++i;
            ok = false;
            continue;
        }

        i += extra + 1;
        AppendUtf16CodePoint(code, out);
    }

    return ok;
}

std::wstring ExtractFolderName(const std::wstring& folderPath)
{
    std::wstring path = folderPath;

    //末尾的分隔符可能有一个也可能有多个（"D:\work\报告\\"），一并去掉
    while (!path.empty() && (path[path.size() - 1] == L'\\' || path[path.size() - 1] == L'/'))
        path.erase(path.size() - 1);

    if (path.empty())
        return std::wstring();

    std::wstring name;
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        name = path;
    else
        name = path.substr(pos + 1);

    //去掉末尾分隔符后剩 "D:" 或 "\\?\D:" 这类：选中的是整个盘符根目录，取不出文件夹名
    if (name.find(L':') != std::wstring::npos)
        return std::wstring();

    return name;
}

std::wstring MakeLongPath(const std::wstring& absolutePath)
{
    if (absolutePath.empty())
        return std::wstring();

    //带长路径前缀的路径不再经系统规范化，留着正斜杠会打不开，先统一换成反斜杠
    std::wstring path = absolutePath;
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (path[i] == L'/')
            path[i] = L'\\';
    }

    const std::wstring prefix(kWindowsLongPathPrefix);
    if (path.size() >= prefix.size() && path.compare(0, prefix.size(), prefix) == 0)
        return path;

    //UNC 路径 "\\server\share" 的长路径写法是 "\\?\UNC\server\share"
    if (path.size() > kUncPrefixChars && path[0] == L'\\' && path[1] == L'\\')
        return prefix + L"UNC\\" + path.substr(kUncPrefixChars);

    //盘符绝对路径 "D:\..." 直接加前缀
    if (path.size() >= kDriveAbsolutePathMinChars
        && IsAsciiAlpha(path[0])
        && path[1] == L':'
        && path[2] == L'\\')
    {
        return prefix + path;
    }

    //相对路径等其它形态加不了前缀，原样返回（调用方本来就该传绝对路径）
    return path;
}

FolderManifestStats ComputeFolderManifestStats(const FolderManifest& manifest)
{
    FolderManifestStats stats;
    stats.m_dirCount = static_cast<int64_t>(manifest.m_dirs.size());
    stats.m_fileCount = static_cast<int64_t>(manifest.m_files.size());

    for (size_t i = 0; i < manifest.m_files.size(); ++i)
        stats.m_totalBytes += manifest.m_files[i].m_sizeBytes;

    return stats;
}

FolderPathVerdict CheckFolderManifest(const FolderManifest& manifest, std::string& badPath)
{
    badPath.clear();

    for (size_t i = 0; i < manifest.m_dirs.size(); ++i)
    {
        const FolderPathVerdict verdict = CheckFolderRelativePath(manifest.m_dirs[i]);
        if (verdict != kFolderPathOk)
        {
            badPath = manifest.m_dirs[i];
            return verdict;
        }
    }

    for (size_t i = 0; i < manifest.m_files.size(); ++i)
    {
        const FolderPathVerdict verdict = CheckFolderRelativePath(manifest.m_files[i].m_relativePath);
        if (verdict != kFolderPathOk)
        {
            badPath = manifest.m_files[i].m_relativePath;
            return verdict;
        }
    }

    return kFolderPathOk;
}

// ======================== 第二部分：目录遍历 ========================

FolderScanOptions::FolderScanOptions()
    : m_maxDepth(kDefaultMaxScanDepth)
    , m_skipReparsePoints(true)
{
}

//1601-01-01 到 1970-01-01 之间的 100 纳秒数，以及 1 毫秒等于多少个 100 纳秒（FILETIME 换算 Unix 毫秒用）
static const int64_t kFileTimeUnixEpochDiff = 116444736000000000LL;
static const int64_t kFileTimeTicksPerMs = 10000;

//把 FILETIME 换算成 Unix 毫秒；早于 1970 年的时间按 0（未知）处理
static int64_t FileTimeToUnixMs(const FILETIME& ft)
{
    const int64_t ticks = static_cast<int64_t>((static_cast<uint64_t>(ft.dwHighDateTime) << kFileSizeHighShiftBits)
                                               | ft.dwLowDateTime);
    if (ticks <= kFileTimeUnixEpochDiff)
        return 0;
    return (ticks - kFileTimeUnixEpochDiff) / kFileTimeTicksPerMs;
}

bool ScanFolderTree(const std::wstring& rootPath, const FolderScanOptions& options, FolderScanResult& out)
{
    out = FolderScanResult();

    if (rootPath.empty())
        return FailScan(out, kFolderScanBadArgument, 0, std::string());

    //根目录自身的名字单独给出，它不进目录清单；取不出名字（选了整个盘）即判参数非法
    const std::wstring folderNameW = ExtractFolderName(rootPath);
    if (folderNameW.empty())
        return FailScan(out, kFolderScanBadArgument, 0, std::string());

    std::string folderNameUtf8;
    if (!Utf16ToUtf8(folderNameW, folderNameUtf8))
        return FailScan(out, kFolderScanBadArgument, 0, std::string());

    //根目录名同样要过一遍安全校验：接收端会拿它建目录，不能是保留名或超长名
    const FolderPathVerdict nameVerdict = CheckFolderRelativePath(folderNameUtf8);
    if (nameVerdict != kFolderPathOk)
        return FailScan(out, kFolderScanUnsafePath, 0, folderNameUtf8);

    out.m_manifest.m_folderName = folderNameUtf8;

    //去掉末尾分隔符，后面所有子路径都由这个根拼出来，免得拼出 "D:\work\\sub"
    std::wstring rootDir = rootPath;
    while (!rootDir.empty() && (rootDir[rootDir.size() - 1] == L'\\' || rootDir[rootDir.size() - 1] == L'/'))
        rootDir.erase(rootDir.size() - 1);

    const DWORD rootAttr = ::GetFileAttributesW(MakeLongPath(rootDir).c_str());
    if (rootAttr == INVALID_FILE_ATTRIBUTES)
    {
        const DWORD err = ::GetLastError();
        const int error = (err == ERROR_ACCESS_DENIED) ? kFolderScanAccessDenied : kFolderScanRootMissing;
        return FailScan(out, error, err, std::string());
    }

    if ((rootAttr & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return FailScan(out, kFolderScanRootMissing, 0, std::string());

    //广度优先队列：用下标推进而不是 pop_front，避免大目录下反复搬移元素
    std::vector<PendingDir> queue;
    PendingDir rootItem;
    queue.push_back(rootItem);

    size_t head = 0;
    while (head < queue.size())
    {
        //取值拷贝而不是引用：下面往 queue 里追加元素可能触发扩容，引用会失效
        const PendingDir current = queue[head];
        ++head;

        std::wstring dirFull = rootDir;
        if (!current.m_relativeW.empty())
        {
            dirFull += L"\\";
            dirFull += current.m_relativeW;
        }

        std::wstring pattern = MakeLongPath(dirFull);
        pattern += L"\\*";

        WIN32_FIND_DATAW findData;
        ::memset(&findData, 0, sizeof(findData));

        HANDLE hFind = ::FindFirstFileW(pattern.c_str(), &findData);
        if (hFind == INVALID_HANDLE_VALUE)
        {
            //空目录也至少能枚举到 "." 与 ".."，故走到这里一定是真的出错了
            const DWORD err = ::GetLastError();
            const int error = (err == ERROR_ACCESS_DENIED) ? kFolderScanAccessDenied : kFolderScanEnumFailed;
            std::string errorPath;
            ToManifestPath(current.m_relativeW, errorPath);
            return FailScan(out, error, err, errorPath);
        }

        DWORD enumError = ERROR_SUCCESS;
        for (;;)
        {
            const wchar_t* const name = findData.cFileName;

            //跳过 "." 与 ".." 这两个每个目录都有的自引用项
            const bool isDotEntry = (name[0] == L'.')
                                    && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'));
            if (!isDotEntry)
            {
                std::wstring childRelativeW = current.m_relativeW;
                if (!childRelativeW.empty())
                    childRelativeW += L"\\";
                childRelativeW += name;

                std::string childPath;
                if (!ToManifestPath(childRelativeW, childPath))
                {
                    //文件名里有不成对的代理项，转不出合法 UTF-8，按不安全路径整体判失败
                    ::FindClose(hFind);
                    return FailScan(out, kFolderScanUnsafePath, 0, childPath);
                }

                const FolderPathVerdict verdict = CheckFolderRelativePath(childPath);
                if (verdict != kFolderPathOk)
                {
                    ::FindClose(hFind);
                    return FailScan(out, kFolderScanUnsafePath, 0, childPath);
                }

                const bool isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                const bool isReparsePoint = (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

                if (isReparsePoint && options.m_skipReparsePoints)
                {
                    //符号链接 / 目录联接可能指回上级形成环，也可能指向选定目录之外，一律不跟进
                    FolderManifestSkipped skipped;
                    skipped.m_relativePath = childPath;
                    skipped.m_reason = kFolderSkipReparsePoint;
                    skipped.m_isDirectory = isDirectory;
                    out.m_manifest.m_skipped.push_back(skipped);
                }
                else if (isDirectory)
                {
                    if (current.m_depth + 1 > options.m_maxDepth)
                    {
                        ::FindClose(hFind);
                        return FailScan(out, kFolderScanTooDeep, 0, childPath);
                    }

                    //空目录也走这条路径进清单，接收端照样会把它建出来
                    out.m_manifest.m_dirs.push_back(childPath);

                    PendingDir next;
                    next.m_relativeW = childRelativeW;
                    next.m_depth = current.m_depth + 1;
                    queue.push_back(next);
                }
                else
                {
                    FolderManifestFile file;
                    file.m_relativePath = childPath;
                    file.m_sizeBytes = (static_cast<int64_t>(findData.nFileSizeHigh) << kFileSizeHighShiftBits)
                                       | static_cast<int64_t>(findData.nFileSizeLow);
                    file.m_mtimeMs = FileTimeToUnixMs(findData.ftLastWriteTime);
                    out.m_manifest.m_files.push_back(file);
                }
            }

            //先清掉错误码，这样枚举正常结束（ERROR_NO_MORE_FILES）与真出错才分得开
            ::SetLastError(ERROR_SUCCESS);
            if (!::FindNextFileW(hFind, &findData))
            {
                enumError = ::GetLastError();
                break;
            }
        }

        ::FindClose(hFind);

        if (enumError != ERROR_NO_MORE_FILES && enumError != ERROR_SUCCESS)
        {
            const int error = (enumError == ERROR_ACCESS_DENIED) ? kFolderScanAccessDenied : kFolderScanEnumFailed;
            std::string errorPath;
            ToManifestPath(current.m_relativeW, errorPath);
            return FailScan(out, error, enumError, errorPath);
        }
    }

    //按相对路径排序：结果与磁盘返回顺序无关（可复现），且父目录必定排在其子目录之前
    std::sort(out.m_manifest.m_dirs.begin(), out.m_manifest.m_dirs.end());
    std::sort(out.m_manifest.m_files.begin(), out.m_manifest.m_files.end(), CompareManifestFile);
    std::sort(out.m_manifest.m_skipped.begin(), out.m_manifest.m_skipped.end(), CompareManifestSkipped);

    out.m_stats = ComputeFolderManifestStats(out.m_manifest);
    out.m_error = kFolderScanOk;
    out.m_win32Error = 0;
    out.m_errorPath.clear();
    return true;
}

bool ScanFolderTree(const std::wstring& rootPath, FolderScanResult& out)
{
    const FolderScanOptions options;
    return ScanFolderTree(rootPath, options, out);
}

} // namespace foldertransfer
