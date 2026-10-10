/**
 *  BalloonSystemSettingDlg.cpp —— "系统设置"对话框实现。
 *
 *  控件树用 balloonui 现成控件程序化组合：根 DuiHBox = 左 CSettingsNavList（圆角
 *  胶囊导航）+ 右 DuiVBox 内容容器；内容容器里叠放 7 个分页 DuiVBox，每页 weight
 *  撑满，初始仅第 0 页可见。导航选中变化（DUIN_VALUECHANGED）时 ShowPage 切换
 *  分页可见性并 ForceLayout 重排 —— DuiVBox 布局自动跳过隐藏子，故"同一时刻只
 *  显示一页"无需自写 deck 容器。
 *
 *  通用 / 消息通知 / 聊天三页为真实设置项：每行由 BuildRowTextBlock（左标题+副标题）
 *  + 右侧 DuiSwitch / DuiComboBox 组成，行间用 DuiSeparator 分隔。初始值从
 *  CUserConfig（开机自启动从注册表 Run 项）读取；任一项变更即写回配置并
 *  SaveUserConfig 即时持久化（设计无保存按钮，改即生效）。下拉/开关与配置之间
 *  的转换走 SettingsBinding 纯函数。其余四页本期仍为占位。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"
#include "BalloonSystemSettingDlg.h"

#include <memory>

#include <mmsystem.h>                     // waveIn/waveOut 设备枚举（麦克风/扬声器）
#include <shlobj.h>                       // SHGetSpecialFolderPath（文件保存路径）
#include <gdiplus.h>                      // 圆角分段条 / 键帽 chip 自绘（抗锯齿）
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdiplus.lib")

#include "../../Common/Log/IULog.h"
#include "../../App/FlamingoClient.h"            // CFlamingoClient::m_UserConfig / SaveUserConfig
#include "../../Session/UserSessionData.h"           // FMG_MSG_OPEN_CHANGEPWD / FMG_MSG_SET_AUTO_LOGIN（转发主面板）、g_szHomePath / g_szDataRoot
#include "../../Common/Util/IniFile.h"                  // CIniFile（数据存储位置存在全局 flamingo.ini 里）
#include "../../Common/Log/LogCleanupPolicy.h"         // 「启动时清空历史日志」开关的读写与默认值
#include "../../Common/I18n/I18n.h"                     // TR：界面文字的多语言查找
#include "../../Common/I18n/I18nLayout.h"               // DefaultButtonWidthForText：按钮按译文宽度只增不减
#include "../../Common/I18n/LanguageSetting.h"          // 「语言」下拉框：选项、[ui] language 的读写、是否需要重启
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // DuiToolTipMgr：行文字放不下时的悬停提示
#include "../../Common/Util/EncodeUtil.h"               // 语言代码转 UTF-8 写日志
#include "../../App/BrandText.h"                        // g_strAppTitle：重启提示里的产品名称
#include "BalloonCacheCleanDlg.h"        // 「立即清理」弹出的清理对话框
#include "SettingsNavList.h"
#include "../../Feature/ScreenShare/RecordEntry.h"   // 录屏设置交给 FlamingoMedia.dll、注册录屏快捷键
#include "../../Feature/Screenshot/ScreenshotEntry.h"   // 截图快捷键：校验、注册、录入期间暂停本程序的全局快捷键
#include "../BalloonMain/MainPanelSessionController.h"   // 「打开主面板」快捷键的主键 kShowPanelHotKey（键帽文字由它生成）
#include "HotkeyCaptureBox.h"                           // 「快捷键」页可录入的截图键帽
#include "SettingsPageModel.h"
#include "SettingsBinding.h"
#include "ChatFontPreview.h"              // 「默认聊天字体」预览标签的外观
#include "AutoReplySettingsRows.h"        // 「离开 / 忙碌时自动回复」两行右侧的输入框与开关
#include "../BalloonChatWnd/ChatSendMode.h"   // 发送键经 ChatSendMode 读写本人账号配置（BUG-119）
#include "../BalloonChatWnd/ChatSendMenu.h"   // kQuickReplyMaxChars：快捷回复输入框的字数上限
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"   // 读自动回复输入框的文字
#include "../BalloonMain/AutoReplyPolicy.h"   // 两段回复的默认文字（输入框为空时的占位提示）
#include "../BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏左侧图标）
#include "../BalloonMain/AutoReplySync.h"  // BuildAutoReplyWire：自动回复设置上传服务端（docs/机器人方案.md §11 Q2）
#include "../../net/IUSocket.h"            // CIUSocket::SendSetAutoReply
#include "../../Feature/Bot/BotUiBridge.h" // botui::IsAvailable / OpenMyBots：「我的机器人」一行
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"        // DuiVBox / DuiHBox
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiBadge.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"

// ---- 对话框尺寸（逻辑像素）---------------------------------------------
// 含标题栏的整窗初始尺寸（逻辑像素）。2026-10-02 修正 ResizeClient 的窗口尺寸时由 720×500 加上
// 修复前 ResizeClient 多叠加的 18×47（120 DPI 实测），保持修复前的窗口大小。
static const int kDlgClientW  = 738;   // 初始宽（较初版收紧，去掉过宽留白）
// 初始高。2026-10-03 聊天页加了「离开 / 忙碌时自动回复」：两个开关行，以及各自下方单独一行的多行
// 输入框（各占 balloonsettings::kAutoReplyEditRowH = 66），由 547 加到 685。2026-10-05 聊天页又加了
// 「截图时隐藏当前窗口」一行（52 + 1 条分隔线），由 685 加到 738。聊天页内容合计
// 10 行 × kRowH(52) + 2 × 66 + 9 条分隔线 = 661，加标题栏 40 与内容区上下内距 24 为 725，余 13 留作余量。
// 2026-10-06 聊天页又加了「快捷回复」一组（docs/快捷回复方案.md §3.5）：标题行 52 + 并排三个输入框一行
// （kQuickReplyEditH 30 + 下方间距 10）+ 1 条分隔线 = 93，由 738 加到 831。
// 2026-10-08 聊天页在自动回复下方加了「我的机器人」一行（52 + 1 条分隔线），由 831 加到 884；
// 这一行不出现时（机器人插件不可用或企业没有允许）页尾多出的空白由末尾的弹性占位吸收
static const int kDlgClientH  = 884;
static const int kDlgMinW     = 620;   // 最小宽
static const int kDlgMinH     = 420;   // 最小高
static const int kTitleBarH   = 40;    // 标题栏高度

// 标题栏左侧图标的位图边长（像素）。标题栏最终按 16x16 绘制，直接生成 16 可免去缩放。
static const int kTitleIconSize = 16;
// 图标合成用的底色，必须与标题栏实际显示的颜色一致，否则图标周围会出现一块异色。
// 本对话框没有调 SetTitleBarTransparent，标题栏由 balloonui 画成不透明的
// RGB(245,246,250)（见 balloonui/Controls/Window/DuiFrameWindow.cpp 的 Title strip
// background 一段），故取同值。缘由详见 BalloonMain/TitleBarIcon.h。
static const COLORREF kTitleBarBg = RGB(245, 246, 250);

// ---- 动画的推进方式 ----------------------------------------------------
// 开关（DuiSwitch）等动画控件鼠标翻转时走 150ms 滑动动画，由 DuiAnimMgr / DuiDoubleAnim
// 推进。本对话框不必为此挂定时器：DuiAnimMgr 自 2026-08-14 起自带脉冲定时器（见
// balloonui/DuiAnimation.cpp 的 StartPulse / PulseProc），有动画在跑时它自己按 16ms 发
// WM_TIMER，跑完即停。它用的是线程定时器（SetTimer 的 hwnd 传 NULL），任何转发未过滤
// 消息的消息循环都能驱动它。

// ---- 左右分区（逻辑像素）-----------------------------------------------
static const int kNavWidth    = 156;   // 左侧导航固定宽

// ---- 右侧内容区内距（逻辑像素）----------------------------------------
static const int kContentPadL = 24;    // 内容区左内距
static const int kContentPadT = 12;    // 内容区上内距
static const int kContentPadR = 24;    // 内容区右内距
static const int kContentPadB = 12;    // 内容区下内距

// ---- 设置行排版（逻辑像素）--------------------------------------------
static const int kRowH        = 52;    // 一行内容高度（含标题/副标题垂直居中，较初版收紧）
static const int kRowDividerH = 1;     // 行分隔线高
static const int kQuickReplyEditH         = 30;   // 快捷回复单行输入框的高度（像素）
static const int kQuickReplyEditGap       = 8;    // 并排的三个快捷回复输入框之间的间距（像素）
static const int kQuickReplyEditBottomGap = 10;   // 快捷回复输入框下方到本组分隔线的间距（像素），与自动回复一组相同
static const int kRowGap      = 12;    // 行内左文字块与右控件的间距
static const int kRowTitleH   = 22;    // 行标题文字高
static const int kRowSubH     = 18;    // 行副标题文字高
static const int kRowTextGap  = 2;     // 标题与副标题间距
static const int kSwitchW     = 46;    // 开关宽
static const int kSwitchH     = 24;    // 开关高
static const int kComboW      = 200;   // 下拉框宽
static const int kComboWideW  = 280;   // 宽下拉框（音视频设备名较长，避免截断）
static const int kComboH      = 34;    // 下拉框高

// ---- 占位分页排版（逻辑像素）------------------------------------------
static const int kPageTitleH  = 30;    // 页标题行高
static const int kPageNoteH   = 22;    // 提示行高
static const int kPageGap     = 6;     // 页标题与提示间距

// ---- 配色 --------------------------------------------------------------
static const COLORREF kPageTitleClr = RGB(0x22, 0x22, 0x26);   // 占位页标题深色
static const COLORREF kPageNoteClr  = RGB(0x9A, 0x9A, 0xA0);   // 占位页提示灰
static const COLORREF kRowTitleClr  = RGB(0x2A, 0x2A, 0x2E);   // 行标题深色
static const COLORREF kRowSubClr    = RGB(0x9A, 0x9A, 0xA0);   // 行副标题灰
static const COLORREF kDividerClr   = RGB(0xEE, 0xEE, 0xF0);   // 行分隔线浅灰
static const COLORREF kAccentOrange = RGB(0xF5, 0x6A, 0x3C);   // 开关打开态品牌橙
static const COLORREF kValueClr     = RGB(0x66, 0x66, 0x6C);   // 只读取值文本灰

// ---- 按钮行（逻辑像素 / 配色）-----------------------------------------
static const int kBtnWidth   = 88;     // 普通按钮宽
static const int kChatFontPreviewW = 120;   // 「默认聊天字体」预览文字的宽度：放得下常用字号的"Aa 字体预览"，再宽会把左侧副标题挤得截断
static const int kBtnHeight  = 32;     // 按钮高

// ---- 键帽 chip（逻辑像素 / 配色）--------------------------------------
static const int kKeyCapH      = 26;   // 键帽高
static const int kKeyCapRadius = 6;    // 键帽圆角
static const COLORREF kKeyCapBg     = RGB(0xF3, 0xF3, 0xF6);  // 键帽浅灰底
static const COLORREF kKeyCapText   = RGB(0x55, 0x55, 0x5C);  // 键帽文字深灰
static const COLORREF kKeyCapBorder = RGB(0xDD, 0xDD, 0xE3);  // 键帽细描边（比底略深）
static const int      kKeyCapPadX   = 12;   // 键帽文字左右内距（贴文字宽 + 呼吸感）
static const int      kKeyCapFontPx = 12;   // 键帽等宽字号
static const TCHAR* const kKeyCapFont = _T("Consolas");   // 键帽等宽字体（设计稿"代码块"观感）

// ---- 「自动离开」那行的两句副标题（正常态 / 被禁止休眠压制时）----------
// 分别用于 ApplyKeepAwakeExclusion 的两条分支，写在一处免得两边文案走样。
static const TCHAR* const kAutoAwaySubtitleNormal =
    TR_NOOP(L"电脑无操作时自动切到离开状态，一有操作再自动切回在线");
static const TCHAR* const kAutoAwaySubtitleBlocked =
    TR_NOOP(L"已开启「禁止机器休眠」，自动离开暂不生效");

// ---- 控件 id（对话框内唯一即可；设置项按页分段编号）-------------------
// 重要：DuiFrameWindow 内部 min/max/close 标题按钮固定占用 ctrlId 1/2/3
// （见 DuiFrameWindow::OnDuiChildNotify，按 ctrlId 1→最小化 / 2→最大化 / 3→关闭
// 分发 SC_*）。客户区任何控件<u>都不能</u>用 1/2/3，否则其冒泡的 DUIN_CLICK 会
// 被标题栏当成按钮点击 —— 导航列表曾用 1，导致点击导航即触发窗口最小化。
enum SettingCtrlId
{
    kIdNavList         = 10,    // 左侧导航列表（避开标题按钮保留的 1/2/3）

    // 通用页
    kIdAutoStart       = 101,   // 开机自启动（落注册表 Run，不入配置）
    kIdCloseAction     = 102,   // 关闭主面板时（下拉）→ ExitWhenCloseMainDlg
    kIdLanguage        = 103,   // 语言（下拉，本期仅存）
    kIdAutoAway        = 105,   // 无操作自动切到"离开"（开关）→ EnableAutoAway
    kIdAutoAwayDelay   = 106,   // 自动离开的等待时长（下拉）→ SetAutoAwayDelayIndex
    kIdKeepAwake       = 107,   // 禁止机器休眠（开关）→ EnableKeepAwake；开启时把上面两项置灰
    kIdAutoAwaySubtitle = 108,  // 「自动离开」那行的副标题（置灰时要换成解释文案，故给它一个 id）
    kIdDockAutoHide    = 109,   // 主面板贴边自动隐藏（开关）→ EnableDockAutoHide
    //110 原为「备忘与提醒」的「打开…」按钮，2026-10-03 入口移到主面板系统菜单后不再使用

    // 消息通知页
    kIdNewMsgNotify    = 201,   // 新消息提醒
    kIdDesktopPopup    = 202,   // 桌面弹窗
    kIdSound           = 203,   // 提示音（= 非静音）
    kIdShakeFlash      = 204,   // 震动闪烁图标
    kIdDoNotDisturb    = 205,   // 勿扰时段
    kIdNotifyAfterLock = 206,   // 锁屏后通知

    // 聊天页
    kIdSendKey         = 301,   // 发送消息快捷键（下拉）→ PressEnterToSend
    kIdAutoRecvImage   = 302,   // 自动接收图片
    kIdAutoRecvFile    = 303,   // 自动接收文件
    kIdMsgRoaming      = 304,   // 消息漫游
    kIdLinkDetect      = 306,   // 自动识别聊天中的链接
    kIdChatFontPreview = 307,   // 默认聊天字体：预览文字（标签）
    kIdChatFontEdit    = 308,   // 默认聊天字体：「修改」按钮，打开字体浮层
    kIdChatFontReset   = 309,   // 默认聊天字体：「恢复默认」按钮，清除全局默认
    kIdChatClean       = 310,   // 清理聊天记录：「清理…」按钮，打开按时间范围清理的对话框
    kIdAutoReplyAway     = 311, // 离开时自动回复（开关）→ EnableAutoReplyAway；关闭时把右侧输入框置灰
    kIdAutoReplyAwayText = 312, // 离开时回复的文字（单行输入框）→ SetAutoReplyAwayText，失去焦点或关窗时存盘
    kIdAutoReplyBusy     = 313, // 忙碌时自动回复（开关）→ EnableAutoReplyBusy；关闭时把右侧输入框置灰
    kIdAutoReplyBusyText = 314, // 忙碌时回复的文字（单行输入框）→ SetAutoReplyBusyText，失去焦点或关窗时存盘
    kIdScreenshotHideWindow = 315, // 截图时隐藏当前窗口（开关）→ EnableScreenshotHideWindow，与聊天窗 ▾ 菜单里的勾选项是同一个配置
    kIdQuickReplyReset   = 316, // 快捷回复：「恢复默认」按钮 → ResetQuickReplies，立即存盘并刷新三个输入框（docs/快捷回复方案.md）
    kIdQuickReplyText1   = 317, // 快捷回复第 1 条（单行输入框）→ SetQuickReply(0)，失去焦点或关窗时存盘
    kIdQuickReplyText2   = 318, // 快捷回复第 2 条（单行输入框）→ SetQuickReply(1)，同上
    kIdQuickReplyText3   = 319, // 快捷回复第 3 条（单行输入框）→ SetQuickReply(2)，同上
    kIdMyBots            = 320, // 我的机器人：「打开…」按钮，打开 FlamingoBot.dll 的「我的机器人」窗口（docs/机器人方案.md §4.3）

    // 音视频页
    kIdMicDevice       = 401,   // 麦克风设备（下拉）
    kIdSpeakerDevice   = 402,   // 扬声器设备（下拉）
    kIdCameraDevice    = 403,   // 摄像头设备（下拉）
    kIdMicTest         = 404,   // 麦克风测试（按钮，占位）
    kIdEchoCancel      = 405,   // 回声消除（开关）
    kIdRecordQuality   = 406,   // 录屏画质（下拉：标准 / 高清）→ SetRecordQuality，并交给 FlamingoMedia.dll
    kIdRecordFolder    = 407,   // 录屏保存位置：「更改」按钮，弹选目录框
    kIdRecordHotkey    = 408,   // 录屏快捷键 Ctrl+Alt+R（开关）→ EnableRecordHotkey，并立即注册或注销

    // 存储页
    kIdClearCache      = 501,   // 清理本地缓存（按钮，占位）
    kIdChangeRecvPath  = 502,   // 更改文件保存位置（按钮，弹选目录框）
    kIdClearLogOnStart = 503,   // 启动时清空历史日志（开关，落 flamingo.ini，不入 CUserConfig）

    // 账号安全页
    kIdManageDevices   = 601,   // 登录设备管理（按钮，占位）
    kIdChangePwd       = 602,   // 修改密码（按钮，转发主面板改密对话框）
    kIdTwoFactor       = 603,   // 二次验证（开关，占位）
    kIdLoginLog        = 604,   // 登录日志（按钮，占位）
    kIdDeactivate      = 605,   // 注销账号（按钮，危险，占位）
    kIdAutoLogin       = 606,   // 自动登录（开关，转交主面板修改本机历史账号列表，不入 CUserConfig）

    // 快捷键页
    kIdShotHotkeyBox    = 701,  // 截图快捷键的可录入键帽（CHotkeyCaptureBox）→ SetScreenshotHotkey，并立即重新注册
    kIdShotHotkeyReset  = 702,  // 截图快捷键的「恢复默认」按钮：恢复为 Alt+Z
    kIdShotHotkeyStatus = 703,  // 「截图」一行的提示文字（录入说明、不合规原因、被占用提示），事后要换文案故给 id
};

// 「截图」一行提示文字出错时的颜色（红）
static const COLORREF kRowErrorClr = RGB(0xE5, 0x48, 0x4D);

// 录屏画质的下拉选项（与 CUserConfig::GetRecordQuality 的取值一一对应：0 标准，1 高清）
static const wchar_t* const kRecordQualityOptions[] = { TR_NOOP(L"标准"), TR_NOOP(L"高清") };
static const int kRecordQualityOptionCount = sizeof(kRecordQualityOptions) / sizeof(kRecordQualityOptions[0]);

// =======================================================================
// 开机自启动 —— 注册表 Run 项读写（HKCU，避免需要管理员权限）
// =======================================================================

// HKCU 下开机自启动项的子键路径与值名。值名用程序名，便于在注册表里辨认。
static const TCHAR* const kRunSubKey    = _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run");
static const TCHAR* const kRunValueName = _T("FlamingoIM");

// 查询开机自启动是否已开启（Run 项下存在本程序的值即视为已开启）。
static bool AutoStartIsEnabled()
{
    HKEY hKey = NULL;
    if (::RegOpenKeyEx(HKEY_CURRENT_USER, kRunSubKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
    {
        return false;
    }
    DWORD dwType = 0;
    DWORD cbData = 0;
    LONG  r = ::RegQueryValueEx(hKey, kRunValueName, NULL, &dwType, NULL, &cbData);
    ::RegCloseKey(hKey);
    return (r == ERROR_SUCCESS && cbData > 0);
}

// 开启 / 关闭开机自启动：开启时写入本 exe 全路径（带引号，含空格安全），关闭时删值。
static void AutoStartSetEnabled(bool enable)
{
    HKEY hKey = NULL;
    if (::RegCreateKeyEx(HKEY_CURRENT_USER, kRunSubKey, 0, NULL, 0,
                         KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
    {
        return;
    }

    if (enable)
    {
        TCHAR szExePath[MAX_PATH] = { 0 };
        ::GetModuleFileName(NULL, szExePath, MAX_PATH);
        CString strQuoted;
        strQuoted.Format(_T("\"%s\""), szExePath);
        ::RegSetValueEx(hKey, kRunValueName, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(strQuoted.GetString()),
                        (DWORD)((strQuoted.GetLength() + 1) * sizeof(TCHAR)));
    }
    else
    {
        ::RegDeleteValue(hKey, kRunValueName);
    }

    ::RegCloseKey(hKey);
}

// =======================================================================
// 音视频设备枚举（winmm；麦克风 waveIn / 扬声器 waveOut）
// =======================================================================

// 把麦克风设备名逐个填进 combo；无设备时填一项占位。返回填入项数（至少 1）。
static int FillMicDevices(balloonwjui::DuiComboBox* combo)
{
    UINT n = ::waveInGetNumDevs();
    int added = 0;
    for (UINT i = 0; i < n; ++i)
    {
        WAVEINCAPS caps;
        ::ZeroMemory(&caps, sizeof(caps));
        if (::waveInGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
        {
            combo->AddString(caps.szPname);
            ++added;
        }
    }
    if (added == 0)
    {
        combo->AddString(TR(L"无可用麦克风"));
        added = 1;
    }
    return added;
}

// 扬声器设备同理。
static int FillSpeakerDevices(balloonwjui::DuiComboBox* combo)
{
    UINT n = ::waveOutGetNumDevs();
    int added = 0;
    for (UINT i = 0; i < n; ++i)
    {
        WAVEOUTCAPS caps;
        ::ZeroMemory(&caps, sizeof(caps));
        if (::waveOutGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
        {
            combo->AddString(caps.szPname);
            ++added;
        }
    }
    if (added == 0)
    {
        combo->AddString(TR(L"无可用扬声器"));
        added = 1;
    }
    return added;
}

// 取本地数据根目录（我的文档\Flamingo）。失败返回空串。
static CString GetFlamingoDataDir()
{
    TCHAR szDoc[MAX_PATH] = { 0 };
    if (!::SHGetSpecialFolderPath(NULL, szDoc, CSIDL_MYDOCUMENTS, FALSE))
    {
        return CString();
    }
    CString dir(szDoc);
    dir += _T("\\Flamingo");
    return dir;
}

// =======================================================================
// 自绘小控件（键帽 chip）—— balloonui 的 DuiBadge 不支持字体/边框，故自绘 DuiControl
// =======================================================================

namespace {

// COLORREF → 不透明 Gdiplus::Color。
Gdiplus::Color GdiColor(COLORREF c)
{
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

// 用四段 90° 圆弧 + 闭合，向 path 添加一个圆角矩形（GDI+ 浮点版）。
void AddRoundRectF(Gdiplus::GraphicsPath& path,
                   Gdiplus::REAL x, Gdiplus::REAL y,
                   Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f)
    {
        r = w / 2.0f;
    }
    if (r > h / 2.0f)
    {
        r = h / 2.0f;
    }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

// 键帽 chip：圆角浅灰底 + 细描边 + 等宽字体文字，贴文字宽度（由 MeasureWidth 算）、
// 右对齐摆放（设计稿"代码块"观感）。balloonui 的 DuiBadge 不支持字体 / 描边，故自绘。
class KeyCapChip : public balloonwjui::DuiControl
{
public:
    void SetText(LPCTSTR text)
    {
        m_text = (text != NULL) ? text : _T("");
        Invalidate();
    }

    // 量出贴文字所需的 chip 宽度（文字宽 + 左右内距），供布局给 Fixed 宽度用。
    static int MeasureWidth(LPCTSTR text)
    {
        if (text == NULL || text[0] == _T('\0'))
        {
            return kKeyCapPadX * 2;
        }
        HDC   hdc  = ::GetDC(NULL);
        HFONT font = MakeFont();
        HFONT old  = (HFONT)::SelectObject(hdc, font);
        SIZE  sz   = { 0, 0 };
        ::GetTextExtentPoint32(hdc, text, (int)::lstrlen(text), &sz);
        ::SelectObject(hdc, old);
        ::DeleteObject(font);
        ::ReleaseDC(NULL, hdc);
        return sz.cx + kKeyCapPadX * 2;
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int x = m_rcItem.left;
        const int y = m_rcItem.top;
        const int w = m_rcItem.right  - m_rcItem.left;
        const int h = m_rcItem.bottom - m_rcItem.top;
        if (w <= 0 || h <= 0)
        {
            return;
        }

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 内缩 0.5px 让 1px 描边完整落在控件矩形内。
        Gdiplus::GraphicsPath path;
        AddRoundRectF(path, (Gdiplus::REAL)x + 0.5f, (Gdiplus::REAL)y + 0.5f,
                      (Gdiplus::REAL)w - 1.0f, (Gdiplus::REAL)h - 1.0f,
                      (Gdiplus::REAL)kKeyCapRadius);
        Gdiplus::SolidBrush bg(GdiColor(kKeyCapBg));
        g.FillPath(&bg, &path);
        Gdiplus::Pen border(GdiColor(kKeyCapBorder), 1.0f);
        g.DrawPath(&border, &path);

        HFONT    font = MakeFont();
        HFONT    old  = (HFONT)::SelectObject(hdc, font);
        int      oldBk  = ::SetBkMode(hdc, TRANSPARENT);
        COLORREF oldClr = ::SetTextColor(hdc, kKeyCapText);
        RECT rc = m_rcItem;
        ::DrawText(hdc, m_text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ::SetTextColor(hdc, oldClr);
        ::SetBkMode(hdc, oldBk);
        ::SelectObject(hdc, old);
        ::DeleteObject(font);
    }

private:
    // 建一支键帽用的等宽字体（caller 负责 DeleteObject）。
    static HFONT MakeFont()
    {
        return ::CreateFont(-kKeyCapFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, kKeyCapFont);
    }

private:
    CString m_text;   // 键帽文字（如 "Ctrl + Alt + F"）
};

} // anonymous namespace

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonSystemSettingDlg::CBalloonSystemSettingDlg(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_built(false)
    , m_curPage(-1)
    , m_ownerHwnd(NULL)
    , m_autoLoginOn(false)
    , m_autoReplyTextDirty(false)
    , m_quickReplyDirty(false)
    , m_shotHotkeyCapturing(false)
    , m_pNav(NULL)
    , m_pContentHost(NULL)
    , m_pRecvPathLabel(NULL)
    , m_pRecordPathLabel(NULL)
    , m_hTitleIcon(NULL)
    , m_cacheCleanDlg(pFMGClient)
    , m_serverConnected(true)
    , m_hChatFontPreview(NULL)
{
    // 「默认聊天字体」的字体浮层：只装"变化"回调，不装"使用默认字体"回调 —— 这里编辑的
    // 就是全局默认本身，浮层上因而没有那个按钮。
    m_chatFontPopup.SetChangeCallback(&CBalloonSystemSettingDlg::OnDefaultChatFontChanged, this);
}

CBalloonSystemSettingDlg::~CBalloonSystemSettingDlg()
{
    // SetIcon 只借用位图、不负责释放，故由本类收尾。
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
    // 预览标签只借用字体句柄，由本类释放。
    if (m_hChatFontPreview != NULL)
    {
        ::DeleteObject(m_hChatFontPreview);
        m_hChatFontPreview = NULL;
    }
}

// =======================================================================
// 打开 / 关闭
// =======================================================================

void CBalloonSystemSettingDlg::OpenSettings(HWND owner)
{
    // 记住宿主（主面板）HWND："账号安全"页"修改密码"按钮据此转发，复用面板改密对话框。
    m_ownerHwnd = owner;

    if (!IsWindow())
    {
        // owner 传 NULL：让子控件通知自路由回本对话框（与修改密码对话框同处理）。
        if (Create(NULL, CWindow::rcDefault, TR(L"系统设置"),
                   WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR(_T("系统设置对话框 Create 失败。"));
            return;
        }
        SetTitle(TR(L"系统设置"));
        SetTitleBarHeight(kTitleBarH);

        // 标题栏左侧图标：程序主图标合成到标题栏底色上再挂，与主面板 / 群管理窗
        // 等窗口同一套做法（合成的缘由见 TitleBarIcon.h）。
        m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBg);
        if (m_hTitleIcon != NULL)
        {
            SetIcon(m_hTitleIcon);
        }
        else
        {
            LOG_WARNING(_T("系统设置对话框标题栏图标位图创建失败，标题栏只显示文字。"));
        }
        //没有所有者、会出现在任务栏：任务栏与 Alt+Tab 也用程序图标（2026-10-07，UI 约定第 10 条）
        m_taskIcons.ApplyTaskIcons(m_hWnd);
        SetButtons(false, false, true);   // 只留关闭按钮
        SetMinSize(kDlgMinW, kDlgMinH);

        m_built = false;
        BuildUiOnce();
        ResizeClient(kDlgClientW, kDlgClientH);
        CenterWindow();
    }

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO(_T("系统设置对话框已打开。"));
}

void CBalloonSystemSettingDlg::OpenSettings(HWND owner, int page)
{
    OpenSettings(owner);
    if (!IsWindow())
    {
        return;
    }
    const int target = balloonsettings::ClampNavSelection(page, (int)m_pages.size());
    //左侧导航与右侧内容区一起切：导航只改选中项、不发通知，内容区由 ShowPage 切换并重排
    if (m_pNav != NULL)
    {
        m_pNav->SetCurSel(target, false);
    }
    ShowPage(target);
    LOG_INFO("Settings: opened at page %d.", target);
}

void CBalloonSystemSettingDlg::SetAutoLoginSwitchState(bool on)
{
    m_autoLoginOn = on;

    //窗口尚未创建：建页时 BuildAccountPage 会按 m_autoLoginOn 设置开关初始状态
    if (!IsWindow())
    {
        return;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiSwitch* sw =
        dynamic_cast<balloonwjui::DuiSwitch*>(content->FindCtrlById(kIdAutoLogin));
    if (sw != NULL)
    {
        //不发通知：这里只是把界面同步到已生效的状态，若发出 DUIN_VALUECHANGED，
        //ApplySettingChange 会再向主面板投递一次同值的设置请求
        sw->SetChecked(on, /*animated*/false, /*notify*/false);
    }
}

void CBalloonSystemSettingDlg::SetServerConnected(bool connected)
{
    m_serverConnected = connected;
    //清理对话框未打开时 SetConnected 内部直接返回，下次打开时按 m_serverConnected 初始化
    m_chatCleanDlg.SetConnected(connected);
}

void CBalloonSystemSettingDlg::OpenChatCleanDialog()
{
    m_chatCleanDlg.OpenDialog(m_hWnd, m_serverConnected);
}

void CBalloonSystemSettingDlg::OnChatCleanResult(int code, int64_t beginTime, int64_t endTime)
{
    if (!m_chatCleanDlg.OnCleanResult(code, beginTime, endTime))
    {
        //对话框已关闭或没有在途请求：本机记录的删除由主面板收到删除推送后照常进行，这里只记日志
        LOG_INFO("Settings: chat clean result not shown by the dialog, code=%d, range=[%lld, %lld].",
                 code, (long long)beginTime, (long long)endTime);
    }
}

LRESULT CBalloonSystemSettingDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 关闭 = 隐藏并销毁本窗口（对象由主面板持有，下次打开重建）。
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonSystemSettingDlg::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM /*lParam*/, BOOL& bHandled)
{
    FlushAutoReplyText();
    FlushQuickReplyText();
    // 截图键帽还在录入时关了窗口：本程序的全局快捷键仍处于暂停状态，在这里恢复
    if (m_shotHotkeyCapturing)
    {
        EndScreenshotHotkeyCapture();
    }
    bHandled = FALSE;
    return 0;
}

// =======================================================================
// 行构建辅助
// =======================================================================

std::unique_ptr<balloonwjui::DuiVBox> CBalloonSystemSettingDlg::BuildRowTextBlock(
    LPCTSTR title, LPCTSTR subtitle, UINT subtitleCtrlId)
{
    std::unique_ptr<balloonwjui::DuiVBox> col(new balloonwjui::DuiVBox());
    col->SetGap(kRowTextGap);

    // 顶部弹性空白：把标题块整体垂直居中于行内。
    col->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                  balloonwjui::DuiLayout::Hint().Weight(1));

    //译文比中文长，一行放不下时以省略号结尾，不画出行外；非简体中文界面另挂悬停提示显示全文（多语言第 4 期）。
    //简体中文的文字都放得下，不挂提示，行为与改动之前相同
    const bool tipFullText = !i18n::IsSourceLanguage();

    std::unique_ptr<balloonwjui::DuiLabel> lblTitle(new balloonwjui::DuiLabel());
    lblTitle->SetText(title);
    lblTitle->SetTextColor(kRowTitleClr);
    lblTitle->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (tipFullText && title != NULL && title[0] != _T('\0'))
    {
        balloonwjui::DuiToolTipMgr::Inst().Register(lblTitle.get(), title);
    }
    col->AddChild(std::move(lblTitle), balloonwjui::DuiLayout::Hint().Fixed(kRowTitleH));

    if (subtitle != NULL && subtitle[0] != _T('\0'))
    {
        std::unique_ptr<balloonwjui::DuiLabel> lblSub(new balloonwjui::DuiLabel());
        lblSub->SetText(subtitle);
        lblSub->SetTextColor(kRowSubClr);
        lblSub->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (tipFullText)
        {
            balloonwjui::DuiToolTipMgr::Inst().Register(lblSub.get(), subtitle);
        }
        if (subtitleCtrlId != 0)
        {
            //只有事后要换文案的行才给 id（见头文件说明）。
            lblSub->SetCtrlId(subtitleCtrlId);
        }
        col->AddChild(std::move(lblSub), balloonwjui::DuiLayout::Hint().Fixed(kRowSubH));
    }

    // 底部弹性空白。
    col->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                  balloonwjui::DuiLayout::Hint().Weight(1));
    return col;
}

void CBalloonSystemSettingDlg::AddRowDivider(balloonwjui::DuiVBox* page)
{
    std::unique_ptr<balloonwjui::DuiSeparator> sep(new balloonwjui::DuiSeparator());
    sep->SetColor(kDividerClr);
    page->AddChild(std::move(sep), balloonwjui::DuiLayout::Hint().Fixed(kRowDividerH));
}

void CBalloonSystemSettingDlg::AddSwitchRow(balloonwjui::DuiVBox* page, LPCTSTR title,
                                            LPCTSTR subtitle, UINT ctrlId, bool on,
                                            bool addDivider, UINT subtitleCtrlId)
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kRowGap);

    row->AddChild(BuildRowTextBlock(title, subtitle, subtitleCtrlId),
                  balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiSwitch> sw(new balloonwjui::DuiSwitch());
    sw->SetCtrlId(ctrlId);
    sw->SetOnColor(kAccentOrange);
    sw->SetChecked(on, /*animated*/false, /*notify*/false);
    row->AddChild(std::move(sw),
                  balloonwjui::DuiLayout::Hint().Fixed(kSwitchW, kSwitchH)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));

    page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));

    if (addDivider)
    {
        AddRowDivider(page);
    }
}

void CBalloonSystemSettingDlg::AddAutoReplyRow(balloonwjui::DuiVBox* page, LPCTSTR title,
                                               LPCTSTR subtitle, UINT switchId, bool on,
                                               UINT editId, LPCTSTR text, LPCTSTR placeholder,
                                               bool addDivider)
{
    //上面一行是普通的开关行（与本页其它开关同一套样式），不加分隔线：下面的输入框属于同一组
    AddSwitchRow(page, title, subtitle, switchId, on, false);

    //下面单独一行是占满宽度的多行输入框
    balloonsettings::AutoReplyEditSpec spec;
    spec.m_editId = editId;
    spec.m_on = on;
    spec.m_text = (text != NULL) ? text : _T("");
    spec.m_placeholder = placeholder;
    page->AddChild(balloonsettings::BuildAutoReplyEditRow(spec),
                   balloonwjui::DuiLayout::Hint().Fixed(balloonsettings::kAutoReplyEditRowH));
    if (addDivider)
    {
        AddRowDivider(page);
    }
}

void CBalloonSystemSettingDlg::OnAutoReplyTextChanged(UINT editId)
{
    CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;
    balloonwjui::DuiControl* content = GetClientContent();
    if (cfg == NULL || content == NULL)
    {
        return;
    }
    balloonwjui::DuiEdit* edit = dynamic_cast<balloonwjui::DuiEdit*>(content->FindCtrlById(editId));
    if (edit == NULL)
    {
        return;
    }
    //改的是内存里的配置，主面板下一次判断要不要自动回复时就用新文字；写盘留到失去焦点或关窗
    const std::wstring text = balloonsettings::AutoReplyTextForConfig((LPCTSTR)edit->GetText());
    if (editId == kIdAutoReplyAwayText)
    {
        cfg->SetAutoReplyAwayText(text.c_str());
    }
    else
    {
        cfg->SetAutoReplyBusyText(text.c_str());
    }
    m_autoReplyTextDirty = true;
}

void CBalloonSystemSettingDlg::FlushAutoReplyText()
{
    if (!m_autoReplyTextDirty || m_lpFMGClient == NULL)
    {
        return;
    }
    m_autoReplyTextDirty = false;
    m_lpFMGClient->SaveUserConfig();
    PushAutoReplyToServer();
    LOG_INFO("Settings: auto-reply text saved.");
}

bool CBalloonSystemSettingDlg::IsMyBotsRowVisible() const
{
    if (m_lpFMGClient == NULL)
    {
        return false;
    }
    const CUserMgr& um = m_lpFMGClient->m_UserMgr;
    return um.m_bBotFeature && um.m_bBotPersonalFeature && botui::IsAvailable();
}

void CBalloonSystemSettingDlg::PushAutoReplyToServer()
{
    //「离开 / 忙碌自动回复」已迁到服务端（docs/机器人方案.md §11 Q2）：企业打开了机器人时，改动同步上传，由服务端代答
    if (m_lpFMGClient == NULL || !m_lpFMGClient->m_UserMgr.m_bBotFeature)
    {
        return;
    }
    const CUserConfig& cfg = m_lpFMGClient->m_UserConfig;
    const net::AutoReplyWire w = balloonmain::BuildAutoReplyWire(cfg.IsEnableAutoReplyAway() != FALSE,
                                                                cfg.GetAutoReplyAwayText(),
                                                                cfg.IsEnableAutoReplyBusy() != FALSE,
                                                                cfg.GetAutoReplyBusyText());
    CIUSocket::GetInstance().SendSetAutoReply(w);
}

void CBalloonSystemSettingDlg::AddQuickReplyRows(balloonwjui::DuiVBox* page)
{
    //标题行：左标题 / 副标题，右「恢复默认」按钮；不加分隔线，下面的输入框属于同一组
    AddButtonRow(page, TR(L"快捷回复"), TR(L"在聊天窗发送按钮旁的 ▾ 菜单里点一下就发出，留空的不显示"),
                 kIdQuickReplyReset, TR(L"恢复默认"), false, false);

    //输入框一行：三个单行输入框并排，平分宽度。窗口没有滚动条，三个各占一行会让窗口过高
    std::unique_ptr<balloonwjui::DuiVBox> group(new balloonwjui::DuiVBox());
    group->SetGap(0);
    std::unique_ptr<balloonwjui::DuiHBox> edits(new balloonwjui::DuiHBox());
    edits->SetGap(kQuickReplyEditGap);
    const UINT ids[CUserConfig::kQuickReplySlotCount] = { kIdQuickReplyText1, kIdQuickReplyText2, kIdQuickReplyText3 };
    for (int i = 0; i < CUserConfig::kQuickReplySlotCount; ++i)
    {
        std::unique_ptr<balloonwjui::DuiEdit> edit(new balloonwjui::DuiEdit());
        edit->SetCtrlId(ids[i]);
        edit->SetMaxLength(balloonchatwnd::kQuickReplyMaxChars);
        edit->SetPlaceholder(TRF(L"快捷回复 {0}", i + 1).c_str());
        //初始文字不发通知：这是把配置里的值显示出来，不是用户改了内容
        const CString text = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserConfig.GetQuickReply(i) : CString();
        edit->SetTextNoNotify(text);
        edits->AddChild(std::move(edit), balloonwjui::DuiLayout::Hint().Weight(1));
    }
    group->AddChild(std::move(edits), balloonwjui::DuiLayout::Hint().Fixed(kQuickReplyEditH));
    //下方间距：输入框不贴着本组的分隔线
    group->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                    balloonwjui::DuiLayout::Hint().Fixed(kQuickReplyEditBottomGap));
    page->AddChild(std::move(group), balloonwjui::DuiLayout::Hint().Fixed(kQuickReplyEditH + kQuickReplyEditBottomGap));
    AddRowDivider(page);
}

int CBalloonSystemSettingDlg::QuickReplyIndexOfEdit(UINT ctrlId)
{
    if (ctrlId == kIdQuickReplyText1)
    {
        return 0;
    }
    if (ctrlId == kIdQuickReplyText2)
    {
        return 1;
    }
    if (ctrlId == kIdQuickReplyText3)
    {
        return 2;
    }
    return -1;
}

void CBalloonSystemSettingDlg::OnQuickReplyTextChanged(UINT editId)
{
    const int index = QuickReplyIndexOfEdit(editId);
    balloonwjui::DuiControl* content = GetClientContent();
    if (index < 0 || m_lpFMGClient == NULL || content == NULL)
    {
        return;
    }
    balloonwjui::DuiEdit* edit = dynamic_cast<balloonwjui::DuiEdit*>(content->FindCtrlById(editId));
    if (edit == NULL)
    {
        return;
    }
    //改的是内存里的配置，聊天窗下一次弹 ▾ 菜单就用新文字；写盘留到失去焦点或关窗。清空也是一种设置：这一条不再显示
    m_lpFMGClient->m_UserConfig.SetQuickReply(index, (LPCTSTR)edit->GetText());
    m_quickReplyDirty = true;
}

void CBalloonSystemSettingDlg::FlushQuickReplyText()
{
    if (!m_quickReplyDirty || m_lpFMGClient == NULL)
    {
        return;
    }
    m_quickReplyDirty = false;
    m_lpFMGClient->SaveUserConfig();
    LOG_INFO("Settings: quick replies saved.");
}

void CBalloonSystemSettingDlg::ResetQuickReplies()
{
    if (m_lpFMGClient == NULL)
    {
        return;
    }
    m_lpFMGClient->m_UserConfig.ResetQuickReplies();
    m_quickReplyDirty = false;
    m_lpFMGClient->SaveUserConfig();
    //三个输入框换回默认文字，不发通知（否则又会被当成用户改过）
    balloonwjui::DuiControl* content = GetClientContent();
    const UINT ids[CUserConfig::kQuickReplySlotCount] = { kIdQuickReplyText1, kIdQuickReplyText2, kIdQuickReplyText3 };
    for (int i = 0; content != NULL && i < CUserConfig::kQuickReplySlotCount; ++i)
    {
        balloonwjui::DuiEdit* edit = dynamic_cast<balloonwjui::DuiEdit*>(content->FindCtrlById(ids[i]));
        if (edit != NULL)
        {
            edit->SetTextNoNotify(m_lpFMGClient->m_UserConfig.GetQuickReply(i));
        }
    }
    LOG_INFO("Settings: quick replies reset to defaults.");
}

void CBalloonSystemSettingDlg::AddComboRow(balloonwjui::DuiVBox* page, LPCTSTR title,
                                           LPCTSTR subtitle, UINT ctrlId,
                                           const wchar_t* const* options, int count,
                                           int selIndex, bool addDivider, bool translateOptions)
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kRowGap);

    row->AddChild(BuildRowTextBlock(title, subtitle), balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiComboBox> combo(new balloonwjui::DuiComboBox());
    combo->SetCtrlId(ctrlId);
    for (int i = 0; i < count; ++i)
    {
        // 选项表里是只做了标记（TR_NOOP）的原文时在这里统一取译文；语言行传入的已是最终显示文字，
        // 不能再查一次（伪语言会对任何文字再加一层括号）。
        // 包一层 CString，兼容 Unicode / MBCS 两种字符集（选项表是 wchar_t*）。
        combo->AddString(CString(translateOptions ? TR(options[i]) : options[i]));
    }
    combo->SetCurSel(selIndex, /*notify*/false);
    row->AddChild(std::move(combo),
                  balloonwjui::DuiLayout::Hint().Fixed(kComboW, kComboH)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));

    page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));

    if (addDivider)
    {
        AddRowDivider(page);
    }
}

// =======================================================================
// 三页真实内容
// =======================================================================

std::unique_ptr<balloonwjui::DuiVBox> CBalloonSystemSettingDlg::BuildGeneralPage()
{
    std::unique_ptr<balloonwjui::DuiVBox> page(new balloonwjui::DuiVBox());
    page->SetGap(0);

    CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;

    // 开机自启动（真值来自注册表 Run 项）。说明文字里的产品名称取 flamingo.ini 的 [ui] apptitle；
    // 注册表 Run 项的值名 kRunValueName 不随它改，否则用户机器上原有的自启动项会失效。
    const std::wstring autoStartDesc = TRF(L"登录系统后自动启动 {0}", g_strAppTitle);
    AddSwitchRow(page.get(), TR(L"开机自启动"), autoStartDesc.c_str(),
                 kIdAutoStart, AutoStartIsEnabled(), true);

    // 关闭主面板时（下拉：最小化到托盘 / 退出）。
    bool exitWhenClose = (cfg != NULL) ? (cfg->IsEnableExitWhenCloseMainDlg() != FALSE) : false;
    AddComboRow(page.get(), TR(L"关闭主面板时"), TR(L"关闭后保留在系统托盘"),
                kIdCloseAction, balloonsettings::CloseActionOptions(),
                balloonsettings::CloseActionOptionCount(),
                balloonsettings::CloseActionIndexFromExitWhenClose(exitWhenClose), true);

    // 主面板贴边自动隐藏（开关）。与「关闭主面板时」同属主面板的行为，故紧挨着放。
    // 运行期由贴边控制器每次轮询重新读取配置（见 BalloonMain/MainPanelDockController.h），
    // 这里只改配置，不需要通知主面板。
    bool dockAutoHide = (cfg != NULL) ? (cfg->IsEnableDockAutoHide() != FALSE) : true;
    AddSwitchRow(page.get(), TR(L"主面板贴边自动隐藏"),
                 TR(L"拖到屏幕上、左、右边缘时自动收起，鼠标移过去再展开"),
                 kIdDockAutoHide, dockAutoHide, true);

    // 禁止机器休眠（开关）。放在「自动离开」<u>之前</u>是有意为之：它一旦开启就会把下面
    // 两行置为禁用，因果顺序与阅读顺序一致 —— 用户操作的是这一行，变化发生在其下方。
    bool keepAwake = (cfg != NULL) ? (cfg->IsEnableKeepAwake() != FALSE) : false;
    //副标题长度须与本页其它行相当：行内文字块用的是单行不折行、不加省略号的绘制方式，
    //写长了会一直画到右侧开关底下（实测 32 个字就被压住了）。
    AddSwitchRow(page.get(), TR(L"禁止机器休眠"),
                 TR(L"开启后本机不会自动休眠、关屏或锁屏，适合演示等场景"),
                 kIdKeepAwake, keepAwake, true);

    // 无操作自动离开（开关）+ 等待时长（下拉）。两行挨着放，读起来就是一句话：
    // "自动离开 —— 电脑无操作 5 分钟后自动切到离开状态"。
    // 副标题给了 id：被上面那个开关置灰时要换成解释文案（见 ApplyKeepAwakeExclusion）。
    bool autoAway = (cfg != NULL) ? (cfg->IsEnableAutoAway() != FALSE) : true;
    AddSwitchRow(page.get(), TR(L"自动离开"), TR(kAutoAwaySubtitleNormal),
                 kIdAutoAway, autoAway, true, kIdAutoAwaySubtitle);

    // 语言（下拉）：跟随系统、简体中文、语言包里的语言（docs/多语言方案.md 第 7 节）。语言包
    // FlamingoLang.dll 不存在时不显示这一行，此时「无操作多久后切换成离开」是末行、不加分隔线。
    // 选项存进 m_languageChoices，下拉框的文字指针指向其中的字符串，二者在对话框生命周期内不变。
    //（2026-10-03 起「备忘与提醒」的入口在主面板系统菜单里，本页不再有那一行）
    std::vector<i18n::LanguageChoice> packLanguages;
    const bool hasLanguagePack = i18n::ListPackLanguages(LanguagePackPath(), packLanguages);

    int awayDelayIdx = (cfg != NULL)
                     ? balloonsettings::ClampAutoAwayDelayIndex(cfg->GetAutoAwayDelayIndex())
                     : balloonsettings::kAutoAwayDelay5Min;
    AddComboRow(page.get(), TR(L"无操作多久后切换成离开"), NULL,
                kIdAutoAwayDelay, balloonsettings::AutoAwayDelayOptions(),
                balloonsettings::AutoAwayDelayOptionCount(), awayDelayIdx, hasLanguagePack);

    if (hasLanguagePack)
    {
        m_languageChoices = i18n::BuildLanguageChoices(packLanguages);
        m_languageOptionTexts.clear();
        for (size_t i = 0; i < m_languageChoices.size(); ++i)
            m_languageOptionTexts.push_back(m_languageChoices[i].m_name.c_str());
        //配置值不在选项里（如手工写的伪语言代码）时显示「跟随系统」；用户不动它就不会改写配置
        int langIdx = i18n::FindLanguageChoice(m_languageChoices,
                                               i18n::ReadConfiguredLanguage(std::wstring(AppIniPath())));
        if (langIdx < 0)
            langIdx = 0;
        AddComboRow(page.get(), TR(L"语言 / Language"), NULL, kIdLanguage, &m_languageOptionTexts[0],
                    (int)m_languageOptionTexts.size(), langIdx, false,    // 末行不加分隔线
                    false);   // 选项已是最终显示文字，不再取译文
    }
    else
    {
        LOG_INFO("Settings: language pack not available, the language row is hidden.");
    }

    // 底部留白把各行顶到内容区顶部。
    page->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));
    return page;
}

std::unique_ptr<balloonwjui::DuiVBox> CBalloonSystemSettingDlg::BuildNotifyPage()
{
    std::unique_ptr<balloonwjui::DuiVBox> page(new balloonwjui::DuiVBox());
    page->SetGap(0);

    CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;

    bool newMsg    = (cfg != NULL) ? (cfg->IsEnableNewMsgNotify()   != FALSE) : true;
    bool popup     = (cfg != NULL) ? (cfg->IsEnableDesktopPopup()   != FALSE) : true;
    bool soundOn   = (cfg != NULL) ? balloonsettings::SoundOnFromMute(cfg->IsEnableMute() != FALSE) : true;
    bool shake     = (cfg != NULL) ? (cfg->IsEnableShakeFlashIcon() != FALSE) : false;
    bool dnd       = (cfg != NULL) ? (cfg->IsEnableDoNotDisturb()   != FALSE) : false;
    bool afterLock = (cfg != NULL) ? (cfg->IsEnableNotifyAfterLock()!= FALSE) : true;

    //2026-10-10：下面几项开关目前只保存设置、没有代码读取；按用户决定不隐藏，副标题写明当前的实际情况。
    //勿扰时段只被文件共享的右下角通知读取（Feature/FileShare/FileShareHost.cpp、ShareNoticePolicy.cpp）
    AddSwitchRow(page.get(), TR(L"新消息提醒"), TR(L"暂未生效：提醒方式请在会话右键菜单里设置"), kIdNewMsgNotify, newMsg, true);
    AddSwitchRow(page.get(), TR(L"桌面弹窗"), TR(L"暂未生效：目前没有新消息弹窗"), kIdDesktopPopup, popup, true);
    AddSwitchRow(page.get(), TR(L"提示音"), NULL, kIdSound, soundOn, true);
    AddSwitchRow(page.get(), TR(L"震动闪烁图标"), TR(L"暂未生效：托盘图标按会话的提醒方式闪烁"), kIdShakeFlash, shake, true);
    AddSwitchRow(page.get(), TR(L"勿扰时段"), TR(L"22:00 - 08:00 只屏蔽文件共享通知，新消息照常提醒"), kIdDoNotDisturb, dnd, true);
    AddSwitchRow(page.get(), TR(L"锁屏后通知"), TR(L"暂未生效：客户端锁屏时不响提示音、不弹窗，托盘照常闪烁"), kIdNotifyAfterLock,
                 afterLock, false);

    page->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));
    return page;
}

std::unique_ptr<balloonwjui::DuiVBox> CBalloonSystemSettingDlg::BuildChatPage()
{
    std::unique_ptr<balloonwjui::DuiVBox> page(new balloonwjui::DuiVBox());
    page->SetGap(0);

    CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;

    //发送键按 ChatSendMode 显示：它与聊天窗读写的是同一份账号配置（BUG-119）
    bool pressEnter   = (balloonchatwnd::GetChatSendMode() == balloonchatwnd::kSendOnEnter);
    bool autoRecvImg  = (cfg != NULL) ? (cfg->IsEnableAutoRecvImage()    != FALSE) : true;
    bool autoRecvFile = (cfg != NULL) ? (cfg->IsEnableAutoRecvFile()     != FALSE) : false;
    bool roaming      = (cfg != NULL) ? (cfg->IsEnableMsgRoaming()       != FALSE) : true;
    bool linkDetect   = (cfg != NULL) ? (cfg->IsEnableLinkDetect()       != FALSE) : true;
    bool replyAway    = (cfg != NULL) ? (cfg->IsEnableAutoReplyAway()    != FALSE) : false;
    bool replyBusy    = (cfg != NULL) ? (cfg->IsEnableAutoReplyBusy()    != FALSE) : false;
    bool hideOnShot   = balloonscreenshot::LoadGlobalScreenshotPrefs().m_hideWindow;   // 2026-10-07 起存在全局配置里

    // 发送消息快捷键（下拉：Enter / Ctrl+Enter）。
    AddComboRow(page.get(), TR(L"发送消息快捷键"), NULL,
                kIdSendKey, balloonsettings::SendKeyOptions(),
                balloonsettings::SendKeyOptionCount(),
                balloonsettings::SendKeyIndexFromPressEnter(pressEnter), true);

    // 默认聊天字体（预览 + 修改 + 恢复默认）。
    AddChatFontRow(page.get());

    //自动接收图片 / 文件与消息漫游三项目前只保存设置。聊天图片总是自动下载，单聊文件走「接收 / 拒绝」，
    //聊天记录每次从服务器拉取；副标题写明这些实际情况
    AddSwitchRow(page.get(), TR(L"自动接收图片"), TR(L"暂未生效：聊天图片总是自动下载"), kIdAutoRecvImage, autoRecvImg, true);
    AddSwitchRow(page.get(), TR(L"自动接收文件"), TR(L"暂未生效：单聊文件要点「接收」后才下载"), kIdAutoRecvFile, autoRecvFile, true);
    AddSwitchRow(page.get(), TR(L"消息漫游"), TR(L"暂未生效：聊天记录始终保存在服务器，各设备都能查看"), kIdMsgRoaming, roaming, true);
    AddSwitchRow(page.get(), TR(L"自动识别链接"), TR(L"关闭后聊天里的网址以普通文字显示"),
                 kIdLinkDetect, linkDetect, true);
    // 截图时隐藏当前窗口（2026-10-05）：与聊天窗剪刀右边 ▾ 菜单里的勾选项是同一个配置
    AddSwitchRow(page.get(), TR(L"截图时隐藏当前窗口"), TR(L"从单聊、群聊发起截图时，截图里不包含这个窗口"),
                 kIdScreenshotHideWindow, hideOnShot, true);

    // 离开 / 忙碌时自动回复（2026-10-03）：每组上面一行开关，下面单独一行多行输入框，只回复单聊消息。
    // 输入框留空时发送默认文字，故以默认文字作占位提示；副标题要短，行内文字块不折行、不加省略号
    AddAutoReplyRow(page.get(), TR(L"离开时自动回复"), TR(L"留空则发送默认文字"),
                    kIdAutoReplyAway, replyAway, kIdAutoReplyAwayText,
                    (cfg != NULL) ? cfg->GetAutoReplyAwayText() : _T(""),
                    balloonmain::kDefaultAwayReplyText, true);
    AddAutoReplyRow(page.get(), TR(L"忙碌时自动回复"), TR(L"留空则发送默认文字"),
                    kIdAutoReplyBusy, replyBusy, kIdAutoReplyBusyText,
                    (cfg != NULL) ? cfg->GetAutoReplyBusyText() : _T(""),
                    balloonmain::kDefaultBusyReplyText, true);

    // 我的机器人（2026-10-08，docs/机器人方案.md §4.3）：自动回复由服务端代答之后，按关键词、时间段、发送者设置的
    // 更多代答规则在「我的机器人」窗口里设置；机器人插件不可用或企业没有允许个人机器人时不出现这一行
    if (IsMyBotsRowVisible())
    {
        AddButtonRow(page.get(), TR(L"我的机器人"), TR(L"按关键词、时间段与发送者设置更多自动代答"),
                     kIdMyBots, TR(L"打开…"), false, true);
    }

    // 快捷回复（2026-10-06，docs/快捷回复方案.md §3.5）：标题行带「恢复默认」，下面一行并排三个单行输入框
    AddQuickReplyRows(page.get());

    // 清理聊天记录（按钮，打开按时间范围清理的对话框；docs/清理聊天记录方案.md §11.1）。
    AddButtonRow(page.get(), TR(L"清理聊天记录"), TR(L"按时间范围删除本机和服务器上的记录，对方不受影响"),
                 kIdChatClean, TR(L"清理…"), false, false);   // 末行不加分隔线

    page->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));
    return page;
}

// =======================================================================
// 行构建辅助（按钮行 / 取值行 / 键帽行）
// =======================================================================

void CBalloonSystemSettingDlg::AddButtonRow(balloonwjui::DuiVBox* page, LPCTSTR title,
                                            LPCTSTR subtitle, UINT ctrlId, LPCTSTR btnText,
                                            bool danger, bool addDivider)
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kRowGap);
    row->AddChild(BuildRowTextBlock(title, subtitle), balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
    btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    btn->SetVariant(danger ? balloonwjui::DuiButton::Variant::Danger
                           : balloonwjui::DuiButton::Variant::Default);
    btn->SetCtrlId(ctrlId);
    btn->SetText(btnText);
    row->AddChild(std::move(btn),
                  balloonwjui::DuiLayout::Hint().Fixed(i18n::DefaultButtonWidthForText(btnText, kBtnWidth), kBtnHeight)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));

    page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    if (addDivider)
    {
        AddRowDivider(page);
    }
}

void CBalloonSystemSettingDlg::AddChatFontRow(balloonwjui::DuiVBox* page)
{
    const net::TextFont def = (m_lpFMGClient != NULL)
        ? m_lpFMGClient->m_UserConfig.ChatFonts().GetDefault()
        : net::TextFont();

    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kRowGap);
    row->AddChild(BuildRowTextBlock(TR(L"默认聊天字体"), TR(L"单独设过字体的聊天窗除外")),
                  balloonwjui::DuiLayout::Hint().Weight(1));

    // 预览文字：按全局默认字体画；未设置时灰字「未设置」。控件尚未挂进树，直接对它设外观
    std::unique_ptr<balloonwjui::DuiLabel> preview(new balloonwjui::DuiLabel());
    preview->SetCtrlId(kIdChatFontPreview);
    preview->SetTextAlign(DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    balloonsettings::ApplyChatFontPreview(preview.get(), def, m_hChatFontPreview);
    row->AddChild(std::move(preview),
                  balloonwjui::DuiLayout::Hint().Fixed(kChatFontPreviewW, kRowH)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));

    std::unique_ptr<balloonwjui::DuiButton> edit(new balloonwjui::DuiButton());
    edit->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    edit->SetVariant(balloonwjui::DuiButton::Variant::Default);
    edit->SetCtrlId(kIdChatFontEdit);
    edit->SetText(TR(L"修改"));
    row->AddChild(std::move(edit),
                  balloonwjui::DuiLayout::Hint().Fixed(i18n::DefaultButtonWidthForText(TR(L"修改"), kBtnWidth), kBtnHeight)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));

    // 「恢复默认」：清除全局默认，发出的消息不再带字体。未设置时不可点
    std::unique_ptr<balloonwjui::DuiButton> reset(new balloonwjui::DuiButton());
    reset->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    reset->SetVariant(balloonwjui::DuiButton::Variant::Default);
    reset->SetCtrlId(kIdChatFontReset);
    reset->SetText(TR(L"恢复默认"));
    reset->SetEnabled(def.m_valid);
    row->AddChild(std::move(reset),
                  balloonwjui::DuiLayout::Hint().Fixed(i18n::DefaultButtonWidthForText(TR(L"恢复默认"), kBtnWidth), kBtnHeight)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));

    page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    AddRowDivider(page);
}

void CBalloonSystemSettingDlg::RefreshChatFontRow()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    const net::TextFont def = (m_lpFMGClient != NULL)
        ? m_lpFMGClient->m_UserConfig.ChatFonts().GetDefault()
        : net::TextFont();

    balloonwjui::DuiLabel* preview =
        dynamic_cast<balloonwjui::DuiLabel*>(content->FindCtrlById(kIdChatFontPreview));
    if (preview != NULL)
    {
        balloonsettings::ApplyChatFontPreview(preview, def, m_hChatFontPreview);
    }
    balloonwjui::DuiControl* reset = content->FindCtrlById(kIdChatFontReset);
    if (reset != NULL)
    {
        reset->SetEnabled(def.m_valid);
    }
}

void CBalloonSystemSettingDlg::OpenDefaultChatFontPopup()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || m_lpFMGClient == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(kIdChatFontEdit);
    if (btn == NULL)
    {
        return;
    }

    // 以「修改」按钮的屏幕矩形为锚点（控件矩形是客户区坐标，转屏幕）
    RECT rc = btn->GetRect();
    POINT tl = { rc.left, rc.top };
    POINT br = { rc.right, rc.bottom };
    ::ClientToScreen(m_hWnd, &tl);
    ::ClientToScreen(m_hWnd, &br);
    RECT anchor = { tl.x, tl.y, br.x, br.y };

    // 回显当前全局默认；未设置时浮层按内置默认（14px、黑、无样式）显示
    m_chatFontPopup.SetFont(m_lpFMGClient->m_UserConfig.ChatFonts().GetDefault());
    m_chatFontPopup.OpenAt(anchor, m_hWnd);
}

void CBalloonSystemSettingDlg::OnDefaultChatFontChanged(void* userdata, const net::TextFont& font)
{
    CBalloonSystemSettingDlg* self = (CBalloonSystemSettingDlg*)userdata;
    if (self == NULL)
    {
        return;
    }
    self->ApplyDefaultChatFont(font);
}

void CBalloonSystemSettingDlg::ApplyDefaultChatFont(const net::TextFont& font)
{
    if (m_lpFMGClient == NULL)
    {
        return;
    }
    // 改即生效：写进账号配置并立刻存盘，与本对话框其它设置项一致
    m_lpFMGClient->m_UserConfig.ChatFonts().SetDefault(font);
    m_lpFMGClient->SaveUserConfig();
    RefreshChatFontRow();

    // 已打开的聊天窗要立刻跟着变：请主面板让没有单独设过字体的聊天窗改用新的默认字体。
    // 聊天窗归主面板持有，设置窗够不着，故经主面板中转
    if (m_ownerHwnd != NULL && ::IsWindow(m_ownerHwnd))
    {
        ::PostMessage(m_ownerHwnd, FMG_MSG_DEFAULT_CHAT_FONT_CHANGED, 0, 0);
    }
    LOG_INFO("Settings: default chat font %s (size %d).",
             font.m_valid ? "set" : "cleared", font.m_valid ? font.m_size : 0);
}

void CBalloonSystemSettingDlg::AddValueRow(balloonwjui::DuiVBox* page, LPCTSTR title,
                                           LPCTSTR subtitle, LPCTSTR value, bool addDivider)
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kRowGap);
    row->AddChild(BuildRowTextBlock(title, subtitle), balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiLabel> val(new balloonwjui::DuiLabel());
    val->SetText(value);
    val->SetTextColor(kValueClr);
    val->SetTextAlign(DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    // 取值（如文件路径）较长，固定较宽右对齐、超长省略号。
    row->AddChild(std::move(val),
                  balloonwjui::DuiLayout::Hint().Fixed(420).AlignC(balloonwjui::DuiLayout::AlignCenter));

    page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    if (addDivider)
    {
        AddRowDivider(page);
    }
}

void CBalloonSystemSettingDlg::AddKeyCapRow(balloonwjui::DuiVBox* page, LPCTSTR title,
                                            LPCTSTR keyText, bool addDivider)
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kRowGap);

    std::unique_ptr<balloonwjui::DuiLabel> lbl(new balloonwjui::DuiLabel());
    lbl->SetText(title);
    lbl->SetTextColor(kRowTitleClr);
    lbl->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    row->AddChild(std::move(lbl), balloonwjui::DuiLayout::Hint().Weight(1));

    // 键帽 chip：自绘 KeyCapChip（等宽字体 + 细描边），贴文字宽度、右对齐、垂直居中。
    // 行左标题 weight=1 把 chip 顶到行右端，即实现右对齐。
    int capW = KeyCapChip::MeasureWidth(keyText);
    std::unique_ptr<KeyCapChip> cap(new KeyCapChip());
    cap->SetText(keyText);
    row->AddChild(std::move(cap),
                  balloonwjui::DuiLayout::Hint().Fixed(capW, kKeyCapH)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));

    page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    if (addDivider)
    {
        AddRowDivider(page);
    }
}

void CBalloonSystemSettingDlg::AddScreenshotHotkeyRow(balloonwjui::DuiVBox* page, bool addDivider)
{
    //截图快捷键 2026-10-07 起存在全局配置里、所有账号共用（docs/截图独立模块方案.md 3.4 节）
    DWORD hotkey = balloonscreenshot::LoadGlobalScreenshotPrefs().m_hotkey;

    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kRowGap);
    // 提示文字先放一句占位，行建好之后由 RefreshScreenshotHotkeyRow 按注册结果改写
    row->AddChild(BuildRowTextBlock(TR(L"截图"), TR(L"点击键帽后按下新组合"), kIdShotHotkeyStatus),
                  balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonsettings::CHotkeyCaptureBox> box(new balloonsettings::CHotkeyCaptureBox());
    box->SetCtrlId(kIdShotHotkeyBox);
    box->SetHotkey(hotkey);
    row->AddChild(std::move(box),
                  balloonwjui::DuiLayout::Hint().Fixed(balloonsettings::CHotkeyCaptureBox::kPreferredWidth,
                                                       balloonsettings::CHotkeyCaptureBox::kPreferredHeight)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));

    std::unique_ptr<balloonwjui::DuiButton> reset(new balloonwjui::DuiButton());
    reset->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    reset->SetVariant(balloonwjui::DuiButton::Variant::Default);
    reset->SetCtrlId(kIdShotHotkeyReset);
    reset->SetText(TR(L"恢复默认"));
    row->AddChild(std::move(reset),
                  balloonwjui::DuiLayout::Hint().Fixed(i18n::DefaultButtonWidthForText(TR(L"恢复默认"), kBtnWidth), kBtnHeight)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));

    page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    if (addDivider)
    {
        AddRowDivider(page);
    }
}

void CBalloonSystemSettingDlg::RefreshScreenshotHotkeyRow(LPCTSTR message, bool isError)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiLabel* status =
        dynamic_cast<balloonwjui::DuiLabel*>(content->FindCtrlById(kIdShotHotkeyStatus));
    if (status == NULL)
    {
        return;
    }

    CString text;
    bool error = isError;
    if (message != NULL)
    {
        text = message;
    }
    else
    {
        DWORD hotkey = balloonscreenshot::LoadGlobalScreenshotPrefs().m_hotkey;
        if (balloonscreenshot::GetScreenshotHotkeyState() == balloonscreenshot::kShotHotkeyUnavailable)
        {
            //截图模块 FlamingoScreenshot.dll 没有加载成功：快捷键不注册（docs/截图独立模块方案.md 3.3 节）
            text = TR(L"截图功能不可用（截图模块没有加载成功）");
            error = true;
        }
        else if (hotkey == 0)
        {
            text = TR(L"已停用，点击键帽可重新设置");
        }
        else if (balloonscreenshot::GetScreenshotHotkeyState() == balloonscreenshot::kShotHotkeyFailed)
        {
            CString keys = balloonscreenshot::FormatHotkey(hotkey, balloonscreenshot::kHotkeyTextCompact);
            text = TRF(L"{0} 已被其他程序占用，请换一个组合", (const wchar_t*)keys).c_str();
            error = true;
        }
        else
        {
            text = TR(L"点击键帽后按下新组合");
        }
    }
    status->SetText(text);
    status->SetTextColor(error ? kRowErrorClr : kRowSubClr);
    if (!i18n::IsSourceLanguage())
    {
        // 译文较长时以省略号结尾，悬停提示显示全文（与 BuildRowTextBlock 一致）
        balloonwjui::DuiToolTipMgr::Inst().Register(status, text);
    }
}

void CBalloonSystemSettingDlg::BeginScreenshotHotkeyCapture()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonsettings::CHotkeyCaptureBox* box = (content != NULL)
        ? dynamic_cast<balloonsettings::CHotkeyCaptureBox*>(content->FindCtrlById(kIdShotHotkeyBox))
        : NULL;
    if (box == NULL || box->IsCapturing())
    {
        return;
    }
    // 先暂停本程序的全局快捷键，再进入录入：否则用户按下的组合恰好是本程序的快捷键时，
    // 系统会把它当作热键拦走，键帽收不到按键
    if (m_ownerHwnd != NULL && ::IsWindow(m_ownerHwnd))
    {
        balloonscreenshot::SuspendAppHotkeys(m_ownerHwnd);
    }
    m_shotHotkeyCapturing = true;
    box->BeginCapture();
    RefreshScreenshotHotkeyRow(TR(L"按 Esc 取消，Backspace 停用"), false);
}

void CBalloonSystemSettingDlg::EndScreenshotHotkeyCapture()
{
    m_shotHotkeyCapturing = false;
    if (m_lpFMGClient != NULL && m_ownerHwnd != NULL && ::IsWindow(m_ownerHwnd))
    {
        CUserConfig& cfg = m_lpFMGClient->m_UserConfig;
        //系统设置窗只在登录后打开，所以按已登录恢复
        balloonscreenshot::ResumeAppHotkeys(m_ownerHwnd, true, cfg.IsEnableRecordHotkey() != FALSE,
                                            balloonscreenshot::LoadGlobalScreenshotPrefs().m_hotkey);
    }
    if (IsWindow())
    {
        RefreshScreenshotHotkeyRow(NULL, false);
    }
}

void CBalloonSystemSettingDlg::ApplyScreenshotHotkeyChoice(DWORD hotkey)
{
    if (m_lpFMGClient == NULL)
    {
        return;
    }
    CUserConfig& cfg = m_lpFMGClient->m_UserConfig;
    balloonwjui::DuiControl* content = GetClientContent();
    balloonsettings::CHotkeyCaptureBox* box = (content != NULL)
        ? dynamic_cast<balloonsettings::CHotkeyCaptureBox*>(content->FindCtrlById(kIdShotHotkeyBox))
        : NULL;
    if (box != NULL)
    {
        // 录入中点了「恢复默认」：键帽不一定失去焦点，这里主动结束录入（不再发通知）
        box->CancelCapture();
    }

    int reservedCount = 0;
    const balloonscreenshot::ReservedHotkey* reserved = balloonscreenshot::AppReservedHotkeys(reservedCount);
    int conflict = -1;
    balloonscreenshot::HotkeyCheckResult check =
        balloonscreenshot::CheckHotkey(hotkey, reserved, reservedCount, &conflict);
    LOG_INFO("settings: screenshot hotkey chosen 0x%08X, check result=%d.", (unsigned)hotkey, (int)check);

    CString error;
    switch (check)
    {
    //可以使用，或停用（0）：保存
    case balloonscreenshot::kHotkeyOk:
    case balloonscreenshot::kHotkeyDisabled:
    {
        //写进全局配置（2026-10-07 起所有账号共用，docs/截图独立模块方案.md 3.4 节）
        balloonscreenshot::ScreenshotPrefs prefs = balloonscreenshot::LoadGlobalScreenshotPrefs();
        prefs.m_hotkey = hotkey;
        balloonscreenshot::SaveGlobalScreenshotPrefs(prefs);
        if (box != NULL)
        {
            box->SetHotkey(hotkey);
        }
        break;
    }
    //组合里没有 Ctrl 或 Alt
    case balloonscreenshot::kHotkeyNeedModifier:
        error = TR(L"组合里须包含 Ctrl 或 Alt");
        break;
    //主键不是字母、数字或 F1~F12
    case balloonscreenshot::kHotkeyUnsupportedKey:
        error = TR(L"主键须为字母、数字或 F1~F12");
        break;
    //与本程序其它全局快捷键重复
    case balloonscreenshot::kHotkeyReserved:
        error = TRF(L"与「{0}」的快捷键重复",
                    (conflict >= 0 && conflict < reservedCount) ? TR(reserved[conflict].m_name) : L"").c_str();
        break;
    //不会出现的取值，按不合规处理
    default:
        error = TR(L"这个组合不能使用");
        break;
    }

    // 无论成功与否都恢复本程序的全局快捷键：成功时按新组合注册，失败时按原组合注册
    m_shotHotkeyCapturing = false;
    if (m_ownerHwnd != NULL && ::IsWindow(m_ownerHwnd))
    {
        //系统设置窗只在登录后打开，所以按已登录恢复
        balloonscreenshot::ResumeAppHotkeys(m_ownerHwnd, true, cfg.IsEnableRecordHotkey() != FALSE,
                                            balloonscreenshot::LoadGlobalScreenshotPrefs().m_hotkey);
    }
    if (!error.IsEmpty())
    {
        RefreshScreenshotHotkeyRow(error, true);
    }
    else
    {
        RefreshScreenshotHotkeyRow(NULL, false);
    }
}

// =======================================================================
// 音视频页
// =======================================================================

std::unique_ptr<balloonwjui::DuiVBox> CBalloonSystemSettingDlg::BuildAudioVideoPage()
{
    std::unique_ptr<balloonwjui::DuiVBox> page(new balloonwjui::DuiVBox());
    page->SetGap(0);

    CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;

    // 麦克风（真实枚举 waveIn 设备）。
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kRowGap);
        row->AddChild(BuildRowTextBlock(TR(L"麦克风"), TR(L"暂未生效：通话用系统默认设备")),
                      balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiComboBox> combo(new balloonwjui::DuiComboBox());
        combo->SetCtrlId(kIdMicDevice);
        int n = FillMicDevices(combo.get());
        long sel = (cfg != NULL) ? cfg->GetAudioInputIndex() : 0;
        if (sel < 0 || sel >= n)
        {
            sel = 0;
        }
        combo->SetCurSel((int)sel, false);
        row->AddChild(std::move(combo),
                      balloonwjui::DuiLayout::Hint().Fixed(kComboWideW, kComboH)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));
        page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
        AddRowDivider(page.get());
    }

    // 扬声器（真实枚举 waveOut 设备）。
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kRowGap);
        row->AddChild(BuildRowTextBlock(TR(L"扬声器"), TR(L"暂未生效：通话用系统默认设备")),
                      balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiComboBox> combo(new balloonwjui::DuiComboBox());
        combo->SetCtrlId(kIdSpeakerDevice);
        int n = FillSpeakerDevices(combo.get());
        long sel = (cfg != NULL) ? cfg->GetAudioOutputIndex() : 0;
        if (sel < 0 || sel >= n)
        {
            sel = 0;
        }
        combo->SetCurSel((int)sel, false);
        row->AddChild(std::move(combo),
                      balloonwjui::DuiLayout::Hint().Fixed(kComboWideW, kComboH)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));
        page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
        AddRowDivider(page.get());
    }

    // 摄像头（占位单项 —— 摄像头枚举走 DirectShow，A/V 子系统未接入前先占位）。
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kRowGap);
        row->AddChild(BuildRowTextBlock(TR(L"摄像头"), TR(L"暂未生效：通话用第一个摄像头")),
                      balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiComboBox> combo(new balloonwjui::DuiComboBox());
        combo->SetCtrlId(kIdCameraDevice);
        combo->AddString(TR(L"系统默认摄像头"));
        combo->SetCurSel(0, false);
        row->AddChild(std::move(combo),
                      balloonwjui::DuiLayout::Hint().Fixed(kComboWideW, kComboH)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));
        page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
        AddRowDivider(page.get());
    }

    // 麦克风测试（按钮，占位 —— 无 A/V 采集管线，点击仅提示）。
    AddButtonRow(page.get(), TR(L"麦克风测试"), TR(L"暂未开放：功能开发中"), kIdMicTest, TR(L"测试"), false, true);

    // 回声消除（开关，本期仅存）。
    bool echo = (cfg != NULL) ? (cfg->IsEnableEchoCancel() != FALSE) : true;
    AddSwitchRow(page.get(), TR(L"回声消除"), TR(L"暂未生效：一对一通话时始终开启"), kIdEchoCancel, echo, true);

    // ---- 屏幕录制（docs/屏幕共享与录制方案.md 6.1 / 6.2）----
    // 画质：标准 / 高清
    int recordQuality = (cfg != NULL) ? (int)cfg->GetRecordQuality() : 0;
    AddComboRow(page.get(), TR(L"录屏画质"), TR(L"高清的分辨率最高 2560、码率约为标准的 2 倍，画面更清晰，文件也更大"),
                kIdRecordQuality, kRecordQualityOptions, kRecordQualityOptionCount, recordQuality, true);

    // 保存位置：左边标题与当前目录，右边「更改」
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kRowGap);
        std::unique_ptr<balloonwjui::DuiVBox> col(new balloonwjui::DuiVBox());
        col->SetGap(kRowTextGap);
        col->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                      balloonwjui::DuiLayout::Hint().Weight(1));
        std::unique_ptr<balloonwjui::DuiLabel> title(new balloonwjui::DuiLabel());
        title->SetText(TR(L"录屏保存位置"));
        title->SetTextColor(kRowTitleClr);
        title->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        col->AddChild(std::move(title), balloonwjui::DuiLayout::Hint().Fixed(kRowTitleH));
        std::unique_ptr<balloonwjui::DuiLabel> pathLbl(new balloonwjui::DuiLabel());
        pathLbl->SetText(RecordFolderDisplayText());
        pathLbl->SetTextColor(kValueClr);
        pathLbl->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        m_pRecordPathLabel = pathLbl.get();   // 借用：改目录后就地 SetText 刷新
        col->AddChild(std::move(pathLbl), balloonwjui::DuiLayout::Hint().Fixed(kRowSubH));
        col->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                      balloonwjui::DuiLayout::Hint().Weight(1));
        row->AddChild(std::move(col), balloonwjui::DuiLayout::Hint().Weight(1));
        std::unique_ptr<balloonwjui::DuiButton> change(new balloonwjui::DuiButton());
        change->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        change->SetVariant(balloonwjui::DuiButton::Variant::Default);
        change->SetCtrlId(kIdRecordFolder);
        change->SetText(TR(L"更改"));
        row->AddChild(std::move(change),
                      balloonwjui::DuiLayout::Hint().Fixed(i18n::DefaultButtonWidthForText(TR(L"更改"), kBtnWidth), kBtnHeight)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));
        page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
        AddRowDivider(page.get());
    }

    // 快捷键：Ctrl+Alt+R 开始 / 停止录屏
    bool recordHotkey = (cfg != NULL) ? (cfg->IsEnableRecordHotkey() != FALSE) : true;
    AddSwitchRow(page.get(), TR(L"录屏快捷键"), TR(L"按 Ctrl+Alt+R 开始录屏，录制中再按一次停止"),
                 kIdRecordHotkey, recordHotkey, false);

    page->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));
    return page;
}

// =======================================================================
// 快捷键页（只读键帽；发送 / 换行随聊天页设置联动，其余都是程序实际注册或处理的快捷键）
// =======================================================================

std::unique_ptr<balloonwjui::DuiVBox> CBalloonSystemSettingDlg::BuildShortcutPage()
{
    std::unique_ptr<balloonwjui::DuiVBox> page(new balloonwjui::DuiVBox());
    page->SetGap(0);

    CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;
    bool pressEnter = (cfg != NULL) ? (cfg->IsEnablePressEnterToSend() != FALSE) : true;

    // 发送 / 换行随"发送消息快捷键"设置联动，保持与聊天页一致。
    LPCTSTR sendKey = pressEnter ? _T("Enter") : _T("Ctrl + Enter");
    //Enter 发送时换行是 Ctrl + Enter（输入框只认 Ctrl，见 BalloonChatWindow.cpp 的 ChatSendOnEnterQuery；
    //此前这里写成 Shift + Enter，与实际不符）
    LPCTSTR newline = pressEnter ? _T("Ctrl + Enter") : _T("Enter");

    // 「打开主面板」的键帽文字由程序实际注册的组合生成，不再手写（BUG-99：手写的 Ctrl + Alt + F 与注册的 Ctrl+Alt+D 不一致）
    CString panelKeys = balloonscreenshot::FormatHotkey(
        balloonscreenshot::MakeHotkey(MOD_CONTROL | MOD_ALT, (UINT)balloonmain::kShowPanelHotKey),
        balloonscreenshot::kHotkeyTextKeyCap);
    AddKeyCapRow(page.get(), TR(L"打开主面板"), panelKeys, true);
    // 截图：可录入的键帽 + 「恢复默认」（2026-10-05）
    AddScreenshotHotkeyRow(page.get(), true);
    AddKeyCapRow(page.get(), TR(L"发送消息"),   sendKey, true);
    AddKeyCapRow(page.get(), TR(L"换行"),       newline, true);
    //快速搜索：主面板在前台时把光标移到搜索框（BalloonMainPanel 的 OnSearchNavKeyDown）。
    //原先还有一行「隐藏窗口 Esc」，从未实现，2026-10-10 用户决定不做，已删去
    AddKeyCapRow(page.get(), TR(L"快速搜索"),   _T("Ctrl + K"), false);

    page->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));
    return page;
}

// =======================================================================
// 存储页（真实保存路径 + 本地数据目录占用分段条；清理为占位）
// =======================================================================

std::unique_ptr<balloonwjui::DuiVBox> CBalloonSystemSettingDlg::BuildStoragePage()
{
    std::unique_ptr<balloonwjui::DuiVBox> page(new balloonwjui::DuiVBox());
    page->SetGap(0);

    // ---- 数据存储位置（可改：右侧"更改"按钮弹选目录框，改动须重启才生效）----
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kRowGap);

        // 左侧文字块：标题 + 当前生效路径（灰、超长省略），整体垂直居中。
        std::unique_ptr<balloonwjui::DuiVBox> col(new balloonwjui::DuiVBox());
        col->SetGap(kRowTextGap);
        col->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                      balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiLabel> title(new balloonwjui::DuiLabel());
        title->SetText(TR(L"数据存储位置"));
        title->SetTextColor(kRowTitleClr);
        title->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        col->AddChild(std::move(title), balloonwjui::DuiLayout::Hint().Fixed(kRowTitleH));

        std::unique_ptr<balloonwjui::DuiLabel> pathLbl(new balloonwjui::DuiLabel());
        pathLbl->SetText(DataRootDisplayText());
        pathLbl->SetTextColor(kValueClr);
        pathLbl->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        m_pRecvPathLabel = pathLbl.get();   // 借用：改路径后就地 SetText 刷新
        col->AddChild(std::move(pathLbl), balloonwjui::DuiLayout::Hint().Fixed(kRowSubH));

        col->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                      balloonwjui::DuiLayout::Hint().Weight(1));
        row->AddChild(std::move(col), balloonwjui::DuiLayout::Hint().Weight(1));

        // 右侧"更改"按钮。
        std::unique_ptr<balloonwjui::DuiButton> change(new balloonwjui::DuiButton());
        change->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        change->SetVariant(balloonwjui::DuiButton::Variant::Default);
        change->SetCtrlId(kIdChangeRecvPath);
        change->SetText(TR(L"更改"));
        row->AddChild(std::move(change),
                      balloonwjui::DuiLayout::Hint().Fixed(i18n::DefaultButtonWidthForText(TR(L"更改"), kBtnWidth), kBtnHeight)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));

        page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
        AddRowDivider(page.get());
    }

    // ---- 启动时清空历史日志（开关，落 flamingo.ini）----
    //
    // 这一项<u>不入 CUserConfig</u>：那份配置是按账号的、登录之后才加载，而清理动作
    // 发生在 WinMain 里、比登录早得多。取值规则见 LogCleanupPolicy.h。
    // 与「开机自启动」同类 —— 那一项落注册表，这一项落 flamingo.ini。
    AddSwitchRow(page.get(), TR(L"启动时清空历史日志"),
                 TR(L"每次启动删除以往的日志文件，只保留本次运行的这一份（重启后生效）"),
                 kIdClearLogOnStart, logcleanup::ReadClearLogOnStart(AppIniPath()), true);

    // ---- 清理本地缓存（按钮，占位）----
    AddButtonRow(page.get(), TR(L"清理本地缓存"), TR(L"清理聊天图片 / 文件缓存"),
                 kIdClearCache, TR(L"立即清理"), false, false);

    page->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));
    return page;
}

CString CBalloonSystemSettingDlg::EffectiveDataRoot()
{
    // 本次会话真正在用的数据根目录。它在进程启动时按 flamingo.ini 的 [storage] datapath
    // 算好（见 Startup.cpp 与 net/StorageLocation.h），运行期不再变。
    return CString(g_szDataRoot);
}

CString CBalloonSystemSettingDlg::AppIniPath()
{
    TCHAR szIniPath[MAX_PATH] = { 0 };
    _stprintf_s(szIniPath, MAX_PATH, _T("%sconfig\\flamingo.ini"), g_szHomePath);
    return CString(szIniPath);
}

std::wstring CBalloonSystemSettingDlg::LanguagePackPath()
{
    return std::wstring(g_szHomePath) + FLAMINGO_LANG_DLL_NAME;
}

LRESULT CBalloonSystemSettingDlg::OnLanguageChosenMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    OnLanguageChosen((int)wParam);
    return 0;
}

void CBalloonSystemSettingDlg::OnLanguageChosen(int index)
{
    if (index < 0 || index >= (int)m_languageChoices.size())
        return;
    const std::wstring code = m_languageChoices[(size_t)index].m_code;
    if (!i18n::WriteConfiguredLanguage(std::wstring(AppIniPath()), code))
    {
        LOG_ERROR("Settings: failed to save the ui language (error %u).", (unsigned)::GetLastError());
        ::MessageBox(m_hWnd, TR(L"保存设置失败，请检查程序目录是否可写。"), TR(L"语言 / Language"),
                     MB_OK | MB_ICONWARNING);
        return;
    }
    LOG_INFO("Settings: ui language set to %s, current language %s.",
             code.empty() ? "follow-system" : EncodeUtil::UnicodeToUtf8(code).c_str(),
             EncodeUtil::UnicodeToUtf8(i18n::GetLanguage()).c_str());

    //新配置下次启动得到的语言与当前一样（如从「简体中文」改成「跟随系统」而系统本来就是简体中文）时不必打扰用户
    if (!i18n::LanguageChangeNeedsRestart(code, ::GetUserDefaultUILanguage()))
        return;

    std::wstring prompt = TRF(L"界面语言将在重新启动 {0} 后生效。是否立即重启？", g_strAppTitle);
    if (::MessageBox(m_hWnd, prompt.c_str(), g_strAppTitle.c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES)
    {
        LOG_INFO("Settings: restart for the language change postponed by the user.");
        return;
    }
    if (m_ownerHwnd != NULL && ::IsWindow(m_ownerHwnd))
    {
        ::PostMessage(m_ownerHwnd, FMG_MSG_RESTART_APP, 0, 0);
        LOG_INFO("Settings: restart for the language change forwarded to the main panel.");
    }
    else
    {
        //只有调试入口会在没有主面板的情况下打开本对话框，此时无从退出整个程序
        LOG_WARNING("Settings: restart requested, but there is no main panel to handle it.");
    }
}

CString CBalloonSystemSettingDlg::ConfiguredDataRoot()
{
    // 配置里当前写着的值。用户刚改完还没重启时，它与 EffectiveDataRoot 不同 ——
    // 界面显示这一个，并在两者不一致时标注"重启后生效"，免得用户以为没改上。
    TCHAR szConfigured[MAX_PATH] = { 0 };
    CIniFile iniFile;
    iniFile.ReadString(_T("storage"), _T("datapath"), _T(""),
                       szConfigured, MAX_PATH, AppIniPath());
    return CString(szConfigured);
}

CString CBalloonSystemSettingDlg::DataRootDisplayText()
{
    const CString configured = ConfiguredDataRoot();
    const CString effective  = EffectiveDataRoot();

    // 没配置过：显示当前生效的（即程序目录）。
    if (configured.IsEmpty())
    {
        return effective;
    }

    // 配置过但与本次会话正在用的不一致，说明改完还没重启。
    CString normalized(configured);
    if (!normalized.IsEmpty() && normalized[normalized.GetLength() - 1] != _T('\\'))
    {
        normalized += _T('\\');
    }
    if (normalized.CompareNoCase(effective) == 0)
    {
        return effective;
    }
    return CString(TRF(L"{0}（重启后生效）", normalized).c_str());
}

CString CBalloonSystemSettingDlg::RecordFolderDisplayText()
{
    const CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;
    CString folder = (cfg != NULL) ? CString(cfg->GetRecordFolder()) : CString();
    if (!folder.IsEmpty())
    {
        return folder;
    }
    return TR(L"默认：「视频」文件夹下的 FlamingoRecords");
}

void CBalloonSystemSettingDlg::PushRecordSettings()
{
    const CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;
    if (cfg == NULL)
    {
        return;
    }
    balloonscreenshare::PushRecordSettings((int)cfg->GetRecordQuality(), cfg->GetRecordFolder());
}

void CBalloonSystemSettingDlg::ChooseRecordFolder()
{
    CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;
    if (cfg == NULL)
    {
        return;
    }
    //与「数据存储位置」同一种选目录框；BIF_NEWDIALOGSTYLE 要求调用线程已初始化 OLE，就地配平
    HRESULT hrCo = ::CoInitialize(NULL);
    BROWSEINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = m_hWnd;
    bi.lpszTitle = TR(L"选择录屏文件的保存位置");
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = ::SHBrowseForFolder(&bi);
    TCHAR szPath[MAX_PATH] = { 0 };
    BOOL gotPath = FALSE;
    if (pidl != NULL)
    {
        gotPath = ::SHGetPathFromIDList(pidl, szPath);
        ::CoTaskMemFree(pidl);
    }
    if (SUCCEEDED(hrCo))
    {
        ::CoUninitialize();
    }
    if (!gotPath || szPath[0] == _T('\0'))
    {
        return;   // 用户取消或取不到路径
    }
    cfg->SetRecordFolder(szPath);
    if (m_lpFMGClient != NULL)
    {
        m_lpFMGClient->SaveUserConfig();
    }
    PushRecordSettings();
    if (m_pRecordPathLabel != NULL)
    {
        m_pRecordPathLabel->SetText(RecordFolderDisplayText());
    }
    LOG_INFO("Settings: record folder changed.");
}

void CBalloonSystemSettingDlg::ChooseDataRoot()
{
    // 弹系统选目录框。BIF_NEWDIALOGSTYLE 需调用线程已初始化 OLE，故就地配平
    // CoInitialize / CoUninitialize（已初始化时返回 S_FALSE，仍需配平以不改引用计数语义）。
    HRESULT hrCo = ::CoInitialize(NULL);

    BROWSEINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = m_hWnd;
    bi.lpszTitle = TR(L"选择本地数据的存储位置");
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = ::SHBrowseForFolder(&bi);
    TCHAR szPath[MAX_PATH] = { 0 };
    BOOL  gotPath = FALSE;
    if (pidl != NULL)
    {
        gotPath = ::SHGetPathFromIDList(pidl, szPath);
        ::CoTaskMemFree(pidl);
    }

    if (SUCCEEDED(hrCo))
    {
        ::CoUninitialize();
    }

    if (!gotPath || szPath[0] == _T('\0'))
    {
        return;   // 用户取消或取不到路径
    }

    // 选中的就是当前正在用的目录：无需任何动作，也不必拿确认框打扰用户。
    CString picked(szPath);
    if (picked.IsEmpty() || picked[picked.GetLength() - 1] != _T('\\'))
    {
        picked += _T('\\');
    }
    if (picked.CompareNoCase(EffectiveDataRoot()) == 0)
    {
        return;
    }

    // 确认框：把后果与手动迁移步骤一次讲清。只说"重启后生效"是不够的 —— 用户多半是
    // 冲着"C 盘满了想挪到 D 盘"来的，未必预期聊天记录也跟着从头开始。
    const CString account = (m_lpFMGClient != NULL)
                          ? CString(m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount.c_str())
                          : CString(TR(L"<账号>"));
    CString prompt;
    prompt.Format(
        TR(L"更改后需要重启客户端才会生效。\n\n"
        L"新位置是空的：聊天记录、公告、本地设置都会从头开始。原有数据仍保留在\n"
        L"    %sUsers\\%s\\\n"
        L"但不再被使用。\n\n"
        L"如需保留原有数据，请手动迁移：\n"
        L"    1. 退出客户端；\n"
        L"    2. 把上面那个目录整个复制到\n"
        L"       %sUsers\\\n"
        L"    3. 再启动客户端。\n\n"
        L"是否继续？"),
        EffectiveDataRoot().GetString(), account.GetString(), picked.GetString());

    if (::MessageBox(m_hWnd, prompt, TR(L"更改数据存储位置"),
                     MB_OKCANCEL | MB_ICONWARNING) != IDOK)
    {
        return;
    }

    // 写全局配置。<u>不能</u>写进账号配置 —— <账号>.ini 本身就在要搬走的那棵树里，
    // 存进去的话下次启动会去新位置找它、找不到就用默认值，这一项永远不会生效
    // （原委见 net/StorageLocation.h）。
    CIniFile iniFile;
    if (!iniFile.WriteString(_T("storage"), _T("datapath"), picked, AppIniPath()))
    {
        LOG_ERROR(_T("系统设置：写入数据存储位置失败（错误码 %u）。"), ::GetLastError());
        ::MessageBox(m_hWnd, TR(L"保存设置失败，请检查程序目录是否可写。"),
                     TR(L"更改数据存储位置"), MB_OK | MB_ICONWARNING);
        return;
    }

    // 刷新显示（会带上"重启后生效"的标注），并提示重启。
    if (m_pRecvPathLabel != NULL)
    {
        m_pRecvPathLabel->SetText(DataRootDisplayText());
        m_pRecvPathLabel->Invalidate();
    }
    LOG_INFO(_T("系统设置：数据存储位置已改为 %s，重启后生效。"), picked.GetString());
    ::MessageBox(m_hWnd, TR(L"设置已保存，重启客户端后生效。"),
                 TR(L"更改数据存储位置"), MB_OK | MB_ICONINFORMATION);
}

// =======================================================================
// 账号安全页（修改密码复用面板改密对话框；自动登录转交主面板；其余按钮/二次验证为占位）
// =======================================================================

std::unique_ptr<balloonwjui::DuiVBox> CBalloonSystemSettingDlg::BuildAccountPage()
{
    std::unique_ptr<balloonwjui::DuiVBox> page(new balloonwjui::DuiVBox());
    page->SetGap(0);

    CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;
    bool twoFactor = (cfg != NULL) ? (cfg->IsEnableTwoFactor() != FALSE) : false;

    AddButtonRow(page.get(), TR(L"登录设备"), TR(L"管理已登录的设备"),
                 kIdManageDevices, TR(L"管理"), false, true);
    AddButtonRow(page.get(), TR(L"修改密码"), NULL, kIdChangePwd, TR(L"修改"), false, true);
    //自动登录：初始状态由主面板经 SetAutoLoginSwitchState 传入（本对话框读不到账号列表）
    AddSwitchRow(page.get(), TR(L"自动登录"), TR(L"启动客户端时自动登录当前账号，登录过程中可以取消"),
                 kIdAutoLogin, m_autoLoginOn, true);
    AddSwitchRow(page.get(), TR(L"二次验证"),
                 twoFactor ? TR(L"已开启") : TR(L"未开启"), kIdTwoFactor, twoFactor, true);
    AddButtonRow(page.get(), TR(L"登录日志"), NULL, kIdLoginLog, TR(L"查看"), false, true);
    AddButtonRow(page.get(), TR(L"注销账号"), NULL, kIdDeactivate, TR(L"注销"), /*danger*/true, false);

    page->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));
    return page;
}

// =======================================================================
// 骨架构建（一次）
// =======================================================================

std::unique_ptr<balloonwjui::DuiVBox> CBalloonSystemSettingDlg::BuildPlaceholderPage(
    const balloonsettings::SettingsNavEntry& entry)
{
    std::unique_ptr<balloonwjui::DuiVBox> page(new balloonwjui::DuiVBox());
    page->SetGap(kPageGap);

    // 页标题（取导航文本，方便切页时目视确认到位）。
    std::unique_ptr<balloonwjui::DuiLabel> title(new balloonwjui::DuiLabel());
    title->SetText(entry.title);
    title->SetTextColor(kPageTitleClr);
    title->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    page->AddChild(std::move(title), balloonwjui::DuiLayout::Hint().Fixed(kPageTitleH));

    // 占位提示（本期未落地页）。
    std::unique_ptr<balloonwjui::DuiLabel> note(new balloonwjui::DuiLabel());
    note->SetText(TR(L"该功能正在开发中，敬请期待。"));
    note->SetTextColor(kPageNoteClr);
    note->SetTextAlign(DT_LEFT | DT_TOP | DT_SINGLELINE);
    page->AddChild(std::move(note), balloonwjui::DuiLayout::Hint().Fixed(kPageNoteH));

    // 余下空间留白（把上面两行顶到内容区顶部）。
    std::unique_ptr<balloonwjui::DuiControl> spacer(new balloonwjui::DuiControl());
    page->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));

    return page;
}

void CBalloonSystemSettingDlg::BuildUiOnce()
{
    if (m_built)
    {
        return;
    }

    std::vector<balloonsettings::SettingsNavEntry> model = balloonsettings::BuildSettingsNavModel();

    std::unique_ptr<balloonwjui::DuiHBox> root(new balloonwjui::DuiHBox());
    root->SetGap(0);
    root->SetPadding(0);

    // ---- 左侧导航 ----
    std::unique_ptr<balloonsettings::CSettingsNavList> nav(new balloonsettings::CSettingsNavList());
    nav->SetCtrlId(kIdNavList);

    std::vector<CString> titles;
    titles.reserve(model.size());
    for (size_t i = 0; i < model.size(); ++i)
    {
        titles.push_back(CString(model[i].title));
    }
    nav->SetTitles(titles);
    nav->SetCurSel(0, false);
    m_pNav = nav.get();
    root->AddChild(std::move(nav), balloonwjui::DuiLayout::Hint().Fixed(kNavWidth));

    // ---- 导航与内容之间的竖直分隔线（对照设计稿左右分栏的浅灰竖线）----
    {
        std::unique_ptr<balloonwjui::DuiSeparator> vsep(new balloonwjui::DuiSeparator());
        vsep->SetOrientation(balloonwjui::DuiSeparator::Vertical);
        vsep->SetColor(kDividerClr);
        vsep->SetThickness(1);
        root->AddChild(std::move(vsep), balloonwjui::DuiLayout::Hint().Fixed(1));
    }

    // ---- 右侧内容容器（承载 7 分页）----
    std::unique_ptr<balloonwjui::DuiVBox> content(new balloonwjui::DuiVBox());
    content->SetPadding(kContentPadL, kContentPadT, kContentPadR, kContentPadB);
    content->SetGap(0);
    m_pContentHost = content.get();

    m_pages.clear();
    m_pages.resize(model.size(), NULL);
    for (size_t i = 0; i < model.size(); ++i)
    {
        std::unique_ptr<balloonwjui::DuiVBox> page;
        switch (model[i].id)
        {
        case balloonsettings::kPageGeneral:
            page = BuildGeneralPage();
            break;
        case balloonsettings::kPageNotify:
            page = BuildNotifyPage();
            break;
        case balloonsettings::kPageChat:
            page = BuildChatPage();
            break;
        case balloonsettings::kPageAudioVideo:
            page = BuildAudioVideoPage();
            break;
        case balloonsettings::kPageShortcut:
            page = BuildShortcutPage();
            break;
        case balloonsettings::kPageStorage:
            page = BuildStoragePage();
            break;
        case balloonsettings::kPageAccount:
            page = BuildAccountPage();
            break;
        default:
            page = BuildPlaceholderPage(model[i]);
            break;
        }
        m_pages[i] = page.get();
        // 每页 weight 撑满内容区；隐藏页布局时被 DuiVBox 跳过，可见页独占全高。
        content->AddChild(std::move(page), balloonwjui::DuiLayout::Hint().Weight(1));
    }

    root->AddChild(std::move(content), balloonwjui::DuiLayout::Hint().Weight(1));

    SetClientContent(std::move(root));

    // 初始仅显示第 0 页（首次真实布局发生在 OpenSettings 的 ResizeClient）。
    ApplyPageVisibility(0);

    // 「禁止机器休眠」若本来就开着，打开设置页时「自动离开」那两行就应当已经是灰的。
    // 放在建树之后：ApplyKeepAwakeExclusion 要靠 FindCtrlById 找控件，树没挂上就找不到。
    {
        CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;
        const bool keepAwakeOn = (cfg != NULL) ? (cfg->IsEnableKeepAwake() != FALSE) : false;
        ApplyKeepAwakeExclusion(keepAwakeOn);
    }

    // 「快捷键」页「截图」一行的提示按实际注册结果显示（如被其他程序占用）。同样要等树挂上之后。
    RefreshScreenshotHotkeyRow(NULL, false);

    m_built = true;
}

// =======================================================================
// 切页
// =======================================================================

void CBalloonSystemSettingDlg::ApplyPageVisibility(int index)
{
    std::vector<bool> vis = balloonsettings::ComputePageVisibility(index, (int)m_pages.size());
    for (size_t i = 0; i < m_pages.size(); ++i)
    {
        if (m_pages[i] != NULL)
        {
            bool v = (i < vis.size()) ? vis[i] : false;
            m_pages[i]->SetVisible(v);
        }
    }
    m_curPage = balloonsettings::ClampNavSelection(index, (int)m_pages.size());
}

void CBalloonSystemSettingDlg::ShowPage(int index)
{
    int target = balloonsettings::ClampNavSelection(index, (int)m_pages.size());
    if (target == m_curPage)
    {
        return;
    }

    ApplyPageVisibility(target);

    // 隐藏页在上次布局时被跳过、矩形为空；切到它须强制重排内容容器再重绘。
    if (m_pContentHost != NULL)
    {
        m_pContentHost->ForceLayout(m_pContentHost->GetRect());
        m_pContentHost->Invalidate();
    }
}

// =======================================================================
// 设置项变更落配置
// =======================================================================

void CBalloonSystemSettingDlg::ApplyKeepAwakeExclusion(bool keepAwakeOn)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    //开关与下拉框：置为禁用后 balloonui 会将其淡化绘制（DuiSwitch 整体按 40% 不透明度
    //绘制、DuiComboBox 改用禁用配色），并且不再响应鼠标，用户可以直接看出此刻不可操作。
    balloonwjui::DuiControl* sw = content->FindCtrlById(kIdAutoAway);
    if (sw != NULL)
    {
        sw->SetEnabled(!keepAwakeOn);
    }
    balloonwjui::DuiControl* combo = content->FindCtrlById(kIdAutoAwayDelay);
    if (combo != NULL)
    {
        combo->SetEnabled(!keepAwakeOn);
    }

    //副标题换成说明文案，讲清"此刻为何不可操作"。仅淡化而不作说明，用户会误以为界面异常。
    balloonwjui::DuiLabel* sub =
        dynamic_cast<balloonwjui::DuiLabel*>(content->FindCtrlById(kIdAutoAwaySubtitle));
    if (sub != NULL)
    {
        sub->SetText(keepAwakeOn ? TR(kAutoAwaySubtitleBlocked) : TR(kAutoAwaySubtitleNormal));
    }
}

bool CBalloonSystemSettingDlg::ApplySettingChange(UINT ctrlId, int value)
{
    bool on = (value != 0);

    // 开机自启动直接落注册表，不入配置文件。
    if (ctrlId == kIdAutoStart)
    {
        AutoStartSetEnabled(on);
        LOG_INFO(_T("系统设置：开机自启动已更新。"));
        return true;
    }

    // 启动时清空历史日志：落机器级的 config\flamingo.ini，同样不入账号配置 ——
    // 清理动作在 WinMain 里执行，那时账号配置还没加载（原委见 LogCleanupPolicy.h）。
    if (ctrlId == kIdClearLogOnStart)
    {
        if (!logcleanup::WriteClearLogOnStart(AppIniPath(), on))
        {
            LOG_ERROR("Settings: failed to save the clear-log-on-start switch (error %u).",
                      (unsigned)::GetLastError());
            ::MessageBox(m_hWnd, TR(L"保存设置失败，请检查程序目录是否可写。"),
                         TR(L"启动时清空历史日志"), MB_OK | MB_ICONWARNING);
            return true;
        }
        LOG_INFO("Settings: clear-log-on-start is now %s, effective on next launch.",
                 on ? "on" : "off");
        return true;
    }

    //界面语言：落机器级的 config\flamingo.ini 的 [ui] language，不入账号配置 —— 登录窗出现时
    //还不知道是哪个账号，语言却必须先定下来。改了要重启才生效，由 OnLanguageChosen 询问。
    //本通知来自下拉框浮层的消息处理过程，不能在这里同步弹询问框（原因见 kMsgLanguageChosen），先投给自己。
    if (ctrlId == kIdLanguage)
    {
        ::PostMessage(m_hWnd, kMsgLanguageChosen, (WPARAM)value, 0);
        return true;
    }

    //自动登录：标志存在本机历史账号列表里，列表归主面板的会话控制器持有，
    //故只记下新状态并转交主面板，由会话控制器修改列表并落盘（不写 CUserConfig）
    if (ctrlId == kIdAutoLogin)
    {
        m_autoLoginOn = on;
        if (m_ownerHwnd != NULL && ::IsWindow(m_ownerHwnd))
        {
            ::PostMessage(m_ownerHwnd, FMG_MSG_SET_AUTO_LOGIN, (WPARAM)(on ? 1 : 0), 0);
            LOG_INFO("Settings: auto-login switch turned %s, forwarded to the main panel.",
                     on ? "on" : "off");
        }
        else
        {
            //只有调试入口会在没有主面板的情况下打开本对话框，此时无处保存
            LOG_WARNING("Settings: auto-login switch turned %s, but there is no main panel to save it.",
                        on ? "on" : "off");
        }
        return true;
    }

    CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;
    if (cfg == NULL)
    {
        return false;   // 无配置源（防御），交回默认处理
    }

    bool handled = true;
    switch (ctrlId)
    {
    // ---- 通用页 ----
    case kIdCloseAction:
        cfg->EnableExitWhenCloseMainDlg(
            balloonsettings::ExitWhenCloseFromCloseActionIndex(value) ? TRUE : FALSE);
        break;
    case kIdAutoAway:
        cfg->EnableAutoAway(on ? TRUE : FALSE);
        break;
    case kIdAutoAwayDelay:
        cfg->SetAutoAwayDelayIndex(balloonsettings::ClampAutoAwayDelayIndex(value));
        break;
    //用户切换「禁止机器休眠」开关：写入配置，并立即把与之互斥的「自动离开」两行置为
    //禁用或恢复。真正获取与释放系统电源请求由自动离开控制器的定时器按配置执行（它每次触发
    //都会重新读取配置），本处不调用 Win32 API；相关日志见 BalloonMain/KeepAwakeSession.cpp。
    case kIdKeepAwake:
        cfg->EnableKeepAwake(on ? TRUE : FALSE);
        ApplyKeepAwakeExclusion(on);
        break;
    //用户切换「主面板贴边自动隐藏」开关：只写配置。主面板正贴边时，贴边控制器下一次轮询
    //（最多 100 毫秒后）读到关闭就会把收起的主面板展开并取消置顶；开启则从下一次拖到
    //屏幕边缘起生效
    case kIdDockAutoHide:
        cfg->EnableDockAutoHide(on ? TRUE : FALSE);
        LOG_INFO("Settings: dock auto-hide switch turned %s.", on ? "on" : "off");
        break;

    // ---- 消息通知页 ----
    case kIdNewMsgNotify:
        cfg->EnableNewMsgNotify(on ? TRUE : FALSE);
        break;
    case kIdDesktopPopup:
        cfg->EnableDesktopPopup(on ? TRUE : FALSE);
        break;
    case kIdSound:
        cfg->EnableMute(balloonsettings::MuteFromSoundOn(on) ? TRUE : FALSE);
        break;
    case kIdShakeFlash:
        cfg->EnableShakeFlashIcon(on ? TRUE : FALSE);
        break;
    case kIdDoNotDisturb:
        cfg->EnableDoNotDisturb(on ? TRUE : FALSE);
        break;
    case kIdNotifyAfterLock:
        cfg->EnableNotifyAfterLock(on ? TRUE : FALSE);
        break;

    // ---- 聊天页 ----
    case kIdSendKey:
        //经 ChatSendMode 写：它绑定的就是本人账号配置，聊天窗按回车与 ▾ 菜单读的是同一份（BUG-119）；
        //再请主面板刷新已打开聊天窗的左下角提示
        balloonchatwnd::SetChatSendMode(balloonsettings::PressEnterFromSendKeyIndex(value)
                                            ? balloonchatwnd::kSendOnEnter : balloonchatwnd::kSendOnCtrlEnter);
        if (::IsWindow(m_ownerHwnd))
        {
            ::PostMessage(m_ownerHwnd, FMG_MSG_SEND_MODE_CHANGED, 0, 0);
        }
        break;
    case kIdAutoRecvImage:
        cfg->EnableAutoRecvImage(on ? TRUE : FALSE);
        break;
    case kIdAutoRecvFile:
        cfg->EnableAutoRecvFile(on ? TRUE : FALSE);
        break;
    case kIdMsgRoaming:
        cfg->EnableMsgRoaming(on ? TRUE : FALSE);
        break;
    //截图时隐藏当前窗口：下一次从聊天窗发起截图时生效。2026-10-07 起存在全局配置里，这里立即写入
    case kIdScreenshotHideWindow:
    {
        balloonscreenshot::ScreenshotPrefs prefs = balloonscreenshot::LoadGlobalScreenshotPrefs();
        prefs.m_hideWindow = on;
        balloonscreenshot::SaveGlobalScreenshotPrefs(prefs);
        break;
    }
    case kIdLinkDetect:
        cfg->EnableLinkDetect(on ? TRUE : FALSE);
        // 已打开的聊天窗要立刻跟着变：请主面板更新进程级开关并让各窗重新测量重绘。
        // 设置窗自己够不着那些窗口（它们归主面板持有），故经主面板中转。
        if (m_ownerHwnd != NULL && ::IsWindow(m_ownerHwnd))
        {
            ::PostMessage(m_ownerHwnd, FMG_MSG_LINK_OPTION_CHANGED,
                          (WPARAM)(on ? 1 : 0), 0);
        }
        break;

    // ---- 音视频页 ----
    case kIdMicDevice:
        cfg->SetAudioInputIndex(value);
        break;
    case kIdSpeakerDevice:
        cfg->SetAudioOutputIndex(value);
        break;
    case kIdCameraDevice:
        cfg->SetCameraIndex(value);
        break;
    case kIdEchoCancel:
        cfg->EnableEchoCancel(on ? TRUE : FALSE);
        break;
    //录屏画质：写配置，并立即交给 FlamingoMedia.dll（下一次录制生效）
    case kIdRecordQuality:
        cfg->SetRecordQuality(value == 1 ? 1 : 0);
        PushRecordSettings();
        break;
    //录屏快捷键：写配置，并立即在主面板上注册或注销
    case kIdRecordHotkey:
        cfg->EnableRecordHotkey(on ? TRUE : FALSE);
        if (m_ownerHwnd != NULL && ::IsWindow(m_ownerHwnd))
        {
            balloonscreenshare::ApplyRecordHotkey(m_ownerHwnd, on);
        }
        break;

    // ---- 聊天页：自动回复两个开关，切换后同步右侧输入框能否编辑 ----
    //「离开时自动回复」开关
    case kIdAutoReplyAway:
        cfg->EnableAutoReplyAway(on ? TRUE : FALSE);
        balloonsettings::ApplyAutoReplyInputEnabled(GetClientContent(), kIdAutoReplyAway, kIdAutoReplyAwayText);
        PushAutoReplyToServer();
        break;
    //「忙碌时自动回复」开关
    case kIdAutoReplyBusy:
        cfg->EnableAutoReplyBusy(on ? TRUE : FALSE);
        balloonsettings::ApplyAutoReplyInputEnabled(GetClientContent(), kIdAutoReplyBusy, kIdAutoReplyBusyText);
        PushAutoReplyToServer();
        break;

    // ---- 账号安全页 ----
    case kIdTwoFactor:
        cfg->EnableTwoFactor(on ? TRUE : FALSE);
        break;

    default:
        handled = false;
        break;
    }

    if (handled && m_lpFMGClient != NULL)
    {
        // 设计无"保存"按钮，改即生效：立刻持久化到该账号的 .ini。
        m_lpFMGClient->SaveUserConfig();
        LOG_INFO(_T("系统设置项已更新并保存。"));
    }
    return handled;
}

// =======================================================================
// 按钮点击处理（账号安全 / 存储 / 音视频里的各按钮）
// =======================================================================

bool CBalloonSystemSettingDlg::HandleButtonClick(UINT ctrlId)
{
    switch (ctrlId)
    {
    case kIdChangePwd:
        // 转发主面板，复用其持有的改密对话框（保证改密应答能回投到同一实例）。
        if (m_ownerHwnd != NULL && ::IsWindow(m_ownerHwnd))
        {
            ::PostMessage(m_ownerHwnd, FMG_MSG_OPEN_CHANGEPWD, 0, 0);
        }
        else
        {
            ::MessageBox(m_hWnd, TR(L"请从主面板系统菜单进入修改密码。"),
                         TR(L"修改密码"), MB_OK | MB_ICONINFORMATION);
        }
        return true;

    case kIdMicTest:
        ::MessageBox(m_hWnd, TR(L"麦克风测试功能开发中。"),
                     TR(L"系统设置"), MB_OK | MB_ICONINFORMATION);
        return true;

    //「快捷键」页的截图键帽：进入录入状态
    case kIdShotHotkeyBox:
        BeginScreenshotHotkeyCapture();
        return true;

    //「快捷键」页截图一行的「恢复默认」：恢复为 Alt+Z
    case kIdShotHotkeyReset:
        ApplyScreenshotHotkeyChoice(balloonscreenshot::kDefaultScreenshotHotkey);
        return true;

    //「默认聊天字体」的「修改」：打开字体浮层编辑全局默认
    case kIdChatFontEdit:
        OpenDefaultChatFontPopup();
        return true;

    //「默认聊天字体」的「恢复默认」：清除全局默认，发出的消息不再带字体
    case kIdChatFontReset:
        ApplyDefaultChatFont(net::TextFont());
        return true;

    //「立即清理」：弹清理对话框，由它列出各类缓存的占用量并按用户勾选执行删除。
    case kIdClearCache:
        m_cacheCleanDlg.OpenDialog(m_hWnd);
        return true;

    //聊天页「清理聊天记录」的「清理…」：弹按时间范围清理的对话框
    case kIdChatClean:
        OpenChatCleanDialog();
        return true;

    //聊天页「我的机器人」的「打开…」：打开 FlamingoBot.dll 的「我的机器人」窗口；插件不可用时什么都不做
    case kIdMyBots:
        botui::OpenMyBots(m_hWnd);
        return true;

    //聊天页「快捷回复」的「恢复默认」：三条都换回默认文字并立即存盘
    case kIdQuickReplyReset:
        ResetQuickReplies();
        return true;

    case kIdChangeRecvPath:
        ChooseDataRoot();
        return true;

    case kIdRecordFolder:
        ChooseRecordFolder();
        return true;

    case kIdManageDevices:
        ::MessageBox(m_hWnd, TR(L"登录设备管理功能开发中。"),
                     TR(L"系统设置"), MB_OK | MB_ICONINFORMATION);
        return true;

    case kIdLoginLog:
        ::MessageBox(m_hWnd, TR(L"登录日志功能开发中。"),
                     TR(L"系统设置"), MB_OK | MB_ICONINFORMATION);
        return true;

    case kIdDeactivate:
        // 危险且不可逆，且服务端未接入：仅提示，不执行任何注销动作。
        ::MessageBox(m_hWnd, TR(L"注销账号为不可逆操作，该功能尚在开发中，暂不可用。"),
                     TR(L"注销账号"), MB_OK | MB_ICONWARNING);
        return true;

    default:
        return false;
    }
}

// =======================================================================
// 事件处理
// =======================================================================

LRESULT CBalloonSystemSettingDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // 按钮点击（DUIN_CLICK）→ 各按钮动作。未知 ctrlId（如标题栏关闭按钮、导航冒泡的
    // 点击）放行 chain，交给 DuiFrameWindow 处理。
    if (pNotify->code == balloonwjui::DUIN_CLICK)
    {
        if (HandleButtonClick(pNotify->ctrlId))
        {
            return 0;
        }
        bHandled = FALSE;
        return 0;
    }

    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED)
    {
        // 左侧导航选中变化 → 切换右侧分页。
        if (pNotify->ctrlId == kIdNavList)
        {
            ShowPage((int)pNotify->extra);
            return 0;
        }

        // 截图键帽录入到新组合（extra 为组合编码，0 表示停用）→ 校验、保存并重新注册。
        // 必须排在 ApplySettingChange 之前：键帽不是普通的开关或下拉，值要先校验。
        if (pNotify->ctrlId == kIdShotHotkeyBox)
        {
            ApplyScreenshotHotkeyChoice((DWORD)pNotify->extra);
            return 0;
        }

        // 自动回复的文字改了 → 只改内存里的配置，存盘留到失去焦点或关窗（见 FlushAutoReplyText）。
        // 必须排在 ApplySettingChange 之前：后者处理的设置项改一次就存一次盘。
        if (pNotify->ctrlId == kIdAutoReplyAwayText || pNotify->ctrlId == kIdAutoReplyBusyText)
        {
            OnAutoReplyTextChanged(pNotify->ctrlId);
            return 0;
        }

        // 快捷回复的文字改了 → 同上，只改内存里的配置，存盘留到失去焦点或关窗（见 FlushQuickReplyText）
        if (QuickReplyIndexOfEdit(pNotify->ctrlId) >= 0)
        {
            OnQuickReplyTextChanged(pNotify->ctrlId);
            return 0;
        }

        // 设置项（开关 / 下拉）变更 → 写配置并即时持久化。
        if (ApplySettingChange(pNotify->ctrlId, (int)pNotify->extra))
        {
            return 0;
        }
    }

    // 截图键帽的录入被取消（Esc / 失去焦点）→ 组合不变，恢复本程序的全局快捷键。
    // 通知码按控件各自编号，必须连同 ctrlId 一起判断（UI 约定第 4 条）
    if (pNotify->code == (UINT)balloonsettings::CHotkeyCaptureBox::DUIN_HOTKEY_CAPTURE_CANCELLED
        && pNotify->ctrlId == kIdShotHotkeyBox)
    {
        EndScreenshotHotkeyCapture();
        return 0;
    }

    // 自动回复输入框失去焦点 → 把改过的文字存盘
    if (pNotify->code == balloonwjui::DUIN_KILLFOCUS
        && (pNotify->ctrlId == kIdAutoReplyAwayText || pNotify->ctrlId == kIdAutoReplyBusyText))
    {
        FlushAutoReplyText();
        return 0;
    }

    // 快捷回复输入框失去焦点 → 把改过的文字存盘
    if (pNotify->code == balloonwjui::DUIN_KILLFOCUS && QuickReplyIndexOfEdit(pNotify->ctrlId) >= 0)
    {
        FlushQuickReplyText();
        return 0;
    }

    bHandled = FALSE;
    return 0;
}
