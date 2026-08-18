/**
 *  BalloonModifyAvatarDlg.cpp —— "修改头像"对话框实现。
 *
 *  控件树用 balloonui 现成控件程序化组合（对照 design/修改头像1/2/3.png）：
 *    · 顶部预览 = AvatarPickerTile（承载 MakeMonogramSquareAvatar 生成的圆角方形
 *      字形位图）+ DuiLabel 说明；
 *    · 三段切换 = 三个 DuiButton（选中 Primary 橙、其余 Default）；
 *    · 系统头像页 = AvatarPickerTile 磁贴网格；首字母页 = DuiEdit + 12 色点；
 *      本地上传页 = 占位（第二期接通）；
 *    · 底部 = DuiButton(取消 / 使用此头像)。
 *  "使用此头像"把当前字形 + 底色经 AvatarSpec::EncodeGlyphAvatar 编码后，
 *  走 CIUSocket::SendChangeAvatar 发 v2 cmd_change_avatar；应答经主面板转发到
 *  OnChangeAvatarResult。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#include "stdafx.h"
#include "BalloonModifyAvatarDlg.h"

#include <memory>

#include <commdlg.h>                      // GetOpenFileNameW（选本地图片）
#pragma comment(lib, "comdlg32.lib")

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")   // AlphaBlend（拖拽框内画所选真图）

#include "../../Common/Log/IULog.h"
#include "../../resource.h"                  // IDR_MAINFRAME（标题栏程序图标）
#include "AvatarSpec.h"                   // Encode/DecodeAvatar、AvatarKind
#include "AvatarPalette.h"                // AvatarColorByIndex / kAvatarPickerSize
#include "AvatarPickerTile.h"             // 可点击 + 可选中磁贴
#include "MainPanelAvatar.h"              // MakeMonogramSquareAvatar
#include "AvatarImageUtil.h"             // 本地上传：ProcessAvatarSourceFile / MakeRoundedAvatarFromFile
#include "../../net/ProfileProtocolClient.h" // net::UpdateProfileResponse
#include "../../net/IUSocket.h"              // CIUSocket::SendChangeAvatar
#include "../../net/ImageTransfer.h"
#include "../../net/CurrentUserAccount.h"   // net::CurrentUserAccount（本地缓存目录按登录账号分）         // net::ImageTransfer / net::AvatarUploadResult（本地上传走图片服务器）
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox / DuiHBox
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"

// ---- 对话框固定尺寸（逻辑像素）-----------------------------------------
static const int kDlgWidth      = 560;
static const int kDlgHeight     = 476;
static const int kDlgTitleBarH  = 36;

// ---- 控件 id（对话框内唯一即可；避开标题栏保留的 1/2/3）----------------
static const UINT kIdTabBase        = 20;    // 三个分段按钮：20/21/22
static const UINT kIdCancel         = 30;    // 取消
static const UINT kIdUse            = 31;    // 使用此头像
static const UINT kIdLetterEdit     = 32;    // 首字母输入框
static const UINT kIdPreview        = 33;    // 预览大头像（点击忽略）
static const UINT kIdUploadPick     = 34;    // 本地上传"点击选择本地图片"按钮
static const UINT kIdDefaultTile     = 35;   // 系统头像页的「默认」磁贴（恢复自动头像）
static const UINT kIdSystemTileBase = 100;   // 系统磁贴：100 + index
static const UINT kIdColorDotBase   = 200;   // 底色点：200 + index

// 本地上传：源图大小上限（2MB，对照设计稿）、处理后目标方形边长（像素）。
static const int64_t kMaxAvatarSrcBytes = 2LL * 1024 * 1024;
static const int     kAvatarUploadPx    = 256;

// ---- 行高 / 间距（逻辑像素）-------------------------------------------
static const int kRootPadding   = 22;
static const int kRootGap       = 14;
static const int kPreviewSize   = 64;    // 预览大头像边长
static const int kPreviewRowH   = 72;
static const int kDividerH      = 1;
static const int kTabRowH       = 34;
static const int kBottomBarH    = 34;
static const int kTabBtnW       = 92;    // 分段按钮宽
static const int kBtnCancelW    = 80;    // 取消按钮宽
static const int kBtnUseW       = 118;   // 使用此头像按钮宽

static const int kTileSize      = 50;    // 系统磁贴边长
static const int kTileGap       = 9;     // 磁贴间距
static const int kTilesPerRow   = 8;     // 每行磁贴数
static const int kDotSize       = 26;    // 底色点边长
static const int kDotGap        = 8;     // 底色点间距
static const int kUploadPreviewSize = 120;  // 本地上传页方形预览框边长

// ---- 标题栏 / 配色 ------------------------------------------------------
static const COLORREF kTitleBarBgColor = RGB(245, 246, 250);
static const int      kTitleIconSize   = 16;
static const COLORREF kHeadingColor    = RGB(0x22, 0x22, 0x22);   // "预览"标题深灰
static const COLORREF kDescColor       = RGB(0x86, 0x86, 0x86);   // 说明文字浅灰
static const COLORREF kFieldLabelColor = RGB(0x3A, 0x3A, 0x3A);   // 小标题深灰
static const COLORREF kDividerColor    = RGB(0xEC, 0xEC, 0xEC);   // 分隔线
static const COLORREF kEditBgColor     = RGB(0xF4, 0xF3, 0xF0);   // 输入框浅灰底
static const COLORREF kHintBgColor     = RGB(0xFD, 0xF3, 0xE7);   // 琥珀提示框底
static const COLORREF kHintTextColor   = RGB(0x8A, 0x6E, 0x52);   // 琥珀提示框文字
static const COLORREF kZoneBgColor     = RGB(0xFA, 0xFA, 0xFA);   // 上传占位框底
static const COLORREF kZoneTextColor   = RGB(0x6E, 0x6E, 0x6E);   // 上传占位主文字
static const COLORREF kZoneHintColor   = RGB(0xA9, 0xA9, 0xA9);   // 上传占位次文字

// 应答里"头像过大"的结果码（对应服务端 error_code_avatar_too_large）。
static const int kRespCodeAvatarTooLarge = 108;
// 应答里"不是群主或管理员"的结果码（对应 net/Protocol.h 的
// error_code_not_group_admin=303）。此处另抄一份而不 include 那个头文件：原先是因为
// 本编译单元同时包含 v1 的 net/Msg.h、两者有同名枚举冲突（v1 协议头已于 2026-08-14
// 删除，该限制不复存在），现在只是不想为一个常量拖进整个协议头。
static const int kRespCodeNotGroupAdmin = 303;

// 预览 / 磁贴圆角比例（半径 = 边长 * 该比例）。
static const int kRadiusNumer = 28;
static const int kRadiusDenom = 100;

// =======================================================================
// 系统头像预置集合（第一期）
// =======================================================================
// 每项 = 一个「字形 + 底色槽」。GDI+ DrawString 不渲染彩色 emoji，故第一期用
// 单 CJK 字这类能被 monogram 字体（Microsoft YaHei）清晰渲染的字形；设计稿里的
// 彩色动物 / 自然 emoji 需后续接入彩色字形渲染（DirectWrite / 预置图）能力，
// 届时只需替换本表。底色槽下标见 AvatarPalette（0~11）。
namespace {

struct GlyphPreset
{
    LPCTSTR glyph;       // 字形（1~2 字符）
    int     colorIndex;  // 底色槽下标 [0,kAvatarPickerSize)
};

static const GlyphPreset kSystemPresets[] =
{
    { _T("萌"), 0 }, { _T("酷"), 1 }, { _T("星"), 2 }, { _T("月"), 3 },
    { _T("花"), 4 }, { _T("云"), 5 }, { _T("山"), 6 }, { _T("海"), 7 },
    { _T("风"), 8 }, { _T("林"), 9 }, { _T("雪"), 10 }, { _T("阳"), 11 },
    { _T("心"), 0 }, { _T("梦"), 4 }, { _T("春"), 2 }, { _T("夏"), 7 },
};
static const int kSystemPresetCount = (int)(sizeof(kSystemPresets) / sizeof(kSystemPresets[0]));

// 把 HICON 合成到 sizePx×sizePx 位图、底色填 bgColor，返回 HBITMAP（同改密对话框）。
HBITMAP MakeTitleIconBitmap(HICON hIcon, int sizePx, COLORREF bgColor)
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
    ::DrawIconEx(mem, 0, 0, hIcon, sizePx, sizePx, 0, NULL, DI_NORMAL);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    return hbmp;
}

// 往 path 追加一个圆角矩形子路径（4 段圆弧 + 闭合）。
static void DropZoneRoundRectPath(Gdiplus::GraphicsPath& path,
                                  Gdiplus::REAL x, Gdiplus::REAL y,
                                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL r)
{
    Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d,   0.0f, 90.0f);
    path.AddArc(x,         y + h - d, d, d,  90.0f, 90.0f);
    path.CloseFigure();
}

} // anonymous namespace

namespace balloonmain {

// 本地上传"拖拽框"控件（对照 design/修改头像3.png 与 修改头像3-鼠标hover.png）：
// 圆角虚线边框；未选图时框内画上传箭头 + "点击选择本地图片" + 格式提示；选图后框内
// 居中显示所选真图。点击 → 向父发 DUIN_CLICK 触发选图。鼠标悬停时边框 / 底色转强调色。
class CUploadDropZone : public balloonwjui::DuiControl
{
public:
    CUploadDropZone()
        : m_image(NULL)
        , m_hover(false)
        , m_pressed(false)
    {
    }
    ~CUploadDropZone()
    {
        if (m_image != NULL)
        {
            ::DeleteObject(m_image);
        }
    }

    // 设置 / 清除框内显示的真图（本控件接管位图所有权，析构 / 重设时释放）。
    void SetImage(HBITMAP hbm)
    {
        if (m_image == hbm)
        {
            return;
        }
        if (m_image != NULL)
        {
            ::DeleteObject(m_image);
        }
        m_image = hbm;
        Invalidate();
    }

    // 只有"本控件上先按下、再抬起"才算点击。这样可挡掉一类杂散抬起：在文件对话框里
    // 双击选图后，第二次点击的 WM_LBUTTONUP 在对话框关闭后才落到下层的拖拽框上——
    // 它没有配对的按下，m_pressed 为假，于是不会被当作点击再次弹出文件对话框
    // （用户反馈：选完图后文件对话框又弹一次）。
    bool OnLButtonDown(POINT /*pt*/, UINT /*mkFlags*/) override
    {
        m_pressed = true;
        return true;
    }
    bool OnLButtonUp(POINT /*pt*/, UINT /*mkFlags*/) override
    {
        bool wasPressed = m_pressed;
        m_pressed = false;
        if (wasPressed)
        {
            NotifyParent(balloonwjui::DUIN_CLICK);
        }
        return true;
    }

    // 悬停态变化需重绘（DuiControl 默认 hover 不自重绘）。自管 m_hover，不依赖基类。
    bool OnMouseEnter() override
    {
        if (!m_hover)
        {
            m_hover = true;
            Invalidate();
        }
        return true;
    }
    bool OnMouseLeave() override
    {
        m_pressed = false;   // 离开即取消按下态，避免按下后移出再抬起仍算点击
        if (m_hover)
        {
            m_hover = false;
            Invalidate();
        }
        return true;
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        RECT rc = GetRect();
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0)
        {
            return;
        }

        const COLORREF kBorderNormal = RGB(0xC8, 0xC8, 0xC8);
        const COLORREF kBorderHover  = RGB(0xFF, 0x7A, 0x45);   // 品牌橙
        const COLORREF kTextNormal   = RGB(0x8A, 0x8A, 0x8A);
        const COLORREF kTextHover    = RGB(0xE8, 0x6A, 0x33);
        const COLORREF kHintColor    = RGB(0xB2, 0xB2, 0xB2);
        const COLORREF kHoverFill    = RGB(0xFF, 0xF3, 0xEA);   // 悬停浅橙底
        const COLORREF border = m_hover ? kBorderHover : kBorderNormal;
        const COLORREF maintc = m_hover ? kTextHover   : kTextNormal;

        // ---- GDI+ 段：虚线圆角边框 +（无图时）箭头 / 文案 ----
        {
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

            Gdiplus::GraphicsPath path;
            DropZoneRoundRectPath(path, (Gdiplus::REAL)rc.left + 1.0f, (Gdiplus::REAL)rc.top + 1.0f,
                                  (Gdiplus::REAL)w - 2.0f, (Gdiplus::REAL)h - 2.0f, 10.0f);

            if (m_hover)
            {
                Gdiplus::SolidBrush bg(Gdiplus::Color(255, GetRValue(kHoverFill),
                                                      GetGValue(kHoverFill), GetBValue(kHoverFill)));
                g.FillPath(&bg, &path);
            }

            Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(border), GetGValue(border), GetBValue(border)), 1.5f);
            Gdiplus::REAL dash[] = { 5.0f, 4.0f };
            pen.SetDashPattern(dash, 2);
            g.DrawPath(&pen, &path);

            if (m_image == NULL)
            {
                const Gdiplus::REAL cx = (Gdiplus::REAL)rc.left + (Gdiplus::REAL)w / 2.0f;
                const Gdiplus::REAL cy = (Gdiplus::REAL)rc.top + (Gdiplus::REAL)h / 2.0f;

                // 上传箭头（竖线 + 顶部 V 形），位于文案上方。
                Gdiplus::Pen apen(Gdiplus::Color(255, GetRValue(maintc), GetGValue(maintc), GetBValue(maintc)), 2.0f);
                apen.SetStartCap(Gdiplus::LineCapRound);
                apen.SetEndCap(Gdiplus::LineCapRound);
                const Gdiplus::REAL ay = cy - 26.0f;   // 箭头中心
                g.DrawLine(&apen, cx, ay - 9.0f, cx, ay + 9.0f);
                g.DrawLine(&apen, cx - 7.0f, ay - 2.0f, cx, ay - 9.0f);
                g.DrawLine(&apen, cx + 7.0f, ay - 2.0f, cx, ay - 9.0f);

                Gdiplus::FontFamily ff(L"Microsoft YaHei");
                Gdiplus::Font fMain(&ff, 13.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::Font fHint(&ff, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::StringFormat sf;
                sf.SetAlignment(Gdiplus::StringAlignmentCenter);
                sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                Gdiplus::SolidBrush mainBrush(Gdiplus::Color(255, GetRValue(maintc), GetGValue(maintc), GetBValue(maintc)));
                Gdiplus::RectF rMain((Gdiplus::REAL)rc.left, cy - 2.0f, (Gdiplus::REAL)w, 22.0f);
                g.DrawString(L"点击选择本地图片", -1, &fMain, rMain, &sf, &mainBrush);

                Gdiplus::SolidBrush hintBrush(Gdiplus::Color(255, GetRValue(kHintColor), GetGValue(kHintColor), GetBValue(kHintColor)));
                Gdiplus::RectF rHint((Gdiplus::REAL)rc.left, cy + 22.0f, (Gdiplus::REAL)w, 18.0f);
                g.DrawString(L"支持 JPG / PNG / GIF · 最大 2MB", -1, &fHint, rHint, &sf, &hintBrush);
            }
        }

        // ---- 选了图：在框内居中 AlphaBlend 所选真图（圆角方形预乘 alpha 位图）----
        if (m_image != NULL)
        {
            BITMAP bm = {};
            ::GetObject(m_image, sizeof(bm), &bm);
            if (bm.bmWidth > 0 && bm.bmHeight > 0)
            {
                int x = rc.left + (w - bm.bmWidth) / 2;
                int y = rc.top + (h - bm.bmHeight) / 2;
                HDC     mem = ::CreateCompatibleDC(hdc);
                HGDIOBJ old = ::SelectObject(mem, m_image);
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                ::AlphaBlend(hdc, x, y, bm.bmWidth, bm.bmHeight, mem, 0, 0, bm.bmWidth, bm.bmHeight, bf);
                ::SelectObject(mem, old);
                ::DeleteDC(mem);
            }
        }
    }

private:
    HBITMAP m_image;   // 框内显示的真图（NULL=未选，画占位）；本控件持有
    bool    m_hover;   // 鼠标悬停态
    bool    m_pressed; // 是否在本控件上按下了左键（配对抬起才算点击）
};

} // namespace balloonmain

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonModifyAvatarDlg::CBalloonModifyAvatarDlg(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_built(false)
    , m_submitting(false)
    , m_page(0)
    , m_colorIndex(0)
    , m_targetKind(target_user)
    , m_targetId(0)
    , m_selfUserId(0)
    , m_selectedPreset(-1)
    , m_defaultSelected(false)
    , m_appliedFacetype(0)
    , m_preview(NULL)
    , m_pageHost(NULL)
    , m_letterEdit(NULL)
    , m_uploadZone(NULL)
    , m_hasPendingUpload(false)
    , m_defaultTile(NULL)
    , m_titleIconBmp(NULL)
{
    m_tabBtns[0] = NULL;
    m_tabBtns[1] = NULL;
    m_tabBtns[2] = NULL;
    m_pages[0]   = NULL;
    m_pages[1]   = NULL;
    m_pages[2]   = NULL;
}

CBalloonModifyAvatarDlg::~CBalloonModifyAvatarDlg()
{
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
}

// =======================================================================
// 打开 / 关闭
// =======================================================================

void CBalloonModifyAvatarDlg::OpenForCurrentUser(HWND /*owner*/, int facetype,
                                                 const std::string& customfaceUtf8,
                                                 LPCTSTR nickname, __int64 userid)
{
    // 目标是登录者自己：取色用的目标 id 与上传分桶用的账号 id 本来就是同一个。
    OpenForTarget(target_user, userid, nickname, facetype, customfaceUtf8, userid);
}

void CBalloonModifyAvatarDlg::OpenForGroup(HWND /*owner*/, __int64 groupId, LPCTSTR groupName,
                                           int facetype, const std::string& customfaceUtf8,
                                           __int64 selfUserId)
{
    // 目标是群：取色 / 取首字用群的 id 与群名，但上传仍按登录者分桶。
    OpenForTarget(target_group, groupId, groupName, facetype, customfaceUtf8, selfUserId);
}

void CBalloonModifyAvatarDlg::OpenForTarget(int targetKind, __int64 targetId, LPCTSTR targetName,
                                            int facetype, const std::string& customfaceUtf8,
                                            __int64 selfUserId)
{
    m_targetKind = targetKind;
    m_targetId   = targetId;
    m_targetName = (targetName != NULL) ? targetName : _T("");
    m_selfUserId = selfUserId;

    // 同一个对话框实例两种目标轮流用，标题每次都要按本次目标写一遍。
    LPCTSTR szTitle = (targetKind == target_group) ? _T("修改群头像") : _T("修改头像");

    if (!IsWindow())
    {
        // owner 传 NULL：让子控件通知自路由回本对话框（与改密 / 新建群对话框同处理）。
        if (Create(NULL, CWindow::rcDefault, szTitle,
                   WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR(_T("修改头像对话框 Create 失败。"));
            return;
        }
        SetTitleBarHeight(kDlgTitleBarH);
        SetButtons(false, false, true);   // 只留关闭按钮
        SetMinSize(kDlgWidth, kDlgHeight);

        if (m_titleIconBmp == NULL)
        {
            HICON hIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR,
                                           kTitleIconSize, kTitleIconSize);
            if (hIcon != NULL)
            {
                m_titleIconBmp = MakeTitleIconBitmap(hIcon, kTitleIconSize, kTitleBarBgColor);
                ::DestroyIcon(hIcon);
            }
        }
        if (m_titleIconBmp != NULL)
        {
            SetIcon(m_titleIconBmp);
        }

        m_built = false;
        BuildUiOnce();
        ResizeClient(kDlgWidth, kDlgHeight);
        CenterWindow();
    }

    // 窗口已开着时也要改：上一次开的可能是另一种目标（改完自己的头像，接着去
    // 群管理页改群头像），标题不跟着换就会指错东西。
    SetTitle(szTitle);

    // 「默认」磁贴画的是"这个目标不设头像时长什么样"，故目标一换就得重画。
    RefreshDefaultTile();

    // ---- 按目标已存头像解码出初值，决定初始页与预选 ----
    balloonmain::AvatarSpec spec = balloonmain::DecodeAvatar(
        facetype, customfaceUtf8, m_targetName, m_targetId);

    if (spec.m_kind == balloonmain::avatar_kind_glyph)
    {
        m_glyph      = spec.m_glyph;
        m_colorIndex = (spec.m_colorIndex >= 0) ? spec.m_colorIndex : 0;
    }
    else
    {
        // 自动 / 自定义图：字形回退目标名首字、底色默认 0。
        m_glyph      = spec.m_glyph;
        m_colorIndex = 0;
    }

    // 目标当前就是自动头像时，预选「默认」磁贴 —— 打开对话框看到的选中项要与
    // 当前实际生效的头像对得上。
    m_defaultSelected = (spec.m_kind == balloonmain::avatar_kind_auto);

    // 与系统预置比对：命中则进系统页 + 选中该磁贴，否则进首字母页。
    int matched = -1;
    for (int i = 0; i < kSystemPresetCount; ++i)
    {
        if (m_glyph == kSystemPresets[i].glyph && m_colorIndex == kSystemPresets[i].colorIndex)
        {
            matched = i;
            break;
        }
    }

    m_selectedPreset = matched;
    for (int i = 0; i < (int)m_systemTiles.size(); ++i)
    {
        m_systemTiles[i]->SetSelected(i == matched);
    }
    if (m_defaultTile != NULL)
    {
        m_defaultTile->SetSelected(m_defaultSelected);
    }
    SyncColorDotsToCurrent();
    if (m_letterEdit != NULL)
    {
        m_letterEdit->SetText(m_glyph);
    }

    // 预选落在系统页的两种情形：命中某枚预置磁贴，或者当前就是自动头像（选中
    // 「默认」那枚）。其余（自定义字形）进首字母页继续微调。
    int initialPage = (matched >= 0 || m_defaultSelected)
                      ? 0
                      : (spec.m_kind == balloonmain::avatar_kind_glyph ? 1 : 0);
    m_page = -1;            // 强制 ShowPage 执行一次切换
    ShowPage(initialPage);

    RefreshPreview();

    m_submitting = false;
    // 重开时清掉上一轮可能残留的待上传图片。
    m_hasPendingUpload = false;
    m_pendingAvatarFile.Empty();

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO(_T("修改头像对话框已打开（目标：%s，id=%lld）。"),
             (m_targetKind == target_group) ? _T("群") : _T("本人"),
             (long long)m_targetId);
}

LRESULT CBalloonModifyAvatarDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                            LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 关闭 = 取消：仅销毁本窗口（对象由主面板持有、可复用）。
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

// =======================================================================
// 骨架构建（一次）
// =======================================================================

void CBalloonModifyAvatarDlg::BuildUiOnce()
{
    if (m_built)
    {
        return;
    }

    // 关闭对话框（DestroyWindow）时框架会连同客户区控件树一起销毁，本类持有的都是
    // 借用裸指针。重开时 OpenForCurrentUser 把 m_built 复位后会重建整棵树；这两个
    // 磁贴 / 色点向量是 push_back 累加的，必须先清空，否则会残留上一次的悬垂指针，
    // 后续遍历它们即崩溃（再次点头像崩溃的根因）。其余裸指针在重建过程中会被重新赋值。
    m_systemTiles.clear();
    m_colorDots.clear();
    m_preview     = NULL;
    m_defaultTile = NULL;
    m_pageHost    = NULL;
    m_letterEdit  = NULL;
    m_uploadZone  = NULL;
    m_tabBtns[0]  = NULL;
    m_tabBtns[1]  = NULL;
    m_tabBtns[2]  = NULL;
    m_pages[0]    = NULL;
    m_pages[1]    = NULL;
    m_pages[2]    = NULL;

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kRootPadding);
    root->SetGap(kRootGap);

    // ---- 顶部：预览大头像 + 说明 ----
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(14);

        std::unique_ptr<balloonmain::AvatarPickerTile> preview(new balloonmain::AvatarPickerTile());
        preview->SetShape(balloonwjui::DuiAvatar::ShapeRoundRect);
        preview->SetCornerRadius(kPreviewSize * kRadiusNumer / kRadiusDenom);
        preview->SetCtrlId(kIdPreview);    // 点击忽略
        m_preview = preview.get();
        row->AddChild(std::move(preview), balloonwjui::DuiLayout::Hint().Fixed(kPreviewSize));

        std::unique_ptr<balloonwjui::DuiVBox> texts(new balloonwjui::DuiVBox());
        texts->SetGap(6);

        std::unique_ptr<balloonwjui::DuiLabel> title(new balloonwjui::DuiLabel());
        title->SetText(_T("预览"));
        title->SetTextColor(kHeadingColor);
        title->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        texts->AddChild(std::move(title), balloonwjui::DuiLayout::Hint().Fixed(20));

        std::unique_ptr<balloonwjui::DuiLabel> desc(new balloonwjui::DuiLabel());
        desc->SetText(_T("选择一种方式来设置你的头像。修改后联系人列表、消息气泡、个人资料处会同步更新。"));
        desc->SetTextColor(kDescColor);
        desc->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);
        texts->AddChild(std::move(desc), balloonwjui::DuiLayout::Hint().Weight(1));

        row->AddChild(std::move(texts), balloonwjui::DuiLayout::Hint().Weight(1));
        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kPreviewRowH));
    }

    // ---- 分隔线 ----
    {
        std::unique_ptr<balloonwjui::DuiVBox> divider(new balloonwjui::DuiVBox());
        divider->SetBgColor(kDividerColor);
        root->AddChild(std::move(divider), balloonwjui::DuiLayout::Hint().Fixed(kDividerH));
    }

    // ---- 三段切换按钮 ----
    {
        std::unique_ptr<balloonwjui::DuiHBox> tabs(new balloonwjui::DuiHBox());
        tabs->SetGap(8);

        LPCTSTR labels[3] = { _T("系统头像"), _T("首字母"), _T("本地上传") };
        for (int i = 0; i < 3; ++i)
        {
            std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
            btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
            btn->SetVariant(balloonwjui::DuiButton::Variant::Default);
            btn->SetCtrlId(kIdTabBase + i);
            btn->SetText(labels[i]);
            m_tabBtns[i] = btn.get();
            tabs->AddChild(std::move(btn), balloonwjui::DuiLayout::Hint().Fixed(kTabBtnW));
        }

        std::unique_ptr<balloonwjui::DuiControl> spacer(new balloonwjui::DuiControl());
        tabs->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));

        root->AddChild(std::move(tabs), balloonwjui::DuiLayout::Hint().Fixed(kTabRowH));
    }

    // ---- 页容器：三页内容叠在同一区域，按可见性切换 ----
    {
        std::unique_ptr<balloonwjui::DuiVBox> host(new balloonwjui::DuiVBox());
        m_pageHost = host.get();

        std::unique_ptr<balloonwjui::DuiVBox> sysPage(new balloonwjui::DuiVBox());
        BuildSystemPage(sysPage.get());
        m_pages[0] = sysPage.get();
        host->AddChild(std::move(sysPage), balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiVBox> letterPage(new balloonwjui::DuiVBox());
        BuildLetterPage(letterPage.get());
        m_pages[1] = letterPage.get();
        host->AddChild(std::move(letterPage), balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiVBox> uploadPage(new balloonwjui::DuiVBox());
        BuildUploadPage(uploadPage.get());
        m_pages[2] = uploadPage.get();
        host->AddChild(std::move(uploadPage), balloonwjui::DuiLayout::Hint().Weight(1));

        root->AddChild(std::move(host), balloonwjui::DuiLayout::Hint().Weight(1));
    }

    // ---- 底部按钮栏 ----
    {
        std::unique_ptr<balloonwjui::DuiHBox> bottom(new balloonwjui::DuiHBox());
        bottom->SetGap(10);

        std::unique_ptr<balloonwjui::DuiControl> spacer(new balloonwjui::DuiControl());
        bottom->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiButton> cancel(new balloonwjui::DuiButton());
        cancel->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        cancel->SetVariant(balloonwjui::DuiButton::Variant::Default);
        cancel->SetCtrlId(kIdCancel);
        cancel->SetText(_T("取消"));
        bottom->AddChild(std::move(cancel), balloonwjui::DuiLayout::Hint().Fixed(kBtnCancelW));

        std::unique_ptr<balloonwjui::DuiButton> use(new balloonwjui::DuiButton());
        use->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        use->SetVariant(balloonwjui::DuiButton::Variant::Primary);
        use->SetCtrlId(kIdUse);
        use->SetText(_T("使用此头像"));
        bottom->AddChild(std::move(use), balloonwjui::DuiLayout::Hint().Fixed(kBtnUseW));

        root->AddChild(std::move(bottom), balloonwjui::DuiLayout::Hint().Fixed(kBottomBarH));
    }

    SetClientContent(std::move(root));
    m_built = true;
}

void CBalloonModifyAvatarDlg::BuildSystemPage(balloonwjui::DuiVBox* host)
{
    host->SetGap(kTileGap);

    // 「默认」磁贴单独占第一行的头一格，其余预置磁贴照旧按行铺开。它画的是"这个
    // 目标不设头像时长什么样"（名字首字 + 按 id 取色），位图在 RefreshDefaultTile
    // 里按当前目标生成 —— 建树时目标可能还没确定，这里先只占位。
    std::unique_ptr<balloonmain::AvatarPickerTile> defaultTile(new balloonmain::AvatarPickerTile());
    defaultTile->SetShape(balloonwjui::DuiAvatar::ShapeRoundRect);
    defaultTile->SetCornerRadius(kTileSize * kRadiusNumer / kRadiusDenom);
    defaultTile->SetCtrlId(kIdDefaultTile);
    m_defaultTile = defaultTile.get();

    std::unique_ptr<balloonwjui::DuiHBox> defaultRow(new balloonwjui::DuiHBox());
    defaultRow->SetGap(kTileGap);
    defaultRow->AddChild(std::move(defaultTile), balloonwjui::DuiLayout::Hint().Fixed(kTileSize));

    std::unique_ptr<balloonwjui::DuiLabel> defaultTip(new balloonwjui::DuiLabel());
    defaultTip->SetText(_T("默认头像 —— 按名称首字自动生成"));
    defaultTip->SetTextColor(kDescColor);
    defaultTip->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    defaultRow->AddChild(std::move(defaultTip), balloonwjui::DuiLayout::Hint().Weight(1));

    host->AddChild(std::move(defaultRow), balloonwjui::DuiLayout::Hint().Fixed(kTileSize));

    int idx = 0;
    while (idx < kSystemPresetCount)
    {
        std::unique_ptr<balloonwjui::DuiHBox> rowBox(new balloonwjui::DuiHBox());
        rowBox->SetGap(kTileGap);

        for (int col = 0; col < kTilesPerRow && idx < kSystemPresetCount; ++col, ++idx)
        {
            std::unique_ptr<balloonmain::AvatarPickerTile> tile(new balloonmain::AvatarPickerTile());
            tile->SetShape(balloonwjui::DuiAvatar::ShapeRoundRect);
            tile->SetCornerRadius(kTileSize * kRadiusNumer / kRadiusDenom);
            tile->SetCtrlId(kIdSystemTileBase + idx);
            tile->SetOwnedBitmap(MakeGlyphBitmap(kSystemPresets[idx].glyph,
                                                 kSystemPresets[idx].colorIndex, kTileSize));
            m_systemTiles.push_back(tile.get());
            rowBox->AddChild(std::move(tile), balloonwjui::DuiLayout::Hint().Fixed(kTileSize));
        }

        // 行内右侧留白，磁贴左对齐。
        std::unique_ptr<balloonwjui::DuiControl> spacer(new balloonwjui::DuiControl());
        rowBox->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));

        host->AddChild(std::move(rowBox), balloonwjui::DuiLayout::Hint().Fixed(kTileSize));
    }

    // 底部留白，把磁贴顶到上方。
    std::unique_ptr<balloonwjui::DuiControl> bottomSpacer(new balloonwjui::DuiControl());
    host->AddChild(std::move(bottomSpacer), balloonwjui::DuiLayout::Hint().Weight(1));
}

void CBalloonModifyAvatarDlg::BuildLetterPage(balloonwjui::DuiVBox* host)
{
    host->SetGap(8);

    std::unique_ptr<balloonwjui::DuiLabel> lab1(new balloonwjui::DuiLabel());
    lab1->SetText(_T("字母 / 字符"));
    lab1->SetTextColor(kFieldLabelColor);
    lab1->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    host->AddChild(std::move(lab1), balloonwjui::DuiLayout::Hint().Fixed(16));

    {
        std::unique_ptr<balloonwjui::DuiHBox> editRow(new balloonwjui::DuiHBox());
        std::unique_ptr<balloonwjui::DuiEdit> edit(new balloonwjui::DuiEdit());
        edit->SetCtrlId(kIdLetterEdit);
        edit->SetMaxLength(2);
        edit->SetBgColor(kEditBgColor);
        m_letterEdit = edit.get();
        editRow->AddChild(std::move(edit), balloonwjui::DuiLayout::Hint().Fixed(96));

        std::unique_ptr<balloonwjui::DuiControl> spacer(new balloonwjui::DuiControl());
        editRow->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));
        host->AddChild(std::move(editRow), balloonwjui::DuiLayout::Hint().Fixed(40));
    }

    std::unique_ptr<balloonwjui::DuiLabel> lab2(new balloonwjui::DuiLabel());
    lab2->SetText(_T("背景色"));
    lab2->SetTextColor(kFieldLabelColor);
    lab2->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    host->AddChild(std::move(lab2), balloonwjui::DuiLayout::Hint().Fixed(16));

    {
        std::unique_ptr<balloonwjui::DuiHBox> dotRow(new balloonwjui::DuiHBox());
        dotRow->SetGap(kDotGap);
        for (int i = 0; i < balloonmain::kAvatarPickerSize; ++i)
        {
            std::unique_ptr<balloonmain::AvatarPickerTile> dot(new balloonmain::AvatarPickerTile());
            dot->SetShape(balloonwjui::DuiAvatar::ShapeCircle);
            dot->SetFallbackBgColor(balloonmain::AvatarColorByIndex(i));
            dot->SetCtrlId(kIdColorDotBase + i);
            m_colorDots.push_back(dot.get());
            dotRow->AddChild(std::move(dot), balloonwjui::DuiLayout::Hint().Fixed(kDotSize));
        }
        std::unique_ptr<balloonwjui::DuiControl> spacer(new balloonwjui::DuiControl());
        dotRow->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));
        host->AddChild(std::move(dotRow), balloonwjui::DuiLayout::Hint().Fixed(kDotSize));
    }

    {
        std::unique_ptr<balloonwjui::DuiVBox> hint(new balloonwjui::DuiVBox());
        hint->SetBgColor(kHintBgColor);
        hint->SetCornerRadius(8);
        hint->SetPadding(12);
        std::unique_ptr<balloonwjui::DuiLabel> tip(new balloonwjui::DuiLabel());
        tip->SetText(_T("字母头像会自动取你昵称的首字。也可以手动输入 1-2 个字符。"));
        tip->SetTextColor(kHintTextColor);
        tip->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);
        hint->AddChild(std::move(tip), balloonwjui::DuiLayout::Hint().Weight(1));
        host->AddChild(std::move(hint), balloonwjui::DuiLayout::Hint().Fixed(52));
    }

    std::unique_ptr<balloonwjui::DuiControl> bottomSpacer(new balloonwjui::DuiControl());
    host->AddChild(std::move(bottomSpacer), balloonwjui::DuiLayout::Hint().Weight(1));
}

void CBalloonModifyAvatarDlg::BuildUploadPage(balloonwjui::DuiVBox* host)
{
    host->SetGap(10);

    // 拖拽框（虚线圆角框，对照设计稿）：未选图时框内画上传箭头 + 文案，选图后框内显示
    // 所选真图。整框可点击 → 弹文件对话框选图。占满本页。
    {
        std::unique_ptr<balloonmain::CUploadDropZone> zone(new balloonmain::CUploadDropZone());
        zone->SetCtrlId(kIdUploadPick);
        m_uploadZone = zone.get();
        host->AddChild(std::move(zone), balloonwjui::DuiLayout::Hint().Weight(1));
    }

    {
        std::unique_ptr<balloonwjui::DuiVBox> hint(new balloonwjui::DuiVBox());
        hint->SetBgColor(kHintBgColor);
        hint->SetCornerRadius(8);
        hint->SetPadding(12);
        std::unique_ptr<balloonwjui::DuiLabel> tip(new balloonwjui::DuiLabel());
        tip->SetText(_T("提示：上传后会自动居中裁剪为 1:1 方形。"));
        tip->SetTextColor(kHintTextColor);
        tip->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);
        hint->AddChild(std::move(tip), balloonwjui::DuiLayout::Hint().Weight(1));
        host->AddChild(std::move(hint), balloonwjui::DuiLayout::Hint().Fixed(44));
    }
}

// =======================================================================
// 切页 / 选中 / 预览
// =======================================================================

void CBalloonModifyAvatarDlg::ShowPage(int kind)
{
    if (kind < 0)
    {
        kind = 0;
    }
    if (kind > 2)
    {
        kind = 2;
    }
    if (kind == m_page)
    {
        return;
    }
    m_page = kind;

    for (int i = 0; i < 3; ++i)
    {
        if (m_pages[i] != NULL)
        {
            m_pages[i]->SetVisible(i == kind);
        }
    }

    // 首字母输入框只在首字母页显示。
    if (m_letterEdit != NULL)
    {
        m_letterEdit->SetVisible(kind == 1);
    }

    ApplyTabVisual();

    // 隐藏页上次布局被跳过、矩形为空；切到它须强制重排页容器再重绘。
    if (m_pageHost != NULL)
    {
        m_pageHost->ForceLayout(m_pageHost->GetRect());
        m_pageHost->Invalidate();
    }
}

void CBalloonModifyAvatarDlg::ApplyTabVisual()
{
    for (int i = 0; i < 3; ++i)
    {
        if (m_tabBtns[i] != NULL)
        {
            m_tabBtns[i]->SetVariant(i == m_page
                                     ? balloonwjui::DuiButton::Variant::Primary
                                     : balloonwjui::DuiButton::Variant::Default);
        }
    }
}

HBITMAP CBalloonModifyAvatarDlg::MakeGlyphBitmap(LPCTSTR glyph, int colorIndex, int sizePx)
{
    int radius = sizePx * kRadiusNumer / kRadiusDenom;
    return balloonmain::MakeMonogramSquareAvatar(glyph,
                                                 balloonmain::AvatarColorByIndex(colorIndex),
                                                 sizePx, radius);
}

HBITMAP CBalloonModifyAvatarDlg::MakeAutoAvatarBitmap(int sizePx)
{
    // 与各渲染点的自动头像同口径（都经 DecodeAvatar）：名字首字 + 按目标 id
    // 稳定取色。这样磁贴 / 预览里看到的，就是点下去之后联系人列表、会话行、
    // 群管理页里会看到的那一张。
    balloonmain::AvatarSpec autoSpec = balloonmain::DecodeAvatar(
        balloonmain::avatar_kind_auto, std::string(), m_targetName, m_targetId);
    return balloonmain::MakeMonogramSquareAvatar(autoSpec.m_glyph, autoSpec.m_color, sizePx,
                                                 sizePx * kRadiusNumer / kRadiusDenom);
}

void CBalloonModifyAvatarDlg::RefreshPreview()
{
    if (m_preview == NULL)
    {
        return;
    }

    // 选中「默认」时预览画的就是自动头像本身，不是调色板里的某个字形。
    if (m_defaultSelected)
    {
        m_preview->SetOwnedBitmap(MakeAutoAvatarBitmap(kPreviewSize));
        return;
    }

    // 空字形时回退目标名首字，预览不至于只剩底色（与最终落库口径一致）。
    CString glyph = m_glyph.IsEmpty() ? balloonmain::AvatarFallbackGlyph(m_targetName) : m_glyph;
    m_preview->SetOwnedBitmap(MakeGlyphBitmap(glyph, m_colorIndex, kPreviewSize));
}

void CBalloonModifyAvatarDlg::RefreshDefaultTile()
{
    if (m_defaultTile == NULL)
    {
        return;
    }
    m_defaultTile->SetOwnedBitmap(MakeAutoAvatarBitmap(kTileSize));
}

void CBalloonModifyAvatarDlg::SelectDefaultTile()
{
    m_defaultSelected = true;
    m_selectedPreset  = -1;

    // 预置磁贴与「默认」互斥：选了默认就把预置那排的选中态全清掉。
    for (int i = 0; i < (int)m_systemTiles.size(); ++i)
    {
        m_systemTiles[i]->SetSelected(false);
    }
    if (m_defaultTile != NULL)
    {
        m_defaultTile->SetSelected(true);
    }

    RefreshPreview();
}

void CBalloonModifyAvatarDlg::SelectSystemPreset(int idx)
{
    if (idx < 0 || idx >= kSystemPresetCount)
    {
        return;
    }
    // 选了具体字形就不再是"恢复默认"了。
    m_defaultSelected = false;
    if (m_defaultTile != NULL)
    {
        m_defaultTile->SetSelected(false);
    }

    m_selectedPreset = idx;
    for (int i = 0; i < (int)m_systemTiles.size(); ++i)
    {
        m_systemTiles[i]->SetSelected(i == idx);
    }

    m_glyph      = kSystemPresets[idx].glyph;
    m_colorIndex = kSystemPresets[idx].colorIndex;

    // 让首字母页输入框 / 色点跟随，便于切到首字母页继续微调。
    if (m_letterEdit != NULL)
    {
        m_letterEdit->SetText(m_glyph);
    }
    SyncColorDotsToCurrent();
    RefreshPreview();
}

void CBalloonModifyAvatarDlg::SelectColorDot(int idx)
{
    if (idx < 0 || idx >= (int)m_colorDots.size())
    {
        return;
    }
    m_colorIndex = idx;
    SyncColorDotsToCurrent();

    // 改了底色 = 脱离系统预置选中（除非仍恰好等于某预置，简化处理直接清磁贴选中），
    // 也不再是"恢复默认"。
    m_selectedPreset  = -1;
    m_defaultSelected = false;
    for (int i = 0; i < (int)m_systemTiles.size(); ++i)
    {
        m_systemTiles[i]->SetSelected(false);
    }
    if (m_defaultTile != NULL)
    {
        m_defaultTile->SetSelected(false);
    }
    RefreshPreview();
}

void CBalloonModifyAvatarDlg::SyncColorDotsToCurrent()
{
    for (int i = 0; i < (int)m_colorDots.size(); ++i)
    {
        m_colorDots[i]->SetSelected(i == m_colorIndex);
    }
}

void CBalloonModifyAvatarDlg::OnLetterChanged()
{
    if (m_letterEdit == NULL)
    {
        return;
    }
    m_glyph = m_letterEdit->GetText();

    // 手动改字 = 脱离系统预置选中，也不再是"恢复默认"。
    m_selectedPreset  = -1;
    m_defaultSelected = false;
    for (int i = 0; i < (int)m_systemTiles.size(); ++i)
    {
        m_systemTiles[i]->SetSelected(false);
    }
    if (m_defaultTile != NULL)
    {
        m_defaultTile->SetSelected(false);
    }
    RefreshPreview();
}

// =======================================================================
// 事件处理
// =======================================================================

LRESULT CBalloonModifyAvatarDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // 首字母输入框每键变化 → 刷新预览。
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kIdLetterEdit)
    {
        OnLetterChanged();
        return 0;
    }

    if (pNotify->code == balloonwjui::DUIN_CLICK)
    {
        UINT id = pNotify->ctrlId;

        if (id == kIdCancel)
        {
            if (IsWindow())
            {
                DestroyWindow();
            }
            return 0;
        }
        if (id == kIdUse)
        {
            DoUse();
            return 0;
        }
        if (id == kIdUploadPick)
        {
            OnPickLocalImage();
            return 0;
        }
        if (id >= kIdTabBase && id < kIdTabBase + 3)
        {
            ShowPage((int)(id - kIdTabBase));
            return 0;
        }
        if (id == kIdDefaultTile)
        {
            SelectDefaultTile();
            return 0;
        }
        if (id >= kIdSystemTileBase && id < kIdSystemTileBase + (UINT)kSystemPresetCount)
        {
            SelectSystemPreset((int)(id - kIdSystemTileBase));
            return 0;
        }
        if (id >= kIdColorDotBase && id < kIdColorDotBase + (UINT)balloonmain::kAvatarPickerSize)
        {
            SelectColorDot((int)(id - kIdColorDotBase));
            return 0;
        }
        // 预览点击等其它 id：忽略。
    }

    bHandled = FALSE;
    return 0;
}

// =======================================================================
// 使用此头像 / 应答
// =======================================================================

void CBalloonModifyAvatarDlg::DoUse()
{
    if (m_submitting)
    {
        return;   // 已提交、等待应答，忽略重复点击
    }

    LPCTSTR szTitle = (m_targetKind == target_group) ? _T("修改群头像") : _T("修改头像");

    // 本地上传页：先把已处理好的图片上传到图片服务器，拿到 fileid 后（在
    // OnAvatarUploadResult 里）再发落库请求。上传这一步与目标是谁无关 ——
    // 图片服务器只管收下这张图给个 fileid。
    if (m_page == 2)
    {
        if (!m_hasPendingUpload || m_pendingAvatarFile.IsEmpty())
        {
            ::MessageBox(IsWindow() ? m_hWnd : NULL, _T("请先选择一张本地图片。"),
                         szTitle, MB_OK | MB_ICONINFORMATION);
            return;
        }
        // 这里判的是"有没有登录"，故看 m_selfUserId 而不是 m_targetId ——
        // 改群头像时后者是群 id，拿它判登录状态永远为真。
        if (m_selfUserId <= 0)
        {
            ::MessageBox(IsWindow() ? m_hWnd : NULL, _T("尚未登录，无法上传头像。"),
                         szTitle, MB_OK | MB_ICONWARNING);
            return;
        }
        m_submitting = true;
        net::ImageTransfer::GetInstance().EnqueueAvatarUpload(
            std::wstring(m_pendingAvatarFile.GetString()), net::CurrentUserAccount());
        LOG_INFO("发起头像图片上传（本地上传，已提交，等待上传结果）。");
        return;
    }

    int facetype = 0;
    std::string customface;

    if (m_defaultSelected)
    {
        // 恢复默认：facetype=0 + 空 customface。两项都要发出去 —— 服务端按
        // has_* 判断改不改，少发一项，库里旧的字形 / fileid 就清不掉。
        facetype = balloonmain::avatar_kind_auto;
        customface.clear();
    }
    else
    {
        // 字形头像（系统头像 / 首字母）：空字形回退目标名首字，保证落库口径与预览一致。
        CString glyph = m_glyph.IsEmpty() ? balloonmain::AvatarFallbackGlyph(m_targetName) : m_glyph;
        balloonmain::EncodeGlyphAvatar(glyph, m_colorIndex, facetype, customface);
    }

    // 记下本次实际发出的值，供主面板在成功应答后写回本端缓存并刷新头像。
    m_appliedFacetype   = facetype;
    m_appliedCustomface = customface;

    m_submitting = true;
    if (m_targetKind == target_group)
    {
        CIUSocket::GetInstance().SendChangeGroupAvatar(m_targetId, facetype, customface);
        LOG_INFO("发起修改群头像请求（groupid=%lld，facetype=%d，已提交，等待应答）。",
                 (long long)m_targetId, facetype);
    }
    else
    {
        CIUSocket::GetInstance().SendChangeAvatar(facetype, customface);
        LOG_INFO("发起修改头像请求（facetype=%d，已提交，等待应答）。", facetype);
    }
}

void CBalloonModifyAvatarDlg::OnPickLocalImage()
{
    // 弹系统文件对话框选图（Win32 GetOpenFileName，避免引 MFC/WTL 对话框类）。
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = IsWindow() ? m_hWnd : NULL;
    ofn.lpstrFilter = L"图片文件 (*.jpg;*.jpeg;*.png;*.gif;*.bmp)\0*.jpg;*.jpeg;*.png;*.gif;*.bmp\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"选择头像图片";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!::GetOpenFileNameW(&ofn))
    {
        return;   // 用户取消
    }

    // 校验大小（≤2MB）。
    HANDLE hf = ::CreateFileW(szFile, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE)
    {
        ::MessageBox(m_hWnd, _T("无法打开所选图片。"), _T("修改头像"), MB_OK | MB_ICONWARNING);
        return;
    }
    LARGE_INTEGER li = { 0 };
    BOOL gotSize = ::GetFileSizeEx(hf, &li);
    ::CloseHandle(hf);
    if (!gotSize || li.QuadPart <= 0 || li.QuadPart > kMaxAvatarSrcBytes)
    {
        ::MessageBox(m_hWnd, _T("图片大小须在 2MB 以内，请换一张。"),
                     _T("修改头像"), MB_OK | MB_ICONWARNING);
        return;
    }

    // 处理：居中裁方 + 缩放到 kAvatarUploadPx，存成 PNG 临时文件（供上传）。
    wchar_t szTempDir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, szTempDir);
    std::wstring outPath = szTempDir;
    outPath += L"flamingo_avatar_pending.png";

    if (!balloonmain::ProcessAvatarSourceFile(szFile, kAvatarUploadPx, outPath.c_str()))
    {
        ::MessageBox(m_hWnd, _T("该图片无法识别或处理失败，请换一张。"),
                     _T("修改头像"), MB_OK | MB_ICONWARNING);
        return;
    }

    m_pendingAvatarFile = outPath.c_str();
    m_hasPendingUpload  = true;

    // 顶部预览换成真图（圆角方形）。脱离字形选中态。
    if (m_preview != NULL)
    {
        HBITMAP hbm = balloonmain::MakeRoundedAvatarFromFile(
            outPath.c_str(), kPreviewSize, kPreviewSize * kRadiusNumer / kRadiusDenom);
        if (hbm != NULL)
        {
            m_preview->SetOwnedBitmap(hbm);
        }
    }
    // 上传框内也显示所选真图（用户要求：上传的头像显示在上传框中）。
    if (m_uploadZone != NULL)
    {
        HBITMAP hbmBox = balloonmain::MakeRoundedAvatarFromFile(
            outPath.c_str(), kUploadPreviewSize, kUploadPreviewSize * kRadiusNumer / kRadiusDenom);
        if (hbmBox != NULL)
        {
            m_uploadZone->SetImage(hbmBox);
        }
    }
    m_selectedPreset  = -1;
    m_defaultSelected = false;
    for (int i = 0; i < (int)m_systemTiles.size(); ++i)
    {
        m_systemTiles[i]->SetSelected(false);
    }
    if (m_defaultTile != NULL)
    {
        m_defaultTile->SetSelected(false);
    }
}

void CBalloonModifyAvatarDlg::OnChangeAvatarResult(const net::UpdateProfileResponse& resp)
{
    m_submitting = false;

    HWND hParent = IsWindow() ? m_hWnd : NULL;

    if (resp.code == 0)
    {
        // 成功：直接关闭。本端头像刷新由主面板在转发应答时一并处理。
        if (IsWindow())
        {
            DestroyWindow();
        }
        return;
    }

    LPCTSTR szTitle = (m_targetKind == target_group) ? _T("修改群头像") : _T("修改头像");

    if (resp.code == kRespCodeAvatarTooLarge)
    {
        ::MessageBox(hParent, _T("头像文件过大，请换一张更小的图片。"),
                     szTitle, MB_OK | MB_ICONWARNING);
        return;
    }

    // 群头像：服务端只放行群主与管理员。本端已按角色置灰过按钮，走到这里通常是
    // "刚被撤了管理员、界面还没刷新"，故给出明确原因而不是笼统的"请稍后重试"。
    if (m_targetKind == target_group && resp.code == kRespCodeNotGroupAdmin)
    {
        ::MessageBox(hParent, _T("只有群主和管理员可以修改群头像。"),
                     szTitle, MB_OK | MB_ICONWARNING);
        return;
    }

    ::MessageBox(hParent, _T("头像修改失败，请稍后重试。"),
                 szTitle, MB_OK | MB_ICONWARNING);
}

void CBalloonModifyAvatarDlg::OnAvatarUploadResult(const net::AvatarUploadResult& result)
{
    HWND hParent = IsWindow() ? m_hWnd : NULL;
    LPCTSTR szTitle = (m_targetKind == target_group) ? _T("修改群头像") : _T("修改头像");

    if (!result.m_ok)
    {
        m_submitting = false;
        ::MessageBox(hParent, _T("头像上传失败，请检查网络后重试。"),
                     szTitle, MB_OK | MB_ICONWARNING);
        return;
    }

    // 上传成功拿到 fileid：发落库请求（facetype=2, customface=fileid）。仍处于
    // 提交态，等应答（OnChangeAvatarResult）再关闭 / 刷新。
    m_appliedFacetype   = balloonmain::avatar_kind_image;
    m_appliedCustomface = result.m_fileid;

    if (m_targetKind == target_group)
    {
        CIUSocket::GetInstance().SendChangeGroupAvatar(
            m_targetId, balloonmain::avatar_kind_image, result.m_fileid);
        LOG_INFO("群头像图片已上传(fileid=%s)，发起 cmd_update_group_info 落库，groupid=%lld。",
                 result.m_fileid.c_str(), (long long)m_targetId);
    }
    else
    {
        CIUSocket::GetInstance().SendChangeAvatar(balloonmain::avatar_kind_image, result.m_fileid);
        LOG_INFO("头像图片已上传(fileid=%s)，发起 cmd_change_avatar 落库。", result.m_fileid.c_str());
    }
}
