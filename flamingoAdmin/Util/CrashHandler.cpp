/**
 *  flamingoAdmin —— 崩溃捕获实现。
 *
 *  filter 入口最早处写 minidump(.dmp 文件,即使后续 StackWalk 自己再崩,
 *  dump 也已经保下来),然后尝试在线 StackWalk + 符号化把调用栈写到日志。
 *  栈帧损毁时在线 StackWalk 会走不下去,但 minidump 仍然完整保存了进程现场,
 *  可用 cdb / VS 离线分析(.ecxr / k / !analyze -v)。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Util/CrashHandler.h"
#include "Util/Logger.h"

#include <dbghelp.h>
#include <tchar.h>
#pragma comment(lib, "dbghelp.lib")

namespace flamingoadmin {

namespace {

// 调用栈最多回溯的帧数。
const int kMaxStackFrames = 40;

// 在 filter 入口最早处写 minidump 文件。
//   路径:与 flamingoAdmin.exe 同目录的 crash_YYYYMMDD_HHMMSS_<PID>.dmp。
//   ep:filter 收到的 EXCEPTION_POINTERS,会被透传给 MiniDumpWriteDump 让
//      dump 携带异常上下文(cdb 用 .ecxr 切到崩溃现场)。
//
//   filter 上下文是脆弱状态(heap 可能已破坏),本函数严格只用栈缓冲与
//   Win32 / dbghelp API,不分配堆、不调 STL。
void WriteCrashMiniDump(EXCEPTION_POINTERS* ep)
{
    // 1) 路径 = exe 所在目录 \ crash_YYYYMMDD_HHMMSS_<PID>.dmp
    TCHAR exePath[MAX_PATH] = { 0 };
    ::GetModuleFileName(nullptr, exePath, MAX_PATH);
    // 砍掉 exe 文件名,只留目录。
    for (int i = static_cast<int>(_tcslen(exePath)) - 1; i >= 0; --i)
    {
        if (exePath[i] == _T('\\') || exePath[i] == _T('/'))
        {
            exePath[i] = _T('\0');
            break;
        }
    }

    SYSTEMTIME st = { 0 };
    ::GetLocalTime(&st);

    TCHAR dumpPath[MAX_PATH] = { 0 };
    _sntprintf_s(dumpPath, MAX_PATH, _TRUNCATE,
                 _T("%s\\crash_%04d%02d%02d_%02d%02d%02d_%lu.dmp"),
                 exePath,
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond,
                 ::GetCurrentProcessId());

    HANDLE hFile = ::CreateFile(dumpPath, GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        FA_LOGE(_T("MiniDump 写入失败: CreateFile error=%lu path=%s"),
                ::GetLastError(), dumpPath);
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei = { 0 };
    mei.ThreadId          = ::GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers    = FALSE;

    // dump 类型组合:
    //   MiniDumpWithDataSegs                —— 含全局 / 静态变量段,便于看出
    //                                          整段进程数据状态。
    //   MiniDumpWithIndirectlyReferencedMemory —— 间接通过指针引用到的堆内存,
    //                                              帮 cdb 把数据结构展开。
    //   MiniDumpWithThreadInfo              —— 含所有线程的状态信息(优先级 /
    //                                          start address / 等)。
    //   MiniDumpWithUnloadedModules         —— 即使模块已卸载也保留信息,
    //                                          避免 cdb 解 frame 时缺模块。
    //
    // 这套组合通常单文件几 MB ~ 几十 MB,足够 cdb 还原 StackWalk + 局部变量。
    const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithDataSegs |
        MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules);

    BOOL ok = ::MiniDumpWriteDump(::GetCurrentProcess(),
                                  ::GetCurrentProcessId(),
                                  hFile,
                                  dumpType,
                                  ep != nullptr ? &mei : nullptr,
                                  nullptr, nullptr);
    DWORD lastErr = ::GetLastError();
    ::CloseHandle(hFile);

    if (ok)
    {
        FA_LOGE(_T("================ MiniDump 已写入 ================"));
        FA_LOGE(_T("路径: %s"), dumpPath);
        FA_LOGE(_T("分析: 用 cdb 或 VS 打开 .dmp,先 .ecxr 切到崩溃现场,"));
        FA_LOGE(_T("       再 k / !analyze -v 看完整调用栈与线程状态。"));
        FA_LOGE(_T("================================================"));
    }
    else
    {
        FA_LOGE(_T("MiniDumpWriteDump 失败 error=%lu path=%s"),
                lastErr, dumpPath);
    }
}

// 未处理异常过滤器:先写 minidump 保现场,再尝试在线 StackWalk 把符号化栈
// 写日志,最后让进程退出。
LONG WINAPI CrashExceptionFilter(EXCEPTION_POINTERS* exceptionInfo)
{
    if (exceptionInfo == nullptr || exceptionInfo->ExceptionRecord == nullptr)
    {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    const EXCEPTION_RECORD* rec = exceptionInfo->ExceptionRecord;
    FA_LOGE(_T("================ 程序崩溃 ================"));
    FA_LOGE(_T("异常码 = 0x%08X  出错地址 = 0x%p"),
            rec->ExceptionCode, rec->ExceptionAddress);

    // 1) 最先写 minidump —— 即使后续 StackWalk 自己出错,dump 已经保下来,
    //    cdb 离线还能还原完整现场(尤其栈被在线 unwind 解不出来的场景)。
    WriteCrashMiniDump(exceptionInfo);

    // 2) 在线尝试 StackWalk + 符号化,给一个"看日志就知道大致位置"的视角。
    //    如果栈被破坏 / unwind info 缺失,这步可能只能输出一两帧。
    HANDLE process = ::GetCurrentProcess();
    HANDLE thread  = ::GetCurrentThread();

    ::SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    ::SymInitialize(process, nullptr, TRUE);

    // 以崩溃现场的 CONTEXT 为起点回溯调用栈。寄存器名按目标平台切换:
    // x64 用 Rip/Rbp/Rsp + IMAGE_FILE_MACHINE_AMD64;
    // x86 用 Eip/Ebp/Esp + IMAGE_FILE_MACHINE_I386。
    CONTEXT context = *exceptionInfo->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
#if defined(_M_X64)
    frame.AddrPC.Offset    = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
    const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_IX86)
    frame.AddrPC.Offset    = context.Eip;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrStack.Offset = context.Esp;
    const DWORD machineType = IMAGE_FILE_MACHINE_I386;
#else
#   error "CrashHandler 仅支持 x86 / x64"
#endif

    // 符号信息缓冲区:SYMBOL_INFO 结构 + 名字字符数组。
    const int kMaxNameChars = 255;
    BYTE symbolBuffer[sizeof(SYMBOL_INFO) + kMaxNameChars + 1] = {};
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen   = kMaxNameChars;

    for (int i = 0; i < kMaxStackFrames; ++i)
    {
        if (!::StackWalk64(machineType, process, thread, &frame,
                           &context, nullptr, ::SymFunctionTableAccess64,
                           ::SymGetModuleBase64, nullptr))
        {
            break;
        }
        if (frame.AddrPC.Offset == 0)
        {
            break;
        }

        DWORD64 displacement = 0;
        if (::SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
        {
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisp = 0;
            if (::SymGetLineFromAddr64(process, frame.AddrPC.Offset,
                                       &lineDisp, &line))
            {
                FA_LOGE(_T("  #%02d %hs + 0x%llX  (%hs:%lu)"),
                        i, symbol->Name, displacement,
                        line.FileName, line.LineNumber);
            }
            else
            {
                FA_LOGE(_T("  #%02d %hs + 0x%llX"),
                        i, symbol->Name, displacement);
            }
        }
        else
        {
            FA_LOGE(_T("  #%02d 0x%llX"), i, frame.AddrPC.Offset);
        }
    }

    FA_LOGE(_T("==========================================="));
    ::SymCleanup(process);

    // 已记录现场,结束进程。
    return EXCEPTION_EXECUTE_HANDLER;
}

} // anonymous namespace

void InstallCrashHandler()
{
    ::SetUnhandledExceptionFilter(CrashExceptionFilter);
}

} // namespace flamingoadmin
