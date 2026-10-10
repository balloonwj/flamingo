/**
 *  FlamingoCaption.dll 的入口与唯一的导出函数 FlamingoCaption_Create。
 *
 *  导出名由 FlamingoCaption.def 固定，不受调用约定与名称修饰的影响，32 位与 64 位相同。
 *  FlamingoMedia 用 GetProcAddress(FLAMINGO_CAPTION_FACTORY_NAME) 取得它，见 CaptionApi.h。
 *
 *  本文件还定义 DLL 自己的 _Module：编辑器窗口等界面代码与 balloonui 的 ATL 窗口类都引用全局 _Module
 *  （声明见 stdafx.h），exe 与 FlamingoMedia 各有自己的一份，本 DLL 也必须有自己的一份。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionApi.h"
#include "Host/CaptionModule.h"

//DLL 自己的 ATL / WTL 模块对象：进程加载本 DLL 时初始化，卸载时释放
CAppModule _Module;

/**
 *  DLL 入口。除初始化 _Module 外不做任何事（加载器锁内不宜做复杂操作），
 *  模块的初始化统一放在 ICaptionModule::Init。
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

    //进程卸载本 DLL：释放 _Module（调用方已在卸载前调用过 ICaptionModule::Shutdown，窗口都已销毁）
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
 *  @param apiVersion 调用方编译时的 kApiVersion
 *  @return 版本兼容时返回模块对象（归 DLL 所有，调用方不能释放）；不兼容返回 NULL
 */
extern "C" caption::ICaptionModule* __cdecl FlamingoCaption_Create(uint32_t apiVersion)
{
    if (!caption::IsApiVersionCompatible(apiVersion, caption::kApiVersion))
        return NULL;
    return &caption::CCaptionModule::Instance();
}
