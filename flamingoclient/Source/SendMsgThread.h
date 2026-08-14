/**
 *  重连登录 worker，SendMsgThread.h
 *
 *  本类原是 v1 的"发送队列线程"，各种请求（注册 / 改状态 / 发消息 / 心跳 / 建群
 *  等）都投给它，由它序列化成 v1 报文发出。v2 改造后那些发送路径全部改走
 *  CIUSocket 的 EnqueueFrame，构造这些请求对象的代码（v1 注册框 / 建群框 / 改密框、
 *  v1 聊天窗、CFlamingoClient 的一批 v1 发送函数）已于 2026-08-14 删除。
 *
 *  现在它只剩一个用途：**把掉线自动重连的登录放到后台线程上跑**。
 *  链路是 MainDlg 的重连定时器 → CFlamingoClient::Login() 投一个 CLoginRequest →
 *  本线程 → HandleLogon → CIUSocket::Login()（走 v2 的 cmd_login）。
 *  之所以还需要一个线程：CIUSocket::Login 是阻塞调用，要等服务端应答帧回来，
 *  在 UI 线程上直接调会把界面卡住。
 *
 *  队列仍沿用 v1 的 CNetData 派生体系作为投递载体，仅因它现成可用；日后若要
 *  彻底摆脱 v1 的数据结构，可把队列换成任务闭包，不影响外部调用方。
 *
 *  balloonwj@qq.com   2026-08-14（本轮 v1 清理后重写文件头）
 */
#pragma once
#include <list>
#include <vector>
#include <mutex>
#include <condition_variable>
#include "net/IUProtocolData.h"
#include "Utils.h"
#include "Thread.h"

class CUserMgr;
class CFlamingoClient;
class CBuddyMessage;
class CGroupMessage;
class CSessMessage;
class CContent;

// 写入一条好友消息记录
void WriteBuddyMsgLog(CUserMgr* lpUserMgr, UINT nUTalkNum, LPCTSTR lpNickName, 
					  BOOL bSelf, CBuddyMessage* lpMsg);

// 写入一条群消息记录
void WriteGroupMsgLog(CUserMgr* lpUserMgr, UINT nGroupNum, UINT nUTalkNum, 
					  LPCTSTR lpNickName, CBuddyMessage* lpMsg);

// 写入一条临时会话(群成员)消息记录
void WriteSessMsgLog(CUserMgr* lpUserMgr, UINT nUTalkNum, LPCTSTR lpNickName, 
					 BOOL bSelf, CSessMessage* lpMsg);

class CMsgItem
{
public:
	CMsgItem(void);
	~CMsgItem(void);

public:
	long				m_nType;
	void*				m_lpMsg;
	UINT				m_nGroupNum;		
	UINT				m_nUTalkNum;		//TODO: 需要删除
	std::vector<UINT>	m_arrTargetIDs;		//该消息的目标用户ID
	tstring				m_strNickName;
	tstring				m_strGroupSig;
	HWND				m_hwndFrom;			//消息来自哪个聊天窗口
};

class CNetData;
class CRegisterRequest;
class CLoginRequest;
class CUserBasicInfoRequest;
class CUserExtendInfoRequest;
class CSentChatMessage;
class CSentChatConfirmImageMessage;
class CHeartbeatMessageRequest;
class CUpdateLogonUserInfoRequest;
class CModifyPasswordRequest;
class CCreateNewGroupRequest;

class CSendMsgThread : public CThread
{
public:
    CSendMsgThread();
    virtual ~CSendMsgThread(void);

public:
	void AddItem(CNetData* pItem);
	void DeleteAllItems();
	

	virtual void Stop() override;

protected:
    virtual void Run() override;

private:
    void HandleItem(CNetData* pNetData);
    void HandleLogon(const CLoginRequest* pLoginRequest);

	
    //TODO: 这四个函数起始可以合并成一个函数
    
	

public:
	CFlamingoClient*			m_lpFMGClient;
	CUserMgr*					m_lpUserMgr;

private:
	UINT						m_nMsgId;
	tstring						m_strGFaceKey;
	tstring						m_strGFaceSig;
	std::vector<CMsgItem*>		m_arrItem;

	std::list<CNetData*>		m_listItems;
    std::mutex                  m_mtItems;
    std::condition_variable     m_cvItems;

    int32_t                     m_seq{};            //包序列号
};
