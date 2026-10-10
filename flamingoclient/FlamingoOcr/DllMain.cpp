/**
 *  FlamingoOcr.dll 的入口与唯一的导出函数 FlamingoOcr_GetApi。
 *
 *  导出名由 FlamingoOcr.def 固定，不受调用约定与名称修饰的影响，32 位与 64 位相同。
 *  宿主用 GetProcAddress(FLAMINGO_OCR_ENTRY_NAME) 取得它，见 FlamingoOcrApi.h。
 *
 *  本文件只编进 DLL；单元测试直接编译 OcrApiTable.cpp 等源文件，并换上假后端。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include <windows.h>

#include "FlamingoOcrApi.h"
#include "OcrApiTable.h"
#include "WinOcr/WinOcrEngine.h"

/**
 *  DLL 入口。只关掉线程创建 / 退出时的通知，不在加载器锁内做任何其它事
 *  （GDI+ 与 WinRT 的初始化都推迟到 m_init，在宿主的预加载线程上进行）。
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

    //DLL 被卸载：宿主加载后不卸载，进程退出时随进程内存一起回收，这里没有要做的事
    case DLL_PROCESS_DETACH:
        break;

    //其它通知已在 DLL_PROCESS_ATTACH 时关闭，不会收到
    default:
        break;
    }
    return TRUE;
}

/**
 *  导出函数：取识别引擎的函数表。首次调用时装上正式的引擎后端。
 *  @return DLL 持有的函数表，永不为 NULL，在 DLL 卸载前一直有效
 */
extern "C" const flamingoocr::OcrApi* __cdecl FlamingoOcr_GetApi()
{
    //只有第一次调用生效；替换引擎时把这里换成别的后端即可，函数表与接口约定不变
    flamingoocr::InstallOcrBackend(flamingoocr::GetWinOcrBackend());
    return flamingoocr::GetOcrApiTable();
}
