#include "wlconfig.h"
#include <comutil.h>
#include "unzip.h"
#include <TlHelp32.h>
#pragma comment(lib,"comsuppw.lib")
/////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct tagVersionInfo
{
	char	Script[64];
	char	zipName[64];
	long	vresion;
	long	ksize;
}VERSION_INFO,*PVERSION_INFO;

TCHAR __main_home__[MAX_PATH]={0};
char __main_home_ansi__[MAX_PATH]={0};	//主路径(ansi)
BOOL blNoupdate(TRUE);
/////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT(STDAPICALLTYPE*pfnRegServer)(void);
//注册com组件
BOOL RegisterCom(LPCTSTR lpszComFile)
{
	HINSTANCE hInst = LoadLibrary(lpszComFile);
	if(NULL==hInst) return(FALSE);

	HRESULT hr(0);
	(FARPROC&)pfnRegServer = GetProcAddress(hInst,"DllRegisterServer");
	if(pfnRegServer) hr = (*pfnRegServer)();

	FreeLibrary(hInst);
	return(SUCCEEDED(hr));
}

BOOL GetVersionInfo(LPSTR lpData,PVERSION_INFO pVersion)
{
	DWORD l(0);
	char*lpMask=NULL;
	memset(pVersion,0,sizeof(VERSION_INFO));
	do
	{
		if(!(lpMask=wl_config::GetApart(lpData,&l,'|'))) break;
		lstrcpynA(pVersion->Script,lpMask,64);

		if(!(lpMask=wl_config::GetApart(lpData,&l,'|'))) break;
		wsprintfA(pVersion->zipName,"update/%s.zip",lpMask);

		if(!(lpMask=wl_config::GetApart(lpData,&l,'|'))) break;
		pVersion->vresion = atol(lpMask);

		if(!(lpMask=wl_config::GetApart(lpData,&l,'|'))) break;
		pVersion->ksize = atol(lpMask);
		return(TRUE);
	}while(0);
	memset(pVersion,0,sizeof(VERSION_INFO));
	return(FALSE);
}

long CompareFile(ZIPENTRY&ze,LPCTSTR filepath)
{
	HANDLE hFile=CreateFile(filepath,
		GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
	if(hFile==INVALID_HANDLE_VALUE) return(0);
	FILETIME ftTime={0};
	if(GetFileTime(hFile,NULL,NULL,&ftTime))
	{
		SYSTEMTIME st1={0},st2={0};
		FileTimeToSystemTime(&ftTime,&st1);
		FileTimeToSystemTime(&ze.mtime,&st2);
		do
		{
			if(st1.wYear!=st2.wYear) break;
			if(st1.wMonth!=st2.wMonth) break;
			if(st1.wDay!=st2.wDay) break;
			if(st1.wHour!=st2.wHour) break;
			if(st1.wMinute!=st2.wMinute) break;
			CloseHandle(hFile);
			return(1);
		}while(0);
	}
	CloseHandle(hFile);
	return(0);
}

BOOL errorUpdate(FALSE);
BOOL blFirst(TRUE);
DWORD WINAPI __UpdateMain(LPVOID lpParam)
{
	wl_config remote_config,local_config;
	TCHAR filename[MAX_PATH]={0};
	if(!remote_config.loadconfig(TEXT("%supdate/update.version"),__main_home__))
	{
		return(0);
	}
	local_config.loadconfig(TEXT("%supdate/update2.version"),__main_home__);
	char szData[256]={0},begin[8]={0},*lpMask=NULL;
	TCHAR fullpath[MAX_PATH]={0};
	VERSION_INFO ver_remote={0},ver_local={0};
	ZIPENTRY ze={0};
	_bstr_t bstrName;
	for(long i=1,j=0,numItems=0;;++i)
	{
		wsprintfA(begin,"<%d>",i);
		if(!remote_config.read(begin,szData,256)) break;
		GetVersionInfo(szData,&ver_remote);

		memset(&ver_local,0,sizeof(VERSION_INFO));
		if(local_config.read(begin,szData,256))
			GetVersionInfo(szData,&ver_local);
		if(ver_remote.vresion==ver_local.vresion) continue;

		wsprintfA(szData,"%s%s",__main_home_ansi__,ver_remote.zipName);
		HZIP hZip=OpenZip(bstrName=szData,NULL);
		if(!hZip) continue;
		SetUnzipBaseDir(hZip,__main_home__);
		GetZipItem(hZip,-1,&ze);
		numItems=ze.index;
		for(j=0;j<numItems;++j)
		{
			GetZipItem(hZip,j,&ze);
			wsprintf(fullpath,TEXT("%s%s"),__main_home__,ze.name);
			if(CompareFile(ze,fullpath)) continue;

			if(blFirst)
			{
				blFirst = FALSE;
				Sleep(3000);
			}
			if(UnzipItem(hZip,j,ze.name)!=ZR_OK) //发生错误
			{
//				MessageBox(NULL,ze.name,TEXT("Error"),MB_OK);
				errorUpdate = TRUE;
//				CloseZip(hZip);
//				return(0);
			}
			else
			{
				if(__string_search__(ze.name,TEXT("GGMSOffice.dll")))
				{
					RegisterCom(fullpath);
				}
			}
		}
		CloseZip(hZip);
	}
	if(!errorUpdate)
		CopyFile(TEXT("update/update.version"),TEXT("update/update2.version"),FALSE);
	return(0);
}

LPTSTR GetApartString(LPTSTR lpString,DWORD*lOffset,TCHAR chMask)
{
	DWORD l(*lOffset),t(l);
	for(;lpString[t]&&lpString[t]!=chMask;++t);
	if(lpString[t]) lpString[t++] = 0;
	*lOffset = t;
	return(lpString+l);
}

void osExec(LPTSTR lpCmdLine)
{
	//DWORD l(0);
	STARTUPINFO si={0};
	//TCHAR cmdName[MAX_PATH]={0};
	//TCHAR cmdLine[256]={0},*lpMask=NULL;
    PROCESS_INFORMATION pi={0};
    si.cb = sizeof(si);

	//if(!(lpMask=GetApartString(lpCmdLine,&l,_T('|')))) return;
	//wsprintf(cmdName,TEXT("%s%s"),__main_home__,lpMask);
	//wsprintf(cmdLine,TEXT("%s%s %s"),__main_home__,lpMask,lpCmdLine+l);
	//if(blNoupdate)
	//	lstrcat(cmdLine,TEXT("|noupdate"));

	CreateProcess(lpCmdLine,NULL,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi);
}

int WINAPI _tWinMain(HINSTANCE hInstance,HINSTANCE,LPTSTR lpCmdLine,int)
{	
    PROCESSENTRY32 pe32 = {0};
    pe32.dwSize = sizeof(PROCESSENTRY32);

    HANDLE hProcessSnap;
    hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE)
        return false;

    if (!Process32First(hProcessSnap, &pe32))
    {           
        CloseHandle(hProcessSnap);
        return false;
    }

    DWORD dwFlamingoProcessID = 0;
    do
    {
        if (_tcsicmp(pe32.szExeFile, _T("Flamingo.exe")) == 0)
        {
            dwFlamingoProcessID = pe32.th32ProcessID;
            break;
        }           

    } while (Process32Next(hProcessSnap, &pe32));

    CloseHandle(hProcessSnap);

    //Flamingo.exe进程已经退掉，则认为升级成功
    if (dwFlamingoProcessID != 0)
    {
        HANDLE hAutoUpdateProcess = ::OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwFlamingoProcessID);
        if (hAutoUpdateProcess == NULL)
            return false;
        DWORD dwRet = ::WaitForSingleObject(hAutoUpdateProcess, 5000);
        if (dwRet != WAIT_OBJECT_0)
            return 0;
    }
    
    long l(0);	
	blNoupdate=(lpCmdLine&&*lpCmdLine);
	
	GetModuleFileName(hInstance,__main_home__,MAX_PATH);
	for(l=lstrlen(__main_home__);l>=0&&__main_home__[l]!=TEXT('\\');__main_home__[l--]=0);
	SetCurrentDirectory(__main_home__);

	GetModuleFileNameA(hInstance,__main_home_ansi__,MAX_PATH);
	for(l=lstrlenA(__main_home_ansi__);l>=0&&__main_home_ansi__[l]!='\\';__main_home_ansi__[l--]=0);

	DWORD idthread=0;
	HANDLE hThread=CreateThread(NULL,0,__UpdateMain,NULL,0,&idthread);
	while(WaitForSingleObject(hThread,150000L)!=WAIT_OBJECT_0)
	{
	}
	osExec(lpCmdLine);
	return(0);
}
