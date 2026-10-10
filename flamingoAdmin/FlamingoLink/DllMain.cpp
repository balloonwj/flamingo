/**
 *  FlamingoLink.dll（管理控制台专用）的入口与唯一的导出函数 FlamingoLink_Create。
 *
 *  导出名由 FlamingoLink.def 固定，不受调用约定与名称修饰的影响，32 位与 64 位相同。
 *  宿主用 GetProcAddress(FLAMINGO_LINK_FACTORY_NAME) 取得它，见 FlamingoLinkApi.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "FlamingoLink/FlamingoLinkApi.h"
#include "FlamingoLink/LinkModule.h"

//本 DLL 自己的 ATL / WTL 模块对象。balloonui 的窗口类经它取模块实例句柄注册窗口类
CAppModule _Module;

/**
 *  DLL 入口。只做 ATL 模块对象的初始化与收尾，业务初始化一律放在 ILinkModule::Init，
 *  不在加载器锁内做复杂操作。
 */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    switch (reason)
    {
    //DLL 被加载进进程：初始化 ATL 模块对象，并关掉线程创建 / 退出的通知（本 DLL 用不到）
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
extern "C" flamingolink::ILinkModule* __cdecl FlamingoLink_Create(uint32_t apiVersion)
{
    if (!flamingolink::IsApiVersionCompatible(apiVersion, flamingolink::kApiVersion))
        return NULL;
    return &flamingolink::CLinkModule::Instance();
}
