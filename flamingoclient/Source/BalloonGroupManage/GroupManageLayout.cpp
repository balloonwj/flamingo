/**
 *  GroupManageLayout.cpp
 *
 *  群管理对话框客户区控件树的程序化构建实现。Header / 左侧导航 / 右侧各
 *  页内容全部用 balloonui 现成控件（DuiVBox 卡片底、DuiAvatar、DuiBadge、
 *  DuiSwitch、DuiComboBox、DuiSearchBox、DuiEditHost、DuiSeparator）加本
 *  目录少量自绘小控件（导航项、操作按钮、单行文本、分组标题）搭成。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 自带 ATL/WTL 前置依赖，
 *  工程里标 PrecompiledHeader = NotUsing。
 *
 *  balloonwj@qq.com   2026-06-03
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601
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

#include "GroupManageLayout.h"
#include "GroupManageModel.h"
#include "GroupManageMapping.h"   // IsPageVisibleForRole：按角色决定导航里出现哪些页
#include "../GroupRoleRules.h"    // CanChangeGroupAvatar：谁能改群头像（与窗口层同一份规则）
#include "../BalloonMain/AvatarPickerTile.h"   // 承载群头像真图（自持位图，随控件树释放）
#include "../BalloonMain/AvatarImageUtil.h"    // MakeRoundedAvatarFromFile：把本地图裁成圆角方形
#include "GroupManageNavItem.h"
#include "GroupManageButton.h"
#include "GroupManagePaint.h"
#include "GroupManageIcons.h"

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiBadge.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSearchBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEditHost.h"

namespace balloongroupmanage {

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiGrid;
using balloonwjui::DuiAvatar;
using balloonwjui::DuiBadge;
using balloonwjui::DuiSeparator;
using balloonwjui::DuiSwitch;
using balloonwjui::DuiComboBox;
using balloonwjui::DuiSearchBox;
using balloonwjui::DuiEditHost;
//「更换头像」按钮按角色置灰时要用基类类型持有 MakeButton 的产物（它返回的是
//派生类 GroupManageButton），故把基类名也引进来
using balloonwjui::DuiButton;

namespace {

typedef DuiLayout::Hint Hint;

// =====================================================================
// 配色与尺寸常量（像素）
// =====================================================================

const COLORREF kColorWhite      = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorPageTitle  = RGB(0x22, 0x22, 0x22);   // 页面大标题
const COLORREF kColorSubtitle   = RGB(0x9A, 0x98, 0x93);   // 副标题 / 说明文字
const COLORREF kColorName       = RGB(0x2A, 0x2A, 0x2A);   // 卡片主标题（昵称）
const COLORREF kColorFieldLabel = RGB(0x3A, 0x3A, 0x3A);   // 表单字段标签
const COLORREF kColorDanger     = RGB(0xD9, 0x3F, 0x3F);   // 危险红

const COLORREF kCardBg     = RGB(0xFA, 0xF9, 0xF6);   // 普通卡片底
const COLORREF kCardBorder = RGB(0xEC, 0xEA, 0xE6);   // 普通卡片描边
const COLORREF kDangerCardBg     = RGB(0xFE, 0xF4, 0xF4);   // 危险卡片底（淡红）
const COLORREF kDangerCardBorder = RGB(0xF3, 0xDD, 0xDD);   // 危险卡片描边
const COLORREF kSeparator  = RGB(0xEC, 0xEA, 0xE6);   // 分隔线

// 分组标题前的圆点色（按角色）。
const COLORREF kDotOwner  = RGB(0xFF, 0x7A, 0x45);
const COLORREF kDotAdmin  = RGB(0x45, 0x6C, 0xDF);
const COLORREF kDotMember = RGB(0xB4, 0xB4, 0xB0);

const int kCardCorner   = 10;   // 卡片圆角
const int kMemberCardH  = 58;   // 成员卡高度
const int kGridGap      = 10;   // 成员网格行列间距
const int kMemberCols   = 3;    // 成员网格列数

// 全局字号收紧量（像素）。各处文本 / 按钮统一比设计基准小这么多，使整体
// 界面文字稍紧凑；下限 kFontFloorPx 防止过小。集中在 MakeText /
// MakeMultiText / MakeButton 应用，避免逐处改字号。
const int kFontTrim   = 1;
const int kFontFloorPx = 11;

// 应用字号收紧（带下限）。
int TrimFont(int px)
{
    const int v = px - kFontTrim;
    return (v < kFontFloorPx) ? kFontFloorPx : v;
}

// =====================================================================
// 自绘小控件
// =====================================================================

/**
 *  GmText —— 单行 / 多行文本控件，字号 / 字重 / 颜色 / 对齐由 Configure
 *  决定。纯绘制控件，铺满分配到的矩形并按对齐绘制。
 */
class GmText : public DuiControl
{
public:
    GmText()
        : m_px(13)
        , m_weight(kGmFontNormal)
        , m_color(kColorName)
        , m_align(DT_LEFT)
        , m_multiline(false)
    {
    }

    // 配置文本与样式。
    void Configure(LPCTSTR text, int px, int weight, COLORREF color,
                   UINT align, bool multiline)
    {
        m_text      = (text != NULL) ? text : _T("");
        m_px        = px;
        m_weight    = weight;
        m_color     = color;
        m_align     = align;
        m_multiline = multiline;
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        UINT flags = m_align | DT_NOPREFIX;
        if (m_multiline)
        {
            flags |= DT_WORDBREAK | DT_TOP;
        }
        else
        {
            flags |= DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
        }
        GmDrawText(hdc, (LPCWSTR)m_text, m_rcItem, m_px, m_weight, m_color, flags);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    CString  m_text;       // 文本内容
    int      m_px;         // 字号（像素）
    int      m_weight;     // 字重，取值见 GmFontWeight
    COLORREF m_color;      // 文字颜色
    UINT     m_align;      // 水平对齐（DT_LEFT / DT_CENTER / DT_RIGHT）
    bool     m_multiline;  // 是否多行换行
};

/**
 *  GmSectionHeader —— 成员管理页的分组标题（左侧彩色圆点 + 文字，如
 *  「● 群主 · 1」）。纯绘制控件。
 */
class GmSectionHeader : public DuiControl
{
public:
    GmSectionHeader() : m_dot(kDotMember) {}

    // 配置圆点颜色与文字。
    void Configure(COLORREF dot, LPCTSTR text)
    {
        m_dot  = dot;
        m_text = (text != NULL) ? text : _T("");
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int cy   = (m_rcItem.top + m_rcItem.bottom) / 2;
        const int dotR = 4;
        const int dotX = m_rcItem.left + 2;

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(m_dot),
                                                 GetGValue(m_dot), GetBValue(m_dot)));
        g.FillEllipse(&brush,
                      Gdiplus::RectF((Gdiplus::REAL)dotX, (Gdiplus::REAL)(cy - dotR),
                                     (Gdiplus::REAL)(dotR * 2), (Gdiplus::REAL)(dotR * 2)));

        RECT tr = { dotX + dotR * 2 + 8, m_rcItem.top, m_rcItem.right, m_rcItem.bottom };
        GmDrawText(hdc, (LPCWSTR)m_text, tr, 12, kGmFontNormal, kColorSubtitle,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    COLORREF m_dot;    // 圆点颜色
    CString  m_text;   // 标题文字
};

/**
 *  GmIcon —— 只画一个本目录图标集里的矢量图标的小控件。
 *
 *  之所以自绘而不用 balloonui 的 DuiImage：DuiImage 收的是位图，而本目录的图标是
 *  一组按 viewBox 24 描边画出来的矢量图形（GroupManageIcons.cpp），要交给 DuiImage
 *  就得先离屏光栅化一张位图、还得跟着 DPI 重做，代价比这十来行大得多。
 *  与同在本匿名命名空间里的 GmText / GmSectionHeader 是同一类小工具。
 */
class GmIcon : public DuiControl
{
public:
    GmIcon() : m_kind(kGmIconMembers), m_color(kColorSubtitle) {}

    // 配置图标种类与颜色。
    //   kind： 取值见 GmIconKind；
    //   color：描边颜色。
    void Configure(int kind, COLORREF color)
    {
        m_kind  = kind;
        m_color = color;
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int w = m_rcItem.right - m_rcItem.left;
        const int h = m_rcItem.bottom - m_rcItem.top;
        if (w <= 0 || h <= 0)
        {
            return;
        }

        //取正方形边长，图标在控件矩形内居中
        const int side = (w < h) ? w : h;
        const Gdiplus::REAL x = (Gdiplus::REAL)(m_rcItem.left + (w - side) / 2);
        const Gdiplus::REAL y = (Gdiplus::REAL)(m_rcItem.top + (h - side) / 2);

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        //描边宽度取 2.0，与左侧导航各图标一致
        const Gdiplus::REAL kIconStrokeWidth = 2.0f;
        DrawGmNavIcon(g, m_kind, x, y, (Gdiplus::REAL)side,
                      Gdiplus::Color(255, GetRValue(m_color), GetGValue(m_color),
                                     GetBValue(m_color)),
                      kIconStrokeWidth);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    int      m_kind;    // 图标种类，取值见 GmIconKind
    COLORREF m_color;   // 描边颜色
};

// =====================================================================
// 构建小工具
// =====================================================================

// 造一个单行文本控件。
std::unique_ptr<GmText> MakeText(LPCTSTR text, int px, int weight,
                                 COLORREF color, UINT align)
{
    std::unique_ptr<GmText> t(new GmText());
    t->Configure(text, TrimFont(px), weight, color, align, false);
    return t;
}

// 造一个多行文本控件。
std::unique_ptr<GmText> MakeMultiText(LPCTSTR text, int px, int weight,
                                      COLORREF color)
{
    std::unique_ptr<GmText> t(new GmText());
    t->Configure(text, TrimFont(px), weight, color, DT_LEFT, true);
    return t;
}

// 造一个分组标题控件。
std::unique_ptr<GmSectionHeader> MakeSectionHeader(COLORREF dot, LPCTSTR text)
{
    std::unique_ptr<GmSectionHeader> h(new GmSectionHeader());
    h->Configure(dot, text);
    return h;
}

/**
 *  GmMemberCard —— 成员卡容器。相比普通圆角卡片只差一件事：整卡接管鼠标命中。
 *
 *  为什么需要它：成员卡右键弹菜单这条链路本来是齐的 —— 基类
 *  DuiControl::OnRButtonDown 会 NotifyParent(DUIN_RCLICK)，宿主窗的 OnDuiNotify
 *  按 ctrlId 落在成员卡 id 区间内就调 ShowMemberContextMenu。断在中间的是命中
 *  测试：DuiControl::HitTest 会一路下钻到最上层的子控件，于是右键打在卡内的
 *  头像 / 昵称 / 角色徽章上，发出的 DUIN_RCLICK 带的是那些子控件的 id（它们
 *  没设 id，为 0），落不进成员卡的 id 区间，菜单便永远弹不出来。
 *
 *  卡内全是纯展示控件、没有任何需要单独响应鼠标的东西，故整卡接管命中是安全的，
 *  这也是让那条既有链路跑通的最小改动 —— 不必新开消息通道，也不动 balloonui。
 */
class GmMemberCard : public DuiVBox
{
public:
    /**
     *  DuiControl 覆写：整卡接管鼠标命中，不下钻到子控件。
     *    ptHostClient：宿主客户区坐标系下的点。
     *  @return 命中本卡返回 this，否则 NULL。
     */
    DuiControl* HitTest(POINT ptHostClient) override
    {
        if (!IsVisible() || !::PtInRect(&m_rcItem, ptHostClient))
        {
            return NULL;
        }
        return this;
    }
};

// 造一个圆角卡片容器（DuiVBox 卡片底）。
std::unique_ptr<DuiVBox> MakeCard(COLORREF bg, COLORREF border, int corner)
{
    std::unique_ptr<DuiVBox> card(new DuiVBox());
    card->SetBgColor(bg);
    card->SetBorderColor(border);
    card->SetBorderWidth(1.0f);
    card->SetCornerRadius(corner);
    return card;
}

// 造一个操作按钮。
std::unique_ptr<GroupManageButton> MakeButton(LPCTSTR text, int style,
                                              UINT ctrlId, int textPx)
{
    std::unique_ptr<GroupManageButton> btn(new GroupManageButton());
    btn->SetText(text);
    btn->SetButtonStyle(style);
    btn->SetTextPx(TrimFont(textPx));
    btn->SetCtrlId(ctrlId);
    return btn;
}

// 把 MemberOnline 翻译成 DuiAvatar 状态点常量。
int AvatarStatusFromOnline(int online)
{
    if (online == kOnlineOnline)
    {
        return DuiAvatar::StatusOnline;
    }
    if (online == kOnlineOffline)
    {
        return DuiAvatar::StatusOffline;
    }
    return DuiAvatar::StatusNone;
}

// 造一个圆角方形头像。
std::unique_ptr<DuiAvatar> MakeAvatar(LPCTSTR text, COLORREF color,
                                      int corner, int online)
{
    std::unique_ptr<DuiAvatar> av(new DuiAvatar());
    av->SetName(text);
    av->SetShape(DuiAvatar::ShapeRoundRect);
    av->SetCornerRadius(corner);
    av->SetFallbackBgColor(color);
    av->SetStatus((DuiAvatar::Status)AvatarStatusFromOnline(online));
    return av;
}

// 造群头像控件：真图已下到本地就画真图，否则画字形 + 底色。
//   model：  数据来源（真图路径 / 字形 / 底色）。
//   sizePx： 边长（像素），生成真图位图时按它缩放。
//   corner： 圆角半径（像素）。
// 真图用 AvatarPickerTile 承载而不是 DuiAvatar：后者的 SetBitmap 只借用位图、
// 由调用方负责释放，而布局层是一串自由函数，没有地方存这个 HBITMAP；前者的
// SetOwnedBitmap 自持并在析构时释放，位图生命周期随控件树走，不会漏。
std::unique_ptr<DuiAvatar> MakeGroupAvatar(const GroupManageModel& model, int sizePx, int corner)
{
    if (!model.m_groupAvatarImagePath.IsEmpty())
    {
        HBITMAP hbm = balloonmain::MakeRoundedAvatarFromFile(
            model.m_groupAvatarImagePath, sizePx, corner);
        if (hbm != NULL)
        {
            std::unique_ptr<balloonmain::AvatarPickerTile> tile(
                new balloonmain::AvatarPickerTile());
            tile->SetShape(DuiAvatar::ShapeRoundRect);
            tile->SetCornerRadius(corner);
            tile->SetOwnedBitmap(hbm);
            return std::unique_ptr<DuiAvatar>(tile.release());
        }
        //位图生成失败（文件被删 / 格式坏）：落回字形，不让头像位空着。
    }
    return MakeAvatar(model.m_groupAvatarText, model.m_groupAvatarColor, corner, kOnlineNone);
}

// 造一个角色徽章 chip（群主 / 管理员 / 成员）。
std::unique_ptr<DuiBadge> MakeRoleBadge(int role)
{
    LPCTSTR  label = _T("成员");
    COLORREF bg    = RGB(0xEF, 0xEE, 0xEA);
    COLORREF fg    = RGB(0x9A, 0x98, 0x93);

    if (role == kRoleOwner)
    {
        label = _T("群主");
        bg    = RGB(0xFD, 0xE9, 0xDD);
        fg    = RGB(0xE0, 0x6A, 0x2E);
    }
    else if (role == kRoleAdmin)
    {
        label = _T("管理员");
        bg    = RGB(0xE6, 0xEC, 0xFB);
        fg    = RGB(0x45, 0x6C, 0xDF);
    }

    std::unique_ptr<DuiBadge> badge(new DuiBadge());
    badge->SetText(label);
    badge->SetMaxDisplayChars(0);    // 不截断（chip 用法）
    badge->SetCornerRadius(4);
    badge->SetHideWhenEmpty(false);
    badge->SetBgColor(bg);
    badge->SetTextColor(fg);
    return badge;
}

// 造一条横向分隔线（用于卡片内部分隔各设置行）。
std::unique_ptr<DuiControl> MakeSeparatorRow()
{
    std::unique_ptr<DuiSeparator> sep(new DuiSeparator());
    sep->SetOrientation(DuiSeparator::Horizontal);
    sep->SetColor(kSeparator);
    return std::unique_ptr<DuiControl>(sep.release());
}

// =====================================================================
// 成员管理页（design/群管理1.png）
// =====================================================================

// 造一张成员卡：头像 + 昵称 / 职务 + 角色徽章。
//   m：       成员数据；
//   memberIndex：该成员在 model.m_members 里的下标，用来算卡片的 ctrl id ——
//               右键菜单靠它认出点的是哪一位。下标为负表示不需要右键（不设 id）。
std::unique_ptr<DuiControl> MakeMemberCard(const GroupMember& m, int memberIndex)
{
    //成员卡用 GmMemberCard 而不是普通卡片容器 —— 它整卡接管鼠标命中并把右键
    //报给宿主窗，否则右键会打在卡内的昵称 / 徽章上，菜单永远弹不出来。
    std::unique_ptr<GmMemberCard> card(new GmMemberCard());
    card->SetBgColor(kCardBg);
    card->SetBorderColor(kCardBorder);
    card->SetBorderWidth(1.0f);
    card->SetCornerRadius(kCardCorner);
    card->SetPadding(14, 0, 14, 0);
    if (memberIndex >= 0 && memberIndex <= (kIdMemberCardMax - kIdMemberCardBase))
    {
        card->SetCtrlId(kIdMemberCardBase + memberIndex);
    }

    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(12);

    row->AddChild(MakeAvatar(m.avatarText, m.avatarColor, 8, m.online),
                  Hint().Fixed(36, 36).AlignC(DuiLayout::AlignCenter));

    std::unique_ptr<DuiVBox> info(new DuiVBox());
    info->SetGap(2);
    info->AddChild(MakeText(m.name, 14, kGmFontSemiBold, kColorName, DT_LEFT),
                   Hint().Fixed(18));
    info->AddChild(MakeText(m.title, 12, kGmFontNormal, kColorSubtitle, DT_LEFT),
                   Hint().Fixed(15));
    Hint infoHint;
    infoHint.fixedCross = 35;
    infoHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(info), infoHint);

    row->AddChild(MakeRoleBadge(m.role),
                  Hint().Fixed(52, 20).AlignC(DuiLayout::AlignCenter));

    card->AddChild(std::move(row));
    return std::unique_ptr<DuiControl>(card.release());
}

// 造某一角色分组的成员网格（3 列，行数随成员数）。outRows 回传行数。
std::unique_ptr<DuiControl> MakeMemberGrid(const GroupManageModel& model,
                                           GroupRole role, int& outRows)
{
    std::unique_ptr<DuiGrid> grid(new DuiGrid());
    grid->SetGap(kGridGap);

    int count = 0;
    for (std::vector<GroupMember>::const_iterator it = model.m_members.begin();
         it != model.m_members.end(); ++it)
    {
        if (it->role == role)
        {
            ++count;
        }
    }
    const int rows = (count + kMemberCols - 1) / kMemberCols;
    outRows = rows;
    grid->SetGrid(rows, kMemberCols);

    //按模型里的原始下标给卡片编 id —— 网格是按角色分组渲染的，卡片的显示顺序
    //与模型顺序不同，故不能用网格内的序号，必须用模型下标。
    for (size_t i = 0; i < model.m_members.size(); ++i)
    {
        if (model.m_members[i].role == role)
        {
            grid->AddChild(MakeMemberCard(model.m_members[i], (int)i));
        }
    }
    return std::unique_ptr<DuiControl>(grid.release());
}

// 往页面追加「分组标题 + 成员网格」一段。
void AppendMemberSection(DuiVBox* page, const GroupManageModel& model,
                         GroupRole role, COLORREF dot, LPCTSTR roleName)
{
    const int count = model.CountByRole(role);
    if (count <= 0)
    {
        return;
    }

    CString header;
    header.Format(_T("%s · %d"), roleName, count);
    page->AddChild(MakeSectionHeader(dot, header), Hint().Fixed(22));

    int rows = 0;
    std::unique_ptr<DuiControl> grid = MakeMemberGrid(model, role, rows);
    const int gridH = rows * kMemberCardH + (rows > 0 ? (rows - 1) * kGridGap : 0);
    page->AddChild(std::move(grid), Hint().Fixed(gridH));
}

std::unique_ptr<DuiControl> BuildMembersPage(const GroupManageModel& model)
{
    std::unique_ptr<DuiVBox> page(new DuiVBox());
    page->SetPadding(24, 16, 24, 18);
    page->SetGap(16);

    // 顶部：搜索框 + 邀请成员。
    std::unique_ptr<DuiHBox> topRow(new DuiHBox());
    topRow->SetGap(12);

    std::unique_ptr<DuiSearchBox> search(new DuiSearchBox());
    search->SetPlaceholder(_T("搜索成员姓名、备注、职务…"));
    search->SetCtrlId(kIdSearchMembers);
    search->SetShowBorder(true);
    topRow->AddChild(std::move(search), Hint().Weight(1));

    topRow->AddChild(MakeButton(_T("＋ 邀请成员"), kGmBtnPrimary, kIdBtnInvite, 13),
                     Hint().Fixed(118));
    page->AddChild(std::move(topRow), Hint().Fixed(44));

    // 三个角色分组。
    AppendMemberSection(page.get(), model, kRoleOwner,  kDotOwner,  _T("群主"));
    AppendMemberSection(page.get(), model, kRoleAdmin,  kDotAdmin,  _T("管理员"));
    AppendMemberSection(page.get(), model, kRoleMember, kDotMember, _T("普通成员"));

    return std::unique_ptr<DuiControl>(page.release());
}

// =====================================================================
// 加群审批页（design/群管理2.png）
// =====================================================================

// 把来源枚举翻译成展示文本。
CString JoinSourceText(int source)
{
    if (source == kJoinFromFriend)
    {
        return _T("好友邀请");
    }
    if (source == kJoinFromQrCode)
    {
        return _T("扫码加群");
    }
    return _T("群搜索");
}

// 造一张加群申请卡。index 用于逐条按钮 id。
std::unique_ptr<DuiControl> MakeRequestCard(const JoinRequest& r, int index)
{
    std::unique_ptr<DuiVBox> card = MakeCard(kCardBg, kCardBorder, kCardCorner);
    card->SetPadding(16, 14, 16, 14);

    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(14);

    row->AddChild(MakeAvatar(r.avatarText, r.avatarColor, 9, kOnlineNone),
                  Hint().Fixed(40, 40).AlignC(DuiLayout::AlignCenter));

    // 中部信息三行。
    std::unique_ptr<DuiVBox> info(new DuiVBox());
    info->SetGap(4);

    CString line1;
    line1.Format(_T("%s    账号 %s"), (LPCTSTR)r.name, (LPCTSTR)r.account);
    info->AddChild(MakeText(line1, 14, kGmFontSemiBold, kColorName, DT_LEFT),
                   Hint().Fixed(22));

    CString quoted;
    quoted.Format(_T("“%s”"), (LPCTSTR)r.message);
    info->AddChild(MakeText(quoted, 12, kGmFontNormal, RGB(0x55, 0x55, 0x55), DT_LEFT),
                   Hint().Fixed(20));

    CString line3;
    line3.Format(_T("来源：%s"), (LPCTSTR)JoinSourceText(r.source));
    if (!r.recommender.IsEmpty())
    {
        line3 += _T(" · 由 ");
        line3 += r.recommender;
        line3 += _T(" 推荐");
    }
    line3 += _T(" · ");
    line3 += r.timeText;
    info->AddChild(MakeText(line3, 11, kGmFontNormal, kColorSubtitle, DT_LEFT),
                   Hint().Fixed(18));

    row->AddChild(std::move(info), Hint().Weight(1));

    // 右侧：同意 / 拒绝 竖排。
    std::unique_ptr<DuiVBox> btns(new DuiVBox());
    btns->SetGap(8);
    btns->AddChild(MakeButton(_T("同意"), kGmBtnPrimary,
                              kIdApprovalAcceptBase + index, 13),
                   Hint().Fixed(30));
    btns->AddChild(MakeButton(_T("拒绝"), kGmBtnSecondary,
                              kIdApprovalRejectBase + index, 13),
                   Hint().Fixed(30));
    Hint btnHint;
    btnHint.fixedMain  = 76;
    btnHint.fixedCross = 68;
    btnHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(btns), btnHint);

    card->AddChild(std::move(row));
    return std::unique_ptr<DuiControl>(card.release());
}

std::unique_ptr<DuiControl> BuildApprovalPage(const GroupManageModel& model)
{
    std::unique_ptr<DuiVBox> page(new DuiVBox());
    page->SetPadding(24, 16, 24, 18);
    page->SetGap(14);

    // 标题行：左侧标题 + 副标题，右侧批量按钮。
    std::unique_ptr<DuiHBox> titleRow(new DuiHBox());
    titleRow->SetGap(10);

    std::unique_ptr<DuiVBox> titleCol(new DuiVBox());
    titleCol->SetGap(4);
    titleCol->AddChild(MakeText(_T("加群申请"), 16, kGmFontBold, kColorPageTitle, DT_LEFT),
                       Hint().Fixed(24));
    CString sub;
    sub.Format(_T("待处理 %d 条 · 已自动拒绝超过 7 天的申请"),
               model.PendingRequestCount());
    titleCol->AddChild(MakeText(sub, 12, kGmFontNormal, kColorSubtitle, DT_LEFT),
                       Hint().Fixed(18));
    titleRow->AddChild(std::move(titleCol), Hint().Weight(1));

    titleRow->AddChild(MakeButton(_T("批量同意"), kGmBtnSecondary, kIdBtnApproveAll, 13),
                       Hint().Fixed(84).AlignC(DuiLayout::AlignCenter));
    titleRow->AddChild(MakeButton(_T("批量拒绝"), kGmBtnSecondary, kIdBtnRejectAll, 13),
                       Hint().Fixed(84).AlignC(DuiLayout::AlignCenter));
    page->AddChild(std::move(titleRow), Hint().Fixed(48));

    // 申请卡列表。
    int index = 0;
    for (std::vector<JoinRequest>::const_iterator it = model.m_requests.begin();
         it != model.m_requests.end(); ++it, ++index)
    {
        page->AddChild(MakeRequestCard(*it, index), Hint().Fixed(88));
    }

    return std::unique_ptr<DuiControl>(page.release());
}

// =====================================================================
// 禁言管理页（design/群管理3.png）
// =====================================================================

// 造一张禁言成员卡。index 用于逐条按钮 id。
std::unique_ptr<DuiControl> MakeMuteCard(const MutedMember& m, int index)
{
    std::unique_ptr<DuiVBox> card = MakeCard(kCardBg, kCardBorder, kCardCorner);
    card->SetPadding(16, 0, 16, 0);

    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(14);

    row->AddChild(MakeAvatar(m.avatarText, m.avatarColor, 9, kOnlineNone),
                  Hint().Fixed(40, 40).AlignC(DuiLayout::AlignCenter));

    std::unique_ptr<DuiVBox> info(new DuiVBox());
    info->SetGap(3);
    info->AddChild(MakeText(m.name, 14, kGmFontSemiBold, kColorName, DT_LEFT),
                   Hint().Fixed(22));
    CString reason;
    reason.Format(_T("原因：%s"), (LPCTSTR)m.reason);
    info->AddChild(MakeText(reason, 12, kGmFontNormal, kColorSubtitle, DT_LEFT),
                   Hint().Fixed(18));
    Hint infoHint;
    infoHint.fixedCross = 40;
    infoHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(info), infoHint);

    // 右侧：剩余时长（永久标红）+ 延长 / 解除。
    std::unique_ptr<DuiVBox> right(new DuiVBox());
    right->SetGap(8);
    const COLORREF remainColor = m.permanent ? kColorDanger : kColorSubtitle;
    right->AddChild(MakeText(m.remainingText, 12,
                             m.permanent ? kGmFontSemiBold : kGmFontNormal,
                             remainColor, DT_RIGHT),
                    Hint().Fixed(18));

    std::unique_ptr<DuiHBox> btnRow(new DuiHBox());
    btnRow->SetGap(8);
    if (!m.permanent)
    {
        btnRow->AddChild(MakeButton(_T("延长"), kGmBtnSecondary,
                                    kIdMuteExtendBase + index, 12),
                         Hint().Fixed(54));
    }
    btnRow->AddChild(MakeButton(_T("解除"), kGmBtnSecondary,
                                kIdMuteReleaseBase + index, 12),
                     Hint().Fixed(54));
    right->AddChild(std::move(btnRow), Hint().Fixed(30));

    Hint rightHint;
    rightHint.fixedMain  = 130;
    rightHint.fixedCross = 56;
    rightHint.alignCross = DuiLayout::AlignCenter;
    rightHint.alignMain  = DuiLayout::AlignFar;
    row->AddChild(std::move(right), rightHint);

    card->AddChild(std::move(row));
    return std::unique_ptr<DuiControl>(card.release());
}

std::unique_ptr<DuiControl> BuildMutePage(const GroupManageModel& model)
{
    std::unique_ptr<DuiVBox> page(new DuiVBox());
    page->SetPadding(24, 16, 24, 18);
    page->SetGap(14);

    // 标题行：标题 + 副标题，右侧「全员禁言」开关。
    std::unique_ptr<DuiHBox> titleRow(new DuiHBox());
    titleRow->SetGap(10);

    std::unique_ptr<DuiVBox> titleCol(new DuiVBox());
    titleCol->SetGap(4);
    titleCol->AddChild(MakeText(_T("禁言管理"), 16, kGmFontBold, kColorPageTitle, DT_LEFT),
                       Hint().Fixed(24));
    CString sub;
    sub.Format(_T("禁言中 %d 人"), model.MutedCount());
    titleCol->AddChild(MakeText(sub, 12, kGmFontNormal, kColorSubtitle, DT_LEFT),
                       Hint().Fixed(18));
    titleRow->AddChild(std::move(titleCol), Hint().Weight(1));

    titleRow->AddChild(MakeText(_T("全员禁言"), 13, kGmFontNormal, kColorFieldLabel, DT_RIGHT),
                       Hint().Fixed(72).AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiSwitch> swAll(new DuiSwitch());
    swAll->SetCtrlId(kIdSwitchMuteAll);
    swAll->SetChecked(model.m_muteAll, false, false);
    titleRow->AddChild(std::move(swAll), Hint().Fixed(46, 24).AlignC(DuiLayout::AlignCenter));
    page->AddChild(std::move(titleRow), Hint().Fixed(48));

    // 添加禁言成员按钮。
    std::unique_ptr<DuiHBox> addRow(new DuiHBox());
    addRow->AddChild(MakeButton(_T("＋ 添加禁言成员"), kGmBtnPrimary, kIdBtnAddMute, 13),
                     Hint().Fixed(140));
    addRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    page->AddChild(std::move(addRow), Hint().Fixed(40));

    // 禁言卡列表。
    int index = 0;
    for (std::vector<MutedMember>::const_iterator it = model.m_muted.begin();
         it != model.m_muted.end(); ++it, ++index)
    {
        page->AddChild(MakeMuteCard(*it, index), Hint().Fixed(70));
    }

    return std::unique_ptr<DuiControl>(page.release());
}

// =====================================================================
// 基本信息页（design/群管理4.png）
// =====================================================================

// 造一行表单：左侧标签（可带副说明）+ 右侧控件。
std::unique_ptr<DuiControl> MakeFormRow(LPCTSTR label, LPCTSTR desc,
                                        std::unique_ptr<DuiControl> control,
                                        int controlWidth, int rowHeight)
{
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(12);

    std::unique_ptr<DuiVBox> labelCol(new DuiVBox());
    labelCol->SetGap(3);
    if (desc != NULL && desc[0] != _T('\0'))
    {
        labelCol->AddChild(MakeText(label, 14, kGmFontNormal, kColorFieldLabel, DT_LEFT),
                           Hint().Fixed(22));
        labelCol->AddChild(MakeText(desc, 12, kGmFontNormal, kColorSubtitle, DT_LEFT),
                           Hint().Fixed(18));
    }
    else
    {
        labelCol->AddChild(MakeText(label, 14, kGmFontNormal, kColorFieldLabel, DT_LEFT),
                           Hint().Fixed(22));
    }
    Hint labelHint;
    labelHint.weight     = 1;
    labelHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(labelCol), labelHint);

    row->AddChild(std::move(control),
                  Hint().Fixed(controlWidth).AlignC(DuiLayout::AlignCenter));

    return std::unique_ptr<DuiControl>(row.release());
}

std::unique_ptr<DuiControl> BuildBasicInfoPage(const GroupManageModel& model)
{
    std::unique_ptr<DuiVBox> page(new DuiVBox());
    page->SetPadding(24, 16, 24, 18);
    page->SetGap(16);

    page->AddChild(MakeText(_T("基本信息"), 16, kGmFontBold, kColorPageTitle, DT_LEFT),
                   Hint().Fixed(28));

    // 群头像卡。
    std::unique_ptr<DuiVBox> avatarCard = MakeCard(kCardBg, kCardBorder, kCardCorner);
    avatarCard->SetPadding(18, 0, 18, 0);
    std::unique_ptr<DuiHBox> avatarRow(new DuiHBox());
    avatarRow->SetGap(16);
    const int kBasicAvatarPx     = 56;   //基本信息页头像卡里的头像边长
    const int kBasicAvatarCorner = 10;   //其圆角半径
    avatarRow->AddChild(MakeGroupAvatar(model, kBasicAvatarPx, kBasicAvatarCorner),
                        Hint().Fixed(kBasicAvatarPx, kBasicAvatarPx)
                              .AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiVBox> avatarInfo(new DuiVBox());
    avatarInfo->SetGap(4);
    avatarInfo->AddChild(MakeText(_T("群头像"), 14, kGmFontSemiBold, kColorName, DT_LEFT),
                         Hint().Fixed(22));
    avatarInfo->AddChild(MakeText(_T("支持 JPG、PNG，建议 200×200 以上"), 12,
                                  kGmFontNormal, kColorSubtitle, DT_LEFT),
                         Hint().Fixed(18));
    Hint avatarInfoHint;
    avatarInfoHint.weight     = 1;
    avatarInfoHint.fixedCross = 44;
    avatarInfoHint.alignCross = DuiLayout::AlignCenter;
    avatarRow->AddChild(std::move(avatarInfo), avatarInfoHint);
    //只有群主和管理员能改群头像（服务端同口径，见 grouprole::CanChangeGroupAvatar）。
    //对普通成员置灰而不是隐藏：按钮没了，旁边"支持 JPG、PNG…"那行说明就成了没有
    //指向的一句话；置灰能让人看出这里能换、只是自己没权限。
    //类型跟着 MakeButton 的返回值走（GroupManageButton 派生自 DuiButton）——
    //写成 unique_ptr<DuiButton> 既要多 include 一个头，也没有额外好处。
    std::unique_ptr<GroupManageButton> btnChangeAvatar =
        MakeButton(_T("更换头像"), kGmBtnSecondary, kIdBtnChangeAvatar, 13);
    if (!grouprole::CanChangeGroupAvatar(UiRoleToNetRole(model.m_myRole)))
    {
        btnChangeAvatar->SetEnabled(false);
    }
    avatarRow->AddChild(std::move(btnChangeAvatar),
                        Hint().Fixed(96, 34).AlignC(DuiLayout::AlignCenter));
    avatarCard->AddChild(std::move(avatarRow));
    page->AddChild(std::move(avatarCard), Hint().Fixed(82));

    // 群名称。
    std::unique_ptr<DuiEditHost> nameEdit(new DuiEditHost());
    nameEdit->SetCtrlId(kIdEditGroupName);
    nameEdit->SetShowBorder(true);
    nameEdit->SetText(model.m_groupName);
    page->AddChild(MakeFormRow(_T("群名称"), _T(""),
                               std::unique_ptr<DuiControl>(nameEdit.release()), 260, 38),
                   Hint().Fixed(40));

    // 群号（只读文本 + 复制按钮）。
    std::unique_ptr<DuiHBox> numberBox(new DuiHBox());
    numberBox->SetGap(8);
    numberBox->AddChild(MakeText(model.m_groupNumber, 14, kGmFontNormal,
                                 kColorName, DT_RIGHT),
                        Hint().Weight(1).AlignC(DuiLayout::AlignCenter));
    numberBox->AddChild(MakeButton(_T("复制"), kGmBtnSecondary, kIdBtnCopyGroupNo, 11),
                        Hint().Fixed(44, 26).AlignC(DuiLayout::AlignCenter));
    page->AddChild(MakeFormRow(_T("群号"), _T(""),
                               std::unique_ptr<DuiControl>(numberBox.release()), 200, 38),
                   Hint().Fixed(40));

    // 群公告（只读摘要 + 跳转按钮）。
    //
    // 这里<u>刻意不给输入框</u>：群公告已经是一条条带标题、发布人、优先级、可撤回的
    // 记录（存在 t_group_announcement 表里），由「群公告」页统一发布与管理。若这里
    // 再留一个直接改文本的框，同一份数据就有了两条写入路径，先后顺序不同结果就不同。
    // 故本行只显示当前最新一条公告，编辑走右边那个跳转按钮。
    std::unique_ptr<DuiHBox> annBox(new DuiHBox());
    annBox->SetGap(8);
    CString annSummary = model.m_announcement;
    if (annSummary.IsEmpty())
    {
        annSummary = _T("暂无群公告");
    }
    annBox->AddChild(MakeText(annSummary, 13, kGmFontNormal, kColorSubtitle, DT_LEFT),
                     Hint().Weight(1).AlignC(DuiLayout::AlignCenter));
    //普通成员看不到「群公告」页，给他们一个点了会落空的按钮没有意义
    if (IsPageVisibleForRole(kPageAnnouncement, model.m_myRole))
    {
        annBox->AddChild(MakeButton(_T("去管理"), kGmBtnSecondary, kIdBtnAnnounceGoto, 11),
                         Hint().Fixed(64, 26).AlignC(DuiLayout::AlignCenter));
    }
    page->AddChild(MakeFormRow(_T("群公告"), _T("在「群公告」里发布与管理"),
                               std::unique_ptr<DuiControl>(annBox.release()), 260, 38),
                   Hint().Fixed(48));

    // 群简介。
    std::unique_ptr<DuiEditHost> introEdit(new DuiEditHost());
    introEdit->SetCtrlId(kIdEditIntro);
    introEdit->SetShowBorder(true);
    introEdit->SetPlaceholder(_T("介绍一下这个群…"));
    introEdit->SetText(model.m_intro);
    page->AddChild(MakeFormRow(_T("群简介"), _T("可选，显示在群资料页"),
                               std::unique_ptr<DuiControl>(introEdit.release()), 260, 38),
                   Hint().Fixed(48));

    // 底部：重置 / 保存修改（右对齐）。
    std::unique_ptr<DuiHBox> btnRow(new DuiHBox());
    btnRow->SetGap(12);
    btnRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    btnRow->AddChild(MakeButton(_T("重置"), kGmBtnSecondary, kIdBtnReset, 13),
                     Hint().Fixed(80));
    btnRow->AddChild(MakeButton(_T("保存修改"), kGmBtnPrimary, kIdBtnSave, 13),
                     Hint().Fixed(110));
    page->AddChild(std::move(btnRow), Hint().Fixed(40));

    return std::unique_ptr<DuiControl>(page.release());
}

// =====================================================================
// 群权限页（design/群管理5.png）
// =====================================================================

// 造一行权限设置：左侧标题 + 说明，右侧控件。trailing 可选（如「管理词库」）。
std::unique_ptr<DuiControl> MakePermRow(LPCTSTR title, LPCTSTR desc,
                                        std::unique_ptr<DuiControl> control,
                                        int controlWidth,
                                        std::unique_ptr<DuiControl> trailing,
                                        int trailingWidth)
{
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(12);
    row->SetPadding(18, 0, 18, 0);

    std::unique_ptr<DuiVBox> labelCol(new DuiVBox());
    labelCol->SetGap(3);
    labelCol->AddChild(MakeText(title, 14, kGmFontNormal, kColorFieldLabel, DT_LEFT),
                       Hint().Fixed(22));
    labelCol->AddChild(MakeText(desc, 12, kGmFontNormal, kColorSubtitle, DT_LEFT),
                       Hint().Fixed(18));
    Hint labelHint;
    labelHint.weight     = 1;
    labelHint.fixedCross = 40;
    labelHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(labelCol), labelHint);

    if (trailing)
    {
        row->AddChild(std::move(trailing),
                      Hint().Fixed(trailingWidth, 30).AlignC(DuiLayout::AlignCenter));
    }
    row->AddChild(std::move(control),
                  Hint().Fixed(controlWidth, controlWidth >= 100 ? 34 : 24)
                        .AlignC(DuiLayout::AlignCenter));
    return std::unique_ptr<DuiControl>(row.release());
}

// 造一个开关，按初值设定。
std::unique_ptr<DuiControl> MakePermSwitch(UINT ctrlId, bool checked)
{
    std::unique_ptr<DuiSwitch> sw(new DuiSwitch());
    sw->SetCtrlId(ctrlId);
    sw->SetChecked(checked, false, false);
    return std::unique_ptr<DuiControl>(sw.release());
}

// =====================================================================
// 管理词库（群权限页「管理词库」按钮进入的二级界面）
// =====================================================================

// 管理词库页的行高与间距（像素）。
const int kWordRowH    = 44;   // 一条敏感词的行高
const int kWordRowGap  = 8;    // 词条之间的间距
const int kWordAddRowH = 36;   // 顶部"输入 + 添加"行的高度

// 造一条敏感词行：词本身 + 添加时间 + 右侧删除按钮。
//   w：        词条数据；
//   wordIndex：该词在 model.m_sensitiveWords 里的下标，用来算删除按钮的 ctrl id。
std::unique_ptr<DuiControl> MakeSensitiveWordRow(const SensitiveWord& w, int wordIndex)
{
    std::unique_ptr<DuiVBox> card = MakeCard(kCardBg, kCardBorder, kCardCorner);
    card->SetPadding(14, 0, 12, 0);

    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(10);

    // 词本身占主要宽度
    Hint wordHint;
    wordHint.weight = 1;
    wordHint.fixedCross = 20;
    wordHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(MakeText(w.word, 13, kGmFontSemiBold, kColorName, DT_LEFT), wordHint);

    // 添加时间靠右显示，比词本身淡一档
    row->AddChild(MakeText(w.timeText, 12, kGmFontNormal, kColorSubtitle, DT_RIGHT),
                  Hint().Fixed(90, 18).AlignC(DuiLayout::AlignCenter));

    // 删除按钮。id 用模型下标算，点击时反查是哪一条。
    if (wordIndex >= 0 && wordIndex <= (kIdSensitiveDelMax - kIdSensitiveDelBase))
    {
        row->AddChild(MakeButton(_T("删除"), kGmBtnDanger,
                                 kIdSensitiveDelBase + wordIndex, 12),
                      Hint().Fixed(56, 26).AlignC(DuiLayout::AlignCenter));
    }

    card->AddChild(std::move(row));
    return std::unique_ptr<DuiControl>(card.release());
}

// =====================================================================
// 群公告页（design/群管理7.png）
// =====================================================================

// 群公告页各块的尺寸（像素）。
const int kAnnFormTitleH   = 34;    // 标题输入框高
const int kAnnFormBodyH    = 120;   // 正文输入框高（约容 5 行）
const int kAnnFormBtnH     = 34;    // 表单底部按钮高
const int kAnnChipW        = 56;    // 优先级 chip 宽
const int kAnnChipH        = 24;    // 优先级 chip 高
const int kAnnRowGap       = 10;    // 历史公告条目之间的间距
const int kAnnRowTitleH    = 22;    // 历史条目的标题行高
const int kAnnRowMetaH     = 18;    // 历史条目的"发布人 · 时间"行高
const int kAnnRowBodyH     = 20;    // 历史条目的正文摘要行高（单行省略）
const int kAnnRowPadV      = 12;    // 历史条目上下内边距
const int kAnnBellSize     = 26;    // 左侧铃铛图标的边长
const int kAnnRowBtnH      = 26;    // 历史条目右侧「重新发布」「删除」按钮的高
const int kAnnRowBtnGap    = 6;     // 上下两个按钮之间的间距

// 一条历史公告的整体行高。由内部各行累加得到，写死一个数字迟早对不上。
//
// 右侧按钮列要的高度是 kAnnRowBtnH * 2 + kAnnRowBtnGap = 58，比中间那三行
// 累加出来的 60 略小，正好放得下。日后调这几个常量时留意别让按钮列超过中间列，
// 否则按钮会被行高截掉一截。
const int kAnnRowH = kAnnRowPadV * 2 + kAnnRowTitleH + kAnnRowBodyH + kAnnRowMetaH;

// 把优先级翻译成徽章的文案与配色。
//   severity：取值见 AnnounceSeverity；
//   bgOut / fgOut：[出参] 徽章底色与文字色。
//   返回：徽章文案（编译期常量，不需释放）。
LPCTSTR SeverityBadgeStyle(int severity, COLORREF& bgOut, COLORREF& fgOut)
{
    if (severity == kSeverityUrgent)
    {
        bgOut = RGB(0xFD, 0xE3, 0xE3);
        fgOut = RGB(0xD9, 0x3F, 0x3F);
        return _T("紧急");
    }
    if (severity == kSeverityImportant)
    {
        bgOut = RGB(0xFD, 0xE9, 0xDD);
        fgOut = RGB(0xE0, 0x6A, 0x2E);
        return _T("重要");
    }
    bgOut = RGB(0xE6, 0xEC, 0xFB);
    fgOut = RGB(0x45, 0x6C, 0xDF);
    return _T("通知");
}

// 造一枚优先级徽章 chip。
std::unique_ptr<DuiBadge> MakeSeverityBadge(int severity)
{
    COLORREF bg = 0;
    COLORREF fg = 0;
    LPCTSTR  label = SeverityBadgeStyle(severity, bg, fg);

    std::unique_ptr<DuiBadge> badge(new DuiBadge());
    badge->SetText(label);
    badge->SetMaxDisplayChars(0);    // 不截断（chip 用法）
    badge->SetCornerRadius(4);
    badge->SetHideWhenEmpty(false);
    badge->SetBgColor(bg);
    badge->SetTextColor(fg);
    return badge;
}

// 造一条历史公告：左侧铃铛 + 标题 / 优先级徽章 + 正文摘要 + 发布人时间，
// 右侧「重新发布」「撤回」两个按钮。对照 design/群管理7.png 的历史公告条目。
//   a：    公告数据；
//   index：该条在 model.m_announcements 里的下标，用来算两个按钮的 ctrl id。
std::unique_ptr<DuiControl> MakeAnnouncementRow(const GroupAnnouncement& a, int index)
{
    std::unique_ptr<DuiVBox> card = MakeCard(kCardBg, kCardBorder, kCardCorner);
    card->SetPadding(14, kAnnRowPadV, 12, kAnnRowPadV);

    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(12);

    // 左侧铃铛图标。复用导航「群公告」项那枚图标，两处是同一个概念。
    std::unique_ptr<GmIcon> bell(new GmIcon());
    bell->Configure(kGmIconAnnounce, RGB(0xE0, 0x6A, 0x2E));
    row->AddChild(std::unique_ptr<DuiControl>(bell.release()),
                  Hint().Fixed(kAnnBellSize, kAnnBellSize).AlignC(DuiLayout::AlignCenter));

    // 中间：标题行（标题 + 优先级徽章 + 已撤回标记）/ 正文摘要 / 发布人·时间
    std::unique_ptr<DuiVBox> col(new DuiVBox());
    col->SetGap(2);

    std::unique_ptr<DuiHBox> titleRow(new DuiHBox());
    titleRow->SetGap(8);
    //标题按内容自然占宽，徽章紧随其后 —— 让徽章贴着标题，而不是被推到最右边
    titleRow->AddChild(MakeText(a.title, 14, kGmFontBold, kColorName, DT_LEFT),
                       Hint().Weight(1).AlignC(DuiLayout::AlignCenter));
    titleRow->AddChild(std::unique_ptr<DuiControl>(MakeSeverityBadge(a.severity).release()),
                       Hint().Fixed(kAnnChipW, kAnnChipH).AlignC(DuiLayout::AlignCenter));
    col->AddChild(std::move(titleRow), Hint().Fixed(kAnnRowTitleH));

    //正文只显示一行摘要 —— 历史列表是用来"找到某条公告"的，不是用来通读的，
    //完整正文在群聊窗边栏的公告卡里看
    col->AddChild(MakeText(a.content, 13, kGmFontNormal, kColorSubtitle, DT_LEFT),
                  Hint().Fixed(kAnnRowBodyH));

    CString meta;
    meta.Format(_T("%s · %s"), (LPCTSTR)a.publisherName, (LPCTSTR)a.timeText);
    col->AddChild(MakeText(meta, 12, kGmFontNormal, kColorSubtitle, DT_LEFT),
                  Hint().Fixed(kAnnRowMetaH));

    Hint colHint;
    colHint.weight     = 1;
    colHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(col), colHint);

    // 右侧两个按钮。id 用<u>本页内</u>的下标算，点击时反查是哪一条。
    if (index >= 0 && index <= (kIdAnnounceRepublishMax - kIdAnnounceRepublishBase))
    {
        std::unique_ptr<DuiVBox> btnCol(new DuiVBox());
        btnCol->SetGap(kAnnRowBtnGap);
        //Hint::Fixed 的第一个参数是<u>主轴</u>尺寸。btnCol 是 DuiVBox，主轴是竖直
        //方向，故这里传的是行高；交叉轴（宽）留 -1 撑满，两个按钮才等宽。
        //曾误写成 Fixed(0, 26)（高 0、宽 26），两个按钮都被压成零高度，界面上
        //一个都看不见。
        btnCol->AddChild(MakeButton(_T("重新发布"), kGmBtnSecondary,
                                    kIdAnnounceRepublishBase + index, 12),
                         Hint().Fixed(kAnnRowBtnH));
        //删除是这一页上唯一不可恢复的操作，用危险色，点击时还要过一道确认
        btnCol->AddChild(MakeButton(_T("删除"), kGmBtnDanger,
                                    kIdAnnounceDeleteBase + index, 12),
                         Hint().Fixed(kAnnRowBtnH));
        row->AddChild(std::move(btnCol),
                      Hint().Fixed(84).AlignC(DuiLayout::AlignCenter));
    }

    card->AddChild(std::move(row));
    return std::unique_ptr<DuiControl>(card.release());
}

std::unique_ptr<DuiControl> BuildAnnouncementPage(const GroupManageModel& model)
{
    std::unique_ptr<DuiVBox> page(new DuiVBox());
    page->SetPadding(24, 16, 24, 18);
    page->SetGap(8);

    page->AddChild(MakeText(_T("群公告"), 16, kGmFontBold, kColorPageTitle, DT_LEFT),
                   Hint().Fixed(28));
    page->AddChild(MakeText(_T("发布后所有成员将收到弹窗通知，并可在群聊窗的「公告」里再次查看"),
                            12, kGmFontNormal, kColorSubtitle, DT_LEFT),
                   Hint().Fixed(22));

    // ---- 发布新公告卡片 ----
    {
        std::unique_ptr<DuiVBox> card = MakeCard(kColorWhite, kCardBorder, kCardCorner);
        card->SetPadding(16, 12, 16, 12);
        card->SetGap(10);

        // 卡片头：「发布新公告」+ 右侧优先级三选一
        std::unique_ptr<DuiHBox> head(new DuiHBox());
        head->SetGap(6);
        head->AddChild(MakeText(_T("发布新公告"), 14, kGmFontSemiBold, kColorName, DT_LEFT),
                       Hint().Weight(1).AlignC(DuiLayout::AlignCenter));
        head->AddChild(MakeText(_T("优先级"), 12, kGmFontNormal, kColorSubtitle, DT_RIGHT),
                       Hint().Fixed(48).AlignC(DuiLayout::AlignCenter));

        //三个 chip 用普通按钮做，选中的那个用主色、其余用次要色 —— 与群文件面板
        //的分类筛选 chips 同一套做法，不必为此新造一个单选控件
        const int chipIds[3] =
        {
            kIdChipSeverityNormal, kIdChipSeverityImportant, kIdChipSeverityUrgent
        };
        LPCTSTR chipLabels[3] = { _T("通知"), _T("重要"), _T("紧急") };
        for (int i = 0; i < 3; ++i)
        {
            const int style = (i == model.m_draftSeverity) ? kGmBtnPrimary : kGmBtnSecondary;
            head->AddChild(MakeButton(chipLabels[i], style, chipIds[i], 12),
                           Hint().Fixed(kAnnChipW, kAnnChipH).AlignC(DuiLayout::AlignCenter));
        }
        card->AddChild(std::move(head), Hint().Fixed(26));

        // 标题（必填）
        std::unique_ptr<DuiEditHost> titleEdit(new DuiEditHost());
        titleEdit->SetCtrlId(kIdEditAnnounceTitle);
        titleEdit->SetShowBorder(true);
        titleEdit->SetPlaceholder(_T("公告标题（必填）"));
        card->AddChild(std::unique_ptr<DuiControl>(titleEdit.release()),
                       Hint().Fixed(kAnnFormTitleH));

        // 正文（多行）
        std::unique_ptr<DuiEditHost> bodyEdit(new DuiEditHost());
        bodyEdit->SetCtrlId(kIdEditAnnounceBody);
        bodyEdit->SetMultiLine(true);
        bodyEdit->SetWordWrap(true);
        bodyEdit->SetShowBorder(true);
        bodyEdit->SetPlaceholder(_T("公告内容（支持换行，Shift+Enter 换行）"));
        card->AddChild(std::unique_ptr<DuiControl>(bodyEdit.release()),
                       Hint().Fixed(kAnnFormBodyH));

        // 底部：清空 / 发布公告（右对齐）
        std::unique_ptr<DuiHBox> btnRow(new DuiHBox());
        btnRow->SetGap(10);
        btnRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
        btnRow->AddChild(MakeButton(_T("清空"), kGmBtnSecondary, kIdBtnAnnounceClear, 13),
                         Hint().Fixed(76, kAnnFormBtnH));
        btnRow->AddChild(MakeButton(_T("发布公告"), kGmBtnPrimary, kIdBtnAnnouncePublish, 13),
                         Hint().Fixed(110, kAnnFormBtnH));
        card->AddChild(std::move(btnRow), Hint().Fixed(kAnnFormBtnH));

        //卡片高度 = 内边距 + 各行 + 行间距（4 行 3 个 gap）
        const int cardH = 12 * 2 + 26 + kAnnFormTitleH + kAnnFormBodyH + kAnnFormBtnH + 10 * 3;
        page->AddChild(std::move(card), Hint().Fixed(cardH).Margin(0, 6, 0, 0));
    }

    // ---- 历史公告列表 ----
    if (!model.m_announcementsLoaded)
    {
        //还没拉回来 —— 与"拉过但一条都没有"要分开说，否则用户会以为公告全没了
        page->AddChild(MakeText(_T("正在加载公告…"), 13, kGmFontNormal,
                                kColorSubtitle, DT_LEFT),
                       Hint().Fixed(40).Margin(0, 14, 0, 0));
    }
    else if (model.m_announcements.empty())
    {
        page->AddChild(MakeText(_T("还没有发布过公告。在上面写一条，全体成员都会收到提醒。"),
                                13, kGmFontNormal, kColorSubtitle, DT_LEFT),
                       Hint().Fixed(40).Margin(0, 14, 0, 0));
    }
    else
    {
        page->AddChild(MakeText(_T("历史公告"), 12, kGmFontNormal, kColorSubtitle, DT_LEFT),
                       Hint().Fixed(22).Margin(0, 12, 0, 0));

        //本页的条目。id 用本页内的下标算 —— 翻页后同一个 id 指向的是另一条，
        //这没关系：按钮点下去时读的是当时那一页的模型。
        for (size_t i = 0; i < model.m_announcements.size(); ++i)
        {
            page->AddChild(MakeAnnouncementRow(model.m_announcements[i], (int)i),
                           Hint().Fixed(kAnnRowH).Margin(0, 0, 0, kAnnRowGap));
        }

        //底部分页栏：「共 N 条 · 第 x/y 页」+ 上一页 / 下一页。与群文件面板同一套
        //交互，公告攒到几百条也只在这一页里翻，不会把控件树撑爆。
        const int pageCount = model.AnnouncementPageCount();
        std::unique_ptr<DuiHBox> pager(new DuiHBox());
        pager->SetGap(8);

        CString pageText;
        pageText.Format(_T("共 %d 条 · 第 %d/%d 页"),
                        model.m_announcementTotal, model.m_announcementPage + 1, pageCount);
        pager->AddChild(MakeText(pageText, 12, kGmFontNormal, kColorSubtitle, DT_LEFT),
                        Hint().Weight(1).AlignC(DuiLayout::AlignCenter));

        //已经在第一页 / 最后一页时那个方向的按钮不建 —— 建出来点了没反应，
        //不如不给。只有一页时两个都不出现。
        if (model.m_announcementPage > 0)
        {
            pager->AddChild(MakeButton(_T("‹"), kGmBtnSecondary, kIdAnnouncePagePrev, 12),
                            Hint().Fixed(28, 26).AlignC(DuiLayout::AlignCenter));
        }
        if (model.m_announcementPage + 1 < pageCount)
        {
            pager->AddChild(MakeButton(_T("›"), kGmBtnSecondary, kIdAnnouncePageNext, 12),
                            Hint().Fixed(28, 26).AlignC(DuiLayout::AlignCenter));
        }

        page->AddChild(std::move(pager), Hint().Fixed(30).Margin(0, 4, 0, 0));
    }

    return std::unique_ptr<DuiControl>(page.release());
}

std::unique_ptr<DuiControl> BuildSensitiveWordsPage(const GroupManageModel& model)
{
    std::unique_ptr<DuiVBox> page(new DuiVBox());
    page->SetPadding(24, 16, 24, 18);
    page->SetGap(8);

    // 标题行：返回按钮 + 页面标题。二级界面必须给回去的路 —— 这一页不在左侧
    // 导航里，没有返回按钮就只能靠切到别的页再切回来。
    {
        std::unique_ptr<DuiHBox> titleRow(new DuiHBox());
        titleRow->SetGap(10);
        titleRow->AddChild(MakeButton(_T("‹ 返回"), kGmBtnSecondary,
                                      kIdBtnSensitiveBack, 12),
                           Hint().Fixed(64, 26).AlignC(DuiLayout::AlignCenter));
        Hint titleHint;
        titleHint.weight = 1;
        titleHint.fixedCross = 26;
        titleHint.alignCross = DuiLayout::AlignCenter;
        titleRow->AddChild(MakeText(_T("管理词库"), 16, kGmFontBold, kColorPageTitle, DT_LEFT),
                           titleHint);
        page->AddChild(std::move(titleRow), Hint().Fixed(28));
    }

    page->AddChild(MakeText(_T("命中词库的消息将被拒绝发送；匹配为子串、不区分大小写"),
                            12, kGmFontNormal, kColorSubtitle, DT_LEFT),
                   Hint().Fixed(22));

    // 新增行：输入框 + 添加按钮。
    {
        std::unique_ptr<DuiHBox> addRow(new DuiHBox());
        addRow->SetGap(10);

        std::unique_ptr<DuiEditHost> edit(new DuiEditHost());
        edit->SetCtrlId(kIdEditSensitiveNew);
        edit->SetShowBorder(true);
        edit->SetPlaceholder(_T("输入要屏蔽的词"));
        addRow->AddChild(std::unique_ptr<DuiControl>(edit.release()),
                         Hint().Weight(1).Fixed(0, kWordAddRowH));

        addRow->AddChild(MakeButton(_T("添加"), kGmBtnPrimary, kIdBtnSensitiveAdd, 13),
                         Hint().Fixed(76, kWordAddRowH));

        page->AddChild(std::move(addRow), Hint().Fixed(kWordAddRowH));
    }

    // 词条列表 / 空状态。
    if (!model.m_sensitiveWordsLoaded)
    {
        // 还没拉回来 —— 与"拉过但是空的"要分开说，否则用户会以为词全没了
        page->AddChild(MakeText(_T("正在加载词库…"), 13, kGmFontNormal,
                                kColorSubtitle, DT_LEFT),
                       Hint().Fixed(40).Margin(0, 12, 0, 0));
    }
    else if (model.m_sensitiveWords.empty())
    {
        page->AddChild(MakeText(_T("还没有敏感词。在上面的输入框里添加第一个。"),
                                13, kGmFontNormal, kColorSubtitle, DT_LEFT),
                       Hint().Fixed(40).Margin(0, 12, 0, 0));
    }
    else
    {
        CString countText;
        countText.Format(_T("共 %d 个词"), (int)model.m_sensitiveWords.size());
        page->AddChild(MakeText(countText, 12, kGmFontNormal, kColorSubtitle, DT_LEFT),
                       Hint().Fixed(22).Margin(0, 6, 0, 0));

        for (size_t i = 0; i < model.m_sensitiveWords.size(); ++i)
        {
            page->AddChild(MakeSensitiveWordRow(model.m_sensitiveWords[i], (int)i),
                           Hint().Fixed(kWordRowH).Margin(0, 0, 0, kWordRowGap));
        }
    }

    return std::unique_ptr<DuiControl>(page.release());
}

std::unique_ptr<DuiControl> BuildPermsPage(const GroupManageModel& model)
{
    std::unique_ptr<DuiVBox> page(new DuiVBox());
    page->SetPadding(24, 16, 24, 18);
    page->SetGap(8);

    page->AddChild(MakeText(_T("群权限"), 16, kGmFontBold, kColorPageTitle, DT_LEFT),
                   Hint().Fixed(28));
    page->AddChild(MakeText(_T("管理成员可做的操作及消息权限"), 12,
                            kGmFontNormal, kColorSubtitle, DT_LEFT),
                   Hint().Fixed(22));

    std::unique_ptr<DuiVBox> card = MakeCard(kColorWhite, kCardBorder, kCardCorner);
    card->SetPadding(0, 6, 0, 6);
    card->SetGap(0);

    // 加群方式（下拉）。
    std::unique_ptr<DuiComboBox> joinCombo(new DuiComboBox());
    joinCombo->SetCtrlId(kIdComboJoinMethod);
    joinCombo->AddString(_T("需要验证消息"));
    joinCombo->AddString(_T("允许任何人加入"));
    joinCombo->AddString(_T("禁止任何人加入"));
    joinCombo->SetCurSel(model.m_perms.joinMethodIndex);
    card->AddChild(MakePermRow(_T("加群方式"), _T("控制哪些人可以加入本群"),
                               std::unique_ptr<DuiControl>(joinCombo.release()), 150,
                               std::unique_ptr<DuiControl>(), 0),
                   Hint().Fixed(52));
    card->AddChild(MakeSeparatorRow(), Hint().Fixed(1).Margin(18, 0, 18, 0));

    // 谁能 @ 全体成员（下拉）。
    std::unique_ptr<DuiComboBox> atCombo(new DuiComboBox());
    atCombo->SetCtrlId(kIdComboAtAllScope);
    atCombo->AddString(_T("仅群主和管理员"));
    atCombo->AddString(_T("所有人"));
    atCombo->SetCurSel(model.m_perms.atAllScopeIndex);
    card->AddChild(MakePermRow(_T("谁能 @ 全体成员"), _T("控制谁可以发起全体提醒"),
                               std::unique_ptr<DuiControl>(atCombo.release()), 150,
                               std::unique_ptr<DuiControl>(), 0),
                   Hint().Fixed(52));
    card->AddChild(MakeSeparatorRow(), Hint().Fixed(1).Margin(18, 0, 18, 0));

    // 新成员查看历史消息（开关）。
    card->AddChild(MakePermRow(_T("新成员查看历史消息"),
                               _T("新加入的成员是否可以查看入群前的聊天记录"),
                               MakePermSwitch(kIdSwitchHistory,
                                              model.m_perms.newMemberSeeHistory),
                               46, std::unique_ptr<DuiControl>(), 0),
                   Hint().Fixed(52));
    card->AddChild(MakeSeparatorRow(), Hint().Fixed(1).Margin(18, 0, 18, 0));

    // 成员邀请好友加群（开关）。
    card->AddChild(MakePermRow(_T("成员邀请好友加群"),
                               _T("允许普通成员邀请好友，无需管理员审批"),
                               MakePermSwitch(kIdSwitchInvite,
                                              model.m_perms.memberInviteFriend),
                               46, std::unique_ptr<DuiControl>(), 0),
                   Hint().Fixed(52));
    card->AddChild(MakeSeparatorRow(), Hint().Fixed(1).Margin(18, 0, 18, 0));

    // 入群欢迎语（开关）。
    card->AddChild(MakePermRow(_T("入群欢迎语"),
                               _T("新成员加入时由机器人自动发送"),
                               MakePermSwitch(kIdSwitchWelcome,
                                              model.m_perms.welcomeMessage),
                               46, std::unique_ptr<DuiControl>(), 0),
                   Hint().Fixed(52));
    card->AddChild(MakeSeparatorRow(), Hint().Fixed(1).Margin(18, 0, 18, 0));

    // 消息敏感词过滤（开关 + 管理词库）。
    card->AddChild(MakePermRow(_T("消息敏感词过滤"),
                               _T("自动屏蔽设置的违规词"),
                               MakePermSwitch(kIdSwitchSensitive,
                                              model.m_perms.sensitiveWordFilter),
                               46,
                               MakeButton(_T("管理词库"), kGmBtnSecondary,
                                          kIdBtnSensitiveDict, 12),
                               76),
                   Hint().Fixed(52));

    // 卡片高度 = 6 行 * 52 + 5 条分隔 + 上下 padding 12。
    page->AddChild(std::move(card), Hint().Fixed(6 * 52 + 5 * 1 + 12));

    return std::unique_ptr<DuiControl>(page.release());
}

// =====================================================================
// 危险区域页（design/群管理6.png）
// =====================================================================

// 造一张危险操作卡：左侧标题 + 说明，右侧操作按钮。
std::unique_ptr<DuiControl> MakeDangerCard(LPCTSTR title, LPCTSTR desc,
                                           LPCTSTR btnText, int btnStyle,
                                           UINT btnId, int btnWidth)
{
    std::unique_ptr<DuiVBox> card = MakeCard(kDangerCardBg, kDangerCardBorder, kCardCorner);
    card->SetPadding(18, 0, 18, 0);

    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(14);

    std::unique_ptr<DuiVBox> info(new DuiVBox());
    info->SetGap(4);
    info->AddChild(MakeText(title, 14, kGmFontSemiBold, kColorName, DT_LEFT),
                   Hint().Fixed(22));
    info->AddChild(MakeMultiText(desc, 12, kGmFontNormal, kColorSubtitle),
                   Hint().Fixed(36));
    Hint infoHint;
    infoHint.weight     = 1;
    infoHint.fixedCross = 60;
    infoHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(info), infoHint);

    row->AddChild(MakeButton(btnText, btnStyle, btnId, 13),
                  Hint().Fixed(btnWidth, 36).AlignC(DuiLayout::AlignCenter));

    card->AddChild(std::move(row));
    return std::unique_ptr<DuiControl>(card.release());
}

std::unique_ptr<DuiControl> BuildDangerPage(const GroupManageModel& /*model*/)
{
    std::unique_ptr<DuiVBox> page(new DuiVBox());
    page->SetPadding(24, 16, 24, 18);
    page->SetGap(14);

    page->AddChild(MakeText(_T("危险区域"), 16, kGmFontBold, kColorDanger, DT_LEFT),
                   Hint().Fixed(28));
    page->AddChild(MakeText(_T("以下操作不可撤销，请谨慎操作"), 12,
                            kGmFontNormal, kColorSubtitle, DT_LEFT),
                   Hint().Fixed(22));

    page->AddChild(MakeDangerCard(_T("转让群主"),
                                  _T("将群主身份转让给其他成员，转让后你将成为普通成员"),
                                  _T("转让群主"), kGmBtnSecondary, kIdBtnTransfer, 96),
                   Hint().Fixed(78));
    page->AddChild(MakeDangerCard(_T("移除全部成员"),
                                  _T("移除除群主以外的所有成员，群聊仍然保留"),
                                  _T("移除全部成员"), kGmBtnDanger, kIdBtnRemoveAll, 116),
                   Hint().Fixed(78));
    page->AddChild(MakeDangerCard(_T("解散群聊"),
                                  _T("解散后本群的聊天记录将全部删除，所有成员将被移出，此操作不可恢复"),
                                  _T("解散群聊"), kGmBtnDanger, kIdBtnDismiss, 96),
                   Hint().Fixed(86));

    return std::unique_ptr<DuiControl>(page.release());
}

} // anonymous namespace

// =====================================================================
// Header
// =====================================================================

std::unique_ptr<DuiControl> BuildHeader(const GroupManageModel& model)
{
    std::unique_ptr<DuiHBox> header(new DuiHBox());
    header->SetPadding(22, 0, 20, 0);
    header->SetGap(12);

    const int kHeaderAvatarPx     = 32;   //顶部 Header 里的群头像边长
    const int kHeaderAvatarCorner = 8;    //其圆角半径
    header->AddChild(MakeGroupAvatar(model, kHeaderAvatarPx, kHeaderAvatarCorner),
                     Hint().Fixed(kHeaderAvatarPx, kHeaderAvatarPx)
                           .AlignC(DuiLayout::AlignCenter));

    header->AddChild(MakeText(model.m_groupName, 16, kGmFontBold, kColorPageTitle, DT_LEFT),
                     Hint().Fixed(130).AlignC(DuiLayout::AlignCenter));

    LPCTSTR roleText = _T("我是成员");
    if (model.m_myRole == kRoleOwner)
    {
        roleText = _T("我是群主");
    }
    else if (model.m_myRole == kRoleAdmin)
    {
        roleText = _T("我是管理员");
    }
    CString sub;
    sub.Format(_T("%s · %d 名成员"), roleText, model.MemberCount());
    header->AddChild(MakeText(sub, 12, kGmFontNormal, kColorSubtitle, DT_LEFT),
                     Hint().Weight(1).AlignC(DuiLayout::AlignCenter));

    return std::unique_ptr<DuiControl>(header.release());
}

// =====================================================================
// 左侧导航
// =====================================================================

namespace {

// 造一个导航分组标题（灰色小标题，如「成员」「设置」）。
std::unique_ptr<DuiControl> MakeNavGroupTitle(LPCTSTR text)
{
    return std::unique_ptr<DuiControl>(
        MakeText(text, 12, kGmFontNormal, RGB(0xAE, 0xAC, 0xA6), DT_LEFT).release());
}

// 造一个导航项。
std::unique_ptr<DuiControl> MakeNavItem(int pageId, int iconKind, LPCTSTR label,
                                        bool danger, int count, bool alert)
{
    std::unique_ptr<GroupManageNavItem> item(new GroupManageNavItem());
    item->SetCtrlId(pageId);
    item->SetNavInfo(iconKind, label, danger);
    item->SetBadge(count, alert);
    return std::unique_ptr<DuiControl>(item.release());
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildNav(const GroupManageModel& model)
{
    std::unique_ptr<DuiVBox> nav(new DuiVBox());
    nav->SetBgColor(kColorWhite);
    nav->SetPadding(12, 14, 12, 14);
    nav->SetGap(2);

    // 按登录者角色决定哪些页出现在导航里：普通成员看不到审批 / 禁言 / 权限，
    // 管理员看不到危险区域。不可见的页整项不构建 —— 灰着显示只会让人反复去点。
    const int myRole = model.m_myRole;

    // 分组：成员。成员管理页对所有角色都可见，故本分组标题恒显示。
    nav->AddChild(MakeNavGroupTitle(_T("成员")), Hint().Fixed(22));
    nav->AddChild(MakeNavItem(kPageMembers, kGmIconMembers, _T("成员管理"),
                              false, model.MemberCount(), false),
                  Hint().Fixed(36));
    if (IsPageVisibleForRole(kPageApproval, myRole))
    {
        nav->AddChild(MakeNavItem(kPageApproval, kGmIconApproval, _T("加群审批"),
                                  false, model.PendingRequestCount(), true),
                      Hint().Fixed(36));
    }
    if (IsPageVisibleForRole(kPageMute, myRole))
    {
        nav->AddChild(MakeNavItem(kPageMute, kGmIconMute, _T("禁言管理"),
                                  false, model.MutedCount(), false),
                      Hint().Fixed(36));
    }

    // 分组：群公告。对照 design/群管理7.png，它在导航里自成一组（而不是并进
    // 「设置」组）—— 公告是日常要反复用的功能，跟"改群名""设权限"这类一次性
    // 配置不是一回事。整组仅群主 / 管理员可见，故组标题也跟着一起隐藏。
    if (IsPageVisibleForRole(kPageAnnouncement, myRole))
    {
        nav->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(10));
        nav->AddChild(MakeNavGroupTitle(_T("群公告")), Hint().Fixed(22));
        //徽章数用总条数（服务端给的 total），不是本页条数 —— 分页后本地只留一页
        nav->AddChild(MakeNavItem(kPageAnnouncement, kGmIconAnnounce, _T("群公告"),
                                  false, model.m_announcementTotal, false),
                      Hint().Fixed(36));
    }

    // 间隔。
    nav->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(10));

    // 分组：设置。基本信息对所有角色可见，故本分组标题同样恒显示。
    nav->AddChild(MakeNavGroupTitle(_T("设置")), Hint().Fixed(22));
    nav->AddChild(MakeNavItem(kPageBasicInfo, kGmIconInfo, _T("基本信息"),
                              false, 0, false),
                  Hint().Fixed(36));
    if (IsPageVisibleForRole(kPagePerms, myRole))
    {
        nav->AddChild(MakeNavItem(kPagePerms, kGmIconLock, _T("群权限"),
                                  false, 0, false),
                      Hint().Fixed(36));
    }
    if (IsPageVisibleForRole(kPageDanger, myRole))
    {
        nav->AddChild(MakeNavItem(kPageDanger, kGmIconDanger, _T("危险区域"),
                                  true, 0, false),
                      Hint().Fixed(36));
    }

    // 余下空间。
    nav->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));

    return std::unique_ptr<DuiControl>(nav.release());
}

// =====================================================================
// 页面分发
// =====================================================================

std::unique_ptr<DuiControl> BuildPage(int pageId, const GroupManageModel& model)
{
    switch (pageId)
    {
    case kPageMembers:
        return BuildMembersPage(model);
    case kPageApproval:
        return BuildApprovalPage(model);
    case kPageMute:
        return BuildMutePage(model);
    case kPageBasicInfo:
        return BuildBasicInfoPage(model);
    case kPagePerms:
        return BuildPermsPage(model);
    case kPageDanger:
        return BuildDangerPage(model);
    case kPageSensitive:
        return BuildSensitiveWordsPage(model);
    case kPageAnnouncement:
        return BuildAnnouncementPage(model);
    default:
        break;
    }

    // 未知页：返回一个空白容器，避免空指针。
    return std::unique_ptr<DuiControl>(new DuiControl());
}

} // namespace balloongroupmanage
