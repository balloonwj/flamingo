#pragma once

HWND CreateOwnerWindow();

int Run(LPTSTR /*lpstrCmdLine*/ = NULL, int nCmdShow = SW_SHOWDEFAULT);

BOOL InitSocket();

void UnInitSocket();

/**
 *@param pszFileSuffixName 要清理的日志文件的扩展名，不要以.开头，例如log
 */
void ClearExpiredLog(PCTSTR pszFileSuffixName);