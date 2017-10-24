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

	tstring GetUserHeadPicFullName(UINT uFace);			// 获取用户头像图片全路径文件名
	tstring GetBuddyHeadPicFullName(UINT uFace);		// 获取好友头像图片全路径文件名
	tstring GetGroupHeadPicFullName(UINT nGroupNum);	// 获取群头像图片全路径文件名
	tstring GetSessHeadPicFullName(UINT nUTalkNum);		// 获取群成员头像图片全路径文件名
	tstring GetChatPicFullName(LPCTSTR lpszFileName);	// 获取聊天图片全路径文件名
	tstring GetMsgLogFullName();						// 获取消息记录全路径文件名
	tstring GetDefaultRecvFilePath();					// 获取默认接收文件路径名

	BOOL IsNeedUpdateBuddyHeadPic(UINT nUTalkNum);		// 判断是否需要更新好友头像
	BOOL IsNeedUpdateGroupHeadPic(UINT nGroupNum);		// 判断是否需要更新群头像
	BOOL IsNeedUpdateSessHeadPic(UINT nUTalkNum);		// 判断是否需要更新群成员头像

	BOOL IsSelf(PCTSTR pszAccountName);
	
	
	BOOL IsFriend(UINT uAccountID);						//指定账号ID是否是好友
	BOOL IsFriend(PCTSTR pszAccountName);				//指定账号名是否是好友

	BOOL AddFriend(UINT uAccountID, PCTSTR pszAccountName, PCTSTR pszNickName);
	BOOL DeleteFriend(UINT uAccountID);

	
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

	BOOL LoadTeamInfo();								//从磁盘中加载分组信息
	BOOL StoreTeamInfo();								//将分组信息存储至磁盘

	BOOL LoadBuddyInfo();
	BOOL SaveBuddyInfo();

	PCTSTR GetMarkNameByAccountID(UINT uAccountID);
	long GetTeamIndexByAccountID(UINT uAccountID);
	

	void ClearUserInfo();

	long GetTotalBuddyCount();	

public:		
	HWND			m_hProxyWnd;
	HWND			m_hCallBackWnd;		//设置为主窗口对话框
	
	CBuddyInfo		m_UserInfo;
	CBuddyList		m_BuddyList;
	
	CMessageList	m_MsgList;
	CMessageLogger	m_MsgLogger;

	CGroupList		m_GroupList;
	CRecentList		m_RecentList;

private:
	std::map<UINT, BuddyInfo*>		m_mapLocalBuddyInfo;
};
