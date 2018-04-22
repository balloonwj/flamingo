#pragma once

typedef struct 
{
	unsigned ( __stdcall *start_address )( void * );
	void	*arglist;
	bool	bInteractive; // 是否支持交互桌面
	HANDLE	hEventTransferArg;  //保证线程一定起起来的事件
}THREAD_ARGLIST, *LPTHREAD_ARGLIST;

unsigned int __stdcall ThreadLoader(LPVOID param);

HANDLE MyCreateThread (LPSECURITY_ATTRIBUTES lpThreadAttributes, // SD
					   SIZE_T dwStackSize,                       // initial stack size
					   LPTHREAD_START_ROUTINE lpStartAddress,    // thread function
					   LPVOID lpParameter,                       // thread argument
					   DWORD dwCreationFlags,                    // creation option
  LPDWORD lpThreadId, bool bInteractive = false);
	
DWORD GetProcessID(LPCTSTR lpProcessName);
TCHAR *GetLogUserXP();
TCHAR *GetLogUser2K();
TCHAR *GetCurrentLoginUser();

bool SwitchInputDesktop();

BOOL SelectHDESK(HDESK new_desktop);
BOOL SelectDesktop(TCHAR *name);
BOOL SimulateCtrlAltDel();
bool http_get(LPCTSTR szURL, LPCTSTR szFileName);
//void EncryptData(unsigned char* szRec, unsigned long nLen, unsigned long key);//解密
void EncryptData(TCHAR* szRec, unsigned long nLen, unsigned long key);//解密