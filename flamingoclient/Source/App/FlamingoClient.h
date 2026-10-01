#pragma once

#include "..\Session\UserMgr.h"
#include "..\Session\UserConfig.h"
#include "..\Threading\CheckNetworkStatusTask.h"
#include "..\Threading\SendMsgThread.h"
#include "..\Threading\FileTaskThread.h"
#include "..\Threading\ImageTaskThread.h"
#include "../net/IUSocket.h"

class CFriendStatus;

class CFlamingoClient
{
public:
    /**
     *  登记「进程内正在使用的那一个 CFlamingoClient」，供 GetInstance 取用。
     *
     *  存在理由：登录信息写进的是进程外壳的成员对象 m_FMGClient，各窗口持有的
     *  m_lpFMGClient 指针也都指向它。GetInstance 必须返回同一个对象，"写登录态"与
     *  "读登录态"才是同一份数据（2026-08-16 之前 GetInstance 返回的是一个谁都没写过
     *  的函数内静态对象，详见 GetInstance 的实现注释）。
     *
     *    lpClient：要登记的实例地址；传 NULL 表示注销。<u>本类不接管所有权</u> ——
     *              对象的生命周期仍由登记者（balloonapp::CAppShell）负责，登记者析构前必须传 NULL
     *              注销，否则 GetInstance 会返回一个已销毁对象的地址。
     *
     *  副作用：覆盖一个已登记的不同实例时记一条 LOG_ERROR（进程内应当只有一个）。
     */
    static void SetInstance(CFlamingoClient* lpClient);

    /**
     *  取进程内正在使用的那一个 CFlamingoClient。
     *
     *  @return 已登记时返回该实例的地址（非空，所有权不转移，调用方不得 delete）；
     *          尚未登记或已注销时返回 NULL —— 此时调用方须自行降级（典型做法是当作
     *          "没有登录态"处理），不要解引用。
     *
     *  副作用：未登记时记一条 LOG_ERROR（每进程只记首次，避免高频调用点刷屏）。
     */
    static CFlamingoClient* GetInstance();

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

	void StartCheckNetworkStatusTask();
	//void StartGetUserInfoTask(long nType);					//获取好友
	void StartHeartbeatTask();

    // 登录：把一个登录请求放进发送线程的队列，结果经 FMG_MSG_LOGIN_RESULT 异步投回。
    // 返回 true 表示请求已入队（之后一定会投回一个结果）；前置条件不满足（不在离线态、
    // 缺账号或密码）而没有入队时返回 false，此时不会有结果投回。断线重连的节流据此判断
    // 是否进入"等结果"状态，见 UI/BalloonMain/ReconnectThrottle.h。
    bool Login(int nStatus = STATUS_ONLINE);
	BOOL Logout();												// 注销
	void CancelLogin();											// 取消登录

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
	void UpdateBuddyList();										// 更新好友列表
	void UpdateGroupList();										// 更新群列表
	void UpdateRecentList();									// 更新最近联系人列表
	void UpdateBuddyInfo(UINT nUTalkUin);						// 更新好友信息
	void UpdateGroupInfo(UINT nGroupCode);						// 更新群信息
	void UpdateBuddyNum(UINT nUTalkUin);						// 更新好友号码
	void UpdateGroupMemberNum(UINT nGroupCode, UINT nUTalkUin);	// 更新群成员号码
	void UpdateGroupMemberNum(UINT nGroupCode, std::vector<UINT>* arrUTalkUin);	// 更新群成员号码
	void UpdateGroupNum(UINT nGroupCode);											// 更新群号码
	void UpdateBuddySign(UINT nUTalkUin);											// 更新好友个性签名
	void UpdateGroupMemberSign(UINT nGroupCode, UINT nUTalkUin);					// 更新群成员个性签名
	void UpdateBuddyHeadPic(UINT nUTalkUin, UINT nUTalkNum);						// 更新好友头像
	void UpdateGroupMemberHeadPic(UINT nGroupCode, UINT nUTalkUin, UINT nUTalkNum);	// 更新群成员头像
	void UpdateGroupHeadPic(UINT nGroupCode, UINT nGroupNum);						// 更新群头像
	void UpdateGroupFaceSignal();													// 更新群表情信令


	BOOL SendSessMsg(UINT nGroupId, UINT nToUin, time_t nTime, LPCTSTR lpMsg);		// 发送临时会话消息

	BOOL IsOffline();											// 是否离线状态


	long GetStatus();											// 获取在线状态
	BOOL GetVerifyCodePic(const BYTE*& lpData, DWORD& dwSize);	// 获取验证码图片

	CBuddyInfo* GetUserInfo(UINT uAccountID=0);			// 获取用户信息
	CGroupList* GetGroupList();						// 获取群列表
	CMessageList* GetMessageList();					// 获取消息列表

	tstring GetUserFolder();							// 获取用户文件夹存放路径

	tstring GetPersonalFolder(UINT nUserNum = 0);		// 获取个人文件夹存放路径
	tstring GetChatPicFolder(UINT nUserNum = 0);		// 获取聊天图片存放路径

	tstring GetSessHeadPicFullName(UINT nUTalkNum);		// 获取群成员头像图片全路径文件名
	tstring GetChatPicFullName(LPCTSTR lpszFileName);	// 获取聊天图片全路径文件名
	tstring GetMsgLogFullName(UINT nUserNum = 0);		// 获取消息记录全路径文件名


	void RequestServerTime();							// 获取服务器时间
	time_t GetCurrentTime();							// 获取当前时间(以服务器时间为基准)
	void LoadUserConfig();								// 加载用户设置信息
	void SaveUserConfig();								// 保存用户设置信息

	void GoOnline();									
	void GoOffline();									// 掉线或者下线

	void CacheBuddyStatus();							// 缓存用户在线状态

private:
	void OnNetworkStatusChange(UINT message, WPARAM wParam, LPARAM lParam);
	void OnLoginResult(UINT message, WPARAM wParam, LPARAM lParam);
	//void OnUpdateC2CMsgSig(UINT message, WPARAM wParam, LPARAM lParam);


	BOOL CreateProxyWnd();		// 创建代理窗口
	BOOL DestroyProxyWnd();		// 销毁代理窗口
	static LRESULT CALLBACK ProxyWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

public:
	CUserMgr						m_UserMgr;
	CCheckNetworkStatusTask			m_CheckNetworkStatusTask;

    CSendMsgThread					m_SendMsgThread;
    CFileTaskThread					m_FileTask;
    CImageTaskThread                m_ImageTask;
    
	CUserConfig						m_UserConfig;

private:
	time_t							m_ServerTime;			//服务器时间
	DWORD							m_StartTime;			//开始计时的时间

	BOOL							m_bNetworkAvailable;	//网络是否可用



	std::map<UINT, long>			m_mapUserStatusCache;	//好友在线状态缓存：key是账户ID，value是状态码


	//上一次 Login() 被前置条件挡下时的原因掩码（取值见 FlamingoClient.cpp 里的
	//kLoginSkipBit* 三个常量），-1 表示"尚未记录过 / 上一次是正常放行的"。
	//用途只有一个：给日志去重。断线重连定时器每 5 秒调一次 Login()，若每次都记一条，
	//几分钟就把日志刷满；只在掩码变化时记，既不会漏掉第一次，原因变了也一定看得见。
	//生命周期与本对象相同，只在 UI 线程的 Login() 里读写，无须加锁。
	int								m_nLastLoginSkipMask;
};
