/**
 *  FlamingoFileShare.dll 的入口与唯一的导出函数 FlamingoFileShare_Create。
 *
 *  导出名由 FlamingoFileShare.def 固定，不受调用约定与名称修饰的影响，32 位与 64 位相同。
 *  宿主用 GetProcAddress(FLAMINGO_FILESHARE_FACTORY_NAME) 取得它，见 FileShareApi.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "FileShareApi.h"
#include "FileShareModule.h"

//本 DLL 自己的 ATL / WTL 模块对象。balloonui 的窗口类经它取模块实例句柄注册窗口类，
//故 DLL 里注册的窗口类归 DLL 所有，与 exe 里同名的窗口类互不冲突（见 docs/文件共享方案.md 3.11）。
CAppModule _Module;

/**
 *  DLL 入口。只做 ATL 模块对象的初始化与收尾（经典 ATL DLL 的做法），业务初始化一律放在
 *  IFileShareModule::Init，不在加载器锁内做复杂操作。
 */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    switch (reason)
    {
    //DLL 被加载进进程：初始化 ATL 模块对象，并关掉每条线程创建 / 退出时的通知（本 DLL 用不到）
    case DLL_PROCESS_ATTACH:
        ::DisableThreadLibraryCalls(hModule);
        if (FAILED(_Module.Init(NULL, hModule)))
            return FALSE;
        break;

    //DLL 被卸载：收尾 ATL 模块对象
    case DLL_PROCESS_DETACH:
        _Module.Term();
        break;

    //线程创建与退出的通知已被 DisableThreadLibraryCalls 关掉，不会走到这里
    default:
        break;
    }
    return TRUE;
}

/**
 *  导出的工厂函数：按接口版本号返回模块对象。
 *  @param apiVersion 宿主编译时的 kApiVersion
 *  @return 版本兼容时返回模块对象（归 DLL 所有，宿主不能释放）；不兼容返回 NULL
 */
extern "C" fileshare::IFileShareModule* __cdecl FlamingoFileShare_Create(uint32_t apiVersion)
{
    if (!fileshare::IsApiVersionCompatible(apiVersion, fileshare::kApiVersion))
        return NULL;
    return &fileshare::CFileShareModule::Instance();
}
