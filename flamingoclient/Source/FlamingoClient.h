#pragma once

#include "UserMgr.h"
#include "UserConfig.h"
#include "CheckNetworkStatusTask.h"
#include "SendMsgThread.h"
#include "RecvMsgThread.h"
#include "FileTaskThread.h"
#include "ImageTaskThread.h"
#include "net/IUSocket.h"

class CFriendStatus;
class CUpdateDlg;

class CFlamingoClient
{
public:
    static CFlamingoClient& GetInstance();

public:
	CFlamingoClient(void);
	~CFlamingoClient(void);

public:
	bool InitProxyWnd();										// 初始化代理窗口
    bool InitNetThreads();                                      // 初始化网络线程
	void Uninit();												// 反初始化客户端

	void SetServer(PCTSTR pszServer);
    void SetFileServer(PCTSTR pszServer);
    void SetImgServer(PCTSTR pszServer);
    void SetPort(short port);
	void SetFilePort(short port);
    void SetImgPort(short port);

	void SetUser(LPCTSTR lpUserAccount, LPCTSTR lpUserPwd);		// 设置UTalk号码和密码
	void SetLoginStatus(long nStatus);							// 设置登录状态
	void SetCallBackWnd(HWND hCallBackWnd);						// 设置回调窗口句柄
	void SetRegisterWindow(HWND hwndRegister);					// 设置注册结果的反馈窗口
	void SetModifyPasswordWindow(HWND hwndModifyPassword);		// 设置修改密码结果反馈窗口
	void SetCreateNewGroupWindow(HWND hwndCreateNewGroup);		// 设置创建群组结果反馈窗口
	void SetFindFriendWindow(HWND hwndFindFriend);				// 设置查找用户结果反馈窗口

	void StartCheckNetworkStatusTask();							
	//void StartGetUserInfoTask(long nType);					//获取好友
	void StartHeartbeatTask();

	void Register(PCTSTR pszAccountName, PCTSTR pszNickName, PCTSTR pszPassword);
    void Login(int nStatus = STATUS_ONLINE);				    // 登录
	BOOL Logout();												// 注销
	void CancelLogin();											// 取消登录
    void GetFriendList();                                       // 获取好友列表
    void GetGroupMembers(int32_t groupid);                      // 获取群成员
    void ChangeStatus(int32_t nNewStatus);                      // 更改自己的登录状态        

	BOOL FindFriend(PCTSTR pszAccountName, long nType, HWND hReflectionWnd);                // 查找好友
	BOOL AddFriend(UINT uAccountToAdd);
	void ResponseAddFriendApply(UINT uAccountID, UINT uCmd);	                            //回应加好友请求任务
	BOOL DeleteFriend(UINT uAccountID);							                            // 删除好友
        
    bool AddNewTeam(PCTSTR pszNewTeamName);                                                 //添加新分组
    bool DeleteTeam(PCTSTR pszOldTeamName);                                                 //删除分组
    bool ModifyTeamName(PCTSTR pszNewTeamName, PCTSTR pszOldTeamName);                      //修改分组名称
    bool MoveFriendToOtherTeam(UINT uUserID, PCTSTR pszOldTeamName, PCTSTR pszNewTeamName); //移动好友至其他分组

	BOOL UpdateLogonUserInfo(PCTSTR pszNickName, 
							 PCTSTR pszSignature,
							 UINT uGender,
							 long nBirthday,
							 PCTSTR pszAddress,
							 PCTSTR pszPhone,
							 PCTSTR pszMail,
							 UINT uSysFaceID,
							 PCTSTR pszCustomFacePath,
							 BOOL bUseCustomThumb);

	void SendHeartbeatMessage();
	void ModifyPassword(PCTSTR pszOldPassword, PCTSTR pszNewPassword);
	void CreateNewGroup(PCTSTR pszGroupName);
    void ModifyFriendMarkName(UINT friendID, PCTSTR pszNewMarkName);
	void ChangeStatus(long nStatus);							// 改变在线状态
	void UpdateBuddyList();										// 更新好友列表
	void UpdateGroupList();										// 更新群列表
	void UpdateRecentList();									// 更新最近联系人列表
	void UpdateBuddyInfo(UINT nUTalkUin);						// 更新好友信息
	void UpdateGroupMemberInfo(UINT nGroupCode, UINT nUTalkUin);// 更新群成员信息
	void UpdateGroupInfo(UINT nGroupCode);						// 更新群信息
	void UpdateBuddyNum(UINT nUTalkUin);						// 更新好友号码
	void UpdateGroupMemberNum(UINT nGroupCode, UINT nUTalkUin);	// 更新群成员号码
	void UpdateGroupMemberNum(UINT nGroupCode, std::vector<UINT>* arrUTalkUin);	// 更新群成员号码
	void UpdateGroupNum(UINT nGroupCode);											// 更新群号码
	void UpdateBuddySign(UINT nUTalkUin);											// 更新好友个性签名
	void UpdateGroupMemberSign(UINT nGroupCode, UINT nUTalkUin);					// 更新群成员个性签名
	void ModifyUTalkSign(LPCTSTR lpSign);											// 修改UTalk个性签名
	void UpdateBuddyHeadPic(UINT nUTalkUin, UINT nUTalkNum);						// 更新好友头像
	void UpdateGroupMemberHeadPic(UINT nGroupCode, UINT nUTalkUin, UINT nUTalkNum);	// 更新群成员头像
	void UpdateGroupHeadPic(UINT nGroupCode, UINT nGroupNum);						// 更新群头像
	void UpdateGroupFaceSignal();													// 更新群表情信令


    BOOL SendBuddyMsg(UINT nFromUin, const tstring& strFromNickName, UINT nToUin, const tstring& strToNickName, time_t nTime, const tstring& strChatMsg, HWND hwndFrom = NULL);// 发送好友消息
	BOOL SendGroupMsg(UINT nGroupId, time_t nTime, LPCTSTR lpMsg, HWND hwndFrom);	// 发送群消息
	BOOL SendSessMsg(UINT nGroupId, UINT nToUin, time_t nTime, LPCTSTR lpMsg);		// 发送临时会话消息
	BOOL SendMultiChatMsg(const std::set<UINT> setAccountID, time_t nTime, LPCTSTR lpMsg, HWND hwndFrom=NULL);//群发消息

	BOOL IsOffline();											// 是否离线状态


	long GetStatus();											// 获取在线状态
	BOOL GetVerifyCodePic(const BYTE*& lpData, DWORD& dwSize);	// 获取验证码图片
	void SetBuddyListAvailable(BOOL bAvailable);
	BOOL IsBuddyListAvailable();

	CBuddyInfo* GetUserInfo(UINT uAccountID=0);			// 获取用户信息
	CBuddyList* GetBuddyList();						// 获取好友列表
	CGroupList* GetGroupList();						// 获取群列表
	CRecentList* GetRecentList();						// 获取最近联系人列表
	CMessageList* GetMessageList();					// 获取消息列表
	CMessageLogger* GetMsgLogger();					// 获取消息记录管理器

	tstring GetUserFolder();							// 获取用户文件夹存放路径

	tstring GetPersonalFolder(UINT nUserNum = 0);		// 获取个人文件夹存放路径
	tstring GetChatPicFolder(UINT nUserNum = 0);		// 获取聊天图片存放路径

	tstring GetUserHeadPicFullName(UINT nUserNum = 0);	// 获取用户头像图片全路径文件名
	tstring GetBuddyHeadPicFullName(UINT nUTalkNum);	// 获取好友头像图片全路径文件名
	tstring GetGroupHeadPicFullName(UINT nGroupNum);	// 获取群头像图片全路径文件名
	tstring GetSessHeadPicFullName(UINT nUTalkNum);		// 获取群成员头像图片全路径文件名
	tstring GetChatPicFullName(LPCTSTR lpszFileName);	// 获取聊天图片全路径文件名
	tstring GetMsgLogFullName(UINT nUserNum = 0);		// 获取消息记录全路径文件名

	BOOL IsNeedUpdateBuddyHeadPic(UINT nUTalkNum);		// 判断是否需要更新好友头像
	BOOL IsNeedUpdateGroupHeadPic(UINT nGroupNum);		// 判断是否需要更新群头像
	BOOL IsNeedUpdateSessHeadPic(UINT nUTalkNum);		// 判断是否需要更新群成员头像

	void RequestServerTime();							// 获取服务器时间
	time_t GetCurrentTime();							// 获取当前时间(以服务器时间为基准)
	void LoadUserConfig();								// 加载用户设置信息
	void SaveUserConfig();								// 保存用户设置信息

	void GoOnline();									
	void GoOffline();									// 掉线或者下线

	long ParseBuddyStatus(long nFlag);					// 解析用户在线状态
	void CacheBuddyStatus();							// 缓存用户在线状态
	BOOL SetBuddyStatus(UINT uAccountID, long nStatus);
    BOOL SetBuddyClientType(UINT uAccountID, long nNewClientType);

private:
	void OnHeartbeatResult(UINT message, WPARAM wParam, LPARAM lParam);
	void OnNetworkStatusChange(UINT message, WPARAM wParam, LPARAM lParam);
	void OnRegisterResult(UINT message, WPARAM wParam, LPARAM lParam);
	void OnLoginResult(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUpdateUserBasicInfo(UINT message, WPARAM wParam, LPARAM lParam);
    void OnUpdateGroupBasicInfo(UINT message, WPARAM wParam, LPARAM lParam);
	void OnModifyInfoResult(UINT message, WPARAM wParam, LPARAM lParam);
	void OnRecvUserStatusChangeData(UINT message, WPARAM wParam, LPARAM lParam);
	void OnRecvAddFriendRequest(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUserStatusChange(UINT message, WPARAM wParam, LPARAM lParam);
    void OnSendConfirmMessage(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUpdateChatMsgID(UINT message, WPARAM wParam, LPARAM lParam);
	void OnFindFriend(UINT message, WPARAM wParam, LPARAM lParam);
	void OnBuddyCustomFaceAvailable(UINT message, WPARAM wParam, LPARAM lParam);
	void OnModifyPasswordResult(UINT message, WPARAM wParam, LPARAM lParam);
	void OnCreateNewGroupResult(UINT message, WPARAM wParam, LPARAM lParam);	
	void OnDeleteFriendResult(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUpdateBuddyList(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUpdateGroupList(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUpdateRecentList(UINT message, WPARAM wParam, LPARAM lParam);
	void OnBuddyMsg(UINT message, WPARAM wParam, LPARAM lParam);
	void OnGroupMsg(UINT message, WPARAM wParam, LPARAM lParam);
	void OnSessMsg(UINT message, WPARAM wParam, LPARAM lParam);
	void OnSysGroupMsg(UINT message, WPARAM wParam, LPARAM lParam);
	void OnStatusChangeMsg(UINT message, WPARAM wParam, LPARAM lParam);
	void OnKickMsg(UINT message, WPARAM wParam, LPARAM lParam);
    void OnScreenshotMsg(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUpdateBuddyNumber(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUpdateGMemberNumber(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUpdateGroupNumber(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUpdateBuddySign(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUpdateGMemberSign(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUpdateBuddyInfo(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUpdateGMemberInfo(UINT message, WPARAM wParam, LPARAM lParam);
	void OnUpdateGroupInfo(UINT message, WPARAM wParam, LPARAM lParam);
	//void OnUpdateC2CMsgSig(UINT message, WPARAM wParam, LPARAM lParam);
	void OnChangeStatusResult(UINT message, WPARAM wParam, LPARAM lParam);
	void OnTargetInfoChange(UINT message, WPARAM wParam, LPARAM lParam);

	void OnInternal_GetBuddyData(UINT message, WPARAM wParam, LPARAM lParam);
	void OnInternal_GetGroupData(UINT message, WPARAM wParam, LPARAM lParam);
	void OnInternal_GetGMemberData(UINT message, WPARAM wParam, LPARAM lParam);
	UINT OnInternal_GroupId2Code(UINT message, WPARAM wParam, LPARAM lParam);

	BOOL CreateProxyWnd();		// 创建代理窗口
	BOOL DestroyProxyWnd();		// 销毁代理窗口
	static LRESULT CALLBACK ProxyWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

public:
	CUserMgr						m_UserMgr;
	CCheckNetworkStatusTask			m_CheckNetworkStatusTask;

    CSendMsgThread					m_SendMsgThread;
    CRecvMsgThread                  m_RecvMsgThread;
    CFileTaskThread					m_FileTask;
    CImageTaskThread                m_ImageTask;
    
	CUserConfig						m_UserConfig;

	std::vector<AddFriendInfo*>		m_aryAddFriendInfo;

private:
	time_t							m_ServerTime;			//服务器时间
	DWORD							m_StartTime;			//开始计时的时间

	BOOL							m_bNetworkAvailable;	//网络是否可用

	HWND							m_hwndRegister;			//注册窗口
	HWND							m_hwndFindFriend;		//查找好友窗口
	HWND							m_hwndModifyPassword;	//修改密码窗口
	HWND							m_hwndCreateNewGroup;	//创建群组窗口

	BOOL							m_bBuddyIDsAvailable;	//用户好友ID是否可用
	BOOL							m_bBuddyListAvailable;	//用户好友列表信息是否可用

	std::map<UINT, long>			m_mapUserStatusCache;	//好友在线状态缓存：key是账户ID，value是状态码
	std::map<UINT, UINT>			m_mapAddFriendCache;	//加好友操作缓存: key是账户ID，value是操作码

	long							m_nGroupCount;
	BOOL							m_bGroupInfoAvailable;
	BOOL							m_bGroupMemberInfoAvailable;
};
