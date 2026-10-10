/**
 *  BalloonAboutDlg.cpp —— "关于"对话框实现。
 *
 *  控件树用 balloonui 现成控件程序化组合（对照 design/关于.png）：
 *    · 品牌区：DuiAvatar（放大的 app 图标，矩形裁剪、透明浮在白底）+ 三行 DuiLabel
 *      （应用名粗体 / 版本行 / 标语）。
 *    · 五个功能行：每行 DuiHBox（左 DuiLabel 标题 + 右 DuiButton）；"检查更新"行右侧是
 *      描边按钮"检查"、左侧两行（标题 + "当前已是最新版本"）；其余四行右侧是 Text 变体
 *      链接式按钮（橙字）。行间 DuiSeparator。
 *    · 页脚：DuiLabel 版权串。
 *  应用名、标语与页脚版权取 flamingo.ini 的配置（g_strAppTitle / g_strAppSlogan /
 *  g_strAppCopyright），其余固定文案取自 AboutDlgModel.h；标题栏标题（含进程 id）由
 *  BuildAboutTitle 拼。
 *  "官方网站"那一行的网址不写死，每次打开时从 flamingo.ini 的 [ui] websiteurl 读
 *  （读不到才回退 AboutDlgModel.h 的默认值），行上显示的就是网址原文、点击打开的
 *  也是它。
 *
 *  app 图标用 IDR_MAINFRAME 加载后合成到底色位图（MakeIconBitmapOnColor）：标题栏用
 *  16px 合成到标题栏底色，品牌区用 64px 合成到对话框白底 —— 与白底无缝，视觉即"图标
 *  透明浮在白底上"。这沿用 BalloonChangePwdDlg 的标题栏图标合成手法、不改 balloonui。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#include "stdafx.h"
#include "BalloonAboutDlg.h"

#include <string>
#include <shellapi.h>      // ShellExecute（"官方网站"按钮打开浏览器）
#include <gdiplus.h>       // app 图标转 32bpp 预乘 alpha 位图（DuiAvatar 渲染用）
#pragma comment(lib, "gdiplus.lib")

#include "AboutDlgModel.h"                 // 文案常量 + BuildAboutTitle
#include "../BalloonUpdate/BalloonUpdateWindow.h"   // 「检查更新」拉起升级窗
#include "../../Common/Log/IULog.h"
#include "../../Common/Util/IniFile.h"                    // CIniFile（读 flamingo.ini 的官网地址）
#include "../../Session/UserSessionData.h"            // g_szHomePath（flamingo.ini 所在目录）
#include "../../resource.h"                   // IDR_MAINFRAME（app 图标）
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"                       // 字体
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"      // DuiVBox / DuiHBox
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Common/I18n/I18nLayout.h"   // WidthForText：「官方网站」译文较长时加宽窗口

// ---- 对话框固定尺寸（逻辑像素）-----------------------------------------
// 客户区宽。本对话框的布局常量是固定像素、不随 DPI 放大，而字体会按系统 DPI 放大，
// 故宽度按 125% 缩放（系统 DPI 120）下的实测文字宽度取值：该缩放下"官方网站"标题宽
// 72px、官网地址宽 285px，加上链接两侧余量 16px、行内间距 10px、左右内边距 56px，
// 整行需要 439px。原先取 430 放不下，官网链接获得焦点时文字会贴住虚线焦点框。
// 150% 缩放下整行需要 511px，仍放不下，届时官网地址由 DT_END_ELLIPSIS 在尾部收成省略号。
static const int kDlgWidth      = 440;
static const int kDlgHeight     = 534;   // 客户区高（按下方品牌区 + 5 行 + 页脚算出）
static const int kDlgMinWidth   = 400;
static const int kDlgMinHeight  = 500;
static const int kDlgTitleBarH  = 36;

// ---- 功能行按钮命令 id（对话框内唯一即可；须 >= 10 避开 DuiFrameWindow 标题栏
//      保留的 1/2/3，否则点击会触发窗口最小化/最大化/关闭）------------------
static const UINT kIdCheckUpdate = 10;   // "检查"按钮
static const UINT kIdAgreement   = 11;   // "用户协议" → 查看
static const UINT kIdPrivacy     = 12;   // "隐私政策" → 查看
static const UINT kIdLicense     = 13;   // "开源许可" → 查看
static const UINT kIdWebsite     = 14;   // "官方网站" → 打开浏览器

// ---- 布局尺寸（逻辑像素）-----------------------------------------------
static const int kRootPadding   = 28;    // 根容器四周内边距
static const int kBigIconSize   = 64;    // 品牌区 app 图标边长
static const int kBrandIconGap  = 14;    // 图标与应用名之间的竖向间距
static const int kAppNameH      = 30;    // 应用名行高
static const int kVerLineH      = 22;    // 版本行行高
static const int kSloganH       = 22;    // 标语行高
static const int kBrandBottomGap= 18;    // 标语与第一条分隔线之间的竖向间距
static const int kSepH          = 1;     // 分隔线高
static const int kRowCheckH     = 60;    // "检查更新"行高（两行文字，略高）
static const int kRowLinkH      = 52;    // 普通功能行高
static const int kFooterTopGap  = 14;    // 末条分隔线与页脚之间的竖向间距
static const int kFooterH       = 20;    // 页脚行高
static const int kCheckBtnW     = 78;    // "检查"按钮宽
static const int kCheckBtnH     = 32;    // "检查"按钮高（行内竖向居中）
static const int kLinkBtnH      = 30;    // 链接式按钮高（行内竖向居中）
// 链接式按钮的<u>最小</u>宽（逻辑像素），四个功能行共用。按钮宽度由 AddLinkRow 按
// 文字实际宽度撑开，本值只在文字极短时起作用，保证按钮不至于窄得难以点中。
// 取值必须小于"查看"两字的宽度加上 kLinkTextPad：按钮文字是居中绘制的，按钮一旦被
// 最小宽度撑得比"文字宽度 + 余量"还宽，文字右缘就会与官网那一行对不齐。
static const int kLinkMinW      = 32;

// 链接式按钮在量出的文字宽度之外再留的余量（逻辑像素），左右各占一半。它同时满足
// 两项要求：一是防止不同 DPI / 字体版本下文字正好卡在边界上被削掉一两个像素；二是
// 按钮获得焦点时，DuiButton 沿按钮矩形向内缩 3px 画虚线焦点框（DuiButton.cpp 的
// kFocusRectInsetPx），文字与虚线框之间要留出间隙。取 16 时每侧 8px，扣掉内缩的
// 3px 后，文字与虚线框之间还有 5px。
static const int kLinkTextPad   = 16;

// 功能行左侧标题至少要留的宽度（逻辑像素）。右侧链接按钮按文字实际宽度撑开，但不能
// 把标题挤没了。"官方网站"四个汉字在 11pt 下实测宽度：100% 缩放 60px、125% 缩放 72px、
// 150% 缩放 88px。取 72 保证 125% 及以下不被截断，本值不能再调小。
static const int kRowTitleMinW  = 72;

// flamingo.ini 里"官方网站"地址的段名 / 键名。文件路径为
// <g_szHomePath>config\flamingo.ini，与旧版关于框（AboutDlg.cpp）取同一份配置。
static LPCTSTR kIniSectionUi     = _T("ui");
static LPCTSTR kIniKeyWebsiteUrl = _T("websiteurl");

// 读 URL 用的缓冲区长度（字符数）。网址一般远短于此，留足余量防截断。
static const int kWebsiteUrlBufLen = 512;
static const int kRowGap        = 10;    // 功能行内 左标题 / 右按钮 之间的间距

// ---- 字号（磅值）--------------------------------------------------------
static const int kAppNamePt     = 16;    // 应用名（粗体）
static const int kRowTitlePt    = 11;    // 功能行左侧标题

// ---- 配色 --------------------------------------------------------------
static const COLORREF kDialogBg      = RGB(0xFF, 0xFF, 0xFF);   // 对话框白底
static const COLORREF kAppNameColor  = RGB(0x22, 0x22, 0x22);   // 应用名深色
static const COLORREF kGrayTextColor = RGB(0x8C, 0x8C, 0x8C);   // 版本/标语/副标题/页脚灰
static const COLORREF kRowTitleColor = RGB(0x33, 0x33, 0x33);   // 功能行标题深灰
static const COLORREF kLinkColor     = RGB(0xF2, 0x6E, 0x35);   // 链接橙（贴合设计稿）
static const COLORREF kSeparatorColor= RGB(0xEC, 0xEC, 0xEC);   // 分隔线浅灰

// ---- 标题栏 --------------------------------------------------------------
// 标题栏背景色（须与 DuiFrameWindow 标题条填充色一致）。标题图标用 SRCCOPY 绘制、
// 不做 alpha 混合，故把 app 图标合成到此底色上，透明区才与标题栏一致。
static const COLORREF kTitleBarBgColor = RGB(245, 246, 250);
// 标题栏图标边长（与 DuiFrameWindow 标题条绘制的 16x16 一致）。
static const int kTitleIconSize = 16;

// =======================================================================
// 文件作用域辅助
// =======================================================================

// 非简体中文界面下「官方网站」标题比 kRowTitleMinW 多出的宽度（像素）；简体中文界面或放得下时为 0。
// 多语言第 4 期：窗口按它加宽，标题完整显示，网址可用的宽度与简体中文界面相同
static int WebsiteTitleExtraWidth()
{
    const int titleW = i18n::WidthForText(TR(L"官方网站"), kRowTitleMinW, i18n::PointToPixelHeight(kRowTitlePt),
                                          false, 0);
    return titleW - kRowTitleMinW;
}

// 应用名一行的行高（像素）。kAppNameH 按微软雅黑设计；非简体中文界面换成行高更高的字体（如 Segoe UI）后，
// 30 像素放不下一行粗体字，字母的下伸部分会被切掉，此时取字体行高，窗口高度相应加上差值（多语言第 4 期）
static int AppNameRowHeight()
{
    return i18n::LineHeightForUiFont(kAppNameH, i18n::PointToPixelHeight(kAppNamePt), true);
}

// GDI+ 懒初始化（与 AvatarImageUtil.cpp / MainPanelAvatar.cpp 同款模式；本 TU
// 留自己的 token，进程级只启动一次、不主动 Shutdown）。
static ULONG_PTR& GdipToken()
{
    static ULONG_PTR s_token = 0;
    return s_token;
}

static void EnsureGdiplus()
{
    if (GdipToken() != 0)
    {
        return;
    }
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&GdipToken(), &input, NULL);
}

// 把 HICON 转成 sizePx×sizePx 的 32bpp 预乘 alpha、top-down DIBSection（透明背景
// 保留）。这是 DuiAvatar 能正确渲染的位图类型 —— 其绘制快路径按 32bpp DIBSection
// 当 PixelFormat32bppPARGB 处理；而 CreateCompatibleBitmap 造出的 DDB 走不通该快
// 路径（落到 FromHBITMAP 兜底，存在上下倒置 / 渲染异常），导致图标显示不出来。
// 故这里改用与 AvatarImageUtil 同款的 CreateDIBSection + GDI+ 绘制：先用
// Gdiplus::Bitmap::FromHICON 正确提取图标含 alpha 的像素（DrawIconEx 直接画到
// 32bpp 位图常丢 alpha），再高质量缩放进 DIBSection。
// 返回的 HBITMAP 所有权归调用方，用完 DeleteObject。
static HBITMAP MakeIconArgbBitmap(HICON hIcon, int sizePx)
{
    if (hIcon == NULL || sizePx <= 0)
    {
        return NULL;
    }

    EnsureGdiplus();

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = sizePx;
    bi.bmiHeader.biHeight      = -sizePx;    // 负高 = top-down，配 DuiAvatar 渲染方向
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void*   bits = NULL;
    HBITMAP hBmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hBmp == NULL || bits == NULL)
    {
        if (hBmp != NULL)
        {
            ::DeleteObject(hBmp);
        }
        return NULL;
    }
    ::ZeroMemory(bits, (size_t)sizePx * sizePx * 4);

    int stride = sizePx * 4;
    Gdiplus::Bitmap canvas(sizePx, sizePx, stride,
                           PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
    Gdiplus::Graphics g(&canvas);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Gdiplus::Bitmap* pIcon = Gdiplus::Bitmap::FromHICON(hIcon);
    if (pIcon != NULL)
    {
        g.DrawImage(pIcon, Gdiplus::Rect(0, 0, sizePx, sizePx),
                    0, 0, (INT)pIcon->GetWidth(), (INT)pIcon->GetHeight(),
                    Gdiplus::UnitPixel);
        delete pIcon;
    }

    return hBmp;
}

// 把 HICON 合成到 sizePx×sizePx 的位图上、底色填 bgColor，返回 HBITMAP。
// 两处用：标题栏小图标合成到标题栏底色；品牌区大图标合成到对话框白底。
// DrawIconEx(DI_NORMAL) 会把图标按其掩码 / alpha 合成到这层底色上，透明区即呈现
// 底色；故底色取目标背景色时，视觉上就是"图标透明浮在该背景上"。
// 返回的 HBITMAP 所有权归调用方，用完 DeleteObject。
static HBITMAP MakeIconBitmapOnColor(HICON hIcon, int sizePx, COLORREF bgColor)
{
    if (hIcon == NULL || sizePx <= 0)
    {
        return NULL;
    }

    HDC     screen = ::GetDC(NULL);
    HDC     mem    = ::CreateCompatibleDC(screen);
    HBITMAP hbmp   = ::CreateCompatibleBitmap(screen, sizePx, sizePx);
    ::ReleaseDC(NULL, screen);
    if (mem == NULL || hbmp == NULL)
    {
        if (mem != NULL)
        {
            ::DeleteDC(mem);
        }
        if (hbmp != NULL)
        {
            ::DeleteObject(hbmp);
        }
        return NULL;
    }

    HGDIOBJ old = ::SelectObject(mem, hbmp);

    RECT   rc  = { 0, 0, sizePx, sizePx };
    HBRUSH hbr = ::CreateSolidBrush(bgColor);
    ::FillRect(mem, &rc, hbr);
    ::DeleteObject(hbr);

    // 把图标按掩码 + alpha 合成到已填好的底色上。
    ::DrawIconEx(mem, 0, 0, hIcon, sizePx, sizePx, 0, NULL, DI_NORMAL);

    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    return hbmp;
}

/**
 *  用指定字体量一段文字占的像素宽度。
 *
 *    text： 待测量的文字，NULL / 空串返回 0
 *    font： 量宽用的字体，<u>必须是文字真正绘制时用的那一支</u>；传 NULL 则按屏幕 DC
 *           的默认字体量，结果通常偏小，不要依赖
 *
 *  @return 文字宽度（像素）。
 *
 *  量宽用的是屏幕 DC（GetDC(NULL)），与控件自己绘制时用的 DC 兼容，同一 DPI 下
 *  结果一致；用完即 ReleaseDC，不留句柄。
 */
static int MeasureTextWidth(LPCTSTR text, HFONT font)
{
    if (text == NULL || *text == _T('\0'))
    {
        return 0;
    }

    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return 0;
    }

    HFONT oldFont = NULL;
    if (font != NULL)
    {
        oldFont = (HFONT)::SelectObject(hdc, font);
    }

    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32(hdc, text, ::lstrlen(text), &sz);

    if (oldFont != NULL)
    {
        ::SelectObject(hdc, oldFont);
    }
    ::ReleaseDC(NULL, hdc);

    return sz.cx;
}

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonAboutDlg::CBalloonAboutDlg()
    : m_built(false)
    , m_titleIconBmp(NULL)
    , m_appIconBmp(NULL)
    , m_websiteUrl(balloonmain::kAboutWebsiteUrl)   // 先置默认，构建客户区时再从 ini 覆盖
{
}

CBalloonAboutDlg::~CBalloonAboutDlg()
{
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
    if (m_appIconBmp != NULL)
    {
        ::DeleteObject(m_appIconBmp);
        m_appIconBmp = NULL;
    }
}

// =======================================================================
// 打开 / 关闭
// =======================================================================

void CBalloonAboutDlg::OpenAbout(HWND /*owner*/)
{
    if (!IsWindow())
    {
        // owner 传 NULL：让子控件通知自路由回本对话框（与改密 / 建群对话框同处理）。
        if (Create(NULL, CWindow::rcDefault, TR(L"关于"),
                   WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR(_T("关于对话框 Create 失败。"));
            return;
        }
        SetTitleBarHeight(kDlgTitleBarH);
        SetButtons(false, false, true);   // 只留关闭按钮
        SetMinSize(kDlgMinWidth, kDlgMinHeight);

        // 标题栏小图标：app 图标 16x16 合成到标题栏底色。只建一次、跨"关闭再打开"复用；
        // DuiFrameWindow::SetIcon 仅借用，本类析构时释放。
        if (m_titleIconBmp == NULL)
        {
            HICON hIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR,
                                           kTitleIconSize, kTitleIconSize);
            if (hIcon != NULL)
            {
                m_titleIconBmp = MakeIconBitmapOnColor(hIcon, kTitleIconSize, kTitleBarBgColor);
                ::DestroyIcon(hIcon);
            }
        }
        if (m_titleIconBmp != NULL)
        {
            SetIcon(m_titleIconBmp);
        }
        //没有所有者、会出现在任务栏：任务栏与 Alt+Tab 也用程序图标（2026-10-07，UI 约定第 10 条）
        m_taskIcons.ApplyTaskIcons(m_hWnd);

        m_built = false;
        BuildUiOnce();
        //kDlgHeight 是标题栏以下的内容高度，整窗高度要加上标题栏（DuiFrameWindow::ResizeClient 设的是整窗）
        //「官方网站」译文较长时窗口相应加宽，应用名一行加高时窗口相应加高（简体中文界面都不变）
        ResizeClient(kDlgWidth + WebsiteTitleExtraWidth(),
                     kDlgHeight + (AppNameRowHeight() - kAppNameH) + kDlgTitleBarH);
        CenterWindow();
    }

    // 标题栏标题：每次打开都按当前进程 id 重置（用户要求标题带进程 id；多开 / 排障时
    // 区分实例）。BuildAboutTitle 返回 std::wstring，Unicode 工程下可直接喂 SetTitle。
    std::wstring title = balloonmain::BuildAboutTitle(
        g_strAppTitle.c_str(), static_cast<unsigned long>(::GetCurrentProcessId()));
    SetTitle(title.c_str());

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO(_T("关于对话框已打开。"));
}

LRESULT CBalloonAboutDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                     LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 关闭 = 销毁本窗口（对象由主面板 / 调试入口持有，可复用重开）。
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

// =======================================================================
// 配置读取
// =======================================================================

void CBalloonAboutDlg::LoadWebsiteUrlFromIni()
{
    CString iniPath(g_szHomePath);
    iniPath += _T("config\\flamingo.ini");

    TCHAR szUrl[kWebsiteUrlBufLen] = { 0 };
    CIniFile iniFile;
    iniFile.ReadString(kIniSectionUi, kIniKeyWebsiteUrl,
                       balloonmain::kAboutWebsiteUrl,
                       szUrl, kWebsiteUrlBufLen, iniPath);

    m_websiteUrl = szUrl;
    m_websiteUrl.Trim();

    // 键存在但值为空串时 ReadString 不会用默认值，这里再兜一次底 —— 否则这一行
    // 会显示成空白、点了也打不开任何东西。
    if (m_websiteUrl.IsEmpty())
    {
        m_websiteUrl = balloonmain::kAboutWebsiteUrl;
        LOG_WARNING(_T("关于对话框：flamingo.ini 的 [ui] websiteurl 为空，回退到默认官网地址。"));
    }

    LOG_INFO(_T("关于对话框：官网地址取自 flamingo.ini —— %s"), (LPCTSTR)m_websiteUrl);
}

// =======================================================================
// 骨架构建（一次）
// =======================================================================

void CBalloonAboutDlg::AddSeparator(balloonwjui::DuiVBox* root)
{
    std::unique_ptr<balloonwjui::DuiSeparator> sep(new balloonwjui::DuiSeparator());
    sep->SetOrientation(balloonwjui::DuiSeparator::Horizontal);
    sep->SetColor(kSeparatorColor);
    root->AddChild(std::move(sep), balloonwjui::DuiLayout::Hint().Fixed(kSepH));
}

void CBalloonAboutDlg::AddCenteredLabel(balloonwjui::DuiVBox* root, LPCTSTR text,
                                        int pt, bool bold, COLORREF color, int height)
{
    std::unique_ptr<balloonwjui::DuiLabel> lab(new balloonwjui::DuiLabel());
    lab->SetText(text);
    lab->SetTextColor(color);
    if (pt > 0)
    {
        lab->SetTextPointSize(pt, bold);
    }
    // DT_END_ELLIPSIS：应用名、版权文字取自配置文件，过长时以省略号收尾，不越出对话框
    lab->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    root->AddChild(std::move(lab), balloonwjui::DuiLayout::Hint().Fixed(height));
}

void CBalloonAboutDlg::AddLinkRow(balloonwjui::DuiVBox* root, LPCTSTR title,
                                  LPCTSTR linkText, UINT ctrlId, int linkWidth)
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kRowGap);

    // 左侧标题（占满剩余宽度，竖向居中）。按磅值设字号，绘制时按窗口 DPI 取字体。
    std::unique_ptr<balloonwjui::DuiLabel> lab(new balloonwjui::DuiLabel());
    lab->SetText(title);
    lab->SetTextColor(kRowTitleColor);
    lab->SetTextPointSize(kRowTitlePt, false);
    lab->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    row->AddChild(std::move(lab), balloonwjui::DuiLayout::Hint().Weight(1));

    // 右侧链接式按钮（Text 变体：橙字、无底无边、hover 浅底）。右对齐贴右边缘、
    // 行内竖向居中。
    std::unique_ptr<balloonwjui::DuiButton> link(new balloonwjui::DuiButton());
    link->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    link->SetVariant(balloonwjui::DuiButton::Variant::Text);
    link->SetCtrlId(ctrlId);
    link->SetText(linkText);
    link->SetTextColor(kLinkColor);
    // 文字水平居中，不能用 DT_RIGHT。DuiButton 获得焦点时，虚线焦点框画在按钮矩形
    // 向内缩 3px 的范围上，而文字是在<u>整个</u>按钮矩形里按这里给的对齐方式绘制的
    // （见 DuiButton::OnPaint）。此前用 DT_RIGHT，"查看"按钮又固定 56px 宽，文字
    // 贴着按钮右边绘制：虚线框左半边是空的，右边框线还压在文字上。改为居中后文字与
    // 虚线框中心重合；再配合下面"按钮宽度 = 文字宽度 + 余量"，各行文字的右缘仍然
    // 对齐，都距行的右缘 kLinkTextPad / 2。
    // DT_END_ELLIPSIS 是兜底：地址长到连下面算出的上限都放不下时，让它在<u>尾部</u>
    // 收成省略号，而不是被硬生生截断 —— 见下面对截断方向的说明。
    link->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 按钮宽度按文字实际宽度撑开，linkWidth 只作最小宽度。
    // 原先"官方网站"那行写死 260px，装不下完整地址，而当时文字按 DT_RIGHT 绘制，
    // 宽度不够时削掉的是<u>开头</u>，于是
    // https://github.com/balloonwj/flamingo 显示成 os://github.com/balloonwj/flamingo，
    // 看着像乱码。地址还是从 flamingo.ini 读的，写死宽度更不可能永远合适。
    //
    // 量宽的字体必须与真正绘制时用的是同一支：调用方没设字体时 DuiButton 会回落到
    // 按控件 DPI 取的默认字体（见 DuiButton.cpp 里 useFont 的取法），这里照同样的
    // 优先级取。
    HFONT linkFont = link->GetFont();
    if (linkFont == NULL)
    {
        linkFont = link->GetDefaultFont();
    }

    // 上限：行宽减去左侧标题至少要占的宽度，不能把标题挤没。撑到上限还放不下的，
    // 交给上面那个 DT_END_ELLIPSIS 收尾，至少协议头和域名看得见。
    const int kMaxLinkW = kDlgWidth - kRootPadding * 2 - kRowTitleMinW - kRowGap;

    // 钳位算术在 AboutDlgModel.h 里，纯函数、有单测（Suite ABOUT）。
    const int width = balloonmain::ComputeLinkButtonWidth(
        MeasureTextWidth(linkText, linkFont), linkWidth, kMaxLinkW, kLinkTextPad);

    row->AddChild(std::move(link),
                  balloonwjui::DuiLayout::Hint().Fixed(width, kLinkBtnH)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));

    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowLinkH));
}

void CBalloonAboutDlg::AddCheckUpdateRow(balloonwjui::DuiVBox* root)
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kRowGap);

    // 左侧两行（标题 + 副标题），竖向居中：上下各塞一个弹性占位把两行夹中间。
    std::unique_ptr<balloonwjui::DuiVBox> left(new balloonwjui::DuiVBox());
    left->SetGap(2);

    std::unique_ptr<balloonwjui::DuiControl> topPad(new balloonwjui::DuiControl());
    left->AddChild(std::move(topPad), balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiLabel> titleLab(new balloonwjui::DuiLabel());
    titleLab->SetText(TR(L"检查更新"));
    titleLab->SetTextColor(kRowTitleColor);
    titleLab->SetTextPointSize(kRowTitlePt, false);
    titleLab->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    left->AddChild(std::move(titleLab), balloonwjui::DuiLayout::Hint().Fixed(20));

    std::unique_ptr<balloonwjui::DuiLabel> subLab(new balloonwjui::DuiLabel());
    subLab->SetText(TR(L"当前已是最新版本"));
    subLab->SetTextColor(kGrayTextColor);
    subLab->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    left->AddChild(std::move(subLab), balloonwjui::DuiLayout::Hint().Fixed(18));

    std::unique_ptr<balloonwjui::DuiControl> botPad(new balloonwjui::DuiControl());
    left->AddChild(std::move(botPad), balloonwjui::DuiLayout::Hint().Weight(1));

    row->AddChild(std::move(left), balloonwjui::DuiLayout::Hint().Weight(1));

    // 右侧普通描边按钮"检查"，行内竖向居中。
    std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
    btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    btn->SetVariant(balloonwjui::DuiButton::Variant::Default);
    btn->SetCtrlId(kIdCheckUpdate);
    btn->SetText(TR(L"检查"));
    row->AddChild(std::move(btn),
                  balloonwjui::DuiLayout::Hint().Fixed(kCheckBtnW, kCheckBtnH)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));

    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowCheckH));
}

void CBalloonAboutDlg::BuildUiOnce()
{
    if (m_built)
    {
        return;
    }

    // 官网地址在建行之前读好 —— 下面"官方网站"那一行直接拿它当显示文本。
    // 对话框关闭即销毁窗口、重开会走到这里重建，故每次打开都会重读 ini。
    LoadWebsiteUrlFromIni();

    // 品牌区放大的 app 图标：加载 64x64 HICON → 转 32bpp 预乘 alpha 位图（保留透明
    // 背景），由 DuiAvatar 以矩形裁剪原样渲染、透明区透出对话框白底。
    if (m_appIconBmp == NULL)
    {
        HICON hBigIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR,
                                          kBigIconSize, kBigIconSize);
        if (hBigIcon != NULL)
        {
            m_appIconBmp = MakeIconArgbBitmap(hBigIcon, kBigIconSize);
            ::DestroyIcon(hBigIcon);
        }
        else
        {
            LOG_ERROR(_T("关于对话框：加载 app 图标 IDR_MAINFRAME(64x64) 失败。"));
        }
        if (m_appIconBmp == NULL)
        {
            LOG_ERROR(_T("关于对话框：app 图标位图生成失败，品牌区图标将缺省。"));
        }
    }

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetBgColor(kDialogBg);
    root->SetPadding(kRootPadding);
    root->SetGap(0);

    // ---- 品牌区：图标 + 应用名 + 版本行 + 标语 ----
    {
        std::unique_ptr<balloonwjui::DuiAvatar> icon(new balloonwjui::DuiAvatar());
        icon->SetShape(balloonwjui::DuiAvatar::ShapeRoundRect);
        // 圆角半径取一个小的非零值：DuiAvatar 的 ShapeRoundRect 在半径=0 时会用
        // 四段零尺寸圆弧拼出退化的裁剪路径，导致 SetClip 后整张图被裁掉（图标
        // 显示不出来）。取 12px 既避开该退化、又只裁掉图标透明的四角、不伤可见内容。
        icon->SetCornerRadius(12);
        if (m_appIconBmp != NULL)
        {
            icon->SetBitmap(m_appIconBmp);
        }
        root->AddChild(std::move(icon),
                       balloonwjui::DuiLayout::Hint().Fixed(kBigIconSize, kBigIconSize)
                           .AlignC(balloonwjui::DuiLayout::AlignCenter));
    }

    {
        std::unique_ptr<balloonwjui::DuiControl> gap(new balloonwjui::DuiControl());
        root->AddChild(std::move(gap), balloonwjui::DuiLayout::Hint().Fixed(kBrandIconGap));
    }

    AddCenteredLabel(root.get(), g_strAppTitle.c_str(), kAppNamePt, true, kAppNameColor, AppNameRowHeight());
    const std::wstring versionLine = balloonmain::AboutVersionLine();
    AddCenteredLabel(root.get(), versionLine.c_str(), 0, false, kGrayTextColor, kVerLineH);
    // 标语配置为空时这一行留空：对话框是固定尺寸，保留行高可避免下方各行上移、底部留出空白
    AddCenteredLabel(root.get(), g_strAppSlogan.c_str(), 0, false, kGrayTextColor, kSloganH);

    {
        std::unique_ptr<balloonwjui::DuiControl> gap(new balloonwjui::DuiControl());
        root->AddChild(std::move(gap), balloonwjui::DuiLayout::Hint().Fixed(kBrandBottomGap));
    }

    // ---- 五个功能行（行间分隔线）----
    AddSeparator(root.get());
    AddCheckUpdateRow(root.get());
    AddSeparator(root.get());
    AddLinkRow(root.get(), TR(L"用户协议"), TR(L"查看"), kIdAgreement, kLinkMinW);
    AddSeparator(root.get());
    AddLinkRow(root.get(), TR(L"隐私政策"), TR(L"查看"), kIdPrivacy, kLinkMinW);
    AddSeparator(root.get());
    AddLinkRow(root.get(), TR(L"开源许可"), TR(L"查看"), kIdLicense, kLinkMinW);
    AddSeparator(root.get());
    AddLinkRow(root.get(), TR(L"官方网站"), (LPCTSTR)m_websiteUrl, kIdWebsite, kLinkMinW);
    AddSeparator(root.get());

    // ---- 页脚版权 ----
    {
        std::unique_ptr<balloonwjui::DuiControl> gap(new balloonwjui::DuiControl());
        root->AddChild(std::move(gap), balloonwjui::DuiLayout::Hint().Fixed(kFooterTopGap));
    }
    AddCenteredLabel(root.get(), g_strAppCopyright.c_str(), 0, false, kGrayTextColor, kFooterH);

    SetClientContent(std::move(root));
    m_built = true;
}

// =======================================================================
// 事件处理
// =======================================================================

LRESULT CBalloonAboutDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                      LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    if (pNotify->code == balloonwjui::DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        case kIdWebsite:
            // "官方网站" → 用默认浏览器打开 flamingo.ini 里配置的网址（构建客户区时
            // 已由 LoadWebsiteUrlFromIni 读进 m_websiteUrl，与行上显示的文本是同一个串）。
            ::ShellExecute(m_hWnd, _T("open"), (LPCTSTR)m_websiteUrl,
                           NULL, NULL, SW_SHOWNORMAL);
            return 0;

        case kIdCheckUpdate:
            // 把本对话框登记为被挡窗口：升级窗一出现就把关于框禁用，升级窗关闭后再恢复，
            // 效果上就是"升级窗相对关于框模态"。不这么做的话，升级窗还开着时用户仍能
            // 在关于框上操作，包括再点一次「检查更新」，弹出第二轮检查。
            CBalloonUpdateWindow::GetInstance().SetGatedWindow(
                m_hWnd, CBalloonUpdateWindow::kGatedModalDlg);

            // 拉起升级窗并立即发起一次检查（只检查、不下载）。查的结果——有新版本、
            // 已是最新、还是检查失败——都由升级窗自己呈现，本对话框不再弹提示框。
            // 若查出来是强制升级，升级窗会自行开始下载并在完成后安装重启。
            CBalloonUpdateWindow::GetInstance().ShowChecking();
            return 0;

        case kIdAgreement:
        case kIdPrivacy:
        case kIdLicense:
            // 用户协议 / 隐私政策 / 开源许可页面暂未接入，占位提示。
            ::MessageBox(m_hWnd, TR(L"功能开发中，敬请期待。"),
                         balloonmain::BuildAboutCaption(g_strAppTitle.c_str()).c_str(),
                         MB_OK | MB_ICONINFORMATION);
            return 0;

        default:
            break;
        }
    }

    bHandled = FALSE;
    return 0;
}
