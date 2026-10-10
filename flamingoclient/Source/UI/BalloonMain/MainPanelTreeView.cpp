/**
 *  MainPanelTreeView.cpp
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-28
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖(等价 NewChatDemo/stdafx.h) ----
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

#include <string>
#include <vector>

#include "MainPanelTreeView.h"
#include "TextWidthCache.h"        // 右侧文字宽度的测量结果缓存
#include "BadgeRender.h"           // FormatBadgeText / ComputeBadgeWidth / ComputeBadgeRect
#include "MainPanelIcons.h"        // DrawLineIcon / kIconBellSlash / kIconDrawer / kIconBlock（提醒模式徽章）
#include "ConversationRowLayout.h" // LayoutConvRowSecondLine / FitTextWithEllipsis：会话行第二行的排布与摘要截断
#include "PresenceBadge.h"         // DrawPresenceBadge：头像右下角在线状态角标的字形与配色
#include "../../net/Protocol.h"     // enum conversation_notify_mode
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"   // 取基类右侧文字默认字体(画时间/量宽)
#include "../../Common/I18n/I18n.h"   // i18n::UiFontName：当前界面语言的界面字体
#include "../BalloonChatWnd/ChatIcons.h"   // DrawChatIcon：「通话中」胶囊里的电话 / 摄像机图标
#include "../BalloonChatWnd/GroupNotifyOnly.h"   // NotifyOnlyBadgeText：「仅通知」标签的文字（docs/群管理接入方案.md §14）

namespace balloonmain {

namespace {

// ---- 未读红点配色 / 几何（与 MainPanelTab / MainPanelListItem 角标同口径）----

// 未读角标红 #FF4D4F。
const Gdiplus::Color kBadgeRed(255, 0xFF, 0x4D, 0x4F);
// 角标数字色（白）。
const COLORREF kBadgeTextColor = RGB(255, 255, 255);
// 红点圆 / 胶囊高度（直径，逻辑像素）。
const int kBadgeDiameter = 16;
// 每多一位数字，胶囊横向加宽的逻辑像素（与 MainPanelTab 同口径）。
const int kBadgeExtraPerChar = 7;
// 「未读小红点」的直径（逻辑像素）：会话被手动标为未读、又没有未读数时画的不带数字的圆点，
// 比数字胶囊小一圈，一眼能与「有 1 条未读」区分开（docs/会话标为未读方案.md）。
const int kUnreadDotDiameter = 10;
// 红点右端内距：对齐 DuiTreeView 单列右端 padding（其私有常量 kRightPad=8），
// 使红点右边缘与上排时间右边缘对齐。
const int kBadgeRightPad = 8;
// 与 DuiTreeView 单列绘制里的私有常量 kCellPad=6 同值：基类在头像与名字之间、名字列与右侧文字之间
// 都留这么宽。本类据此从名字起点推出头像右边界（在线状态角标），并推出名字列的右边界（会话行摘要的上限）。
const int kBaseCellPad = 6;
// 红点垂直中心相对行中线 mid 的下移量（逻辑像素）：基类把"预览"副标签按
// DT_TOP 画在 [mid, bottom]，其文字中心约在 mid + 副标签半字高处，这里取 6 让
// 红点与预览同高。
const int kBadgePreviewCenterOffset = 6;
// 角标数字字号（逻辑像素）。
const int kBadgeTextPx = 10;

// ---- 置顶 / 免打扰标记的配色与几何 ----

// 置顶行的覆盖色：低透明度的黑（alpha 26/255 ≈ 10%）。压在文字与头像上不至于让它们
// 发灰，只让整行比旁边的行"重"一点。原先取 14（≈5.5%）在浅色背景上几乎看不出来，故
// 提到 26；再往上加就开始明显影响文字对比度了（本控件的覆盖是画在内容<u>之上</u>的，
// 见 OnPaint 说明）。底色只是辅助，置顶的主要标记是下面那根左侧强调竖条。
const Gdiplus::Color kPinnedRowTint(26, 0, 0, 0);

// ---- 置顶行左侧强调竖条 ----

// 竖条颜色：品牌橙 #FF7A45（与头像调色板第 0 槽、选中态描边同一个橙，见 AvatarPalette.h）。
// 不透明 —— 它是置顶的主要视觉标记，压在行左边缘的空白上，不与任何文字重叠。
const Gdiplus::Color kPinnedAccentColor(255, 0xFF, 0x7A, 0x45);

// 竖条宽度（逻辑像素）。3px 在 320 宽的面板里足够醒目又不显得笨重。
const int kPinnedAccentWidth = 3;

// 竖条高度占行高的百分比。取一半：短于行高才看得出是"标记"而不是分隔线，
// 上下留白也让连续几行置顶时每行的竖条各自独立、不连成一整条。
const int kPinnedAccentHeightPercent = 50;

// 竖条圆角半径（逻辑像素）。取宽度的一半即两端半圆，呈胶囊状。
const Gdiplus::REAL kPinnedAccentRadius = (Gdiplus::REAL)kPinnedAccentWidth / 2.0f;

// 免打扰会话的未读胶囊底色（中灰 #9AA0A6）。保留数字但不再用醒目的红——"有新消息，
// 但不必打扰你"。灰度取到能让白色数字仍然清晰的程度。
const Gdiplus::Color kBadgeMutedGray(255, 0x9A, 0xA0, 0xA6);

// 静音图标边长（逻辑像素）。与未读胶囊同排，故比胶囊直径略小。
// 「特别关注」星标：画在主标签文字之后（会话行版式画在第二行），金色实心五角星。
// 12px 与静音图标、在线状态角标的圆底大致同尺寸，一行里几个装饰视觉重量才一致。
const int kStarIconSize = 12;
// 星与名字末尾之间的间距。留 4px：贴太紧会像名字的一部分，拉太开又不知道它属于谁。
const int kStarIconGap = 4;
// 金色 #F5A623。与未读的红 #FF4D4F、静音的灰 #9AA0A6 都拉得开；白底、悬停浅蓝
// #E8F0FC、选中品牌蓝 #2D6CDF 三种行底色上都够亮，故选中态不必另换一套颜色。
const Gdiplus::Color kStarIconColor(255, 0xF5, 0xA6, 0x23);

// 「我的收藏」徽章：画在名字（有星标时在星标）之后的实心书签。尺寸与星标相同，
// 一行里几个装饰视觉重量一致。
const int kFavoriteIconSize = 12;
// 徽章与它左边的内容（名字末尾或星标）之间的间距，理由同 kStarIconGap。
const int kFavoriteIconGap = 4;
// 玫红 #EC4899。与星标的金、未读的红 #FF4D4F、置顶竖条的橙 #FF7A45、在线状态的绿
// 都拉得开；在白底、悬停浅蓝 #E8F0FC、选中品牌蓝 #2D6CDF 三种行底色上都醒目
//（蓝色系的颜色在选中行上会看不清，故不取）。
const Gdiplus::Color kFavoriteIconColor(255, 0xEC, 0x48, 0x99);

// 「字幕模式」徽章（docs/聊天字幕模式方案.md 第 1 节第 13 条）：这个会话的桌面字幕开着时，画在名字（有星标、
// 收藏徽章时在它们）之后、「通话中」胶囊之前；图形与聊天窗头部的「字幕模式」按钮相同（ChatIcons 的
// kChatIconSubtitle）。尺寸与间距同收藏徽章，一行里几个装饰视觉重量一致。
const int kSubtitleIconSize = 12;
const int kSubtitleIconGap = 4;
// 蓝 #3B82F6（用户 2026-10-05 选定）。选中行的底色是品牌蓝 #2D6CDF，蓝色徽章在上面看不清，选中行改画白色
const Gdiplus::Color kSubtitleIconColor(255, 0x3B, 0x82, 0xF6);
const Gdiplus::Color kSubtitleIconSelectedColor(255, 0xFF, 0xFF, 0xFF);
// 图标描边宽度（以 viewBox 24 为基准）：12 像素下约 1.3 像素，比头部按钮的 2 粗一点，缩小后线条仍清楚
const Gdiplus::REAL kSubtitleIconStroke = 2.6f;

// 「通话中」胶囊（docs/音视频通话优化方案.md 第 3 项）：画在名字（有星标、收藏徽章时在它们）之后。
// 胶囊高 16，与未读胶囊同高；左右内边距 6；图标边长 10；图标与文字间距 3；与左边内容的间距 6
//（比星标、收藏徽章的 4 宽一点：它带文字，贴太近会像名字的一部分）。
const int kInCallCapsuleH = 16;
const int kInCallPadX = 6;
const int kInCallIconSize = 10;
const int kInCallIconTextGap = 3;
const int kInCallGap = 6;
// 绿 #1FA85C 底、白色图标与文字。与在线状态的绿 #12B76A 相近但更深，白字在上面清晰；
// 在白底、悬停浅蓝、选中品牌蓝三种行底色上都醒目
const Gdiplus::Color kInCallFill(255, 0x1F, 0xA8, 0x5C);
const Gdiplus::Color kInCallInk(255, 0xFF, 0xFF, 0xFF);
// 图标描边宽度（以 viewBox 24 为基准，与聊天窗头部按钮同为 2）
const Gdiplus::REAL kInCallIconStroke = 2.0f;
// 胶囊里文字的字号相对默认字体缩小的比例：默认字体是名字的字号，胶囊里写同样大的字会喧宾夺主
const Gdiplus::REAL kInCallTextScale = 0.85f;

// 「仅通知」标签（docs/群管理接入方案.md §14）：灰底、深灰字，高度、内边距、与前一个标记的间距、文字缩放都与
// 「通话中」胶囊相同，只是没有图标、颜色是中性的灰（它说明的是群的状态，不需要醒目）
const Gdiplus::Color kNotifyOnlyFill(255, 0xEC, 0xEC, 0xEC);
const Gdiplus::Color kNotifyOnlyInk(255, 0x80, 0x80, 0x80);
// 媒体类型：与 IMediaHost::OnCallPresence 的 media 取值一致
const int kInCallMediaVideo = 2;

// 提醒模式徽章（铃铛加斜线 / 抽屉柜 / 禁止符）的边长（逻辑像素）。与未读胶囊同排，故比胶囊
// 直径略小；与星标、收藏徽章同为 12，一行里几个装饰视觉重量一致。
const int kMuteIconSize = 12;
// 徽章与其右侧未读胶囊之间的横向间距（逻辑像素）。
const int kMuteIconGap = 4;
// 徽章的描边宽度，以 MainPanelIcons 的 viewBox 24 为基准（与菜单图标同口径）。
const Gdiplus::REAL kMuteIconStroke = 2.0f;

// ---- 会话行版式的第二行（docs/消息提醒模式与消息盒子方案.md 第 13 节）----

// 第二行各项之间、摘要与右侧徽章区之间的统一间距（逻辑像素）。取提醒模式图标与未读胶囊之间原有的间距，
// 这两项的位置因此与改版前完全相同（它们仍由 OnPaint 的右侧列那一段按 kMuteIconGap 摆放）。
const int kSecondLineGap = kMuteIconGap;
// 摘要至少保留的宽度（逻辑像素），约三个汉字加省略号。徽章区要挤占这个宽度时，先省略低优先级的徽章。
const int kConvSummaryMinWidth = 48;
// 摘要为空时写进基类副标签的占位文字：一个空格。基类只有副标签非空才按两行排版，名字才会留在上半行与时间同高。
const wchar_t kEmptySummaryPlaceholder[] = L" ";

// ---- 头像右下角的在线状态角标 ----

// 角标字形的绘制方框边长（逻辑像素）。状态徽标的圆底占方框的 20/24，12 对应直径 10 的圆底，
// 与原来行右端徽标的圆底一样大；再小时钟指针会糊成一团（2026-08-12 实拍确认）。
const int kPresenceCornerBox = 12;
// 圆底直径占绘制方框的比例：MainPanelIcons 里状态徽标圆底半径 10，viewBox 24。
const Gdiplus::REAL kPresenceDiscRatio = 20.0f / 24.0f;
// 圆底外面描边的宽度（逻辑像素），把角标与头像本身的颜色隔开，与搜索下拉里头像状态点的白描边同宽。
const Gdiplus::REAL kPresenceCornerRingW = 2.0f;
// 角标圆心离头像右边、下边的距离（逻辑像素）。取 5：36 与 32 两种头像上，圆心都大致落在圆形头像轮廓的
// 右下 45° 处，角标一半压在头像上、一半在外，看得出属于这个头像。
const int kPresenceCornerInset = 5;
// 描边颜色：普通行用白色；选中行改用选中底色品牌蓝 #2D6CDF（DuiTreeView 默认的选中底色，主面板三棵树
// 都没有改它），角标在蓝底上看起来是从头像上挖出的缺口，不会多出一圈白边。
const Gdiplus::Color kPresenceRingColor(255, 0xFF, 0xFF, 0xFF);
const Gdiplus::Color kPresenceRingSelectedColor(255, 0x2D, 0x6C, 0xDF);

// 某个在线状态要不要在头像右下角画角标：只有离开、忙碌两种画；在线不画，离线由头像灰度表示。
bool PresenceHasCorner(int presence)
{
    return presence == presence_away || presence == presence_busy;
}

// 某种提醒模式在行右下角画哪个徽章；不画徽章的模式（允许提醒、特别关注、不认识的值）返回 -1。
int NotifyModeIconKind(int notifyMode)
{
    switch (notifyMode)
    {
    //接收消息但不提醒；只提醒 @我和关注的人平时同样不提醒，沿用同一个徽章
    case notify_mode_silent:
    case notify_mode_mention_follow:
        return kIconBellSlash;

    //收进消息盒子且不提醒
    case notify_mode_msgbox:
        return kIconDrawer;

    //屏蔽消息（仅群聊）
    case notify_mode_block:
        return kIconBlock;

    //允许提醒、特别关注（星标画在名字后面），以及不认识的值：不画
    default:
        return -1;
    }
}

// 向 path 添加一个圆角矩形（四段 90° 圆弧 + 闭合）。radius == 高/2 且为正方形时
// 即为正圆；宽 > 高时呈横向胶囊。半径会被 clamp 到不超过宽 / 高的一半。
void AddRoundRectPath(Gdiplus::GraphicsPath& path,
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

// 用 GDI 在矩形内居中绘制角标数字 —— DrawText 经系统 ClearType 光栅化，清晰锐利。
void DrawBadgeText(HDC hdc, LPCWSTR text, const RECT& rc)
{
    if (text == NULL || text[0] == L'\0')
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -kBadgeTextPx, 0, 0, 0,
        FW_SEMIBOLD,
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
    COLORREF oldColor  = ::SetTextColor(hdc, kBadgeTextColor);

    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

// 用屏幕 DC 量一段文字在指定字体下的像素宽（TextWidthCache 的测量函数）。
// 与窗口是否已创建无关。font 为 NULL 时用屏幕 DC 的默认字体；取不到 DC 返回 0。
int MeasureWithScreenDc(HFONT font, LPCWSTR text, int len)
{
    HDC dc = ::GetDC(NULL);
    if (dc == NULL)
    {
        return 0;
    }
    HGDIOBJ old = (font != NULL) ? ::SelectObject(dc, font) : NULL;
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(dc, text, len, &sz);
    if (old != NULL)
    {
        ::SelectObject(dc, old);
    }
    ::ReleaseDC(NULL, dc);
    return sz.cx;
}

// 进程内共用的右侧文字宽度缓存。三棵树的右侧文字都用基类默认字体，时间文字与占位
// 空格大量重复，量一次即可；不同 DPI 下的默认字体句柄不同，缓存按字体句柄区分。
balloonmain::TextWidthCache& DefaultFontWidthCache()
{
    static balloonmain::TextWidthCache s_cache(MeasureWithScreenDc);
    return s_cache;
}

// 用基类右侧文字所用的默认字体，量一段文字的像素宽（结果经 DefaultFontWidthCache
// 缓存）。取不到返回 0。
//   defaultFont  树控件按自身 DPI 取到的默认字体（MainPanelTreeView::GetDefaultFont）。
int MeasureDefaultTextWidth(HFONT defaultFont, LPCWSTR text)
{
    return DefaultFontWidthCache().Width(defaultFont, text);
}

// 造一串等宽空格，使其在默认字体下的渲染宽 >= widthPx。塞进基类 rightText 占位
// （空格不可见），让基类据此把左侧标题 / 预览裁剪在右侧列左边、不顶进来。
//   defaultFont  同 MeasureDefaultTextWidth。
CString BuildSpacerForWidth(HFONT defaultFont, int widthPx)
{
    if (widthPx <= 0)
    {
        return CString();
    }
    int spaceW = MeasureDefaultTextWidth(defaultFont, L" ");
    if (spaceW <= 0)
    {
        spaceW = 4;   // 取不到字体时退化为经验值（约 4px / 空格）
    }
    int count = widthPx / spaceW + 2;   // 向上取整再多留 1 个，确保够宽
    CString s;
    for (int i = 0; i < count; ++i)
    {
        s += L' ';
    }
    return s;
}

// 用基类同款默认字体 / 灰色，在矩形内右对齐 + 底对齐画上排右侧文字（会话行的时间 /
// 群行的成员数）—— 与基类两行节点的主标签同一垂直带（DT_BOTTOM 贴 mid），从而与
// 左侧的姓名 / 群名同高。
//   defaultFont  树控件按自身 DPI 取到的默认字体，与基类绘制文字所用的相同。
void DrawRightTopText(HDC hdc, HFONT defaultFont, LPCWSTR text, const RECT& rc, COLORREF color)
{
    if (text == NULL || text[0] == L'\0')
    {
        return;
    }
    HFONT f = defaultFont;
    HFONT oldFont = (f != NULL) ? (HFONT)::SelectObject(hdc, f) : NULL;
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, color);

    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r,
                DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);

    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    if (oldFont != NULL)
    {
        ::SelectObject(hdc, oldFont);
    }
}

// 一个待绘红点：外形矩形 + 显示文字 + 是否免打扰（决定底色红 / 灰）。OnPaint 先一遍
// 算好，再分两遍（GDI+ 填底、GDI 写字）绘制，避免在同一 HDC 上交错混用 GDI+ 与 GDI。
struct BadgeDraw
{
    RECT    m_rc;      // 红点外形矩形（客户区坐标）
    CString m_text;    // 红点内显示的数字文字
    bool    m_muted;   // 该会话是否处于不提醒的模式（接收不提醒 / 收进消息盒子 / 屏蔽）：true 用灰底，false 用红底

    BadgeDraw()
        : m_muted(false)
    {
        m_rc.left   = 0;
        m_rc.top    = 0;
        m_rc.right  = 0;
        m_rc.bottom = 0;
    }
};

} // anonymous namespace

/**
 *  会话行版式的一行在一次绘制里的排布结果。PrepareConvRows_ 在基类绘制之前算好，PaintNameMarks 据此
 *  画第二行的标记；只在一次 OnPaint 期间存在。
 */
struct ConvRowPaint
{
    int                     m_id;        // 节点 id
    int                     m_centerY;   // 第二行标记的垂直中心（客户区坐标），与未读胶囊、摘要同高
    bool                    m_selected;  // 这一行是否为选中行（字幕徽章在选中行上画白色）
    ConvRowSecondLineLayout m_layout;    // 第二行各项的横向排布

    ConvRowPaint()
        : m_id(-1)
        , m_centerY(0)
        , m_selected(false)
    {
    }
};

// 类内初始化的静态常量在被按引用使用时（如单元测试的断言）需要一处定义。
const int MainPanelTreeView::kNoPresence;

MainPanelTreeView::MainPanelTreeView()
{
    // 滚轮 / 角标外的默认行为继承自 DuiTreeView,无额外初始化。
}

void MainPanelTreeView::SetItemRightTopText(int id, LPCTSTR text)
{
    CString t = (text != NULL) ? text : _T("");
    if (t.IsEmpty())
    {
        // 空文字不留空条目：只用到这一个能力的树（如群树）在人数全未知时映射恒空，
        // OnPaint 仍能走短路分支。
        m_rightTopText.erase(id);
    }
    else
    {
        m_rightTopText[id] = t;
    }

    UpdateRightReserve_(id);
    Invalidate();
}

void MainPanelTreeView::SetItemRightStacked(int id, LPCTSTR timeText, int badgeCount)
{
    // 先落红点数据：下面的 SetItemRightTopText 会重算右侧预留宽，而预留宽要同时
    // 考虑上排时间与下排红点，红点晚一步写就会按旧值算宽。
    if (badgeCount > 0)
    {
        m_badges[id] = badgeCount;
    }
    else
    {
        m_badges.erase(id);
    }

    // 时间与群成员数走的是同一条上排通路，交给它统一处理（内部会重算预留宽并重绘）。
    SetItemRightTopText(id, timeText);
}

void MainPanelTreeView::SetItemConversationFlags(int id, bool pinned, int notifyMode)
{
    if (pinned)
    {
        m_pinnedRows.insert(id);
    }
    else
    {
        m_pinnedRows.erase(id);
    }

    if (NotifyModeIconKind(notifyMode) >= 0)
    {
        m_notifyModeRows[id] = notifyMode;
    }
    else
    {
        m_notifyModeRows.erase(id);
    }

    // 提醒模式徽章会在未读胶囊左边多出一个图标，右下角占位变宽，须重算预留宽，
    // 否则左侧的预览文字会顶到图标底下。
    UpdateRightReserve_(id);
    Invalidate();
}

void MainPanelTreeView::SetItemNotifyMode(int id, int notifyMode)
{
    const int current = GetItemNotifyMode(id);
    const int target = (NotifyModeIconKind(notifyMode) >= 0) ? notifyMode : (int)notify_mode_allow;
    if (current == target)
    {
        return;   // 状态没变，不必重绘
    }

    if (target == notify_mode_allow)
    {
        m_notifyModeRows.erase(id);
    }
    else
    {
        m_notifyModeRows[id] = target;
    }

    // 理由同 SetItemConversationFlags：徽章宽度计入右侧占位。
    UpdateRightReserve_(id);
    Invalidate();
}

int MainPanelTreeView::GetItemNotifyMode(int id) const
{
    std::map<int, int>::const_iterator it = m_notifyModeRows.find(id);
    if (it == m_notifyModeRows.end())
    {
        return notify_mode_allow;
    }
    return it->second;
}

void MainPanelTreeView::SetItemQuietUnread(int id, bool quiet)
{
    if (IsItemQuietUnread(id) == quiet)
    {
        return;
    }
    if (quiet)
    {
        m_quietRows.insert(id);
    }
    else
    {
        m_quietRows.erase(id);
    }
    Invalidate();
}

bool MainPanelTreeView::IsItemQuietUnread(int id) const
{
    return m_quietRows.count(id) != 0;
}

void MainPanelTreeView::SetItemStarred(int id, bool starred)
{
    const bool had = (m_starredRows.count(id) != 0);
    if (had == starred)
    {
        return;   // 状态没变，不必重绘
    }

    if (starred)
    {
        m_starredRows.insert(id);
    }
    else
    {
        m_starredRows.erase(id);
    }

    // 星标画在名字之后，但它的宽度要计入右侧占位：名字过长时让基类先截断名字、给星
    // 留出位置，否则星会跟在截断处之后压到右侧的时间上。
    UpdateRightReserve_(id);
    Invalidate();
}

void MainPanelTreeView::SetItemFavorite(int id, bool favorite)
{
    const bool had = (m_favoriteRows.count(id) != 0);
    if (had == favorite)
    {
        return;   // 状态没变，不必重绘
    }

    if (favorite)
    {
        m_favoriteRows.insert(id);
    }
    else
    {
        m_favoriteRows.erase(id);
    }

    // 理由同 SetItemStarred：徽章宽度计入右侧占位。
    UpdateRightReserve_(id);
    Invalidate();
}

void MainPanelTreeView::SetItemSubtitle(int id, bool subtitle)
{
    const bool had = (m_subtitleRows.count(id) != 0);
    if (had == subtitle)
    {
        return;   // 状态没变，不必重绘
    }

    if (subtitle)
    {
        m_subtitleRows.insert(id);
    }
    else
    {
        m_subtitleRows.erase(id);
    }

    // 理由同 SetItemStarred：徽章宽度计入右侧占位。
    UpdateRightReserve_(id);
    Invalidate();
}

void MainPanelTreeView::ClearAllRowDecorations()
{
    if (m_rightTopText.empty() && m_badges.empty()
        && m_pinnedRows.empty() && m_notifyModeRows.empty()
        && m_quietRows.empty()
        && m_starredRows.empty() && m_favoriteRows.empty()
        && m_subtitleRows.empty()
        && m_inCallRows.empty() && m_notifyOnlyRows.empty() && m_unreadDotRows.empty()
        && m_summaryRows.empty() && m_presenceRows.empty()
        && m_rightSpacerLen.empty())
    {
        return;
    }
    m_rightTopText.clear();
    m_badges.clear();
    m_pinnedRows.clear();
    m_notifyModeRows.clear();
    m_quietRows.clear();
    m_starredRows.clear();
    m_favoriteRows.clear();
    m_subtitleRows.clear();
    m_inCallRows.clear();
    m_notifyOnlyRows.clear();
    m_unreadDotRows.clear();
    m_summaryRows.clear();
    m_presenceRows.clear();
    m_rightSpacerLen.clear();
    Invalidate();
}

void MainPanelTreeView::SetItemSummary(int id, LPCTSTR text)
{
    const CString summary = (text != NULL) ? text : _T("");
    std::map<int, CString>::const_iterator it = m_summaryRows.find(id);
    const bool wasConvLayout = (it != m_summaryRows.end());
    if (wasConvLayout && it->second == summary)
    {
        return;   // 摘要没变，不必重绘
    }
    m_summaryRows[id] = summary;

    // 先把原文写进基类副标签，使这一行立即按两行排版；绘制前 PrepareConvRows_ 会按当时的宽度换成截断后的文字。
    // 原文为空时写一个空格占位，名字仍在上半行与时间同高
    SetItemSubLabel(id, summary.IsEmpty() ? kEmptySummaryPlaceholder : (LPCTSTR)summary);

    // 第一次变成会话行版式时，名字之后的标记移到第二行，右侧占位只按时间算，须重算
    if (!wasConvLayout)
    {
        UpdateRightReserve_(id);
    }
    Invalidate();
}

CString MainPanelTreeView::GetItemSummary(int id) const
{
    std::map<int, CString>::const_iterator it = m_summaryRows.find(id);
    return (it != m_summaryRows.end()) ? it->second : CString();
}

void MainPanelTreeView::SetItemPresence(int id, int presence)
{
    const int current = GetItemPresence(id);
    const int target = (presence >= 0) ? presence : kNoPresence;
    if (current == target)
    {
        return;   // 状态没变，不必重绘
    }
    if (target == kNoPresence)
    {
        m_presenceRows.erase(id);
    }
    else
    {
        m_presenceRows[id] = target;
    }
    // 角标画在头像上，不占文字宽度，不必重算右侧占位
    Invalidate();
}

int MainPanelTreeView::GetItemPresence(int id) const
{
    std::map<int, int>::const_iterator it = m_presenceRows.find(id);
    return (it != m_presenceRows.end()) ? it->second : kNoPresence;
}

int MainPanelTreeView::UnreadWidth_(int id) const
{
    std::map<int, int>::const_iterator itBadge = m_badges.find(id);
    if (itBadge != m_badges.end() && itBadge->second > 0)
    {
        const CString badgeText = FormatBadgeText(itBadge->second);
        return ComputeBadgeWidth(badgeText.GetLength(), kBadgeDiameter, kBadgeExtraPerChar);
    }
    if (m_unreadDotRows.count(id) != 0)
    {
        return kUnreadDotDiameter;   // 没有未读数时才画小红点
    }
    return 0;
}

int MainPanelTreeView::BaseTextRight_(int id) const
{
    // 与 DuiTreeView 单列绘制同口径：行右端减去右内边距；有右侧文字时再减去它的宽度与一个间距。
    // 本类管理的行，右侧文字就是 UpdateRightReserve_ 写进去的那串空格，按记下的个数量宽即可。
    int right = m_rcItem.right - kBadgeRightPad;
    std::map<int, int>::const_iterator it = m_rightSpacerLen.find(id);
    if (it != m_rightSpacerLen.end() && it->second > 0)
    {
        const CString spacer(L' ', it->second);
        right -= MeasureDefaultTextWidth(GetDefaultFont(), (LPCWSTR)spacer) + kBaseCellPad;
    }
    return right;
}

int MainPanelTreeView::NameMarksWidth_(int id) const
{
    // 会话行版式的标记画在第二行，名字之后不占位
    if (m_summaryRows.count(id) != 0)
    {
        return 0;
    }
    int width = 0;
    if (m_starredRows.count(id) != 0)
    {
        width += kStarIconGap + kStarIconSize;
    }
    if (m_favoriteRows.count(id) != 0)
    {
        width += kFavoriteIconGap + kFavoriteIconSize;
    }
    if (m_subtitleRows.count(id) != 0)
    {
        width += kSubtitleIconGap + kSubtitleIconSize;
    }
    if (m_inCallRows.count(id) != 0)
    {
        width += kInCallGap + InCallCapsuleWidth_();
    }
    if (m_notifyOnlyRows.count(id) != 0)
    {
        width += kInCallGap + NotifyOnlyCapsuleWidth_();
    }
    return width;
}

int MainPanelTreeView::NotifyOnlyCapsuleWidth_() const
{
    const int textW = MeasureDefaultTextWidth(GetDefaultFont(), balloonchatwnd::NotifyOnlyBadgeText());
    //文字与「通话中」胶囊一样缩小绘制，宽度按比例折算后向上取整
    const int scaledW = (int)(textW * kInCallTextScale + 0.99f);
    return kInCallPadX + scaledW + kInCallPadX;
}

void MainPanelTreeView::SetItemNotifyOnly(int id, bool on)
{
    const bool had = (m_notifyOnlyRows.count(id) != 0);
    if (had == on)
    {
        return;   // 状态没变，不必重绘
    }
    if (on)
    {
        m_notifyOnlyRows.insert(id);
    }
    else
    {
        m_notifyOnlyRows.erase(id);
    }
    // 理由同 SetItemStarred：标签宽度计入右侧占位
    UpdateRightReserve_(id);
    Invalidate();
}

void MainPanelTreeView::SetItemUnreadDot(int id, bool on)
{
    const bool had = (m_unreadDotRows.count(id) != 0);
    if (had == on)
    {
        return;   // 状态没变，不必重绘
    }
    if (on)
    {
        m_unreadDotRows.insert(id);
    }
    else
    {
        m_unreadDotRows.erase(id);
    }
    // 红点占下排的位置，须重算右侧预留宽
    UpdateRightReserve_(id);
    Invalidate();
}

int MainPanelTreeView::InCallCapsuleWidth_() const
{
    const int textW = MeasureDefaultTextWidth(GetDefaultFont(), TR(L"通话中"));
    //文字按 kInCallTextScale 缩小绘制，宽度按比例折算后向上取整
    const int scaledW = (int)(textW * kInCallTextScale + 0.99f);
    return kInCallPadX + kInCallIconSize + kInCallIconTextGap + scaledW + kInCallPadX;
}

void MainPanelTreeView::SetItemInCall(int id, int media)
{
    std::map<int, int>::iterator it = m_inCallRows.find(id);
    const int had = (it != m_inCallRows.end()) ? it->second : 0;
    if (had == media)
    {
        return;   // 状态没变，不必重绘
    }
    if (media > 0)
    {
        m_inCallRows[id] = media;
    }
    else
    {
        m_inCallRows.erase(id);
    }
    // 理由同 SetItemStarred：胶囊宽度计入右侧占位
    UpdateRightReserve_(id);
    Invalidate();
}

int MainPanelTreeView::GetItemInCallMedia(int id) const
{
    std::map<int, int>::const_iterator it = m_inCallRows.find(id);
    return (it != m_inCallRows.end()) ? it->second : 0;
}

void MainPanelTreeView::UpdateRightReserve_(int id)
{
    // 上排宽 = 上排右侧文字宽（会话树的时间 / 群树的成员数，用基类默认字体量）。
    int topW = 0;
    std::map<int, CString>::const_iterator itTop = m_rightTopText.find(id);
    if (itTop != m_rightTopText.end())
    {
        topW = MeasureDefaultTextWidth(GetDefaultFont(), (LPCWSTR)itTop->second);
    }

    int reserveW = topW;
    if (m_summaryRows.count(id) == 0)
    {
        // 下排宽 = 未读胶囊宽（按位数算）+ 有提醒模式徽章时的徽章宽与间距。
        int bottomW = UnreadWidth_(id);
        if (m_notifyModeRows.count(id) != 0)
        {
            bottomW += kMuteIconSize;
            if (bottomW > kMuteIconSize)
            {
                bottomW += kMuteIconGap;   // 图标与胶囊之间的间距，只在两者都在时才需要
            }
        }

        // 名字之后的标记（星标 / 收藏徽章等）也要占位：它们跟在名字后面，名字过长时须由
        // 基类先截断名字才放得下。代价是这类行的第二行预览也随之少显示这么宽。
        const int marksW = NameMarksWidth_(id);
        reserveW = ((topW > bottomW) ? topW : bottomW) + marksW;
    }
    // 会话行版式的行只为上排时间让位：名字只按时间截断；第二行的摘要由 PrepareConvRows_ 按徽章区自己截断。

    // 换算成等宽空格塞进基类 rightText，让基类把左侧标题 / 预览裁剪在右侧列左边。
    // 空格个数与上次写入的相同就不再写回：基类 SetItemRightText 要按 id 逐个扫描节点。
    // 比较的是空格个数而不是预留宽度 —— 字体变化后同样的宽度对应的个数会变，照样写回。
    const CString spacer = BuildSpacerForWidth(GetDefaultFont(), reserveW);
    std::map<int, int>::const_iterator itLen = m_rightSpacerLen.find(id);
    if (itLen != m_rightSpacerLen.end() && itLen->second == spacer.GetLength())
    {
        return;
    }
    m_rightSpacerLen[id] = spacer.GetLength();
    SetItemRightText(id, spacer);
}

void MainPanelTreeView::OnDpiChanged(int dpi)
{
    DuiTreeView::OnDpiChanged(dpi);

    // 只有写过占位空格的行需要重算。先把 id 拷出来再逐个重算，因为
    // UpdateRightReserve_ 会改写 m_rightSpacerLen，不能边遍历边改。
    std::vector<int> ids;
    ids.reserve(m_rightSpacerLen.size());
    for (std::map<int, int>::const_iterator it = m_rightSpacerLen.begin();
         it != m_rightSpacerLen.end(); ++it)
    {
        ids.push_back(it->first);
    }
    for (size_t i = 0; i < ids.size(); ++i)
    {
        UpdateRightReserve_(ids[i]);
    }
}

void MainPanelTreeView::DirtyRowRange_(const RECT& rcDirty, int& outFirst, int& outLast) const
{
    outFirst = 0;
    outLast  = -1;
    const int rowHeight    = GetRowHeight();
    const int visibleCount = GetVisibleCount();
    if (rowHeight <= 0 || visibleCount <= 0 || rcDirty.bottom <= rcDirty.top)
    {
        return;
    }

    // 行 r 占 [top + r*h, top + (r+1)*h)，与 DuiTreeView 单列绘制同口径。脏区在列表上方
    // 时减出来是负数，按 0 算；在列表下方时超出可见行数，按最后一行截住。
    const int topOffset    = rcDirty.top - m_rcItem.top;
    const int bottomOffset = rcDirty.bottom - 1 - m_rcItem.top;
    if (bottomOffset < 0)
    {
        return;   // 脏区整个在列表上方
    }
    outFirst = (topOffset > 0) ? (topOffset / rowHeight) : 0;
    outLast  = bottomOffset / rowHeight;
    if (outLast >= visibleCount)
    {
        outLast = visibleCount - 1;
    }
}

void MainPanelTreeView::PrepareConvRows_(HDC hdc, const RECT& rcDirty, std::vector<ConvRowPaint>& outRows)
{
    outRows.clear();
    const int rowHeight = GetRowHeight();
    if (m_summaryRows.empty() || rowHeight <= 0)
    {
        return;   // 没有会话行版式的行（通讯录树、群树）：零开销
    }

    const int rightEdge   = m_rcItem.right - kBadgeRightPad;
    const int inCallW     = m_inCallRows.empty() ? 0 : InCallCapsuleWidth_();
    const int notifyOnlyW = m_notifyOnlyRows.empty() ? 0 : NotifyOnlyCapsuleWidth_();

    // 摘要用基类副标签的字体测量（基类按 SetSubLabelPointSize 的字号画副标签）
    HFONT subFont = GetFontByPointSize(GetSubLabelPointSize(), false);
    HGDIOBJ oldFont = (subFont != NULL) ? ::SelectObject(hdc, subFont) : NULL;

    int firstRow = 0;
    int lastRow  = -1;
    DirtyRowRange_(rcDirty, firstRow, lastRow);
    for (int row = firstRow; row <= lastRow; ++row)
    {
        const int nodeId = GetIdAtVisibleRow(row);
        std::map<int, CString>::const_iterator itSum = m_summaryRows.find(nodeId);
        if (itSum == m_summaryRows.end())
        {
            continue;
        }

        const int rowTop = m_rcItem.top + row * rowHeight;
        const int mid    = rowTop + rowHeight / 2;

        ConvRowSecondLineInput in;
        in.m_right = rightEdge;
        // 摘要的起点就是名字的起点（基类在同一个横坐标画主标签与副标签）。取不到名字矩形（可用宽度已被挤没）时
        // 按没有摘要空间处理：可省略的徽章全部省略，只留必须保留的几项。
        RECT rcLabel;
        in.m_left = GetItemLabelTextRect(nodeId, rcLabel) ? rcLabel.left : rightEdge;
        in.m_summaryCap = BaseTextRight_(nodeId);
        in.m_hasSummary = !itSum->second.IsEmpty();
        in.m_minSummaryWidth = kConvSummaryMinWidth;
        in.m_gap = kSecondLineGap;
        in.m_markWidth[conv_row_mark_star]     = (m_starredRows.count(nodeId) != 0) ? kStarIconSize : 0;
        in.m_markWidth[conv_row_mark_favorite] = (m_favoriteRows.count(nodeId) != 0) ? kFavoriteIconSize : 0;
        in.m_markWidth[conv_row_mark_subtitle] = (m_subtitleRows.count(nodeId) != 0) ? kSubtitleIconSize : 0;
        in.m_markWidth[conv_row_mark_in_call]  = (m_inCallRows.count(nodeId) != 0) ? inCallW : 0;
        in.m_markWidth[conv_row_mark_notify_only] = (m_notifyOnlyRows.count(nodeId) != 0) ? notifyOnlyW : 0;
        std::map<int, int>::const_iterator itMode = m_notifyModeRows.find(nodeId);
        const bool hasModeIcon = (itMode != m_notifyModeRows.end()) && (NotifyModeIconKind(itMode->second) >= 0);
        in.m_notifyIconWidth = hasModeIcon ? kMuteIconSize : 0;
        in.m_unreadWidth = UnreadWidth_(nodeId);

        ConvRowPaint paint;
        paint.m_id       = nodeId;
        paint.m_centerY  = mid + kBadgePreviewCenterOffset;
        paint.m_selected = (GetCurSel() == nodeId);
        paint.m_layout   = LayoutConvRowSecondLine(in);
        outRows.push_back(paint);

        // 摘要截断到摘要矩形的宽度，写进基类副标签；与现有内容相同时不写（写入会请求重画）
        const int summaryW = paint.m_layout.m_summaryRight - paint.m_layout.m_summaryLeft;
        const std::wstring fitted = FitTextWithEllipsis(hdc, std::wstring((LPCWSTR)itSum->second), summaryW);
        const CString subLabel = fitted.empty() ? CString(kEmptySummaryPlaceholder) : CString(fitted.c_str());
        if (GetItemSubLabel(nodeId) != subLabel)
        {
            SetItemSubLabel(nodeId, subLabel);
        }
    }

    if (oldFont != NULL)
    {
        ::SelectObject(hdc, oldFont);
    }
}

void MainPanelTreeView::PaintPresenceCorners_(HDC hdc, const RECT& rcDirty)
{
    const int rowHeight = GetRowHeight();
    const int iconSize  = GetIconSize();
    if (m_presenceRows.empty() || rowHeight <= 0 || iconSize <= 0)
    {
        return;
    }

    // 先收集要画的角标（圆心与状态），再统一开一次 GDI+ 画完
    std::vector<POINT> centers;
    std::vector<int>   presences;
    std::vector<bool>  selected;
    int firstRow = 0;
    int lastRow  = -1;
    DirtyRowRange_(rcDirty, firstRow, lastRow);
    for (int row = firstRow; row <= lastRow; ++row)
    {
        const int nodeId = GetIdAtVisibleRow(row);
        std::map<int, int>::const_iterator it = m_presenceRows.find(nodeId);
        if (it == m_presenceRows.end() || !PresenceHasCorner(it->second) || GetItemIcon(nodeId) == NULL)
        {
            continue;
        }
        // 头像位置按基类的排布推出：头像右边界 = 名字起点 - 间距，头像在行内垂直居中。取不到名字矩形时不画
        RECT rcLabel;
        if (!GetItemLabelTextRect(nodeId, rcLabel))
        {
            continue;
        }
        const int rowTop     = m_rcItem.top + row * rowHeight;
        const int iconRight  = rcLabel.left - kBaseCellPad;
        const int iconTop    = rowTop + (rowHeight - iconSize) / 2;
        const int iconBottom = iconTop + iconSize;
        POINT center;
        center.x = iconRight - kPresenceCornerInset;
        center.y = iconBottom - kPresenceCornerInset;
        centers.push_back(center);
        presences.push_back(it->second);
        selected.push_back(GetCurSel() == nodeId);
    }
    if (centers.empty())
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const Gdiplus::REAL box   = (Gdiplus::REAL)kPresenceCornerBox;
    const Gdiplus::REAL ringR = box * kPresenceDiscRatio / 2.0f + kPresenceCornerRingW;
    for (size_t i = 0; i < centers.size(); ++i)
    {
        const Gdiplus::REAL cx = (Gdiplus::REAL)centers[i].x;
        const Gdiplus::REAL cy = (Gdiplus::REAL)centers[i].y;
        // 先垫一个比圆底大一圈的实心圆作描边，把角标与头像隔开；再画状态字形
        Gdiplus::SolidBrush ring(selected[i] ? kPresenceRingSelectedColor : kPresenceRingColor);
        g.FillEllipse(&ring, cx - ringR, cy - ringR, ringR * 2.0f, ringR * 2.0f);
        DrawPresenceBadge(g, presences[i], false, cx - box / 2.0f, cy - box / 2.0f, box);
    }
}

void MainPanelTreeView::PaintNameMarks(HDC hdc, const RECT& rcDirty, const std::vector<ConvRowPaint>& convRows)
{
    if (m_starredRows.empty() && m_favoriteRows.empty() && m_subtitleRows.empty() && m_inCallRows.empty()
        && m_notifyOnlyRows.empty())
    {
        return;
    }

    // 先按可见行收集要画的矩形，再统一开一次 GDI+ 画完 —— 与下面那套装饰同一路子：
    // Graphics 对象建一次就够，逐行新建既慢又容易与 GDI 交错出问题。
    std::vector<RECT> starRects;
    std::vector<RECT> favoriteRects;
    std::vector<RECT> subtitleRects;      // 「字幕模式」徽章的矩形
    std::vector<bool> subtitleSelected;   // 与 subtitleRects 一一对应：这一行是否为选中行（选中行画白色）
    std::vector<RECT> inCallRects;   // 「通话中」胶囊的矩形
    std::vector<int>  inCallMedia;   // 与 inCallRects 一一对应的媒体类型
    std::vector<RECT> notifyOnlyRects;   // 「仅通知」标签的矩形
    const int markRight = m_rcItem.right - kBadgeRightPad;
    const int capsuleW = m_inCallRows.empty() ? 0 : InCallCapsuleWidth_();
    const int notifyOnlyW = m_notifyOnlyRows.empty() ? 0 : NotifyOnlyCapsuleWidth_();

    // 会话行版式的行：标记画在第二行，位置取自 PrepareConvRows_ 算好的排布，与摘要、未读胶囊同一条中线
    for (size_t i = 0; i < convRows.size(); ++i)
    {
        const ConvRowPaint& paint = convRows[i];
        const ConvRowSecondLineLayout& lay = paint.m_layout;
        const int iconTop    = paint.m_centerY - kStarIconSize / 2;
        const int capsuleTop = paint.m_centerY - kInCallCapsuleH / 2;
        if (lay.m_markLeft[conv_row_mark_star] != kConvRowNotDrawn)
        {
            RECT rc = { lay.m_markLeft[conv_row_mark_star], iconTop,
                        lay.m_markLeft[conv_row_mark_star] + kStarIconSize, iconTop + kStarIconSize };
            starRects.push_back(rc);
        }
        if (lay.m_markLeft[conv_row_mark_favorite] != kConvRowNotDrawn)
        {
            RECT rc = { lay.m_markLeft[conv_row_mark_favorite], iconTop,
                        lay.m_markLeft[conv_row_mark_favorite] + kFavoriteIconSize, iconTop + kFavoriteIconSize };
            favoriteRects.push_back(rc);
        }
        if (lay.m_markLeft[conv_row_mark_subtitle] != kConvRowNotDrawn)
        {
            RECT rc = { lay.m_markLeft[conv_row_mark_subtitle], iconTop,
                        lay.m_markLeft[conv_row_mark_subtitle] + kSubtitleIconSize, iconTop + kSubtitleIconSize };
            subtitleRects.push_back(rc);
            subtitleSelected.push_back(paint.m_selected);
        }
        if (lay.m_markLeft[conv_row_mark_in_call] != kConvRowNotDrawn)
        {
            RECT rc = { lay.m_markLeft[conv_row_mark_in_call], capsuleTop,
                        lay.m_markLeft[conv_row_mark_in_call] + capsuleW, capsuleTop + kInCallCapsuleH };
            inCallRects.push_back(rc);
            std::map<int, int>::const_iterator itCall = m_inCallRows.find(paint.m_id);
            inCallMedia.push_back((itCall != m_inCallRows.end()) ? itCall->second : 0);
        }
        if (lay.m_markLeft[conv_row_mark_notify_only] != kConvRowNotDrawn)
        {
            RECT rc = { lay.m_markLeft[conv_row_mark_notify_only], capsuleTop,
                        lay.m_markLeft[conv_row_mark_notify_only] + notifyOnlyW, capsuleTop + kInCallCapsuleH };
            notifyOnlyRects.push_back(rc);
        }
    }

    // 其余的行：标记画在名字之后。只看与脏区相交的那几行。原先遍历全部可见行、对每个带标记的行都算一次名字区域
    // （其中要在全部可见行里找这一行），5000 人的通讯录每画一次要几十到一百毫秒，
    // 而真正落在屏幕上的标记只有十几个（2026-10-01 基准实测）。
    int firstRow = 0;
    int lastRow  = -1;
    DirtyRowRange_(rcDirty, firstRow, lastRow);
    for (int row = firstRow; row <= lastRow; ++row)
    {
        const int nodeId = GetIdAtVisibleRow(row);
        if (m_summaryRows.count(nodeId) != 0)
        {
            continue;   // 会话行版式的行上面已经按第二行处理过
        }
        const bool starred  = (m_starredRows.count(nodeId) != 0);
        const bool favorite = (m_favoriteRows.count(nodeId) != 0);
        const bool subtitle = (m_subtitleRows.count(nodeId) != 0);
        std::map<int, int>::const_iterator itCall = m_inCallRows.find(nodeId);
        const int callMedia = (itCall != m_inCallRows.end()) ? itCall->second : 0;
        const bool notifyOnly = (m_notifyOnlyRows.count(nodeId) != 0);
        if (!starred && !favorite && !subtitle && callMedia == 0 && !notifyOnly)
        {
            continue;
        }

        RECT rcText;
        if (!GetItemLabelTextRect(nodeId, rcText))
        {
            // 节点不可见 / 标签为空 / 可用宽度已被挤没：该行不画，不做近似猜测。
            continue;
        }

        // 不与脏区相交的行跳过（基类没重绘它，hdc 也被裁掉）。
        if (rcText.bottom <= rcDirty.top || rcText.top >= rcDirty.bottom)
        {
            continue;
        }

        // 从名字末尾往右依次排：星标在前，收藏徽章在后。两者都与名字那一行的文字
        // 垂直居中对齐，而不是与整行居中 —— 两行节点里后者会落到姓名与预览之间的中缝上。
        // 标记宽度已计入右侧占位（见 UpdateRightReserve_），名字过长时基类已先截断
        // 名字，这里的越界判断只是兜底：万一还是放不下就不画，不压到行右端。
        int cursor = rcText.right;
        if (starred)
        {
            RECT rcStar;
            rcStar.left   = cursor + kStarIconGap;
            rcStar.right  = rcStar.left + kStarIconSize;
            rcStar.top    = (rcText.top + rcText.bottom - kStarIconSize) / 2;
            rcStar.bottom = rcStar.top + kStarIconSize;
            if (rcStar.right > markRight)
            {
                continue;
            }
            starRects.push_back(rcStar);
            cursor = rcStar.right;
        }
        if (favorite)
        {
            RECT rcFav;
            rcFav.left   = cursor + kFavoriteIconGap;
            rcFav.right  = rcFav.left + kFavoriteIconSize;
            rcFav.top    = (rcText.top + rcText.bottom - kFavoriteIconSize) / 2;
            rcFav.bottom = rcFav.top + kFavoriteIconSize;
            if (rcFav.right > markRight)
            {
                continue;
            }
            favoriteRects.push_back(rcFav);
            cursor = rcFav.right;
        }
        if (subtitle)
        {
            RECT rcSub;
            rcSub.left   = cursor + kSubtitleIconGap;
            rcSub.right  = rcSub.left + kSubtitleIconSize;
            rcSub.top    = (rcText.top + rcText.bottom - kSubtitleIconSize) / 2;
            rcSub.bottom = rcSub.top + kSubtitleIconSize;
            if (rcSub.right > markRight)
            {
                continue;
            }
            subtitleRects.push_back(rcSub);
            subtitleSelected.push_back(GetCurSel() == nodeId);
            cursor = rcSub.right;
        }
        if (callMedia > 0)
        {
            RECT rcCall;
            rcCall.left   = cursor + kInCallGap;
            rcCall.right  = rcCall.left + capsuleW;
            rcCall.top    = (rcText.top + rcText.bottom - kInCallCapsuleH) / 2;
            rcCall.bottom = rcCall.top + kInCallCapsuleH;
            if (rcCall.right > markRight)
            {
                continue;
            }
            inCallRects.push_back(rcCall);
            inCallMedia.push_back(callMedia);
            cursor = rcCall.right;
        }
        if (notifyOnly)
        {
            RECT rcNo;
            rcNo.left   = cursor + kInCallGap;
            rcNo.right  = rcNo.left + notifyOnlyW;
            rcNo.top    = (rcText.top + rcText.bottom - kInCallCapsuleH) / 2;
            rcNo.bottom = rcNo.top + kInCallCapsuleH;
            if (rcNo.right <= markRight)
            {
                notifyOnlyRects.push_back(rcNo);
            }
        }
    }

    if (starRects.empty() && favoriteRects.empty() && subtitleRects.empty() && inCallRects.empty()
        && notifyOnlyRects.empty())
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    for (size_t i = 0; i < starRects.size(); ++i)
    {
        const RECT& rc = starRects[i];
        DrawLineIcon(g, kIconStar,
                     (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                     (Gdiplus::REAL)kStarIconSize, kStarIconColor, 0.0f);
    }
    for (size_t i = 0; i < favoriteRects.size(); ++i)
    {
        const RECT& rc = favoriteRects[i];
        DrawLineIcon(g, kIconBookmark,
                     (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                     (Gdiplus::REAL)kFavoriteIconSize, kFavoriteIconColor, 0.0f);
    }
    for (size_t i = 0; i < subtitleRects.size(); ++i)
    {
        const RECT& rc = subtitleRects[i];
        balloonchatwnd::DrawChatIcon(g, balloonchatwnd::kChatIconSubtitle,
                                     (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top, (Gdiplus::REAL)kSubtitleIconSize,
                                     subtitleSelected[i] ? kSubtitleIconSelectedColor : kSubtitleIconColor,
                                     kSubtitleIconStroke);
    }
    if (inCallRects.empty() && notifyOnlyRects.empty())
    {
        return;
    }

    // 「通话中」胶囊：圆角底 + 图标 + 文字。文字用默认字体缩小绘制，白色、在胶囊里垂直居中
    Gdiplus::Font baseFont(hdc, GetDefaultFont());
    Gdiplus::FontFamily family;
    baseFont.GetFamily(&family);
    Gdiplus::Font textFont(&family, baseFont.GetSize() * kInCallTextScale, baseFont.GetStyle(), baseFont.GetUnit());
    Gdiplus::SolidBrush fill(kInCallFill);
    Gdiplus::SolidBrush ink(kInCallInk);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    const CString label = TR(L"通话中");
    for (size_t i = 0; i < inCallRects.size(); ++i)
    {
        const RECT& rc = inCallRects[i];
        const Gdiplus::REAL radius = kInCallCapsuleH / 2.0f;
        Gdiplus::GraphicsPath path;
        path.AddArc((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top, radius * 2, radius * 2, 90.0f, 180.0f);
        path.AddArc((Gdiplus::REAL)rc.right - radius * 2, (Gdiplus::REAL)rc.top, radius * 2, radius * 2, 270.0f, 180.0f);
        path.CloseFigure();
        g.FillPath(&fill, &path);

        const int iconKind = (inCallMedia[i] == kInCallMediaVideo) ? balloonchatwnd::kChatIconVideo
                                                                     : balloonchatwnd::kChatIconPhone;
        const Gdiplus::REAL iconX = (Gdiplus::REAL)(rc.left + kInCallPadX);
        const Gdiplus::REAL iconY = (Gdiplus::REAL)(rc.top + (kInCallCapsuleH - kInCallIconSize) / 2);
        balloonchatwnd::DrawChatIcon(g, iconKind, iconX, iconY, (Gdiplus::REAL)kInCallIconSize, kInCallInk,
                                     kInCallIconStroke);

        const Gdiplus::REAL textX = iconX + kInCallIconSize + kInCallIconTextGap;
        const Gdiplus::RectF textRc(textX, (Gdiplus::REAL)rc.top, (Gdiplus::REAL)rc.right - textX,
                                    (Gdiplus::REAL)kInCallCapsuleH);
        g.DrawString((LPCWSTR)label, label.GetLength(), &textFont, textRc, &format, &ink);
    }

    // 「仅通知」标签：灰底圆角 + 深灰文字，字体与「通话中」胶囊相同，没有图标（docs/群管理接入方案.md §14）
    if (notifyOnlyRects.empty())
    {
        return;
    }
    Gdiplus::SolidBrush noFill(kNotifyOnlyFill);
    Gdiplus::SolidBrush noInk(kNotifyOnlyInk);
    const CString noLabel = balloonchatwnd::NotifyOnlyBadgeText();
    for (size_t i = 0; i < notifyOnlyRects.size(); ++i)
    {
        const RECT& rc = notifyOnlyRects[i];
        const Gdiplus::REAL radius = kInCallCapsuleH / 2.0f;
        Gdiplus::GraphicsPath path;
        path.AddArc((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top, radius * 2, radius * 2, 90.0f, 180.0f);
        path.AddArc((Gdiplus::REAL)rc.right - radius * 2, (Gdiplus::REAL)rc.top, radius * 2, radius * 2, 270.0f, 180.0f);
        path.CloseFigure();
        g.FillPath(&noFill, &path);

        const Gdiplus::REAL textX = (Gdiplus::REAL)(rc.left + kInCallPadX);
        const Gdiplus::RectF textRc(textX, (Gdiplus::REAL)rc.top, (Gdiplus::REAL)rc.right - textX,
                                    (Gdiplus::REAL)kInCallCapsuleH);
        g.DrawString((LPCWSTR)noLabel, noLabel.GetLength(), &textFont, textRc, &format, &noInk);
    }
}

void MainPanelTreeView::OnPaint(HDC hdc, const RECT& rcDirty)
{
    // 会话行版式的行：先按当前宽度排好第二行，把截断后的摘要写进基类副标签，基类接着就按它画
    std::vector<ConvRowPaint> convRows;
    PrepareConvRows_(hdc, rcDirty, convRows);

    // 先让基类把整行其余内容画完（行底色 / 头像 / 标题 / 预览）。被本类接管的行，
    // 基类右侧文字是占位空格、不可见；未接管的行（如联系人树）右侧文字仍由基类正常绘制。
    DuiTreeView::OnPaint(hdc, rcDirty);

    // 头像右下角的在线状态角标紧接着基类的头像画，排在置顶底色之前：置顶行上它与头像一起被覆上底色，
    // 描边的白色与覆色后的行底色一致，不会多出一圈亮边。
    PaintPresenceCorners_(hdc, rcDirty);

    // 行标记（特别关注星标、收藏徽章等）单独一段：通讯录与群行画在<u>名字之后</u>，会话行画在第二行，
    // 与下面那套右侧装饰（时间 / 未读 / 静音 / 置顶）分开写，两边都好读。一个标记都没有时这一段整段跳过。
    PaintNameMarks(hdc, rcDirty, convRows);

    // 一样都没接管的树（联系人树）直接短路，零开销。
    if (m_rightTopText.empty() && m_badges.empty()
        && m_pinnedRows.empty() && m_notifyModeRows.empty() && m_unreadDotRows.empty())
    {
        return;
    }

    const int rowHeight = GetRowHeight();
    if (rowHeight <= 0)
    {
        return;
    }

    const int rightEdge    = m_rcItem.right - kBadgeRightPad;
    const COLORREF topTextClr = GetRightTextColor();   // 与基类右侧文字同色（默认灰）
    const HFONT defaultFont   = GetDefaultFont();      // 按本控件 DPI 取，与基类绘制文字同一字体

    // ---- 第一遍：遍历与脏区相交的行，收集置顶行底色 / 画上排时间 / 收集下排提醒模式徽章与红点 ----
    // 只看与脏区相交的那几行，不必把几千个可见行逐个过一遍再跳过。
    std::vector<RECT>      pinnedRects;   // 待覆盖淡暗色的置顶行矩形
    std::vector<RECT>      muteRects;     // 待画提醒模式徽章的矩形
    std::vector<int>       muteKinds;     // 与 muteRects 一一对应的徽章图标（MainPanelIcons 的 IconKind）
    std::vector<BadgeDraw> draws;
    int firstRow = 0;
    int lastRow  = -1;
    DirtyRowRange_(rcDirty, firstRow, lastRow);
    for (int row = firstRow; row <= lastRow; ++row)
    {
        int nodeId = GetIdAtVisibleRow(row);

        // 复算该行矩形 —— 与 DuiTreeView 单列绘制同口径：m_rcItem.top + r*rowHeight。
        RECT rcRow;
        rcRow.left   = m_rcItem.left;
        rcRow.right  = m_rcItem.right;
        rcRow.top    = m_rcItem.top + row * rowHeight;
        rcRow.bottom = rcRow.top + rowHeight;

        // 不与脏区相交的行跳过（基类没重绘它，hdc 也被裁掉，省一次绘制）。
        if (rcRow.bottom <= rcDirty.top || rcRow.top >= rcDirty.bottom)
        {
            continue;
        }

        const int  mid   = (rcRow.top + rcRow.bottom) / 2;
        std::map<int, int>::const_iterator itMode = m_notifyModeRows.find(nodeId);
        const int  modeIcon = (itMode != m_notifyModeRows.end()) ? NotifyModeIconKind(itMode->second) : -1;
        // 胶囊灰底：有提醒模式徽章的行，以及只要灰底、不画徽章的行（消息盒子那一行）
        const bool muted = (modeIcon >= 0) || (m_quietRows.count(nodeId) != 0);

        // 本行右侧列（上排文字 / 下排胶囊与静音图标）的右边缘。所有行都贴同一条右边线，时间与未读数在
        // 各行之间上下对齐。2026-10-07 之前单聊行右端有在线状态徽标，右侧列要让到它左边，单聊行的时间
        // 因此比群行靠左 18 像素；在线状态改画在头像右下角以后不再有这个让位（回归用例见
        // MainPanelRowMarkTests 的 CRL1）。
        const int rowRightEdge = rightEdge;

        // 置顶行：整行覆一层淡暗色。这里只收集矩形，实际填充统一放到第二遍的 GDI+
        // 块里做，避免在同一 HDC 上与下面的 GDI 文字绘制交错。
        if (m_pinnedRows.count(nodeId) != 0)
        {
            pinnedRects.push_back(rcRow);
        }

        // 上排右侧文字（会话行的时间 / 群行的成员数）：与主标签同一垂直带
        // （基类主标签画在 [top, mid] 内、DT_BOTTOM）。
        std::map<int, CString>::const_iterator itTop = m_rightTopText.find(nodeId);
        if (itTop != m_rightTopText.end() && !itTop->second.IsEmpty())
        {
            RECT rcTop = { rcRow.left, rcRow.top, rowRightEdge, mid };
            DrawRightTopText(hdc, defaultFont, (LPCWSTR)itTop->second, rcTop, topTextClr);
        }

        // 下排：提醒模式徽章 + 未读胶囊，右对齐排列，与预览同高（基类预览画在
        // [mid, bottom] 内、DT_TOP）。不提醒的几种模式下胶囊改灰。
        const int centerY = mid + kBadgePreviewCenterOffset;
        int bottomRight = rowRightEdge;   // 下排从右往左排布的游标

        std::map<int, int>::const_iterator itBadge = m_badges.find(nodeId);
        if (itBadge != m_badges.end() && itBadge->second > 0)
        {
            BadgeDraw d;
            d.m_text  = FormatBadgeText(itBadge->second);
            d.m_muted = muted;
            if (!d.m_text.IsEmpty())
            {
                int badgeWidth = ComputeBadgeWidth(d.m_text.GetLength(),
                                                   kBadgeDiameter, kBadgeExtraPerChar);
                d.m_rc = ComputeBadgeRect(bottomRight, badgeWidth, kBadgeDiameter, centerY);
                draws.push_back(d);
                bottomRight = d.m_rc.left - kMuteIconGap;   // 徽章让到胶囊左边
            }
        }
        else if (m_unreadDotRows.count(nodeId) != 0)
        {
            //手动标为未读、又没有未读数：画一个不带数字的小圆点，配色与胶囊同一套（不提醒的模式下为灰）
            BadgeDraw d;
            d.m_muted = muted;
            d.m_rc = ComputeBadgeRect(bottomRight, kUnreadDotDiameter, kUnreadDotDiameter, centerY);
            draws.push_back(d);
            bottomRight = d.m_rc.left - kMuteIconGap;
        }

        if (modeIcon >= 0)
        {
            RECT rcMute;
            rcMute.right  = bottomRight;
            rcMute.left   = rcMute.right - kMuteIconSize;
            rcMute.top    = centerY - kMuteIconSize / 2;
            rcMute.bottom = rcMute.top + kMuteIconSize;
            muteRects.push_back(rcMute);
            muteKinds.push_back(modeIcon);
        }
    }

    if (pinnedRects.empty() && muteRects.empty() && draws.empty())
    {
        return;
    }

    // ---- 第二遍：GDI+ 抗锯齿画置顶行底色、胶囊底、提醒模式徽章 ----
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 置顶行底色必须先于胶囊 / 图标画，否则会把它们一起蒙上一层。
        {
            Gdiplus::SolidBrush tint(kPinnedRowTint);
            for (size_t i = 0; i < pinnedRects.size(); ++i)
            {
                const RECT& rc = pinnedRects[i];
                g.FillRectangle(&tint,
                                rc.left, rc.top,
                                rc.right - rc.left, rc.bottom - rc.top);
            }
        }

        // 左侧强调竖条 —— 置顶行的主要标记。画在底色之后：底色是覆盖整行的半透明
        // 层，先画竖条会被它蒙一层、橙色发暗。竖条本身不透明，压住底色不受影响。
        {
            Gdiplus::SolidBrush accent(kPinnedAccentColor);
            for (size_t i = 0; i < pinnedRects.size(); ++i)
            {
                const RECT rcBar = ComputePinnedAccentRect(
                    pinnedRects[i], kPinnedAccentWidth, kPinnedAccentHeightPercent);
                if (rcBar.right <= rcBar.left || rcBar.bottom <= rcBar.top)
                {
                    continue;   // 行高异常时算出空矩形，跳过
                }
                Gdiplus::GraphicsPath barPath;
                AddRoundRectPath(barPath,
                                 (Gdiplus::REAL)rcBar.left, (Gdiplus::REAL)rcBar.top,
                                 (Gdiplus::REAL)(rcBar.right - rcBar.left),
                                 (Gdiplus::REAL)(rcBar.bottom - rcBar.top),
                                 kPinnedAccentRadius);
                g.FillPath(&accent, &barPath);
            }
        }

        // 未读胶囊（单字符即正圆）：不提醒的几种模式用灰底，其余用红底。
        Gdiplus::SolidBrush redBrush(kBadgeRed);
        Gdiplus::SolidBrush grayBrush(kBadgeMutedGray);
        for (size_t i = 0; i < draws.size(); ++i)
        {
            const RECT& rc = draws[i].m_rc;
            Gdiplus::GraphicsPath path;
            AddRoundRectPath(path,
                             (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                             (Gdiplus::REAL)(rc.right - rc.left),
                             (Gdiplus::REAL)(rc.bottom - rc.top),
                             (Gdiplus::REAL)(rc.bottom - rc.top) / 2.0f);
            g.FillPath(draws[i].m_muted ? &grayBrush : &redBrush, &path);
        }

        // 提醒模式徽章：与右侧文字同色（默认灰），描边风格与菜单图标同源。
        const Gdiplus::Color muteColor(255, GetRValue(topTextClr),
                                       GetGValue(topTextClr), GetBValue(topTextClr));
        for (size_t i = 0; i < muteRects.size(); ++i)
        {
            const RECT& rc = muteRects[i];
            DrawLineIcon(g, muteKinds[i],
                         (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                         (Gdiplus::REAL)kMuteIconSize, muteColor, kMuteIconStroke);
        }
    }

    // ---- 第三遍：GDI 画白色数字（GDI+ 块已结束，不与 GDI 交错）----
    for (size_t i = 0; i < draws.size(); ++i)
    {
        //未读小红点没有数字
        if (draws[i].m_text.IsEmpty())
        {
            continue;
        }
        DrawBadgeText(hdc, (LPCWSTR)draws[i].m_text, draws[i].m_rc);
    }
}

} // namespace balloonmain
