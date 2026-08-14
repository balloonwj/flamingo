/**
 *  GroupMemberItem.cpp
 *
 *  群成员行的绘制实现。头像 / 悬停底 / 群主徽标用 GDI+ 抗锯齿，文字用
 *  GDI DrawText（清晰）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-22
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "GroupMemberItem.h"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"    // GetHost()->m_hWnd（弹菜单要宿主 HWND）
#include "GroupMentionMenu.h"                     // 成员操作菜单模型与投递负载（与气泡共用）
#include "../GroupRoleRules.h"                    // NetGroupRole：角色徽标与菜单门控用的角色取值
#include "../CustomMsgDef.h"                      // WM_CHAT_MENTION_ACTION
#include "../BalloonMain/PopupMenuPlacement.h"    // ClampPopupMenuToWorkArea（菜单不越屏）

namespace balloonchatwnd {

namespace {

// ---- 几何 / 配色 -------------------------------------------------------

const int kEdgePad      = 10;   // 行左右内边距
const int kAvatarSize   = 26;   // 头像边长
const int kAvatarGap    = 8;    // 头像与昵称间距
const int kNamePx       = 13;   // 昵称字号
const int kBadgePx      = 10;   // 群主徽标字号

const Gdiplus::REAL kAvatarRadius = 5.0f;   // 头像圆角
const Gdiplus::REAL kHoverRadius  = 5.0f;   // 悬停底圆角

const Gdiplus::Color kHoverFill (255, 0xF2, 0xF1, 0xEE);          // 悬停底
const Gdiplus::Color kBadgeFill (255, 0xFF, 0xF0, 0xE9);          // 群主徽标底
const COLORREF       kNameColor  = RGB(0x3C, 0x3C, 0x3C);         // 昵称
const COLORREF       kBadgeColor = RGB(0xFF, 0x7A, 0x45);         // 群主徽标文字
const COLORREF       kAvatarText = RGB(255, 255, 255);            // 头像字母

// 向 path 添加一个圆角矩形。
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f) { r = w / 2.0f; }
    if (r > h / 2.0f) { r = h / 2.0f; }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

// 用 GDI 绘制文字。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei");
    if (hFont == NULL)
    {
        return;
    }
    HFONT    hOldFont  = (HFONT)::SelectObject(hdc, hFont);
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, color);
    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r, dtFlags);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

// 测量一段文字的像素宽度。
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight, bool bold)
{
    if (text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei");
    if (hFont == NULL)
    {
        return 0;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz.cx;
}

} // anonymous namespace

GroupMemberItem::GroupMemberItem()
    : m_isSelf(false)
    , m_userid(0)
    , m_avatarColor(RGB(0xFF, 0x7A, 0x45))
    , m_netRole(grouprole::kNetRoleMember)
    , m_myNetRole(-1)
    , m_starredQuery(NULL)
    , m_starredQueryUd(NULL)
    , m_avatarBmp(NULL)
    , m_avatarLoadTried(false)
{
}

GroupMemberItem::~GroupMemberItem()
{
    ReleaseAvatarBitmap();
}

// 成员菜单的尺寸估值（逻辑像素）。Win32 的 HMENU 在弹出前拿不到精确尺寸，而落点
// 夹取必须在弹出<u>之前</u>算好，故按"每项一行"估。估大不会误伤：夹取只在放不下时
// 才改落点，估大顶多让菜单靠近屏幕边缘时更早翻向左 / 上。与 ChatBubble 里同名常量
// 取值一致 —— 两处弹的是同一个菜单。
static const int kMemberMenuEstWidth      = 140;   // 最长项"复制 TA 的昵称"约 110px + 边距
static const int kMemberMenuEstItemHeight = 26;

bool GroupMemberItem::OnRButtonDown(POINT pt, UINT /*mkFlags*/)
{
    if (m_userid == 0)
    {
        return false;   // 未绑定 userid（mock / 占位行），没有可操作的对象
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (hwnd == NULL)
    {
        return false;
    }

    // 与气泡里 @某人 片段共用同一套菜单模型：本人只留「查看资料」，其余四项，
    // 群主还会在末尾多出「设为 / 撤销管理员」。
    // 成员既然出现在侧栏里就一定还在群里，故 stillInGroup 恒传 true。
    // 「特别关注」那一项的文案要问主面板的关注名单，本控件不认识它，故经宿主群聊窗
    // 装下来的查询钩子代问；没装钩子时按"未关注"显示。
    const bool isStarred = (m_starredQuery != NULL)
                               ? m_starredQuery(m_starredQueryUd, m_userid) : false;

    const std::vector<balloonchatwnd::MentionMenuEntry> model =
        balloonchatwnd::BuildMentionMenuModel(m_userid, m_isSelf ? m_userid : 0, true,
                                              m_myNetRole, m_netRole, isStarred);
    if (model.empty())
    {
        return false;
    }

    HMENU menu = ::CreatePopupMenu();
    if (menu == NULL)
    {
        return false;
    }
    for (std::size_t i = 0; i < model.size(); ++i)
    {
        if (model[i].isSeparator)
        {
            ::AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            continue;
        }
        const UINT flags = MF_STRING | (model[i].enabled ? MF_ENABLED : MF_GRAYED);
        ::AppendMenuW(menu, flags, (UINT_PTR)model[i].id, model[i].label);
    }

    // 落点必须夹进锚点所在显示器的工作区 —— 成员侧栏贴着窗口右沿，窗口再靠近屏幕
    // 右边缘时，直接把光标坐标交给 TrackPopupMenu 菜单就跑到桌面外了。
    POINT scr = pt;
    ::ClientToScreen(hwnd, &scr);
    SIZE menuSize;
    menuSize.cx = kMemberMenuEstWidth;
    menuSize.cy = (LONG)model.size() * kMemberMenuEstItemHeight;
    const POINT at = balloonmain::ClampPopupMenuToWorkArea(scr, menuSize);

    const int cmd = ::TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        at.x, at.y, 0, hwnd, NULL);
    ::DestroyMenu(menu);

    if (cmd == 0)
    {
        return true;   // 用户取消了，但这一次右键已经由本行处理掉
    }

    // 四个动作都要碰业务，本控件不认识那些对象，打包投给宿主群聊窗执行 ——
    // 与气泡里 @ 菜单走的是同一条消息、同一个处理函数。
    balloonchatwnd::MentionActionData* pData = new balloonchatwnd::MentionActionData();
    pData->id          = (balloonchatwnd::MentionMenuId)cmd;
    pData->uid         = m_userid;
    pData->displayName = (LPCWSTR)m_name;
    if (!::PostMessage(hwnd, WM_CHAT_MENTION_ACTION, 0, (LPARAM)pData))
    {
        delete pData;
    }
    return true;
}

void GroupMemberItem::ReleaseAvatarBitmap()
{
    if (m_avatarBmp != NULL)
    {
        delete m_avatarBmp;
        m_avatarBmp = NULL;
    }
}

void GroupMemberItem::SetMember(const CString& name, const CString& avatarText,
                                COLORREF avatarColor, int netRole,
                                const CString& avatarImagePath)
{
    m_name        = name;
    m_avatarText  = avatarText;
    m_avatarColor = avatarColor;
    m_netRole     = netRole;
    // 头像真图路径变化时丢弃旧位图缓存，下次绘制按新路径重新解码（空路径=回退字形）。
    if (m_avatarImagePath != avatarImagePath)
    {
        m_avatarImagePath = avatarImagePath;
        ReleaseAvatarBitmap();
        m_avatarLoadTried = false;
    }
    Invalidate();
}

void GroupMemberItem::SetAvatarImage(const CString& path)
{
    if (m_avatarImagePath == path)
    {
        return;   // 路径没变，无需重解码 / 重绘
    }
    m_avatarImagePath = path;
    ReleaseAvatarBitmap();
    m_avatarLoadTried = false;
    Invalidate();
}

void GroupMemberItem::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
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

    // 悬停：浅灰圆角底（行内缩 3px 留呼吸位）。
    if (IsHover())
    {
        Gdiplus::GraphicsPath hp;
        AddRoundRect(hp, (Gdiplus::REAL)(x + 3), (Gdiplus::REAL)(y + 2),
                     (Gdiplus::REAL)(w - 6), (Gdiplus::REAL)(h - 4), kHoverRadius);
        Gdiplus::SolidBrush hb(kHoverFill);
        g.FillPath(&hb, &hp);
    }

    // 头像（圆角方块，纵向居中）。真图命中画圆角真图，否则填底色 + 后面画字形。
    const int avX = x + kEdgePad;
    const int avY = y + (h - kAvatarSize) / 2;
    bool drewAvatarImage = false;
    {
        Gdiplus::GraphicsPath av;
        AddRoundRect(av, (Gdiplus::REAL)avX, (Gdiplus::REAL)avY,
                     (Gdiplus::REAL)kAvatarSize, (Gdiplus::REAL)kAvatarSize,
                     kAvatarRadius);

        // 首次绘制按路径解码头像真图并缓存（失败也置位，避免每帧重复解码）。
        if (!m_avatarLoadTried && !m_avatarImagePath.IsEmpty())
        {
            m_avatarLoadTried = true;
            Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(
                (LPCWSTR)m_avatarImagePath, FALSE);
            if (bmp != NULL && bmp->GetLastStatus() == Gdiplus::Ok)
            {
                m_avatarBmp = bmp;
            }
            else
            {
                delete bmp;
            }
        }

        if (m_avatarBmp != NULL)
        {
            Gdiplus::Rect avRect(avX, avY, kAvatarSize, kAvatarSize);
            Gdiplus::InterpolationMode oldMode = g.GetInterpolationMode();
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.SetClip(&av);
            g.DrawImage(m_avatarBmp, avRect);
            g.ResetClip();
            g.SetInterpolationMode(oldMode);
            drewAvatarImage = true;
        }
        else
        {
            Gdiplus::SolidBrush avBrush(Gdiplus::Color(255,
                GetRValue(m_avatarColor), GetGValue(m_avatarColor),
                GetBValue(m_avatarColor)));
            g.FillPath(&avBrush, &av);
        }
    }

    // 角色徽标（右侧）—— 先算宽度，昵称避让它。普通成员没有徽标，昵称用满整行。
    int badgeW = 0;
    const wchar_t* badgeText = NULL;
    if (m_netRole == grouprole::kNetRoleOwner)
    {
        badgeText = L"群主";
    }
    else if (m_netRole == grouprole::kNetRoleAdmin)
    {
        badgeText = L"管理员";
    }
    if (badgeText != NULL)
    {
        const int badgeTextW = MeasureTextWidth(hdc, badgeText, kBadgePx, true);
        badgeW = badgeTextW + 10;
        const int badgeH = 16;
        const int badgeX = x + w - kEdgePad - badgeW;
        const int badgeY = y + (h - badgeH) / 2;
        Gdiplus::GraphicsPath bp;
        AddRoundRect(bp, (Gdiplus::REAL)badgeX, (Gdiplus::REAL)badgeY,
                     (Gdiplus::REAL)badgeW, (Gdiplus::REAL)badgeH, 3.0f);
        Gdiplus::SolidBrush bb(kBadgeFill);
        g.FillPath(&bb, &bp);
        RECT badgeRc = { badgeX, badgeY, badgeX + badgeW, badgeY + badgeH };
        DrawGdiText(hdc, badgeText, badgeRc, kBadgePx, true, kBadgeColor,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 头像字母（已画真图则跳过，避免字形盖在真图上）。
    if (!drewAvatarImage && !m_avatarText.IsEmpty())
    {
        RECT avRc = { avX, avY, avX + kAvatarSize, avY + kAvatarSize };
        DrawGdiText(hdc, (LPCWSTR)m_avatarText, avRc, 13, true, kAvatarText,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 昵称（头像右侧，避让群主徽标）。
    const int nameLeft  = avX + kAvatarSize + kAvatarGap;
    const int nameRight = x + w - kEdgePad - (badgeW > 0 ? badgeW + 6 : 0);
    RECT nameRc = { nameLeft, y, nameRight, y + h };
    DrawGdiText(hdc, (LPCWSTR)m_name, nameRc, kNamePx, false, kNameColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

} // namespace balloonchatwnd
