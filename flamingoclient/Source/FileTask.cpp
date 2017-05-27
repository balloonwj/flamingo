#include "stdafx.h"
#include "FileTask.h"
#include "UserSessionData.h"
#include "IULog.h"
#include "MD5Sum.h"
#include "Utils.h"
#include "EncodingUtil.h"
#include "Path.h"
#include "FlamingoClient.h"
#include "File.h"
#include "MiniBuffer.h"
#include "net/IUProtocolData.h"
#include "net/IUSocket.h"
#include "net/FileMsg.h"
#include "net/protocolstream.h"
#include "net/IUProtocolData.h"

using namespace balloon;

CFileTask::CFileTask(CIUSocket* sockeClient) : m_SocketClient(sockeClient), m_seq(0)
{
	m_lpFMGClient = NULL;
	m_pProtocol = NULL;
	
	m_pCurrentTransferringItem = NULL;
}

CFileTask::~CFileTask()
{
	ClearAllItems();
}


void CFileTask::Stop()
{
	m_bStop = true;
	m_cvItems.notify_one();
}

BOOL CFileTask::AddItem(CFileItemRequest* pItem)
{
	if (NULL == pItem)
		return FALSE;

	std::lock_guard<std::mutex> guard(m_mtItems);
	m_Filelist.push_back(pItem);
	m_cvItems.notify_one();

	return TRUE;
}

void CFileTask::RemoveItem(CFileItemRequest* pItem)
{
	if(pItem == NULL)
		return;

	std::lock_guard<std::mutex> guard(m_mtItems);
	//未开始下载或上传的项直接移除
	if(pItem->m_bPending)
	{
		std::list<CFileItemRequest*>::iterator iter = m_Filelist.begin();
		for(; iter!=m_Filelist.end(); ++iter)
		{
			if(pItem ==*iter)
			{
				m_Filelist.erase(iter);
				delete pItem;
				break;
			}
		}
	}
	//正在下载或上传的项使用信号让其停止
	else
	{
		if(pItem->m_hCancelEvent != NULL)
			::SetEvent(pItem->m_hCancelEvent);
	}

	//AtlTrace(_T("File item count: %d.\n"), m_Filelist.size());
}

void CFileTask::ClearAllItems()
{
	std::lock_guard<std::mutex> guard(m_mtItems);
	for (auto& iter : m_Filelist)
	{
		delete iter;
	}

	m_Filelist.clear();
}

void CFileTask::Run()
{
	while (!m_bStop)
	{
		CFileItemRequest* pFileItem;
		{
			std::unique_lock<std::mutex> guard(m_mtItems);
			while (m_Filelist.empty())
			{
				if (m_bStop)
					return;

				m_cvItems.wait(guard);
			}

			pFileItem = m_Filelist.front();
			m_Filelist.pop_front();
		}

		HandleItem(pFileItem);
	}
}

void CFileTask::HandleItem(CFileItemRequest* pFileItem)
{
	if(pFileItem==NULL || pFileItem->m_uType!=NET_DATA_FILE)
		return;

	//开始进行处理
	pFileItem->m_bPending = FALSE;
	m_pCurrentTransferringItem = pFileItem;
	
	BOOL bRet = FALSE;
	CUploadFileResult* pUploadFileResult = new CUploadFileResult();
	if(pFileItem->m_nFileType == FILE_ITEM_UPLOAD_USER_THUMB)
	{
		bRet = UploadUserThumb(pFileItem->m_szFilePath, pFileItem->m_hwndReflection,*pUploadFileResult);
		long nUploadUserThumbResult = (bRet==0 ? UPLOAD_USER_THUMB_RESULT_SUCCESS : UPLOAD_USER_THUMB_RESULT_FAILED);
        ::PostMessage(pFileItem->m_hwndReflection, FMG_MSG_UPLOAD_USER_THUMB, (WPARAM)bRet, (LPARAM)pUploadFileResult);      
	}
	else if(pFileItem->m_nFileType==FILE_ITEM_UPLOAD_CHAT_IMAGE || pFileItem->m_nFileType==FILE_ITEM_UPLOAD_CHAT_OFFLINE_FILE)
	{
		//上传文件如果失败，则重试三次
		pUploadFileResult->m_uSenderID = pFileItem->m_uSenderID;
		pUploadFileResult->m_setTargetIDs = pFileItem->m_setTargetIDs;
		pUploadFileResult->m_nFileType = pFileItem->m_nFileType; 
		pUploadFileResult->m_hwndReflection = pFileItem->m_hwndReflection;
		long nRetCode;
		while(pFileItem->m_nRetryTimes < 3)
		{
			//nRetCode = m_pProtocol->UploadFile3(pFileItem->m_szFilePath, pFileItem->m_hwndReflection, pFileItem->m_hCancelEvent,*pUploadFileResult);
			nRetCode = UploadFile(pFileItem->m_szFilePath, pFileItem->m_hwndReflection, pFileItem->m_hCancelEvent,*pUploadFileResult);
			if(nRetCode==FILE_UPLOAD_SUCCESS || nRetCode==FILE_UPLOAD_USERCANCEL)
				break;

			::Sleep(3000);

			++pFileItem->m_nRetryTimes;
		}
		
		//除非用户取消，否则上传成功或失败都要告诉对方
		if(nRetCode != FILE_UPLOAD_USERCANCEL)
		{
			//SendConfirmMessage(pUploadFileResult);

			//克隆一份上传结果数据以用于PostMessage
			CUploadFileResult* pResult = new CUploadFileResult();
			pResult->Clone(pUploadFileResult);
			long nSendFileResultCode = (nRetCode==FILE_UPLOAD_SUCCESS ? SEND_FILE_SUCCESS : SEND_FILE_FAILED);
            //如果对话框已经关闭，则直接发给代理窗口
            if (::IsWindow(pFileItem->m_hwndReflection))
                ::PostMessage(pFileItem->m_hwndReflection, FMG_MSG_SEND_FILE_RESULT, (WPARAM)nSendFileResultCode, (LPARAM)pResult);
            else
                ::PostMessage(m_lpFMGClient->m_UserMgr.m_hProxyWnd, FMG_MSG_SEND_FILE_RESULT, (WPARAM)nSendFileResultCode, (LPARAM)pResult);			
		}
	}
	else if(pFileItem->m_nFileType==FILE_ITEM_DOWNLOAD_USER_THUMB)
	{
		CString strDumyPath;
		CString strThumb;
		//std::set<UINT>::const_iterator iter = pFileItem->m_setTargetIDs.begin();
		UINT uTargetID = pFileItem->m_uAccountID;
		strThumb.Format(_T("%s%u.png"), m_lpFMGClient->m_UserMgr.GetCustomUserThumbFolder().c_str(), uTargetID);
		long nRetCode = DownloadFile3(pFileItem->m_szUtfFilePath, strThumb, TRUE, pFileItem->m_hwndReflection, pFileItem->m_hCancelEvent);

		if(nRetCode == FILE_DOWNLOAD_SUCCESS)
		{		
			TransformImage(strThumb, strThumb, 64, 64, strDumyPath);
			::PostMessage(m_lpFMGClient->m_UserMgr.m_hProxyWnd, FMG_MSG_CUSTOMFACE_AVAILABLE, (WPARAM)(uTargetID), 0);
		}
        else
            CIULog::Log(LOG_ERROR, __FUNCSIG__, "download user thumb [%s] failed, userid = %d.", pFileItem->m_szUtfFilePath, pFileItem->m_uAccountID);
	}
	//下载离线文件
	else if(pFileItem->m_nFileType == FILE_ITEM_DOWNLOAD_CHAT_OFFLINE_FILE)
	{
		long nRetCode;
		while(pFileItem->m_nRetryTimes < 3)
		{
            nRetCode = DownloadFile3(pFileItem->m_szUtfFilePath, pFileItem->m_szFilePath, TRUE, pFileItem->m_hwndReflection, pFileItem->m_hCancelEvent);
            //nRetCode = m_pProtocol->DownloadFile3(pFileItem->m_szUtfFilePath, pFileItem->m_szFilePath, TRUE, pFileItem->m_hwndReflection, pFileItem->m_hCancelEvent);
			if(nRetCode==FILE_DOWNLOAD_SUCCESS || nRetCode==FILE_DOWNLOAD_USERCANCEL)
				break;

			++pFileItem->m_nRetryTimes;
			::Sleep(3000);
		}
		
		//TODO: 应该改成PostMessage
		if(nRetCode == FILE_DOWNLOAD_SUCCESS)
			::SendMessage(pFileItem->m_hwndReflection, FMG_MSG_RECV_FILE_RESULT, RECV_FILE_SUCCESS, (LPARAM)pFileItem);
		else if(nRetCode == FILE_DOWNLOAD_FAILED)
			::SendMessage(pFileItem->m_hwndReflection, FMG_MSG_RECV_FILE_RESULT, RECV_FILE_FAILED, (LPARAM)pFileItem);
	}
    //下载聊天图片
	else if(pFileItem->m_nFileType == FILE_ITEM_DOWNLOAD_CHAT_IMAGE)
	{
		long nRetCode;
		while(pFileItem->m_nRetryTimes < 3)
		{
			if(pFileItem->m_szUtfFilePath[0] == NULL)
				break;
			//nRetCode = m_pProtocol->DownloadFile3(pFileItem->m_szUtfFilePath, pFileItem->m_szFilePath, TRUE, pFileItem->m_hwndReflection, pFileItem->m_hCancelEvent);
            nRetCode = DownloadFile3(pFileItem->m_szUtfFilePath, pFileItem->m_szFilePath, TRUE, pFileItem->m_hwndReflection, pFileItem->m_hCancelEvent);
            if(nRetCode==FILE_DOWNLOAD_SUCCESS || nRetCode==FILE_DOWNLOAD_USERCANCEL)
				break;

			++pFileItem->m_nRetryTimes;
			::Sleep(3000);
		}
		
		CBuddyMessage* lpMsg = pFileItem->m_pBuddyMsg;

		UINT nSenderID = lpMsg->m_nFromUin;
		UINT nTargetID = lpMsg->m_nToUin;
		
		if(IsGroupTarget(nTargetID))
		{
			//非其他平台同步的消息，界面提示用户
			if(m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID != nSenderID)
				::PostMessage(m_lpFMGClient->m_UserMgr.m_hProxyWnd, FMG_MSG_GROUP_MSG, 0, (LPARAM)lpMsg);
		}
		else
		{
			//其它平台同步的消息
			if(m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID == nSenderID)
			{
				::PostMessage(m_lpFMGClient->m_UserMgr.m_hProxyWnd, FMG_MSG_BUDDY_MSG, 0, (LPARAM)lpMsg);
			}	
			else
			{
				//正常好友发来的消息
				if(nTargetID == m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID)
					::PostMessage(m_lpFMGClient->m_UserMgr.m_hProxyWnd, FMG_MSG_BUDDY_MSG, 0, (LPARAM)lpMsg);
				else
				{
					lpMsg->m_nFromUin = nTargetID;
					::PostMessage(m_lpFMGClient->m_UserMgr.m_hProxyWnd, FMG_MSG_BUDDY_MSG, 0, (LPARAM)lpMsg);
				}
			}
		}
	}
	
    //TODO: 记得在对应的目标窗口出删除，否则会有内存泄露
	//delete pUploadFileResult;
	delete pFileItem;

	m_pCurrentTransferringItem = NULL;
}

//void CFileTask::SendConfirmMessage(const CUploadFileResult* pUploadFileResult)
//{
//	if(pUploadFileResult == NULL)
//		return;
//	
//	//上传图片结果
//	if(pUploadFileResult->m_nFileType == FILE_ITEM_UPLOAD_CHAT_IMAGE)
//	{
//		time_t nTime = time(NULL);
//		TCHAR szMd5[64] = {0};
//		AnsiToUnicode(pUploadFileResult->m_szMd5, szMd5, ARRAYSIZE(szMd5));
//		CString strImageName;
//		strImageName.Format(_T("%s.%s"), szMd5, Hootina::CPath::GetExtension(pUploadFileResult->m_szLocalName).c_str());
//		long nWidth = 0;
//		long nHeight = 0;
//		GetImageWidthAndHeight(pUploadFileResult->m_szLocalName, nWidth, nHeight);
//		char szUtf8FileName[MAX_PATH] = {0};
//		UnicodeToUtf8(strImageName, szUtf8FileName, ARRAYSIZE(szUtf8FileName));
//		CStringA strImageAcquireMsg;
//        //if (pUploadFileResult->m_bSuccessful)
//        //    strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%llu,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"%s\",%u,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_szRemoteName, pUploadFileResult->m_dwFileSize, nWidth, nHeight);
//        //else
//        //    strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%llu,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"\",%u,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_dwFileSize, nWidth, nHeight);
//
//		if(pUploadFileResult->m_bSuccessful)
//            strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%llu,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"%s\",%u,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_szRemoteName, pUploadFileResult->m_dwFileSize, nWidth, nHeight);
//		else
//            strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%llu,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"\",%u,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_dwFileSize, nWidth, nHeight);
//
//		long nBodyLength = strImageAcquireMsg.GetLength()+1;
//		char* pszMsgBody = new char[nBodyLength];
//		memset(pszMsgBody, 0, nBodyLength);
//		strcpy_s(pszMsgBody, nBodyLength, strImageAcquireMsg);
//		CSentChatConfirmImageMessage* pConfirm = new CSentChatConfirmImageMessage();
//		pConfirm->m_hwndChat = pUploadFileResult->m_hwndReflection;
//		pConfirm->m_pszConfirmBody = pszMsgBody;
//		pConfirm->m_uConfirmBodySize = nBodyLength-1;
//		pConfirm->m_uSenderID = pUploadFileResult->m_uSenderID;
//		pConfirm->m_setTargetIDs = pUploadFileResult->m_setTargetIDs;
//		if(pConfirm->m_setTargetIDs.size() > 1)
//			pConfirm->m_nType = CHAT_CONFIRM_TYPE_MULTI;
//		else
//			pConfirm->m_nType = CHAT_CONFIRM_TYPE_SINGLE;
//
//		m_lpFMGClient->m_SendMsgThread.AddItem(pConfirm);
//	}
//}

long CFileTask::UploadFile(PCTSTR pszFileName, HWND hwndReflection, HANDLE hCancelEvent, CUploadFileResult& uploadFileResult)
{
	_tcscpy_s(uploadFileResult.m_szLocalName, ARRAYSIZE(uploadFileResult.m_szLocalName), pszFileName);

	//文件md5值
	char szMd5[64] = { 0 };
	//TODO: 提示用户正在校验文件
	FileProgress* pFileProgress = NULL;
	pFileProgress = new FileProgress();
	memset(pFileProgress, 0, sizeof(FileProgress));
	pFileProgress->nPercent = -1;
	pFileProgress->nVerificationPercent = 0;
	_tcscpy_s(pFileProgress->szDestPath, ARRAYSIZE(pFileProgress->szDestPath), pszFileName);
	::PostMessage(hwndReflection, FMG_MSG_SEND_FILE_PROGRESS, 0, (LPARAM)pFileProgress);
	long nRetCode = GetFileMd5ValueA(pszFileName, szMd5, ARRAYSIZE(szMd5), hwndReflection, hCancelEvent);
	if (nRetCode == GET_FILE_MD5_FAILED)
	{
		CIULog::Log(LOG_ERROR, __FUNCSIG__, _T("Failed to upload file:%s as unable to get file md5."), pszFileName);
		return FILE_UPLOAD_FAILED;
	}
	else if (nRetCode == GET_FILE_MD5_USERCANCEL)
	{
		CIULog::Log(LOG_NORMAL, __FUNCSIG__, _T("User cancel to upload file:%s."), pszFileName);
		return FILE_UPLOAD_USERCANCEL;
	}

	strcpy_s(uploadFileResult.m_szMd5, ARRAYSIZE(uploadFileResult.m_szMd5), szMd5);

	HANDLE	hFile = ::CreateFile(pszFileName,
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		CIULog::Log(LOG_ERROR, __FUNCSIG__, _T("Failed to upload file:%s as unable to open the file."), pszFileName);
		return FILE_UPLOAD_FAILED;
	}

	//方便hFile在函数调用结束时自动关闭
	CAutoFileHandle autoFile(hFile);

	//文件utf8格式名称
	char szUtf8Name[MAX_PATH] = { 0 };
	UnicodeToUtf8(::PathFindFileName(pszFileName), szUtf8Name, ARRAYSIZE(szUtf8Name));

	//文件大小
	DWORD nFileSize = ::GetFileSize(hFile, NULL);
	//文件太大，转换成nFileSize会溢出导致nFileSize值为负数
	if (nFileSize < 0)
	{
		CIULog::Log(LOG_ERROR, __FUNCSIG__, _T("Failed to upload file:%s as file is too big."), pszFileName);
		return FILE_UPLOAD_FAILED;
	}
	uploadFileResult.m_dwFileSize = nFileSize;

    size_t offsetX = 0;
    size_t t = 9;
    size_t offset2 = 0;

    while (true)
    {
        std::string outbuf;
        BinaryWriteStream writeStream(&outbuf);
        writeStream.WriteInt32(msg_type_upload_req);
        writeStream.WriteInt32(m_seq);
        writeStream.WriteCString(szMd5, 32);
        writeStream.WriteInt32((int32_t)offsetX);
        writeStream.WriteInt32((int32_t)nFileSize);
        size_t eachfilesize = 512* 1024;
        if (nFileSize - offsetX < eachfilesize)
            eachfilesize = nFileSize - offsetX;
        CMiniBuffer buffer(eachfilesize);
        DWORD dwFileRead;
        if (!::ReadFile(hFile, buffer.GetBuffer(), (DWORD)eachfilesize, &dwFileRead, NULL) || eachfilesize != dwFileRead)
            break;
        string filedata;
        filedata.append(buffer.GetBuffer(), buffer.GetSize());
        writeStream.WriteString(filedata);
        writeStream.Flush();
        file_msg headerx = { outbuf.length() };
        outbuf.insert(0, (const char*)&headerx, sizeof(headerx));
        if (!m_SocketClient->SendOnFilePort(outbuf.c_str(), outbuf.length()))
            break;

        offsetX += eachfilesize;
        pFileProgress = new FileProgress();
        memset(pFileProgress, 0, sizeof(FileProgress));
        //AtlTrace(_T("nTotalSent:%d\n"), nTotalSent);
        //AtlTrace(_T("nFileSize:%d\n"), nFileSize);
        //nTotalSent*100可能会超出long的范围，故先临时转换成__int64
        pFileProgress->nPercent = (long)((__int64)offsetX* 100 / nFileSize);
        //AtlTrace(_T("pFileProgress->nPercent:%d\n"), pFileProgress->nPercent);
        _tcscpy_s(pFileProgress->szDestPath, ARRAYSIZE(pFileProgress->szDestPath), pszFileName);
        ::PostMessage(hwndReflection, FMG_MSG_SEND_FILE_PROGRESS, 0, (LPARAM)pFileProgress);

        file_msg header;
        if (!m_SocketClient->RecvOnFilePort((char*)&header, sizeof(header)))
            break;

        CMiniBuffer recvBuf(header.packagesize);
        if (!m_SocketClient->RecvOnFilePort(recvBuf.GetBuffer(), recvBuf.GetSize()))
            break;

        BinaryReadStream readStream(recvBuf.GetBuffer(), recvBuf.GetSize());
        int32_t cmd;
        if (!readStream.ReadInt32(cmd) || cmd != msg_type_upload_resp)
            break;

        //int seq;
        if (!readStream.ReadInt32(m_seq))
            break;

        std::string filemd5;
        size_t md5length;
        if (!readStream.ReadString(&filemd5, 0, md5length) || md5length != 32)
            break;

        int32_t offset;
        if (!readStream.ReadInt32(offset))
            break;

        int32_t filesize;
        if (!readStream.ReadInt32(filesize))
            break;

        string dummyfiledata;
        size_t filedatalength;
        if (!readStream.ReadString(&dummyfiledata, 0, filedatalength) || filedatalength != 0)
            break;

        if (offset == -1 && filesize == -1)
        {
            FillUploadFileResult(uploadFileResult, pszFileName, filemd5.c_str(), nFileSize, szMd5);
            CIULog::Log(LOG_NORMAL, __FUNCSIG__, _T("Succeed to upload file:%s as there already exist file on server."), pszFileName);

            //TODO: 如果外部不释放则会有内存泄露
            pFileProgress = new FileProgress();
            memset(pFileProgress, 0, sizeof(FileProgress));
            pFileProgress->nPercent = 100;
            _tcscpy_s(pFileProgress->szDestPath, ARRAYSIZE(pFileProgress->szDestPath), pszFileName);
            ::PostMessage(hwndReflection, FMG_MSG_SEND_FILE_PROGRESS, 0, (LPARAM)pFileProgress);
            return FILE_UPLOAD_SUCCESS;
        }

    }


	return FILE_UPLOAD_FAILED;
}

void CFileTask::FillUploadFileResult(CUploadFileResult& uploadFileResult, PCTSTR pszLocalName, PCSTR pszRemoteName, long nFileSize, char* pszMd5)
{
	uploadFileResult.m_bSuccessful = TRUE;
	uploadFileResult.m_dwFileSize = (DWORD)nFileSize;
	_tcscpy_s(uploadFileResult.m_szLocalName, ARRAYSIZE(uploadFileResult.m_szLocalName), pszLocalName);
	strcpy_s(uploadFileResult.m_szRemoteName, ARRAYSIZE(uploadFileResult.m_szRemoteName), pszRemoteName);
	strcpy_s(uploadFileResult.m_szMd5, ARRAYSIZE(uploadFileResult.m_szMd5), pszMd5);
}

long CFileTask::DownloadFile3(LPCSTR lpszFileName, LPCTSTR lpszDestPath, BOOL bOverwriteIfExist, HWND hwndReflection, HANDLE hCancelEvent)
{
    //TODO: 确定是否覆盖的方法应该是根据md5值来判断本地的文件和下载的文件是否完全相同
    if (Hootina::CPath::IsFileExist(lpszDestPath) && IUGetFileSize2(lpszDestPath)>0 && !bOverwriteIfExist)
    {
        CIULog::Log(LOG_NORMAL, __FUNCSIG__, _T("File %s already exsited, there is no need to download."), lpszDestPath);
        return FILE_DOWNLOAD_SUCCESS;
    }

    //偏移量
    long nOffset = 0;
    long nBreakType = FILE_DOWNLOAD_SUCCESS;

    char* pBuffer = NULL;
    //nCurrentContentSize为当前收到的包中文件内容字节数大小
    long nCurrentContentSize = 0;
    DWORD dwSizeToWrite = 0;
    DWORD dwSizeWritten = 0;
    BOOL bRet = TRUE;
    long nFileSize = 0;

    HANDLE hFile = ::CreateFile(lpszDestPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    //AtlTrace(_T("lpszDestPath:%s.\n"), lpszDestPath);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        CIULog::Log(LOG_ERROR, __FUNCSIG__, _T("Failed to download file %s as unable to create the file."), lpszDestPath);
        return FILE_DOWNLOAD_FAILED;
    }
    CAutoFileHandle autoFileHandle(hFile);
    FileProgress* pFileProgress = NULL;
    
    size_t offset = 0;
    while (true)
    {
        std::string outbuf;
        BinaryWriteStream writeStream(&outbuf);
        writeStream.WriteInt32(msg_type_download_req);
        writeStream.WriteInt32(m_seq);
        writeStream.WriteCString(lpszFileName, strlen(lpszFileName));
        size_t dummyoffset = 0;
        writeStream.WriteInt32((int32_t)dummyoffset);
        size_t dummyfilesize = 0;
        writeStream.WriteInt32((int32_t)dummyfilesize);
        string dummyfiledata;
        writeStream.WriteString(dummyfiledata);
        writeStream.Flush();

        file_msg header = { outbuf.length() };
        outbuf.insert(0, (const char*)&header, sizeof(header));

        if (!m_SocketClient->SendOnFilePort(outbuf.c_str(), outbuf.length()))
        {
            nBreakType = FILE_DOWNLOAD_FAILED;
            break;
        }

        file_msg recvheader;
        if (!m_SocketClient->RecvOnFilePort((char*)&recvheader, sizeof(recvheader)))
        {
            nBreakType = FILE_DOWNLOAD_FAILED;
            break;
        }

        CMiniBuffer buffer(recvheader.packagesize);
        if (!m_SocketClient->RecvOnFilePort(buffer.GetBuffer(), recvheader.packagesize))
        {
            nBreakType = FILE_DOWNLOAD_FAILED;
            break;
        }

        BinaryReadStream readStream(buffer.GetBuffer(), recvheader.packagesize);
        int32_t cmd;
        if (!readStream.ReadInt32(cmd) || cmd != msg_type_download_resp)
        {
            nBreakType = FILE_DOWNLOAD_FAILED;
            break;
        }

        //int seq;
        if (!readStream.ReadInt32(m_seq))
        {
            nBreakType = FILE_DOWNLOAD_FAILED;
            break;
        }

        std::string filemd5;
        size_t md5length;
        if (!readStream.ReadString(&filemd5, 0, md5length) || md5length == 0)
        {
            nBreakType = FILE_DOWNLOAD_FAILED;
            break;
        }

        int32_t offset;
        if (!readStream.ReadInt32(offset))
        {
            nBreakType = FILE_DOWNLOAD_FAILED;
            break;
        }

        int32_t filesize;
        if (!readStream.ReadInt32(filesize))
        {
            nBreakType = FILE_DOWNLOAD_FAILED;
            break;
        }

        string filedata;
        size_t filedatalength;
        if (!readStream.ReadString(&filedata, 0, filedatalength) || filedatalength == 0)
        {
            nBreakType = FILE_DOWNLOAD_FAILED;
            break;
        }

        DWORD dwBytesWritten;
        if (!::WriteFile(hFile, filedata.c_str(), filedata.length(), &dwBytesWritten, NULL) || dwBytesWritten != filedata.length())
        {
            nBreakType = FILE_DOWNLOAD_FAILED;
            break;
        }

        offset += filedata.length();

        //！！！！TODO: 对于非下载聊天图片，这块内存因为未释放而产生内存泄露！！！！
        pFileProgress = new FileProgress();
        memset(pFileProgress, 0, sizeof(FileProgress));
        _tcscpy_s(pFileProgress->szDestPath, ARRAYSIZE(pFileProgress->szDestPath), lpszDestPath);
        pFileProgress->nPercent = long(((__int64)offset* 100) / filesize);
        ::PostMessage(hwndReflection, FMG_MSG_RECV_FILE_PROGRESS, 0, (LPARAM)(pFileProgress));


        if (offset == filesize)
        {
            nBreakType = FILE_DOWNLOAD_SUCCESS;
            break;
        }
    }// end while-loop

    //下载成功
    if (nBreakType == FILE_DOWNLOAD_SUCCESS)
    {
        //::CloseHandle(hFile);
        CIULog::Log(LOG_NORMAL, _T("Succeed to download file: %s."), lpszDestPath);
        pFileProgress = new FileProgress();
        memset(pFileProgress, 0, sizeof(FileProgress));
        _tcscpy_s(pFileProgress->szDestPath, ARRAYSIZE(pFileProgress->szDestPath), lpszDestPath);
        pFileProgress->nPercent = /*nOffset* 100 / nFileSize*/100;
        ::PostMessage(hwndReflection, FMG_MSG_RECV_FILE_PROGRESS, 0, (LPARAM)(pFileProgress));
    }
    //下载失败或者用户取消下载
    else
    {
        if (nBreakType == FILE_DOWNLOAD_FAILED)
            CIULog::Log(LOG_ERROR, _T("Failed to download file: %s."), lpszDestPath);
        else
            CIULog::Log(LOG_NORMAL, __FUNCSIG__, _T("User canceled to download file: %s."), lpszDestPath);
        //为了能删除下载的半成品，显式关闭文件句柄
        autoFileHandle.Release();
        ::DeleteFile(lpszDestPath);
    }

    return nBreakType;
}

BOOL CFileTask::UploadUserThumb(PCTSTR pszFileName, HWND hwndReflection, CUploadFileResult& uploadFileResult)
{
    _tcscpy_s(uploadFileResult.m_szLocalName, ARRAYSIZE(uploadFileResult.m_szLocalName), pszFileName);

    //文件md5值
    char szMd5[64] = { 0 };
    //TODO: 提示用户正在校验文件
    FileProgress* pFileProgress = NULL;
    pFileProgress = new FileProgress();
    memset(pFileProgress, 0, sizeof(FileProgress));
    pFileProgress->nPercent = -1;
    pFileProgress->nVerificationPercent = 0;
    _tcscpy_s(pFileProgress->szDestPath, ARRAYSIZE(pFileProgress->szDestPath), pszFileName);
    ::PostMessage(hwndReflection, FMG_MSG_SEND_FILE_PROGRESS, 0, (LPARAM)pFileProgress);
    long nRetCode = GetFileMd5ValueA(pszFileName, szMd5, ARRAYSIZE(szMd5), hwndReflection, NULL);
    if (nRetCode == GET_FILE_MD5_FAILED)
    {
        CIULog::Log(LOG_ERROR, __FUNCSIG__, _T("Failed to upload file:%s as unable to get file md5."), pszFileName);
        return UPLOAD_USER_THUMB_RESULT_FAILED;
    }
    else if (nRetCode == GET_FILE_MD5_USERCANCEL)
    {
        CIULog::Log(LOG_NORMAL, __FUNCSIG__, _T("User cancel to upload file:%s."), pszFileName);
        return FILE_UPLOAD_USERCANCEL;
    }

    strcpy_s(uploadFileResult.m_szMd5, ARRAYSIZE(uploadFileResult.m_szMd5), szMd5);

    HANDLE	hFile = ::CreateFile(pszFileName,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        CIULog::Log(LOG_ERROR, __FUNCSIG__, _T("Failed to upload file:%s as unable to open the file."), pszFileName);
        return UPLOAD_USER_THUMB_RESULT_FAILED;
    }

    //方便hFile在函数调用结束时自动关闭
    CAutoFileHandle autoFile(hFile);

    //文件utf8格式名称
    char szUtf8Name[MAX_PATH] = { 0 };
    UnicodeToUtf8(::PathFindFileName(pszFileName), szUtf8Name, ARRAYSIZE(szUtf8Name));

    //文件大小
    DWORD nFileSize = ::GetFileSize(hFile, NULL);
    //文件太大，转换成nFileSize会溢出导致nFileSize值为负数
    if (nFileSize < 0)
    {
        CIULog::Log(LOG_ERROR, __FUNCSIG__, _T("Failed to upload file:%s as file is too big."), pszFileName);
        return UPLOAD_USER_THUMB_RESULT_FAILED;
    }
    uploadFileResult.m_dwFileSize = nFileSize;

    size_t offsetX = 0;
    size_t t = 9;
    size_t offset2 = 0;

    while (true)
    {
        std::string outbuf;
        BinaryWriteStream writeStream(&outbuf);
        writeStream.WriteInt32(msg_type_upload_req);
        writeStream.WriteInt32(m_seq);
        writeStream.WriteCString(szMd5, 32);
        writeStream.WriteInt32((int32_t)offsetX);
        writeStream.WriteInt32((int32_t)nFileSize);
        size_t eachfilesize = 512* 1024;
        if (nFileSize - offsetX < eachfilesize)
            eachfilesize = nFileSize - offsetX;
        CMiniBuffer buffer(eachfilesize);
        DWORD dwFileRead;
        if (!::ReadFile(hFile, buffer.GetBuffer(), (DWORD)eachfilesize, &dwFileRead, NULL) || eachfilesize != dwFileRead)
            break;
        string filedata;
        filedata.append(buffer.GetBuffer(), buffer.GetSize());
        writeStream.WriteString(filedata);
        writeStream.Flush();
        file_msg headerx = { outbuf.length() };
        outbuf.insert(0, (const char*)&headerx, sizeof(headerx));
        if (!m_SocketClient->SendOnFilePort(outbuf.c_str(), outbuf.length()))
            break;

        offsetX += eachfilesize;
        pFileProgress = new FileProgress();
        memset(pFileProgress, 0, sizeof(FileProgress));
        //AtlTrace(_T("nTotalSent:%d\n"), nTotalSent);
        //AtlTrace(_T("nFileSize:%d\n"), nFileSize);
        //nTotalSent*100可能会超出long的范围，故先临时转换成__int64
        pFileProgress->nPercent = (long)((__int64)offsetX* 100 / nFileSize);
        //AtlTrace(_T("pFileProgress->nPercent:%d\n"), pFileProgress->nPercent);
        _tcscpy_s(pFileProgress->szDestPath, ARRAYSIZE(pFileProgress->szDestPath), pszFileName);
        ::PostMessage(hwndReflection, FMG_MSG_SEND_FILE_PROGRESS, 0, (LPARAM)pFileProgress);

        file_msg header;
        if (!m_SocketClient->RecvOnFilePort((char*)&header, sizeof(header)))
            break;

        CMiniBuffer recvBuf(header.packagesize);
        if (!m_SocketClient->RecvOnFilePort(recvBuf.GetBuffer(), recvBuf.GetSize()))
            break;

        BinaryReadStream readStream(recvBuf.GetBuffer(), recvBuf.GetSize());
        int32_t cmd;
        if (!readStream.ReadInt32(cmd) || cmd != msg_type_upload_resp)
            break;

        //int seq;
        if (!readStream.ReadInt32(m_seq))
            break;

        std::string filemd5;
        size_t md5length;
        if (!readStream.ReadString(&filemd5, 0, md5length) || md5length != 32)
            break;

        int32_t offset;
        if (!readStream.ReadInt32(offset))
            break;

        int32_t filesize;
        if (!readStream.ReadInt32(filesize))
            break;

        string dummyfiledata;
        size_t filedatalength;
        if (!readStream.ReadString(&dummyfiledata, 0, filedatalength) || filedatalength != 0)
            break;

        if (offset == -1 && filesize == -1)
        {
            FillUploadFileResult(uploadFileResult, pszFileName, filemd5.c_str(), nFileSize, szMd5);
            CIULog::Log(LOG_NORMAL, __FUNCSIG__, _T("Succeed to upload file:%s as there already exist file on server."), pszFileName);

            //TODO: 如果外部不释放则会有内存泄露
            pFileProgress = new FileProgress();
            memset(pFileProgress, 0, sizeof(FileProgress));
            pFileProgress->nPercent = 100;
            _tcscpy_s(pFileProgress->szDestPath, ARRAYSIZE(pFileProgress->szDestPath), pszFileName);
            ::PostMessage(hwndReflection, FMG_MSG_SEND_FILE_PROGRESS, 0, (LPARAM)pFileProgress);
            return UPLOAD_USER_THUMB_RESULT_SUCCESS;
        }

    }


    return UPLOAD_USER_THUMB_RESULT_FAILED;
}

BOOL CFileTask::DownloadFileSynchronously(LPCSTR lpszFileName, LPCTSTR lpszDestPath, BOOL bOverwriteIfExist)
{
    return DownloadFile3(lpszFileName, lpszDestPath, bOverwriteIfExist, NULL, NULL) == FILE_DOWNLOAD_SUCCESS;
}