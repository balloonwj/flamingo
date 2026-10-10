#include "stdafx.h"
#include "Crash.h"
#include "..\Session\UserSessionData.h"
#include "..\Common\Log\IULog.h"
#include <Dbghelp.h>
#include "../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

#pragma comment(lib, "DbgHelp")

// ---- 转储目录与文件名 ---------------------------------------------------

// 转储目录（相对 exe 所在目录）。进程内这条路径与 WER LocalDumps 写到同一处，
// 排查时不必分头去找；与账号配置文件分开，免得 Users 目录越堆越乱。
static const TCHAR* const kDumpSubDir = _T("Users\\dump");

// 进程内转储的文件名前缀。WER 产出的名字形如 Flamingo.exe.<pid>.dmp，前缀不同，
// 一眼就能看出某个转储是哪条路径写的。
static const TCHAR* const kDumpFilePrefix = _T("Flamingo_");

// ---- WER LocalDumps 配置 ------------------------------------------------

// LocalDumps 只能配在 HKLM（没有当前用户级的等价项），故写它需要管理员权限。
static const TCHAR* const kWerLocalDumpsKey =
    _T("SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\Flamingo.exe");

// 完整转储（带堆内存）。栈破坏这类问题常需要看堆与相邻内存，小转储信息不够。
static const DWORD kWerDumpTypeFull = 2;
// 目录里最多保留几个 WER 转储，超出后由系统删最旧的。
static const DWORD kWerDumpCount = 10;

// 拼出转储目录的绝对路径（不带尾部反斜杠）。g_szHomePath 自带尾部反斜杠。
static void BuildDumpDir(TCHAR* pszBuf, size_t cchBuf)
{
    _stprintf_s(pszBuf, cchBuf, _T("%s%s"), g_szHomePath, kDumpSubDir);
}

// 确保转储目录存在。Users 与 Users\dump 两级都可能缺，逐级建。
// 返回是否可用（目录已存在或创建成功）。
static bool EnsureDumpDirExists()
{
    TCHAR szUsers[MAX_PATH] = { 0 };
    _stprintf_s(szUsers, MAX_PATH, _T("%sUsers"), g_szHomePath);
    if (::GetFileAttributes(szUsers) == INVALID_FILE_ATTRIBUTES)
    {
        ::CreateDirectory(szUsers, NULL);
    }

    TCHAR szDump[MAX_PATH] = { 0 };
    BuildDumpDir(szDump, MAX_PATH);
    if (::GetFileAttributes(szDump) == INVALID_FILE_ATTRIBUTES)
    {
        ::CreateDirectory(szDump, NULL);
    }

    return ::GetFileAttributes(szDump) != INVALID_FILE_ATTRIBUTES;
}

// 判断当前进程是否以管理员权限运行。
//
// 用令牌的 TokenElevation 而不是 IsUserAnAdmin —— 后者只回答"账户在不在管理员组里"，
// 开着 UAC 时管理员账户的普通进程同样拿不到 HKLM 写权限，据它判断会误判成能写。
static bool IsRunningElevated()
{
    HANDLE hToken = NULL;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        return false;
    }

    TOKEN_ELEVATION elevation = { 0 };
    DWORD cbSize = sizeof(elevation);
    bool bElevated = false;
    if (::GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize))
    {
        bElevated = (elevation.TokenIsElevated != 0);
    }

    ::CloseHandle(hToken);
    return bElevated;
}

// 把 WER LocalDumps 的三个值写进注册表。调用方须已确认自身已提权。
// 返回是否全部写入成功。
static bool WriteWerLocalDumpsConfig(const TCHAR* pszDumpDir)
{
    HKEY hKey = NULL;
    LONG lRet = ::RegCreateKeyEx(HKEY_LOCAL_MACHINE, kWerLocalDumpsKey, 0, NULL,
                                 REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL);
    if (lRet != ERROR_SUCCESS)
    {
        LOG_WARNING("WER LocalDumps: RegCreateKeyEx failed, error %ld.", lRet);
        return false;
    }

    // DumpFolder 按文档取 REG_EXPAND_SZ；这里写的是绝对路径，没有可展开的变量，
    // 但类型仍按文档给，免得日后有人改成带 %LOCALAPPDATA% 的写法时踩坑。
    const DWORD cbFolder = (DWORD)((_tcslen(pszDumpDir) + 1) * sizeof(TCHAR));
    LONG lFolder = ::RegSetValueEx(hKey, _T("DumpFolder"), 0, REG_EXPAND_SZ,
                                   (const BYTE*)pszDumpDir, cbFolder);

    DWORD dwType = kWerDumpTypeFull;
    LONG lType = ::RegSetValueEx(hKey, _T("DumpType"), 0, REG_DWORD,
                                 (const BYTE*)&dwType, sizeof(dwType));

    DWORD dwCount = kWerDumpCount;
    LONG lCount = ::RegSetValueEx(hKey, _T("DumpCount"), 0, REG_DWORD,
                                  (const BYTE*)&dwCount, sizeof(dwCount));

    ::RegCloseKey(hKey);

    if (lFolder != ERROR_SUCCESS || lType != ERROR_SUCCESS || lCount != ERROR_SUCCESS)
    {
        LOG_WARNING("WER LocalDumps: RegSetValueEx failed, folder=%ld type=%ld count=%ld.",
                    lFolder, lType, lCount);
        return false;
    }

    return true;
}

void ConfigureCrashDumpEnvironment()
{
    // 目录无论提权与否都要建：进程内那条转储路径也往这里写。
    if (!EnsureDumpDirExists())
    {
        LOG_WARNING("crash dump: cannot create dump directory under %s.", "Users\\dump");
        // 目录建不出来也继续往下走 —— 也许只是这一刻被占用，真崩溃时再试一次也无妨。
    }

    // 未提权就到此为止：不弹界面、不请求提权、不报错。这条路径失败并不影响主流程，
    // 进程内的转储照常工作，只是 __fastfail 那类崩溃拿不到转储。
    if (!IsRunningElevated())
    {
        LOG_INFO("WER LocalDumps: not elevated, skip registry setup "
                 "(in-process dump still active; __fastfail crashes will not be captured).");
        return;
    }

    TCHAR szDumpDir[MAX_PATH] = { 0 };
    BuildDumpDir(szDumpDir, MAX_PATH);

    if (WriteWerLocalDumpsConfig(szDumpDir))
    {
        LOG_INFO("WER LocalDumps: configured, dumpType=%u dumpCount=%u.",
                 (unsigned int)kWerDumpTypeFull, (unsigned int)kWerDumpCount);
    }
}

std::wstring PrepareCrashDumpDirectory()
{
    if (!EnsureDumpDirExists())
    {
        return std::wstring();
    }
    TCHAR szDumpDir[MAX_PATH] = { 0 };
    BuildDumpDir(szDumpDir, MAX_PATH);
    return std::wstring(szDumpDir);
}

// Dumps
/////////////////////////////////////////////////////////////////////////////////////////////////
class AutoUnhandleExceptionFilter
{
public:
	AutoUnhandleExceptionFilter::AutoUnhandleExceptionFilter(void)
	{
		::SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);
	}
	static LONG WINAPI AutoUnhandleExceptionFilter::MyUnhandledExceptionFilter(struct _EXCEPTION_POINTERS* ExceptionInfo)
	{
		// 日志是异步写入的，崩溃前最后几行可能还在队列里。先等它们写进文件再弹框，
		// 崩溃前后的日志往往是排查时最需要的
		LOG_ERROR("unhandled exception 0x%08X, flushing log before exit.",
		          ExceptionInfo != NULL && ExceptionInfo->ExceptionRecord != NULL
		              ? (unsigned int)ExceptionInfo->ExceptionRecord->ExceptionCode : 0u);
		CIULog::Flush(kIULogFlushTimeoutMs);

		if(IDYES ==::MessageBox(NULL, TR(L"程序遇到一个致命的问题，需要关闭，我们对此表示非常的抱歉。我们将帮您重启，现在就重新启动吗？"), TR(L"程序出错"), MB_YESNO|MB_ICONERROR|MB_SYSTEMMODAL))
		{
			::ShellExecute(NULL,NULL, _T("Flamingo.exe"), NULL, g_szHomePath, SW_SHOW);
		}

		SYSTEMTIME st={0};
		::GetLocalTime(&st);

		// 目录可能还不存在（崩得比 ConfigureCrashDumpEnvironment 还早），这里再兜一次。
		EnsureDumpDirExists();

		// 年月日时分秒一律补零成定长，文件名按名称排序即等于按时间排序。
		// 改版前是 %d%d%d%02d%02d%02d，年月日不补零，8 月 5 日与 12 月 5 日的名字长度
		// 都不一样，在文件管理器里排不出先后。
		TCHAR szDumpDir[MAX_PATH] = { 0 };
		BuildDumpDir(szDumpDir, MAX_PATH);

		TCHAR szDumpFilePath[MAX_PATH]={0};
		_stprintf_s(szDumpFilePath, MAX_PATH,
				_T("%s\\%s%04d%02d%02d_%02d%02d%02d.dmp"), szDumpDir, kDumpFilePrefix,
				st.wYear, st.wMonth, st.wDay,
				st.wHour,st.wMinute,st.wSecond);

		HANDLE lhDumpFile = ::CreateFile(szDumpFilePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL ,NULL);
		if(INVALID_HANDLE_VALUE != lhDumpFile)
		{
			MINIDUMP_EXCEPTION_INFORMATION loExceptionInfo;
			loExceptionInfo.ExceptionPointers = ExceptionInfo;
			loExceptionInfo.ThreadId = GetCurrentThreadId();
			loExceptionInfo.ClientPointers = TRUE;
			::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), lhDumpFile, MiniDumpNormal, &loExceptionInfo, NULL, NULL);
			::CloseHandle(lhDumpFile);
		}
		return EXCEPTION_EXECUTE_HANDLER;
	}
};

//必须申明这个全局变量
AutoUnhandleExceptionFilter autoUnhandleExceptionFilter;
