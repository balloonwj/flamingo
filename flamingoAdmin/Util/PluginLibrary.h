/**
 *  flamingoAdmin —— 功能模块 DLL 的装卸：按完整路径加载 DLL、按名称取导出函数、退出时卸载。
 *
 *  只管 DLL 本身，不调用模块的初始化与关闭方法，也不写日志，便于单元测试（Tests/FlamingoLinkHostTests.cpp
 *  的 FLK12）。做法与 PC 客户端的 Source/App/PluginModuleLoader.h 相同，控制台不依赖客户端源码，故另写一份。
 *  只在 UI 线程使用。
 *
 *  典型用法：
 *      PluginLibrary lib;
 *      FARPROC entry = nullptr;
 *      if (lib.Load(path, "FlamingoLink_Create", entry) == PluginLoadResult::Ok) { ... }
 *      ...
 *      lib.Free();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <windows.h>

namespace flamingoadmin {

// 加载结果
enum class PluginLoadResult
{
    Ok           = 0,   // 加载成功，导出函数可用
    BadPath      = 1,   // 传入的路径或导出函数名为空
    NotFound     = 2,   // 路径上没有这个文件，或它依赖的 DLL 缺失
    LoadFailed   = 3,   // 文件存在但无法加载（32 位 / 64 位不符、文件损坏、不是 DLL）
    EntryMissing = 4,   // 加载成功但没有导出指定名称的函数，说明不是期望的模块
};

// 功能模块 DLL。不可复制；析构时自动卸载。
class PluginLibrary
{
public:
    PluginLibrary();

    // 析构时若仍处于加载状态则卸载。
    ~PluginLibrary();

    PluginLibrary(const PluginLibrary&) = delete;
    PluginLibrary& operator=(const PluginLibrary&) = delete;

    // 加载 DLL 并取指定名称的导出函数。已处于加载状态时先卸载再重新加载。
    // DLL 以 LOAD_WITH_ALTERED_SEARCH_PATH 方式加载：它自己的依赖从它所在的目录开始查找。
    //   dllPath：DLL 的完整路径；为空串时返回 BadPath。
    //   factoryName：导出函数名（ANSI）；为空或空串时返回 BadPath。
    //   entry：[出参] 成功时为导出函数的地址，失败时为 nullptr。
    //   返回：加载结果；非 Ok 时本对象保持未加载状态。
    PluginLoadResult Load(const wchar_t* dllPath, const char* factoryName, FARPROC& entry);

    // 卸载 DLL。未加载时调用是安全的。调用前必须先关闭模块（停线程、销毁窗口）。
    void Free();

    // 是否处于加载状态。
    bool IsLoaded() const { return m_module != nullptr; }

    // 加载结果的英文说明，供写日志。
    //   r：加载结果。
    static const wchar_t* DescribeResult(PluginLoadResult r);

private:
    HMODULE m_module;   // 已加载的 DLL；未加载时为 nullptr
};

} // namespace flamingoadmin
