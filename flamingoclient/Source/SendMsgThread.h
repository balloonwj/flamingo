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
class CLoginUserFriendsIDRequest;
class CSentChatMessage;
class CSentChatConfirmImageMessage;
class CFindFriendRequest;
class COperateFriendRequest;
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
	
    BOOL AddBuddyMsg(UINT nFromUin, const tstring& strFromNickName, UINT nToUin, const tstring& strToNickName, time_t nTime, const tstring& strChatMsg, HWND hwndFrom = NULL);
	BOOL AddGroupMsg(UINT nGroupId, time_t nTime, LPCTSTR lpMsg, HWND hwndFrom=NULL);
	BOOL AddMultiMsg(const std::set<UINT> setAccountID, time_t nTime, LPCTSTR lpMsg, HWND hwndFrom=NULL);
	BOOL AddSessMsg(UINT nGroupId, UINT nToUin, time_t nTime, LPCTSTR lpMsg);

	virtual void Stop() override;

protected:
    virtual void Run() override;

private:
    void HandleItem(CNetData* pNetData);
	void HandleRegister(const CRegisterRequest* pRegisterRequest);
    void HandleLogon(const CLoginRequest* pLoginRequest);
	void HandleUserBasicInfo(const CUserBasicInfoRequest* pUserBasicInfo);
    void HandleChangeUserStatus(const CChangeUserStatusRequest* pChangeUserStatusRequest);
    void HandleGroupBasicInfo(const CGroupBasicInfoRequest* pGroupBasicInfo);
	BOOL HandleSentChatMessage(const CSentChatMessage* pSentChatMessage);				
	BOOL HandleSentConfirmImageMessage(const CSentChatConfirmImageMessage* pConfirmImageMessage);	//图片上传成功以后的确认信息
	void HandleFindFriendMessage(const CFindFriendRequest* pFindFriendRequest);
	void HandleOperateFriendMessage(const COperateFriendRequest* pOperateFriendRequest);
	BOOL HandleHeartbeatMessage(const CHeartbeatMessageRequest* pHeartbeatRequest);
	void HandleUpdateLogonUserInfoMessage(const CUpdateLogonUserInfoRequest* pRequest);
	void HandleModifyPassword(const CModifyPasswordRequest* pModifyPassword);
	void HandleCreateNewGroup(const CCreateNewGroupRequest* pCreateNewGroup);
    void HandleOperateTeam(const CAddTeamInfoRequest* pAddNewTeam);
    void HandleModifyFriendMarkName(const CModifyFriendMakeNameRequest* pModifyFriendMakeNameRequest);
    void HandleMoveFriendMessage(const CMoveFriendRequest* pMoveFriendRequest);

	BOOL HandleFontInfo(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent);
	BOOL HandleSysFaceId(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent);
	BOOL HandleShakeWindowMsg(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent);
	BOOL HandleCustomPic(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent);
	BOOL HandleFile(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent);
    BOOL HandleRemoteDesktop(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent);
	BOOL CreateMsgContent(const tstring& strChatMsg, std::vector<CContent*>& arrContent);
	
    //TODO: 这四个函数起始可以合并成一个函数
    BOOL SendBuddyMsg(CMsgItem* lpMsgItem);			// 发送好友消息
	BOOL SendGroupMsg(CMsgItem* lpMsgItem);			// 发送群消息
	BOOL SendMultiMsg(CMsgItem* lpMsgItem);			// 群发消息
    BOOL SendMultiChatMessage(const char* pszChatMsg, int nChatMsgLength, UINT* pAccountList, int nAccountNum);
	BOOL SendSessMsg(CMsgItem* lpMsgItem);			// 发送群成员消息
    
	
	BOOL ProcessBuddyMsg(CBuddyMessage* pMsg);		//对发送的消息进行加工
	BOOL ProcessMultiMsg(CMsgItem* pMsgItem);		//对群发消息进行加工
	void AddItemToRecentSessionList(UINT uUserID, UINT uFaceID, PCTSTR pszNickName, PCTSTR pszText, time_t nMsgTime);		//向最近联系人列表中添加一项
	std::wstring UnicodeToHexStr(const WCHAR* lpStr, BOOL bDblSlash);

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
