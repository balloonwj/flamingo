/**
 *  单测程序的进程级设置：关闭崩溃与报错对话框，TestCrashDialogSetup.cpp
 *
 *  Debug 版运行库在 abort()、断言失败、无效参数时会弹出模态对话框，等人点按钮。单测常在共用桌面上、由脚本或其它会话
 *  启动，弹框既打扰正在用桌面的人，又让测试进程挂住不退出。本文件用一个静态对象在 main 之前完成设置：
 *    - abort() 不弹框、不写 Windows 错误报告，进程直接以退出码 3 结束；
 *    - Debug 运行库的断言与错误报告改为写到标准错误；
 *    - 系统级的崩溃对话框（未处理异常、加载失败）不弹出；
 *    - 设置了环境变量 FLAMINGO_TEST_MINIDUMP_DIR 时，第一次发生访问冲突就在该目录写一份 minidump
 *      （FlamingoUnit_<进程号>.dmp），供排查只在不挂调试器时才出现的偶发崩溃；没设置时不做任何事；
 *    - 本进程不加载输入法（IMM 与 TSF 都禁用）。2026-10-04 用上面的 minidump 查出：桌面上的第三方输入法（华宇拼音的
 *      HuayuPYTSF.dll）在测试窗口获得焦点时载入本进程、窗口销毁后又被卸载，它留在消息队列里的消息随后被用例抽取分发，
 *      跳进已卸载的代码（转储里崩溃地址为 <Unloaded_HuayuPYTSF.dll>+0x6425c），FSD7、FSD11 在机器负载高时约 5% 的概率
 *      以 SIGSEGV 失败。用例都用 WM_CHAR 直接投递文字，不需要输入法。
 *  只影响本测试程序，不影响产品代码。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <windows.h>
#include <imm.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <crtdbg.h>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "imm32.lib")

namespace {

//ImmDisableIME / ImmDisableTextFrameService 的线程参数取这个值时作用于本进程的全部线程
const DWORD kAllThreadsInProcess = static_cast<DWORD>(-1);

//写 minidump 的目录所在的环境变量
const wchar_t* const kMiniDumpDirEnv = L"FLAMINGO_TEST_MINIDUMP_DIR";

//minidump 的内容：线程栈、被栈引用的内存、全局数据与句柄，以及已卸载模块的清单（崩在已卸载 DLL 的代码里时，据此查出
//那段地址原来属于哪个 DLL），足够在 cdb 里看清崩溃现场
const MINIDUMP_TYPE kMiniDumpType = static_cast<MINIDUMP_TYPE>(
    MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo
    | MiniDumpWithUnloadedModules);

//写 minidump 的目录（构造时从环境变量读入）；为空表示不写
wchar_t g_miniDumpDir[MAX_PATH] = { 0 };

//是否已写过一份（只写第一次访问冲突）
volatile LONG g_miniDumpWritten = 0;

/**
 *  向量化异常处理函数：第一次访问冲突时写 minidump，然后交还给后续的处理（Catch2 照常报告 SIGSEGV）。
 *  @param info 异常信息
 *  @return 总是 EXCEPTION_CONTINUE_SEARCH，不改变原有的处理流程
 */
LONG CALLBACK WriteMiniDumpOnAccessViolation(EXCEPTION_POINTERS* info)
{
    if (info == NULL || info->ExceptionRecord == NULL || info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    if (::InterlockedExchange(&g_miniDumpWritten, 1) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    wchar_t path[MAX_PATH] = { 0 };
    swprintf_s(path, L"%s\\FlamingoUnit_%lu.dmp", g_miniDumpDir, ::GetCurrentProcessId());
    HANDLE file = ::CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return EXCEPTION_CONTINUE_SEARCH;
    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = ::GetCurrentThreadId();
    mei.ExceptionPointers = info;
    mei.ClientPointers = FALSE;
    ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file, kMiniDumpType, &mei, NULL, NULL);
    ::CloseHandle(file);
    return EXCEPTION_CONTINUE_SEARCH;
}

/**
 *  构造时完成进程级设置的静态对象。
 */
class CrashDialogSetup
{
public:
    CrashDialogSetup()
    {
        //abort()：不显示消息框，也不调用 Windows 错误报告
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        //Debug 运行库的断言与错误报告：写到标准错误，不弹框
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        //系统级的错误对话框：未处理异常与严重错误都不弹框
        ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        //不加载输入法：须在任何线程建窗口之前调用（原因见文件头）
        ::ImmDisableIME(kAllThreadsInProcess);
        ::ImmDisableTextFrameService(kAllThreadsInProcess);
        //排查偶发崩溃用：设置了环境变量才登记，排在最前面，先于 Catch2 的处理函数看到异常
        const DWORD n = ::GetEnvironmentVariableW(kMiniDumpDirEnv, g_miniDumpDir, MAX_PATH);
        if (n > 0 && n < MAX_PATH)
            ::AddVectoredExceptionHandler(1, WriteMiniDumpOnAccessViolation);
        else
            g_miniDumpDir[0] = L'\0';
    }
};

//在 main 之前构造
CrashDialogSetup g_crashDialogSetup;

} // namespace
