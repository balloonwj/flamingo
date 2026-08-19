#pragma once

#include "UserSessionData.h"
#include "..\Common\Log\MessageLogger.h"
#include "UserConfig.h"
#include <map>

#define GROUP_ID_BASE 0x0FFFFFFF

#define IsGroupTarget(u) ((u >= GROUP_ID_BASE) ? TRUE : FALSE)

//当前登录的用户信息
class CUserMgr
{
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

	tstring GetNickName(UINT uAccountID) const;				//通过内部ID获取昵称名

	void	SetStatus(UINT uAccountID, long nStatus);	//设置自己或者某个好友的状态

    void	SetClientType(UINT uAccountID, long nNewClientType);

	BOOL	SetMsgID(UINT uAccountID, UINT uMsgID);

	void	ResetToOfflineStatus();						//重置成离线状态

	BOOL LoadRecentList();								//从磁盘中加载最近联系人
	BOOL StoreRecentList();								//将最近联系人存储至磁盘

	void ClearUserInfo();

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
};
