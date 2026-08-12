/**
 *  zip 条目名安全校验的实现，ZipPathSafety.cpp
 *  balloonwj@qq.com   2026-08-12
 */
#include "ZipPathSafety.h"

#include <vector>

namespace iupdate {

namespace {

//路径分隔符：zip 规范用正斜杠，但实际产出的包里两种都见得到，一律按分隔符处理。
bool IsSeparator(wchar_t c)
{
    return c == L'/' || c == L'\\';
}

//是否是盘符字母。只认 ASCII 字母，全角字母不算。
bool IsDriveLetter(wchar_t c)
{
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}

} // namespace

bool IsDirectoryEntry(const std::wstring& raw)
{
    if (raw.empty())
    {
        return false;
    }
    return IsSeparator(raw[raw.size() - 1]);
}

bool NormalizeZipEntryName(const std::wstring& raw, std::wstring& out)
{
    out.clear();

    if (raw.empty())
    {
        return false;
    }

    //① 绝对路径：以分隔符开头。顺带把 UNC（\\server\share）和 \\?\ 前缀一并挡掉——
    //   它们同样以分隔符开头，不必再单独判一次。
    if (IsSeparator(raw[0]))
    {
        return false;
    }

    //② 盘符：形如 C:\foo、C:foo。后者是"相对于该盘当前目录"，同样不能接受。
    if (raw.size() >= 2 && raw[1] == L':' && IsDriveLetter(raw[0]))
    {
        return false;
    }

    //③ 逐段扫描：拆出以分隔符分隔的各段，边拆边判。
    std::vector<std::wstring> segments;
    std::wstring current;

    for (size_t i = 0; i <= raw.size(); ++i)
    {
        //末尾也走一次循环体，好把最后一段收进去
        const bool atEnd = (i == raw.size());
        if (!atEnd && !IsSeparator(raw[i]))
        {
            current.push_back(raw[i]);
            continue;
        }

        if (current == L"..")
        {
            //穿越：拒绝整包。不做"往上退一级再看看还在不在目录内"这种补救——
            //那种写法很容易在符号链接、短路径名等情形下被绕过，直接拒绝最稳妥。
            out.clear();
            return false;
        }

        //连续分隔符产生的空段、以及 . 段，都直接丢掉
        if (!current.empty() && current != L".")
        {
            segments.push_back(current);
        }
        current.clear();
    }

    //④ 归一化后什么都不剩（例如 "." 或 "./"）：不是一个能落盘的路径
    if (segments.empty())
    {
        return false;
    }

    for (size_t i = 0; i < segments.size(); ++i)
    {
        if (i > 0)
        {
            out.push_back(L'\\');
        }
        out += segments[i];
    }

    return true;
}

} // namespace iupdate
