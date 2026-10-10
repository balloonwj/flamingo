/**
 *  FlamingoP2P.dll 的入口与唯一的导出函数 FlamingoP2P_Create。
 *
 *  导出名由 FlamingoP2P.def 固定，不受调用约定与名称修饰的影响，32 位与 64 位相同。
 *  宿主用 GetProcAddress(FLAMINGO_P2P_FACTORY_NAME) 取得它，见 FlamingoP2PApi.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "FlamingoP2PApi.h"
#include "Host/P2PModule.h"

/**
 *  DLL 入口。只关掉每条线程创建 / 退出时的通知，业务初始化一律放在 IP2PModule::Init，
 *  不在加载器锁内做任何复杂操作（包括 sodium_init 与 Winsock 初始化）。
 */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    switch (reason)
    {
    //DLL 被加载进进程：关掉线程创建 / 退出的通知（本 DLL 用不到）
    case DLL_PROCESS_ATTACH:
        ::DisableThreadLibraryCalls(hModule);
        break;

    //DLL 被卸载：模块的收尾在 IP2PModule::Shutdown 里做，宿主卸载前必须先调用它，这里没有要做的事
    case DLL_PROCESS_DETACH:
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
extern "C" p2p::IP2PModule* __cdecl FlamingoP2P_Create(uint32_t apiVersion)
{
    if (!p2p::IsApiVersionCompatible(apiVersion, p2p::kApiVersion))
        return NULL;
    return &p2p::CP2PModule::Instance();
}
