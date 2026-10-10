#include "stdafx.h"
#include "UserConfig.h"
#include "..\Common\Util\IniFile.h"
#include "ConfigTextCodec.h"		// 自动回复、快捷回复的文字编码成 ASCII 后存盘
#include "../Common/I18n/I18n.h"	// TR / TR_NOOP：快捷回复的默认文字随界面语言翻译
#include "../Feature/Screenshot/ScreenshotHotkey.h"	// 截图快捷键的缺省值 kDefaultScreenshotHotkey

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

// 第 index 条快捷回复在 [QuickReply] 段里的键名：Text1 ~ Text3（docs/快捷回复方案.md §3.1）
static CString QuickReplyKey(int index)
{
	CString key;
	key.Format(_T("Text%d"), index + 1);
	return key;
}

// 读「发送代码」窗口上次手选的语言标识时的缓冲区长度（字符）。语言标识最长十来个字符，留足余量。
static const DWORD kCodeSnippetLangChars = 32;

// 读 [ChatFontOverride] 整段时缓冲区的初始长度与上限（字符）。一行约 50 个字符，
// 初始长度够放几百个会话；放不下时按倍数扩大，到上限为止。
static const DWORD kOverrideSectionInitChars = 32 * 1024;
static const DWORD kOverrideSectionMaxChars  = 4 * 1024 * 1024;

// GetPrivateProfileSection 在缓冲区不够时返回"缓冲区长度 - 2"，据此判断要不要扩大。
static const DWORD kSectionTruncatedDelta = 2;

// ---- 聊天字幕模式偏好在账号配置里的存放位置（格式见 SubtitlePrefs.h）----
// 外观放在 [Subtitle] 段的三个键，各会话的字幕位置单独成 [SubtitlePlacement] 段，一个会话一行。
static const TCHAR* const kSubtitleSection          = _T("Subtitle");
static const TCHAR* const kSubtitleFontSizeKey      = _T("FontSize");
static const TCHAR* const kSubtitleMaxLinesKey      = _T("MaxLines");
static const TCHAR* const kSubtitleHideCaptureKey   = _T("HideFromCapture");
static const TCHAR* const kSubtitlePlacementSection = _T("SubtitlePlacement");

// 整段读出一个配置段的全部"键 = 值"。缓冲区从 kOverrideSectionInitChars 起，不够时加倍重读，到
// kOverrideSectionMaxChars 为止。
//   section：    段名；
//   pszFilePath：账号配置文件路径；
//   entries：    [出参] 读出的键值，调用前会被清空；没有"="或键为空的行跳过。
static void ReadSectionEntries(PCTSTR section, PCTSTR pszFilePath, std::vector<std::pair<CString, CString> >& entries)
{
	entries.clear();

	//整段读：结果是 "键=值\0键=值\0\0"。缓冲区不够时加倍重读
	std::vector<TCHAR> buf(kOverrideSectionInitChars);
	DWORD got = 0;
	while (true)
	{
		got = ::GetPrivateProfileSection(section, &buf[0], (DWORD)buf.size(), pszFilePath);
		if (got + kSectionTruncatedDelta < (DWORD)buf.size() || buf.size() >= kOverrideSectionMaxChars)
			break;
		buf.resize(buf.size() * 2);
	}

	const TCHAR* p = &buf[0];
	while (*p != _T('\0'))
	{
		const CString line(p);
		const int eq = line.Find(_T('='));
		if (eq > 0)
			entries.push_back(std::make_pair(line.Left(eq), line.Mid(eq + 1)));
		p += line.GetLength() + 1;
	}
}

// 把"键 = 值"列表整段写成一个配置段（覆盖原有内容）；列表为空时删掉该段。
//   section：    段名；
//   pszFilePath：账号配置文件路径；
//   entries：    要写入的键值。
static void WriteSectionEntries(PCTSTR section, PCTSTR pszFilePath,
								const std::vector<std::pair<CString, CString> >& entries)
{
	if (entries.empty())
	{
		::WritePrivateProfileString(section, NULL, NULL, pszFilePath);
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
	::WritePrivateProfileSection(section, &block[0], pszFilePath);
}

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

	std::vector<std::pair<CString, CString> > entries;
	ReadSectionEntries(kChatFontOverrideSection, pszFilePath, entries);
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
	WriteSectionEntries(kChatFontOverrideSection, pszFilePath, entries);
}

// 从账号配置读出聊天字幕偏好，整体替换 prefs。缺键时外观取默认值（字号中、2 行、不排除采集），
// 没有任何会话位置；超出范围的外观值由 SetAppearance 整理。
//   iniFile：   读写 ini 的工具对象；
//   pszFilePath：账号配置文件路径；
//   prefs：     [出参] 读出的偏好。
static void LoadSubtitlePrefs(CIniFile& iniFile, PCTSTR pszFilePath, subtitle::SubtitlePrefs& prefs)
{
	prefs = subtitle::SubtitlePrefs();

	subtitle::SubtitleAppearance ap;
	ap.m_fontSize = (int)iniFile.ReadInt(kSubtitleSection, kSubtitleFontSizeKey, subtitle::kDefaultFontSize, pszFilePath);
	ap.m_maxLines = (int)iniFile.ReadInt(kSubtitleSection, kSubtitleMaxLinesKey, subtitle::kDefaultMaxLines, pszFilePath);
	ap.m_hideFromCapture = iniFile.ReadInt(kSubtitleSection, kSubtitleHideCaptureKey, 0, pszFilePath) != 0;
	prefs.SetAppearance(ap);

	std::vector<std::pair<CString, CString> > entries;
	ReadSectionEntries(kSubtitlePlacementSection, pszFilePath, entries);
	prefs.ImportPlacements(entries);
}

// 把聊天字幕偏好写回账号配置。[SubtitlePlacement] 段整段重写：没有任何会话位置时删掉该段。
//   iniFile：   读写 ini 的工具对象；
//   pszFilePath：账号配置文件路径；
//   prefs：     要写入的偏好。
static void SaveSubtitlePrefs(CIniFile& iniFile, PCTSTR pszFilePath, const subtitle::SubtitlePrefs& prefs)
{
	const subtitle::SubtitleAppearance& ap = prefs.GetAppearance();
	iniFile.WriteInt(kSubtitleSection, kSubtitleFontSizeKey, ap.m_fontSize, pszFilePath);
	iniFile.WriteInt(kSubtitleSection, kSubtitleMaxLinesKey, ap.m_maxLines, pszFilePath);
	iniFile.WriteInt(kSubtitleSection, kSubtitleHideCaptureKey, ap.m_hideFromCapture ? 1 : 0, pszFilePath);

	std::vector<std::pair<CString, CString> > entries;
	prefs.ExportPlacements(entries);
	WriteSectionEntries(kSubtitlePlacementSection, pszFilePath, entries);
}

CUserConfig::CUserConfig()
{
	m_bMute = FALSE;
	m_bDestroyAfterRead = FALSE;
	m_bEnableRevokeChatMsg = FALSE;

	m_bEnableExitPrompt = TRUE;
	m_bExitWhenCloseMainDlg = FALSE;
	m_bTrayMinimizeTipShown = FALSE;		//新账号默认没提示过，首次最小化到托盘时弹一次

	m_nBuddyListHeadPicStyle = 0;
	m_bBuddyListShowBigHeadPicInSel = TRUE;

	m_nNameStyleIndex = NAME_STYLE_SHOW_NICKNAME_AND_ACCOUNT;
	m_strCodeSnippetLang.clear();
	m_bCodeComposeLineNumbers = TRUE;
	m_bCodeNotesPaneOpen = TRUE;
	m_bShowSimpleProfile = FALSE;

	m_cxChatDlg = 587;
	m_cyChatDlg = 535;

	m_cxGroupDlg = 670;
	m_cyGroupDlg = 600;

	m_nHistoryMgrPaneWidth = 0;
	m_bHistoryMgrCollapsed = FALSE;
	m_nGroupMemberView = 0;

	m_bPressEnterToSend = TRUE;
	m_bEnterToSendLoaded = FALSE;
	for (int i = 0; i < kQuickReplySlotCount; ++i)
	{
		m_bQuickReplySet[i] = FALSE;
	}

	m_uFaceID = 0;
	memset(m_szCustomFace, 0, sizeof(m_szCustomFace));

	m_bShowLastMsg = TRUE;

	// 系统设置（新版对话框）新增项默认值，对照 design/系统设置1~3.png 各开关初始态。
	m_bNewMsgNotify = TRUE;
	m_bDesktopPopup = TRUE;
	m_bShakeFlashIcon = FALSE;
	m_bDoNotDisturb = FALSE;
	m_bNotifyAfterLock = TRUE;
	m_bAutoAway = TRUE;				//默认开：无操作满 5 分钟自动切到离开
	m_nRecordQuality = 0;			//默认标准画质
	m_strMouseEffects.Empty();		//默认没有改过鼠标效果，按默认设置（只开光圈）
	m_bRecordHotkey = TRUE;			//默认启用 Ctrl+Alt+R
	m_dwScreenshotHotkey = balloonscreenshot::kDefaultScreenshotHotkey;	//默认 Alt+Z
	m_bScreenshotHideWindow = TRUE;	//默认截图时隐藏发起截图的聊天窗
	m_bKeepAwake = FALSE;			//默认关闭：该功能用于演示、长时间传输文件一类的临时场景
	m_bDockAutoHide = TRUE;			//默认开：主面板拖到屏幕边缘时自动隐藏
	m_nAutoAwayDelayIndex = 0;		//默认 5 分钟（下拉第一项）
	m_bAutoRecvImage = TRUE;
	m_bAutoRecvFile = FALSE;
	m_bMsgRoaming = TRUE;
	m_bLinkDetect = TRUE;
	m_bAutoReplyAway = FALSE;	//默认关闭：用户在聊天页打开后才自动回复
	m_bAutoReplyBusy = FALSE;
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
	const long nChatDlgDefaultWidth = 587;
	const long nChatDlgDefaultHeight = 535;

	const long nGroupDlgDefaultWidth = 670;
	const long nGroupDlgDefaultHeight = 600;
	
	CIniFile iniFile;
	//App
	m_bMute = iniFile.ReadInt(_T("App"), _T("Mute"), 0, pszFilePath) ? TRUE : FALSE;
	m_bDestroyAfterRead = iniFile.ReadInt(_T("App"), _T("DestroyAfterRead"), 0, pszFilePath) ? TRUE : FALSE;
	m_bEnableRevokeChatMsg = iniFile.ReadInt(_T("App"), _T("RevokeChatMsg"), 0, pszFilePath) ? TRUE : FALSE;
	
	//主对话框
	winplace::LoadWindowPlacementPrefs(iniFile, pszFilePath, _T("MainPanel"), _T("MainPanel"), m_mainPanelPlacement);
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
	//发送键：键不存在（升级上来的老配置）时记下来，由 ChatSendMode 用 flamingo.ini 里的旧值迁移一次（BUG-119）
	{
		const long kAbsent = -1;
		const long nEnterToSend = (long)(int)iniFile.ReadInt(_T("ChatDlg"), _T("EnableEnterToSend"), kAbsent, pszFilePath);
		m_bEnterToSendLoaded = (nEnterToSend != kAbsent) ? TRUE : FALSE;
		m_bPressEnterToSend = (nEnterToSend == 0) ? FALSE : TRUE;
	}
	LoadChatFontPrefs(iniFile, pszFilePath, m_chatFontPrefs);
	LoadSubtitlePrefs(iniFile, pszFilePath, m_subtitlePrefs);
	m_bShowLastMsg = iniFile.ReadInt(_T("ChatDlg"), _T("EnableShowLastMsg"), 1, pszFilePath) ? TRUE : FALSE;
	//「发送代码」窗口上次手选的语言：语言标识只含 ASCII 字符，其它字符一律忽略
	{
		TCHAR szCodeLang[kCodeSnippetLangChars] = { 0 };
		iniFile.ReadString(_T("ChatDlg"), _T("CodeSnippetLang"), _T(""), szCodeLang, ARRAYSIZE(szCodeLang), pszFilePath);
		m_strCodeSnippetLang.clear();
		for (size_t i = 0; szCodeLang[i] != 0; ++i)
		{
			if (szCodeLang[i] > 0 && szCodeLang[i] < 0x80)
				m_strCodeSnippetLang += static_cast<char>(szCodeLang[i]);
		}
	}
	//「发送代码」窗口是否显示行号、有批注时批注栏是否展开：缺省都是 1
	m_bCodeComposeLineNumbers = iniFile.ReadInt(_T("ChatDlg"), _T("CodeComposeLineNumbers"), 1, pszFilePath) ? TRUE : FALSE;
	m_bCodeNotesPaneOpen = iniFile.ReadInt(_T("ChatDlg"), _T("CodeNotesPaneOpen"), 1, pszFilePath) ? TRUE : FALSE;

	m_uFaceID = (UINT)iniFile.ReadInt(_T("UserInfo"), _T("FaceID"), 0, pszFilePath);
	iniFile.ReadString(_T("UserInfo"), _T("CustomFace"), _T(""), m_szCustomFace, ARRAYSIZE(m_szCustomFace), pszFilePath);

	m_cxGroupDlg = iniFile.ReadInt(_T("GroupDlg"), _T("GroupDlgWidth"), nGroupDlgDefaultWidth, pszFilePath);
	m_cyGroupDlg = iniFile.ReadInt(_T("GroupDlg"), _T("GroupDlgWidth"), nGroupDlgDefaultHeight, pszFilePath);

	winplace::LoadWindowPlacementPrefs(iniFile, pszFilePath, _T("HistoryDlg"), _T("HistoryDlg"), m_historyDlgPlacement);
	m_nHistoryMgrPaneWidth = iniFile.ReadInt(_T("HistoryDlg"), _T("ManagerPaneWidth"), 0, pszFilePath);
	m_bHistoryMgrCollapsed = iniFile.ReadInt(_T("HistoryDlg"), _T("ManagerCollapsed"), 0, pszFilePath) ? TRUE : FALSE;
	//群聊窗成员页签的视图：只认 1（部门树），其余一律按 0（平铺）
	m_nGroupMemberView = (iniFile.ReadInt(_T("GroupDlg"), _T("MemberView"), 0, pszFilePath) == 1) ? 1 : 0;

	// 系统设置（新版对话框）新增项 —— 默认值与构造函数一致，缺键时回落默认。
	m_bNewMsgNotify = iniFile.ReadInt(_T("Settings"), _T("NewMsgNotify"), 1, pszFilePath) ? TRUE : FALSE;
	m_bDesktopPopup = iniFile.ReadInt(_T("Settings"), _T("DesktopPopup"), 1, pszFilePath) ? TRUE : FALSE;
	m_bShakeFlashIcon = iniFile.ReadInt(_T("Settings"), _T("ShakeFlashIcon"), 0, pszFilePath) ? TRUE : FALSE;
	m_bDoNotDisturb = iniFile.ReadInt(_T("Settings"), _T("DoNotDisturb"), 0, pszFilePath) ? TRUE : FALSE;
	m_bNotifyAfterLock = iniFile.ReadInt(_T("Settings"), _T("NotifyAfterLock"), 1, pszFilePath) ? TRUE : FALSE;
	//缺省值 1 / 0：老账号的配置文件里没有这两项，按"开启、5 分钟"处理，与设置页默认值一致
	m_bAutoAway = iniFile.ReadInt(_T("Settings"), _T("AutoAway"), 1, pszFilePath) ? TRUE : FALSE;
	m_bKeepAwake = iniFile.ReadInt(_T("Settings"), _T("KeepAwake"), 0, pszFilePath) ? TRUE : FALSE;
	m_nRecordQuality = iniFile.ReadInt(_T("Settings"), _T("RecordQuality"), 0, pszFilePath) == 1 ? 1 : 0;
	m_bRecordHotkey = iniFile.ReadInt(_T("Settings"), _T("RecordHotkey"), 1, pszFilePath) ? TRUE : FALSE;
	//老账号的配置文件里没有这两项，按「Alt+Z、截图时隐藏当前窗口」处理；ScreenshotHotkey 为 0 表示用户停用了截图快捷键
	m_dwScreenshotHotkey = (DWORD)iniFile.ReadInt(_T("Settings"), _T("ScreenshotHotkey"),
		(int)balloonscreenshot::kDefaultScreenshotHotkey, pszFilePath);
	m_bScreenshotHideWindow = iniFile.ReadInt(_T("Settings"), _T("ScreenshotHideWindow"), 1, pszFilePath) ? TRUE : FALSE;
	{
		TCHAR szFolder[MAX_PATH] = { 0 };
		iniFile.ReadString(_T("Settings"), _T("RecordFolder"), _T(""), szFolder, ARRAYSIZE(szFolder), pszFilePath);
		m_strRecordFolder = szFolder;
	}
	{
		//鼠标效果设置（第 16 期）：一行 ASCII 文本，没有这一项时为空，按默认设置
		TCHAR szMouseEffects[MAX_PATH] = { 0 };
		iniFile.ReadString(_T("Settings"), _T("MouseEffects"), _T(""), szMouseEffects, ARRAYSIZE(szMouseEffects),
		                   pszFilePath);
		m_strMouseEffects = szMouseEffects;
	}
	//缺省值 1：配置文件里没有这一项时按"开启"处理，与设置页默认值一致
	m_bDockAutoHide = iniFile.ReadInt(_T("Settings"), _T("DockAutoHide"), 1, pszFilePath) ? TRUE : FALSE;
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

	//自动回复：两段文字存的是 cfgtext 编码串，解不开（配置被手工改坏）时按没有设置处理，发送默认文字
	m_bAutoReplyAway = iniFile.ReadInt(_T("AutoReply"), _T("AwayEnabled"), 0, pszFilePath) ? TRUE : FALSE;
	m_bAutoReplyBusy = iniFile.ReadInt(_T("AutoReply"), _T("BusyEnabled"), 0, pszFilePath) ? TRUE : FALSE;
	{
		//编码串的缓冲区长度（字符数）：100 个 UTF-16 字符的回复文字转成 UTF-8 最多 300 字节、编码后 600 个字符，留足余量
		const DWORD kEncodedTextChars = 2048;
		std::vector<TCHAR> buf(kEncodedTextChars, _T('\0'));
		std::wstring decoded;

		iniFile.ReadString(_T("AutoReply"), _T("AwayText"), _T(""), &buf[0], kEncodedTextChars, pszFilePath);
		m_strAutoReplyAwayText = cfgtext::DecodeConfigText(&buf[0], decoded) ? decoded.c_str() : _T("");

		buf.assign(kEncodedTextChars, _T('\0'));
		iniFile.ReadString(_T("AutoReply"), _T("BusyText"), _T(""), &buf[0], kEncodedTextChars, pszFilePath);
		m_strAutoReplyBusyText = cfgtext::DecodeConfigText(&buf[0], decoded) ? decoded.c_str() : _T("");

		//快捷回复：键不存在时用默认文字；键存在但为空表示用户特意清空了这一条。编码串只含 0-9 与 a-f，
		//故用一个编码串里不可能出现的字符作「键不存在」的缺省值。解不开（配置被手工改坏）时按没有设置处理
		const TCHAR* const kAbsentMark = _T("?");
		for (int i = 0; i < kQuickReplySlotCount; ++i)
		{
			buf.assign(kEncodedTextChars, _T('\0'));
			iniFile.ReadString(_T("QuickReply"), QuickReplyKey(i), kAbsentMark, &buf[0], kEncodedTextChars, pszFilePath);
			if (_tcscmp(&buf[0], kAbsentMark) == 0 || !cfgtext::DecodeConfigText(&buf[0], decoded))
			{
				m_bQuickReplySet[i] = FALSE;
				m_strQuickReplies[i].Empty();
			}
			else
			{
				m_bQuickReplySet[i] = TRUE;
				m_strQuickReplies[i] = decoded.c_str();
			}
		}
	}

	//主面板搜索框的最近搜索
	{
		//编码串的缓冲区长度（字符数）：最多 10 个关键字、每个最多 32 个 UTF-16 字符，转成 UTF-8
		//约 1 KB、编码后约 2 KB 字符，留足余量
		const DWORD kRecentEncodedChars = 4096;
		std::vector<TCHAR> buf(kRecentEncodedChars, _T('\0'));
		std::wstring decoded;
		iniFile.ReadString(_T("Search"), _T("Recent"), _T(""), &buf[0], kRecentEncodedChars, pszFilePath);
		m_strRecentSearches = cfgtext::DecodeConfigText(&buf[0], decoded) ? decoded.c_str() : _T("");
	}

	return TRUE;
}


BOOL CUserConfig::SaveConfig(PCTSTR pszFilePath)
{
	CIniFile iniFile;
	//App
	iniFile.WriteInt(_T("App"), _T("Mute"), m_bMute, pszFilePath);
	iniFile.WriteInt(_T("App"), _T("DestroyAfterRead"), m_bDestroyAfterRead, pszFilePath);
	iniFile.WriteInt(_T("App"), _T("RevokeChatMsg"), m_bEnableRevokeChatMsg, pszFilePath);
	
	//主对话框
	winplace::SaveWindowPlacementPrefs(iniFile, pszFilePath, _T("MainPanel"), _T("MainPanel"), m_mainPanelPlacement);
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
	SaveSubtitlePrefs(iniFile, pszFilePath, m_subtitlePrefs);
	iniFile.WriteInt(_T("ChatDlg"), _T("EnableShowLastMsg"), m_bShowLastMsg, pszFilePath);
	iniFile.WriteString(_T("ChatDlg"), _T("CodeSnippetLang"), CString(m_strCodeSnippetLang.c_str()), pszFilePath);
	iniFile.WriteInt(_T("ChatDlg"), _T("CodeComposeLineNumbers"), m_bCodeComposeLineNumbers, pszFilePath);
	iniFile.WriteInt(_T("ChatDlg"), _T("CodeNotesPaneOpen"), m_bCodeNotesPaneOpen, pszFilePath);

	iniFile.WriteInt(_T("UserInfo"), _T("FaceID"), m_uFaceID, pszFilePath);
	iniFile.WriteString(_T("UserInfo"), _T("CustomFace"), m_szCustomFace, pszFilePath);

	iniFile.WriteInt(_T("GroupDlg"), _T("GroupDlgWidth"), m_cxGroupDlg, pszFilePath);
	iniFile.WriteInt(_T("GroupDlg"), _T("GroupDlgHeight"), m_cyGroupDlg, pszFilePath);

	winplace::SaveWindowPlacementPrefs(iniFile, pszFilePath, _T("HistoryDlg"), _T("HistoryDlg"), m_historyDlgPlacement);
	iniFile.WriteInt(_T("HistoryDlg"), _T("ManagerPaneWidth"), m_nHistoryMgrPaneWidth, pszFilePath);
	iniFile.WriteInt(_T("HistoryDlg"), _T("ManagerCollapsed"), m_bHistoryMgrCollapsed, pszFilePath);
	iniFile.WriteInt(_T("GroupDlg"), _T("MemberView"), m_nGroupMemberView, pszFilePath);

	// 系统设置（新版对话框）新增项 —— [Settings] 节。
	iniFile.WriteInt(_T("Settings"), _T("NewMsgNotify"), m_bNewMsgNotify, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("DesktopPopup"), m_bDesktopPopup, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("ShakeFlashIcon"), m_bShakeFlashIcon, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("DoNotDisturb"), m_bDoNotDisturb, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("NotifyAfterLock"), m_bNotifyAfterLock, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("AutoAway"), m_bAutoAway, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("KeepAwake"), m_bKeepAwake, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("RecordQuality"), m_nRecordQuality, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("RecordHotkey"), m_bRecordHotkey, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("ScreenshotHotkey"), (int)m_dwScreenshotHotkey, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("ScreenshotHideWindow"), m_bScreenshotHideWindow, pszFilePath);
	iniFile.WriteString(_T("Settings"), _T("RecordFolder"), m_strRecordFolder, pszFilePath);
	iniFile.WriteString(_T("Settings"), _T("MouseEffects"), m_strMouseEffects, pszFilePath);
	iniFile.WriteInt(_T("Settings"), _T("DockAutoHide"), m_bDockAutoHide, pszFilePath);
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

	//自动回复
	iniFile.WriteInt(_T("AutoReply"), _T("AwayEnabled"), m_bAutoReplyAway, pszFilePath);
	iniFile.WriteInt(_T("AutoReply"), _T("BusyEnabled"), m_bAutoReplyBusy, pszFilePath);
	iniFile.WriteString(_T("AutoReply"), _T("AwayText"),
		cfgtext::EncodeConfigText((LPCTSTR)m_strAutoReplyAwayText).c_str(), pszFilePath);
	iniFile.WriteString(_T("AutoReply"), _T("BusyText"),
		cfgtext::EncodeConfigText((LPCTSTR)m_strAutoReplyBusyText).c_str(), pszFilePath);

	//快捷回复：设过的写编码串（空串也写，表示特意清空）；没设过的删掉键，读回时用默认文字
	for (int i = 0; i < kQuickReplySlotCount; ++i)
	{
		if (m_bQuickReplySet[i])
		{
			iniFile.WriteString(_T("QuickReply"), QuickReplyKey(i),
				cfgtext::EncodeConfigText((LPCTSTR)m_strQuickReplies[i]).c_str(), pszFilePath);
		}
		else
		{
			iniFile.WriteString(_T("QuickReply"), QuickReplyKey(i), NULL, pszFilePath);
		}
	}

	//主面板搜索框的最近搜索
	iniFile.WriteString(_T("Search"), _T("Recent"),
		cfgtext::EncodeConfigText((LPCTSTR)m_strRecentSearches).c_str(), pszFilePath);

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
	
BOOL CUserConfig::IsEnableRevokeChatMsg() const
{
	return m_bEnableRevokeChatMsg;
}

const winplace::WindowPlacementPrefs& CUserConfig::GetMainPanelPlacement() const
{
	return m_mainPanelPlacement;
}

void CUserConfig::SetMainPanelPlacement(const winplace::WindowPlacementPrefs& placement)
{
	m_mainPanelPlacement = placement;
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

void CUserConfig::SetCodeSnippetLang(const std::string& id)
{
	m_strCodeSnippetLang = id;
}

std::string CUserConfig::GetCodeSnippetLang() const
{
	return m_strCodeSnippetLang;
}

void CUserConfig::SetCodeComposeLineNumbers(BOOL bShow)
{
	m_bCodeComposeLineNumbers = bShow;
}

BOOL CUserConfig::IsCodeComposeLineNumbers() const
{
	return m_bCodeComposeLineNumbers;
}

void CUserConfig::SetCodeNotesPaneOpen(BOOL bOpen)
{
	m_bCodeNotesPaneOpen = bOpen;
}

BOOL CUserConfig::IsCodeNotesPaneOpen() const
{
	return m_bCodeNotesPaneOpen;
}

void CUserConfig::EnableSimpleProfile(BOOL bEnable)
{
	m_bShowSimpleProfile = bEnable;
}

BOOL CUserConfig::IsEnableSimpleProfile()
{
	return m_bShowSimpleProfile;
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
	//显式设过之后，存盘时一定会写这个键，不再需要从 flamingo.ini 迁移
	m_bEnterToSendLoaded = TRUE;
}

BOOL CUserConfig::HasEnterToSendSetting() const
{
	return m_bEnterToSendLoaded;
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

subtitle::SubtitlePrefs& CUserConfig::Subtitles()
{
	return m_subtitlePrefs;
}

const subtitle::SubtitlePrefs& CUserConfig::Subtitles() const
{
	return m_subtitlePrefs;
}

const winplace::WindowPlacementPrefs& CUserConfig::GetHistoryDlgPlacement() const
{
	return m_historyDlgPlacement;
}

void CUserConfig::SetHistoryDlgPlacement(const winplace::WindowPlacementPrefs& placement)
{
	m_historyDlgPlacement = placement;
}

int CUserConfig::GetHistoryMgrPaneWidth() const
{
	return (int)m_nHistoryMgrPaneWidth;
}

bool CUserConfig::IsHistoryMgrCollapsed() const
{
	return m_bHistoryMgrCollapsed != FALSE;
}

void CUserConfig::SetHistoryMgrPaneLayout(int paneWidth, bool collapsed)
{
	m_nHistoryMgrPaneWidth = (paneWidth > 0) ? paneWidth : 0;
	m_bHistoryMgrCollapsed = collapsed ? TRUE : FALSE;
}

bool CUserConfig::IsGroupMemberTreeView() const
{
	return m_nGroupMemberView == 1;
}

void CUserConfig::SetGroupMemberTreeView(bool treeView)
{
	m_nGroupMemberView = treeView ? 1 : 0;
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

void CUserConfig::SetRecordQuality(long quality)
{
	m_nRecordQuality = (quality == 1) ? 1 : 0;
}

long CUserConfig::GetRecordQuality() const
{
	return m_nRecordQuality;
}

void CUserConfig::SetRecordFolder(LPCTSTR folder)
{
	m_strRecordFolder = (folder != NULL) ? folder : _T("");
}

LPCTSTR CUserConfig::GetRecordFolder() const
{
	return m_strRecordFolder;
}

void CUserConfig::SetMouseEffects(LPCTSTR text)
{
	m_strMouseEffects = (text != NULL) ? text : _T("");
}

LPCTSTR CUserConfig::GetMouseEffects() const
{
	return m_strMouseEffects;
}

void CUserConfig::EnableRecordHotkey(BOOL bEnable)
{
	m_bRecordHotkey = bEnable;
}

BOOL CUserConfig::IsEnableRecordHotkey() const
{
	return m_bRecordHotkey;
}

void CUserConfig::SetScreenshotHotkey(DWORD dwHotkey)
{
	m_dwScreenshotHotkey = dwHotkey;
}

DWORD CUserConfig::GetScreenshotHotkey() const
{
	return m_dwScreenshotHotkey;
}

void CUserConfig::EnableScreenshotHideWindow(BOOL bEnable)
{
	m_bScreenshotHideWindow = bEnable;
}

BOOL CUserConfig::IsScreenshotHideWindow() const
{
	return m_bScreenshotHideWindow;
}

void CUserConfig::EnableKeepAwake(BOOL bEnable)
{
	m_bKeepAwake = bEnable;
}

BOOL CUserConfig::IsEnableKeepAwake() const
{
	return m_bKeepAwake;
}

void CUserConfig::EnableDockAutoHide(BOOL bEnable)
{
	m_bDockAutoHide = bEnable;
}

BOOL CUserConfig::IsEnableDockAutoHide() const
{
	return m_bDockAutoHide;
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

void CUserConfig::EnableAutoReplyAway(BOOL bEnable)
{
	m_bAutoReplyAway = bEnable;
}

BOOL CUserConfig::IsEnableAutoReplyAway() const
{
	return m_bAutoReplyAway;
}

void CUserConfig::EnableAutoReplyBusy(BOOL bEnable)
{
	m_bAutoReplyBusy = bEnable;
}

BOOL CUserConfig::IsEnableAutoReplyBusy() const
{
	return m_bAutoReplyBusy;
}

void CUserConfig::SetAutoReplyAwayText(PCTSTR pszText)
{
	m_strAutoReplyAwayText = (pszText != NULL) ? pszText : _T("");
}

PCTSTR CUserConfig::GetAutoReplyAwayText() const
{
	return m_strAutoReplyAwayText;
}

void CUserConfig::SetAutoReplyBusyText(PCTSTR pszText)
{
	m_strAutoReplyBusyText = (pszText != NULL) ? pszText : _T("");
}

PCTSTR CUserConfig::GetAutoReplyBusyText() const
{
	return m_strAutoReplyBusyText;
}

CString CUserConfig::GetQuickReply(int index) const
{
	if (index < 0 || index >= kQuickReplySlotCount)
	{
		return CString();
	}
	return m_bQuickReplySet[index] ? m_strQuickReplies[index] : GetDefaultQuickReply(index);
}

void CUserConfig::SetQuickReply(int index, PCTSTR pszText)
{
	if (index < 0 || index >= kQuickReplySlotCount)
	{
		return;
	}
	m_strQuickReplies[index] = (pszText != NULL) ? pszText : _T("");
	m_bQuickReplySet[index] = TRUE;
}

BOOL CUserConfig::IsQuickReplyCustomized(int index) const
{
	if (index < 0 || index >= kQuickReplySlotCount)
	{
		return FALSE;
	}
	return m_bQuickReplySet[index];
}

void CUserConfig::ResetQuickReplies()
{
	for (int i = 0; i < kQuickReplySlotCount; ++i)
	{
		m_bQuickReplySet[i] = FALSE;
		m_strQuickReplies[i].Empty();
	}
}

CString CUserConfig::GetDefaultQuickReply(int index)
{
	//三条默认文字（2026-10-06 与用户确认）。文件作用域的静态表在语言确定之前就已初始化，只用 TR_NOOP 标记，
	//取用时再经 TR 翻译
	static const wchar_t* const kDefaults[kQuickReplySlotCount] =
	{
		TR_NOOP(L"收到"),
		TR_NOOP(L"好的，稍后回复"),
		TR_NOOP(L"正在开会，稍后联系"),
	};
	if (index < 0 || index >= kQuickReplySlotCount)
	{
		return CString();
	}
	return CString(TR(kDefaults[index]));
}

void CUserConfig::SetRecentSearches(PCTSTR pszText)
{
	m_strRecentSearches = (pszText != NULL) ? pszText : _T("");
}

PCTSTR CUserConfig::GetRecentSearches() const
{
	return m_strRecentSearches;
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


