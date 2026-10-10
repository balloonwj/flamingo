/**
 *  GroupMemberItem.cpp
 *
 *  群成员行的绘制与交互实现。头像 / 悬停底 / 群主徽标用 GDI+ 抗锯齿，文字用
 *  GDI DrawText（清晰）。交互有两项：右键弹成员操作菜单，双击打开与该成员的
 *  单聊窗；两者都把动作打包投给宿主群聊窗执行。
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
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
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
#include "GroupMemberList.h"                       // RequestSelect：请所在的成员列表选中本行
#include "../../../third-party/balloonui/balloonui/DuiHost.h"    // GetHost()->m_hWnd（弹菜单要宿主 HWND）
#include "GroupMentionMenu.h"                     // 成员操作菜单模型与投递负载（与气泡共用）
#include "../../Session/GroupRoleRules.h"                    // NetGroupRole：角色徽标与菜单门控用的角色取值
#include "../../App/CustomMsgDef.h"                      // WM_CHAT_MENTION_ACTION
#include "../BalloonMain/PopupMenuPlacement.h"    // ClampPopupMenuToWorkArea（菜单不越屏）
#include "../../Common/Log/IULog.h"              // 双击开单聊的日志
#include "../../net/Protocol.h"                  // presence_type：在线状态的合法取值范围
#include "../BalloonMain/PresenceBadge.h"         // DrawPresenceBadge：头像角上的在线状态徽标（与主面板通讯录同一套图形）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../BalloonMain/AvatarGray.h"         // MakeGrayColor：离线头像置灰的颜色换算
#include "../BalloonMain/AvatarGlyph.h"        // DrawCenteredGlyphGdi：头像字居中
#include "../BalloonMain/OnlineCapability.h"   // ShouldGrayUserAvatar / kConnUiOnline：头像是否置灰
#include "ChatAvatarBitmapCache.h"             // 真图头像的共享位图缓存（抗锯齿圆角，含灰度副本）
#include "GroupDeptHeaderItem.h"               // GroupDeptIndentPx：部门树视图的缩进量
#include "GroupMustRead.h"                     // MustReadMemberMarkText：「未确认必读」标记的文字

namespace balloonchatwnd {

namespace {

// ---- 几何 / 配色 -------------------------------------------------------

const int kEdgePad      = 10;   // 行左右内边距
const int kAvatarSize   = kGroupMemberAvatarPx;      // 头像边长
const int kAvatarGap    = kGroupMemberAvatarGapPx;   // 头像与昵称间距
const int kNamePx       = 13;   // 昵称字号
const int kBadgePx      = 10;   // 群主徽标字号

const Gdiplus::REAL kAvatarRadius = 5.0f;   // 头像圆角

// 头像角上的在线状态徽标：绘制方框边长 10（DrawPresenceBadge 实测下限，再小时钟指针会糊），
// 中心距头像右、下边缘各 6（方框一半再加 1）
const int           kPresenceBadgePx    = 10;
const int           kPresenceBadgeInset = 6;
// 徽标外圈白描边：把徽标与头像底色隔开，否则深色头像上的徽标会和底色混在一起。白圈按徽标
// 圆底（viewBox 24 中直径 20）的半径外扩，而不是按绘制方框外扩，与主面板列表项的画法一致
const Gdiplus::REAL kPresenceRingPx     = 1.5f;
const Gdiplus::REAL kPresenceDiscRatio  = 20.0f / 24.0f;
const Gdiplus::Color kPresenceRingColor(255, 255, 255, 255);
const Gdiplus::REAL kHoverRadius  = 5.0f;   // 悬停底圆角

const Gdiplus::Color kHoverFill (255, 0xF2, 0xF1, 0xEE);          // 悬停底
// 选中样式（2026-10-08 用户从三个候选里选定，样图 tmp/icon_preview/member_select_preview.png）：浅橙底与悬停底同一位置、
// 同一圆角，左侧再画一条橙色竖条（橙色取群聊窗的主色，与边栏页签下划线、发送按钮相同）
const Gdiplus::Color kSelectFill(255, 0xFF, 0xF1, 0xE8);          // 选中底
const Gdiplus::Color kSelectBar (255, 0xFF, 0x7A, 0x45);          // 选中行左侧的竖条
const int           kSelectBarLeft   = 3;     // 竖条左边距行左缘的距离（与选中底的左缘对齐）
const int           kSelectBarWidth  = 3;     // 竖条宽度
const int           kSelectBarInsetY = 8;     // 竖条上下各比行短出的距离
const Gdiplus::REAL kSelectBarRadius = 1.5f;  // 竖条圆角（两端为半圆）
const Gdiplus::Color kBadgeFill (255, 0xFF, 0xF0, 0xE9);          // 群主徽标底
const COLORREF       kNameColor  = RGB(0x3C, 0x3C, 0x3C);         // 昵称
const COLORREF       kBadgeColor = RGB(0xFF, 0x7A, 0x45);         // 群主徽标文字
const COLORREF       kAvatarText = RGB(255, 255, 255);            // 头像字母
const COLORREF       kSecondaryColor = RGB(0x99, 0x99, 0x99);     // 兼属部门的「兼」字
const Gdiplus::Color kMustReadFill (255, 0xEE, 0xF0, 0xF3);       // 「未确认必读」标记的底色（浅灰蓝，与角色徽标区分）
const COLORREF       kMustReadColor = RGB(0x5B, 0x6B, 0x80);      // 「未确认必读」标记的文字
const int            kSecondaryPx  = 11;   // 「兼」字字号
const int            kSecondaryGap = 4;    // 名字与「兼」字之间的间距

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
        i18n::UiFontName());
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
        i18n::UiFontName());
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

RECT ComputeMemberPresenceBadgeBox(int avatarLeft, int avatarTop, int avatarSize)
{
    const int cx = avatarLeft + avatarSize - kPresenceBadgeInset;
    const int cy = avatarTop + avatarSize - kPresenceBadgeInset;
    const int half = kPresenceBadgePx / 2;
    RECT box = { cx - half, cy - half, cx - half + kPresenceBadgePx, cy - half + kPresenceBadgePx };
    return box;
}

GroupMemberItem::GroupMemberItem()
    : m_isSelf(false)
    , m_userid(0)
    , m_avatarColor(RGB(0xFF, 0x7A, 0x45))
    , m_netRole(grouprole::kNetRoleMember)
    , m_myNetRole(-1)
    , m_starredQuery(NULL)
    , m_starredQueryUd(NULL)
    , m_groupFollowQuery(NULL)
    , m_groupFollowQueryUd(NULL)
    , m_presence(kMemberPresenceUnknown)
    , m_selfConnUiState(balloonmain::kConnUiOnline)
    , m_treeDepth(0)
    , m_secondary(false)
    , m_mustReadMark(false)
    , m_mentionAllowed(true)
    , m_selected(false)
{
}

void GroupMemberItem::SetMustReadMark(bool on)
{
    if (m_mustReadMark == on)
    {
        return;
    }
    m_mustReadMark = on;
    Invalidate();
}

void GroupMemberItem::SetTreeLayout(int depth, bool secondary)
{
    m_treeDepth = (depth > 0) ? depth : 0;
    m_secondary = secondary;
    Invalidate();
}

void GroupMemberItem::SetPresence(int presence)
{
    //协议之外的取值一律按未知处理，不画徽标
    if (presence < ::presence_offline || presence > ::presence_busy)
    {
        presence = kMemberPresenceUnknown;
    }
    if (presence == m_presence)
    {
        return;
    }
    m_presence = presence;
    Invalidate();
}

void GroupMemberItem::SetSelfConnectionState(int connUiState)
{
    const bool wasGray = IsAvatarGray();
    m_selfConnUiState = connUiState;
    //只有本人那一行的灰彩会因此变化；别人那一行结果不变，不必重绘
    if (IsAvatarGray() != wasGray)
    {
        Invalidate();
    }
}

bool GroupMemberItem::IsAvatarGray() const
{
    return balloonmain::ShouldGrayUserAvatar(m_isSelf, m_presence, m_selfConnUiState);
}

GroupMemberItem::~GroupMemberItem()
{
    // 真图头像的位图归共享缓存所有，本类没有需要释放的 GDI 资源。
}

// 成员菜单的尺寸估值（逻辑像素）。Win32 的 HMENU 在弹出前拿不到精确尺寸，而落点
// 夹取必须在弹出<u>之前</u>算好，故按"每项一行"估。估大不会误伤：夹取只在放不下时
// 才改落点，估大顶多让菜单靠近屏幕边缘时更早翻向左 / 上。与 ChatBubble 里同名常量
// 取值一致 —— 两处弹的是同一个菜单。
static const int kMemberMenuEstWidth      = 140;   // 最长项"取消特别关注"六个汉字约 100px + 边距
static const int kMemberMenuEstItemHeight = 26;

void GroupMemberItem::SetSelected(bool selected)
{
    if (m_selected == selected)
    {
        return;
    }
    m_selected = selected;
    Invalidate();
}

void GroupMemberItem::RequestSelect()
{
    if (m_userid == 0)
    {
        return;
    }
    GroupMemberList* list = dynamic_cast<GroupMemberList*>(GetParent());
    if (list != NULL)
    {
        list->SelectMember(m_userid);
    }
}

bool GroupMemberItem::OnLButtonDown(POINT pt, UINT mkFlags)
{
    RequestSelect();
    return DuiControl::OnLButtonDown(pt, mkFlags);
}

bool GroupMemberItem::OnRButtonDown(POINT pt, UINT /*mkFlags*/)
{
    if (m_userid == 0)
    {
        return false;   // 未绑定 userid（mock / 占位行），没有可操作的对象
    }
    // 先选中本行再弹菜单，一眼看出菜单是对哪一行弹的（2026-10-08）；没有宿主窗口时（单元测试）照样选中
    RequestSelect();
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
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
    // 「关注 TA 在本群的发言」的文案同样经钩子问主面板的群关注名单
    const bool isGroupFollowed = (m_groupFollowQuery != NULL)
                                     ? m_groupFollowQuery(m_groupFollowQueryUd, m_userid) : false;

    std::vector<balloonchatwnd::MentionMenuEntry> model =
        balloonchatwnd::BuildMentionMenuModel(m_userid, m_isSelf ? m_userid : 0, true,
                                              m_myNetRole, m_netRole, isStarred, isGroupFollowed);
    // 没有输入框的宿主（帖子群窗口的群成员面板）：去掉「@TA」
    if (!m_mentionAllowed)
    {
        model = balloonchatwnd::RemoveMentionMenuEntry(model);
    }
    if (model.empty())
    {
        return false;
    }

    POINT scr = pt;
    ::ClientToScreen(hwnd, &scr);
    const int cmd = TrackMemberMenu(hwnd, model, scr);

    if (cmd == 0)
    {
        return true;   // 用户取消了，但这一次右键已经由本行处理掉
    }

    // 四个动作都要碰业务，本控件不认识那些对象，打包投给宿主群聊窗执行 ——
    // 与气泡里 @ 菜单走的是同一条消息、同一个处理函数。
    PostMemberAction(hwnd, cmd);
    return true;
}

int GroupMemberItem::TrackMemberMenu(HWND owner, const std::vector<MentionMenuEntry>& model, POINT screenPt)
{
    if (model.empty() || !::IsWindow(owner))
    {
        return 0;
    }
    HMENU menu = ::CreatePopupMenu();
    if (menu == NULL)
    {
        return 0;
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
    SIZE menuSize;
    menuSize.cx = kMemberMenuEstWidth;
    menuSize.cy = (LONG)model.size() * kMemberMenuEstItemHeight;
    const POINT at = balloonmain::ClampPopupMenuToWorkArea(screenPt, menuSize);

    const int cmd = ::TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        at.x, at.y, 0, owner, NULL);
    ::DestroyMenu(menu);
    return cmd;
}

bool GroupMemberItem::OnLButtonDblClk(POINT pt, UINT mkFlags)
{
    // 本人那一行不开单聊：主面板本来就拒绝和自己聊天，右键自己时菜单里也没有
    // 「发消息」。未绑定 userid 的 mock / 占位行没有可以聊天的对象。
    if (m_isSelf || m_userid == 0)
    {
        return DuiControl::OnLButtonDblClk(pt, mkFlags);
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return DuiControl::OnLButtonDblClk(pt, mkFlags);
    }

    // 与右键菜单选「发消息」投同一条消息：打开或激活单聊窗由群聊窗转请主面板
    // 完成，本控件只负责把目标成员与动作编号投给宿主。
    LOG_INFO("Group member row double-clicked, ask host to open chat, uid=%lld.",
             (long long)m_userid);
    return PostMemberAction(hwnd, balloonchatwnd::kMentionMenuChat);
}

bool GroupMemberItem::PostMemberAction(HWND hwnd, int action)
{
    // 负载由宿主群聊窗处理完 delete；投递失败（窗口恰好在此刻销毁）时由本函数释放。
    balloonchatwnd::MentionActionData* pData = new balloonchatwnd::MentionActionData();
    pData->id          = (balloonchatwnd::MentionMenuId)action;
    pData->uid         = m_userid;
    pData->displayName = (LPCWSTR)m_name;
    if (!::PostMessage(hwnd, WM_CHAT_MENTION_ACTION, 0, (LPARAM)pData))
    {
        delete pData;
        return false;
    }
    return true;
}

void GroupMemberItem::SetMember(const CString& name, const CString& avatarText,
                                COLORREF avatarColor, int netRole,
                                const CString& avatarImagePath)
{
    m_name        = name;
    m_avatarText  = avatarText;
    m_avatarColor = avatarColor;
    m_netRole     = netRole;
    // 头像真图路径（空路径=回退字形）。位图在共享缓存里按路径取用，换路径无需释放任何东西。
    m_avatarImagePath = avatarImagePath;
    Invalidate();
}

void GroupMemberItem::SetAvatarImage(const CString& path)
{
    m_avatarImagePath = path;
    // 本方法在头像下载完成后调用。路径没变也要处理：同一路径的文件可能刚被新下载的头像覆盖，
    // 共享缓存里那张还是旧图（或是当初文件还不存在时记下的失败），得作废，下次绘制才会重新解码。
    // 作废是幂等的，同一头像的多个气泡与成员行各调一次，只在随后绘制时解码一次。
    if (!path.IsEmpty())
    {
        ChatAvatarBitmapCache::Inst().Invalidate(std::wstring((LPCWSTR)path));
    }
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

    // 选中：浅橙圆角底 + 左侧橙色竖条；悬停：浅灰圆角底（行内缩 3px 留呼吸位）。
    // 选中行在鼠标停留时仍画选中样式，不换成悬停底
    if (m_selected)
    {
        Gdiplus::GraphicsPath sp;
        AddRoundRect(sp, (Gdiplus::REAL)(x + 3), (Gdiplus::REAL)(y + 2),
                     (Gdiplus::REAL)(w - 6), (Gdiplus::REAL)(h - 4), kHoverRadius);
        Gdiplus::SolidBrush sb(kSelectFill);
        g.FillPath(&sb, &sp);
        Gdiplus::GraphicsPath bar;
        AddRoundRect(bar, (Gdiplus::REAL)(x + kSelectBarLeft), (Gdiplus::REAL)(y + kSelectBarInsetY),
                     (Gdiplus::REAL)kSelectBarWidth, (Gdiplus::REAL)(h - kSelectBarInsetY * 2), kSelectBarRadius);
        Gdiplus::SolidBrush bb(kSelectBar);
        g.FillPath(&bb, &bar);
    }
    else if (IsHover())
    {
        Gdiplus::GraphicsPath hp;
        AddRoundRect(hp, (Gdiplus::REAL)(x + 3), (Gdiplus::REAL)(y + 2),
                     (Gdiplus::REAL)(w - 6), (Gdiplus::REAL)(h - 4), kHoverRadius);
        Gdiplus::SolidBrush hb(kHoverFill);
        g.FillPath(&hb, &hp);
    }

    // 头像（圆角方块，纵向居中）。真图命中画圆角真图，否则填底色 + 后面画字形。
    // 离线的成员（以及本端掉线时的本人）头像画成灰度：真图换成灰度副本，字形换成灰色底。
    const int avX = x + kEdgePad + GroupDeptIndentPx(m_treeDepth);   //部门树视图按层级缩进
    const int avY = y + (h - kAvatarSize) / 2;
    const bool avatarGray = IsAvatarGray();
    bool drewAvatarImage = false;
    {
        Gdiplus::GraphicsPath av;
        AddRoundRect(av, (Gdiplus::REAL)avX, (Gdiplus::REAL)avY,
                     (Gdiplus::REAL)kAvatarSize, (Gdiplus::REAL)kAvatarSize,
                     kAvatarRadius);

        // 真图头像从共享缓存取：与聊天气泡同一份，已经缩放成本行头像的尺寸、带好抗锯齿的圆角
        // 与预乘透明通道（见 ChatAvatarBitmapCache.h）。离线时取灰度副本，取不到时退回彩色原图；
        // 解码失败缓存里记为空，这里退回画字形。
        HBITMAP avatarBmp = NULL;
        if (!m_avatarImagePath.IsEmpty())
        {
            const std::wstring avatarPath((LPCWSTR)m_avatarImagePath);
            if (avatarGray)
            {
                avatarBmp = ChatAvatarBitmapCache::Inst().GetGray(avatarPath, kAvatarSize, (int)kAvatarRadius);
            }
            if (avatarBmp == NULL)
            {
                avatarBmp = ChatAvatarBitmapCache::Inst().Get(avatarPath, kAvatarSize, (int)kAvatarRadius);
            }
        }

        DIBSECTION avatarDib;
        if (avatarBmp != NULL
            && ::GetObject(avatarBmp, sizeof(avatarDib), &avatarDib) == sizeof(avatarDib)
            && avatarDib.dsBm.bmBits != NULL)
        {
            // 用 GDI+ 包一层直接引用位图像素（不拷贝），按预乘透明通道 1:1 贴上去：尺寸已经对上，
            // 不再缩放；圆角外的像素是透明的，不再设裁剪区（裁剪区按整像素划分，会把抗锯齿的边切成锯齿）。
            Gdiplus::Bitmap wrap(avatarDib.dsBm.bmWidth, avatarDib.dsBm.bmHeight,
                                 avatarDib.dsBm.bmWidthBytes, PixelFormat32bppPARGB,
                                 static_cast<BYTE*>(avatarDib.dsBm.bmBits));
            g.DrawImage(&wrap, avX, avY, kAvatarSize, kAvatarSize);
            drewAvatarImage = true;
        }
        else
        {
            const COLORREF fill = avatarGray ? balloonmain::MakeGrayColor(m_avatarColor) : m_avatarColor;
            Gdiplus::SolidBrush avBrush(Gdiplus::Color(255,
                GetRValue(fill), GetGValue(fill), GetBValue(fill)));
            // 按像素格对齐地填充，与头像字、照片头像对齐（见 AvatarGlyph.h）
            balloonmain::FillAvatarBackground(g, avBrush, av);
        }
    }

    // 角色徽标（右侧）—— 先算宽度，昵称避让它。普通成员没有徽标，昵称用满整行。
    int badgeW = 0;
    const wchar_t* badgeText = NULL;
    //徽标配色：角色徽标为橙色；没有角色徽标、又尚未确认入群必读时画灰蓝色的「未确认必读」
    Gdiplus::Color badgeFill = kBadgeFill;
    COLORREF badgeTextColor = kBadgeColor;
    if (m_netRole == grouprole::kNetRoleOwner)
    {
        badgeText = TR(L"群主");
    }
    else if (m_netRole == grouprole::kNetRoleAdmin)
    {
        badgeText = TR(L"管理员");
    }
    else if (m_mustReadMark)
    {
        badgeText = MustReadMemberMarkText();
        badgeFill = kMustReadFill;
        badgeTextColor = kMustReadColor;
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
        Gdiplus::SolidBrush bb(badgeFill);
        g.FillPath(&bb, &bp);
        RECT badgeRc = { badgeX, badgeY, badgeX + badgeW, badgeY + badgeH };
        DrawGdiText(hdc, badgeText, badgeRc, kBadgePx, true, badgeTextColor,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 头像字母（已画真图则跳过，避免字形盖在真图上）。
    if (!drewAvatarImage && !m_avatarText.IsEmpty())
    {
        RECT avRc = { avX, avY, avX + kAvatarSize, avY + kAvatarSize };
        // 按字形笔画外框居中（见 AvatarGlyph.h），DrawText 按单元格居中会让字偏低
        balloonmain::DrawCenteredGlyphGdi(hdc, (LPCWSTR)m_avatarText, avRc, 13, true, kAvatarText);
    }

    // 头像右下角的在线状态徽标（盖在头像字之上，故放在字形之后另起一个 Graphics 绘制，
    // 不与上面的 GDI 文字交错）。状态未知时不画。
    if (m_presence != kMemberPresenceUnknown)
    {
        Gdiplus::Graphics gb(hdc);
        gb.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const RECT box = ComputeMemberPresenceBadgeBox(avX, avY, kAvatarSize);
        const Gdiplus::REAL cx = (Gdiplus::REAL)(box.left + box.right) / 2.0f;
        const Gdiplus::REAL cy = (Gdiplus::REAL)(box.top + box.bottom) / 2.0f;
        const Gdiplus::REAL ringR = (Gdiplus::REAL)kPresenceBadgePx * kPresenceDiscRatio / 2.0f
                                  + kPresenceRingPx;
        Gdiplus::SolidBrush ring(kPresenceRingColor);
        gb.FillEllipse(&ring, cx - ringR, cy - ringR, ringR * 2.0f, ringR * 2.0f);
        balloonmain::DrawPresenceBadge(gb, m_presence, false, (Gdiplus::REAL)box.left,
                                       (Gdiplus::REAL)box.top, (Gdiplus::REAL)kPresenceBadgePx);
    }

    // 昵称（头像右侧，避让群主徽标）。
    const int nameLeft  = avX + kAvatarSize + kAvatarGap;
    int nameRight = x + w - kEdgePad - (badgeW > 0 ? badgeW + 6 : 0);
    // 部门树视图下兼属部门的那一行：名字后面紧跟一个灰色「兼」字，名字过长时先截断名字
    int secondaryW = 0;
    const wchar_t* secondaryText = m_secondary ? TR(L"兼") : NULL;
    if (secondaryText != NULL)
    {
        secondaryW = MeasureTextWidth(hdc, secondaryText, kSecondaryPx, false);
        const int nameW = MeasureTextWidth(hdc, (LPCWSTR)m_name, kNamePx, false);
        const int maxNameRight = nameRight - kSecondaryGap - secondaryW;
        const int fullNameRight = nameLeft + nameW;
        const int drawnNameRight = (fullNameRight < maxNameRight) ? fullNameRight : maxNameRight;
        RECT markRc = { drawnNameRight + kSecondaryGap, y, nameRight, y + h };
        DrawGdiText(hdc, secondaryText, markRc, kSecondaryPx, false, kSecondaryColor,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        nameRight = drawnNameRight;
    }
    RECT nameRc = { nameLeft, y, nameRight, y + h };
    DrawGdiText(hdc, (LPCWSTR)m_name, nameRc, kNamePx, false, kNameColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

} // namespace balloonchatwnd
