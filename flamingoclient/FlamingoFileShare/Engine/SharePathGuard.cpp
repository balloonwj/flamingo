/**
 *  文件共享的路径安全检查 SharePathGuard 的实现，见 SharePathGuard.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "SharePathGuard.h"

#include <windows.h>
#include "ShareWinPath.h"
#include <string.h>   //strchr / _stricmp
#include <vector>

namespace fileshare {

namespace {

//Windows 保留的设备名，不区分大小写；带扩展名（如 nul.txt）同样是设备名
const char* const kReservedNames[] =
{
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
};

//文件名里不允许出现的字符（':' 另行处理，覆盖盘符与备用数据流）
const char* const kIllegalChars = "<>\"|?*:\\";

//ASCII 控制字符的上界（不含）：0x00~0x1F 都不能出现在文件名里
const unsigned char kFirstPrintable = 0x20;

//GetFinalPathNameByHandleW 返回的路径前缀
const wchar_t* const kFinalPathPrefix = L"\\\\?\\";
const wchar_t* const kFinalUncPrefix = L"\\\\?\\UNC\\";

//一段是不是 Windows 保留的设备名：取第一个点之前的部分、去掉末尾空格后比较
bool IsReservedName(const std::string& segment)
{
    std::string base = segment.substr(0, segment.find('.'));
    while (!base.empty() && base[base.size() - 1] == ' ')
        base.erase(base.size() - 1);
    for (size_t i = 0; i < sizeof(kReservedNames) / sizeof(kReservedNames[0]); ++i)
    {
        if (::_stricmp(base.c_str(), kReservedNames[i]) == 0)
            return true;
    }
    return false;
}

//检查一段文件名的写法
bool IsValidSegment(const std::string& segment)
{
    if (segment.empty() || segment == "." || segment == "..")
        return false;
    if (segment[0] == ' ' || segment[segment.size() - 1] == ' ' || segment[segment.size() - 1] == '.')
        return false;
    for (size_t i = 0; i < segment.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(segment[i]);
        if (c < kFirstPrintable || ::strchr(kIllegalChars, static_cast<int>(c)) != NULL)
            return false;
    }
    return !IsReservedName(segment);
}

//去掉结尾的反斜杠；盘符根目录（"X:\"）保留
std::wstring TrimTrailingSlash(const std::wstring& path)
{
    std::wstring s = path;
    while (s.size() > 3 && (s[s.size() - 1] == L'\\' || s[s.size() - 1] == L'/'))
        s.erase(s.size() - 1);
    if (s.size() == 3 && s[1] == L':' && s[2] == L'/')
        s[2] = L'\\';
    return s;
}

//不区分大小写比较两个宽字符串的前 len 个字符（序数比较，不受区域设置影响）
bool EqualsIgnoreCase(const wchar_t* a, const wchar_t* b, int len)
{
    return ::CompareStringOrdinal(a, len, b, len, TRUE) == CSTR_EQUAL;
}

//取一个已存在条目的最终路径（解析全部目录联接与符号链接），去掉 \\?\ 前缀；取不到返回 false
bool GetFinalPath(const std::wstring& path, std::wstring& out)
{
    //加长路径前缀：共享根下的深层路径可能超过 MAX_PATH，不加前缀会打开失败
    HANDLE h = ::CreateFileW(ShareToLongPath(path).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    std::vector<wchar_t> buf(MAX_PATH);
    DWORD len = ::GetFinalPathNameByHandleW(h, &buf[0], static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED);
    if (len >= buf.size())
    {
        buf.resize(len + 1);
        len = ::GetFinalPathNameByHandleW(h, &buf[0], static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED);
    }
    ::CloseHandle(h);
    if (len == 0 || len >= buf.size())
        return false;

    std::wstring s(&buf[0], len);
    const std::wstring unc(kFinalUncPrefix);
    const std::wstring local(kFinalPathPrefix);
    if (s.compare(0, unc.size(), unc) == 0)
        s = L"\\\\" + s.substr(unc.size());
    else if (s.compare(0, local.size(), local) == 0)
        s = s.substr(local.size());
    out = TrimTrailingSlash(s);
    return true;
}

} // namespace

bool SharePathGuard::IsValidRelativePath(const std::string& utf8Path, bool singleFileShare)
{
    if (utf8Path.empty())
        return true;
    if (singleFileShare || utf8Path.size() > kShareRelPathMaxBytes)
        return false;

    std::wstring wide;
    if (!ShareUtf8ToWide(utf8Path, wide))
        return false;

    //按 '/' 逐段检查；以 '/' 开头或结尾、连续的 '/' 都会切出空段
    size_t begin = 0;
    while (true)
    {
        const size_t end = utf8Path.find('/', begin);
        const std::string segment = utf8Path.substr(begin, (end == std::string::npos) ? std::string::npos : end - begin);
        if (!IsValidSegment(segment))
            return false;
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return true;
}

bool SharePathGuard::ResolveUnderRoot(const std::wstring& root, const std::string& utf8RelPath, bool singleFileShare,
                                      std::wstring& out)
{
    out.clear();
    if (!IsValidRelativePath(utf8RelPath, singleFileShare))
        return false;

    const std::wstring base = TrimTrailingSlash(root);
    if (utf8RelPath.empty())
    {
        out = base;
        return true;
    }

    std::wstring rel;
    if (!ShareUtf8ToWide(utf8RelPath, rel))
        return false;
    for (size_t i = 0; i < rel.size(); ++i)
    {
        if (rel[i] == L'/')
            rel[i] = L'\\';
    }

    //盘符根目录本身以反斜杠结尾，其余补一个分隔符
    std::wstring joined = base;
    if (joined.empty() || joined[joined.size() - 1] != L'\\')
        joined += L'\\';
    joined += rel;

    //前面的逐段检查已经排除了一切跳出共享根的写法，这里再按目录边界核对一次
    if (!IsUnderRoot(base, joined))
        return false;
    out = joined;
    return true;
}

bool SharePathGuard::IsUnderRoot(const std::wstring& root, const std::wstring& path)
{
    const std::wstring base = TrimTrailingSlash(root);
    const std::wstring target = TrimTrailingSlash(path);
    if (base.empty())
        return false;

    if (target.size() == base.size())
        return EqualsIgnoreCase(base.c_str(), target.c_str(), static_cast<int>(base.size()));

    //根本身以反斜杠结尾（盘符根目录）时不必再补
    std::wstring prefix = base;
    if (prefix[prefix.size() - 1] != L'\\')
        prefix += L'\\';
    if (target.size() < prefix.size())
        return false;
    return EqualsIgnoreCase(prefix.c_str(), target.c_str(), static_cast<int>(prefix.size()));
}

bool SharePathGuard::IsReparseSafe(const std::wstring& root, const std::wstring& absPath)
{
    std::wstring finalRoot;
    if (!GetFinalPath(TrimTrailingSlash(root), finalRoot))
        return false;

    //目标尚不存在（新建目录、上传的新文件）时，逐级向上找到最深一层已存在的上级
    std::wstring probe = TrimTrailingSlash(absPath);
    //同样加长路径前缀，否则超过 MAX_PATH 的已存在路径会被误当作不存在，检查退到较浅的上级，漏掉深层的重解析点
    while (::GetFileAttributesW(ShareToLongPath(probe).c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        const size_t pos = probe.find_last_of(L'\\');
        if (pos == std::wstring::npos || pos < 2)
            return false;
        probe = TrimTrailingSlash(probe.substr(0, pos + 1));
    }

    std::wstring finalTarget;
    if (!GetFinalPath(probe, finalTarget))
        return false;
    return IsUnderRoot(finalRoot, finalTarget);
}

} // namespace fileshare
