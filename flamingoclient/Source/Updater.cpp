#include "stdafx.h"
#include "Updater.h"
#include "UserSessionData.h"
#include "File2.h"
#include "EncodeUtil.h"
#include "Utils.h"
#include "FileTaskThread.h"

Updater::Updater(CFileTaskThread* pFileTask)
{
    m_pFileTask = pFileTask;
}

Updater::~Updater()
{
	m_aryUpdateFileList.clear();
}

BOOL Updater::IsNeedUpdate()
{
	//assert(m_lpProcotol != NULL);

	TCHAR szLocalVersionFile[MAX_PATH] = {0};
	_tcscpy_s(szLocalVersionFile, MAX_PATH, g_szHomePath);
	_tcscat_s(szLocalVersionFile, MAX_PATH, _T("update"));
	//如果程序目录下Update文件夹不存在则先创建之
	//或者不存在，Update文件夹创建不成功，下面的版本文件也会下载不成功
	if(!::PathFileExists(szLocalVersionFile) && !::CreateDirectory(szLocalVersionFile, NULL))
		return FALSE;
	
	_tcscat_s(szLocalVersionFile, MAX_PATH, _T("\\update2.version"));
	//检测本地版本文件是否存在，不存在，则从服务器下载
	if(!::PathFileExists(szLocalVersionFile))
	{	
		//下载失败也不升级
        if (!m_pFileTask->DownloadFileSynchronously("update2.version", szLocalVersionFile, FALSE))
			return FALSE;
	}
	
	//如果本地版本文件存在
	CFile file;
	if(!file.Open(szLocalVersionFile, FALSE))
		return FALSE;
	
	const char* pBuffer = file.Read();
	CString strLocal;
	if(pBuffer != NULL)
	{
        EncodeUtil::Utf8ToUnicode(pBuffer, strLocal.GetBuffer(file.GetSize() * 2), file.GetSize() * 2);
		strLocal.ReleaseBuffer();
	}
	if(strLocal.IsEmpty())
		return FALSE;

	file.Close();

	//从服务器下载校准版本文件
	TCHAR szCorrectionVersionFile[MAX_PATH] = {0};
	_tcscpy_s(szCorrectionVersionFile, MAX_PATH, g_szHomePath);
	_tcscat_s(szCorrectionVersionFile, MAX_PATH, _T("update\\update.version"));
	if(!m_pFileTask->DownloadFileSynchronously("update.version", szCorrectionVersionFile, TRUE))
		return FALSE;

	//读取服务器版本文件信息
	if(!file.Open(szCorrectionVersionFile, FALSE))
		return FALSE;

	CString strCorrection;
	pBuffer = file.Read();
	if(pBuffer != NULL)
	{
        EncodeUtil::Utf8ToUnicode(pBuffer, strCorrection.GetBuffer(file.GetSize() * 2), file.GetSize() * 2);
		strCorrection.ReleaseBuffer();
	}
	if(strCorrection.IsEmpty())
		return FALSE;

	file.Close();

	
	//服务器上版本大于本地版本需要升级
	if(strCorrection <= strLocal)
		return FALSE;

	SplitString(strCorrection, _T("\r\n"), m_aryUpdateFileList);	
	
	return TRUE;
}
