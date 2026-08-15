#pragma once

#include "UserSessionData.h"
#include "MessageLogger.h"
#include "UserConfig.h"
#include <map>

#define GROUP_ID_BASE 0x0FFFFFFF

#define IsGroupTarget(u) ((u >= GROUP_ID_BASE) ? TRUE : FALSE)

//当前登录的用户信息
class CUserMgr
{
public:
    static CUserMgr& GetInstance();

public:
	CUserMgr(void);
	~CUserMgr(void);

public:
	tstring GetUserFolder();							// 获取用户文件夹存放路径
	
	tstring GetPersonalFolder();						// 获取个人文件夹存放路径
	tstring GetChatPicFolder();							// 获取聊天图片存放路径
	tstring GetCustomUserThumbFolder();					// 获取自定义用户头像目录

	tstring GetSessHeadPicFullName(UINT nUTalkNum);		// 获取群成员头像图片全路径文件名
	tstring GetChatPicFullName(LPCTSTR lpszFileName);	// 获取聊天图片全路径文件名
	tstring GetMsgLogFullName();						// 获取消息记录全路径文件名
	tstring GetDefaultRecvFilePath();					// 获取默认接收文件路径名
	void	SetCustomRecvFilePath(PCTSTR pszPath);		// 设置自定义接收目录（空=用默认）；GetDefaultRecvFilePath 优先用它

	BOOL IsNeedUpdateSessHeadPic(UINT nUTalkNum);		// 判断是否需要更新群成员头像

	BOOL IsSelf(PCTSTR pszAccountName);

	tstring GetAccountName(UINT uAccountID) const;			//通过内部ID获取账户名
	void	SetAccountName(UINT uAccountID, PCTSTR pszAccountName);

	tstring GetNickName(UINT uAccountID) const;				//通过内部ID获取昵称名
	void	SetNickName(UINT uAccountID, PCTSTR pszNickName);

	tstring GetGroupName(UINT uGroupCode);
	tstring	GetGroupAccount(UINT uGroupCode);

	UINT	GetFaceID(UINT uAccountID) const;
	void	SetFaceID(UINT uAccountID, UINT uFaceID);

	PCTSTR  GetCustomFace(UINT uAccountID) const;

	void	SetStatus(UINT uAccountID, long nStatus);	//设置自己或者某个好友的状态
	long	GetStatus(UINT uAccountID);					//获取某个自己或者某个好友的状态

    void	SetClientType(UINT uAccountID, long nNewClientType);
    long	GetClientType(UINT uAccountID);

	UINT	GetMsgID(UINT uAccountID) const;
	BOOL	SetMsgID(UINT uAccountID, UINT uMsgID);

	tstring GetNickName(PCTSTR pszAccountName) const;	//通过账户获取昵称名

	UINT	GetFaceID(PCTSTR pszAccountName) const;		//通过账户获取昵称

	void	ResetToOfflineStatus();						//重置成离线状态

	BOOL ExitGroup(UINT uGroupID);						//退出群组
	BOOL IsGroupMember(UINT uAccountID, UINT uGroupID);	//账户uAccount是否是指定群的成员

	BOOL LoadRecentList();								//从磁盘中加载最近联系人
	BOOL StoreRecentList();								//将最近联系人存储至磁盘

	long GetRecentListCount();							//获取最近联系人数量
	void ClearRecentList();								//清空最近联系人
	BOOL DeleteRecentItem(UINT uAccountID);				//删除指定好友会话

	void ClearUserInfo();

	long GetTotalBuddyCount();	

public:		
	HWND			m_hProxyWnd;
	HWND			m_hCallBackWnd;		//设置为主窗口对话框
	
	//企业版说明：v1 好友列表（CBuddyList m_BuddyList）及其增删查改、分组信息 / 好友信息的
	//本地落盘（BuddyTeamInfo.bti / BuddyInfo.bi）已整体删除 —— 通讯录改由服务端组织架构
	//下发，由 BalloonMainPanel 自己缓存 v2 联系人（见其 m_contacts）。此处只保留本人资料、
	//消息列表、群列表与最近联系人列表。
	CBuddyInfo		m_UserInfo;

	CMessageList	m_MsgList;
	CMessageLogger	m_MsgLogger;

	CGroupList		m_GroupList;
	CRecentList		m_RecentList;

private:
	tstring							m_strCustomRecvPath;	// 系统设置里选的自定义接收目录；空=用默认推导路径
};
