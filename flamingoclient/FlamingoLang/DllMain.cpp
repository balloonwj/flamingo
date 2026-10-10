/**
 *  FlamingoLang.dll 的入口、唯一的导出函数 FlamingoLang_GetApi，以及从 DLL 资源读取 .po 译文的数据来源。
 *
 *  导出名由 FlamingoLang.def 固定，不受调用约定与名称修饰的影响，32 位与 64 位相同。
 *  宿主用 GetProcAddress(FLAMINGO_LANG_ENTRY_NAME) 取得它，见 FlamingoLangApi.h。
 *
 *  本文件只编进 DLL；单元测试直接编译 LangModule.cpp 等源文件，并换用内存中的数据来源。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include <windows.h>

#include "FlamingoLangApi.h"
#include "LangModule.h"

namespace {

//本 DLL 的模块句柄，DLL_PROCESS_ATTACH 时记下，读取资源用
HMODULE g_hLangModule = NULL;

/**
 *  从本 DLL 的 RCDATA 资源中取某种语言的 .po 原始字节。资源随 DLL 映射在内存中，DLL 卸载前一直有效。
 *  参数与返回值的含义见 LangModule.h 的 PoSourceFn。
 */
bool LoadPoFromResource(const wchar_t* code, int resourceId, const char** data, size_t* size)
{
    (void)code;
    if (g_hLangModule == NULL || data == NULL || size == NULL)
        return false;

    HRSRC res = ::FindResourceW(g_hLangModule, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (res == NULL)
        return false;
    HGLOBAL handle = ::LoadResource(g_hLangModule, res);
    if (handle == NULL)
        return false;
    const void* bytes = ::LockResource(handle);
    DWORD length = ::SizeofResource(g_hLangModule, res);
    if (bytes == NULL || length == 0)
        return false;

    *data = static_cast<const char*>(bytes);
    *size = (size_t)length;
    return true;
}

} // namespace

/**
 *  DLL 入口。只记下模块句柄、关掉线程创建 / 退出时的通知，不在加载器锁内做任何其它事。
 */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    switch (reason)
    {
    //DLL 被加载进进程：记下模块句柄，关掉线程创建 / 退出的通知（本 DLL 用不到）
    case DLL_PROCESS_ATTACH:
        g_hLangModule = hModule;
        ::DisableThreadLibraryCalls(hModule);
        break;

    //DLL 被卸载：已解析的译文随进程内存一起释放，这里没有要做的事
    case DLL_PROCESS_DETACH:
        break;

    //其它通知已在 DLL_PROCESS_ATTACH 时关闭，不会收到
    default:
        break;
    }
    return TRUE;
}

/**
 *  导出函数：取语言包的函数表。首次调用时把译文数据来源设为本 DLL 的资源。
 *  @return DLL 持有的函数表，永不为 NULL，在 DLL 卸载前一直有效
 */
extern "C" const flamingolang::LangApi* __cdecl FlamingoLang_GetApi()
{
    flamingolang::CLangModule::Instance().SetPoSource(LoadPoFromResource);
    return flamingolang::GetLangApiTable();
}
