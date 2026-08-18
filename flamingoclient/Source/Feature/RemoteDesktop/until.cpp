#include "stdafx.h"
#include <windows.h>
#include <process.h>
#include <Tlhelp32.h>
#include <Wtsapi32.h>
#include <wininet.h>
#include <tchar.h>
#pragma comment(lib, "Wtsapi32.lib")
#include "until.h"

unsigned int __stdcall ThreadLoader(LPVOID param)
{
    unsigned int	nRet = 0;
#ifdef _DLL
    try
    {
#endif	
        THREAD_ARGLIST	arg;
        memcpy(&arg, param, sizeof(arg));
        SetEvent(arg.hEventTransferArg);
        // 与卓面交互
        //TODO: 搞清楚这个是做什么用的
        if (arg.bInteractive)
            SelectDesktop(NULL);

        nRet = arg.start_address(arg.arglist);
#ifdef _DLL
    }
    catch (...){};
#endif
    return nRet;
}

HANDLE MyCreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, // SD
    SIZE_T dwStackSize,                       // initial stack size
    LPTHREAD_START_ROUTINE lpStartAddress,    // thread function
    LPVOID lpParameter,                       // thread argument
    DWORD dwCreationFlags,                    // creation option
    LPDWORD lpThreadId, bool bInteractive)
{
    HANDLE	hThread = INVALID_HANDLE_VALUE;
    THREAD_ARGLIST	arg;
    arg.start_address = (unsigned(__stdcall *)(void *))lpStartAddress;
    arg.arglist = (void *)lpParameter;
    arg.bInteractive = bInteractive;
    arg.hEventTransferArg = CreateEvent(NULL, FALSE, FALSE, NULL);
    //dwStackSize 是调用方传入的线程栈大小，实际取值远小于 unsigned int 的上限，这里显式收窄以消除 C4244 告警。
    hThread = (HANDLE)_beginthreadex((void *)lpThreadAttributes, static_cast<unsigned>(dwStackSize), ThreadLoader, &arg, dwCreationFlags, (unsigned *)lpThreadId);
    WaitForSingleObject(arg.hEventTransferArg, INFINITE);
    CloseHandle(arg.hEventTransferArg);

    return hThread;
}

DWORD GetProcessID(LPCTSTR lpProcessName)
{
    DWORD RetProcessID = 0;
    HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32* info = new PROCESSENTRY32;
    info->dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(handle, info))
    {
        if (_tcsicmp(info->szExeFile, lpProcessName) == 0)
        {
            RetProcessID = info->th32ProcessID;
            return RetProcessID;
        }
        while (Process32Next(handle, info) != FALSE)
        {
            if (lstrcmpi(info->szExeFile, lpProcessName) == 0)
            {
                RetProcessID = info->th32ProcessID;
                return RetProcessID;
            }
        }
    }
    return RetProcessID;
}

TCHAR *GetLogUserXP()
{
    TCHAR	*szLogName = NULL;
    DWORD	dwSize = 0;
    if (WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSUserName, &szLogName, &dwSize))
    {
        TCHAR	*lpUser = new TCHAR[256];
        lstrcpy(lpUser, szLogName);
        WTSFreeMemory(szLogName);
        return lpUser;
    }
    else
        return NULL;
}

//原先按系统版本在 Windows 2000（主版本 5、次版本 0）与后续系统之间分流：Windows 2000
//走 GetLogUser2K，更新的系统走 GetLogUserXP。由于本程序的最低支持版本已经是 Windows 7，
//Windows 2000 分支不再可达；同时 GetVersionEx 从 Windows 8.1 起被废弃、返回值不再可靠，
//故移除版本判断，统一走 GetLogUserXP（原 GetLogUser2K 已无调用点，一并删除）。
TCHAR *GetCurrentLoginUser()
{
    return GetLogUserXP();
}

bool SwitchInputDesktop()
{
    BOOL	bRet = false;
    DWORD	dwLengthNeeded;

    HDESK	hOldDesktop, hNewDesktop;
    TCHAR	strCurrentDesktop[256], strInputDesktop[256];

    hOldDesktop = GetThreadDesktop(GetCurrentThreadId());
    memset(strCurrentDesktop, 0, sizeof(strCurrentDesktop));
    GetUserObjectInformation(hOldDesktop, UOI_NAME, &strCurrentDesktop, sizeof(strCurrentDesktop), &dwLengthNeeded);


    hNewDesktop = OpenInputDesktop(0, FALSE, MAXIMUM_ALLOWED);
    memset(strInputDesktop, 0, sizeof(strInputDesktop));
    GetUserObjectInformation(hNewDesktop, UOI_NAME, &strInputDesktop, sizeof(strInputDesktop), &dwLengthNeeded);

    if (lstrcmpi(strInputDesktop, strCurrentDesktop) != 0)
    {
        SetThreadDesktop(hNewDesktop);
        bRet = true;
    }
    CloseDesktop(hOldDesktop);

    CloseDesktop(hNewDesktop);


    return bRet;
}

BOOL SelectHDESK(HDESK new_desktop)
{
    HDESK old_desktop = GetThreadDesktop(GetCurrentThreadId());

    DWORD dummy;
    char new_name[256];

    if (!GetUserObjectInformation(new_desktop, UOI_NAME, &new_name, 256, &dummy)) {
        return FALSE;
    }

    // Switch the desktop
    if (!SetThreadDesktop(new_desktop)) {
        return FALSE;
    }

    // Switched successfully - destroy the old desktop
    CloseDesktop(old_desktop);

    return TRUE;
}

// - SelectDesktop(char *)
// Switches the current thread into a different desktop, by name
// Calling with a valid desktop name will place the thread in that desktop.
// Calling with a NULL name will place the thread in the current input desktop.

BOOL SelectDesktop(TCHAR *name)
{
    HDESK desktop;

    if (name != NULL)
    {
        // Attempt to open the named desktop
        desktop = OpenDesktop(name, 0, FALSE,
            DESKTOP_CREATEMENU | DESKTOP_CREATEWINDOW |
            DESKTOP_ENUMERATE | DESKTOP_HOOKCONTROL |
            DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS |
            DESKTOP_SWITCHDESKTOP | GENERIC_WRITE);
    }
    else
    {
        // No, so open the input desktop
        desktop = OpenInputDesktop(0, FALSE,
            DESKTOP_CREATEMENU | DESKTOP_CREATEWINDOW |
            DESKTOP_ENUMERATE | DESKTOP_HOOKCONTROL |
            DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS |
            DESKTOP_SWITCHDESKTOP | GENERIC_WRITE);
    }

    // Did we succeed?
    if (desktop == NULL) {
        return FALSE;
    }

    // Switch to the new desktop
    if (!SelectHDESK(desktop)) {
        // Failed to enter the new desktop, so free it!
        CloseDesktop(desktop);
        return FALSE;
    }

    // We successfully switched desktops!
    return TRUE;
}

BOOL SimulateCtrlAltDel()
{
    HDESK old_desktop = GetThreadDesktop(GetCurrentThreadId());

    // Switch into the Winlogon desktop
    TCHAR str1[50] = TEXT("梕hjogoh");
    EncryptData((TCHAR *)&str1, lstrlen(str1), 98);
    if (!SelectDesktop(str1))
    {
        return FALSE;
    }

    // Fake a hotkey event to any windows we find there.... :(
    // Winlogon uses hotkeys to trap Ctrl-Alt-Del...
    PostMessage(HWND_BROADCAST, WM_HOTKEY, 0, MAKELONG(MOD_ALT | MOD_CONTROL, VK_DELETE));

    // Switch back to our original desktop
    if (old_desktop != NULL)
        SelectHDESK(old_desktop);

    return TRUE;
}
bool http_get(LPCTSTR szURL, LPCTSTR szFileName)
{
    HINTERNET	hInternet, hUrl;
    HANDLE		hFile;
    char		buffer[1024];
    DWORD		dwBytesRead = 0;
    DWORD		dwBytesWritten = 0;
    BOOL		bIsFirstPacket = true;
    BOOL		bRet = true;

    hInternet = InternetOpen(TEXT("Mozilla/4.0 (compatible)"), INTERNET_OPEN_TYPE_PRECONFIG, NULL, INTERNET_INVALID_PORT_NUMBER, 0);
    if (hInternet == NULL)
        return false;

    hUrl = InternetOpenUrl(hInternet, szURL, NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (hUrl == NULL)
        return false;

    hFile = CreateFile(szFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);

    if (hFile != INVALID_HANDLE_VALUE)
    {
        do
        {
            memset(buffer, 0, sizeof(buffer));
            InternetReadFile(hUrl, buffer, sizeof(buffer), &dwBytesRead);
            // 由判断第一个数据包是不是有效的PE文件
            if (bIsFirstPacket && ((PIMAGE_DOS_HEADER)buffer)->e_magic != IMAGE_DOS_SIGNATURE)
            {
                bRet = false;
                break;
            }
            bIsFirstPacket = false;

            WriteFile(hFile, buffer, dwBytesRead, &dwBytesWritten, NULL);
        } while (dwBytesRead > 0);
        CloseHandle(hFile);
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    return bRet;
}

//void EncryptData(unsigned TCHAR *szRec, unsigned long nLen, unsigned long key)//解密
//by zhangyl
void EncryptData(TCHAR *szRec, unsigned long nLen, unsigned long key)//解密
{
    unsigned long i;
    unsigned char p;

    if (nLen == 0) 
        nLen = lstrlen((TCHAR*)szRec);
    p = (unsigned char)key % 254 + 1;

    for (i = 0; i < nLen; i++)
    {
        *szRec ^= p;
        *szRec += p;
        szRec++;
    }
}