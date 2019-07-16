#pragma once
#include "Thread.h"
#include <list>
#include <mutex>
#include <condition_variable>

class CIUProtocol;
class CFileItemRequest;
class CFlamingoClient;
class CUploadFileResult;

//TODO: 后面改成
class CFileTaskThread : public CThread
{
public:
	CFileTaskThread();
	virtual ~CFileTaskThread(void);

public:
	virtual void Stop() override;
	
	BOOL AddItem(CFileItemRequest* pItem);
	void RemoveItem(CFileItemRequest* pItem);									//移除队列中下载或者上传的项
	void ClearAllItems();

	void HandleItem(CFileItemRequest* pFileItem);

	//void SendConfirmMessage(const CUploadFileResult* pUploadFileResult);		//聊天图片或者文件上传成功以后发送确认聊天消息
    //用于升级功能的版本号文件下载
    BOOL DownloadFileSynchronously(LPCSTR lpszFileName, LPCTSTR lpszDestPath, BOOL bOverwriteIfExist);

protected:
	virtual void Run() override;

private:
    long UploadFile(PCTSTR pszFileName, HWND hwndReflection, HANDLE hCancelEvent, CUploadFileResult& uploadFileResult);
	void FillUploadFileResult(CUploadFileResult& uploadFileResult, PCTSTR pszLocalName, PCSTR pszRemoteName, int64_t nFileSize, char* pszMd5);
    long DownloadFile(LPCSTR lpszFileName, LPCTSTR lpszDestPath, BOOL bOverwriteIfExist, HWND hwndReflection, HANDLE hCancelEvent);
    
public:
	CIUProtocol*					m_pProtocol;
	CFlamingoClient*				m_lpFMGClient;

private:
	CFileItemRequest*				m_pCurrentTransferringItem;					//当前正在传输的项
	std::list<CFileItemRequest*>	m_Filelist;	
	std::mutex						m_mtItems;
	std::condition_variable			m_cvItems;
    int32_t                         m_seq;
};