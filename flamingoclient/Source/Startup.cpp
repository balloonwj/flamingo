/** 
* 程序入口函数
* zhangyl 2017.03.16
**/

#include "stdafx.h"

#include "resource.h"
#include "Startup.h"

#include "aboutdlg.h"
#include "MainDlg.h"
#include "UpdateDlg.h"
#include "Updater.h"
#include "LoginSettingsDlg.h"
#include "File2.h"
#include "IULog.h"
#include "UserSessionData.h"
#include "Utils.h"
#include "IniFile.h"
#include "UIText.h"
#include "PerformanceCounter.h"

CAppModule _Module;

//关于g_hwndOwner的说明：
//之所以创建这个窗口为为了产生主窗口和登录对话框不在任务栏显示的效果，
//如果单纯地将窗口风格由WS_EX_APPWINDOW设置为WS_EX_TOOLWINDOW后，此种风格的窗口
//会在失去焦点后默认Z轴变为0，这点特别讨厌。
HWND	   g_hwndOwner = NULL;	

HWND CreateOwnerWindow()
{
	PCTSTR pszOwnerWindowClass = _T("__Flamingo_Owner__");
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = DefWindowProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = NULL;
	wcex.hCursor = NULL;
	wcex.hbrBackground = NULL;
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = pszOwnerWindowClass;
	wcex.hIconSm = NULL;

	if (!::RegisterClassEx(&wcex))
		return NULL;

	HWND hOwnerWindow = ::CreateWindow(pszOwnerWindowClass, NULL, WS_OVERLAPPEDWINDOW, 
									   CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
	
	return hOwnerWindow;
}

int Run(LPTSTR /*lpstrCmdLine = NULL*/, int nCmdShow/* = SW_SHOWDEFAULT*/)
{
	CMessageLoop theLoop;
	_Module.AddMessageLoop(&theLoop);

	CMainDlg dlgMain;
	
	g_hwndOwner = CreateOwnerWindow();
	if(dlgMain.Create(g_hwndOwner) == NULL)
	{
		ATLTRACE(_T("Main dialog creation failed!\n"));
		return 0;
	}

	dlgMain.ShowWindow(nCmdShow);

	int nRet = theLoop.Run();

	_Module.RemoveMessageLoop();
	return nRet;
}

void ClearExpiredLog(PCTSTR pszFileSuffixName)
{    
    if (pszFileSuffixName == NULL)
        return;
    
    WIN32_FIND_DATA win32FindData = { 0 };
    TCHAR szLogFilePath[MAX_PATH] = { 0 };
    _stprintf_s(szLogFilePath, MAX_PATH, _T("%sLogs\\*.%s"), g_szHomePath, pszFileSuffixName);
    HANDLE hFindFile = ::FindFirstFile(szLogFilePath, &win32FindData);
    if (hFindFile == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (_tcsicmp(win32FindData.cFileName, _T(".")) != 0 ||
            _tcsicmp(win32FindData.cFileName, _T("..")) != 0)
        {
            memset(szLogFilePath, 0, sizeof(szLogFilePath));
            _stprintf_s(szLogFilePath, MAX_PATH, _T("%sLogs\\%s"), g_szHomePath, win32FindData.cFileName);
            //这里不用检测是否删除成功,因为最新的一个log是我们需要的,不能删除,它正被此进程占用着,所以删不掉
            ::DeleteFile(szLogFilePath);
        }

        if (!::FindNextFile(hFindFile, &win32FindData))
            break;

    } while (true);

    ::FindClose(hFindFile);
}


BOOL InitSocket()
{
	WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	int nErrorID = ::WSAStartup(wVersionRequested, &wsaData);
	if(nErrorID != 0)
		return FALSE;

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
    {
        UnInitSocket();
        return FALSE;
    }

	return TRUE;
}

void UnInitSocket()
{
	::WSACleanup();
}

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpstrCmdLine, int nCmdShow)
{
#ifdef _DEBUG
	_CrtSetDebugFillThreshold( 0 );
#endif
	_tcscpy_s(g_szHomePath, MAX_PATH, Hootina::CPath::GetAppPath().c_str());
	strcpy_s(g_szHomePathAscii, MAX_PATH, Hootina::CPath::GetAppPathAscii().c_str());

	SYSTEMTIME st = {0};
	::GetLocalTime(&st);

    //加上PID，便于同一个目录启动多个进程区分是哪个程序的日志
    DWORD dwProcessId = ::GetCurrentProcessId();
	TCHAR szLogFileName[MAX_PATH] = {0};
	_stprintf_s(szLogFileName, MAX_PATH, _T("%s\\Logs\\%04d%02d%02d%02d%02d%02d_%d.log"), g_szHomePath, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, (int32_t)dwProcessId);
	CIULog::Init(true, false, szLogFileName);

    //初始化性能计数器
    TCHAR szPerformanceFileName[MAX_PATH] = { 0 };
    _stprintf_s(szPerformanceFileName, MAX_PATH, _T("%s\\Logs\\%04d%02d%02d%02d%02d%02d_%d.perf"), g_szHomePath, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, (int32_t)dwProcessId);
    CPerformanceCounter::Init(true, szPerformanceFileName);

    //TODO: 统一到AppConfig类中去
    CIniFile iniFile;
    CString strIniFilePath(g_szHomePath);
    strIniFilePath += _T("config\\flamingo.ini");
    bool bNeedClear = true;
    if (iniFile.ReadInt(_T("app"), _T("clearexpirelog"), 0, strIniFilePath) == 0)
        bNeedClear = false;

    if (bNeedClear)
    {
        //清理过期的日志文件
        ClearExpiredLog(_T("log"));
        ClearExpiredLog(_T("perf"));
    }

    //日志级别
    int nLogLevel = iniFile.ReadInt(_T("app"), _T("loglevel"), 0, strIniFilePath);
    CIULog::SetLevel((LOG_LEVEL)nLogLevel);
    
	if(!InitSocket())
		return 0;

    //加载配置的程序标题
    TCHAR szAppTitle[128] = { 0 };
    iniFile.ReadString(_T("ui"), _T("apptitle"), UI_APP_TITLE, szAppTitle, ARRAYSIZE(szAppTitle), strIniFilePath);
    g_strAppTitle = szAppTitle;

	tstring strFileName = Hootina::CPath::GetAppPath() + _T("richFace.dll");
	BOOL bRet = DllRegisterServer(strFileName.c_str());	// 注册COM组件
	if (!bRet)
	{
		::MessageBox(NULL, _T("COM组件注册失败，应用程序无法完成初始化操作！"), _T("提示"), MB_OK);
		return 0;
	}

	HRESULT hRes = ::OleInitialize(NULL);
	ATLASSERT(SUCCEEDED(hRes));

	// this resolves ATL window thunking problem when Microsoft Layer for Unicode (MSLU) is used
	::DefWindowProc(NULL, 0, 0, 0L);

	AtlInitCommonControls(ICC_BAR_CLASSES);	// add flags to support other controls
	HMODULE hRichEditDll = ::LoadLibrary(CRichEditCtrl::GetLibraryName());	// 加载RichEdit控件DLL

	hRes = _Module.Init(NULL, hInstance);
	ATLASSERT(SUCCEEDED(hRes));

	CSkinManager::Init();	// 初始化皮肤管理器
	
	tstring strSkinPath = Hootina::CPath::GetAppPath() + _T("Skins\\");	// 设置皮肤文件夹路径
	CSkinManager::GetInstance()->SetSkinPath(strSkinPath.c_str());
	
	CSkinManager::GetInstance()->LoadConfigXml();	// 加载皮肤列表配置文件

	int nRet = Run(lpstrCmdLine, nCmdShow);

	CSkinManager::UnInit();	// 反初始化皮肤管理器

	if (hRichEditDll != NULL)		// 卸载RichEdit控件DLL
	{
		::FreeLibrary(hRichEditDll);
		hRichEditDll = NULL;
	}

	_Module.Term();

	UnInitSocket();

	CIULog::Uninit();
	
	::OleUninitialize();

	return nRet;
}
