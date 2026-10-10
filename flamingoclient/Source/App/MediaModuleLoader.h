/**
 *  FlamingoMedia.dll 的加载器：负责动态加载 DLL、取导出的工厂函数、校验接口版本，并在
 *  退出时卸载。加载失败时给出明确原因，供调用方写日志；exe 在加载失败时照常运行，只是
 *  音视频类功能不可用。
 *
 *  本类不调用模块的 Init / Shutdown，也不写日志，只管 DLL 本身的装卸，便于单元测试
 *  （Tests/MediaModuleLoaderTests.cpp）。调用方须在 Unload 之前先调用模块的 Shutdown。
 *
 *  典型用法（见 AppShell.cpp）：
 *
 *      balloonapp::CMediaModuleLoader loader;
 *      balloonapp::MediaLoadResult r = loader.Load(dllPath, flamingomedia::kApiVersion);
 *      if (r == balloonapp::media_load_ok)
 *          loader.GetModule()->Init(&host, &params);
 *      else
 *          LOG_WARNING("media module unavailable: %s.", balloonapp::CMediaModuleLoader::DescribeResult(r));
 *      ...
 *      loader.GetModule()->Shutdown();
 *      loader.Unload();
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include "../../FlamingoMedia/Api/FlamingoMediaApi.h"

namespace balloonapp {

//Load 的结果。
enum MediaLoadResult
{
    media_load_ok               = 0,   //加载成功，GetModule 可用
    media_load_bad_path         = 1,   //传入的路径为 NULL 或空串
    media_load_not_found        = 2,   //路径上没有这个文件（或它依赖的 DLL 缺失）
    media_load_failed           = 3,   //文件存在但无法加载（如 32 位 / 64 位不匹配、文件损坏）
    media_load_entry_missing    = 4,   //加载成功但没有导出 FlamingoMedia_Create，不是本模块
    media_load_version_mismatch = 5,   //工厂函数拒绝了宿主的接口版本号（返回 NULL）
};

/**
 *  FlamingoMedia.dll 的加载器。不可复制；析构时自动卸载。只在 UI 线程使用。
 */
class CMediaModuleLoader
{
public:
    CMediaModuleLoader();

    //析构时若仍处于加载状态则卸载（调用方应已先调用模块的 Shutdown）。
    ~CMediaModuleLoader();

    CMediaModuleLoader(const CMediaModuleLoader&) = delete;
    CMediaModuleLoader& operator=(const CMediaModuleLoader&) = delete;

    /**
     *  加载 DLL 并取得模块对象。已处于加载状态时先卸载再重新加载。
     *  @param dllPath    DLL 的完整路径（UTF-16），为 NULL 或空串时返回 media_load_bad_path
     *  @param apiVersion 宿主编译时的接口版本号，一般传 flamingomedia::kApiVersion
     *  @return 结果，见 enum MediaLoadResult；非 media_load_ok 时本对象保持未加载状态
     */
    MediaLoadResult Load(const wchar_t* dllPath, uint32_t apiVersion);

    /**
     *  卸载 DLL。未加载时调用是安全的。卸载后 GetModule 返回 NULL。
     *  调用前必须先调用模块的 Shutdown，否则 DLL 里仍在运行的线程与窗口会随代码一起被卸掉。
     */
    void Unload();

    /**
     *  取模块对象。
     *  @return 已加载时返回模块对象（归 DLL 所有，不可释放）；未加载返回 NULL
     */
    flamingomedia::IMediaModule* GetModule() const;

    //是否处于加载状态。
    bool IsLoaded() const;

    /**
     *  把加载结果转成英文描述，供写日志。
     *  @param result 加载结果
     *  @return 静态字符串，不需要释放；不认识的值返回 "unknown"
     */
    static const char* DescribeResult(MediaLoadResult result);

private:
    HMODULE                      m_hModule;   //DLL 模块句柄；未加载时为 NULL
    flamingomedia::IMediaModule* m_module;    //工厂函数返回的模块对象（归 DLL 所有）；未加载时为 NULL
};

} // namespace balloonapp
