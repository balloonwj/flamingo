#include "stdafx.h"
#include "UserConfig.h"
#include "IniFile.h"

CUserConfig::CUserConfig() : m_strFontName(_T("微软雅黑"))
{
	m_bMute = FALSE;
	m_bDestroyAfterRead = FALSE;
	m_bAutoReply = FALSE;
	memset(m_szAutoReplyContent, 0, sizeof(m_szAutoReplyContent));
	m_bEnableRevokeChatMsg = FALSE;

	m_xMainDlg = 0;
	m_yMainDlg = 0;
	m_cxMainDlg = 0;
	m_cyMainDlg = 0;

	m_bEnableExitPrompt = TRUE;
	m_bExitWhenCloseMainDlg = FALSE;

	m_nBuddyListHeadPicStyle = 0;
	m_bBuddyListShowBigHeadPicInSel = TRUE;

	m_nNameStyleIndex = NAME_STYLE_SHOW_NICKNAME_AND_ACCOUNT;
	m_bShowSimpleProfile = FALSE;

	m_cxChatDlg = 587;
	m_cyChatDlg = 535;

	m_cxGroupDlg = 670;
	m_cyGroupDlg = 600;
	
	m_bPressEnterToSend = TRUE;
	m_nFontSize = 12;
	m_lFontColor = 0;
	m_bFontBold = FALSE;
	m_bFontItalic = FALSE;
	m_bFontItalic = FALSE;
	m_bFontUnderline = FALSE;

	m_uFaceID = 0;
	memset(m_szCustomFace, 0, sizeof(m_szCustomFace));

	m_bShowLastMsg = TRUE;
}

CUserConfig::~CUserConfig()
{
}

BOOL CUserConfig::LoadConfig(PCTSTR pszFilePath)
{
	int cxScreenResolution = ::GetSystemMetrics(SM_CXSCREEN);
	const long nMainDlgDefaultX = cxScreenResolution-330;
	const long nMainDlgDefaultY  = 150;
	const long nMainDlgDefaultWidth = 280;
	const long nMainDlgDefaultHeight = 720;

	const long nChatDlgDefaultWidth = 587;
	const long nChatDlgDefaultHeight = 535;

	const long nGroupDlgDefaultWidth = 670;
	const long nGroupDlgDefaultHeight = 600;
	
	CIniFile iniFile;
	//App
	m_bMute = iniFile.ReadInt(_T("App"), _T("Mute"), 0, pszFilePath) ? TRUE : FALSE;
	m_bDestroyAfterRead = iniFile.ReadInt(_T("App"), _T("DestroyAfterRead"), 0, pszFilePath) ? TRUE : FALSE;
	m_bAutoReply = iniFile.ReadInt(_T("App"), _T("AutoReply"), 0, pszFilePath) ? TRUE : FALSE;
	iniFile.ReadString(_T("App"), _T("AutoReplyContent"), _T(""), m_szAutoReplyContent, ARRAYSIZE(m_szAutoReplyContent), pszFilePath);
	m_bEnableRevokeChatMsg = iniFile.ReadInt(_T("App"), _T("RevokeChatMsg"), 0, pszFilePath) ? TRUE : FALSE;
	
	//主对话框
	m_xMainDlg = iniFile.ReadInt(_T("MainDlg"), _T("MainDlgX"), nMainDlgDefaultX, pszFilePath);
	m_yMainDlg = iniFile.ReadInt(_T("MainDlg"), _T("MainDlgY"),  nMainDlgDefaultY, pszFilePath);
	m_cxMainDlg = iniFile.ReadInt(_T("MainDlg"), _T("MainDlgWidth"), nMainDlgDefaultWidth, pszFilePath);
	m_cyMainDlg = iniFile.ReadInt(_T("MainDlg"), _T("MainDlgHeight"), nMainDlgDefaultHeight, pszFilePath);
	m_bEnableExitPrompt = iniFile.ReadInt(_T("MainDlg"), _T("ExitPrompt"), 1, pszFilePath) ? TRUE : FALSE;
	m_bExitWhenCloseMainDlg = iniFile.ReadInt(_T("MainDlg"), _T("ExitWhenClose"), 0, pszFilePath) ? TRUE : FALSE;

	m_nBuddyListHeadPicStyle = iniFile.ReadInt(_T("MainDlg"), _T("BuddyListHeadPicStyle"), 0, pszFilePath);
	m_bBuddyListShowBigHeadPicInSel = iniFile.ReadInt(_T("MainDlg"), _T("ShowBigHeadPicInSel"), 0, pszFilePath) ? TRUE : FALSE;

	m_nNameStyleIndex = iniFile.ReadInt(_T("MainDlg"), _T("NameStyleIndex"), 0, pszFilePath);
	m_bShowSimpleProfile = iniFile.ReadInt(_T("MainDlg"), _T("SimpleProfile"), 0, pszFilePath) ? TRUE:FALSE;

	// 聊天对话框
	m_cxChatDlg = iniFile.ReadInt(_T("ChatDlg"), _T("ChatDlgWidth"), nChatDlgDefaultWidth, pszFilePath);
	m_cyChatDlg = iniFile.ReadInt(_T("ChatDlg"), _T("ChatDlgWidth"), nChatDlgDefaultHeight, pszFilePath);
	m_bPressEnterToSend = iniFile.ReadInt(_T("ChatDlg"), _T("EnableEnterToSend"), 1, pszFilePath) ? TRUE : FALSE;
	iniFile.ReadString(_T("ChatDlg"), _T("FontName"), _T("微软雅黑"), m_strFontName.GetBuffer(32), 32, pszFilePath);
	m_strFontName.ReleaseBuffer();
	m_nFontSize = iniFile.ReadInt(_T("ChatDlg"), _T("FontSize"), 12, pszFilePath);
	m_lFontColor = iniFile.ReadInt(_T("ChatDlg"), _T("FontColor"), 0,pszFilePath);
	m_bFontBold = iniFile.ReadInt(_T("ChatDlg"), _T("FontBold"), 0, pszFilePath) ? TRUE : FALSE;
	m_bFontItalic = iniFile.ReadInt(_T("ChatDlg"), _T("FontItalic"), 0, pszFilePath) ? TRUE : FALSE;
	m_bFontUnderline = iniFile.ReadInt(_T("ChatDlg"), _T("FontUnderLine"), 0, pszFilePath) ? TRUE : FALSE;
	m_bShowLastMsg = iniFile.ReadInt(_T("ChatDlg"), _T("EnableShowLastMsg"), 1, pszFilePath) ? TRUE : FALSE;

	m_uFaceID = (UINT)iniFile.ReadInt(_T("UserInfo"), _T("FaceID"), 0, pszFilePath);
	iniFile.ReadString(_T("UserInfo"), _T("CustomFace"), _T(""), m_szCustomFace, ARRAYSIZE(m_szCustomFace), pszFilePath);

	m_cxGroupDlg = iniFile.ReadInt(_T("GroupDlg"), _T("GroupDlgWidth"), nGroupDlgDefaultWidth, pszFilePath);
	m_cyGroupDlg = iniFile.ReadInt(_T("GroupDlg"), _T("GroupDlgWidth"), nGroupDlgDefaultHeight, pszFilePath);

	return TRUE;
}


BOOL CUserConfig::SaveConfig(PCTSTR pszFilePath)
{
	CIniFile iniFile;
	//App
	iniFile.WriteInt(_T("App"), _T("Mute"), m_bMute, pszFilePath);
	iniFile.WriteInt(_T("App"), _T("DestroyAfterRead"), m_bDestroyAfterRead, pszFilePath);
	iniFile.WriteInt(_T("App"), _T("AutoReply"), m_bAutoReply, pszFilePath);
	iniFile.WriteString(_T("App"), _T("AutoReplyContent"), m_szAutoReplyContent, pszFilePath);
	iniFile.WriteInt(_T("App"), _T("RevokeChatMsg"), m_bEnableRevokeChatMsg, pszFilePath);
	
	//主对话框
	iniFile.WriteInt(_T("MainDlg"), _T("MainDlgX"), m_xMainDlg, pszFilePath);
	iniFile.WriteInt(_T("MainDlg"), _T("MainDlgY"),  m_yMainDlg, pszFilePath);
	iniFile.WriteInt(_T("MainDlg"), _T("MainDlgWidth"), m_cxMainDlg, pszFilePath);
	iniFile.WriteInt(_T("MainDlg"), _T("MainDlgHeight"), m_cyMainDlg, pszFilePath);
	iniFile.WriteInt(_T("MainDlg"), _T("ExitPrompt"), m_bEnableExitPrompt, pszFilePath);
	iniFile.WriteInt(_T("MainDlg"), _T("ExitWhenClose"), m_bExitWhenCloseMainDlg, pszFilePath);

	iniFile.WriteInt(_T("MainDlg"), _T("BuddyListHeadPicStyle"), m_nBuddyListHeadPicStyle, pszFilePath);
	iniFile.WriteInt(_T("MainDlg"), _T("ShowBigHeadPicInSel"), m_bBuddyListShowBigHeadPicInSel, pszFilePath);

	iniFile.WriteInt(_T("MainDlg"), _T("NameStyleIndex"), m_nNameStyleIndex, pszFilePath);
	iniFile.WriteInt(_T("MainDlg"), _T("SimpleProfile"), m_bShowSimpleProfile, pszFilePath);

	// 聊天对话框
	iniFile.WriteInt(_T("ChatDlg"), _T("ChatDlgWidth"), m_cxChatDlg, pszFilePath);
	iniFile.WriteInt(_T("ChatDlg"), _T("ChatDlgHeight"), m_cyChatDlg, pszFilePath);
	iniFile.WriteInt(_T("ChatDlg"), _T("EnableEnterToSend"), m_bPressEnterToSend, pszFilePath);
	iniFile.WriteString(_T("ChatDlg"), _T("FontName"), m_strFontName, pszFilePath);
	iniFile.WriteInt(_T("ChatDlg"), _T("FontSize"), m_nFontSize, pszFilePath);
	iniFile.WriteInt(_T("ChatDlg"), _T("FontColor"), m_lFontColor, pszFilePath);
	iniFile.WriteInt(_T("ChatDlg"), _T("FontBold"), m_bFontBold, pszFilePath);
	iniFile.WriteInt(_T("ChatDlg"), _T("FontItalic"), m_bFontItalic, pszFilePath);
	iniFile.WriteInt(_T("ChatDlg"), _T("FontUnderLine"), m_bFontUnderline, pszFilePath);
	iniFile.WriteInt(_T("ChatDlg"), _T("EnableShowLastMsg"), m_bShowLastMsg, pszFilePath);

	iniFile.WriteInt(_T("UserInfo"), _T("FaceID"), m_uFaceID, pszFilePath);
	iniFile.WriteString(_T("UserInfo"), _T("CustomFace"), m_szCustomFace, pszFilePath);

	iniFile.WriteInt(_T("GroupDlg"), _T("GroupDlgWidth"), m_cxGroupDlg, pszFilePath);
	iniFile.WriteInt(_T("GroupDlg"), _T("GroupDlgHeight"), m_cyGroupDlg, pszFilePath);
	
	return TRUE;
}

void CUserConfig::EnableMute(BOOL bMute)
{
	m_bMute = bMute;
}

void CUserConfig::EnableDestroyAfterRead(BOOL bEnable)
{
	m_bDestroyAfterRead = bEnable;
}

void CUserConfig::EnableAutoReply(BOOL bAutoReply)
{
	m_bAutoReply = bAutoReply;
}

void CUserConfig::SetAutoReplyContent(PCTSTR pszAutoReplyContent)
{
	_tcscpy_s(m_szAutoReplyContent, ARRAYSIZE(m_szAutoReplyContent), pszAutoReplyContent);
}

void CUserConfig::EnableRevokeChatMsg(BOOL bEnable)
{
	m_bEnableRevokeChatMsg = bEnable;
}

BOOL CUserConfig::IsEnableMute() const
{
	return m_bMute;
}

BOOL CUserConfig::IsEnableDestroyAfterRead() const
{
	return m_bDestroyAfterRead;
}
	
BOOL CUserConfig::IsEnableAutoReply() const
{
	return m_bAutoReply;
}
	
PCTSTR  CUserConfig::GetAutoReplyContent() const
{
	return m_szAutoReplyContent;
}

BOOL CUserConfig::IsEnableRevokeChatMsg() const
{
	return m_bEnableRevokeChatMsg;
}

void CUserConfig::SetMainDlgX(long x)
{
	m_xMainDlg = x;
}

void CUserConfig::SetMainDlgY(long y)
{
	m_yMainDlg = y;
}

void CUserConfig::SetMainDlgWidth(long nWidth)
{
	m_cxMainDlg = nWidth;
}

void CUserConfig::SetMainDlgHeight(long nHeight)
{
	m_cyMainDlg = nHeight;
}

void CUserConfig::EnableExitPrompt(BOOL bEnable)
{
	m_bEnableExitPrompt = bEnable;
}

BOOL CUserConfig::IsEnableExitPrompt()
{
	return m_bEnableExitPrompt;
}

void CUserConfig::EnableExitWhenCloseMainDlg(BOOL bEnable)
{
	m_bExitWhenCloseMainDlg = bEnable;
}

BOOL CUserConfig::IsEnableExitWhenCloseMainDlg()
{
	return m_bExitWhenCloseMainDlg;
}

void CUserConfig::SetBuddyListHeadPicStyle(long nStyle)
{
	m_nBuddyListHeadPicStyle = nStyle;
}

void CUserConfig::EnableBuddyListShowBigHeadPicInSel(BOOL bEnable)
{
	m_bBuddyListShowBigHeadPicInSel = bEnable;
}

void CUserConfig::SetNameStyle(long nStyle)
{
	m_nNameStyleIndex = nStyle;
}

long CUserConfig::GetNameStyle()
{
	return m_nNameStyleIndex;
}

void CUserConfig::EnableSimpleProfile(BOOL bEnable)
{
	m_bShowSimpleProfile = bEnable;
}

BOOL CUserConfig::IsEnableSimpleProfile()
{
	return m_bShowSimpleProfile;
}

long CUserConfig::GetMainDlgX()
{
	long cxScreen = ::GetSystemMetrics(SM_CXSCREEN);
	if(m_xMainDlg>cxScreen || m_xMainDlg<0)
		m_xMainDlg = cxScreen-300;
	
	return m_xMainDlg;
}
	
long CUserConfig::GetMainDlgY()
{
	long cyScreen = ::GetSystemMetrics(SM_CYSCREEN);
	if(m_yMainDlg>cyScreen || m_yMainDlg<0)
		m_yMainDlg = cyScreen-30;
	
	return m_yMainDlg;
}

long CUserConfig::GetMainDlgWidth() const
{
	return m_cxMainDlg;
}

long CUserConfig::GetMainDlgHeight() const
{
	return m_cyMainDlg;
}

long CUserConfig::GetBuddyListHeadPicStyle() const
{
	return m_nBuddyListHeadPicStyle;
}

BOOL CUserConfig::IsEnableBuddyListShowBigHeadPicInSel() const
{
	return m_bBuddyListShowBigHeadPicInSel;
}

void CUserConfig::SetChatDlgWidth(long nWidth)
{
	m_cxChatDlg = nWidth;
}

void CUserConfig::SetChatDlgHeight(long nHeight)
{
	m_cyChatDlg = nHeight;
}

long CUserConfig::GetChatDlgWidth() const
{
	return m_cxChatDlg;
}

long CUserConfig::GetChatDlgHeight() const
{
	return m_cyChatDlg;
}

void CUserConfig::SetGroupDlgWidth(long nWidth)
{
	m_cxGroupDlg = nWidth;
}

void CUserConfig::SetGroupDlgHeight(long nHeight)
{
	m_cyGroupDlg = nHeight;
}

long CUserConfig::GetGroupDlgWidth() const
{
	return m_cxGroupDlg;
}

long CUserConfig::GetGroupDlgHeight() const
{
	return m_cyGroupDlg;
}

void CUserConfig::EnablePressEnterToSend(BOOL bEnable)
{
	m_bPressEnterToSend = bEnable;
}

void CUserConfig::SetFontName(PCTSTR pszFontName)
{
	m_strFontName = pszFontName;
}

void CUserConfig::SetFontSize(long nSize)
{
	m_nFontSize = nSize;
}

void CUserConfig::SetFontColor(long lColor)
{
	m_lFontColor = lColor;
}

void CUserConfig::EnableFontBold(BOOL bBold)
{
	m_bFontBold = bBold;
}

void CUserConfig::EnableFontItalic(BOOL bItalic)
{
	m_bFontItalic = bItalic;
}

void CUserConfig::EnableFontUnderline(BOOL bUnderline)
{
	m_bFontUnderline = bUnderline;
}

BOOL CUserConfig::IsEnablePressEnterToSend() const
{
	return m_bPressEnterToSend;
}

PCTSTR CUserConfig::GetFontName() const
{
	return m_strFontName;
}

long CUserConfig::GetFontSize() const
{
	return m_nFontSize;
}

long CUserConfig::GetFontColor() const
{
	return m_lFontColor;
}

BOOL CUserConfig::IsEnableFontBold() const
{
	return m_bFontBold;
}

BOOL CUserConfig::IsEnableFontItalic() const
{
	return m_bFontItalic;
}

BOOL CUserConfig::IsEnableFontUnderline() const
{
	return m_bFontUnderline;
}

void CUserConfig::SetFaceID(UINT uFaceID)
{
	m_uFaceID = uFaceID;
}
	
UINT CUserConfig::GetFaceID() const
{
	return m_uFaceID;
}

void CUserConfig::SetCustomFace(PCTSTR pszCustomFace)
{
	_tcscpy_s(m_szCustomFace, ARRAYSIZE(m_szCustomFace), pszCustomFace);
}
	
PCTSTR CUserConfig::GetCustomFace() const
{
	return m_szCustomFace;
}

void CUserConfig::EnableShowLastMsgInChatDlg(BOOL bEnable)
{
	m_bShowLastMsg = bEnable;
}

BOOL CUserConfig::IsEnableShowLastMsgInChatDlg()
{
	return m_bShowLastMsg;
}


