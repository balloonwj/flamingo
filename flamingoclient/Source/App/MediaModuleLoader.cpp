/**
 *  FlamingoMedia.dll 加载器的实现，见 MediaModuleLoader.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "MediaModuleLoader.h"
#include "../Common/Util/QuietLoadLibrary.h"   // LoadLibraryQuietly：DLL 损坏时不让系统弹框卡住启动

namespace balloonapp {

CMediaModuleLoader::CMediaModuleLoader()
    : m_hModule(NULL)
    , m_module(NULL)
{
}

CMediaModuleLoader::~CMediaModuleLoader()
{
    Unload();
}

MediaLoadResult CMediaModuleLoader::Load(const wchar_t* dllPath, uint32_t apiVersion)
{
    Unload();
    if (dllPath == NULL || dllPath[0] == L'\0')
    {
        return media_load_bad_path;
    }

    //LOAD_WITH_ALTERED_SEARCH_PATH：DLL 自身的依赖从它所在目录开始找，而不是从 exe 的当前目录找。
    //加载期间屏蔽系统错误框：DLL 损坏、被截断、不是 DLL 时只返回失败，不弹「损坏的映像」框（2026-10-04 起）
    HMODULE h = quietload::LoadLibraryQuietly(dllPath);
    if (h == NULL)
    {
        DWORD err = ::GetLastError();
        //文件或路径不存在、或它依赖的 DLL 缺失，统一归为「找不到」；其余（位数不符、文件损坏）归为「无法加载」
        if (err == ERROR_MOD_NOT_FOUND || err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
        {
            return media_load_not_found;
        }
        return media_load_failed;
    }

    flamingomedia::FlamingoMediaCreateFn create =
        reinterpret_cast<flamingomedia::FlamingoMediaCreateFn>(::GetProcAddress(h, FLAMINGO_MEDIA_FACTORY_NAME));
    if (create == NULL)
    {
        ::FreeLibrary(h);
        return media_load_entry_missing;
    }

    flamingomedia::IMediaModule* module = create(apiVersion);
    if (module == NULL)
    {
        ::FreeLibrary(h);
        return media_load_version_mismatch;
    }

    m_hModule = h;
    m_module = module;
    return media_load_ok;
}

void CMediaModuleLoader::Unload()
{
    m_module = NULL;
    if (m_hModule != NULL)
    {
        ::FreeLibrary(m_hModule);
        m_hModule = NULL;
    }
}

flamingomedia::IMediaModule* CMediaModuleLoader::GetModule() const
{
    return m_module;
}

bool CMediaModuleLoader::IsLoaded() const
{
    return m_module != NULL;
}

const char* CMediaModuleLoader::DescribeResult(MediaLoadResult result)
{
    switch (result)
    {
    //加载成功
    case media_load_ok:
        return "ok";
    //传入的路径为空
    case media_load_bad_path:
        return "empty dll path";
    //文件不存在或其依赖缺失
    case media_load_not_found:
        return "dll not found";
    //文件存在但无法加载
    case media_load_failed:
        return "dll could not be loaded";
    //缺少导出函数
    case media_load_entry_missing:
        return "factory export missing";
    //接口版本不一致
    case media_load_version_mismatch:
        return "api version mismatch";
    //不认识的取值
    default:
        return "unknown";
    }
}

} // namespace balloonapp
