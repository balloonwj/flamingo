#pragma once

#include "ChatFontPrefs.h"		// chatfont::ChatFontPrefs：聊天字体（全局默认 + 各会话自定义）
#include "WindowPlacementPrefs.h"	// winplace::WindowPlacementPrefs：窗口位置与大小

enum NAME_STYLE
{
	NAME_STYLE_SHOW_NICKNAME_AND_ACCOUNT,		//显示昵称和账号
	NAME_STYLE_SHOW_NICKNAME,					//显示昵称
	NAME_STYLE_SHOW_ACCOUNT						//显示账号
};

class CUserConfig
{
public:
	CUserConfig();
	~CUserConfig();

	BOOL LoadConfig(PCTSTR pszFilePath);
	BOOL SaveConfig(PCTSTR pszFilePath);

	void EnableMute(BOOL bMute);
	void EnableDestroyAfterRead(BOOL bEnable);
	void EnableAutoReply(BOOL bAutoReply);
	void SetAutoReplyContent(PCTSTR pszAutoReplyContent);
	void EnableRevokeChatMsg(BOOL bEnable);

	BOOL IsEnableMute() const;
	BOOL IsEnableDestroyAfterRead() const;
	BOOL IsEnableAutoReply() const;
	PCTSTR  GetAutoReplyContent() const;
	BOOL IsEnableRevokeChatMsg() const;

	void SetMainDlgX(long x);
	void SetMainDlgY(long y);
	void SetMainDlgWidth(long nWidth);
	void SetMainDlgHeight(long nHeight);

	void EnableExitPrompt(BOOL bEnable);				
	BOOL IsEnableExitPrompt();

	void EnableExitWhenCloseMainDlg(BOOL bEnable);
	BOOL IsEnableExitWhenCloseMainDlg();

	// 记录"主面板最小化到托盘"的气泡提示是否已经给该账号弹过。
	// 主面板隐藏到托盘后既不在任务栏、也不在 Alt+Tab 里，首次发生时弹一次
	// 气泡告诉用户去哪儿找它；弹过一次即置位并落盘，之后不再打扰。
	//   bShown：TRUE=已提示过（不再弹），FALSE=尚未提示过。
	void SetTrayMinimizeTipShown(BOOL bShown);
	BOOL IsTrayMinimizeTipShown() const;

	void SetBuddyListHeadPicStyle(long nStyle);
	void EnableBuddyListShowBigHeadPicInSel(BOOL bEnable);

	void SetNameStyle(long nStyle);
	long GetNameStyle();

	void EnableSimpleProfile(BOOL bEnable);
	BOOL IsEnableSimpleProfile();

	long GetMainDlgX();
	long GetMainDlgY();
	long GetMainDlgWidth() const;
	long GetMainDlgHeight() const;

	long GetBuddyListHeadPicStyle() const;
	BOOL IsEnableBuddyListShowBigHeadPicInSel() const;

	
	void SetChatDlgWidth(long nWidth);
	void SetChatDlgHeight(long nHeight);

	long GetChatDlgWidth() const;
	long GetChatDlgHeight() const;

	void SetGroupDlgWidth(long nWidth);
	void SetGroupDlgHeight(long nHeight);

	long GetGroupDlgWidth() const;
	long GetGroupDlgHeight() const;
	
	void EnablePressEnterToSend(BOOL bEnable);
	BOOL IsEnablePressEnterToSend() const;

	// 聊天字体偏好：全局默认（系统设置「聊天」页）+ 各会话自定义（聊天窗「AF」浮层），
	// 取用规则见 ChatFontPrefs.h。随 LoadConfig / SaveConfig 读写，改动后由调用方负责
	// 调 CFlamingoClient::SaveUserConfig 存盘。返回的引用与本对象同生命周期。
	chatfont::ChatFontPrefs& ChatFonts();
	const chatfont::ChatFontPrefs& ChatFonts() const;

	// 消息记录窗口上次关闭时的位置与大小（配置里的 [HistoryDlg] 一节）。没有保存过时
	// m_hasSaved 为 false。打开窗口时怎么据此算出实际位置见 winplace::ResolveRestoredWindowRect。
	const winplace::WindowPlacementPrefs& GetHistoryDlgPlacement() const;
	// 记下消息记录窗口的位置与大小。只改内存里的值，由调用方负责调
	// CFlamingoClient::SaveUserConfig 存盘。
	void SetHistoryDlgPlacement(const winplace::WindowPlacementPrefs& placement);

	void SetFaceID(UINT uFaceID);
	UINT GetFaceID() const;

	void SetCustomFace(PCTSTR pszCustomFace);
	PCTSTR GetCustomFace() const;

	void EnableShowLastMsgInChatDlg(BOOL bEnable);
	BOOL IsEnableShowLastMsgInChatDlg();

	// ---- 系统设置（新版 balloonui 系统设置对话框）新增项 ----
	// 通用页
	void SetLanguageIndex(long nIndex);				//语言下拉索引（本期仅存、暂不真正切换）
	long GetLanguageIndex() const;
	// 消息通知页
	void EnableNewMsgNotify(BOOL bEnable);			//新消息提醒总开关
	BOOL IsEnableNewMsgNotify() const;
	void EnableDesktopPopup(BOOL bEnable);			//新消息桌面气泡弹窗
	BOOL IsEnableDesktopPopup() const;
	void EnableShakeFlashIcon(BOOL bEnable);		//震动 / 闪烁托盘图标
	BOOL IsEnableShakeFlashIcon() const;
	void EnableDoNotDisturb(BOOL bEnable);			//勿扰时段开关（本期仅存、时段固定文案）
	BOOL IsEnableDoNotDisturb() const;
	void EnableNotifyAfterLock(BOOL bEnable);		//锁屏后是否继续通知
	BOOL IsEnableNotifyAfterLock() const;
	// 通用页：无操作自动切到"离开"。关掉后本功能完全停用（不再自动切离开；
	// 若此刻正处于自动设上的离开，会被切回在线）。默认开。
	void EnableAutoAway(BOOL bEnable);
	BOOL IsEnableAutoAway() const;
	// 自动离开的等待时长，存<u>下拉索引</u>而非分钟数（与语言 / 字体大小等下拉项同口径，
	// 索引 → 分钟数的映射在 BalloonSettings/SettingsBinding.h）。默认 0，即 5 分钟。
	void SetAutoAwayDelayIndex(long nIndex);
	long GetAutoAwayDelayIndex() const;
	// 通用页：禁止机器休眠。开启后本机不休眠、不关闭显示器、也不锁屏（系统电源请求 +
	// 周期性合成输入事件，见 BalloonMain/KeepAwakeSession.h）。默认关闭 —— 该功能用于
	// 演示、长时间传输文件一类的临时场景，不应长期开启。它与"自动离开"互斥：开启期间
	// 自动离开不生效（合成输入事件会把全系统空闲计时器重置为 0，而自动离开正是依据该
	// 计时器判定空闲）。
	void EnableKeepAwake(BOOL bEnable);
	BOOL IsEnableKeepAwake() const;
	// 聊天页
	void EnableAutoRecvImage(BOOL bEnable);			//自动接收图片
	BOOL IsEnableAutoRecvImage() const;
	void EnableAutoRecvFile(BOOL bEnable);			//自动接收文件
	BOOL IsEnableAutoRecvFile() const;
	void EnableMsgRoaming(BOOL bEnable);			//消息漫游（本期仅存、暂不联服务端）
	BOOL IsEnableMsgRoaming() const;
	void EnableLinkDetect(BOOL bEnable);			//自动识别聊天内容里的链接（默认开）
	BOOL IsEnableLinkDetect() const;
	// 音视频页（本期仅存索引/开关，A/V 子系统未接入，暂不真正生效）
	void SetAudioInputIndex(long nIndex);			//麦克风设备下拉索引
	long GetAudioInputIndex() const;
	void SetAudioOutputIndex(long nIndex);			//扬声器设备下拉索引
	long GetAudioOutputIndex() const;
	void SetCameraIndex(long nIndex);				//摄像头设备下拉索引
	long GetCameraIndex() const;
	void EnableEchoCancel(BOOL bEnable);			//回声消除
	BOOL IsEnableEchoCancel() const;
	// 账号安全页
	void EnableTwoFactor(BOOL bEnable);				//二次验证（本期仅存，服务端未接入）
	BOOL IsEnableTwoFactor() const;
	// 存储页
	void SetRecvFilePath(PCTSTR pszPath);			//接收文件保存目录（空=用默认 我的文档\Flamingo\FileRecv\账号）
	PCTSTR GetRecvFilePath() const;


private:
	BOOL	m_bMute;				//是否关闭所有声音
	BOOL    m_bDestroyAfterRead;	//是否阅后即焚
	BOOL	m_bAutoReply;			//是否自动回复
	TCHAR	m_szAutoReplyContent[256];	//自动回复内容
	BOOL	m_bEnableRevokeChatMsg; //是否启用消息撤回

	//主对话框
	long	m_xMainDlg;
	long	m_yMainDlg;
	long	m_cxMainDlg;
	long	m_cyMainDlg;

	BOOL    m_bEnableExitPrompt;					//退出是否提示
	BOOL	m_bExitWhenCloseMainDlg;				//关闭主对话框时退出程序（还是最小化到托盘）
	BOOL	m_bTrayMinimizeTipShown;				//"已最小化到托盘"气泡提示是否已弹过（每账号一次，随 .ini 持久化，一经置位不再复位）

	long	m_nBuddyListHeadPicStyle;				//好友列表中头像风格（大头像、小头像、标准头像）
	BOOL	m_bBuddyListShowBigHeadPicInSel;		//当显示模式是小头像时，选中某个头像是否显示大头像

	long	m_nNameStyleIndex;						//0显示昵称和账号、1显示昵称、2显示账号
	BOOL	m_bShowSimpleProfile;					//是否显示清爽资料
	
	UINT	m_uFaceID;								//当前用户头像ID
	TCHAR   m_szCustomFace[MAX_PATH];


	//聊天对话框
	long	m_cxChatDlg;
	long	m_cyChatDlg;
	long	m_cxGroupDlg;
	long	m_cyGroupDlg;
	BOOL	m_bPressEnterToSend;
	//聊天字体偏好（全局默认 + 各会话自定义），存在 [ChatDlg] 的 DefaultChatFont 键与
	//[ChatFontOverride] 段；LoadConfig 时整体重建
	chatfont::ChatFontPrefs	m_chatFontPrefs;
	//消息记录窗口的位置与大小，存在 [HistoryDlg] 段；LoadConfig 时整体重建，窗口关闭时更新
	winplace::WindowPlacementPrefs	m_historyDlgPlacement;

	BOOL	m_bShowLastMsg;

	// ---- 系统设置（新版 balloonui 系统设置对话框）新增项，持久化在 ini 的 [Settings] 节 ----
	long	m_nLanguageIndex;		//语言下拉索引（0=简体中文）；本期仅存、暂不真正切换
	BOOL	m_bNewMsgNotify;		//新消息提醒总开关
	BOOL	m_bDesktopPopup;		//新消息桌面气泡弹窗
	BOOL	m_bShakeFlashIcon;		//震动 / 闪烁托盘图标提醒
	BOOL	m_bDoNotDisturb;		//勿扰时段（开启后时段内不提醒）；本期仅存开关、时段固定文案
	BOOL	m_bNotifyAfterLock;		//锁屏后是否继续通知
	BOOL	m_bAutoAway;			//电脑无操作时是否自动切到"离开"（默认开）
	long	m_nAutoAwayDelayIndex;	//自动离开的等待时长下拉索引（0=5分钟 1=10 2=15 3=30），映射见 SettingsBinding.h
	BOOL	m_bKeepAwake;			//禁止机器休眠（不休眠 / 不关闭显示器 / 不锁屏）；默认关闭，与自动离开互斥
	BOOL	m_bAutoRecvImage;		//聊天自动接收图片
	BOOL	m_bAutoRecvFile;		//聊天自动接收文件
	BOOL	m_bMsgRoaming;			//消息漫游（多设备同步）；本期仅存、暂不联服务端
	BOOL	m_bLinkDetect;			//自动识别聊天内容里的链接（关掉后链接以普通文字显示）
	long	m_nAudioInputIndex;		//麦克风设备下拉索引；本期仅存、A/V 未接入暂不生效
	long	m_nAudioOutputIndex;	//扬声器设备下拉索引；同上
	long	m_nCameraIndex;			//摄像头设备下拉索引；同上
	BOOL	m_bEchoCancel;			//回声消除；同上
	BOOL	m_bTwoFactor;			//二次验证开关；本期仅存、服务端未接入
	CString	m_strRecvFilePath;		//接收文件保存目录；空=用默认 我的文档\Flamingo\FileRecv\账号

};
