/**
 *  FlamingoBot.dll 的入口与唯一的导出函数 FlamingoBot_Create。
 *
 *  导出名由 FlamingoBot.def 固定，不受调用约定与名称修饰的影响，32 位与 64 位相同。
 *  宿主用 GetProcAddress(FLAMINGO_BOT_FACTORY_NAME) 取得它，见 BotApi.h。
 *
 *  本文件还定义 DLL 自己的 _Module：balloonui 的 ATL 窗口类引用全局 _Module（声明见 stdafx.h），
 *  exe 那一份在另一个模块里，DLL 必须有自己的一份。
 *
 *  导出函数在返回模块对象之前把窗口工厂（UI/BotUiFactory）登记给模块。单元测试只编译模块与 Core，
 *  不经过这里，模块没有工厂时打开窗口直接返回 false（见 CBotModule::SetWindowFactory）。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"

#include "BotApi.h"
#include "Host/BotModule.h"
#include "UI/BotUiFactory.h"

//DLL 自己的 ATL / WTL 模块对象：进程加载本 DLL 时初始化，卸载时释放
CAppModule _Module;

/**
 *  DLL 入口。除初始化 _Module 外不做任何事（加载器锁内不宜做复杂操作），
 *  模块的初始化统一放在 IBotModule::Init。
 */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    switch (reason)
    {
    //进程加载本 DLL：初始化 _Module，并关闭线程挂接通知（本模块不需要 DLL_THREAD_ATTACH / DETACH）
    case DLL_PROCESS_ATTACH:
        ::DisableThreadLibraryCalls(hModule);
        if (FAILED(_Module.Init(NULL, hModule)))
            return FALSE;
        break;

    //进程卸载本 DLL：释放 _Module（宿主已在卸载前调用过 IBotModule::Shutdown，窗口都已销毁）
    case DLL_PROCESS_DETACH:
        _Module.Term();
        break;

    //线程挂接 / 分离通知：已在加载时关闭，正常情况下不会走到这里
    default:
        break;
    }
    return TRUE;
}

/**
 *  导出的工厂函数：按接口版本号返回模块对象。
 *  @param apiVersion 宿主编译时的 kApiVersion
 *  @return 版本一致时返回模块对象（归 DLL 所有，宿主不能释放）；不一致返回 NULL
 */
extern "C" flamingobot::IBotModule* __cdecl FlamingoBot_Create(uint32_t apiVersion)
{
    if (!flamingobot::IsApiVersionCompatible(apiVersion, flamingobot::kApiVersion))
        return NULL;
    flamingobot::CBotModule& module = flamingobot::CBotModule::Instance();
    module.SetWindowFactory(&flamingobot::CBotUiFactory::Instance());
    return &module;
}
