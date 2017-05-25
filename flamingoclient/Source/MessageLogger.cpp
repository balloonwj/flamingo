#include "stdafx.h"
#include "MessageLogger.h"

void CloneStr(const wchar_t* src, wchar_t** dest, UINT* destlen)
{
	if (NULL == dest || NULL == destlen)
		return;

	*dest = NULL;
	*destlen = 0;

	if (NULL == src)
		return;

	size_t len;
	wchar_t*copy;
	len = wcslen(src) + 1;
	if ((copy = new wchar_t[len]) == NULL)
		return;
	memcpy(copy, src, len*sizeof(wchar_t));

	*dest = copy;
	*destlen = len-1;
}

CMessageLogger::CMessageLogger(void) : m_hMutex(NULL)
{
}

CMessageLogger::~CMessageLogger(void)
{
	if (m_hMutex != NULL)
	{
		CloseHandle(m_hMutex);
		m_hMutex = NULL;
	}
}

// 获取消息记录文件名称
tstring CMessageLogger::GetMsgLogFileName()
{
	return m_strFileName;
}

// 设置消息记录文件名称
void CMessageLogger::SetMsgLogFileName(LPCTSTR lpszFileName)
{
	if (NULL == lpszFileName || NULL ==*lpszFileName)
		return;

	m_strFileName = lpszFileName;

	if (m_hMutex != NULL)
	{
		CloseHandle(m_hMutex);
		m_hMutex = NULL;
	}

	tstring strMutexName = m_strFileName;
	this->Replace(strMutexName, _T("\\"), _T(""));

	m_hMutex = ::OpenMutex(MUTEX_ALL_ACCESS, FALSE, strMutexName.c_str());
	if (NULL == m_hMutex)
	{
		m_hMutex = ::CreateMutex(NULL, FALSE, strMutexName.c_str());
		if (NULL == m_hMutex)	// 创建互斥量失败
		{
			::MessageBox(NULL, _T("创建互斥量失败！"), _T("提示"), 0);
			return;
		}
	}
}

// 判断消息记录文件是否有效
BOOL CMessageLogger::IsMsgLogFileValid()
{
	if (m_strFileName.empty())	// 文件名为空
		return FALSE;

	if(::GetFileAttributes(m_strFileName.c_str()) == 0xFFFFFFFF)	// 文件不存在
		return FALSE;

	sqlite3*conn = NULL;
	int nRet = sqlite3_open16(m_strFileName.c_str(), &conn);		// 打开文件失败
	if (nRet != SQLITE_OK)
		return FALSE;
	
	sqlite3_close(conn);
	return TRUE;
}

// 创建消息记录文件
BOOL CMessageLogger::CreateMsgLogFile()
{
	sqlite3*conn = NULL;
	char*err_msg = NULL;
	char* lpSql;
	int nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return FALSE;

	lpSql = "CREATE TABLE IF NOT EXISTS [tb_BuddyMsg] ([id] INTEGER PRIMARY KEY AUTOINCREMENT, [uin] INTEGER, [nickname] TEXT, [time] INTEGER, [sendflag] INTEGER, [content] TEXT)";
	nRet = sqlite3_exec(conn, lpSql, 0, 0, &err_msg);	// 创建好友消息表
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		::DeleteFile(m_strFileName.c_str());
		return FALSE;
	}

	lpSql = "CREATE TABLE IF NOT EXISTS [tb_GroupMsg] ([id] INTEGER PRIMARY KEY AUTOINCREMENT, [groupnum] INTEGER, [uin] INTEGER, [nickname] TEXT, [time] INTEGER, [content] TEXT)";
	nRet = sqlite3_exec(conn, lpSql, 0, 0, &err_msg);	// 创建群消息表
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		::DeleteFile(m_strFileName.c_str());
		return FALSE;
	}

	lpSql = "CREATE TABLE IF NOT EXISTS [tb_SessMsg] ([id] INTEGER PRIMARY KEY AUTOINCREMENT, [uin] INTEGER, [nickname] TEXT, [time] INTEGER, [sendflag] INTEGER, [content] TEXT)";
	nRet = sqlite3_exec(conn, lpSql, 0, 0, &err_msg);	// 创建临时会话(群成员)消息表
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		::DeleteFile(m_strFileName.c_str());
		return FALSE;
	}

	sqlite3_close(conn);
	return TRUE;
}

// 写入一条好友消息记录
BOOL CMessageLogger::WriteBuddyMsgLog(UINT nUTalkNum, LPCTSTR lpNickName,
									  UINT64 nTime, BOOL bSendFlag, LPCTSTR lpContent)
{
	BUDDY_MSG_LOG stMsgLog = {0};
	stMsgLog.nUTalkNum = nUTalkNum;
	if (lpNickName != NULL)
		stMsgLog.strNickName = lpNickName;
	stMsgLog.nTime = nTime;
	stMsgLog.bSendFlag = bSendFlag;
	if (lpContent != NULL)
		stMsgLog.strContent = lpContent;
	return WriteBuddyMsgLog(&stMsgLog);
}

// 写入一条群消息记录
BOOL CMessageLogger::WriteGroupMsgLog(UINT nGroupNum, UINT nUTalkNum,
									  LPCTSTR lpNickName, UINT64 nTime, LPCTSTR lpContent)
{
	GROUP_MSG_LOG stMsgLog = {0};
	stMsgLog.nGroupNum = nGroupNum;
	stMsgLog.nUTalkNum = nUTalkNum;
	if (lpNickName != NULL)
		stMsgLog.strNickName = lpNickName;
	stMsgLog.nTime = nTime;
	if (lpContent != NULL)
		stMsgLog.strContent = lpContent;
	return WriteGroupMsgLog(&stMsgLog);
}

// 写入一条临时会话(群成员)消息记录
BOOL CMessageLogger::WriteSessMsgLog(UINT nUTalkNum, LPCTSTR lpNickName,
									 UINT64 nTime, BOOL bSendFlag, LPCTSTR lpContent)
{
	SESS_MSG_LOG stMsgLog = {0};
	stMsgLog.nUTalkNum = nUTalkNum;
	if (lpNickName != NULL)
		stMsgLog.strNickName = lpNickName;
	stMsgLog.nTime = nTime;
	stMsgLog.bSendFlag = bSendFlag;
	if (lpContent != NULL)
		stMsgLog.strContent = lpContent;
	return WriteSessMsgLog(&stMsgLog);
}

// 写入一条好友消息记录
BOOL CMessageLogger::WriteBuddyMsgLog(BUDDY_MSG_LOG* lpMsgLog)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql = "INSERT INTO [tb_BuddyMsg] ([id],[uin],[nickname],[time],[sendflag],[content]) VALUES (NULL,?,?,?,?,?)";
	int nRet;

	if (NULL == lpMsgLog)
		return FALSE;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return FALSE;

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return FALSE;
	}

	sqlite3_bind_int(stmt, 1, lpMsgLog->nUTalkNum);
	sqlite3_bind_text16(stmt, 2, lpMsgLog->strNickName.c_str(), -1, SQLITE_STATIC);
	//暂且这么转换一下,lpMsgLog->nTime可能不对！
	sqlite3_bind_int64(stmt, 3, lpMsgLog->nTime);
	sqlite3_bind_int(stmt, 4, lpMsgLog->bSendFlag);
	sqlite3_bind_text16(stmt, 5, lpMsgLog->strContent.c_str(), -1, SQLITE_STATIC);

	nRet = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return (nRet != SQLITE_DONE) ? FALSE : TRUE;
}

// 写入一条群消息记录
BOOL CMessageLogger::WriteGroupMsgLog(GROUP_MSG_LOG* lpMsgLog)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql = "INSERT INTO [tb_GroupMsg] ([id],[groupnum],[uin],[nickname],[time],[content]) VALUES (NULL,?,?,?,?,?)";
	int nRet;

	if (NULL == lpMsgLog)
		return FALSE;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return FALSE;

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return FALSE;
	}

	sqlite3_bind_int(stmt, 1, lpMsgLog->nGroupNum);
	sqlite3_bind_int(stmt, 2, lpMsgLog->nUTalkNum);
	sqlite3_bind_text16(stmt, 3, lpMsgLog->strNickName.c_str(), -1, SQLITE_STATIC);
	//暂且这么转换一下,lpMsgLog->nTime可能不对！
	sqlite3_bind_int64(stmt, 4, lpMsgLog->nTime);
	sqlite3_bind_text16(stmt, 5, lpMsgLog->strContent.c_str(), -1, SQLITE_STATIC);

	nRet = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return (nRet != SQLITE_DONE) ? FALSE : TRUE;
}

// 写入一条临时会话(群成员)消息记录
BOOL CMessageLogger::WriteSessMsgLog(SESS_MSG_LOG* lpMsgLog)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql = "INSERT INTO [tb_SessMsg] ([id],[uin],[nickname],[time],[sendflag],[content]) VALUES (NULL,?,?,?,?,?)";
	int nRet;

	if (NULL == lpMsgLog)
		return FALSE;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return FALSE;

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return FALSE;
	}

	sqlite3_bind_int(stmt, 1, lpMsgLog->nUTalkNum);
	sqlite3_bind_text16(stmt, 2, lpMsgLog->strNickName.c_str(), -1, SQLITE_STATIC);
	//暂且这么转换一下,lpMsgLog->nTime可能不对！
	sqlite3_bind_int64(stmt, 3, lpMsgLog->nTime);
	sqlite3_bind_int(stmt, 4, lpMsgLog->bSendFlag);
	sqlite3_bind_text16(stmt, 5, lpMsgLog->strContent.c_str(), -1, SQLITE_STATIC);

	nRet = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return (nRet != SQLITE_DONE) ? FALSE : TRUE;
}

// 读出一条或多条好友消息记录
UINT CMessageLogger::ReadBuddyMsgLog(UINT nUTalkNum, UINT nOffset, UINT nRows, 
									 std::vector<BUDDY_MSG_LOG*>& arrMsgLog)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql;
	int nRet;

	UINT nMsgCnt = ReadBuddyMsgLogCount(nUTalkNum, nOffset, nRows);
	if (nMsgCnt <= 0)
		return 0;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	if (0 == nOffset && 0 == nRows)
		lpSql = "SELECT* FROM [tb_BuddyMsg] WHERE [uin]=? ORDER BY [time]";
	else
		lpSql = "SELECT* FROM [tb_BuddyMsg] WHERE [uin]=? ORDER BY [time] LIMIT ?,?";

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	if (0 == nOffset && 0 == nRows)
		sqlite3_bind_int(stmt, 1, nUTalkNum);
	else
	{
		sqlite3_bind_int(stmt, 1, nUTalkNum);
		sqlite3_bind_int(stmt, 2, nOffset);
		sqlite3_bind_int(stmt, 3, nRows);
	}

	while (SQLITE_ROW == sqlite3_step(stmt))
	{
		int nCols = sqlite3_column_count(stmt);
		if (nCols != 6)
			continue;

		BUDDY_MSG_LOG* lpMsgLog = new BUDDY_MSG_LOG;
		if (NULL == lpMsgLog)
			continue;

		lpMsgLog->nID = sqlite3_column_int(stmt, 0);
		lpMsgLog->nUTalkNum = sqlite3_column_int(stmt, 1);
		const wchar_t* lpNickName = (const wchar_t*)sqlite3_column_text16(stmt, 2);
		if (lpNickName != NULL)
			lpMsgLog->strNickName = lpNickName;
		lpMsgLog->nTime = sqlite3_column_int64(stmt, 3);
		lpMsgLog->bSendFlag = sqlite3_column_int(stmt, 4);
		const wchar_t* lpContent = (const wchar_t*)sqlite3_column_text16(stmt, 5);
		if (lpContent != NULL)
			lpMsgLog->strContent = lpContent;

		arrMsgLog.push_back(lpMsgLog);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return arrMsgLog.size();
}

// 读出一条或多条群消息记录
UINT CMessageLogger::ReadGroupMsgLog(UINT nGroupNum, UINT nOffset, UINT nRows, 
									 std::vector<GROUP_MSG_LOG*>& arrMsgLog)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql;
	int nRet;

	UINT nMsgCnt = ReadGroupMsgLogCount(nGroupNum, nOffset, nRows);
	if (nMsgCnt <= 0)
		return 0;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	if (0 == nOffset && 0 == nRows)
		lpSql = "SELECT* FROM [tb_GroupMsg] WHERE [groupnum]=? ORDER BY [time]";
	else
		lpSql = "SELECT* FROM [tb_GroupMsg] WHERE [groupnum]=? ORDER BY [time] LIMIT ?,?";

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	if (0 == nOffset && 0 == nRows)
		sqlite3_bind_int(stmt, 1, nGroupNum);
	else
	{
		sqlite3_bind_int(stmt, 1, nGroupNum);
		sqlite3_bind_int(stmt, 2, nOffset);
		sqlite3_bind_int(stmt, 3, nRows);
	}

	while (SQLITE_ROW == sqlite3_step(stmt))
	{
		int nCols = sqlite3_column_count(stmt);
		if (nCols != 6)
			continue;

		GROUP_MSG_LOG* lpMsgLog = new GROUP_MSG_LOG;
		if (NULL == lpMsgLog)
			continue;

		lpMsgLog->nID = sqlite3_column_int(stmt, 0);
		lpMsgLog->nGroupNum = sqlite3_column_int(stmt, 1);
		lpMsgLog->nUTalkNum = sqlite3_column_int(stmt, 2);
		const wchar_t* lpNickName = (const wchar_t*)sqlite3_column_text16(stmt, 3);
		if (lpNickName != NULL)
			lpMsgLog->strNickName = lpNickName;
		lpMsgLog->nTime = sqlite3_column_int64(stmt, 4);
		const wchar_t* lpContent = (const wchar_t*)sqlite3_column_text16(stmt, 5);
		if (lpContent != NULL)
			lpMsgLog->strContent = lpContent;

		arrMsgLog.push_back(lpMsgLog);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return arrMsgLog.size();
}

// 读出一条或多条临时会话(群成员)消息记录
UINT CMessageLogger::ReadSessMsgLog(UINT nUTalkNum, UINT nOffset, UINT nRows, 
									std::vector<SESS_MSG_LOG*>& arrMsgLog)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql;
	int nRet;

	UINT nMsgCnt = ReadSessMsgLogCount(nUTalkNum, nOffset, nRows);
	if (nMsgCnt <= 0)
		return 0;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	if (0 == nOffset && 0 == nRows)
		lpSql = "SELECT* FROM [tb_SessMsg] WHERE [uin]=? ORDER BY [time]";
	else
		lpSql = "SELECT* FROM [tb_SessMsg] WHERE [uin]=? ORDER BY [time] LIMIT ?,?";

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	if (0 == nOffset && 0 == nRows)
		sqlite3_bind_int(stmt, 1, nUTalkNum);
	else
	{
		sqlite3_bind_int(stmt, 1, nUTalkNum);
		sqlite3_bind_int(stmt, 2, nOffset);
		sqlite3_bind_int(stmt, 3, nRows);
	}

	while (SQLITE_ROW == sqlite3_step(stmt))
	{
		int nCols = sqlite3_column_count(stmt);
		if (nCols != 6)
			continue;

		SESS_MSG_LOG* lpMsgLog = new SESS_MSG_LOG;
		if (NULL == lpMsgLog)
			continue;

		lpMsgLog->nID = sqlite3_column_int(stmt, 0);
		lpMsgLog->nUTalkNum = sqlite3_column_int(stmt, 1);
		const wchar_t* lpNickName = (const wchar_t*)sqlite3_column_text16(stmt, 2);
		if (lpNickName != NULL)
			lpMsgLog->strNickName = lpNickName;
		lpMsgLog->nTime = sqlite3_column_int64(stmt, 3);
		lpMsgLog->bSendFlag = sqlite3_column_int(stmt, 4);
		const wchar_t* lpContent = (const wchar_t*)sqlite3_column_text16(stmt, 5);
		if (lpContent != NULL)
			lpMsgLog->strContent = lpContent;

		arrMsgLog.push_back(lpMsgLog);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return arrMsgLog.size();
}

// 获取好友消息记录数
UINT CMessageLogger::GetBuddyMsgLogCount(UINT nUTalkNum)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql = "SELECT COUNT(*) FROM [tb_BuddyMsg] WHERE [uin]=?";
	int nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	sqlite3_bind_int(stmt, 1, nUTalkNum);

	nRet = sqlite3_step(stmt);
	if (nRet != SQLITE_ROW)
	{
		sqlite3_finalize(stmt);
		sqlite3_close(conn);
		return 0;
	}

	UINT nCount = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return nCount;
}

// 获取群消息记录数
UINT CMessageLogger::GetGroupMsgLogCount(UINT nGroupNum)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql = "SELECT COUNT(*) FROM [tb_GroupMsg] WHERE [groupnum]=?";
	int nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	sqlite3_bind_int(stmt, 1, nGroupNum);

	nRet = sqlite3_step(stmt);
	if (nRet != SQLITE_ROW)
	{
		sqlite3_finalize(stmt);
		sqlite3_close(conn);
		return 0;
	}

	UINT nCount = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return nCount;
}

// 获取临时会话(群成员)消息记录数
UINT CMessageLogger::GetSessMsgLogCount(UINT nUTalkNum)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql = "SELECT COUNT(*) FROM [tb_SessMsg] WHERE [uin]=?";
	int nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	sqlite3_bind_int(stmt, 1, nUTalkNum);

	nRet = sqlite3_step(stmt);
	if (nRet != SQLITE_ROW)
	{
		sqlite3_finalize(stmt);
		sqlite3_close(conn);
		return 0;
	}

	UINT nCount = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return nCount;
}

// 删除所有好友消息记录
BOOL CMessageLogger::DelAllBuddyMsgLog()
{
	sqlite3*conn = NULL;
	char*err_msg = NULL;
	char* lpSql = "DELETE FROM [tb_BuddyMsg]";
	int nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	nRet = sqlite3_exec(conn, lpSql, 0, 0, &err_msg);

	sqlite3_close(conn);

	return ((nRet != SQLITE_OK) ? FALSE : TRUE);
}

// 删除所有群消息记录
BOOL CMessageLogger::DelAllGroupMsgLog()
{
	sqlite3*conn = NULL;
	char*err_msg = NULL;
	char* lpSql = "DELETE FROM [tb_GroupMsg]";
	int nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	nRet = sqlite3_exec(conn, lpSql, 0, 0, &err_msg);

	sqlite3_close(conn);

	return ((nRet != SQLITE_OK) ? FALSE : TRUE);
}

// 删除所有临时会话(群成员)消息记录
BOOL CMessageLogger::DelAllSessMsgLog()
{
	sqlite3*conn = NULL;
	char*err_msg = NULL;
	char* lpSql = "DELETE FROM [tb_SessMsg]";
	int nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	nRet = sqlite3_exec(conn, lpSql, 0, 0, &err_msg);

	sqlite3_close(conn);

	return ((nRet != SQLITE_OK) ? FALSE : TRUE);
}

// 删除指定好友的所有消息记录
BOOL CMessageLogger::DelBuddyMsgLog(UINT nUTalkNum)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql = "DELETE FROM [tb_BuddyMsg] WHERE [uin]=?";
	int nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	sqlite3_bind_int(stmt, 1, nUTalkNum);

	nRet = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return ((nRet != SQLITE_DONE) ? FALSE : TRUE);
}

// 删除指定群的所有消息记录
BOOL CMessageLogger::DelGroupMsgLog(UINT nGroupNum)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql = "DELETE FROM [tb_GroupMsg] WHERE [groupnum]=?";
	int nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	sqlite3_bind_int(stmt, 1, nGroupNum);

	nRet = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return ((nRet != SQLITE_DONE) ? FALSE : TRUE);
}

// 删除指定临时会话(群成员)的所有消息记录
BOOL CMessageLogger::DelSessMsgLog(UINT nUTalkNum)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql = "DELETE FROM [tb_SessMsg] WHERE [uin]=?";
	int nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	sqlite3_bind_int(stmt, 1, nUTalkNum);

	nRet = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return ((nRet != SQLITE_DONE) ? FALSE : TRUE);
}

// 获取消息ID通过消息文本
UINT CMessageLogger::DelBuddyMsgLogByText(LPCTSTR lpStr)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	//char* lpSql = "DELETE FROM [tb_BuddyMsg] WHERE [content]=?";
	char* lpSql = "SELECT [id]= FROM [tb_BuddyMsg] WHERE [content]=?";
	UINT nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	//sqlite3_bind_int(stmt, 1, nID);
	//sqlite3_bind_text(stmt, 1, lpStr);
	nRet = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return nRet;
}
// 删除指定ID的好友消息记录
BOOL CMessageLogger::DelBuddyMsgLogByID(UINT nID)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql = "DELETE FROM [tb_BuddyMsg] WHERE [id]=?";
	int nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	sqlite3_bind_int(stmt, 1, nID);

	nRet = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return ((nRet != SQLITE_DONE) ? FALSE : TRUE);
}

// 删除指定ID的群消息记录
BOOL CMessageLogger::DelGroupMsgLogByID(UINT nID)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql = "DELETE FROM [tb_GroupMsg] WHERE [id]=?";
	int nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	sqlite3_bind_int(stmt, 1, nID);

	nRet = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return ((nRet != SQLITE_DONE) ? FALSE : TRUE);
}

// 删除指定ID的临时会话(群成员)消息记录
BOOL CMessageLogger::DelSessMsgLogByID(UINT nID)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql = "DELETE FROM [tb_SessMsg] WHERE [id]=?";
	int nRet;

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	sqlite3_bind_int(stmt, 1, nID);

	nRet = sqlite3_step(stmt);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return ((nRet != SQLITE_DONE) ? FALSE : TRUE);
}

// 加锁函数
void CMessageLogger::Lock()
{
	::WaitForSingleObject(m_hMutex, INFINITE);
}

// 解锁函数
void CMessageLogger::UnLock()
{
	::ReleaseMutex(m_hMutex);
}

UINT CMessageLogger::ReadBuddyMsgLogCount(UINT nUTalkNum, UINT nOffset, UINT nRows)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql;
	int nRet;

	if (0 == nOffset && 0 == nRows)
		return GetBuddyMsgLogCount(nUTalkNum);

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	lpSql = "SELECT COUNT(*) FROM (SELECT* FROM [tb_BuddyMsg] WHERE [uin]=? ORDER BY [time] LIMIT ?,?)";

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	sqlite3_bind_int(stmt, 1, nUTalkNum);
	sqlite3_bind_int(stmt, 2, nOffset);
	sqlite3_bind_int(stmt, 3, nRows);

	nRet = sqlite3_step(stmt);
	if (nRet != SQLITE_ROW)
	{
		sqlite3_finalize(stmt);
		sqlite3_close(conn);
		return 0;
	}

	int nCount = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return nCount;
}

UINT CMessageLogger::ReadGroupMsgLogCount(UINT nGroupNum, UINT nOffset, UINT nRows)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql;
	int nRet;

	if (0 == nOffset && 0 == nRows)
		return GetGroupMsgLogCount(nGroupNum);

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	lpSql = "SELECT COUNT(*) FROM (SELECT* FROM [tb_GroupMsg] WHERE [groupnum]=? ORDER BY [time] LIMIT ?,?)";

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	sqlite3_bind_int(stmt, 1, nGroupNum);
	sqlite3_bind_int(stmt, 2, nOffset);
	sqlite3_bind_int(stmt, 3, nRows);

	nRet = sqlite3_step(stmt);
	if (nRet != SQLITE_ROW)
	{
		sqlite3_finalize(stmt);
		sqlite3_close(conn);
		return 0;
	}

	int nCount = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return nCount;
}

UINT CMessageLogger::ReadSessMsgLogCount(UINT nUTalkNum, UINT nOffset, UINT nRows)
{
	sqlite3*conn = NULL;
	sqlite3_stmt*stmt = NULL;
	const char*err_msg = NULL;
	char* lpSql;
	int nRet;

	if (0 == nOffset && 0 == nRows)
		return GetSessMsgLogCount(nUTalkNum);

	nRet = sqlite3_open16(m_strFileName.c_str(), &conn);
	if (nRet != SQLITE_OK)
		return 0;

	lpSql = "SELECT COUNT(*) FROM (SELECT* FROM [tb_SessMsg] WHERE [uin]=? ORDER BY [time] LIMIT ?,?)";

	nRet = sqlite3_prepare(conn, lpSql, -1, &stmt, &err_msg);
	if (nRet != SQLITE_OK)
	{
		sqlite3_close(conn);
		return 0;
	}

	sqlite3_bind_int(stmt, 1, nUTalkNum);
	sqlite3_bind_int(stmt, 2, nOffset);
	sqlite3_bind_int(stmt, 3, nRows);

	nRet = sqlite3_step(stmt);
	if (nRet != SQLITE_ROW)
	{
		sqlite3_finalize(stmt);
		sqlite3_close(conn);
		return 0;
	}

	int nCount = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	sqlite3_close(conn);

	return nCount;
}

void CMessageLogger::Replace(tstring& strText, const TCHAR* lpOldStr, const TCHAR* lpNewStr)
{
	if (NULL == lpOldStr || NULL == lpNewStr)
		return;

	int nOldStrLen = _tcslen(lpOldStr);
	int nNewStrLen = _tcslen(lpNewStr);

	tstring::size_type nPos = 0;
	while ((nPos = strText.find(lpOldStr, nPos)) != tstring::npos)
	{
		strText.replace(nPos, nOldStrLen, lpNewStr);
		nPos += nNewStrLen;
	}
}

