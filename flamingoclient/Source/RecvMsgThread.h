#pragma once

#include <mutex>
#include <condition_variable>
#include <list>
#include <string>
#include "UserMgr.h"
#include "Thread.h"

// 写入一条好友消息记录
void WriteBuddyMsgLog(CUserMgr* lpUserMgr, UINT nUTalkNum, 
					  LPCTSTR lpNickName, BOOL bSelf, CBuddyMessage* lpMsg);

// 写入一条群消息记录
void WriteGroupMsgLog(CUserMgr* lpUserMgr, UINT nGroupNum, UINT nUTalkNum, 
					  LPCTSTR lpNickName, CBuddyMessage* lpMsg);

// 写入一条临时会话(群成员)消息记录
void WriteSessMsgLog(CUserMgr* lpUserMgr, UINT nUTalkNum, 
					 LPCTSTR lpNickName, BOOL bSelf, CSessMessage* lpMsg);

class CGMemberMapKey
{
public:
	CGMemberMapKey(void):m_nGroupCode(0), m_nUTalkUin(0){}
	~CGMemberMapKey(void){}

public:
	bool operator < (const CGMemberMapKey& rhs) const
	{
		if(m_nGroupCode < rhs.m_nGroupCode) return true;
		if(m_nGroupCode > rhs.m_nGroupCode) return false;
		return  m_nUTalkUin < rhs.m_nUTalkUin;
	}

public:
	UINT m_nGroupCode;
	UINT m_nUTalkUin;
};

class CRecvMsg
{
public:
	CRecvMsg(void);
	~CRecvMsg(void);

public:
	CBuddyMessage*	m_lpMsg;
};

struct RMT_BUDDY_DATA
{
	UINT nUTalkNum;
	TCHAR szNickName[32];
};

struct RMT_GROUP_DATA
{
	BOOL bHasGroupInfo;
	UINT nGroupNum;
};

struct RMT_GMEMBER_REQ
{
	UINT nGroupCode;
	UINT nUTalkUin;
};

class CIUProtocol;
class CNetData;
class CRegisterResult;
class CLoginResult;
class CChatMessage;
class CFriendStatus;
class CUserBasicInfoResult;
class CUserExtendInfoResult;
class CLoginUserFriendsIDResult;
class CFindFriendResult;
class COperateFriendResult;
class CRecvChatMessage;
class CUpdateLogonUserInfoResult;
class CTargetInfoChangeResult;
class CModifyPasswordResult;
class CCreateNewGroupResult;

class CFlamingoClient;

//缓存的聊天消息
struct CachedChatMsg
{
    int         targetid;
    int         senderid;
    std::string strChatMsg;
};

//缓存的通知类消息
struct CachedNotifyMsg
{
    std::string strNotifyMsg;
};

class CRecvMsgThread : public CThread
{
public:
    CRecvMsgThread();
    virtual ~CRecvMsgThread(void);

public:
	void AddMsgData(const std::string& pMsgData);
	void DelAllMsgData();

    void NotifyNetError();

    void SetProxyWnd(HWND hwnd)
    {
        m_hProxyWnd = hwnd;
    }

    void EnableUI(bool bEnable);

	virtual void Stop() override;

protected:
    virtual void Run() override;

private:
	BOOL HandleMessage(const std::string& strMsg);
    BOOL HandleRegisterMessage(const std::string& strMsg);
    BOOL HandleLoginMessage(const std::string& strMsg);
    BOOL HandleFriendListInfo(const std::string& strMsg);
    BOOL HandleGroupBasicInfo(const std::string& strMsg);
    BOOL HandleFindFriendMessage(const std::string& strMsg);
    BOOL HandleUserStatusNotifyMessage(int targetId, const std::string& strMsg);
    BOOL HandleUpdateLogonUserInfoMessage(const std::string& strMsg);
    BOOL HandleModifyPasswordResult(const std::string& strMsg);

    BOOL HandleOperateFriendMessage(const std::string& strMsg);
    BOOL CacheNotifyMsg(const std::string& strMsg);

    BOOL CacheChatMessage(int32_t senderId, int32_t targetId, const std::string& strMsg);
    BOOL HandleChatMessage(int32_t senderId, int32_t targetId, const std::string& strMsg);

    BOOL HandleCreateNewGroupResult(const std::string& strMsg);
    BOOL HandleScreenshotMessage(int32_t targetId, const std::string& strBmpHeader, const std::string& strBmpData);

		   
	BOOL HandleTargetInfoChangeMessage(CTargetInfoChangeResult* pResult);

    BOOL ParseChatMessage(int32_t senderId, int32_t targetId, const std::string& strMsg, std::vector<CRecvMsg*>& arrMsg);
    BOOL ParseChatMessage(int32_t senderId, int32_t targetId, const std::string& strRawMsg, Json::Value& JsonValue, CRecvMsg& msg);
	
	BOOL HandleBuddyMsg(CRecvMsg* lpRecvMsg);
	BOOL HandleGroupMsg(CRecvMsg* lpRecvMsg);
	BOOL HandleSessMsg(CRecvMsg* lpRecvMsg);

	RMT_BUDDY_DATA* GetBuddyData(UINT nUTalkUin);
	RMT_GROUP_DATA* GetGroupData(UINT nGroupCode);
	RMT_BUDDY_DATA* GetGMemberData(UINT nGroupCode, UINT nUTalkUin);
	UINT GroupId2Code(UINT nGroupId);

	BOOL IsNeedDownloadPic(const CBuddyMessage* pBuddyMessage);
	void GetChatPic(CBuddyMessage* pBuddyMessage);

	BOOL ProcessBuddyMsg(CBuddyMessage* lpBuddyMsg);									//对收取的消息进行加工
	void AddItemToRecentSessionList(UINT uUserID, UINT uFaceID, PCTSTR pszNickName, PCTSTR pszText, time_t nMsgTime);		//向最近联系人列表中添加一项

public:
	CFlamingoClient*		                    m_pFMGClient;
	CUserMgr*			                        m_lpUserMgr;
	
private:
	std::list<std::string>						m_listItems;
    std::mutex                                  m_mtItems;
    std::condition_variable                     m_cvItems;

	std::map<UINT,RMT_BUDDY_DATA*>				m_mapBuddyData;
	std::map<UINT,RMT_GROUP_DATA*>				m_mapGroupData;
	std::map<UINT,UINT>							m_mapGroupId2Code;
	std::map<CGMemberMapKey, RMT_BUDDY_DATA*>	m_mapGMemberData;
	
    std::list<CachedChatMsg>	                m_listCachedChatMsg;			//缓存的聊天消息
    std::list<CachedNotifyMsg>                  m_listCachedNotifyMsg;          //缓存的通知类消息
	bool										m_bUIEnable;                    //UI界面是否已经准备好
    std::mutex                                  m_mtUIEnable;                   //多线程保护m_bUIEnable和m_listCacheChatMsg的变量

	UINT										m_nPreMsgId, m_nPreMsgId2;

    int32_t                                     m_seq;

    HWND                                        m_hProxyWnd;

    bool                                        m_bNetError;        //网络有错误
};