#pragma once

#include "ChatFontPrefs.h"		// chatfont::ChatFontPrefs：聊天字体（全局默认 + 各会话自定义）
#include "WindowPlacementPrefs.h"	// winplace::WindowPlacementPrefs：窗口位置与大小
#include "SubtitlePrefs.h"			// subtitle::SubtitlePrefs：聊天字幕模式的外观与各会话位置
#include <string>					// std::string：「发送代码」窗口上次手选的语言标识

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
	void EnableRevokeChatMsg(BOOL bEnable);

	BOOL IsEnableMute() const;
	BOOL IsEnableDestroyAfterRead() const;
	BOOL IsEnableRevokeChatMsg() const;

	// 主面板上次被用户拖动或拉伸后的位置与大小（配置里的 [MainPanel] 一节）。没有保存过时
	// m_hasSaved 为 false，登录后按默认大小居中；保存过时登录后据此摆放，实际位置的计算
	// 见 winplace::ResolveRestoredWindowRect。m_maximized 恒为 false（主面板不提供最大化）。
	const winplace::WindowPlacementPrefs& GetMainPanelPlacement() const;
	// 记下主面板的位置与大小。只改内存里的值，由调用方负责调
	// CFlamingoClient::SaveUserConfig 存盘。
	void SetMainPanelPlacement(const winplace::WindowPlacementPrefs& placement);

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

	//「发送代码」窗口上次手选的语言标识（如 "cpp"，见 codesnippet::CodeLanguageId；docs/代码片段方案.md 第 3.1 节）。
	//  id：语言标识，只认 ASCII；空串表示没有手选过，打开窗口时默认「自动识别」
	void SetCodeSnippetLang(const std::string& id);
	//返回上次手选的语言标识；没有手选过时为空串
	std::string GetCodeSnippetLang() const;

	//「发送代码」窗口是否显示行号（docs/代码片段方案.md 第 10.3 节），默认显示。
	//  bShow：TRUE 显示，FALSE 隐藏
	void SetCodeComposeLineNumbers(BOOL bShow);
	//返回「发送代码」窗口是否显示行号
	BOOL IsCodeComposeLineNumbers() const;

	//代码有批注时，「发送代码」窗口与代码查看窗的批注栏是否展开（docs/代码片段方案.md 第 10.5 节），默认展开。
	//  bOpen：TRUE 展开，FALSE 收起
	void SetCodeNotesPaneOpen(BOOL bOpen);
	//返回有批注时批注栏是否展开
	BOOL IsCodeNotesPaneOpen() const;

	void EnableSimpleProfile(BOOL bEnable);
	BOOL IsEnableSimpleProfile();

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

	// 聊天字幕模式的偏好：外观（字号、最多行数、共享屏幕或录屏时是否隐藏，所有会话共用）与
	// 各会话的字幕位置和锁定状态，格式见 SubtitlePrefs.h。随 LoadConfig / SaveConfig 读写，
	// 改动后由调用方负责调 CFlamingoClient::SaveUserConfig 存盘。返回的引用与本对象同生命周期。
	subtitle::SubtitlePrefs& Subtitles();
	const subtitle::SubtitlePrefs& Subtitles() const;

	// 消息记录窗口上次关闭时的位置与大小（配置里的 [HistoryDlg] 一节）。没有保存过时
	// m_hasSaved 为 false。打开窗口时怎么据此算出实际位置见 winplace::ResolveRestoredWindowRect。
	const winplace::WindowPlacementPrefs& GetHistoryDlgPlacement() const;
	// 记下消息记录窗口的位置与大小。只改内存里的值，由调用方负责调
	// CFlamingoClient::SaveUserConfig 存盘。
	void SetHistoryDlgPlacement(const winplace::WindowPlacementPrefs& placement);

	// 聊天记录管理器（消息记录窗左栏）上次关闭时的左栏宽度（像素），存在 [HistoryDlg] 的
	// ManagerPaneWidth 键。没有保存过时为 0，窗口按默认宽度显示
	int GetHistoryMgrPaneWidth() const;
	// 聊天记录管理器上次关闭时左栏是否折叠，存在 [HistoryDlg] 的 ManagerCollapsed 键
	bool IsHistoryMgrCollapsed() const;
	// 记下聊天记录管理器左栏的宽度与折叠状态。只改内存里的值，由调用方负责调
	// CFlamingoClient::SaveUserConfig 存盘。
	//   paneWidth：左栏宽度（像素）；不大于 0 时记为 0（下次按默认宽度）
	//   collapsed：左栏是否折叠
	void SetHistoryMgrPaneLayout(int paneWidth, bool collapsed);

	// 群聊窗「成员」页签上次选的视图，存在 [GroupDlg] 的 MemberView 键（docs/群成员部门树方案.md）：
	// 0 平铺、1 按部门显示的部门树；没有保存过、或值不认识时按平铺。所有群共用这一个设置
	bool IsGroupMemberTreeView() const;
	// 记下群聊窗「成员」页签的视图。只改内存里的值，由调用方负责调 CFlamingoClient::SaveUserConfig 存盘。
	//   treeView：true 部门树，false 平铺
	void SetGroupMemberTreeView(bool treeView);

	void SetFaceID(UINT uFaceID);
	UINT GetFaceID() const;

	void SetCustomFace(PCTSTR pszCustomFace);
	PCTSTR GetCustomFace() const;

	void EnableShowLastMsgInChatDlg(BOOL bEnable);
	BOOL IsEnableShowLastMsgInChatDlg();

	// ---- 系统设置（新版 balloonui 系统设置对话框）新增项 ----
	// 通用页
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

	//屏幕录制的画质：0 标准，1 高清（交给 FlamingoMedia.dll，见 docs/屏幕共享与录制方案.md 6.1）
	void SetRecordQuality(long quality);
	long GetRecordQuality() const;
	//屏幕录制的保存目录；为空表示用默认目录（「视频」文件夹下的 FlamingoRecords）
	void SetRecordFolder(LPCTSTR folder);
	LPCTSTR GetRecordFolder() const;
	//屏幕共享与录屏的鼠标效果设置（第 16 期），文本格式见 FlamingoMedia 的 SerializeMouseEffectSettings，形如
	//「halo=1;click=0;trail=0;halocolor=0;trailcolor=1」；为空表示没有改过，按默认设置（只开光圈）。存在 [Settings] MouseEffects
	void SetMouseEffects(LPCTSTR text);
	LPCTSTR GetMouseEffects() const;
	//是否启用录屏快捷键 Ctrl+Alt+R（开始 / 停止录屏），默认开启
	void EnableRecordHotkey(BOOL bEnable);
	BOOL IsEnableRecordHotkey() const;
	//截图全局快捷键，编码见 Feature/Screenshot/ScreenshotHotkey.h：高 16 位为 MOD_* 组合，低 16 位为虚拟键码，
	//0 表示停用。存在 [Settings] ScreenshotHotkey，缺省为 Alt+Z（docs/截图快捷键与隐藏当前窗口方案.md）
	void SetScreenshotHotkey(DWORD dwHotkey);
	DWORD GetScreenshotHotkey() const;
	//从单聊、群聊窗发起截图时，是否把该聊天窗排除在截图之外（截图时隐藏当前窗口），默认开启。
	//存在 [Settings] ScreenshotHideWindow
	//注意：2026-10-07 起截图快捷键与本项改存全局配置 config\flamingo.ini 的 [screenshot] 段、所有账号共用
	//（Feature/Screenshot/ScreenshotPrefs.h）。账号配置里的这两项只在登录时用作迁移来源，界面不再读写它们
	void EnableScreenshotHideWindow(BOOL bEnable);
	BOOL IsScreenshotHideWindow() const;
	// 通用页：主面板贴边自动隐藏。开启后主面板被拖到屏幕上、左、右边缘时吸附过去，
	// 鼠标离开后滑出屏幕只留一条细条，鼠标碰到细条再滑回来（见 BalloonMain/MainPanelDockController.h）。
	// 默认开启。关掉时若主面板正处于收起状态，会立即展开并取消置顶。
	void EnableDockAutoHide(BOOL bEnable);
	BOOL IsEnableDockAutoHide() const;
	// 聊天页
	void EnableAutoRecvImage(BOOL bEnable);			//自动接收图片
	BOOL IsEnableAutoRecvImage() const;
	void EnableAutoRecvFile(BOOL bEnable);			//自动接收文件
	BOOL IsEnableAutoRecvFile() const;
	void EnableMsgRoaming(BOOL bEnable);			//消息漫游（本期仅存、暂不联服务端）
	BOOL IsEnableMsgRoaming() const;
	void EnableLinkDetect(BOOL bEnable);			//自动识别聊天内容里的链接（默认开）
	BOOL IsEnableLinkDetect() const;
	// 聊天页：离开 / 忙碌时自动回复（[AutoReply] 段，2026-10-03 起）。两个开关各管一种状态，默认都关闭；
	// 两段文字是用户设置的原文，不含「[自动回复] 」前缀，可以为空 —— 为空时发送默认文字
	// （见 BalloonMain/AutoReplyPolicy.h 的 NormalizeAutoReplyText）。文字经 cfgtext::EncodeConfigText
	// 编码后存盘，表情等代码页里没有的字符不会丢。只改内存里的值，存盘由调用方负责。
	void EnableAutoReplyAway(BOOL bEnable);			//「离开时自动回复」开关
	BOOL IsEnableAutoReplyAway() const;
	void EnableAutoReplyBusy(BOOL bEnable);			//「忙碌时自动回复」开关
	BOOL IsEnableAutoReplyBusy() const;
	void SetAutoReplyAwayText(PCTSTR pszText);		//离开时回复的文字；NULL 按空串处理
	PCTSTR GetAutoReplyAwayText() const;
	void SetAutoReplyBusyText(PCTSTR pszText);		//忙碌时回复的文字；NULL 按空串处理
	PCTSTR GetAutoReplyBusyText() const;
	// 聊天页：快捷回复（[QuickReply] 段 Text1~Text3，2026-10-06 起，docs/快捷回复方案.md）。固定三条，下标 0 起。
	// 键不存在时取默认文字（随界面语言翻译）；键存在但为空表示用户特意清空了这一条，不在菜单里显示。
	// 文字经 cfgtext::EncodeConfigText 编码后存盘。只改内存里的值，存盘由调用方负责。
	static const int kQuickReplySlotCount = 3;		//快捷回复的条数
	CString GetQuickReply(int index) const;			//第 index 条的实际文字（没设过时为默认文字）；index 越界返回空串
	void SetQuickReply(int index, PCTSTR pszText);	//设置第 index 条，可以为空串（表示不显示）；NULL 按空串处理；越界时不做任何事
	BOOL IsQuickReplyCustomized(int index) const;	//第 index 条是否设过；FALSE 表示用默认文字；越界返回 FALSE
	void ResetQuickReplies();						//三条都恢复为默认文字（存盘时删掉三个键）
	static CString GetDefaultQuickReply(int index);	//第 index 条的默认文字（已按界面语言翻译）；越界返回空串
	// 发送键（BUG-119）：最近一次 LoadConfig 读到的账号配置里有没有 [ChatDlg] EnableEnterToSend。为 FALSE 说明是
	// 升级上来的老配置，发送键要用 flamingo.ini 里的旧值迁移一次（见 ChatSendMode）
	BOOL HasEnterToSendSetting() const;
	// 主面板搜索框的最近搜索（[Search] 段，2026-10-04 起）：一行文字，关键字之间用 U+001F 隔开，
	// 格式由 BalloonSearch/RecentSearchList.h 的 Serialize / Deserialize 负责。经 cfgtext::EncodeConfigText
	// 编码后存盘。只改内存里的值，存盘由调用方负责。
	void SetRecentSearches(PCTSTR pszText);			//最近搜索的那一行文字；NULL 按空串处理
	PCTSTR GetRecentSearches() const;
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
	BOOL	m_bEnableRevokeChatMsg; //是否启用消息撤回

	//主面板的位置与大小，存在 [MainPanel] 段；LoadConfig 时整体重建，用户拖动或拉伸主面板后更新
	winplace::WindowPlacementPrefs	m_mainPanelPlacement;

	BOOL    m_bEnableExitPrompt;					//退出是否提示
	BOOL	m_bExitWhenCloseMainDlg;				//关闭主对话框时退出程序（还是最小化到托盘）
	BOOL	m_bTrayMinimizeTipShown;				//"已最小化到托盘"气泡提示是否已弹过（每账号一次，随 .ini 持久化，一经置位不再复位）

	long	m_nBuddyListHeadPicStyle;				//好友列表中头像风格（大头像、小头像、标准头像）
	BOOL	m_bBuddyListShowBigHeadPicInSel;		//当显示模式是小头像时，选中某个头像是否显示大头像

	long	m_nNameStyleIndex;						//0显示昵称和账号、1显示昵称、2显示账号
	std::string m_strCodeSnippetLang;				//「发送代码」窗口上次手选的语言标识；空串表示没有手选过（默认自动识别）
	BOOL	m_bCodeComposeLineNumbers;				//「发送代码」窗口是否显示行号，默认 TRUE
	BOOL	m_bCodeNotesPaneOpen;					//代码有批注时批注栏是否展开，默认 TRUE
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
	//聊天字幕模式的偏好（外观 + 各会话的字幕位置），存在 [Subtitle] 与 [SubtitlePlacement] 段；
	//LoadConfig 时整体重建，字幕被拖动、调宽、锁定或改外观时更新
	subtitle::SubtitlePrefs	m_subtitlePrefs;
	//消息记录窗口的位置与大小，存在 [HistoryDlg] 段；LoadConfig 时整体重建，窗口关闭时更新
	winplace::WindowPlacementPrefs	m_historyDlgPlacement;
	//聊天记录管理器左栏的宽度（像素，0 表示用默认宽度）与是否折叠，存在 [HistoryDlg] 段；
	//LoadConfig 时读入，窗口关闭时更新
	long	m_nHistoryMgrPaneWidth;
	BOOL	m_bHistoryMgrCollapsed;
	//群聊窗「成员」页签的视图：0 平铺、1 部门树，存在 [GroupDlg] 的 MemberView 键；LoadConfig 时读入，切换时更新
	long	m_nGroupMemberView;

	BOOL	m_bShowLastMsg;

	// ---- 系统设置（新版 balloonui 系统设置对话框）新增项，持久化在 ini 的 [Settings] 节 ----
	BOOL	m_bNewMsgNotify;		//新消息提醒总开关
	BOOL	m_bDesktopPopup;		//新消息桌面气泡弹窗
	BOOL	m_bShakeFlashIcon;		//震动 / 闪烁托盘图标提醒
	BOOL	m_bDoNotDisturb;		//勿扰时段（开启后时段内不提醒）；本期仅存开关、时段固定文案
	BOOL	m_bNotifyAfterLock;		//锁屏后是否继续通知
	BOOL	m_bAutoAway;			//电脑无操作时是否自动切到"离开"（默认开）
	long	m_nAutoAwayDelayIndex;	//自动离开的等待时长下拉索引（0=5分钟 1=10 2=15 3=30），映射见 SettingsBinding.h
	long	m_nRecordQuality;		//屏幕录制的画质：0 标准，1 高清
	CString	m_strRecordFolder;	//屏幕录制的保存目录；为空表示默认目录
	CString	m_strMouseEffects;	//屏幕共享与录屏的鼠标效果设置文本（第 16 期）；为空表示没有改过，按默认设置
	BOOL	m_bRecordHotkey;		//是否启用录屏快捷键 Ctrl+Alt+R
	DWORD	m_dwScreenshotHotkey;	//截图全局快捷键的编码（高 16 位 MOD_*，低 16 位虚拟键码）；0 表示停用
	BOOL	m_bScreenshotHideWindow;	//从聊天窗发起截图时是否把该聊天窗排除在截图之外
	BOOL	m_bKeepAwake;			//禁止机器休眠（不休眠 / 不关闭显示器 / 不锁屏）；默认关闭，与自动离开互斥
	BOOL	m_bDockAutoHide;		//主面板贴边自动隐藏（默认开）
	BOOL	m_bAutoRecvImage;		//聊天自动接收图片
	BOOL	m_bAutoRecvFile;		//聊天自动接收文件
	BOOL	m_bMsgRoaming;			//消息漫游（多设备同步）；本期仅存、暂不联服务端
	BOOL	m_bLinkDetect;			//自动识别聊天内容里的链接（关掉后链接以普通文字显示）
	BOOL	m_bAutoReplyAway;		//离开时是否自动回复单聊消息；默认关闭
	BOOL	m_bAutoReplyBusy;		//忙碌时是否自动回复单聊消息；默认关闭
	CString	m_strAutoReplyAwayText;	//离开时回复的文字（原文、不含前缀）；空表示发送默认文字
	CString	m_strAutoReplyBusyText;	//忙碌时回复的文字（原文、不含前缀）；空表示发送默认文字
	CString	m_strQuickReplies[kQuickReplySlotCount];	//三条快捷回复的原文；对应的 m_bQuickReplySet 为 FALSE 时不用这里的值
	BOOL	m_bQuickReplySet[kQuickReplySlotCount];		//每条快捷回复是否设过（配置里有这个键）；FALSE 表示用默认文字
	BOOL	m_bEnterToSendLoaded;	//LoadConfig 时 [ChatDlg] EnableEnterToSend 是否存在；SetEnablePressEnterToSend 之后视为存在
	CString	m_strRecentSearches;	//主面板搜索框的最近搜索，关键字之间用 U+001F 隔开；空表示没有记录
	long	m_nAudioInputIndex;		//麦克风设备下拉索引；本期仅存、A/V 未接入暂不生效
	long	m_nAudioOutputIndex;	//扬声器设备下拉索引；同上
	long	m_nCameraIndex;			//摄像头设备下拉索引；同上
	BOOL	m_bEchoCancel;			//回声消除；同上
	BOOL	m_bTwoFactor;			//二次验证开关；本期仅存、服务端未接入
	CString	m_strRecvFilePath;		//接收文件保存目录；空=用默认 我的文档\Flamingo\FileRecv\账号

};
