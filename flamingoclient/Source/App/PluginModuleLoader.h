/**
 *  功能模块 DLL 的通用加载器：动态加载 DLL、按名称取导出的工厂函数、把宿主的接口版本号交给
 *  工厂函数换取模块对象，并在退出时卸载。适用于「只导出一个工厂函数、工厂函数只接收接口版本号、
 *  返回归 DLL 所有的模块对象」这一形态的功能模块（FlamingoFileShare.dll 即是，见
 *  docs/文件共享方案.md 7.2；FlamingoMedia.dll 的形态相同，其专用加载器 CMediaModuleLoader
 *  日后可改为本模板的一个实例）。
 *
 *  分两层：
 *    - CPluginLibrary：与模块接口类型无关的部分，负责 LoadLibraryExW / GetProcAddress / FreeLibrary，
 *      实现放在 PluginModuleLoader.cpp，只编译一份；
 *    - CPluginModuleLoader<TModule>：按模块接口类型区分的模板外壳，负责调用工厂函数、持有模块指针。
 *
 *  本类不调用模块的初始化与关闭方法，也不写日志，只管 DLL 本身的装卸，便于单元测试
 *  （Tests/PluginModuleLoaderTests.cpp）。调用方须在 Unload 之前先关闭模块。
 *
 *  典型用法：
 *
 *      balloonapp::CPluginModuleLoader<fileshare::IFileShareModule> loader;
 *      balloonapp::PluginLoadResult r = loader.Load(dllPath, FLAMINGO_FILESHARE_FACTORY_NAME,
 *                                                   fileshare::kApiVersion);
 *      if (r == balloonapp::plugin_load_ok)
 *          loader.GetModule()->Init(&host, &params);
 *      else
 *          LOG_WARNING("file share module unavailable: %s.", balloonapp::CPluginLibrary::DescribeResult(r));
 *      ...
 *      loader.GetModule()->Shutdown();
 *      loader.Unload();
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include <stdint.h>

namespace balloonapp {

//加载结果。
enum PluginLoadResult
{
    plugin_load_ok               = 0,   //加载成功，GetModule 可用
    plugin_load_bad_path         = 1,   //传入的 DLL 路径或工厂函数名为 NULL 或空串
    plugin_load_not_found        = 2,   //路径上没有这个文件，或它依赖的 DLL 缺失
    plugin_load_failed           = 3,   //文件存在但无法加载（如 32 位 / 64 位不匹配、文件损坏）
    plugin_load_entry_missing    = 4,   //加载成功但没有导出指定名称的工厂函数，说明不是期望的模块
    plugin_load_version_mismatch = 5,   //工厂函数拒绝了宿主的接口版本号（返回 NULL）
};

/**
 *  DLL 装卸与导出函数获取，与模块接口类型无关。不可复制；析构时自动卸载。只在 UI 线程使用。
 */
class CPluginLibrary
{
public:
    CPluginLibrary();

    //析构时若仍处于加载状态则卸载。
    ~CPluginLibrary();

    CPluginLibrary(const CPluginLibrary&) = delete;
    CPluginLibrary& operator=(const CPluginLibrary&) = delete;

    /**
     *  加载 DLL 并取指定名称的导出函数。已处于加载状态时先卸载再重新加载。
     *  DLL 以 LOAD_WITH_ALTERED_SEARCH_PATH 方式加载：它自己的依赖从它所在的目录开始查找。
     *  调用方应传完整路径，避免按系统搜索顺序加载到同名的其它 DLL。
     *  @param dllPath     DLL 的完整路径（UTF-16），为 NULL 或空串时返回 plugin_load_bad_path
     *  @param factoryName 导出函数名（ANSI，GetProcAddress 使用），为 NULL 或空串时返回 plugin_load_bad_path
     *  @param entry       出参：成功时为导出函数的地址，失败时置为 NULL
     *  @return 结果；非 plugin_load_ok 时本对象保持未加载状态
     */
    PluginLoadResult Load(const wchar_t* dllPath, const char* factoryName, FARPROC& entry);

    /**
     *  卸载 DLL。未加载时调用是安全的。
     *  调用前必须先关闭模块，否则 DLL 里仍在运行的线程与窗口会随代码一起被卸掉。
     */
    void Free();

    //是否处于加载状态。
    bool IsLoaded() const;

    /**
     *  把加载结果转成英文描述，供写日志。
     *  @param result 加载结果
     *  @return 静态字符串，不需要释放；不认识的值返回 "unknown"
     */
    static const char* DescribeResult(PluginLoadResult result);

private:
    HMODULE m_hModule;   //DLL 模块句柄；未加载时为 NULL
};

/**
 *  功能模块的加载器模板。TModule 为模块接口类型，其对象由 DLL 自己持有，宿主不得释放。
 *  工厂函数的签名约定为 TModule* __cdecl Create(uint32_t apiVersion)，版本不符时返回 NULL。
 *  不可复制；析构时自动卸载。只在 UI 线程使用。
 */
template <typename TModule>
class CPluginModuleLoader
{
public:
    //工厂函数的类型。
    typedef TModule* (__cdecl* CreateFn)(uint32_t apiVersion);

    CPluginModuleLoader()
        : m_module(NULL)
    {
    }

    //析构时若仍处于加载状态则卸载（调用方应已先关闭模块）。
    ~CPluginModuleLoader()
    {
        Unload();
    }

    CPluginModuleLoader(const CPluginModuleLoader&) = delete;
    CPluginModuleLoader& operator=(const CPluginModuleLoader&) = delete;

    /**
     *  加载 DLL 并经工厂函数取得模块对象。已处于加载状态时先卸载再重新加载。
     *  @param dllPath     DLL 的完整路径（UTF-16）
     *  @param factoryName 导出的工厂函数名（ANSI）
     *  @param apiVersion  宿主编译时的接口版本号，原样交给工厂函数
     *  @return 结果；非 plugin_load_ok 时本对象保持未加载状态，DLL 已被卸载
     */
    PluginLoadResult Load(const wchar_t* dllPath, const char* factoryName, uint32_t apiVersion)
    {
        Unload();

        FARPROC entry = NULL;
        PluginLoadResult result = m_library.Load(dllPath, factoryName, entry);
        if (result != plugin_load_ok)
            return result;

        CreateFn create = reinterpret_cast<CreateFn>(entry);
        TModule* module = create(apiVersion);
        if (module == NULL)
        {
            m_library.Free();
            return plugin_load_version_mismatch;
        }

        m_module = module;
        return plugin_load_ok;
    }

    /**
     *  卸载 DLL。未加载时调用是安全的。卸载后 GetModule 返回 NULL。
     *  调用前必须先关闭模块。
     */
    void Unload()
    {
        m_module = NULL;
        m_library.Free();
    }

    /**
     *  取模块对象。
     *  @return 已加载时返回模块对象（归 DLL 所有，不可释放）；未加载返回 NULL
     */
    TModule* GetModule() const
    {
        return m_module;
    }

    //是否处于加载状态。
    bool IsLoaded() const
    {
        return m_module != NULL;
    }

private:
    CPluginLibrary m_library;   //DLL 本身的装卸
    TModule*       m_module;    //工厂函数返回的模块对象（归 DLL 所有）；未加载时为 NULL
};

} // namespace balloonapp
