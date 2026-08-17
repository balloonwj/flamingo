#pragma once

HWND CreateOwnerWindow();

int Run(LPTSTR /*lpstrCmdLine*/ = NULL, int nCmdShow = SW_SHOWDEFAULT);

BOOL InitSocket();

void UnInitSocket();

/**
 * 清空程序目录 Logs 下指定后缀的日志文件。
 *
 * 注意<u>没有时间判断</u>：该后缀的文件全部删除，只有本次运行的那一份因为正被本
 * 进程占用着删不掉而留下来。函数名里的"过期"没有对应实现，保留只是为了与配置项
 * 键名（clearexpirelog）一致，详见实现处的注释与 LogCleanupPolicy.h。
 *
 *@param pszFileSuffixName 要清理的日志文件的扩展名，不要以.开头，例如log
 */
void ClearExpiredLog(PCTSTR pszFileSuffixName);