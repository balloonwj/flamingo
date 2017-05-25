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

};
