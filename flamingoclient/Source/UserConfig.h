#pragma once

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
	void SetFontName(PCTSTR pszFontName);
	void SetFontSize(long nSize);
	void SetFontColor(long lColor);
	void EnableFontBold(BOOL bBold);
	void EnableFontItalic(BOOL bItalic);
	void EnableFontUnderline(BOOL bUnderline);

	BOOL IsEnablePressEnterToSend() const;
	PCTSTR GetFontName() const;
	long GetFontSize() const;
	long GetFontColor() const;
	BOOL IsEnableFontBold() const;
	BOOL IsEnableFontItalic() const;
	BOOL IsEnableFontUnderline() const;

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
	void SetUiFontScaleIndex(long nIndex);			//字体大小档位索引（0小/1中/2大，本期仅存、暂不生效）
	long GetUiFontScaleIndex() const;
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
	// 聊天页
	void EnableAutoRecvImage(BOOL bEnable);			//自动接收图片
	BOOL IsEnableAutoRecvImage() const;
	void EnableAutoRecvFile(BOOL bEnable);			//自动接收文件
	BOOL IsEnableAutoRecvFile() const;
	void EnableMsgRoaming(BOOL bEnable);			//消息漫游（本期仅存、暂不联服务端）
	BOOL IsEnableMsgRoaming() const;
	void EnableEmojiRain(BOOL bEnable);				//开启表情雨特效
	BOOL IsEnableEmojiRain() const;
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
	BOOL	m_bTrayMinimizeTipShown;				//"已最小化到托盘"气泡提示是否已弹过（每账号一次，随 .cfg 持久化，一经置位不再复位）

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
	CString	m_strFontName;
	long	m_nFontSize;
	long	m_lFontColor;
	BOOL	m_bFontBold;
	BOOL	m_bFontItalic;
	BOOL	m_bFontUnderline;

	BOOL	m_bShowLastMsg;

	// ---- 系统设置（新版 balloonui 系统设置对话框）新增项，持久化在 ini 的 [Settings] 节 ----
	long	m_nLanguageIndex;		//语言下拉索引（0=简体中文）；本期仅存、暂不真正切换
	long	m_nUiFontScaleIndex;	//字体大小档位索引（0小/1中/2大）；本期仅存、暂不生效
	BOOL	m_bNewMsgNotify;		//新消息提醒总开关
	BOOL	m_bDesktopPopup;		//新消息桌面气泡弹窗
	BOOL	m_bShakeFlashIcon;		//震动 / 闪烁托盘图标提醒
	BOOL	m_bDoNotDisturb;		//勿扰时段（开启后时段内不提醒）；本期仅存开关、时段固定文案
	BOOL	m_bNotifyAfterLock;		//锁屏后是否继续通知
	BOOL	m_bAutoRecvImage;		//聊天自动接收图片
	BOOL	m_bAutoRecvFile;		//聊天自动接收文件
	BOOL	m_bMsgRoaming;			//消息漫游（多设备同步）；本期仅存、暂不联服务端
	BOOL	m_bEmojiRain;			//开启表情雨特效
	long	m_nAudioInputIndex;		//麦克风设备下拉索引；本期仅存、A/V 未接入暂不生效
	long	m_nAudioOutputIndex;	//扬声器设备下拉索引；同上
	long	m_nCameraIndex;			//摄像头设备下拉索引；同上
	BOOL	m_bEchoCancel;			//回声消除；同上
	BOOL	m_bTwoFactor;			//二次验证开关；本期仅存、服务端未接入
	CString	m_strRecvFilePath;		//接收文件保存目录；空=用默认 我的文档\Flamingo\FileRecv\账号

};
