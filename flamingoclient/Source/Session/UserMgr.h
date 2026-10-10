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
	//本人所属企业的文件共享开关：登录成功时由 CFlamingoClient::OnLoginResult 按登录应答的 features.fileshare 写入，
	//主面板随后交给文件共享宿主（CFileShareHost::SetFeatureEnabled）；运行中的变化走 1013，不改这里
	bool			m_bFileShareFeature;
	//本人所属企业的「邀请入群需被邀请人同意」开关：登录成功时按登录应答的 features.inviteconfirm 写入，运行中由主面板
	//收到 1013 推送时更新；邀请对话框打开时读取，决定说明行是否提示「对方同意后才会入群」
	bool			m_bInviteConfirmFeature;
	//本人所属企业是否打开了群语音 / 群视频通话（登录应答的 features.groupcall），收到 1013 推送时更新；
	//群聊窗据此决定「语音通话」「视频通话」按钮显隐
	bool			m_bGroupCallFeature;
	//本人所属企业是否打开了语音字幕（登录应答的 features.caption）：登录成功时写入，主面板随后交给字幕宿主
	//（CCaptionHost::OnLoginSucceeded）；运行中的变化走 1013，由主面板直接交给字幕宿主并同步这里
	bool			m_bCaptionFeature;
	//本人所属企业是否开通了云端录制（登录应答的 features.cloudrecord，收到 1013 推送时更新）：FlamingoMedia 经
	//IMediaHost::IsCloudRecordEnabled 读取，决定共享控制条与录制选择窗有没有云端录制的入口
	bool			m_bCloudRecordFeature;
	//本人所属企业是否打开了单聊密聊（登录应答的 features.secretchat）：登录成功时写入，主面板随后交给密聊宿主；
	//运行中的变化走 1013，由主面板交给密聊宿主并同步这里
	bool			m_bSecretChatFeature;
	//本人所属企业是否打开了帖子群（登录应答的 features.threadgroup）：登录成功时写入，主面板随后交给帖子群宿主；
	//关闭时帖子群在群列表、会话列表、搜索与转发里全部隐藏（docs/帖子群方案.md 4.7）
	bool			m_bThreadGroupFeature;
	//本人所属企业是否打开了机器人（登录应答的 features.bot，收到 1013 推送时更新）：关闭时不加载 FlamingoBot.dll，
	//群聊窗不显示机器人分组与管理入口（docs/机器人方案.md §4.1）
	bool			m_bBotFeature;
	//是否允许群主添加群机器人（features.botgroup）
	bool			m_bBotGroupFeature;
	//是否允许员工配置个人机器人（features.botpersonal）；关闭时「我的机器人」入口隐藏
	bool			m_bBotPersonalFeature;
	//是否允许群主给群机器人配置外部服务（features.botgroupexternal）
	bool			m_bBotGroupExternalFeature;

	CMessageList	m_MsgList;
	CMessageLogger	m_MsgLogger;

	CGroupList		m_GroupList;
	CRecentList		m_RecentList;

private:
};
