#include "stdafx.h"
#include "UserConfig.h"
#include "..\Common\Util\IniFile.h"

#include <utility>
#include <vector>

// ---- 聊天字体偏好在账号配置里的存放位置（格式见 ChatFontPrefs.h）----
// 全局默认字体放在 [ChatDlg] 段，各会话的自定义字体单独成 [ChatFontOverride] 段，
// 一个会话一行，键为"会话类型_会话ID"。
static const TCHAR* const kChatFontSection      = _T("ChatDlg");
static const TCHAR* const kDefaultChatFontKey   = _T("DefaultChatFont");
static const TCHAR* const kChatFontOverrideSection = _T("ChatFontOverride");

// 读全局默认字体时的缓冲区长度（字符）。编码后最长约 40 个字符，留足余量。
static const DWORD kChatFontValueChars = 64;

// 读 [ChatFontOverride] 整段时缓冲区的初始长度与上限（字符）。一行约 50 个字符，
// 初始长度够放几百个会话；放不下时按倍数扩大，到上限为止。
static const DWORD kOverrideSectionInitChars = 32 * 1024;
static const DWORD kOverrideSectionMaxChars  = 4 * 1024 * 1024;

// GetPrivateProfileSection 在缓冲区不够时返回"缓冲区长度 - 2"，据此判断要不要扩大。
static const DWORD kSectionTruncatedDelta = 2;

// 从账号配置读出聊天字体偏好，整体替换 prefs。缺键时为"未设置、无会话自定义"。
//   iniFile：   读写 ini 的工具对象；
//   pszFilePath：账号配置文件路径；
//   prefs：     [出参] 读出的偏好。
static void LoadChatFontPrefs(CIniFile& iniFile, PCTSTR pszFilePath, chatfont::ChatFontPrefs& prefs)
{
	prefs = chatfont::ChatFontPrefs();

	TCHAR szDefault[kChatFontValueChars] = {0};
	iniFile.ReadString(kChatFontSection, kDefaultChatFontKey, _T("0"), szDefault, kChatFontValueChars, pszFilePath);
	net::TextFont defaultFont;
	//格式不对（被手工改坏）时按未设置处理
	if (chatfont::DecodeFont(szDefault, defaultFont))
		prefs.SetDefault(defaultFont);

	//整段读 [ChatFontOverride]：结果是 "键=值\0键=值\0\0"。缓冲区不够时加倍重读
	std::vector<TCHAR> buf(kOverrideSectionInitChars);
	DWORD got = 0;
	while (true)
	{
		got = ::GetPrivateProfileSection(kChatFontOverrideSection, &buf[0], (DWORD)buf.size(), pszFilePath);
		if (got + kSectionTruncatedDelta < (DWORD)buf.size() || buf.size() >= kOverrideSectionMaxChars)
			break;
		buf.resize(buf.size() * 2);
	}

	std::vector<std::pair<CString, CString> > entries;
	const TCHAR* p = &buf[0];
	while (*p != _T('\0'))
	{
		const CString line(p);
		const int eq = line.Find(_T('='));
		if (eq > 0)
			entries.push_back(std::make_pair(line.Left(eq), line.Mid(eq + 1)));
		p += line.GetLength() + 1;
	}
	prefs.ImportOverrides(entries);
}

// 把聊天字体偏好写回账号配置。[ChatFontOverride] 段整段重写：没有任何会话自定义时删掉该段。
//   iniFile：   读写 ini 的工具对象；
//   pszFilePath：账号配置文件路径；
//   prefs：     要写入的偏好。
static void SaveChatFontPrefs(CIniFile& iniFile, PCTSTR pszFilePath, const chatfont::ChatFontPrefs& prefs)
{
	iniFile.WriteString(kChatFontSection, kDefaultChatFontKey, chatfont::EncodeFont(prefs.GetDefault()), pszFilePath);

	std::vector<std::pair<CString, CString> > entries;
	prefs.ExportOverrides(entries);
	if (entries.empty())
	{
		::WritePrivateProfileString(kChatFontOverrideSection, NULL, NULL, pszFilePath);
		return;
	}

	//拼成 WritePrivateProfileSection 要的 "键=值\0键=值\0\0"
	std::vector<TCHAR> block;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		CString line = entries[i].first + _T("=") + entries[i].second;
		for (int k = 0; k < line.GetLength(); ++k)
			block.push_back(line[k]);
		block.push_back(_T('\0'));
	}
	block.push_back(_T('\0'));
	::WritePrivateProfileSection(kChatFontOverrideSection, &block[0], pszFilePath);
}

CUserConfig::CUserConfig()
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
	m_bTrayMinimizeTipShown = FALSE;		//新账号默认没提示过，首次最小化到托盘时弹一次

	m_nBuddyListHeadPicStyle = 0;
	m_bBuddyListShowBigHeadPicInSel = TRUE;

	m_nNameStyleIndex = NAME_STYLE_SHOW_NICKNAME_AND_ACCOUNT;
	m_bShowSimpleProfile = FALSE;

	m_cxChatDlg = 587;
	m_cyChatDlg = 535;

	m_cxGroupDlg = 670;
	m_cyGroupDlg = 600;
	
	m_bPressEnterToSend = TRUE;

	m_uFaceID = 0;
	memset(m_szCustomFace, 0, sizeof(m_szCustomFace));

	m_bShowLastMsg = TRUE;

	// 系统设置（新版对话框）新增项默认值，对照 design/系统设置1~3.png 各开关初始态。
	m_nLanguageIndex = 0;			//简体中文
	m_bNewMsgNotify = TRUE;
	m_bDesktopPopup = TRUE;
	m_bShakeFlashIcon = FALSE;
	m_bDoNotDisturb = FALSE;
	m_bNotifyAfterLock = TRUE;
	m_bAutoAway = TRUE;				//默认开：无操作满 5 分钟自动切到离开
	m_bKeepAwake = FALSE;			//默认关闭：该功能用于演示、长时间传输文件一类的临时场景
	m_nAutoAwayDelayIndex = 0;		//默认 5 分钟（下拉第一项）
	m_bAutoRecvImage = TRUE;
	m_bAutoRecvFile = FALSE;
	m_bMsgRoaming = TRUE;
	m_bLinkDetect = TRUE;
	m_nAudioInputIndex = 0;
	m_nAudioOutputIndex = 0;
	m_nCameraIndex = 0;
	m_bEchoCancel = TRUE;		//设计稿默认开
	m_bTwoFactor = FALSE;		//设计稿默认关
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
	// 老配置文件里没有这个键，缺省按 0（没提示过）读，使升级上来的用户也能收到一次提示。
	m_bTrayMinimizeTipShown = iniFile.ReadInt(_T("MainDlg"), _T("TrayMinimizeTipShown"), 0, pszFilePath) ? TRUE : FALSE;

	m_nBuddyListHeadPicStyle = iniFile.ReadInt(_T("MainDlg"), _T("BuddyListHeadPicStyle"), 0, pszFilePath);
	m_bBuddyListShowBigHeadPicInSel = iniFile.ReadInt(_T("MainDlg"), _T("ShowBigHeadPicInSel"), 0, pszFilePath) ? TRUE : FALSE;

	m_nNameStyleIndex = iniFile.ReadInt(_T("MainDlg"), _T("NameStyleIndex"), 0, pszFilePath);
	m_bShowSimpleProfile = iniFile.ReadInt(_T("MainDlg"), _T("SimpleProfile"), 0, pszFilePath) ? TRUE:FALSE;

	// 聊天对话框
	m_cxChatDlg = iniFile.ReadInt(_T("ChatDlg"), _T("ChatDlgWidth"), nChatDlgDefaultWidth, pszFilePath);
	m_cyChatDlg = iniFile.ReadInt(_T("ChatDlg"), _T("ChatDlgWidth"), nChatDlgDefaultHeight, pszFilePath);
	m_bPressEnterToSend = iniFile.ReadInt(_T("ChatDlg"), _T("EnableEnterToSend"), 1, pszFilePath) ? TRUE : FALSE;
	LoadChatFontPrefs(iniFile, pszFilePath, m_chatFontPrefs);
	m_bShowLastMsg = iniFile.ReadInt(_T("ChatDlg"), _T("EnableShowLastMsg"), 1, pszFilePath) ? TRUE : FALSE;

	m_uFaceID = (UINT)iniFile.ReadInt(_T("UserInfo"), _T("FaceID"), 0, pszFilePath);
	iniFile.ReadString(_T("UserInfo"), _T("CustomFace"), _T(""), m_szCustomFace, ARRAYSIZE(m_szCustomFace), pszFilePath);

	m_cxGroupDlg = iniFile.ReadInt(_T("GroupDlg"), _T("GroupDlgWidth"), nGroupDlgDefaultWidth, pszFilePath);
	m_cyGroupDlg = iniFile.ReadInt(_T("GroupDlg"), _T("GroupDlgWidth"), nGroupDlgDefaultHeight, pszFilePath);

	winplace::LoadWindowPlacementPrefs(iniFile, pszFilePath, _T("HistoryDlg"), _T("HistoryDlg"), m_historyDlgPlacement);

	// 系统设置（新版对话框）新增项 —— 默认值与构造函数一致，缺键时回落默认。
	m_nLanguageIndex = iniFile.ReadInt(_T("Settings"), _T("LanguageIndex"), 0, pszFilePath);
	m_bNewMsgNotify = iniFile.ReadInt(_T("Settings"), _T("NewMsgNotify"), 1, pszFilePath) ? TRUE : FALSE;
	m_bDesktopPopup = iniFile.ReadInt(_T("Settings"), _T("DesktopPopup"), 1, pszFilePath) ? TRUE : FALSE;
	m_bShakeFlashIcon = iniFile.ReadInt(_T("Settings"), _T("ShakeFlashIcon"), 0, pszFilePath) ? TRUE : FALSE;
	m_bDoNotDisturb = iniFile.ReadInt(_T("Settings"), _T("DoNotDisturb"), 0, pszFilePath) ? TRUE : FALSE;
	m_bNotifyAfterLock = iniFile.ReadInt(_T("Settings"), _T("NotifyAfterLock"), 1, pszFilePath) ? TRUE : FALSE;
	//缺省值 1 / 0：老账号的配置文件里没有这两项，按"开启、5 分钟"处理，与设置页默认值一致
	m_bAutoAway = iniFile.ReadInt(_T("Settings"), _T("AutoAway"), 1, pszFilePath) ? TRUE : FALSE;
	m_bKeepAwake = iniFile.ReadInt(_T("Settings"), _T("KeepAwake"), 0, pszFilePath) ? TRUE : FALSE;
	m_nAutoAwayDelayIndex = iniFile.ReadInt(_T("Settings"), _T("AutoAwayDelayIndex"), 0, pszFilePath);
	m_bAutoRecvImage = iniFile.ReadInt(_T("Settings"), _T("AutoRecvImage"), 1, pszFilePath) ? TRUE : FALSE;
	m_bAutoRecvFile = iniFile.ReadInt(_T("Settings"), _T("AutoRecvFile"), 0, pszFilePath) ? TRUE : FALSE;
	m_bMsgRoaming = iniFile.ReadInt(_T("Settings"), _T("MsgRoaming"), 1, pszFilePath) ? TRUE : FALSE;
	//缺省值 1：老账号的配置文件里没有这一项，按"识别"处理，与设置页默认值一致
	m_bLinkDetect = iniFile.ReadInt(_T("Settings"), _T("LinkDetect"), 1, pszFilePath) ? TRUE : FALSE;
	m_nAudioInputIndex = iniFile.ReadInt(_T("Settings"), _T("AudioInputIndex"), 0, pszFilePath);
	m_nAudioOutputIndex = iniFile.ReadInt(_T("Settings"), _T("AudioOutputIndex"), 0, pszFilePath);
	m_nCameraIndex = iniFile.ReadInt(_T("Settings"), _T("CameraIndex"), 0, pszFilePath);
	m_bEchoCancel = iniFile.ReadInt(_T("Settings"), _T("EchoCancel"), 1, pszFilePath) ? TRUE : FALSE;
	m_bTwoFactor = iniFile.ReadInt(_T("Settings"), _T("TwoFactor"), 0, pszFilePath) ? TRUE : FALSE;
	iniFile.ReadString(_T("Settings"), _T("RecvFilePath"), _T(""), m_strRecvFilePath.GetBuffer(MAX_PATH), MAX_PATH, pszFilePath);
	m_strRecvFilePath.ReleaseBuffer();

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
	iniFile.WriteInt(_T("MainDlg"), _T("TrayMinimizeTipShown"), m_bTrayMinimizeTipShown, pszFilePath);

	iniFile.WriteInt(_T("MainDlg"), _T("BuddyListHeadPicStyle"), m_nBuddyListHeadPicStyle, pszFilePath);
	iniFile.WriteInt(_T("MainDlg"), _T("ShowBigHeadPicInSel"), m_bBuddyListShowBigHeadPicInSel, pszFilePath);

	iniFile.WriteInt(_T("MainDlg"), _T("NameStyleIndex"), m_nNameStyleIndex, pszFilePath);
	iniFile.WriteInt(_T("MainDlg"), _T("SimpleProfile"), m_bShowSimpleProfile, pszFilePath);

	// 聊天对话框
	iniFile.WriteInt(_T("ChatDlg"), _T("ChatDlgWidth"), m_cxChatDlg, pszFilePath);
	iniFile.WriteInt(_T("ChatDlg"), _T("ChatDlgHeight"), m_cyChatDlg, pszFilePath);
	iniFile.WriteInt(_T("ChatDlg"), _T("EnableEnterToSend"), m_bPressEnterToSend, pszFilePath);
	SaveChatFontPrefs(iniFile, pszFilePath, m_chatFontPrefs);
	iniFile.WriteInt(_T("ChatDlg"), _T("EnableShowLastMsg"), m_bShowLastMsg, pszFilePath);

	iniFile.WriteInt(_T("UserInfo"), _T("FaceID"), m_uFaceID, pszFilePath);
	iniFile.WriteString(_T("UserInfo"), _T("CustomFace"), m_szCustomFace, pszFilePath);

	iniFile.WriteInt(_T("GroupDlg"), _T("GroupDlgWidth"), m_cxGroupDlg, pszFilePath);
	iniFile.WriteInt(_T("GroupDlg"), _T("GroupDlgHeight"), m_cyGroupDlg, pszFilePath);

	winplace::SaveWindowPlacementPrefs(iniFile, pszFilePath, _T("HistoryDlg"), _T("HistoryDlg"), m_historyDlgPlacement);

	// 系统设置（新版对话框）新增项 —— [Settings] 节。
	iniFile.WriteInt(_T("Settings"), _T("LanguageIndex"), m_nLanguageIndex, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("NewMsgNotify"), m_bNewMsgNotify, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("DesktopPopup"), m_bDesktopPopup, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("ShakeFlashIcon"), m_bShakeFlashIcon, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("DoNotDisturb"), m_bDoNotDisturb, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("NotifyAfterLock"), m_bNotifyAfterLock, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("AutoAway"), m_bAutoAway, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("KeepAwake"), m_bKeepAwake, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("AutoAwayDelayIndex"), m_nAutoAwayDelayIndex, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("AutoRecvImage"), m_bAutoRecvImage, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("AutoRecvFile"), m_bAutoRecvFile, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("MsgRoaming"), m_bMsgRoaming, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("LinkDetect"), m_bLinkDetect, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("AudioInputIndex"), m_nAudioInputIndex, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("AudioOutputIndex"), m_nAudioOutputIndex, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("CameraIndex"), m_nCameraIndex, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("EchoCancel"), m_bEchoCancel, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("TwoFactor"), m_bTwoFactor, pszFilePath);
	iniFile.WriteString(_T("Settings"), _T("RecvFilePath"), m_strRecvFilePath, pszFilePath);

	return TRUE;
}

void CUserConfig::SetRecvFilePath(PCTSTR pszPath)
{
	m_strRecvFilePath = (pszPath != NULL) ? pszPath : _T("");
}

PCTSTR CUserConfig::GetRecvFilePath() const
{
	return m_strRecvFilePath;
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

void CUserConfig::SetTrayMinimizeTipShown(BOOL bShown)
{
	m_bTrayMinimizeTipShown = bShown;
}

BOOL CUserConfig::IsTrayMinimizeTipShown() const
{
	return m_bTrayMinimizeTipShown;
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

BOOL CUserConfig::IsEnablePressEnterToSend() const
{
	return m_bPressEnterToSend;
}

chatfont::ChatFontPrefs& CUserConfig::ChatFonts()
{
	return m_chatFontPrefs;
}

const chatfont::ChatFontPrefs& CUserConfig::ChatFonts() const
{
	return m_chatFontPrefs;
}

const winplace::WindowPlacementPrefs& CUserConfig::GetHistoryDlgPlacement() const
{
	return m_historyDlgPlacement;
}

void CUserConfig::SetHistoryDlgPlacement(const winplace::WindowPlacementPrefs& placement)
{
	m_historyDlgPlacement = placement;
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

// ======================= 系统设置（新版对话框）新增项 =======================

void CUserConfig::SetLanguageIndex(long nIndex)
{
	m_nLanguageIndex = nIndex;
}

long CUserConfig::GetLanguageIndex() const
{
	return m_nLanguageIndex;
}

void CUserConfig::EnableNewMsgNotify(BOOL bEnable)
{
	m_bNewMsgNotify = bEnable;
}

BOOL CUserConfig::IsEnableNewMsgNotify() const
{
	return m_bNewMsgNotify;
}

void CUserConfig::EnableDesktopPopup(BOOL bEnable)
{
	m_bDesktopPopup = bEnable;
}

BOOL CUserConfig::IsEnableDesktopPopup() const
{
	return m_bDesktopPopup;
}

void CUserConfig::EnableShakeFlashIcon(BOOL bEnable)
{
	m_bShakeFlashIcon = bEnable;
}

BOOL CUserConfig::IsEnableShakeFlashIcon() const
{
	return m_bShakeFlashIcon;
}

void CUserConfig::EnableDoNotDisturb(BOOL bEnable)
{
	m_bDoNotDisturb = bEnable;
}

BOOL CUserConfig::IsEnableDoNotDisturb() const
{
	return m_bDoNotDisturb;
}

void CUserConfig::EnableNotifyAfterLock(BOOL bEnable)
{
	m_bNotifyAfterLock = bEnable;
}

BOOL CUserConfig::IsEnableNotifyAfterLock() const
{
	return m_bNotifyAfterLock;
}

void CUserConfig::EnableAutoAway(BOOL bEnable)
{
	m_bAutoAway = bEnable;
}

BOOL CUserConfig::IsEnableAutoAway() const
{
	return m_bAutoAway;
}

void CUserConfig::EnableKeepAwake(BOOL bEnable)
{
	m_bKeepAwake = bEnable;
}

BOOL CUserConfig::IsEnableKeepAwake() const
{
	return m_bKeepAwake;
}

void CUserConfig::SetAutoAwayDelayIndex(long nIndex)
{
	m_nAutoAwayDelayIndex = nIndex;
}

long CUserConfig::GetAutoAwayDelayIndex() const
{
	return m_nAutoAwayDelayIndex;
}

void CUserConfig::EnableAutoRecvImage(BOOL bEnable)
{
	m_bAutoRecvImage = bEnable;
}

BOOL CUserConfig::IsEnableAutoRecvImage() const
{
	return m_bAutoRecvImage;
}

void CUserConfig::EnableAutoRecvFile(BOOL bEnable)
{
	m_bAutoRecvFile = bEnable;
}

BOOL CUserConfig::IsEnableAutoRecvFile() const
{
	return m_bAutoRecvFile;
}

void CUserConfig::EnableMsgRoaming(BOOL bEnable)
{
	m_bMsgRoaming = bEnable;
}

BOOL CUserConfig::IsEnableMsgRoaming() const
{
	return m_bMsgRoaming;
}

void CUserConfig::EnableLinkDetect(BOOL bEnable)
{
	m_bLinkDetect = bEnable;
}

BOOL CUserConfig::IsEnableLinkDetect() const
{
	return m_bLinkDetect;
}

void CUserConfig::SetAudioInputIndex(long nIndex)
{
	m_nAudioInputIndex = nIndex;
}

long CUserConfig::GetAudioInputIndex() const
{
	return m_nAudioInputIndex;
}

void CUserConfig::SetAudioOutputIndex(long nIndex)
{
	m_nAudioOutputIndex = nIndex;
}

long CUserConfig::GetAudioOutputIndex() const
{
	return m_nAudioOutputIndex;
}

void CUserConfig::SetCameraIndex(long nIndex)
{
	m_nCameraIndex = nIndex;
}

long CUserConfig::GetCameraIndex() const
{
	return m_nCameraIndex;
}

void CUserConfig::EnableEchoCancel(BOOL bEnable)
{
	m_bEchoCancel = bEnable;
}

BOOL CUserConfig::IsEnableEchoCancel() const
{
	return m_bEchoCancel;
}

void CUserConfig::EnableTwoFactor(BOOL bEnable)
{
	m_bTwoFactor = bEnable;
}

BOOL CUserConfig::IsEnableTwoFactor() const
{
	return m_bTwoFactor;
}


