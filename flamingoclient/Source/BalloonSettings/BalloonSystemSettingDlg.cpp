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

#include "../IULog.h"
#include "../FlamingoClient.h"            // CFlamingoClient::m_UserConfig / SaveUserConfig
#include "../UserSessionData.h"           // FMG_MSG_OPEN_CHANGEPWD（"修改密码"转发主面板）、g_szHomePath / g_szDataRoot
#include "../IniFile.h"                  // CIniFile（数据存储位置存在全局 flamingo.ini 里）
#include "BalloonCacheCleanDlg.h"        // 「立即清理」弹出的清理对话框
#include "SettingsNavList.h"
#include "SettingsPageModel.h"
#include "SettingsBinding.h"
#include "../BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏左侧图标）
#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"        // DuiVBox / DuiHBox
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiBadge.h"
#include "../../balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../balloonui/balloonui/DuiAnimation.h"                    // DuiAnimMgr::TickAll（开关滑动动画需 host 周期性驱动）

// ---- 对话框尺寸（逻辑像素）---------------------------------------------
static const int kDlgClientW  = 720;   // 客户区宽（较初版收紧，去掉过宽留白）
static const int kDlgClientH  = 500;   // 客户区高
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

// ---- 动画时钟 ----------------------------------------------------------
// 开关（DuiSwitch）等动画控件鼠标翻转时走 150ms 滑动动画，由 DuiAnimMgr / DuiDoubleAnim
// 驱动；host 必须周期性调 DuiAnimMgr::Inst().TickAll(GetTickCount()) 才推进中间帧（见
// DuiSwitch.h 注释，与 balloonui 的 LoginFrame / GalleryFrame 同套路）。否则开关点击后逻辑
// 状态翻转、DUIN_VALUECHANGED 也发出来了，但滑块 / 底色停在原处，视觉上像"拨不动、没反应"。
static const UINT_PTR kAnimTickTimerId    = 0xA10;   // 动画 pulse 定时器 id（与业务 timer 不撞即可）
static const UINT     kAnimTickIntervalMs = 16;      // 帧间隔（毫秒），约 60Hz

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
    _T("电脑无操作时自动切到离开状态，一有操作再自动切回在线");
static const TCHAR* const kAutoAwaySubtitleBlocked =
    _T("已开启「禁止机器休眠」，自动离开暂不生效");

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
    kIdFontScale       = 104,   // 字体大小（下拉，本期仅存）
    kIdAutoAway        = 105,   // 无操作自动切到"离开"（开关）→ EnableAutoAway
    kIdAutoAwayDelay   = 106,   // 自动离开的等待时长（下拉）→ SetAutoAwayDelayIndex
    kIdKeepAwake       = 107,   // 禁止机器休眠（开关）→ EnableKeepAwake；开启时把上面两项置灰
    kIdAutoAwaySubtitle = 108,  // 「自动离开」那行的副标题（置灰时要换成解释文案，故给它一个 id）

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

    // 音视频页
    kIdMicDevice       = 401,   // 麦克风设备（下拉）
    kIdSpeakerDevice   = 402,   // 扬声器设备（下拉）
    kIdCameraDevice    = 403,   // 摄像头设备（下拉）
    kIdMicTest         = 404,   // 麦克风测试（按钮，占位）
    kIdEchoCancel      = 405,   // 回声消除（开关）

    // 存储页
    kIdClearCache      = 501,   // 清理本地缓存（按钮，占位）
    kIdChangeRecvPath  = 502,   // 更改文件保存位置（按钮，弹选目录框）

    // 账号安全页
    kIdManageDevices   = 601,   // 登录设备管理（按钮，占位）
    kIdChangePwd       = 602,   // 修改密码（按钮，转发主面板改密对话框）
    kIdTwoFactor       = 603,   // 二次验证（开关，占位）
    kIdLoginLog        = 604,   // 登录日志（按钮，占位）
    kIdDeactivate      = 605,   // 注销账号（按钮，危险，占位）
};

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
        combo->AddString(_T("无可用麦克风"));
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
        combo->AddString(_T("无可用扬声器"));
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
    , m_pNav(NULL)
    , m_pContentHost(NULL)
    , m_pRecvPathLabel(NULL)
    , m_hTitleIcon(NULL)
    , m_cacheCleanDlg(pFMGClient)
{
}

CBalloonSystemSettingDlg::~CBalloonSystemSettingDlg()
{
    // SetIcon 只借用位图、不负责释放，故由本类收尾。
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
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
        if (Create(NULL, CWindow::rcDefault, _T("系统设置"),
                   WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR(_T("系统设置对话框 Create 失败。"));
            return;
        }
        SetTitle(_T("系统设置"));
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
        SetButtons(false, false, true);   // 只留关闭按钮
        SetMinSize(kDlgMinW, kDlgMinH);

        m_built = false;
        BuildUiOnce();
        ResizeClient(kDlgClientW, kDlgClientH);
        CenterWindow();

        // 启动动画 pulse 定时器：开关等动画控件的滑动中间帧靠它周期性 TickAll 推进。
        // 跟随窗口生命周期：本窗口在 OnCloseMsg 里 DestroyWindow，KillTimer 同处收尾。
        SetTimer(kAnimTickTimerId, kAnimTickIntervalMs);
    }

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO(_T("系统设置对话框已打开。"));
}

LRESULT CBalloonSystemSettingDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 关闭 = 隐藏并销毁本窗口（对象由主面板持有，下次打开重建）。先停掉动画 pulse 定时器。
    if (IsWindow())
    {
        KillTimer(kAnimTickTimerId);
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonSystemSettingDlg::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam,
                                             LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam == kAnimTickTimerId)
    {
        // 推进所有在跑的 DuiAnimMgr 动画一帧（开关滑动 / 底色渐变等）。无在跑动画时本调用
        // 廉价、不产生重绘。
        balloonwjui::DuiAnimMgr::Inst().TickAll(::GetTickCount());
        return 0;
    }

    // 非动画定时器：放行 chain，交给 DuiFrameWindow / 其它处理。
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

    std::unique_ptr<balloonwjui::DuiLabel> lblTitle(new balloonwjui::DuiLabel());
    lblTitle->SetText(title);
    lblTitle->SetTextColor(kRowTitleClr);
    lblTitle->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    col->AddChild(std::move(lblTitle), balloonwjui::DuiLayout::Hint().Fixed(kRowTitleH));

    if (subtitle != NULL && subtitle[0] != _T('\0'))
    {
        std::unique_ptr<balloonwjui::DuiLabel> lblSub(new balloonwjui::DuiLabel());
        lblSub->SetText(subtitle);
        lblSub->SetTextColor(kRowSubClr);
        lblSub->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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

void CBalloonSystemSettingDlg::AddComboRow(balloonwjui::DuiVBox* page, LPCTSTR title,
                                           LPCTSTR subtitle, UINT ctrlId,
                                           const wchar_t* const* options, int count,
                                           int selIndex, bool addDivider)
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kRowGap);

    row->AddChild(BuildRowTextBlock(title, subtitle), balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiComboBox> combo(new balloonwjui::DuiComboBox());
    combo->SetCtrlId(ctrlId);
    for (int i = 0; i < count; ++i)
    {
        // 包一层 CString，兼容 Unicode / MBCS 两种字符集（选项表是 wchar_t*）。
        combo->AddString(CString(options[i]));
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

    // 开机自启动（真值来自注册表 Run 项）。
    AddSwitchRow(page.get(), _T("开机自启动"), _T("登录系统后自动启动 FlamingoIM"),
                 kIdAutoStart, AutoStartIsEnabled(), true);

    // 关闭主面板时（下拉：最小化到托盘 / 退出）。
    bool exitWhenClose = (cfg != NULL) ? (cfg->IsEnableExitWhenCloseMainDlg() != FALSE) : false;
    AddComboRow(page.get(), _T("关闭主面板时"), _T("关闭后保留在系统托盘"),
                kIdCloseAction, balloonsettings::CloseActionOptions(),
                balloonsettings::CloseActionOptionCount(),
                balloonsettings::CloseActionIndexFromExitWhenClose(exitWhenClose), true);

    // 禁止机器休眠（开关）。放在「自动离开」<u>之前</u>是有意为之：它一旦开启就会把下面
    // 两行置为禁用，因果顺序与阅读顺序一致 —— 用户操作的是这一行，变化发生在其下方。
    bool keepAwake = (cfg != NULL) ? (cfg->IsEnableKeepAwake() != FALSE) : false;
    //副标题长度须与本页其它行相当：行内文字块用的是单行不折行、不加省略号的绘制方式，
    //写长了会一直画到右侧开关底下（实测 32 个字就被压住了）。
    AddSwitchRow(page.get(), _T("禁止机器休眠"),
                 _T("开启后本机不会自动休眠、关屏或锁屏，适合演示等场景"),
                 kIdKeepAwake, keepAwake, true);

    // 无操作自动离开（开关）+ 等待时长（下拉）。两行挨着放，读起来就是一句话：
    // "自动离开 —— 电脑无操作 5 分钟后自动切到离开状态"。
    // 副标题给了 id：被上面那个开关置灰时要换成解释文案（见 ApplyKeepAwakeExclusion）。
    bool autoAway = (cfg != NULL) ? (cfg->IsEnableAutoAway() != FALSE) : true;
    AddSwitchRow(page.get(), _T("自动离开"), kAutoAwaySubtitleNormal,
                 kIdAutoAway, autoAway, true, kIdAutoAwaySubtitle);

    int awayDelayIdx = (cfg != NULL)
                     ? balloonsettings::ClampAutoAwayDelayIndex(cfg->GetAutoAwayDelayIndex())
                     : balloonsettings::kAutoAwayDelay5Min;
    AddComboRow(page.get(), _T("无操作多久后切换成离开"), NULL,
                kIdAutoAwayDelay, balloonsettings::AutoAwayDelayOptions(),
                balloonsettings::AutoAwayDelayOptionCount(), awayDelayIdx, true);

    // 语言（下拉，本期仅存）。
    int langIdx = (cfg != NULL) ? balloonsettings::ClampLanguageIndex(cfg->GetLanguageIndex()) : 0;
    AddComboRow(page.get(), _T("语言"), NULL,
                kIdLanguage, balloonsettings::LanguageOptions(),
                balloonsettings::LanguageOptionCount(), langIdx, true);

    // 字体大小（下拉，本期仅存）。
    int fontIdx = (cfg != NULL)
                ? balloonsettings::ClampFontScaleIndex(cfg->GetUiFontScaleIndex())
                : balloonsettings::kFontScaleMedium;
    AddComboRow(page.get(), _T("字体大小"), NULL,
                kIdFontScale, balloonsettings::FontScaleOptions(),
                balloonsettings::FontScaleOptionCount(), fontIdx, false);   // 末行不加分隔线

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

    AddSwitchRow(page.get(), _T("新消息提醒"), NULL, kIdNewMsgNotify, newMsg, true);
    AddSwitchRow(page.get(), _T("桌面弹窗"), _T("新消息时弹出气泡通知"), kIdDesktopPopup, popup, true);
    AddSwitchRow(page.get(), _T("提示音"), NULL, kIdSound, soundOn, true);
    AddSwitchRow(page.get(), _T("震动闪烁图标"), NULL, kIdShakeFlash, shake, true);
    AddSwitchRow(page.get(), _T("勿扰时段"), _T("22:00 - 08:00 不接收任何提醒"), kIdDoNotDisturb, dnd, true);
    AddSwitchRow(page.get(), _T("锁屏后通知"), NULL, kIdNotifyAfterLock, afterLock, false);

    page->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));
    return page;
}

std::unique_ptr<balloonwjui::DuiVBox> CBalloonSystemSettingDlg::BuildChatPage()
{
    std::unique_ptr<balloonwjui::DuiVBox> page(new balloonwjui::DuiVBox());
    page->SetGap(0);

    CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;

    bool pressEnter   = (cfg != NULL) ? (cfg->IsEnablePressEnterToSend() != FALSE) : true;
    bool autoRecvImg  = (cfg != NULL) ? (cfg->IsEnableAutoRecvImage()    != FALSE) : true;
    bool autoRecvFile = (cfg != NULL) ? (cfg->IsEnableAutoRecvFile()     != FALSE) : false;
    bool roaming      = (cfg != NULL) ? (cfg->IsEnableMsgRoaming()       != FALSE) : true;
    bool linkDetect   = (cfg != NULL) ? (cfg->IsEnableLinkDetect()       != FALSE) : true;

    // 发送消息快捷键（下拉：Enter / Ctrl+Enter）。
    AddComboRow(page.get(), _T("发送消息快捷键"), NULL,
                kIdSendKey, balloonsettings::SendKeyOptions(),
                balloonsettings::SendKeyOptionCount(),
                balloonsettings::SendKeyIndexFromPressEnter(pressEnter), true);

    AddSwitchRow(page.get(), _T("自动接收图片"), NULL, kIdAutoRecvImage, autoRecvImg, true);
    AddSwitchRow(page.get(), _T("自动接收文件"), NULL, kIdAutoRecvFile, autoRecvFile, true);
    AddSwitchRow(page.get(), _T("消息漫游"), _T("多设备间同步最近 30 天的消息"), kIdMsgRoaming, roaming, true);
    AddSwitchRow(page.get(), _T("自动识别链接"), _T("关闭后聊天里的网址以普通文字显示"),
                 kIdLinkDetect, linkDetect, false);

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
                  balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth, kBtnHeight)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));

    page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    if (addDivider)
    {
        AddRowDivider(page);
    }
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
        row->AddChild(BuildRowTextBlock(_T("麦克风"), NULL), balloonwjui::DuiLayout::Hint().Weight(1));

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
        row->AddChild(BuildRowTextBlock(_T("扬声器"), NULL), balloonwjui::DuiLayout::Hint().Weight(1));

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
        row->AddChild(BuildRowTextBlock(_T("摄像头"), NULL), balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiComboBox> combo(new balloonwjui::DuiComboBox());
        combo->SetCtrlId(kIdCameraDevice);
        combo->AddString(_T("系统默认摄像头"));
        combo->SetCurSel(0, false);
        row->AddChild(std::move(combo),
                      balloonwjui::DuiLayout::Hint().Fixed(kComboWideW, kComboH)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));
        page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
        AddRowDivider(page.get());
    }

    // 麦克风测试（按钮，占位 —— 无 A/V 采集管线，点击仅提示）。
    AddButtonRow(page.get(), _T("麦克风测试"), NULL, kIdMicTest, _T("测试"), false, true);

    // 回声消除（开关，本期仅存）。
    bool echo = (cfg != NULL) ? (cfg->IsEnableEchoCancel() != FALSE) : true;
    AddSwitchRow(page.get(), _T("回声消除"), NULL, kIdEchoCancel, echo, false);

    page->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));
    return page;
}

// =======================================================================
// 快捷键页（只读键帽；发送 / 换行随聊天页设置联动，其余为文档默认值）
// =======================================================================

std::unique_ptr<balloonwjui::DuiVBox> CBalloonSystemSettingDlg::BuildShortcutPage()
{
    std::unique_ptr<balloonwjui::DuiVBox> page(new balloonwjui::DuiVBox());
    page->SetGap(0);

    CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;
    bool pressEnter = (cfg != NULL) ? (cfg->IsEnablePressEnterToSend() != FALSE) : true;

    // 发送 / 换行随"发送消息快捷键"设置联动，保持与聊天页一致。
    LPCTSTR sendKey = pressEnter ? _T("Enter") : _T("Ctrl + Enter");
    LPCTSTR newline = pressEnter ? _T("Shift + Enter") : _T("Enter");

    AddKeyCapRow(page.get(), _T("打开主面板"), _T("Ctrl + Alt + F"), true);
    AddKeyCapRow(page.get(), _T("截图"),       _T("Ctrl + Alt + A"), true);
    AddKeyCapRow(page.get(), _T("发送消息"),   sendKey, true);
    AddKeyCapRow(page.get(), _T("换行"),       newline, true);
    AddKeyCapRow(page.get(), _T("快速搜索"),   _T("Ctrl + K"), true);
    AddKeyCapRow(page.get(), _T("隐藏窗口"),   _T("Esc"), false);

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
        title->SetText(_T("数据存储位置"));
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
        change->SetText(_T("更改"));
        row->AddChild(std::move(change),
                      balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth, kBtnHeight)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));

        page->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
        AddRowDivider(page.get());
    }

    // ---- 清理本地缓存（按钮，占位）----
    AddButtonRow(page.get(), _T("清理本地缓存"), _T("清理聊天图片 / 文件缓存"),
                 kIdClearCache, _T("立即清理"), false, false);

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

CString CBalloonSystemSettingDlg::ConfiguredDataRoot()
{
    // 配置里当前写着的值。用户刚改完还没重启时，它与 EffectiveDataRoot 不同 ——
    // 界面显示这一个，并在两者不一致时标注"重启后生效"，免得用户以为没改上。
    TCHAR szIniPath[MAX_PATH] = { 0 };
    _stprintf_s(szIniPath, MAX_PATH, _T("%sconfig\\flamingo.ini"), g_szHomePath);

    TCHAR szConfigured[MAX_PATH] = { 0 };
    CIniFile iniFile;
    iniFile.ReadString(_T("storage"), _T("datapath"), _T(""),
                       szConfigured, MAX_PATH, szIniPath);
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
    return normalized + _T("（重启后生效）");
}

void CBalloonSystemSettingDlg::ChooseDataRoot()
{
    // 弹系统选目录框。BIF_NEWDIALOGSTYLE 需调用线程已初始化 OLE，故就地配平
    // CoInitialize / CoUninitialize（已初始化时返回 S_FALSE，仍需配平以不改引用计数语义）。
    HRESULT hrCo = ::CoInitialize(NULL);

    BROWSEINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = m_hWnd;
    bi.lpszTitle = _T("选择本地数据的存储位置");
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
                          : CString(_T("<账号>"));
    CString prompt;
    prompt.Format(
        _T("更改后需要重启客户端才会生效。\n\n")
        _T("新位置是空的：聊天记录、公告、本地设置都会从头开始。原有数据仍保留在\n")
        _T("    %sUsers\\%s\\\n")
        _T("但不再被使用。\n\n")
        _T("如需保留原有数据，请手动迁移：\n")
        _T("    1. 退出客户端；\n")
        _T("    2. 把上面那个目录整个复制到\n")
        _T("       %sUsers\\\n")
        _T("    3. 再启动客户端。\n\n")
        _T("是否继续？"),
        EffectiveDataRoot().GetString(), account.GetString(), picked.GetString());

    if (::MessageBox(m_hWnd, prompt, _T("更改数据存储位置"),
                     MB_OKCANCEL | MB_ICONWARNING) != IDOK)
    {
        return;
    }

    // 写全局配置。<u>不能</u>写进账号配置 —— <账号>.cfg 本身就在要搬走的那棵树里，
    // 存进去的话下次启动会去新位置找它、找不到就用默认值，这一项永远不会生效
    // （原委见 net/StorageLocation.h）。
    TCHAR szIniPath[MAX_PATH] = { 0 };
    _stprintf_s(szIniPath, MAX_PATH, _T("%sconfig\\flamingo.ini"), g_szHomePath);

    CIniFile iniFile;
    if (!iniFile.WriteString(_T("storage"), _T("datapath"), picked, szIniPath))
    {
        LOG_ERROR(_T("系统设置：写入数据存储位置失败（错误码 %u）。"), ::GetLastError());
        ::MessageBox(m_hWnd, _T("保存设置失败，请检查程序目录是否可写。"),
                     _T("更改数据存储位置"), MB_OK | MB_ICONWARNING);
        return;
    }

    // 刷新显示（会带上"重启后生效"的标注），并提示重启。
    if (m_pRecvPathLabel != NULL)
    {
        m_pRecvPathLabel->SetText(DataRootDisplayText());
        m_pRecvPathLabel->Invalidate();
    }
    LOG_INFO(_T("系统设置：数据存储位置已改为 %s，重启后生效。"), picked.GetString());
    ::MessageBox(m_hWnd, _T("设置已保存，重启客户端后生效。"),
                 _T("更改数据存储位置"), MB_OK | MB_ICONINFORMATION);
}

// =======================================================================
// 账号安全页（修改密码复用面板改密对话框；其余按钮/二次验证为占位）
// =======================================================================

std::unique_ptr<balloonwjui::DuiVBox> CBalloonSystemSettingDlg::BuildAccountPage()
{
    std::unique_ptr<balloonwjui::DuiVBox> page(new balloonwjui::DuiVBox());
    page->SetGap(0);

    CUserConfig* cfg = (m_lpFMGClient != NULL) ? &m_lpFMGClient->m_UserConfig : NULL;
    bool twoFactor = (cfg != NULL) ? (cfg->IsEnableTwoFactor() != FALSE) : false;

    AddButtonRow(page.get(), _T("登录设备"), _T("管理已登录的设备"),
                 kIdManageDevices, _T("管理"), false, true);
    AddButtonRow(page.get(), _T("修改密码"), NULL, kIdChangePwd, _T("修改"), false, true);
    AddSwitchRow(page.get(), _T("二次验证"),
                 twoFactor ? _T("已开启") : _T("未开启"), kIdTwoFactor, twoFactor, true);
    AddButtonRow(page.get(), _T("登录日志"), NULL, kIdLoginLog, _T("查看"), false, true);
    AddButtonRow(page.get(), _T("注销账号"), NULL, kIdDeactivate, _T("注销"), /*danger*/true, false);

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
    note->SetText(_T("该功能正在开发中，敬请期待。"));
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
        sub->SetText(keepAwakeOn ? kAutoAwaySubtitleBlocked : kAutoAwaySubtitleNormal);
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
    case kIdLanguage:
        cfg->SetLanguageIndex(balloonsettings::ClampLanguageIndex(value));
        break;
    case kIdFontScale:
        cfg->SetUiFontScaleIndex(balloonsettings::ClampFontScaleIndex(value));
        break;
    case kIdAutoAway:
        cfg->EnableAutoAway(on ? TRUE : FALSE);
        break;
    case kIdAutoAwayDelay:
        cfg->SetAutoAwayDelayIndex(balloonsettings::ClampAutoAwayDelayIndex(value));
        break;
    //用户切换「禁止机器休眠」开关：写入配置，并立即把与之互斥的「自动离开」两行置为
    //禁用或恢复。真正获取与释放系统电源请求由 CMainDlg 的定时器按配置执行（它每次触发
    //都会重新读取配置），本处不调用 Win32 API；相关日志见 BalloonMain/KeepAwakeSession.cpp。
    case kIdKeepAwake:
        cfg->EnableKeepAwake(on ? TRUE : FALSE);
        ApplyKeepAwakeExclusion(on);
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
        cfg->EnablePressEnterToSend(
            balloonsettings::PressEnterFromSendKeyIndex(value) ? TRUE : FALSE);
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
        // 设计无"保存"按钮，改即生效：立刻持久化到该账号的 .cfg。
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
            ::MessageBox(m_hWnd, _T("请从主面板系统菜单进入修改密码。"),
                         _T("修改密码"), MB_OK | MB_ICONINFORMATION);
        }
        return true;

    case kIdMicTest:
        ::MessageBox(m_hWnd, _T("麦克风测试功能开发中。"),
                     _T("系统设置"), MB_OK | MB_ICONINFORMATION);
        return true;

    //「立即清理」：弹清理对话框，由它列出各类缓存的占用量并按用户勾选执行删除。
    case kIdClearCache:
        m_cacheCleanDlg.OpenDialog(m_hWnd);
        return true;

    case kIdChangeRecvPath:
        ChooseDataRoot();
        return true;

    case kIdManageDevices:
        ::MessageBox(m_hWnd, _T("登录设备管理功能开发中。"),
                     _T("系统设置"), MB_OK | MB_ICONINFORMATION);
        return true;

    case kIdLoginLog:
        ::MessageBox(m_hWnd, _T("登录日志功能开发中。"),
                     _T("系统设置"), MB_OK | MB_ICONINFORMATION);
        return true;

    case kIdDeactivate:
        // 危险且不可逆，且服务端未接入：仅提示，不执行任何注销动作。
        ::MessageBox(m_hWnd, _T("注销账号为不可逆操作，该功能尚在开发中，暂不可用。"),
                     _T("注销账号"), MB_OK | MB_ICONWARNING);
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

        // 设置项（开关 / 下拉）变更 → 写配置并即时持久化。
        if (ApplySettingChange(pNotify->ctrlId, (int)pNotify->extra))
        {
            return 0;
        }
    }

    bHandled = FALSE;
    return 0;
}
