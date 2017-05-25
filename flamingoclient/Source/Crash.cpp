#include "stdafx.h"
#include "UserSessionData.h"
#include <Dbghelp.h>

#pragma comment(lib, "DbgHelp")

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
		if(IDYES == ::MessageBox(NULL, _T("程序遇到一个致命的问题，需要关闭，我们对此表示非常的抱歉。我们将帮您重启，现在就重新启动吗？"), _T("程序出错"), MB_YESNO|MB_ICONERROR|MB_SYSTEMMODAL))
		{
			::ShellExecute(NULL,NULL, _T("Flamingo.exe"), NULL, g_szHomePath, SW_SHOW);
		}
		
		SYSTEMTIME st={0};
		::GetLocalTime(&st);

		TCHAR szDumpFilePath[MAX_PATH]={0};
		wsprintf(szDumpFilePath, 
				_T("%sUsers/%d%d%d%02d%02d%02d.dmp"), g_szHomePath,
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