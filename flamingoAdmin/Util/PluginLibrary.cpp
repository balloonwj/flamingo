/**
 *  flamingoAdmin —— 功能模块 DLL 装卸的实现，说明见 PluginLibrary.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Util/PluginLibrary.h"

namespace flamingoadmin {

PluginLibrary::PluginLibrary()
    : m_module(nullptr)
{
}

PluginLibrary::~PluginLibrary()
{
    Free();
}

PluginLoadResult PluginLibrary::Load(const wchar_t* dllPath, const char* factoryName, FARPROC& entry)
{
    entry = nullptr;
    Free();

    if (dllPath == nullptr || dllPath[0] == L'\0' || factoryName == nullptr || factoryName[0] == '\0')
    {
        return PluginLoadResult::BadPath;
    }

    //先看文件在不在：不在时 LoadLibraryExW 也会失败，但单独判断能把「没装这个模块」与「模块坏了」分开
    const DWORD attr = ::GetFileAttributesW(dllPath);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return PluginLoadResult::NotFound;
    }

    HMODULE module = ::LoadLibraryExW(dllPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr)
    {
        //文件在，但它依赖的 DLL 缺失时系统报「找不到模块」
        const DWORD err = ::GetLastError();
        return (err == ERROR_MOD_NOT_FOUND) ? PluginLoadResult::NotFound : PluginLoadResult::LoadFailed;
    }

    FARPROC proc = ::GetProcAddress(module, factoryName);
    if (proc == nullptr)
    {
        ::FreeLibrary(module);
        return PluginLoadResult::EntryMissing;
    }

    m_module = module;
    entry = proc;
    return PluginLoadResult::Ok;
}

void PluginLibrary::Free()
{
    if (m_module != nullptr)
    {
        ::FreeLibrary(m_module);
        m_module = nullptr;
    }
}

const wchar_t* PluginLibrary::DescribeResult(PluginLoadResult r)
{
    switch (r)
    {
    // 加载成功
    case PluginLoadResult::Ok:
        return L"ok";

    // 路径或导出函数名为空
    case PluginLoadResult::BadPath:
        return L"bad path";

    // 文件不存在，或依赖的 DLL 缺失
    case PluginLoadResult::NotFound:
        return L"dll not found";

    // 文件存在但加载失败
    case PluginLoadResult::LoadFailed:
        return L"load failed (wrong architecture or corrupted file)";

    // 没有导出指定的函数
    case PluginLoadResult::EntryMissing:
        return L"factory function not exported";

    // 未知结果（不会出现）
    default:
        return L"unknown";
    }
}

} // namespace flamingoadmin
