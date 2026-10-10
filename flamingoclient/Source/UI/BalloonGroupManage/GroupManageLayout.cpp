/**
 *  GroupManageLayout.cpp
 *
 *  群管理对话框客户区控件树的程序化构建实现。Header / 左侧导航 / 右侧各
 *  页内容全部用 balloonui 现成控件（DuiVBox 卡片底、DuiAvatar、DuiBadge、
 *  DuiSwitch、DuiComboBox、DuiSearchBox、DuiEdit、DuiSeparator）加本
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

#include "GroupManageLayout.h"
#include "GroupManageModel.h"
#include "GroupManageMapping.h"   // IsPageVisibleForRole：按角色决定导航里出现哪些页
#include "../../Session/GroupRoleRules.h"    // CanChangeGroupAvatar：谁能改群头像（与窗口层同一份规则）
#include "../BalloonMain/AvatarPickerTile.h"   // 承载群头像真图（自持位图，随控件树释放）
#include "../BalloonMain/AvatarImageUtil.h"    // MakeRoundedAvatarFromFile：把本地图裁成圆角方形
#include "GroupManageNavItem.h"
#include "GroupManageButton.h"
#include "GroupManagePaint.h"
#include "GroupManageIcons.h"

#include "../../../third-party/balloonui/balloonui/DuiDpi.h"   // Scale：窗口尺寸按 DPI 换算
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiBadge.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSearchBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../BalloonChatWnd/GroupNotifyOnly.h"   // 「仅通知模式」的标题与说明文字（docs/群管理接入方案.md §14）
#include "../BalloonChatWnd/GroupMustRead.h"     // 「入群必读」「未确认必读」的文字（docs/群公告接入方案.md §14）
#include "../BalloonChatWnd/GroupBotSection.h"   // BuildGroupBotRow：群机器人页的每一行与成员页签的机器人行同一样式
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // 「仅通知模式」一行的悬停提示

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
using balloonwjui::DuiEdit;
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
const COLORREF kColorDanger     = RGB(0xEF, 0x44, 0x44);   // 危险红（与导航危险项同色，取自设计稿）

const COLORREF kCardBg     = RGB(0xFA, 0xF9, 0xF6);   // 普通卡片底
const COLORREF kCardBorder = RGB(0xEC, 0xEA, 0xE6);   // 普通卡片描边
// 危险区域页的两种卡片（取自 design/群管理6.png）：不可逆操作用淡红卡片，
// 转让群主这类可挽回的操作用中性浅灰卡片，不让整页都是红色。
const COLORREF kDangerCardBg      = RGB(0xFE, 0xF8, 0xF8);   // 危险卡片底（淡红）
const COLORREF kDangerCardBorder  = RGB(0xFC, 0xDD, 0xDD);   // 危险卡片描边
const COLORREF kNeutralCardBg     = RGB(0xF5, 0xF4, 0xF1);   // 中性卡片底（浅灰）
const COLORREF kNeutralCardBorder = RGB(0xE4, 0xE3, 0xE0);   // 中性卡片描边
const COLORREF kSeparator  = RGB(0xEC, 0xEA, 0xE6);   // 分隔线

// 左侧导航的底色与分组标题色（取自 design/群管理8.png）。导航用浅灰底，右侧内容
// 区是白底，两边靠底色区分层次；选中项的白色卡片浮在这层浅灰上。
const COLORREF kNavBg              = RGB(0xFA, 0xFA, 0xF9);
const COLORREF kNavGroupTitleColor = RGB(0x9A, 0x9A, 0x9A);

// 分组标题前的圆点色（按角色）。
const COLORREF kDotOwner  = RGB(0xFF, 0x7A, 0x45);
const COLORREF kDotAdmin  = RGB(0x45, 0x6C, 0xDF);
const COLORREF kDotMember = RGB(0xB4, 0xB4, 0xB0);

// 分隔线粗细（像素）。恒为 1px 细线，不经 GmScale 换算 —— 高 DPI 下随之加粗
// 反而显得笨重。
const int kSeparatorPx  = 1;

const int kCardCorner   = 10;   // 卡片圆角
const int kMemberCardH  = 58;   // 成员卡高度
const int kGridGap      = 10;   // 成员网格行列间距
const int kMemberCols   = 3;    // 成员网格列数

// 字号不在本文件里写像素值：MakeText / MakeMultiText / MakeButton 只收
// GmTextStyle，字号与字重统一取自 GmTextStyleSpec（GroupManagePaint.h）。

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

    // 当前显示的文字（CollectPageTexts 读取，供单元测试核对页面内容）。
    const CString& GetText() const
    {
        return m_text;
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
        // 圆点半径、圆点左缩进、圆点与文字的间距（96 DPI 下的像素，经 GmScale 换算）
        const int kDotRadius  = 4;
        const int kDotIndent  = 2;
        const int kDotTextGap = 8;

        const int cy   = (m_rcItem.top + m_rcItem.bottom) / 2;
        const int dotR = GmScale(kDotRadius);
        const int dotX = m_rcItem.left + GmScale(kDotIndent);

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(m_dot),
                                                 GetGValue(m_dot), GetBValue(m_dot)));
        g.FillEllipse(&brush,
                      Gdiplus::RectF((Gdiplus::REAL)dotX, (Gdiplus::REAL)(cy - dotR),
                                     (Gdiplus::REAL)(dotR * 2), (Gdiplus::REAL)(dotR * 2)));

        RECT tr = { dotX + dotR * 2 + GmScale(kDotTextGap), m_rcItem.top,
                    m_rcItem.right, m_rcItem.bottom };
        const GmTextSpec spec = GmTextStyleSpec(kGmTextBody);
        GmDrawText(hdc, (LPCWSTR)m_text, tr, spec.m_px, spec.m_weight, kColorSubtitle,
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

        //描边宽度取 2.0，与左侧导航各图标一致。单位是图标 24 格视图坐标，已随图标
        //边长缩放，不再按 DPI 换算
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
//   style：文字样式，字号与字重取自 GmTextStyleSpec。参数类型是枚举而不是 int，
//          传像素值会编译不过，避免有人绕开字号表另写一个数。
std::unique_ptr<GmText> MakeText(LPCTSTR text, GmTextStyle style,
                                 COLORREF color, UINT align)
{
    const GmTextSpec spec = GmTextStyleSpec(style);
    std::unique_ptr<GmText> t(new GmText());
    t->Configure(text, spec.m_px, spec.m_weight, color, align, false);
    return t;
}

// 造一个多行文本控件。style 的含义同 MakeText。
std::unique_ptr<GmText> MakeMultiText(LPCTSTR text, GmTextStyle style,
                                      COLORREF color)
{
    const GmTextSpec spec = GmTextStyleSpec(style);
    std::unique_ptr<GmText> t(new GmText());
    t->Configure(text, spec.m_px, spec.m_weight, color, DT_LEFT, true);
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
//   corner：96 DPI 下的圆角半径（像素），内部经 GmScale 换算。
// 描边恒为 1px 细线，不随 DPI 加粗。
std::unique_ptr<DuiVBox> MakeCard(COLORREF bg, COLORREF border, int corner)
{
    std::unique_ptr<DuiVBox> card(new DuiVBox());
    card->SetBgColor(bg);
    card->SetBorderColor(border);
    card->SetBorderWidth(1.0f);
    card->SetCornerRadius(GmScale(corner));
    return card;
}

// 造一个操作按钮。
//   style：    按钮风格，取值见 GmButtonStyle；
//   textStyle：文字样式，只取其字号（字重由按钮风格决定）。普通按钮用 kGmTextBody，
//              小按钮用 kGmTextCaption。
std::unique_ptr<GroupManageButton> MakeButton(LPCTSTR text, int style,
                                              UINT ctrlId, GmTextStyle textStyle)
{
    std::unique_ptr<GroupManageButton> btn(new GroupManageButton());
    btn->SetText(text);
    btn->SetButtonStyle(style);
    btn->SetTextPx(GmTextStyleSpec(textStyle).m_px);
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
//   corner：96 DPI 下的圆角半径（像素），内部经 GmScale 换算。
std::unique_ptr<DuiAvatar> MakeAvatar(LPCTSTR text, COLORREF color,
                                      int corner, int online)
{
    std::unique_ptr<DuiAvatar> av(new DuiAvatar());
    av->SetName(text);
    av->SetShape(DuiAvatar::ShapeRoundRect);
    av->SetCornerRadius(GmScale(corner));
    av->SetFallbackBgColor(color);
    av->SetStatus((DuiAvatar::Status)AvatarStatusFromOnline(online));
    return av;
}

// 造群头像控件：真图已下到本地就画真图，否则画字形 + 底色。
//   model：  数据来源（真图路径 / 字形 / 底色）。
//   sizePx： 96 DPI 下的边长（像素），生成真图位图时按它换算后的尺寸缩放。
//   corner： 96 DPI 下的圆角半径（像素）。
// 真图用 AvatarPickerTile 承载而不是 DuiAvatar：后者的 SetBitmap 只借用位图、
// 由调用方负责释放，而布局层是一串自由函数，没有地方存这个 HBITMAP；前者的
// SetOwnedBitmap 自持并在析构时释放，位图生命周期随控件树走，不会漏。
std::unique_ptr<DuiAvatar> MakeGroupAvatar(const GroupManageModel& model, int sizePx, int corner)
{
    if (!model.m_groupAvatarImagePath.IsEmpty())
    {
        //位图按换算后的物理尺寸生成，高 DPI 下才不会被放大发虚
        HBITMAP hbm = balloonmain::MakeRoundedAvatarFromFile(
            model.m_groupAvatarImagePath, GmScale(sizePx), GmScale(corner));
        if (hbm != NULL)
        {
            std::unique_ptr<balloonmain::AvatarPickerTile> tile(
                new balloonmain::AvatarPickerTile());
            tile->SetShape(DuiAvatar::ShapeRoundRect);
            tile->SetCornerRadius(GmScale(corner));
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
    //徽章是单个人的角色，三种角色都用带上下文的 TRC：与导航分组标题等处的同一原文区分（如「成员」英文取单数），
    //且成员卡上的徽章较窄，译文可取更短的写法（多语言第 4 期）
    LPCTSTR  label = TRC(L"群成员角色", L"成员");
    COLORREF bg    = RGB(0xEF, 0xEE, 0xEA);
    COLORREF fg    = RGB(0x9A, 0x98, 0x93);

    if (role == kRoleOwner)
    {
        label = TRC(L"群成员角色", L"群主");
        bg    = RGB(0xFD, 0xE9, 0xDD);
        fg    = RGB(0xE0, 0x6A, 0x2E);
    }
    else if (role == kRoleAdmin)
    {
        label = TRC(L"群成员角色", L"管理员");
        bg    = RGB(0xE6, 0xEC, 0xFB);
        fg    = RGB(0x45, 0x6C, 0xDF);
    }

    std::unique_ptr<DuiBadge> badge(new DuiBadge());
    badge->SetText(label);
    badge->SetMaxDisplayChars(0);    // 不截断（chip 用法）
    badge->SetCornerRadius(GmScale(4));
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

// ---- 页面框架：内容限宽 + 标题块 -----------------------------------------

// 限宽页的内容最大宽度（96 DPI 下的像素）。按设计稿实测，基本信息、群权限、危险区域
// 三页的内容宽约 650 / 625 / 600 设计稿像素，按窗口宽度比例 960/1125 折算为
// 555 / 533 / 512，统一取 560。成员管理、加群审批、禁言管理、群公告四页铺满。
const int kMaxContentWidth = 560;

// 页面标题块（96 DPI 下的像素）：标题行高、副标题行高、两行之间的间距，以及标题块
// 与下方正文之间统一留出的距离。设计稿里标题与副标题几乎贴在一起，与正文则隔开一段。
const int kPageTitleHeight      = 24;
const int kPageSubtitleHeight   = 18;
const int kPageTitleGap         = 4;
const int kPageTitleBlockBottom = 16;

/**
 *  限宽的页面容器：可用宽度超过「最大内容宽度 + 左右内边距」时，只按这个宽度靠左
 *  排版，右侧留白；可用宽度不足时照常铺满。与 DuiVBox 只差 Layout 里这一步。
 */
class GmMaxWidthBox : public DuiVBox
{
public:
    // maxContentWidth：内容区（不含左右内边距）的最大宽度，当前 DPI 下的像素。
    explicit GmMaxWidthBox(int maxContentWidth)
        : m_maxContentWidth(maxContentWidth)
    {
    }

    void Layout(const RECT& rcAvail) override
    {
        RECT rc = rcAvail;
        const int maxOuterWidth = m_maxContentWidth + m_padL + m_padR;
        if (rc.right - rc.left > maxOuterWidth)
        {
            rc.right = rc.left + maxOuterWidth;
        }
        DuiVBox::Layout(rc);
    }

private:
    int m_maxContentWidth;   // 内容区最大宽度（当前 DPI 下的像素，不含内边距）
};

// 造一个限宽页面容器，内边距与间距由调用方设置。
std::unique_ptr<DuiVBox> MakeWidthLimitedPage()
{
    return std::unique_ptr<DuiVBox>(new GmMaxWidthBox(GmScale(kMaxContentWidth)));
}

/**
 *  造页面标题块：标题 + 可选的副标题，两行紧挨着。标题与副标题分别带
 *  kIdPageTitle / kIdPageSubtitle。
 *    title：     标题文字；
 *    titleColor：标题颜色（危险区域用红色，其余用标题色）；
 *    subtitle：  副标题文字，NULL 或空串表示没有副标题。
 *  @return 标题块；高度见 PageTitleBlockHeight。
 */
std::unique_ptr<DuiVBox> MakePageTitleBlock(LPCTSTR title, COLORREF titleColor,
                                            LPCTSTR subtitle)
{
    std::unique_ptr<DuiVBox> block(new DuiVBox());
    block->SetGap(GmScale(kPageTitleGap));

    std::unique_ptr<GmText> titleText = MakeText(title, kGmTextPageTitle, titleColor, DT_LEFT);
    titleText->SetCtrlId(kIdPageTitle);
    block->AddChild(std::unique_ptr<DuiControl>(titleText.release()),
                    Hint().Fixed(GmScale(kPageTitleHeight)));

    if (subtitle != NULL && subtitle[0] != _T('\0'))
    {
        std::unique_ptr<GmText> sub = MakeText(subtitle, kGmTextBody, kColorSubtitle, DT_LEFT);
        sub->SetCtrlId(kIdPageSubtitle);
        block->AddChild(std::unique_ptr<DuiControl>(sub.release()),
                        Hint().Fixed(GmScale(kPageSubtitleHeight)));
    }
    return block;
}

// 标题块在当前 DPI 下的高度（像素）。各项分别换算后再相加，与块内逐项换算一致。
int PageTitleBlockHeight(bool hasSubtitle)
{
    int h = GmScale(kPageTitleHeight);
    if (hasSubtitle)
    {
        h += GmScale(kPageTitleGap) + GmScale(kPageSubtitleHeight);
    }
    return h;
}

/**
 *  把标题块放到页面最上方，并保证它与下方正文之间的距离为 kPageTitleBlockBottom：
 *  页面自身的间距不够时，用标题块的下边距补足。
 *    page：    页面容器（借用）；
 *    pageGap： 页面容器的子控件间距（96 DPI 下的像素，即调用方传给 SetGap 的原值）；
 *    其余参数：同 MakePageTitleBlock。
 */
// ---- 表单控件样式（取自 design/群管理4/5.png）-------------------------------

// 开关打开 / 关闭时的轨道色，下拉框箭头色。
const COLORREF kSwitchOnColor  = RGB(0xFF, 0x7A, 0x45);   // 品牌橙
const COLORREF kSwitchOffColor = RGB(0xD8, 0xD5, 0xD0);   // 暖灰
const COLORREF kComboArrowColor = RGB(0x66, 0x66, 0x66);

/**
 *  群管理窗样式的输入框：与 DuiEdit 只差描边色，按 GmInputBorderColor 取色
 *  （常态浅灰、焦点品牌橙），替换 DuiEdit 默认的中灰 / 蓝色描边。
 */
class GmEdit : public DuiEdit
{
protected:
    // DuiRichEdit 覆写：按群管理窗的配色返回当前状态下的描边色。
    COLORREF BorderColor() const override
    {
        return GmInputBorderColor(m_bEnabled, m_bFocused, m_bHover);
    }
};

/**
 *  群管理窗样式的搜索框：与 DuiSearchBox 只差描边色，取色同 GmEdit。
 */
class GmSearchBox : public DuiSearchBox
{
protected:
    // DuiRichEdit 覆写：按群管理窗的配色返回当前状态下的描边色。
    COLORREF BorderColor() const override
    {
        return GmInputBorderColor(m_bEnabled, m_bFocused, m_bHover);
    }
};

// 给开关套上群管理窗的配色。
void StyleSwitch(DuiSwitch* sw)
{
    sw->SetOnColor(kSwitchOnColor);
    sw->SetOffColor(kSwitchOffColor);
}

// 给下拉框套上群管理窗的配色：箭头灰色；边框与输入框同一套取色（常态浅灰，
// 悬停或浮层展开时品牌橙，对应输入框的焦点色）。
void StyleCombo(DuiComboBox* combo)
{
    combo->SetArrowColor(kComboArrowColor);
    combo->SetBorderColors(GmInputBorderColor(true, false, false),
                           GmInputBorderColor(true, true, false));
}

void AddPageTitleBlock(DuiVBox* page, int pageGap, LPCTSTR title, COLORREF titleColor,
                       LPCTSTR subtitle)
{
    const bool hasSubtitle = (subtitle != NULL && subtitle[0] != _T('\0'));
    const int  extra = (pageGap < kPageTitleBlockBottom) ? (kPageTitleBlockBottom - pageGap) : 0;
    page->AddChild(std::unique_ptr<DuiControl>(
                       MakePageTitleBlock(title, titleColor, subtitle).release()),
                   Hint().Fixed(PageTitleBlockHeight(hasSubtitle))
                         .Margin(0, 0, 0, GmScale(extra)));
}

// 加载提示行的高度（96 DPI 下的像素），与群公告页「正在加载公告…」那一行相同。
const int kLoadStatusHeight = 40;

/**
 *  页面数据尚未到齐时，往页面追加一行加载提示或失败提示。
 *
 *  群管理窗打开时成员与加群申请都还没拉回来，这时既不能显示空列表（看起来像
 *  "一个成员都没有"），更不能显示示例数据，只显示一行提示；应答到达后整页重建，
 *  提示行随之消失。
 *
 *    page：       页面容器（借用）；
 *    state：      本页数据的加载状态，取值见 LoadState；
 *    pendingText：数据尚未到达时的提示文字；
 *    failedText： 数据加载失败时的提示文字。
 *  @return 追加了提示行时返回 true，调用方不应再追加列表内容；数据已加载
 *          （kLoadDone）时什么都不做并返回 false。
 */
bool AppendLoadStatus(DuiVBox* page, LoadState state,
                      LPCTSTR pendingText, LPCTSTR failedText)
{
    if (state == kLoadDone)
    {
        return false;
    }

    const bool failed = (state == kLoadFailed);
    std::unique_ptr<GmText> text = MakeText(failed ? failedText : pendingText,
                                            kGmTextBody, kColorSubtitle, DT_LEFT);
    text->SetCtrlId(failed ? kIdLoadFailed : kIdLoadPending);
    page->AddChild(std::unique_ptr<DuiControl>(text.release()),
                   Hint().Fixed(GmScale(kLoadStatusHeight)));
    return true;
}

// =====================================================================
// 成员管理页（design/群管理1.png）
// =====================================================================

// 造一张成员卡：头像 + 昵称 / 职务 + 角色徽章。
//   m：       成员数据；
//   memberIndex：该成员在 model.m_members 里的下标，用来算卡片的 ctrl id ——
//               右键菜单靠它认出点的是哪一位。下标为负表示不需要右键（不设 id）。
// 「未确认必读」标记与「入群必读」徽章的文字色（蓝）
const COLORREF kColorMustReadMark = RGB(0x45, 0x6C, 0xDF);

std::unique_ptr<DuiControl> MakeMemberCard(const GroupMember& m, int memberIndex, bool showMustRead)
{
    //成员卡用 GmMemberCard 而不是普通卡片容器 —— 它整卡接管鼠标命中并把右键
    //报给宿主窗，否则右键会打在卡内的昵称 / 徽章上，菜单永远弹不出来。
    std::unique_ptr<GmMemberCard> card(new GmMemberCard());
    card->SetBgColor(kCardBg);
    card->SetBorderColor(kCardBorder);
    card->SetBorderWidth(1.0f);
    card->SetCornerRadius(GmScale(kCardCorner));
    card->SetPadding(GmScale(14), 0, GmScale(14), 0);
    if (memberIndex >= 0 && memberIndex <= (kIdMemberCardMax - kIdMemberCardBase))
    {
        card->SetCtrlId(kIdMemberCardBase + memberIndex);
    }

    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(GmScale(12));

    row->AddChild(MakeAvatar(m.avatarText, m.avatarColor, 8, m.online),
                  Hint().Fixed(GmScale(36), GmScale(36)).AlignC(DuiLayout::AlignCenter));

    std::unique_ptr<DuiVBox> info(new DuiVBox());
    info->SetGap(GmScale(2));
    info->AddChild(MakeText(m.name, kGmTextCardTitle, kColorName, DT_LEFT),
                   Hint().Fixed(GmScale(18)));
    //尚未确认入群必读的成员：副标题换成「未确认必读」（有职务时接在后面），只有群主与管理员看得到
    if (showMustRead && m.mustReadPending)
    {
        CString sub = balloonchatwnd::MustReadMemberMarkText();
        if (!m.title.IsEmpty())
        {
            sub += _T(" · ");
            sub += m.title;
        }
        info->AddChild(MakeText(sub, kGmTextCaption, kColorMustReadMark, DT_LEFT),
                       Hint().Fixed(GmScale(15)));
    }
    else
    {
        info->AddChild(MakeText(m.title, kGmTextCaption, kColorSubtitle, DT_LEFT),
                       Hint().Fixed(GmScale(15)));
    }
    Hint infoHint;
    infoHint.fixedCross = GmScale(35);
    infoHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(info), infoHint);

    //角色徽章：设计宽 52、高 20（96 DPI 下的像素）。非简体中文界面下文字比设计宽度宽时按文字宽度加宽，
    //文字左右各留 8 像素（多语言第 4 期）；简体中文界面保持设计宽度
    const int kCardRoleBadgeW = 52;
    const int kCardRoleBadgeH = 20;
    const int kCardRoleTextPad = 8;
    std::unique_ptr<DuiBadge> roleBadge = MakeRoleBadge(m.role);
    int roleBadgeW = GmScale(kCardRoleBadgeW);
    if (!i18n::IsSourceLanguage())
    {
        const int needW = GmMeasureTextWidth(roleBadge->GetText(), kGmTextCaption) + GmScale(kCardRoleTextPad) * 2;
        if (needW > roleBadgeW)
        {
            roleBadgeW = needW;
        }
    }
    row->AddChild(std::unique_ptr<DuiControl>(roleBadge.release()),
                  Hint().Fixed(roleBadgeW, GmScale(kCardRoleBadgeH)).AlignC(DuiLayout::AlignCenter));

    card->AddChild(std::move(row));
    return std::unique_ptr<DuiControl>(card.release());
}

// 造某一角色分组的成员网格（3 列，行数随成员数）。outRows 回传行数。
std::unique_ptr<DuiControl> MakeMemberGrid(const GroupManageModel& model,
                                           GroupRole role, int& outRows)
{
    std::unique_ptr<DuiGrid> grid(new DuiGrid());
    grid->SetGap(GmScale(kGridGap));

    //只收命中搜索关键词的成员；关键词为空时 MemberMatchesKeyword 恒为真，即全部成员
    const int count = model.CountByRoleMatching(role);
    const int rows = (count + kMemberCols - 1) / kMemberCols;
    outRows = rows;
    grid->SetGrid(rows, kMemberCols);

    //按模型里的原始下标给卡片编 id —— 网格是按角色分组渲染的，卡片的显示顺序
    //与模型顺序不同，故不能用网格内的序号，必须用模型下标。搜索过滤后同样如此，
    //右键菜单才能认出点的是哪一位。
    for (size_t i = 0; i < model.m_members.size(); ++i)
    {
        if (model.m_members[i].role == role
            && MemberMatchesKeyword(model.m_members[i], model.m_memberKeyword))
        {
            grid->AddChild(MakeMemberCard(model.m_members[i], (int)i,
                                          model.m_myRole == kRoleOwner || model.m_myRole == kRoleAdmin));
        }
    }
    return std::unique_ptr<DuiControl>(grid.release());
}

// 往页面追加「分组标题 + 成员网格」一段。
void AppendMemberSection(DuiVBox* page, const GroupManageModel& model,
                         GroupRole role, COLORREF dot, LPCTSTR roleName)
{
    //搜索时标题上的人数是命中的人数；一个都没命中的分组整组不显示
    const int count = model.CountByRoleMatching(role);
    if (count <= 0)
    {
        return;
    }

    CString header;
    header.Format(_T("%s · %d"), roleName, count);
    page->AddChild(MakeSectionHeader(dot, header), Hint().Fixed(GmScale(22)));

    int rows = 0;
    std::unique_ptr<DuiControl> grid = MakeMemberGrid(model, role, rows);
    //总高由换算后的卡片高与间距逐项相加，不能对总和整体换算：后者比各项分别换算
    //再相加少若干像素的取整误差，行数一多，最后一行成员卡就会被压扁。
    const int gridH = rows * GmScale(kMemberCardH)
                    + (rows > 0 ? (rows - 1) * GmScale(kGridGap) : 0);
    page->AddChild(std::move(grid), Hint().Fixed(gridH));
}

void AppendMembersPageBodyImpl(DuiVBox* page, const GroupManageModel& model);   //定义紧跟在本函数之后

std::unique_ptr<DuiControl> BuildMembersPage(const GroupManageModel& model)
{
    std::unique_ptr<DuiVBox> page(new DuiVBox());
    page->SetPadding(GmScale(24), GmScale(16), GmScale(24), GmScale(18));
    page->SetGap(GmScale(16));

    // 顶部：搜索框 + 邀请成员。
    std::unique_ptr<DuiHBox> topRow(new DuiHBox());
    topRow->SetGap(GmScale(12));

    std::unique_ptr<DuiSearchBox> search(new GmSearchBox());
    search->SetPlaceholder(TR(L"搜索成员姓名、备注、职务…"));
    search->SetCtrlId(kIdSearchMembers);
    search->SetShowBorder(true);
    topRow->AddChild(std::move(search), Hint().Weight(1));

    topRow->AddChild(MakeButton(TR(L"＋ 邀请成员"), kGmBtnPrimary, kIdBtnInvite, kGmTextBody),
                     Hint().Fixed(GmScale(118)));
    page->AddChild(std::move(topRow), Hint().Fixed(GmScale(44)));

    AppendMembersPageBodyImpl(page.get(), model);
    return std::unique_ptr<DuiControl>(page.release());
}

// 往成员管理页追加搜索栏以下的部分（加载提示，或按搜索关键词过滤后的三个角色分组与
// 「没有找到匹配的成员」提示行）。BuildMembersPage 构建时调用一次，RefilterMembersPage
// 在关键词变化后重新调用。
void AppendMembersPageBodyImpl(DuiVBox* page, const GroupManageModel& model)
{
    if (page == NULL)
    {
        return;
    }

    // 成员还没拉回来或拉取失败：只显示一行提示。搜索框与邀请按钮仍保留在原位，
    // 数据到达后页面顶部不会跳动。
    if (AppendLoadStatus(page, model.m_membersState,
                         TR(L"正在加载成员…"),
                         TR(L"成员列表加载失败，请关闭本窗口后重新打开。")))
    {
        return;
    }

    // 三个角色分组，各自只收命中搜索关键词的成员。
    AppendMemberSection(page, model, kRoleOwner,  kDotOwner,  TR(L"群主"));
    AppendMemberSection(page, model, kRoleAdmin,  kDotAdmin,  TR(L"管理员"));
    AppendMemberSection(page, model, kRoleMember, kDotMember, TR(L"普通成员"));

    // 关键词一个成员也没命中：三个分组都不出现，给一行提示，免得页面只剩搜索框、
    // 看不出是"没有匹配"还是"还没加载出来"。
    const int matched = model.CountByRoleMatching(kRoleOwner)
                      + model.CountByRoleMatching(kRoleAdmin)
                      + model.CountByRoleMatching(kRoleMember);
    if (matched == 0 && !model.m_memberKeyword.IsEmpty())
    {
        std::unique_ptr<GmText> text = MakeText(TR(L"没有找到匹配的成员"),
                                                kGmTextBody, kColorSubtitle, DT_LEFT);
        text->SetCtrlId(kIdMemberSearchEmpty);
        page->AddChild(std::unique_ptr<DuiControl>(text.release()),
                       Hint().Fixed(GmScale(kLoadStatusHeight)));
    }
}

// =====================================================================
// 加群审批页（design/群管理2.png）
// =====================================================================

// 把来源枚举翻译成展示文本。
CString JoinSourceText(int source)
{
    if (source == kJoinFromFriend)
    {
        return TR(L"好友邀请");
    }
    if (source == kJoinFromQrCode)
    {
        return TR(L"扫码加群");
    }
    return TR(L"群搜索");
}

// 造一张加群申请卡。index 用于逐条按钮 id。
std::unique_ptr<DuiControl> MakeRequestCard(const JoinRequest& r, int index)
{
    std::unique_ptr<DuiVBox> card = MakeCard(kCardBg, kCardBorder, kCardCorner);
    card->SetPadding(GmScale(16), GmScale(14), GmScale(16), GmScale(14));

    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(GmScale(14));

    row->AddChild(MakeAvatar(r.avatarText, r.avatarColor, 9, kOnlineNone),
                  Hint().Fixed(GmScale(40), GmScale(40)).AlignC(DuiLayout::AlignCenter));

    // 中部信息三行。
    std::unique_ptr<DuiVBox> info(new DuiVBox());
    info->SetGap(GmScale(4));

    CString line1;
    line1.Format(TR(L"%s    账号 %s"), (LPCTSTR)r.name, (LPCTSTR)r.account);
    info->AddChild(MakeText(line1, kGmTextCardTitle, kColorName, DT_LEFT),
                   Hint().Fixed(GmScale(22)));

    // 验证消息：有内容时加引号；没填时显示「未填写验证消息」并用更浅的灰色，不再显示一对空引号
    bool messagePlaceholder = false;
    const CString messageLine = RequestMessageLineText(r.message, &messagePlaceholder);
    info->AddChild(MakeText(messageLine, kGmTextBody,
                            messagePlaceholder ? kColorSubtitle : RGB(0x55, 0x55, 0x55), DT_LEFT),
                   Hint().Fixed(GmScale(20)));

    CString line3;
    line3.Format(TR(L"来源：%s"), (LPCTSTR)JoinSourceText(r.source));
    if (!r.recommender.IsEmpty())
    {
        line3 += _T(" · ");
        line3 += TRF(L"由 {0} 推荐", r.recommender).c_str();
    }
    line3 += _T(" · ");
    line3 += r.timeText;
    info->AddChild(MakeText(line3, kGmTextCaption, kColorSubtitle, DT_LEFT),
                   Hint().Fixed(GmScale(18)));

    row->AddChild(std::move(info), Hint().Weight(1));

    // 右侧：同意 / 拒绝 竖排。
    std::unique_ptr<DuiVBox> btns(new DuiVBox());
    btns->SetGap(GmScale(8));
    btns->AddChild(MakeButton(TR(L"同意"), kGmBtnPrimary,
                              kIdApprovalAcceptBase + index, kGmTextBody),
                   Hint().Fixed(GmScale(30)));
    btns->AddChild(MakeButton(TR(L"拒绝"), kGmBtnSecondary,
                              kIdApprovalRejectBase + index, kGmTextBody),
                   Hint().Fixed(GmScale(30)));
    Hint btnHint;
    btnHint.fixedMain  = GmScale(76);
    btnHint.fixedCross = GmScale(68);
    btnHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(btns), btnHint);

    card->AddChild(std::move(row));
    return std::unique_ptr<DuiControl>(card.release());
}

std::unique_ptr<DuiControl> BuildApprovalPage(const GroupManageModel& model)
{
    std::unique_ptr<DuiVBox> page(new DuiVBox());
    page->SetPadding(GmScale(24), GmScale(16), GmScale(24), GmScale(18));
    page->SetGap(GmScale(14));

    // 标题行：左侧标题 + 副标题，右侧批量按钮。
    std::unique_ptr<DuiHBox> titleRow(new DuiHBox());
    titleRow->SetGap(GmScale(10));

    //副标题里的条数只在申请已拉回来时才有意义；未到齐时不显示副标题，
    //免得出现"待处理 0 条"
    CString sub;
    if (model.m_requestsState == kLoadDone)
    {
        sub.Format(TR(L"待处理 %d 条 · 已自动拒绝超过 7 天的申请"),
                   model.PendingRequestCount());
    }
    titleRow->AddChild(std::unique_ptr<DuiControl>(
                           MakePageTitleBlock(TR(L"加群申请"), kColorPageTitle, sub).release()),
                       Hint().Weight(1));

    titleRow->AddChild(MakeButton(TR(L"批量同意"), kGmBtnSecondary, kIdBtnApproveAll, kGmTextBody),
                       Hint().Fixed(GmScale(84)).AlignC(DuiLayout::AlignCenter));
    titleRow->AddChild(MakeButton(TR(L"批量拒绝"), kGmBtnSecondary, kIdBtnRejectAll, kGmTextBody),
                       Hint().Fixed(GmScale(84)).AlignC(DuiLayout::AlignCenter));
    page->AddChild(std::move(titleRow), Hint().Fixed(GmScale(48)));

    // 申请还没拉回来或拉取失败：只显示一行提示。
    if (AppendLoadStatus(page.get(), model.m_requestsState,
                         TR(L"正在加载加群申请…"),
                         TR(L"加群申请加载失败，请关闭本窗口后重新打开。")))
    {
        return std::unique_ptr<DuiControl>(page.release());
    }

    // 申请卡列表。
    int index = 0;
    for (std::vector<JoinRequest>::const_iterator it = model.m_requests.begin();
         it != model.m_requests.end(); ++it, ++index)
    {
        page->AddChild(MakeRequestCard(*it, index), Hint().Fixed(GmScale(88)));
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
    card->SetPadding(GmScale(16), 0, GmScale(16), 0);

    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(GmScale(14));

    row->AddChild(MakeAvatar(m.avatarText, m.avatarColor, 9, kOnlineNone),
                  Hint().Fixed(GmScale(40), GmScale(40)).AlignC(DuiLayout::AlignCenter));

    std::unique_ptr<DuiVBox> info(new DuiVBox());
    info->SetGap(GmScale(3));
    info->AddChild(MakeText(m.name, kGmTextCardTitle, kColorName, DT_LEFT),
                   Hint().Fixed(GmScale(22)));
    CString reason;
    reason.Format(TR(L"原因：%s"), (LPCTSTR)m.reason);
    info->AddChild(MakeText(reason, kGmTextCaption, kColorSubtitle, DT_LEFT),
                   Hint().Fixed(GmScale(18)));
    Hint infoHint;
    infoHint.fixedCross = GmScale(40);
    infoHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(info), infoHint);

    // 右侧：剩余时长（永久标红）+ 延长 / 解除。
    std::unique_ptr<DuiVBox> right(new DuiVBox());
    right->SetGap(GmScale(8));
    const COLORREF remainColor = m.permanent ? kColorDanger : kColorSubtitle;
    right->AddChild(MakeText(m.remainingText, kGmTextCaption,
                             remainColor, DT_RIGHT),
                    Hint().Fixed(GmScale(18)));

    std::unique_ptr<DuiHBox> btnRow(new DuiHBox());
    btnRow->SetGap(GmScale(8));
    if (!m.permanent)
    {
        btnRow->AddChild(MakeButton(TR(L"延长"), kGmBtnSecondary,
                                    kIdMuteExtendBase + index, kGmTextCaption),
                         Hint().Fixed(GmScale(54)));
    }
    btnRow->AddChild(MakeButton(TR(L"解除"), kGmBtnSecondary,
                                kIdMuteReleaseBase + index, kGmTextCaption),
                     Hint().Fixed(GmScale(54)));
    right->AddChild(std::move(btnRow), Hint().Fixed(GmScale(30)));

    Hint rightHint;
    rightHint.fixedMain  = GmScale(130);
    rightHint.fixedCross = GmScale(56);
    rightHint.alignCross = DuiLayout::AlignCenter;
    rightHint.alignMain  = DuiLayout::AlignFar;
    row->AddChild(std::move(right), rightHint);

    card->AddChild(std::move(row));
    return std::unique_ptr<DuiControl>(card.release());
}

std::unique_ptr<DuiControl> BuildMutePage(const GroupManageModel& model)
{
    std::unique_ptr<DuiVBox> page(new DuiVBox());
    page->SetPadding(GmScale(24), GmScale(16), GmScale(24), GmScale(18));
    page->SetGap(GmScale(14));

    // 标题行：标题 + 副标题。
    std::unique_ptr<DuiHBox> titleRow(new DuiHBox());
    titleRow->SetGap(GmScale(10));

    //禁言列表由群成员应答派生，成员未到齐时不显示副标题，免得出现"禁言中 0 人"
    CString sub;
    if (model.m_membersState == kLoadDone)
    {
        sub.Format(TR(L"禁言中 %d 人"), model.MutedCount());
    }
    titleRow->AddChild(std::unique_ptr<DuiControl>(
                           MakePageTitleBlock(TR(L"禁言管理"), kColorPageTitle, sub).release()),
                       Hint().Weight(1));
    page->AddChild(std::move(titleRow), Hint().Fixed(GmScale(48)));

    //「仅通知模式」（原称全员禁言，docs/群管理接入方案.md §14）单独一行：左侧标题与灰色说明，右侧开关，标题与说明都挂
    //悬停提示（译文较长、说明被截断时也能看全）。开关取自群资料，群资料未到齐时整行不显示（与上面副标题的处理一致），
    //否则显示的是默认的"关"，管理员会误以为本群没有开启
    if (model.m_groupInfoState == kLoadDone)
    {
        std::unique_ptr<DuiHBox> notifyRow(new DuiHBox());
        notifyRow->SetGap(GmScale(12));

        std::unique_ptr<DuiVBox> labelCol(new DuiVBox());
        labelCol->SetGap(GmScale(3));
        std::unique_ptr<GmText> title = MakeText(balloonchatwnd::NotifyOnlyTitleText(), kGmTextItemTitle,
                                                 kColorFieldLabel, DT_LEFT);
        title->SetCtrlId(kIdNotifyOnlyTitle);
        balloonwjui::DuiToolTipMgr::Inst().Register(title.get(), balloonchatwnd::NotifyOnlyDescriptionText());
        labelCol->AddChild(std::unique_ptr<DuiControl>(title.release()), Hint().Fixed(GmScale(22)));
        std::unique_ptr<GmText> desc = MakeText(balloonchatwnd::NotifyOnlyDescriptionText(), kGmTextCaption,
                                                kColorSubtitle, DT_LEFT);
        desc->SetCtrlId(kIdNotifyOnlyDesc);
        balloonwjui::DuiToolTipMgr::Inst().Register(desc.get(), balloonchatwnd::NotifyOnlyDescriptionText());
        labelCol->AddChild(std::unique_ptr<DuiControl>(desc.release()), Hint().Fixed(GmScale(18)));
        Hint labelHint;
        labelHint.weight     = 1;
        labelHint.fixedCross = GmScale(40);
        labelHint.alignCross = DuiLayout::AlignCenter;
        notifyRow->AddChild(std::move(labelCol), labelHint);

        std::unique_ptr<DuiSwitch> swAll(new DuiSwitch());
        swAll->SetCtrlId(kIdSwitchMuteAll);
        StyleSwitch(swAll.get());
        swAll->SetChecked(model.m_muteAll, false, false);
        notifyRow->AddChild(std::move(swAll), Hint().Fixed(GmScale(46), GmScale(24)).AlignC(DuiLayout::AlignCenter));
        page->AddChild(std::move(notifyRow), Hint().Fixed(GmScale(44)));
    }

    // 添加禁言成员按钮。
    std::unique_ptr<DuiHBox> addRow(new DuiHBox());
    addRow->AddChild(MakeButton(TR(L"＋ 添加禁言成员"), kGmBtnPrimary, kIdBtnAddMute, kGmTextBody),
                     Hint().Fixed(GmScale(140)));
    addRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    page->AddChild(std::move(addRow), Hint().Fixed(GmScale(40)));

    // 成员还没拉回来或拉取失败：只显示一行提示。
    if (AppendLoadStatus(page.get(), model.m_membersState,
                         TR(L"正在加载禁言成员…"),
                         TR(L"禁言成员加载失败，请关闭本窗口后重新打开。")))
    {
        return std::unique_ptr<DuiControl>(page.release());
    }

    // 禁言卡列表。
    int index = 0;
    for (std::vector<MutedMember>::const_iterator it = model.m_muted.begin();
         it != model.m_muted.end(); ++it, ++index)
    {
        page->AddChild(MakeMuteCard(*it, index), Hint().Fixed(GmScale(70)));
    }

    return std::unique_ptr<DuiControl>(page.release());
}

// =====================================================================
// 基本信息页（design/群管理4.png）
// =====================================================================

// 基本信息页表单行的尺寸（96 DPI 下的像素，取自 design/群管理4.png）：每行高度、
// 右侧控件高度，以及左侧标签与说明两行各自的行高和两行之间的间距。
const int kFormRowHeight     = 48;
const int kFormControlHeight = 32;
const int kFormLabelHeight   = 22;
const int kFormDescHeight    = 18;
const int kFormLabelDescGap  = 3;

// 造一行表单：左侧标签（可带副说明）+ 右侧控件，两者在行内上下居中。
//   controlWidth：右侧控件宽度（96 DPI 下的像素）；控件高度统一为 kFormControlHeight。
std::unique_ptr<DuiControl> MakeFormRow(LPCTSTR label, LPCTSTR desc,
                                        std::unique_ptr<DuiControl> control,
                                        int controlWidth)
{
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(GmScale(12));

    const bool hasDesc = (desc != NULL && desc[0] != _T('\0'));
    std::unique_ptr<DuiVBox> labelCol(new DuiVBox());
    labelCol->SetGap(GmScale(kFormLabelDescGap));
    labelCol->AddChild(MakeText(label, kGmTextItemTitle, kColorFieldLabel, DT_LEFT),
                       Hint().Fixed(GmScale(kFormLabelHeight)));
    int labelBlockHeight = GmScale(kFormLabelHeight);
    if (hasDesc)
    {
        labelCol->AddChild(MakeText(desc, kGmTextCaption, kColorSubtitle, DT_LEFT),
                           Hint().Fixed(GmScale(kFormDescHeight)));
        labelBlockHeight += GmScale(kFormLabelDescGap) + GmScale(kFormDescHeight);
    }

    //交叉轴高度必须给成标签块的实际高度：不给（-1）就会被拉满整行，居中不起作用，
    //文字从顶部开始排，看起来是顶对齐
    Hint labelHint;
    labelHint.weight     = 1;
    labelHint.fixedCross = labelBlockHeight;
    labelHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(labelCol), labelHint);

    row->AddChild(std::move(control),
                  Hint().Fixed(GmScale(controlWidth), GmScale(kFormControlHeight))
                        .AlignC(DuiLayout::AlignCenter));

    return std::unique_ptr<DuiControl>(row.release());
}

// 把一行表单放进表单列表，并在它下面加一条 1px 分隔线（design/群管理4.png）。
void AddFormRow(DuiVBox* list, std::unique_ptr<DuiControl> row)
{
    list->AddChild(std::move(row), Hint().Fixed(GmScale(kFormRowHeight)));
    list->AddChild(MakeSeparatorRow(), Hint().Fixed(kSeparatorPx));
}

std::unique_ptr<DuiControl> BuildBasicInfoPage(const GroupManageModel& model)
{
    // 本页的页面间距（96 DPI 下的像素）
    const int kBasicPageGap = 16;

    //限宽页：内容不超过 kMaxContentWidth，右侧留白（design/群管理4.png）
    std::unique_ptr<DuiVBox> page = MakeWidthLimitedPage();
    page->SetPadding(GmScale(24), GmScale(16), GmScale(24), GmScale(18));
    page->SetGap(GmScale(kBasicPageGap));

    AddPageTitleBlock(page.get(), kBasicPageGap, TR(L"基本信息"), kColorPageTitle, NULL);

    // 群资料还没拉回来或拉取失败：只显示一行提示，不显示表单。此时模型里的群简介等
    // 只是构造时的默认值，不能当作真实内容显示（见 GroupManageModel::m_groupInfoState）。
    if (AppendLoadStatus(page.get(), model.m_groupInfoState,
                         TR(L"正在加载群资料…"),
                         TR(L"群资料加载失败，请关闭本窗口后重新打开。")))
    {
        return std::unique_ptr<DuiControl>(page.release());
    }

    // 群头像卡。
    std::unique_ptr<DuiVBox> avatarCard = MakeCard(kCardBg, kCardBorder, kCardCorner);
    avatarCard->SetPadding(GmScale(18), 0, GmScale(18), 0);
    std::unique_ptr<DuiHBox> avatarRow(new DuiHBox());
    avatarRow->SetGap(GmScale(16));
    const int kBasicAvatarPx     = 56;   //基本信息页头像卡里的头像边长
    const int kBasicAvatarCorner = 10;   //其圆角半径
    avatarRow->AddChild(MakeGroupAvatar(model, kBasicAvatarPx, kBasicAvatarCorner),
                        Hint().Fixed(GmScale(kBasicAvatarPx), GmScale(kBasicAvatarPx))
                              .AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiVBox> avatarInfo(new DuiVBox());
    avatarInfo->SetGap(GmScale(4));
    avatarInfo->AddChild(MakeText(TR(L"群头像"), kGmTextCardTitle, kColorName, DT_LEFT),
                         Hint().Fixed(GmScale(22)));
    avatarInfo->AddChild(MakeText(TR(L"支持 JPG、PNG，建议 200×200 以上"), kGmTextBody, kColorSubtitle, DT_LEFT),
                         Hint().Fixed(GmScale(18)));
    Hint avatarInfoHint;
    avatarInfoHint.weight     = 1;
    avatarInfoHint.fixedCross = GmScale(44);
    avatarInfoHint.alignCross = DuiLayout::AlignCenter;
    avatarRow->AddChild(std::move(avatarInfo), avatarInfoHint);
    //只有群主和管理员能改群头像（服务端同口径，见 grouprole::CanChangeGroupAvatar）。
    //对普通成员置灰而不是隐藏：按钮没了，旁边"支持 JPG、PNG…"那行说明就成了没有
    //指向的一句话；置灰能让人看出这里能换、只是自己没权限。
    //类型跟着 MakeButton 的返回值走（GroupManageButton 派生自 DuiButton）——
    //写成 unique_ptr<DuiButton> 既要多 include 一个头，也没有额外好处。
    std::unique_ptr<GroupManageButton> btnChangeAvatar =
        MakeButton(TR(L"更换头像"), kGmBtnSecondary, kIdBtnChangeAvatar, kGmTextBody);
    if (!grouprole::CanChangeGroupAvatar(UiRoleToNetRole(model.m_myRole)))
    {
        btnChangeAvatar->SetEnabled(false);
    }
    avatarRow->AddChild(std::move(btnChangeAvatar),
                        Hint().Fixed(GmScale(96), GmScale(34)).AlignC(DuiLayout::AlignCenter));
    avatarCard->AddChild(std::move(avatarRow));
    page->AddChild(std::move(avatarCard), Hint().Fixed(GmScale(82)));

    // 表单列表：群名称、群号、群公告、群简介四行，每行下面一条分隔线，行与线紧挨着
    //（design/群管理4.png）。
    std::unique_ptr<DuiVBox> formList(new DuiVBox());
    formList->SetCtrlId(kIdBasicFormList);
    formList->SetGap(0);

    // 群名称。清空后保存即恢复为自动群名（服务端按成员昵称生成，并随成员变化重算），
    // 占位文字把这一点告诉用户。
    std::unique_ptr<DuiEdit> nameEdit(new GmEdit());
    nameEdit->SetCtrlId(kIdEditGroupName);
    nameEdit->SetShowBorder(true);
    nameEdit->SetPlaceholder(TR(L"留空则按成员自动生成"));
    nameEdit->SetText(model.m_groupName);
    AddFormRow(formList.get(), MakeFormRow(TR(L"群名称"), _T(""),
                                           std::unique_ptr<DuiControl>(nameEdit.release()), 260));

    // 群号（只读文本 + 复制按钮）。
    std::unique_ptr<DuiHBox> numberBox(new DuiHBox());
    numberBox->SetGap(GmScale(8));
    numberBox->AddChild(MakeText(model.m_groupNumber, kGmTextItemTitle,
                                 kColorName, DT_RIGHT),
                        Hint().Weight(1).AlignC(DuiLayout::AlignCenter));
    numberBox->AddChild(MakeButton(TR(L"复制"), kGmBtnSecondary, kIdBtnCopyGroupNo, kGmTextCaption),
                        Hint().Fixed(GmScale(44), GmScale(26)).AlignC(DuiLayout::AlignCenter));
    AddFormRow(formList.get(), MakeFormRow(TR(L"群号"), _T(""),
                                           std::unique_ptr<DuiControl>(numberBox.release()), 200));

    // 群公告（只读摘要 + 跳转按钮）。
    //
    // 这里<u>刻意不给输入框</u>：群公告已经是一条条带标题、发布人、优先级、可撤回的
    // 记录（存在 t_group_announcement 表里），由「群公告」页统一发布与管理。若这里
    // 再留一个直接改文本的框，同一份数据就有了两条写入路径，先后顺序不同结果就不同。
    // 故本行只显示当前最新一条公告，编辑走右边那个跳转按钮。帖子群没有群公告，整行不出现
    //（docs/帖子群方案.md §15.1 第 3 条）。
    if (!model.m_isThreadGroup)
    {
        std::unique_ptr<DuiHBox> annBox(new DuiHBox());
        annBox->SetGap(GmScale(8));
        CString annSummary = model.m_announcement;
        if (annSummary.IsEmpty())
        {
            annSummary = TR(L"暂无群公告");
        }
        annBox->AddChild(MakeText(annSummary, kGmTextBody, kColorSubtitle, DT_LEFT),
                         Hint().Weight(1).AlignC(DuiLayout::AlignCenter));
        //普通成员看不到「群公告」页，给他们一个点了会落空的按钮没有意义
        if (IsAnnouncementPageVisible(model.m_myRole, model.m_isThreadGroup))
        {
            annBox->AddChild(MakeButton(TR(L"去管理"), kGmBtnSecondary, kIdBtnAnnounceGoto, kGmTextCaption),
                             Hint().Fixed(GmScale(64), GmScale(26)).AlignC(DuiLayout::AlignCenter));
        }
        AddFormRow(formList.get(), MakeFormRow(TR(L"群公告"), TR(L"在「群公告」里发布与管理"),
                                               std::unique_ptr<DuiControl>(annBox.release()), 260));
    }

    // 群简介。
    std::unique_ptr<DuiEdit> introEdit(new GmEdit());
    introEdit->SetCtrlId(kIdEditIntro);
    introEdit->SetShowBorder(true);
    introEdit->SetPlaceholder(TR(L"介绍一下这个群…"));
    introEdit->SetText(model.m_intro);
    AddFormRow(formList.get(), MakeFormRow(TR(L"群简介"), TR(L"可选，显示在群资料页"),
                                           std::unique_ptr<DuiControl>(introEdit.release()), 260));

    // 列表高度 = 各行高 + 各分隔线高。行数取自列表本身，增删行时不用改这里。
    const int formRowCount = (int)formList->Children().size() / 2;
    page->AddChild(std::unique_ptr<DuiControl>(formList.release()),
                   Hint().Fixed(formRowCount * (GmScale(kFormRowHeight) + kSeparatorPx)));

    // 底部：重置 / 保存修改（右对齐）。
    std::unique_ptr<DuiHBox> btnRow(new DuiHBox());
    btnRow->SetGap(GmScale(12));
    btnRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    btnRow->AddChild(MakeButton(TR(L"重置"), kGmBtnSecondary, kIdBtnReset, kGmTextBody),
                     Hint().Fixed(GmScale(80)));
    btnRow->AddChild(MakeButton(TR(L"保存修改"), kGmBtnPrimary, kIdBtnSave, kGmTextBody),
                     Hint().Fixed(GmScale(110)));
    page->AddChild(std::move(btnRow), Hint().Fixed(GmScale(40)));

    return std::unique_ptr<DuiControl>(page.release());
}

// =====================================================================
// 群权限页（design/群管理5.png）
// =====================================================================

// 造一行权限设置：左侧标题 + 说明，右侧控件。
// （原先还有一个可选的尾随控件参数，只给「消息敏感词过滤」一行的「管理词库」按钮用；
//  群级敏感词于 2026-10-02 删除后该参数没有调用方，一并去掉。）
std::unique_ptr<DuiControl> MakePermRow(LPCTSTR title, LPCTSTR desc,
                                        std::unique_ptr<DuiControl> control,
                                        int controlWidth)
{
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(GmScale(12));
    row->SetPadding(GmScale(18), 0, GmScale(18), 0);

    std::unique_ptr<DuiVBox> labelCol(new DuiVBox());
    labelCol->SetGap(GmScale(3));
    labelCol->AddChild(MakeText(title, kGmTextItemTitle, kColorFieldLabel, DT_LEFT),
                       Hint().Fixed(GmScale(22)));
    labelCol->AddChild(MakeText(desc, kGmTextCaption, kColorSubtitle, DT_LEFT),
                       Hint().Fixed(GmScale(18)));
    Hint labelHint;
    labelHint.weight     = 1;
    labelHint.fixedCross = GmScale(40);
    labelHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(labelCol), labelHint);

    row->AddChild(std::move(control),
                  Hint().Fixed(GmScale(controlWidth), GmScale(controlWidth >= 100 ? 34 : 24))
                        .AlignC(DuiLayout::AlignCenter));
    return std::unique_ptr<DuiControl>(row.release());
}

// 造一个开关，按初值设定。
std::unique_ptr<DuiControl> MakePermSwitch(UINT ctrlId, bool checked)
{
    std::unique_ptr<DuiSwitch> sw(new DuiSwitch());
    sw->SetCtrlId(ctrlId);
    StyleSwitch(sw.get());
    sw->SetChecked(checked, false, false);
    return std::unique_ptr<DuiControl>(sw.release());
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
const int kAnnMustReadChipW = 60;   // 「入群必读」徽章的宽
const int kAnnMustReadBtnW  = 96;   // 发布人一行右侧「设为入群必读 / 取消入群必读」按钮的宽
const COLORREF kColorMustReadBadgeBg = RGB(0xE6, 0xEC, 0xFB);   // 「入群必读」徽章底色（浅蓝）

// 一条历史公告在当前 DPI 下的整体行高（像素）。由内部各行累加得到，写死一个数字
// 迟早对不上；各项分别换算后再相加，与条目内部逐项换算的结果一致。
//
// 右侧按钮列要的高度是 kAnnRowBtnH * 2 + kAnnRowBtnGap = 58，比中间那三行
// 累加出来的 60 略小，正好放得下。日后调这几个常量时留意别让按钮列超过中间列，
// 否则按钮会被行高截掉一截。
int AnnRowHeight()
{
    return GmScale(kAnnRowPadV) * 2 + GmScale(kAnnRowTitleH)
         + GmScale(kAnnRowBodyH) + GmScale(kAnnRowMetaH);
}

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
        return TR(L"紧急");
    }
    if (severity == kSeverityImportant)
    {
        bgOut = RGB(0xFD, 0xE9, 0xDD);
        fgOut = RGB(0xE0, 0x6A, 0x2E);
        return TR(L"重要");
    }
    bgOut = RGB(0xE6, 0xEC, 0xFB);
    fgOut = RGB(0x45, 0x6C, 0xDF);
    return TR(L"通知");
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
    badge->SetCornerRadius(GmScale(4));
    badge->SetHideWhenEmpty(false);
    badge->SetBgColor(bg);
    badge->SetTextColor(fg);
    return badge;
}

// 造一条历史公告：左侧铃铛 + 标题 / 优先级徽章 + 正文摘要 + 发布人时间，
// 右侧「重新发布」「撤回」两个按钮。对照 design/群管理7.png 的历史公告条目。
//   a：    公告数据；
//   index：该条在 model.m_announcements 里的下标，用来算两个按钮的 ctrl id。
std::unique_ptr<DuiControl> MakeAnnouncementRow(const GroupAnnouncement& a, int index, bool isMustRead)
{
    std::unique_ptr<DuiVBox> card = MakeCard(kCardBg, kCardBorder, kCardCorner);
    card->SetPadding(GmScale(14), GmScale(kAnnRowPadV), GmScale(12), GmScale(kAnnRowPadV));

    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(GmScale(12));

    // 左侧铃铛图标。复用导航「群公告」项那枚图标，两处是同一个概念。
    std::unique_ptr<GmIcon> bell(new GmIcon());
    bell->Configure(kGmIconAnnounce, RGB(0xE0, 0x6A, 0x2E));
    row->AddChild(std::unique_ptr<DuiControl>(bell.release()),
                  Hint().Fixed(GmScale(kAnnBellSize), GmScale(kAnnBellSize)).AlignC(DuiLayout::AlignCenter));

    // 中间：标题行（标题 + 优先级徽章 + 已撤回标记）/ 正文摘要 / 发布人·时间
    std::unique_ptr<DuiVBox> col(new DuiVBox());
    col->SetGap(GmScale(2));

    std::unique_ptr<DuiHBox> titleRow(new DuiHBox());
    titleRow->SetGap(GmScale(8));
    //标题按内容自然占宽，徽章紧随其后 —— 让徽章贴着标题，而不是被推到最右边
    titleRow->AddChild(MakeText(a.title, kGmTextCardTitle, kColorName, DT_LEFT),
                       Hint().Weight(1).AlignC(DuiLayout::AlignCenter));
    titleRow->AddChild(std::unique_ptr<DuiControl>(MakeSeverityBadge(a.severity).release()),
                       Hint().Fixed(GmScale(kAnnChipW), GmScale(kAnnChipH)).AlignC(DuiLayout::AlignCenter));
    //被设为入群必读的那一条：优先级徽章后面再标「入群必读」（docs/群公告接入方案.md §14）
    if (isMustRead)
    {
        std::unique_ptr<DuiBadge> mustRead(new DuiBadge());
        mustRead->SetText(balloonchatwnd::MustReadSenderName());
        mustRead->SetMaxDisplayChars(0);
        mustRead->SetCornerRadius(GmScale(4));
        mustRead->SetHideWhenEmpty(false);
        mustRead->SetBgColor(kColorMustReadBadgeBg);
        mustRead->SetTextColor(kColorMustReadMark);
        titleRow->AddChild(std::unique_ptr<DuiControl>(mustRead.release()),
                           Hint().Fixed(GmScale(kAnnMustReadChipW), GmScale(kAnnChipH)).AlignC(DuiLayout::AlignCenter));
    }
    col->AddChild(std::move(titleRow), Hint().Fixed(GmScale(kAnnRowTitleH)));

    //正文只显示一行摘要 —— 历史列表是用来"找到某条公告"的，不是用来通读的，
    //完整正文在群聊窗边栏的公告卡里看
    col->AddChild(MakeText(a.content, kGmTextBody, kColorSubtitle, DT_LEFT),
                  Hint().Fixed(GmScale(kAnnRowBodyH)));

    CString meta;
    meta.Format(_T("%s · %s"), (LPCTSTR)a.publisherName, (LPCTSTR)a.timeText);
    //发布人一行右侧放「设为入群必读 / 取消入群必读」：右侧按钮列已放满两个按钮，再加会超出行高
    std::unique_ptr<DuiHBox> metaRow(new DuiHBox());
    metaRow->SetGap(GmScale(8));
    metaRow->AddChild(MakeText(meta, kGmTextCaption, kColorSubtitle, DT_LEFT),
                      Hint().Weight(1).AlignC(DuiLayout::AlignCenter));
    if (index >= 0 && index <= (kIdAnnounceMustReadMax - kIdAnnounceMustReadBase))
    {
        metaRow->AddChild(MakeButton(isMustRead ? TR(L"取消入群必读") : TR(L"设为入群必读"), kGmBtnSecondary,
                                     kIdAnnounceMustReadBase + index, kGmTextCaption),
                          Hint().Fixed(GmScale(kAnnMustReadBtnW)));
    }
    col->AddChild(std::move(metaRow), Hint().Fixed(GmScale(kAnnRowMetaH)));

    Hint colHint;
    colHint.weight     = 1;
    colHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(col), colHint);

    // 右侧两个按钮。id 用<u>本页内</u>的下标算，点击时反查是哪一条。
    if (index >= 0 && index <= (kIdAnnounceRepublishMax - kIdAnnounceRepublishBase))
    {
        std::unique_ptr<DuiVBox> btnCol(new DuiVBox());
        btnCol->SetGap(GmScale(kAnnRowBtnGap));
        //Hint::Fixed 的第一个参数是<u>主轴</u>尺寸。btnCol 是 DuiVBox，主轴是竖直
        //方向，故这里传的是行高；交叉轴（宽）留 -1 撑满，两个按钮才等宽。
        //曾误写成 Fixed(0, 26)（高 0、宽 26），两个按钮都被压成零高度，界面上
        //一个都看不见。
        btnCol->AddChild(MakeButton(TR(L"重新发布"), kGmBtnSecondary,
                                    kIdAnnounceRepublishBase + index, kGmTextCaption),
                         Hint().Fixed(GmScale(kAnnRowBtnH)));
        //删除是这一页上唯一不可恢复的操作，用危险色，点击时还要过一道确认
        btnCol->AddChild(MakeButton(TR(L"删除"), kGmBtnDanger,
                                    kIdAnnounceDeleteBase + index, kGmTextCaption),
                         Hint().Fixed(GmScale(kAnnRowBtnH)));
        row->AddChild(std::move(btnCol),
                      Hint().Fixed(GmScale(84)).AlignC(DuiLayout::AlignCenter));
    }

    card->AddChild(std::move(row));
    return std::unique_ptr<DuiControl>(card.release());
}

std::unique_ptr<DuiControl> BuildAnnouncementPage(const GroupManageModel& model)
{
    // 本页的页面间距（96 DPI 下的像素）
    const int kAnnPageGap = 8;

    std::unique_ptr<DuiVBox> page(new DuiVBox());
    page->SetPadding(GmScale(24), GmScale(16), GmScale(24), GmScale(18));
    page->SetGap(GmScale(kAnnPageGap));

    AddPageTitleBlock(page.get(), kAnnPageGap, TR(L"群公告"), kColorPageTitle,
                      TR(L"发布后所有成员将收到弹窗通知，并可在群聊窗的「公告」里再次查看"));

    // ---- 发布新公告卡片 ----
    {
        std::unique_ptr<DuiVBox> card = MakeCard(kColorWhite, kCardBorder, kCardCorner);
        card->SetPadding(GmScale(16), GmScale(12), GmScale(16), GmScale(12));
        card->SetGap(GmScale(10));

        // 卡片头：「发布新公告」+ 右侧优先级三选一
        std::unique_ptr<DuiHBox> head(new DuiHBox());
        head->SetGap(GmScale(6));
        head->AddChild(MakeText(TR(L"发布新公告"), kGmTextCardTitle, kColorName, DT_LEFT),
                       Hint().Weight(1).AlignC(DuiLayout::AlignCenter));
        head->AddChild(MakeText(TR(L"优先级"), kGmTextCaption, kColorSubtitle, DT_RIGHT),
                       Hint().Fixed(GmScale(48)).AlignC(DuiLayout::AlignCenter));

        //三个 chip 用普通按钮做，选中的那个用主色、其余用次要色 —— 与群文件面板
        //的分类筛选 chips 同一套做法，不必为此新造一个单选控件
        const int chipIds[3] =
        {
            kIdChipSeverityNormal, kIdChipSeverityImportant, kIdChipSeverityUrgent
        };
        LPCTSTR chipLabels[3] = { TR(L"通知"), TR(L"重要"), TR(L"紧急") };
        for (int i = 0; i < 3; ++i)
        {
            const int style = (i == model.m_draftSeverity) ? kGmBtnPrimary : kGmBtnSecondary;
            head->AddChild(MakeButton(chipLabels[i], style, chipIds[i], kGmTextCaption),
                           Hint().Fixed(GmScale(kAnnChipW), GmScale(kAnnChipH)).AlignC(DuiLayout::AlignCenter));
        }
        card->AddChild(std::move(head), Hint().Fixed(GmScale(26)));

        // 标题（必填）
        std::unique_ptr<DuiEdit> titleEdit(new GmEdit());
        titleEdit->SetCtrlId(kIdEditAnnounceTitle);
        titleEdit->SetShowBorder(true);
        titleEdit->SetPlaceholder(TR(L"公告标题（必填）"));
        card->AddChild(std::unique_ptr<DuiControl>(titleEdit.release()),
                       Hint().Fixed(GmScale(kAnnFormTitleH)));

        // 正文（多行）
        std::unique_ptr<DuiEdit> bodyEdit(new GmEdit());
        bodyEdit->SetCtrlId(kIdEditAnnounceBody);
        bodyEdit->SetMultiLine(true);
        bodyEdit->SetWordWrap(true);
        bodyEdit->SetShowBorder(true);
        bodyEdit->SetPlaceholder(TR(L"公告内容（支持换行，Shift+Enter 换行）"));
        card->AddChild(std::unique_ptr<DuiControl>(bodyEdit.release()),
                       Hint().Fixed(GmScale(kAnnFormBodyH)));

        // 底部：清空 / 发布公告（右对齐）
        std::unique_ptr<DuiHBox> btnRow(new DuiHBox());
        btnRow->SetGap(GmScale(10));
        btnRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
        btnRow->AddChild(MakeButton(TR(L"清空"), kGmBtnSecondary, kIdBtnAnnounceClear, kGmTextBody),
                         Hint().Fixed(GmScale(76), GmScale(kAnnFormBtnH)));
        btnRow->AddChild(MakeButton(TR(L"发布公告"), kGmBtnPrimary, kIdBtnAnnouncePublish, kGmTextBody),
                         Hint().Fixed(GmScale(110), GmScale(kAnnFormBtnH)));
        card->AddChild(std::move(btnRow), Hint().Fixed(GmScale(kAnnFormBtnH)));

        //卡片高度 = 内边距 + 各行 + 行间距（4 行 3 个 gap）。各项分别换算后再相加，
        //与卡片内部逐项换算的结果一致，不会因取整误差挤压最后一行。
        const int cardH = GmScale(12) * 2 + GmScale(26) + GmScale(kAnnFormTitleH)
                        + GmScale(kAnnFormBodyH) + GmScale(kAnnFormBtnH) + GmScale(10) * 3;
        page->AddChild(std::move(card), Hint().Fixed(cardH).Margin(0, GmScale(6), 0, 0));
    }

    // ---- 历史公告列表 ----
    if (!model.m_announcementsLoaded)
    {
        //还没拉回来 —— 与"拉过但一条都没有"要分开说，否则用户会以为公告全没了
        page->AddChild(MakeText(TR(L"正在加载公告…"), kGmTextBody,
                                kColorSubtitle, DT_LEFT),
                       Hint().Fixed(GmScale(40)).Margin(0, GmScale(14), 0, 0));
    }
    else if (model.m_announcements.empty())
    {
        page->AddChild(MakeText(TR(L"还没有发布过公告。在上面写一条，全体成员都会收到提醒。"),
                                kGmTextBody, kColorSubtitle, DT_LEFT),
                       Hint().Fixed(GmScale(40)).Margin(0, GmScale(14), 0, 0));
    }
    else
    {
        page->AddChild(MakeText(TR(L"历史公告"), kGmTextBody, kColorSubtitle, DT_LEFT),
                       Hint().Fixed(GmScale(22)).Margin(0, GmScale(12), 0, 0));

        //本页的条目。id 用本页内的下标算 —— 翻页后同一个 id 指向的是另一条，
        //这没关系：按钮点下去时读的是当时那一页的模型。
        for (size_t i = 0; i < model.m_announcements.size(); ++i)
        {
            const bool isMustRead = (model.m_mustReadId > 0 && model.m_announcements[i].id == model.m_mustReadId);
            page->AddChild(MakeAnnouncementRow(model.m_announcements[i], (int)i, isMustRead),
                           Hint().Fixed(AnnRowHeight()).Margin(0, 0, 0, GmScale(kAnnRowGap)));
        }

        //底部分页栏：「共 N 条 · 第 x/y 页」+ 上一页 / 下一页。与群文件面板同一套
        //交互，公告攒到几百条也只在这一页里翻，不会把控件树撑爆。
        const int pageCount = model.AnnouncementPageCount();
        std::unique_ptr<DuiHBox> pager(new DuiHBox());
        pager->SetGap(GmScale(8));

        CString pageText;
        pageText.Format(TR(L"共 %d 条 · 第 %d/%d 页"),
                        model.m_announcementTotal, model.m_announcementPage + 1, pageCount);
        pager->AddChild(MakeText(pageText, kGmTextCaption, kColorSubtitle, DT_LEFT),
                        Hint().Weight(1).AlignC(DuiLayout::AlignCenter));

        //已经在第一页 / 最后一页时那个方向的按钮不建 —— 建出来点了没反应，
        //不如不给。只有一页时两个都不出现。
        if (model.m_announcementPage > 0)
        {
            pager->AddChild(MakeButton(_T("‹"), kGmBtnSecondary, kIdAnnouncePagePrev, kGmTextCaption),
                            Hint().Fixed(GmScale(28), GmScale(26)).AlignC(DuiLayout::AlignCenter));
        }
        if (model.m_announcementPage + 1 < pageCount)
        {
            pager->AddChild(MakeButton(_T("›"), kGmBtnSecondary, kIdAnnouncePageNext, kGmTextCaption),
                            Hint().Fixed(GmScale(28), GmScale(26)).AlignC(DuiLayout::AlignCenter));
        }

        page->AddChild(std::move(pager), Hint().Fixed(GmScale(30)).Margin(0, GmScale(4), 0, 0));
    }

    return std::unique_ptr<DuiControl>(page.release());
}

// ---- 加群链接页（docs/邀请入群方案.md §12）----
const int kLinkRowH        = 64;    // 一条链接的卡片高
const int kLinkRowGap      = 8;     // 卡片之间的间距
const int kLinkRevokeW     = 64;    // 「作废」按钮的宽
const int kLinkRevokeH     = 28;    // 「作废」按钮的高
const int kGroupLinkRowMax = 100;   // 最多显示的条数（与 kIdLinkRevokeBase 段的大小一致）

// 造一条链接：左侧链接文字与「生成人 · 生成时间 · 到期」，右侧「作废」
std::unique_ptr<DuiControl> MakeLinkRow(const GroupLinkRow& link, int index)
{
    std::unique_ptr<DuiVBox> card = MakeCard(kCardBg, kCardBorder, kCardCorner);
    card->SetPadding(GmScale(14), GmScale(10), GmScale(12), GmScale(10));
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(GmScale(12));

    std::unique_ptr<DuiVBox> col(new DuiVBox());
    col->SetGap(GmScale(4));
    col->AddChild(MakeText(link.url, kGmTextBody, kColorName, DT_LEFT), Hint().Fixed(GmScale(20)));
    CString meta;
    meta.Format(_T("%s · %s · %s"), (LPCTSTR)link.creatorName, (LPCTSTR)link.createText, (LPCTSTR)link.expireText);
    col->AddChild(MakeText(meta, kGmTextCaption, kColorSubtitle, DT_LEFT), Hint().Fixed(GmScale(18)));
    Hint colHint;
    colHint.weight     = 1;
    colHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(col), colHint);

    row->AddChild(MakeButton(TR(L"作废"), kGmBtnDangerOutline, kIdLinkRevokeBase + index, kGmTextCaption),
                  Hint().Fixed(GmScale(kLinkRevokeW), GmScale(kLinkRevokeH)).AlignC(DuiLayout::AlignCenter));
    card->AddChild(std::move(row));
    return std::unique_ptr<DuiControl>(card.release());
}

std::unique_ptr<DuiControl> BuildLinksPage(const GroupManageModel& model)
{
    // 本页的页面间距（96 DPI 下的像素）
    const int kLinksPageGap = 8;
    std::unique_ptr<DuiVBox> page(new DuiVBox());
    page->SetPadding(GmScale(24), GmScale(16), GmScale(24), GmScale(18));
    page->SetGap(GmScale(kLinksPageGap));
    AddPageTitleBlock(page.get(), kLinksPageGap, TR(L"加群链接"), kColorPageTitle,
                      TR(L"本群仍然有效的加群链接。作废后链接立即失效，已经通过它加入的成员不受影响"));

    if (AppendLoadStatus(page.get(), model.m_linksState, TR(L"正在加载加群链接…"), TR(L"加群链接没有加载出来，请稍后重试。")))
    {
        return std::unique_ptr<DuiControl>(page.release());
    }
    if (model.m_links.empty())
    {
        page->AddChild(MakeText(TR(L"还没有有效的加群链接。在主面板群列表的右键菜单里点「生成加群链接」即可生成。"),
                                kGmTextBody, kColorSubtitle, DT_LEFT),
                       Hint().Fixed(GmScale(40)).Margin(0, GmScale(14), 0, 0));
        return std::unique_ptr<DuiControl>(page.release());
    }
    const size_t count = (model.m_links.size() < (size_t)kGroupLinkRowMax) ? model.m_links.size() : (size_t)kGroupLinkRowMax;
    for (size_t i = 0; i < count; ++i)
    {
        page->AddChild(MakeLinkRow(model.m_links[i], (int)i),
                       Hint().Fixed(GmScale(kLinkRowH)).Margin(0, 0, 0, GmScale(kLinkRowGap)));
    }
    return std::unique_ptr<DuiControl>(page.release());
}

// =====================================================================
// 群机器人页（docs/机器人方案.md §14.9）
// =====================================================================

std::unique_ptr<DuiControl> BuildBotsPage(const GroupManageModel& model)
{
    // 本页的页面间距（96 DPI 下的像素）
    const int kBotsPageGap = 8;
    // 机器人行与行之间的间距（像素）
    const int kBotRowGap = 2;
    // 「添加与管理机器人」按钮的宽、高（96 DPI 下的像素）
    const int kManageBtnW = 160;
    const int kManageBtnH = 32;
    // 本页最多显示的行数
    const size_t kMaxRows = (size_t)(kIdBotRowMax - kIdBotRowBase + 1);

    std::unique_ptr<DuiVBox> page = MakeWidthLimitedPage();
    page->SetPadding(GmScale(24), GmScale(16), GmScale(24), GmScale(18));
    page->SetGap(GmScale(kBotsPageGap));
    AddPageTitleBlock(page.get(), kBotsPageGap, TR(L"群机器人"), kColorPageTitle,
                      TR(L"本群可用的机器人。群机器人由群主添加与管理，企业机器人由企业管理员配置"));

    if (AppendLoadStatus(page.get(), model.m_botsState, TR(L"正在加载本群的机器人…"),
                         TR(L"本群的机器人没有加载出来，请稍后重试。")))
    {
        return std::unique_ptr<DuiControl>(page.release());
    }
    if (model.m_botRows.empty())
    {
        std::unique_ptr<GmText> empty = MakeText(TR(L"本群还没有机器人"), kGmTextBody, kColorSubtitle, DT_LEFT);
        empty->SetCtrlId(kIdBotsEmpty);
        page->AddChild(std::unique_ptr<DuiControl>(empty.release()), Hint().Fixed(GmScale(kLoadStatusHeight)));
    }
    for (size_t i = 0; i < model.m_botRows.size() && i < kMaxRows; ++i)
    {
        //行控件 id 换成本页的取值段：点了打开群机器人管理窗
        balloonchatwnd::GroupBotSectionRow row = model.m_botRows[i];
        row.m_ctrlId = kIdBotRowBase + (UINT)i;
        page->AddChild(balloonchatwnd::BuildGroupBotRow(row),
                       Hint().Fixed(balloonchatwnd::kBotSectionRowHeight).Margin(0, 0, 0, kBotRowGap));
    }

    std::unique_ptr<DuiHBox> btnRow(new DuiHBox());
    btnRow->AddChild(MakeButton(TR(L"添加与管理机器人"), kGmBtnPrimary, kIdBtnBotsManage, kGmTextBody),
                     Hint().Fixed(GmScale(kManageBtnW)));
    btnRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    page->AddChild(std::unique_ptr<DuiControl>(btnRow.release()),
                   Hint().Fixed(GmScale(kManageBtnH)).Margin(0, GmScale(kBotsPageGap), 0, 0));
    return std::unique_ptr<DuiControl>(page.release());
}

std::unique_ptr<DuiControl> BuildPermsPage(const GroupManageModel& model)
{
    // 本页的页面间距（96 DPI 下的像素）
    const int kPermsPageGap = 8;

    //限宽页（design/群管理5.png、群管理8.png）
    std::unique_ptr<DuiVBox> page = MakeWidthLimitedPage();
    page->SetPadding(GmScale(24), GmScale(16), GmScale(24), GmScale(18));
    page->SetGap(GmScale(kPermsPageGap));

    AddPageTitleBlock(page.get(), kPermsPageGap, TR(L"群权限"), kColorPageTitle,
                      TR(L"管理成员可做的操作及消息权限"));

    // 群资料还没拉回来或拉取失败：只显示一行提示。若照常显示开关，默认值看上去与真实
    // 设置无异，管理员据此拨动开关还会把错误的值写回服务端。
    if (AppendLoadStatus(page.get(), model.m_groupInfoState,
                         TR(L"正在加载群权限…"),
                         TR(L"群权限加载失败，请关闭本窗口后重新打开。")))
    {
        return std::unique_ptr<DuiControl>(page.release());
    }

    std::unique_ptr<DuiVBox> card = MakeCard(kColorWhite, kCardBorder, kCardCorner);
    card->SetPadding(0, GmScale(6), 0, GmScale(6));
    card->SetGap(0);

    // 加群方式（下拉）。
    std::unique_ptr<DuiComboBox> joinCombo(new DuiComboBox());
    joinCombo->SetCtrlId(kIdComboJoinMethod);
    StyleCombo(joinCombo.get());
    //各项顺序必须与服务端 group_join_mode 一致（0 自由加入、1 需审批、2 禁止）：选中下标在
    //收发两处都直接当作 joinmode 使用，顺序一乱，界面显示与实际设置就对不上
    joinCombo->AddString(TR(L"允许任何人加入"));
    joinCombo->AddString(TR(L"需要验证消息"));
    joinCombo->AddString(TR(L"禁止任何人加入"));
    joinCombo->SetCurSel(model.m_perms.joinMethodIndex);
    card->AddChild(MakePermRow(TR(L"加群方式"), TR(L"控制哪些人可以加入本群"),
                               std::unique_ptr<DuiControl>(joinCombo.release()), 150),
                   Hint().Fixed(GmScale(52)));
    card->AddChild(MakeSeparatorRow(), Hint().Fixed(kSeparatorPx).Margin(GmScale(18), 0, GmScale(18), 0));

    // 谁能 @ 全体成员（下拉）。
    std::unique_ptr<DuiComboBox> atCombo(new DuiComboBox());
    atCombo->SetCtrlId(kIdComboAtAllScope);
    StyleCombo(atCombo.get());
    atCombo->AddString(TR(L"仅群主和管理员"));
    atCombo->AddString(TR(L"所有人"));
    atCombo->SetCurSel(model.m_perms.atAllScopeIndex);
    card->AddChild(MakePermRow(TR(L"谁能 @ 全体成员"), TR(L"控制谁可以发起全体提醒"),
                               std::unique_ptr<DuiControl>(atCombo.release()), 150),
                   Hint().Fixed(GmScale(52)));
    card->AddChild(MakeSeparatorRow(), Hint().Fixed(kSeparatorPx).Margin(GmScale(18), 0, GmScale(18), 0));

    // 新成员查看历史消息（开关）。
    card->AddChild(MakePermRow(TR(L"新成员查看历史消息"),
                               TR(L"新加入的成员是否可以查看入群前的聊天记录"),
                               MakePermSwitch(kIdSwitchHistory,
                                              model.m_perms.newMemberSeeHistory),
                               46),
                   Hint().Fixed(GmScale(52)));
    card->AddChild(MakeSeparatorRow(), Hint().Fixed(kSeparatorPx).Margin(GmScale(18), 0, GmScale(18), 0));

    // 成员邀请好友加群（开关）。
    card->AddChild(MakePermRow(TR(L"成员邀请好友加群"),
                               TR(L"允许普通成员邀请好友，无需管理员审批"),
                               MakePermSwitch(kIdSwitchInvite,
                                              model.m_perms.memberInviteFriend),
                               46),
                   Hint().Fixed(GmScale(52)));
    card->AddChild(MakeSeparatorRow(), Hint().Fixed(kSeparatorPx).Margin(GmScale(18), 0, GmScale(18), 0));

    // 入群欢迎语（开关）。
    card->AddChild(MakePermRow(TR(L"入群欢迎语"),
                               TR(L"新成员加入时由机器人自动发送"),
                               MakePermSwitch(kIdSwitchWelcome,
                                              model.m_perms.welcomeMessage),
                               46),
                   Hint().Fixed(GmScale(52)));

    // 原先欢迎语之后还有一行「消息敏感词过滤」（开关 + 管理词库按钮），随群级敏感词
    // 于 2026-10-02 删除，敏感词改为企业级词库、由管理端统一配置（docs/敏感词方案.md）。

    // 卡片高度 = 行数 * 52 + (行数 - 1) 条分隔 + 上下 padding 各 6。各项分别换算后再相加；
    // 分隔线恒为 1px 细线，不随 DPI 加粗。增删上面的设置行时须同步修改本常量。
    const int kPermRowCount = 5;
    page->AddChild(std::move(card),
                   Hint().Fixed(kPermRowCount * GmScale(52) + (kPermRowCount - 1) * kSeparatorPx
                                + GmScale(6) * 2));

    return std::unique_ptr<DuiControl>(page.release());
}

// =====================================================================
// 危险区域页（design/群管理6.png）
// =====================================================================

// 造一张危险区域操作卡：左侧标题 + 说明，右侧操作按钮。
//   cardBg / cardBorder：卡片底色与描边色（淡红或中性浅灰，见 kDangerCardBg 一组常量）；
//   btnWidth：按钮宽度（96 DPI 下的像素）。
std::unique_ptr<DuiControl> MakeDangerCard(LPCTSTR title, LPCTSTR desc,
                                           LPCTSTR btnText, int btnStyle,
                                           UINT btnId, int btnWidth,
                                           COLORREF cardBg, COLORREF cardBorder)
{
    std::unique_ptr<DuiVBox> card = MakeCard(cardBg, cardBorder, kCardCorner);
    card->SetPadding(GmScale(18), 0, GmScale(18), 0);

    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(GmScale(14));

    std::unique_ptr<DuiVBox> info(new DuiVBox());
    info->SetGap(GmScale(4));
    info->AddChild(MakeText(title, kGmTextCardTitle, kColorName, DT_LEFT),
                   Hint().Fixed(GmScale(22)));
    info->AddChild(MakeMultiText(desc, kGmTextBody, kColorSubtitle),
                   Hint().Fixed(GmScale(36)));
    Hint infoHint;
    infoHint.weight     = 1;
    infoHint.fixedCross = GmScale(60);
    infoHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::move(info), infoHint);

    row->AddChild(MakeButton(btnText, btnStyle, btnId, kGmTextBody),
                  Hint().Fixed(GmScale(btnWidth), GmScale(36)).AlignC(DuiLayout::AlignCenter));

    card->AddChild(std::move(row));
    return std::unique_ptr<DuiControl>(card.release());
}

std::unique_ptr<DuiControl> BuildDangerPage(const GroupManageModel& model)
{
    // 本页的页面间距（96 DPI 下的像素）
    const int kDangerPageGap = 14;

    //限宽页（design/群管理6.png）
    std::unique_ptr<DuiVBox> page = MakeWidthLimitedPage();
    page->SetPadding(GmScale(24), GmScale(16), GmScale(24), GmScale(18));
    page->SetGap(GmScale(kDangerPageGap));

    AddPageTitleBlock(page.get(), kDangerPageGap, TR(L"危险区域"), kColorDanger,
                      TR(L"以下操作不可撤销，请谨慎操作"));

    //转让群主可以再转回来，用中性卡片；后两项不可逆，用淡红卡片
    page->AddChild(MakeDangerCard(TR(L"转让群主"),
                                  TR(L"将群主身份转让给其他成员，转让后你将成为普通成员"),
                                  TR(L"转让群主"), kGmBtnSecondary, kIdBtnTransfer, 96,
                                  kNeutralCardBg, kNeutralCardBorder),
                   Hint().Fixed(GmScale(78)));
    page->AddChild(MakeDangerCard(TR(L"移除全部成员"),
                                  TR(L"移除除群主以外的所有成员，群聊仍然保留"),
                                  TR(L"移除全部成员"), kGmBtnDanger, kIdBtnRemoveAll, 116,
                                  kDangerCardBg, kDangerCardBorder),
                   Hint().Fixed(GmScale(78)));
    const CString dismissDesc = BuildDismissDescription(model.m_groupName);
    page->AddChild(MakeDangerCard(TR(L"解散群聊"), dismissDesc,
                                  TR(L"解散群聊"), kGmBtnDanger, kIdBtnDismiss, 96,
                                  kDangerCardBg, kDangerCardBorder),
                   Hint().Fixed(GmScale(86)));

    return std::unique_ptr<DuiControl>(page.release());
}

} // anonymous namespace

// =====================================================================
// Header
// =====================================================================

namespace {

// 顶栏各部分的尺寸（96 DPI 下的像素，使用前经 GmScale 换算）。
const int kHeaderPadLeft      = 22;   // 左内边距
const int kHeaderPadRight     = 20;   // 右内边距
const int kHeaderAvatarPx     = 32;   // 群头像边长
const int kHeaderAvatarCorner = 8;    // 群头像圆角半径
const int kHeaderAvatarGap    = 12;   // 群头像与群名之间的间距
const int kHeaderItemGap      = 8;    // 群名、角色标签、人数三者之间的间距
const int kHeaderRoleHPad     = 8;    // 角色标签文字左右内边距
const int kHeaderRoleHeight   = 20;   // 角色标签高度

// 量出的文字宽度之外再留的余量（96 DPI 下的像素），避免取整误差让本来放得下的
// 文字被 DT_END_ELLIPSIS 截成省略号。
const int kHeaderTextSlack    = 2;

/**
 *  顶栏容器：横排群头像、群名、角色标签、人数，末尾一个占位控件占掉剩余空间。
 *
 *  与普通 DuiHBox 只差一件事：群名宽度 = min(群名文字宽度, 剩余可用宽度)。群名
 *  放得下时按文字宽度撑开，角色标签与人数紧跟其后；放不下时只收窄群名（末尾显示
 *  省略号），后两项仍完整显示。文字宽度在构建时量好，剩余可用宽度要到排版时才
 *  知道，故覆写 Layout，在排版前把群名的固定宽度算出来。
 */
class GmHeaderRow : public DuiHBox
{
public:
    GmHeaderRow()
        : m_name(NULL)
        , m_nameTextWidth(0)
        , m_nameMarginLeft(0)
        , m_otherWidth(0)
    {
    }

    /**
     *  登记群名控件及排版所需的宽度。
     *    name：          群名控件，须已加入本容器（借用，随本容器析构）；
     *    nameTextWidth： 群名文字宽度（当前 DPI 下的像素，含余量）；
     *    nameMarginLeft：群名左侧额外留的边距（像素），用于让头像与群名的间距
     *                    大于其余各项之间的间距；
     *    otherWidth：    群名以外所有子控件的固定宽度、全部间距、群名左边距与
     *                    左右内边距之和（当前 DPI 下的像素）。
     */
    void SetNameMetrics(DuiControl* name, int nameTextWidth, int nameMarginLeft,
                        int otherWidth)
    {
        m_name           = name;
        m_nameTextWidth  = nameTextWidth;
        m_nameMarginLeft = nameMarginLeft;
        m_otherWidth     = otherWidth;
    }

    void Layout(const RECT& rcAvail) override
    {
        // 本函数里要 SetHint，先挡住它触发的二次排版（见 DuiLayout::LayoutGuard）
        DuiLayout::LayoutGuard guard(*this);
        if (m_name != NULL)
        {
            int avail = (rcAvail.right - rcAvail.left) - m_otherWidth;
            if (avail < 0)
            {
                avail = 0;
            }
            const int width = (m_nameTextWidth < avail) ? m_nameTextWidth : avail;
            SetHint(m_name, Hint().Fixed(width)
                                  .Margin(m_nameMarginLeft, 0, 0, 0)
                                  .AlignC(DuiLayout::AlignCenter));
        }
        DuiHBox::Layout(rcAvail);
    }

private:
    DuiControl* m_name;            // 群名控件（借用，随本容器一起析构）
    int         m_nameTextWidth;   // 群名文字宽度（当前 DPI 下的像素，含余量）
    int         m_nameMarginLeft;  // 群名左边距（像素）
    int         m_otherWidth;      // 群名以外各项宽度 + 间距 + 内边距之和（像素）
};

} // anonymous namespace

std::unique_ptr<DuiControl> BuildHeader(const GroupManageModel& model)
{
    const int padLeft    = GmScale(kHeaderPadLeft);
    const int padRight   = GmScale(kHeaderPadRight);
    const int avatarPx   = GmScale(kHeaderAvatarPx);
    const int itemGap    = GmScale(kHeaderItemGap);
    const int nameMargin = GmScale(kHeaderAvatarGap - kHeaderItemGap);
    const int slack      = GmScale(kHeaderTextSlack);

    std::unique_ptr<GmHeaderRow> header(new GmHeaderRow());
    header->SetPadding(padLeft, 0, padRight, 0);
    header->SetGap(itemGap);

    header->AddChild(MakeGroupAvatar(model, kHeaderAvatarPx, kHeaderAvatarCorner),
                     Hint().Fixed(avatarPx, avatarPx).AlignC(DuiLayout::AlignCenter));

    // 群名：宽度在 GmHeaderRow::Layout 里按剩余空间确定，这里先给文字宽度
    const int nameTextWidth =
        GmMeasureTextWidth(model.m_groupName, kGmTextPageTitle) + slack;
    std::unique_ptr<GmText> name =
        MakeText(model.m_groupName, kGmTextPageTitle, kColorPageTitle, DT_LEFT);
    name->SetCtrlId(kIdHeaderName);
    DuiControl* nameCtrl = name.get();
    header->AddChild(std::unique_ptr<DuiControl>(name.release()),
                     Hint().Fixed(nameTextWidth).Margin(nameMargin, 0, 0, 0)
                           .AlignC(DuiLayout::AlignCenter));

    // 本人角色标签：与成员卡上的角色标签同一套文案与配色
    std::unique_ptr<DuiBadge> role = MakeRoleBadge(model.m_myRole);
    role->SetCtrlId(kIdHeaderRole);
    const int roleWidth = GmMeasureTextWidth(role->GetText(), kGmTextCaption)
                        + GmScale(kHeaderRoleHPad) * 2;
    header->AddChild(std::unique_ptr<DuiControl>(role.release()),
                     Hint().Fixed(roleWidth, GmScale(kHeaderRoleHeight))
                           .AlignC(DuiLayout::AlignCenter));

    // 成员人数。只在成员已拉回来时显示：开窗时成员尚未到达，此时显示"0 名成员"
    // 是错误信息，故整项不建；应答到达后整窗重建，人数随之出现在角色标签右侧，
    // 前面几项的位置不变。
    int metaWidth = 0;
    const bool showMeta = (model.m_membersState == kLoadDone);
    if (showMeta)
    {
        CString meta;
        meta.Format(TR(L"· %d 名成员"), model.MemberCount());
        metaWidth = GmMeasureTextWidth(meta, kGmTextCaption) + slack;
        std::unique_ptr<GmText> metaText = MakeText(meta, kGmTextCaption, kColorSubtitle, DT_LEFT);
        metaText->SetCtrlId(kIdHeaderMeta);
        header->AddChild(std::unique_ptr<DuiControl>(metaText.release()),
                         Hint().Fixed(metaWidth).AlignC(DuiLayout::AlignCenter));
    }

    // 末尾占位：占掉剩余空间，前面几项因此靠左紧挨着排
    header->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));

    // 群名以外的占用：内边距 + 头像 + 标签 + 人数 + 各段间距 + 群名左边距。
    // 显示人数时有 4 段间距（头像-群名、群名-标签、标签-人数、人数-占位），
    // 不显示人数时少了「标签-人数」这一段，为 3 段。
    const int kHeaderGapCountWithMeta    = 4;
    const int kHeaderGapCountWithoutMeta = 3;
    const int gapCount = showMeta ? kHeaderGapCountWithMeta : kHeaderGapCountWithoutMeta;
    const int otherWidth = padLeft + padRight + avatarPx + roleWidth + metaWidth
                         + itemGap * gapCount + nameMargin;
    header->SetNameMetrics(nameCtrl, nameTextWidth, nameMargin, otherWidth);

    return std::unique_ptr<DuiControl>(header.release());
}

// =====================================================================
// 左侧导航
// =====================================================================

namespace {

// 导航栏左右内边距（96 DPI 下的像素）
const int kNavPadX = 12;

// 导航栏上下内边距（96 DPI 下的像素）
const int kNavPadY = 14;

// 造一个导航分组标题（灰色小标题，如「成员」「设置」）。
std::unique_ptr<DuiControl> MakeNavGroupTitle(LPCTSTR text)
{
    return std::unique_ptr<DuiControl>(
        MakeText(text, kGmTextCaption, kNavGroupTitleColor, DT_LEFT).release());
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
    nav->SetBgColor(kNavBg);
    nav->SetPadding(GmScale(kNavPadX), GmScale(kNavPadY), GmScale(kNavPadX), GmScale(kNavPadY));
    nav->SetGap(GmScale(2));

    // 按登录者角色决定哪些页出现在导航里：普通成员看不到审批 / 禁言 / 权限，
    // 管理员看不到危险区域。不可见的页整项不构建 —— 灰着显示只会让人反复去点。
    const int myRole = model.m_myRole;

    // 分组：成员。成员管理页对所有角色都可见，故本分组标题恒显示。
    nav->AddChild(MakeNavGroupTitle(TR(L"成员")), Hint().Fixed(GmScale(22)));
    nav->AddChild(MakeNavItem(kPageMembers, kGmIconMembers, TR(L"成员管理"),
                              false, model.MemberCount(), false),
                  Hint().Fixed(GmScale(36)));
    if (IsPageVisibleForRole(kPageApproval, myRole))
    {
        nav->AddChild(MakeNavItem(kPageApproval, kGmIconApproval, TR(L"加群审批"),
                                  false, model.PendingRequestCount(), true),
                      Hint().Fixed(GmScale(36)));
    }
    if (IsPageVisibleForRole(kPageMute, myRole))
    {
        nav->AddChild(MakeNavItem(kPageMute, kGmIconMute, TR(L"禁言管理"),
                                  false, model.MutedCount(), false),
                      Hint().Fixed(GmScale(36)));
    }
    //加群链接：与成员进出相关，放在「成员」组（docs/邀请入群方案.md §12）
    if (IsPageVisibleForRole(kPageLinks, myRole))
    {
        nav->AddChild(MakeNavItem(kPageLinks, kGmIconApproval, TR(L"加群链接"),
                                  false, 0, false),
                      Hint().Fixed(GmScale(36)));
    }

    // 分组：群公告。对照 design/群管理7.png，它在导航里自成一组（而不是并进
    // 「设置」组）—— 公告是日常要反复用的功能，跟"改群名""设权限"这类一次性
    // 配置不是一回事。整组仅群主 / 管理员可见，故组标题也跟着一起隐藏。帖子群没有群公告，整组都不出现。
    if (IsAnnouncementPageVisible(myRole, model.m_isThreadGroup))
    {
        nav->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(GmScale(10)));
        nav->AddChild(MakeNavGroupTitle(TR(L"群公告")), Hint().Fixed(GmScale(22)));
        //徽章数用总条数（服务端给的 total），不是本页条数 —— 分页后本地只留一页
        //导航项的「群公告」「群权限」用带上下文的 TRC：分组标题已写明是群，译文可取更短的写法，
        //免得非简体中文界面下导航栏为最长一项加宽太多、挤窄右侧内容（多语言第 4 期）
        nav->AddChild(MakeNavItem(kPageAnnouncement, kGmIconAnnounce, TRC(L"群管理窗左侧导航", L"群公告"),
                                  false, model.m_announcementTotal, false),
                      Hint().Fixed(GmScale(36)));
    }

    // 间隔。
    nav->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(GmScale(10)));

    // 分组：设置。基本信息对所有角色可见，故本分组标题同样恒显示。
    nav->AddChild(MakeNavGroupTitle(TR(L"设置")), Hint().Fixed(GmScale(22)));
    nav->AddChild(MakeNavItem(kPageBasicInfo, kGmIconInfo, TR(L"基本信息"),
                              false, 0, false),
                  Hint().Fixed(GmScale(36)));
    if (IsPageVisibleForRole(kPagePerms, myRole))
    {
        nav->AddChild(MakeNavItem(kPagePerms, kGmIconLock, TRC(L"群管理窗左侧导航", L"群权限"),
                                  false, 0, false),
                      Hint().Fixed(GmScale(36)));
    }
    //群机器人：只有群主，且企业打开机器人、插件已加载时才有（2026-10-08 用户确认，docs/机器人方案.md §14.9）
    if (IsBotsPageVisible(myRole, model.m_botsAvailable))
    {
        nav->AddChild(MakeNavItem(kPageBots, kGmIconBot, TR(L"群机器人"), false, 0, false),
                      Hint().Fixed(GmScale(36)));
    }
    if (IsPageVisibleForRole(kPageDanger, myRole))
    {
        nav->AddChild(MakeNavItem(kPageDanger, kGmIconDanger, TR(L"危险区域"),
                                  true, 0, false),
                      Hint().Fixed(GmScale(36)));
    }

    // 余下空间。
    nav->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));

    return std::unique_ptr<DuiControl>(nav.release());
}

int MeasureNavRequiredWidth(const DuiControl* nav)
{
    if (nav == NULL)
    {
        return 0;
    }
    int maxItemW = 0;
    const std::vector<std::unique_ptr<DuiControl> >& kids = nav->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const GroupManageNavItem* item = dynamic_cast<const GroupManageNavItem*>(kids[i].get());
        if (item == NULL)
        {
            continue;
        }
        //这四项带计数徽章，计数可能稍后才到达或随时变化，按两位数预留徽章宽度
        const int pageId = (int)item->GetCtrlId();
        const bool reserveBadge = (pageId == kPageMembers || pageId == kPageApproval
                                   || pageId == kPageMute || pageId == kPageAnnouncement);
        const int itemW = item->MeasureRequiredWidth(reserveBadge);
        if (itemW > maxItemW)
        {
            maxItemW = itemW;
        }
    }
    return maxItemW + GmScale(kNavPadX) * 2;
}

// =====================================================================
// 页面分发
// =====================================================================

void RefilterMembersPage(DuiVBox* page, const GroupManageModel& model)
{
    if (page == NULL)
    {
        return;
    }
    //成员页根容器的第一个子控件是搜索栏，其余全部摘掉。先复制指针清单再逐个摘，
    //避免一边遍历一边改动子控件数组
    std::vector<DuiControl*> stale;
    const std::vector<std::unique_ptr<DuiControl> >& kids = page->Children();
    for (size_t i = 1; i < kids.size(); ++i)
    {
        stale.push_back(kids[i].get());
    }
    for (size_t i = 0; i < stale.size(); ++i)
    {
        page->RemoveChild(stale[i]);
    }
    AppendMembersPageBodyImpl(page, model);

    //增删子控件不会触发重新排版，而页面矩形没变时 SetRect 会直接返回，所以必须用
    //ForceLayout，否则新追加的成员卡没有排过版、显示为空白（开发约定文档 UI 约定第 5 条）
    page->ForceLayout(page->GetRect());
}

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
    case kPageAnnouncement:
        return BuildAnnouncementPage(model);
    //加群链接页（docs/邀请入群方案.md §12）
    case kPageLinks:
        return BuildLinksPage(model);
    //群机器人页（docs/机器人方案.md §14.9）
    case kPageBots:
        return BuildBotsPage(model);
    default:
        break;
    }

    // 未知页：返回一个空白容器，避免空指针。
    return std::unique_ptr<DuiControl>(new DuiControl());
}

// =====================================================================
// 表单控件样式判定
// =====================================================================

bool IsGmStyledInput(const balloonwjui::DuiControl* ctrl)
{
    return dynamic_cast<const GmEdit*>(ctrl) != NULL
        || dynamic_cast<const GmSearchBox*>(ctrl) != NULL;
}

// =====================================================================
// 危险区域：解散说明
// =====================================================================

CString BuildDismissDescription(const CString& groupName)
{
    // 群名最多保留的字数；再长就截断加省略号，免得说明在卡片里排到第三行
    const int kMaxNameChars = 16;

    CString name = groupName;
    if (name.GetLength() > kMaxNameChars)
    {
        name = name.Left(kMaxNameChars) + _T("…");
    }
    CString desc;
    desc.Format(TR(L"解散后 %s 的所有成员将被移出，成员本地的聊天记录随之清除，此操作不可恢复"),
                (LPCTSTR)name);
    return desc;
}

// =====================================================================
// 窗口尺寸
// =====================================================================

namespace {

// 窗口客户区的设计尺寸与最小尺寸（96 DPI 下的像素）。
const int kClientDesignWidth  = 960;
const int kClientDesignHeight = 660;
const int kClientMinWidth     = 820;
const int kClientMinHeight    = 560;

// 窗口初始尺寸与屏幕工作区边缘之间至少留出的距离（96 DPI 下的像素），
// 避免窗口一打开就贴满整个屏幕。
const int kWorkAreaMargin = 20;

// 把 value 限制在 [lower, upper] 之内；两者矛盾（upper < lower）时以 lower 为准。
int ClampWithFloor(int value, int lower, int upper)
{
    if (value > upper)
    {
        value = upper;
    }
    if (value < lower)
    {
        value = lower;
    }
    return value;
}

} // anonymous namespace

SIZE ComputeMinClientSize(int dpi)
{
    SIZE sz;
    sz.cx = balloonwjui::DuiDpi::Scale(kClientMinWidth, dpi);
    sz.cy = balloonwjui::DuiDpi::Scale(kClientMinHeight, dpi);
    return sz;
}

SIZE ComputeInitialClientSize(int dpi, const RECT& workArea)
{
    const SIZE minSize = ComputeMinClientSize(dpi);
    const int  margin  = balloonwjui::DuiDpi::Scale(kWorkAreaMargin, dpi);

    // 工作区扣掉四周边距后还能给多大
    const int availWidth  = (workArea.right - workArea.left) - margin * 2;
    const int availHeight = (workArea.bottom - workArea.top) - margin * 2;

    SIZE sz;
    sz.cx = ClampWithFloor(balloonwjui::DuiDpi::Scale(kClientDesignWidth, dpi),
                           minSize.cx, availWidth);
    sz.cy = ClampWithFloor(balloonwjui::DuiDpi::Scale(kClientDesignHeight, dpi),
                           minSize.cy, availHeight);
    return sz;
}

CString RequestMessageLineText(const CString& message, bool* placeholder)
{
    //半角空格、制表符、回车换行与全角空格（U+3000）都算空白
    CString trimmed = message;
    trimmed.Trim(L" \t\r\n\x3000");
    const bool empty = trimmed.IsEmpty();
    if (placeholder != NULL)
    {
        *placeholder = empty;
    }
    if (empty)
    {
        return CString(TR(L"未填写验证消息"));
    }
    CString quoted;
    quoted.Format(_T("“%s”"), (LPCTSTR)message);
    return quoted;
}

void CollectPageTexts(const balloonwjui::DuiControl* root, std::vector<CString>& out)
{
    if (root == NULL)
    {
        return;
    }
    const GmText* text = dynamic_cast<const GmText*>(root);
    if (text != NULL)
    {
        out.push_back(text->GetText());
    }
    const std::vector<std::unique_ptr<DuiControl> >& kids = root->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        CollectPageTexts(kids[i].get(), out);
    }
}

} // namespace balloongroupmanage
