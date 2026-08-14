/**
 *  历史消息记录窗口——balloonui 客户区布局实现，HistoryWindowLayout.cpp
 *
 *  程序化搭建：标题区、搜索行、类型胶囊 + 时间下拉、消息列表、底部分页 + 导出。
 *  全部用 balloonui 现成控件（DuiVBox/DuiHBox/DuiLabel/DuiSearchBox/DuiComboBox/
 *  DuiButton/DuiAvatar/DuiScrollView/DuiSeparator）组合而成，列表行不自绘。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 自带 ATL/WTL 前置依赖，工程里标
 *  PrecompiledHeader = NotUsing（与 GroupManageLayout.cpp 同约定）。
 *
 *  balloonwj@qq.com   2026-06-09
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖 ----
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

#include "HistoryWindowLayout.h"
#include "HistoryView.h"              // BuildPagerModel / BuildEmptyStateText
#include "HistoryHighlightLabel.h"    // 预览文字的关键字高亮
#include "HistoryContentView.h"       // 行正文内容块（多行 + 表情 + 折叠展开）
#include "HistoryRow.h"               // 可悬停 / 可双击的行容器 + Pin 标记

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSearchBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSpinBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"   // 量文字宽度要用与控件同一支默认字体

namespace balloonhistory {

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiAvatar;
using balloonwjui::DuiSeparator;
using balloonwjui::DuiButton;
using balloonwjui::DuiSearchBox;
using balloonwjui::DuiSpinBox;
using balloonwjui::DuiComboBox;
using balloonwjui::DuiScrollView;

namespace {

typedef DuiLayout::Hint Hint;

// ---- 配色（COLORREF）----
const COLORREF kColorWhite    = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorTitle    = RGB(0x22, 0x22, 0x22);   // 标题
const COLORREF kColorSubtitle = RGB(0x9A, 0x98, 0x93);   // 副标题 / 底部信息
const COLORREF kColorName     = RGB(0x2A, 0x2A, 0x2A);   // 行内昵称
const COLORREF kColorTime     = RGB(0xA8, 0xA6, 0xA2);   // 行内时间
const COLORREF kColorPreview  = RGB(0x55, 0x55, 0x55);   // 行内内容预览
const COLORREF kColorLocateText = RGB(0x6B, 0x6B, 0x6B); // “定位”按钮文字（柔和灰）
const COLORREF kColorSepLine  = RGB(0xEC, 0xEA, 0xE6);   // 分隔线
const COLORREF kColorEmptyTitle = RGB(0x6B, 0x6B, 0x6B);  // 空状态主文案（比正文淡、比副文案深）

// ---- 尺寸（像素）----
const int kHeaderH      = 32;   // 标题区高（仅“共 N 条消息”副行；标题在标题栏显示）
const int kSearchRowH   = 44;   // 搜索行高（含上下内边距；搜索框本身约 30 高）
const int kFilterRowH   = 44;   // 筛选行高
const int kFooterH      = 52;   // 底部行高
const int kRowH         = 56;   // 列表行高（名字行 + 预览行 + 上下内边距）
const int kRowCompactH  = 30;   // 折叠行行高：省掉了名字与时间那一行，只剩预览
const int kNameLineH    = 20;   // 行内“名字 + 时间”那一行的高
const int kNameWidth    = 96;   // 行内昵称列宽（时间紧跟其后，不再右对齐）
const int kGroupHeaderH = 28;   // 日期分组头行高
const int kPinMarkW     = 16;   // 已 Pin 行时间右侧那枚小图钉的占位宽
const int kThumbSize    = 36;   // 行内缩略图 / 文件类型色块的边长（与头像同高，一排看着齐整）
const int kTitleLineH   = 26;   // 标题文字高
const int kSubLineH     = 18;   // 副标题文字高
const int kAvatarSize   = 36;   // 头像边长
const int kAvatarCorner = 8;    // 头像圆角
const int kTimeWidth    = 96;   // 行内时间列宽
const int kTextLineH    = 22;   // 行内单行文字高
const int kLocateW      = 56;   // “定位”按钮宽
const int kRowPinW      = 56;   // 行内“Pin / 取消”按钮宽（与“定位”同宽，两枚并排看着齐整）
const int kChipW        = 56;   // 类型胶囊宽（均为 2 字）
const int kTimeComboMinW = 120;  // 时间下拉的最小宽度（实测宽度不足这个数时按它取）
const int kPagerBtnW    = 48;   // 分页“首页/末页”按钮宽
const int kPagerCellW   = 34;   // 分页箭头 / 页码按钮宽
const int kExportW      = 96;   // 导出按钮宽
// “跳至”是两个汉字、“页”是一个，宽度分开给 —— 都按两个字算会让“页”左边空一块。
// 原先两个都用 28，两个汉字塞不下，“跳”被切掉了半个。
const int kJumpLabelW   = 40;   // “跳至” 标签宽（两个汉字 + 一点余量）
const int kJumpUnitW    = 22;   // “页” 标签宽（一个汉字）
const int kJumpBoxW     = 48;   // 跳页微调框宽（够放 3 位页码 + ▲▼ 按钮条）
const int kJumpBoxH     = 28;   // 跳页微调框高（与旁边分页按钮同高，不再撑满整行）
const int kEmptyTitleH  = 26;   // 空状态主文案行高
const int kEmptySubH    = 22;   // 空状态副文案行高
const int kPadH         = 20;   // 左右内边距
const int kCustomRowH   = 36;   // 自定义时间区间行高
const int kExpandLinkH  = 20;   // 行内“展开 / 收起”链接那一行的高
const int kRowPadV      = 8;    // 行的上下内边距（与 BuildRow 里 SetPadding 的上下值一致）
const int kListScrollBarW = 12; // 列表滚动条占的宽度，算正文可用宽度时要先扣掉
// 列表可用宽度未知时（单元测试直接调 BuildListContent、没有窗口）退而求其次用的宽度。
// 取一个中等窗口下的典型值：正文照它折行，行高照它算，视觉上不会离谱。
const int kListFallbackWidth = 860;
// 自定义时间区间“年”下拉的可选年份范围（含两端）。
const int kCustomYearStart = 2018;
const int kCustomYearEnd   = 2035;

// DuiComboBox 画只读文字时，控件宽度里有一截是留给内边距与右侧箭头的，文字只能用剩下
// 那部分：左内边距 8 + 文字与箭头的间隙 4 + 箭头三角 14 + 箭头到右边框 8 = 34px
// （对应 DuiComboBox.cpp 里的 kTextPadLPx / kTextArrowGap / kArrowWPx / kArrowPadRPx）。
// 给下拉定宽时必须把这 34px 加回去，否则文字会被 DT_END_ELLIPSIS 截断。
const int kComboChromeW = 34;
// 量宽之后再留的余量（像素）。不同字体的字距、以及 DrawText 与 GetTextExtentPoint32
// 之间一两个像素的出入，都靠它兜住，免得刚好差一点又被截成省略号。
const int kComboSlackW  = 8;

/**
 *  量一段文字用 balloonui 默认字体画出来有多宽（像素）。
 *
 *  自定义时间区间那几个下拉必须照实际字体定宽，不能写死像素：默认字体是按当前 DPI
 *  算出来的（DuiResMgr 里 lfHeight = -MulDiv(9, dpi, 72)），125% 缩放下比 100% 宽出
 *  四分之一，而 DuiLayout 的 Fixed() 给的是<u>不随 DPI 变化</u>的像素。原先“年”下拉
 *  写死 76px，100% 下勉强够、125% 下就把“2026年”截成了“202...”。
 *
 *    text：待测量的文字；空串返回 0。
 *  返回：文字宽度（像素）；取不到设备上下文或字体时返回 0（调用方据此退回保守宽度）。
 */
int MeasureDefaultFontWidth(LPCTSTR text)
{
    if (text == NULL || *text == 0)
    {
        return 0;
    }

    HDC hdcScreen = ::GetDC(NULL);
    if (hdcScreen == NULL)
    {
        return 0;
    }

    int width = 0;
    HFONT hFont = balloonwjui::DuiResMgr::Inst().GetDefaultFont();
    HFONT hOld = (hFont != NULL) ? (HFONT)::SelectObject(hdcScreen, hFont) : NULL;
    SIZE sz = { 0, 0 };
    if (::GetTextExtentPoint32(hdcScreen, text, ::lstrlen(text), &sz))
    {
        width = sz.cx;
    }
    if (hOld != NULL)
    {
        ::SelectObject(hdcScreen, hOld);
    }
    ::ReleaseDC(NULL, hdcScreen);
    return width;
}

// 造一个文本标签。
std::unique_ptr<DuiControl> MakeLabel(LPCTSTR text, COLORREF color, DWORD dtAlign, UINT ctrlId = 0)
{
    std::unique_ptr<DuiLabel> lbl(new DuiLabel());
    lbl->SetText(text);
    lbl->SetTextColor(color);
    lbl->SetTextAlign(dtAlign);
    if (ctrlId != 0)
    {
        lbl->SetCtrlId(ctrlId);
    }
    return std::unique_ptr<DuiControl>(lbl.release());
}

// 造一个普通按钮（分页 / 导出 / 行内定位）。
std::unique_ptr<DuiControl> MakeButton(LPCTSTR text, UINT ctrlId, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> btn(new DuiButton());
    btn->SetButtonType(DuiButton::StylePushButton);
    btn->SetText(text);
    btn->SetCtrlId(ctrlId);
    btn->SetVariant(variant);
    return std::unique_ptr<DuiControl>(btn.release());
}

// 造一个类型筛选胶囊；选中态用 Primary、其余用 Default。
std::unique_ptr<DuiControl> MakeChip(LPCTSTR text, UINT ctrlId, bool active)
{
    return MakeButton(text, ctrlId, active ? DuiButton::Variant::Primary : DuiButton::Variant::Default);
}

// 算出行内“正文内容块”能用多宽（像素）。
//
// 行是个 HBox：[头像][可选的缩略图][正文列(weight=1)][Pin 槽][定位槽]，外加左右内边距
// 与各列之间的 gap。正文列拿到的就是总宽减去其余各列。行高要按正文实际折行的行数算，
// 而折行又取决于这个宽度，所以它必须与 BuildRow 里的实际布局<u>严格对应</u> ——
// 这里少扣一列，行高就会算少，正文最后一行会被下一行压住。
//   listWidth：列表（滚动视口内容）的总宽度；<=0 时用 kListFallbackWidth 兜底。
//   hasThumb： 本行左侧有没有那一小块缩略图 / 类型色块。
int RowContentWidth(int listWidth, bool hasThumb)
{
    if (listWidth <= 0)
    {
        listWidth = kListFallbackWidth;
    }

    //行内各列的宽度之和（不含正文列本身）。gap 数 = 列数 - 1。
    const int kRowPadH = 8;              //行左右内边距，与 BuildRow 的 SetPadding 一致
    const int kRowGap  = 10;             //行内列间距，与 BuildRow 的 SetGap 一致
    int columns = 4;                     //头像 + 正文 + Pin 槽 + 定位槽
    int used = kAvatarSize + kRowPinW + kLocateW;
    if (hasThumb)
    {
        used += kThumbSize;
        ++columns;
    }
    used += kRowPadH * 2;
    used += kRowGap * (columns - 1);
    used += kListScrollBarW;

    const int width = listWidth - used;
    //再窄也给一个下限，免得算出负数让排版引擎逐字折行、行数爆炸。
    const int kMinContentWidth = 80;
    return (width > kMinContentWidth) ? width : kMinContentWidth;
}

// 算一行占多高（像素）：名字时间行 + 正文内容块 + 可选的“展开”链接 + 上下内边距。
//   row：            行数据。
//   contentWidth：   正文可用宽度（RowContentWidth 的结果）。
//   outNeedsExpand： [出参] 本行是否需要“展开 / 收起”链接。BuildRow 也要用同一个
//                    结论，故在这里一次算好、两处共用，避免各算一遍出现偏差。
int MeasureRowHeight(const HistoryRowView& row, int contentWidth, bool& outNeedsExpand)
{
    //内容块的测量与绘制共用同一份实现，故这里临时造一个来问尺寸 —— 它很轻（只存
    //段序列与一个测量缓存），一次布局造一遍不构成负担。
    HistoryContentView probe;
    probe.SetContent(row.m_fullText);
    probe.SetExpanded(row.m_expanded);

    outNeedsExpand = probe.NeedsExpandLink(contentWidth);

    int height = probe.MeasureHeight(contentWidth);
    if (!row.m_compact)
    {
        height += kNameLineH;
    }
    if (outNeedsExpand)
    {
        height += kExpandLinkH;
    }
    //正文与名字行之间、正文与链接之间各有一个 gap（mid 那个 VBox 的 SetGap(2)）。
    const int kMidGap = 2;
    int gaps = 0;
    if (!row.m_compact)
    {
        ++gaps;
    }
    if (outNeedsExpand)
    {
        ++gaps;
    }
    height += kMidGap * gaps;
    height += kRowPadV * 2;

    //再矮也不低于原先的固定行高，保证短消息那一行的观感与改造前一致。
    const int kMinRowHeight = kRowH;
    return (height > kMinRowHeight) ? height : kMinRowHeight;
}

// 造一条消息行：头像 + （昵称/时间 + 正文内容块 + 可选的“展开”链接） + 操作按钮。
//   contentWidth：   正文内容块的可用宽度（像素），由 RowContentWidth 按列表宽算出。
//   needsExpandLink：正文是否超出折叠行数、需要挂一枚“展开 / 收起”链接。
//                    由调用方先算好传进来 —— BuildListContent 也要用它来算行高，
//                    两处各算一遍容易漂移。
std::unique_ptr<DuiControl> BuildRow(const HistoryRowView& row, int index,
                                     int contentWidth, bool needsExpandLink)
{
    std::unique_ptr<HistoryRow> line(new HistoryRow());
    line->SetCtrlId((UINT)(kIdHistoryRowBase + index));
    line->SetPadding(8, 8, 8, 8);
    line->SetGap(10);

    //折叠行（与上一行同发送者、同一天）省掉头像，但<u>照旧占住那一列的宽度</u>，
    //使预览文字与上一行严丝合缝地对齐 —— 不占位的话这一行会整体左移，读起来像是
    //另起了一段。
    if (row.m_compact)
    {
        line->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(kAvatarSize));
    }
    else
    {
        std::unique_ptr<DuiAvatar> avatar(new DuiAvatar());
        avatar->SetShape(DuiAvatar::ShapeRoundRect);
        avatar->SetCornerRadius(kAvatarCorner);
        avatar->SetName(row.m_senderName);
        avatar->SetFallbackBgColor(row.m_avatarColor);
        line->AddChild(std::unique_ptr<DuiControl>(avatar.release()), Hint().Fixed(kAvatarSize));
    }

    //行内左侧那一小块：图片画缩略图、文件画类型色块。纯文字的 `[图片]` `[文件] xxx`
    //在一列里辨识度太低，扫过去全是方括号。
    if (!row.m_thumbImagePath.IsEmpty() || !row.m_thumbFileExt.IsEmpty())
    {
        std::unique_ptr<HistoryRowThumb> thumb(new HistoryRowThumb());
        if (!row.m_thumbImagePath.IsEmpty())
        {
            thumb->SetImage(std::wstring((LPCWSTR)row.m_thumbImagePath));
        }
        else
        {
            thumb->SetFile(row.m_thumbFileExt);
        }
        line->AddChild(std::unique_ptr<DuiControl>(thumb.release()), Hint().Fixed(kThumbSize));
    }

    std::unique_ptr<DuiVBox> mid(new DuiVBox());
    mid->SetGap(2);

    //名字与时间是同一件事的两半，紧挨着排 —— 原先时间固定 96px 右对齐，名字与它之间
    //空出几百像素，视线要横跨整行才能把"谁"和"什么时候"对上。
    //时间用 Weight(1) 吃掉右侧剩余空间并左对齐，效果就是紧跟在名字后面。
    std::unique_ptr<DuiHBox> nameLine(new DuiHBox());
    nameLine->SetGap(8);
    nameLine->AddChild(MakeLabel(row.m_senderName, kColorName,
                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                       Hint().Fixed(kNameWidth));
    nameLine->AddChild(MakeLabel(row.m_timeText, kColorTime,
                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE), Hint().Fixed(kTimeWidth));

    //已 Pin 的行在时间右边点一枚小图钉。行内那枚常驻的「取消」按钮在最右侧，
    //扫一列行时不容易看出"这条钉着"；标记贴着名字与时间，纵向扫下来一眼能挑出来。
    if (row.m_pinned)
    {
        nameLine->AddChild(std::unique_ptr<DuiControl>(new HistoryPinMark()),
                           Hint().Fixed(kPinMarkW));
    }
    //余下的空间留白，使名字与时间靠左成组，不被拉散。
    nameLine->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));

    //折叠行不重复画名字与时间那一行，把整行高度让给预览 —— 同一个人连着说的几句，
    //纵向看过去只剩内容本身，密度立刻上来。
    if (!row.m_compact)
    {
        mid->AddChild(std::move(nameLine), Hint().Fixed(kNameLineH));
    }

    //正文内容块：多行、把 /f["<id>"] 画成表情、搜索命中处铺一层淡黄底。
    //长文默认折叠（最多 kHistoryCollapsedMaxLines 行），由下面那枚链接展开。
    std::unique_ptr<HistoryContentView> content(new HistoryContentView());
    content->SetContent(row.m_fullText);
    content->SetTextColor(kColorPreview);
    content->SetHighlightRanges(row.m_highlights);
    content->SetExpanded(row.m_expanded);
    const int contentH = content->MeasureHeight(contentWidth);
    mid->AddChild(std::unique_ptr<DuiControl>(content.release()), Hint().Fixed(contentH));

    //「展开 / 收起」链接：只有正文真的超出折叠行数时才建 —— 内容本来就画得下却挂着
    //一个「展开」，点下去毫无变化，比不给还糟。
    if (needsExpandLink)
    {
        std::unique_ptr<DuiLabel> expandLink(new DuiLabel());
        expandLink->SetText(row.m_expanded ? _T("收起") : _T("展开"));
        expandLink->SetCtrlId((UINT)(kIdHistoryExpandBase + index));
        //ModeLink 才会悬停变手型、点击发 DUIN_CLICK；ModeText 是纯绘制控件，点了没反应。
        expandLink->SetMode(DuiLabel::ModeLink);
        expandLink->SetAutoNavigate(false);   //不打开浏览器，点击交给窗口处理
        expandLink->SetLinkColor(kColorLocateText);
        expandLink->SetHoverColor(kColorName);
        expandLink->SetVisitedColor(kColorLocateText);
        expandLink->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        mid->AddChild(std::unique_ptr<DuiControl>(expandLink.release()),
                      Hint().Fixed(kExpandLinkH));
    }

    line->AddChild(std::move(mid), Hint().Weight(1));

    //“Pin / 取消”按钮：与“定位”同一套次操作风格，排在它左边。文案随本条当前是否
    //被钉住取反 —— 不新增第二枚按钮，一枚按钮按状态切换含义，与气泡右键菜单里那一项
    //口径一致。点下去只发请求，界面等服务端推送回来再刷。
    std::unique_ptr<DuiButton> pin(new DuiButton());
    pin->SetButtonType(DuiButton::StylePushButton);
    pin->SetText(row.m_pinned ? _T("取消") : _T("Pin"));
    pin->SetCtrlId((UINT)(kIdHistoryPinBase + index));
    pin->SetVariant(DuiButton::Variant::Default);
    pin->SetTextColor(kColorLocateText);
    pin->SetTextPointSize(9);
    //按钮外面套一层固定宽度的容器：DuiLayout 布局时会<u>跳过不可见的子控件</u>，
    //直接把按钮隐藏的话它那一列的宽度会被旁边的预览文字吃掉，鼠标一进来文字被挤短、
    //看着抖一下。外层容器始终可见、占住宽度，只切里面按钮的可见性就没有这个问题。
    DuiControl* pinRaw = pin.get();
    std::unique_ptr<DuiHBox> pinSlot(new DuiHBox());
    pinSlot->AddChild(std::unique_ptr<DuiControl>(pin.release()), Hint().Weight(1));
    line->AddChild(std::unique_ptr<DuiControl>(pinSlot.release()), Hint().Fixed(kRowPinW));

    //“定位”按钮：次操作风格（白底 + 1px 灰边 + 柔和灰字、字号略小），轻量不抢眼，
    //对齐设计稿的浅灰小药丸；不用 Outlined（那是品牌蓝边 + 蓝字，过于扎眼）。
    std::unique_ptr<DuiButton> locate(new DuiButton());
    locate->SetButtonType(DuiButton::StylePushButton);
    locate->SetText(_T("定位"));
    locate->SetCtrlId((UINT)(kIdHistoryLocateBase + index));
    locate->SetVariant(DuiButton::Variant::Default);
    locate->SetTextColor(kColorLocateText);
    locate->SetTextPointSize(9);
    DuiControl* locateRaw = locate.get();
    std::unique_ptr<DuiHBox> locateSlot(new DuiHBox());
    locateSlot->AddChild(std::unique_ptr<DuiControl>(locate.release()), Hint().Weight(1));
    line->AddChild(std::unique_ptr<DuiControl>(locateSlot.release()), Hint().Fixed(kLocateW));

    //两枚操作按钮改成"悬停才显示"：常驻的话一屏八行就是十六枚按钮，右侧糊成一道
    //按钮墙，而它们绝大多数时候都不被点。<u>位置照旧用 Fixed 预留</u>（上面已经加进
    //布局了），这里只切可见性 —— 不预留的话鼠标一进来预览文字就被挤短、看着抖一下。
    //
    //例外：已 Pin 的行，它的「取消」按钮常驻。已经钉住是一个需要能一眼看见、且随时
    //可撤的状态，藏起来反而找不到。
    if (!row.m_pinned)
    {
        line->AddHoverOnlyControl(pinRaw);
    }
    line->AddHoverOnlyControl(locateRaw);

    return std::unique_ptr<DuiControl>(line.release());
}

// 分页器的按钮填充逻辑抽到命名空间作用域的 PopulatePagerButtons（供窗口翻页时局部
// 重填复用），见本文件下方。

// 造一条横向分隔线。
std::unique_ptr<DuiControl> MakeSeparator()
{
    std::unique_ptr<DuiSeparator> sep(new DuiSeparator());
    sep->SetOrientation(DuiSeparator::Horizontal);
    sep->SetColor(kColorSepLine);
    return std::unique_ptr<DuiControl>(sep.release());
}

// 造一个整数下拉（值 first..last，每项带后缀如“年/月/日”）。用于自定义日期选择。
std::unique_ptr<DuiControl> MakeIntCombo(UINT ctrlId, int first, int last, LPCTSTR suffix)
{
    std::unique_ptr<DuiComboBox> combo(new DuiComboBox());
    combo->SetCtrlId(ctrlId);
    for (int v = first; v <= last; ++v)
    {
        CString s;
        s.Format(_T("%d%s"), v, suffix);
        combo->AddString(s);
    }
    combo->SetCurSel(0, false);
    return std::unique_ptr<DuiControl>(combo.release());
}

// 算出一个整数下拉要多宽，才能把它<u>最宽的那一项</u>完整显示出来（像素）。
//
// 必须实测、不能写死像素：默认字体随 DPI 放大而 Hint().Fixed() 的像素不会，
// 详见 MeasureDefaultFontWidth 的说明。
//   first/last：下拉的取值范围（含两端）。
//   suffix：    每项的后缀（“年”/“月”/“日”）。
int IntComboWidth(int first, int last, LPCTSTR suffix)
{
    int widestText = 0;
    for (int v = first; v <= last; ++v)
    {
        CString s;
        s.Format(_T("%d%s"), v, suffix);
        //逐项都量一遍而不是只量最后一项：位数不一定单调（如 9月 比 12月 窄），
        //取最宽的那项才保证每一项都显示得下。
        const int w = MeasureDefaultFontWidth(s);
        if (w > widestText)
        {
            widestText = w;
        }
    }
    return widestText + kComboChromeW + kComboSlackW;
}

// 造“自定义时间区间”行：从 [年][月][日] 到 [年][月][日]。默认隐藏，时间下拉选“自定义”
// 时由窗口 SetVisible(true) 显示；各下拉静态填充、默认选第一项，窗口显示时再改选今天。
std::unique_ptr<DuiControl> BuildCustomDateRow()
{
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetCtrlId(kIdHistoryCustomRow);
    row->SetPadding(kPadH, 0, kPadH, 8);
    row->SetGap(4);
    row->SetVisible(false);

    //六个下拉的宽度<u>先全部算好</u>再往下添子控件。不要图省事写成"造下拉时顺带回填
    //宽度、同一句里拿去 Fixed()"—— 同一个函数调用的各实参求值次序在 C++ 里是未指定的，
    //Hint().Fixed(w) 完全可能先于回填 w 的那个实参求值，结果宽度取到 0。
    const int kYearW  = IntComboWidth(kCustomYearStart, kCustomYearEnd, _T("年"));
    const int kMonthW = IntComboWidth(1, 12, _T("月"));
    const int kDayW   = IntComboWidth(1, 31, _T("日"));

    //“从”“到”两个字同样按实测定宽 —— 理由与下拉一致，写死像素在高 DPI 下会被切掉半个字。
    //左右各留 6px 呼吸空间，使它与两侧下拉之间不至于贴得太紧。
    const int kFromToPadW = 12;
    const int kFromToLabelW = MeasureDefaultFontWidth(_T("到")) + kFromToPadW;

    row->AddChild(MakeLabel(_T("从"), kColorSubtitle, DT_RIGHT | DT_VCENTER | DT_SINGLELINE),
                  Hint().Fixed(kFromToLabelW));
    row->AddChild(MakeIntCombo(kIdHistoryCustomStartY, kCustomYearStart, kCustomYearEnd, _T("年")),
                  Hint().Fixed(kYearW));
    row->AddChild(MakeIntCombo(kIdHistoryCustomStartM, 1, 12, _T("月")), Hint().Fixed(kMonthW));
    row->AddChild(MakeIntCombo(kIdHistoryCustomStartD, 1, 31, _T("日")), Hint().Fixed(kDayW));
    row->AddChild(MakeLabel(_T("到"), kColorSubtitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE),
                  Hint().Fixed(kFromToLabelW));
    row->AddChild(MakeIntCombo(kIdHistoryCustomEndY, kCustomYearStart, kCustomYearEnd, _T("年")),
                  Hint().Fixed(kYearW));
    row->AddChild(MakeIntCombo(kIdHistoryCustomEndM, 1, 12, _T("月")), Hint().Fixed(kMonthW));
    row->AddChild(MakeIntCombo(kIdHistoryCustomEndD, 1, 31, _T("日")), Hint().Fixed(kDayW));
    return std::unique_ptr<DuiControl>(row.release());
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildListContent(const std::vector<HistoryRowView>& rows,
                                             LPCTSTR emptyTitle, LPCTSTR emptySubtitle,
                                             int listWidth)
{
    std::unique_ptr<DuiVBox> listContent(new DuiVBox());
    listContent->SetCtrlId(kIdHistoryList);
    listContent->SetPadding(8, 0, 8, 0);
    listContent->SetGap(0);

    //空状态：上下各留一块弹性空白把文案挤到中间，不写死一个"顶部间距"——
    //列表区高度随窗口缩放变化，写死的话窄窗口下文案会被挤出可视区。
    if (rows.empty())
    {
        listContent->AddChild(std::unique_ptr<DuiControl>(new DuiControl()),
                              DuiLayout::Hint().Weight(1));
        listContent->AddChild(MakeLabel((emptyTitle != NULL) ? emptyTitle : _T(""),
                                        kColorEmptyTitle,
                                        DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                                        kIdHistoryEmptyTitle),
                              DuiLayout::Hint().Fixed(kEmptyTitleH));
        if (emptySubtitle != NULL && emptySubtitle[0] != _T('\0'))
        {
            listContent->AddChild(MakeLabel(emptySubtitle, kColorSubtitle,
                                            DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                                            kIdHistoryEmptySub),
                                  DuiLayout::Hint().Fixed(kEmptySubH));
        }
        listContent->AddChild(std::unique_ptr<DuiControl>(new DuiControl()),
                              DuiLayout::Hint().Weight(1));
        return std::unique_ptr<DuiControl>(listContent.release());
    }

    for (size_t i = 0; i < rows.size(); ++i)
    {
        //日期分组头：不可点、不给 ctrlId —— 它不是一行消息，不该占行按钮的 id 段，
        //否则后面各行的"定位 / Pin"按钮下标就与 m_model.Records() 的下标对不上了。
        if (!rows[i].m_groupLabel.IsEmpty())
        {
            listContent->AddChild(MakeLabel(rows[i].m_groupLabel, kColorSubtitle,
                                            DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                                  DuiLayout::Hint().Fixed(kGroupHeaderH));
        }

        //行高按正文实际折行的行数算，不再是编译期常量 —— 长消息折叠态最多 5 行，
        //展开后要多高就多高。宽度与"要不要展开链接"两处都由同一次测量给出，
        //保证测量与随后的 BuildRow 用的是同一套结论。
        const bool hasThumb = (!rows[i].m_thumbImagePath.IsEmpty()
                               || !rows[i].m_thumbFileExt.IsEmpty());
        const int contentW = RowContentWidth(listWidth, hasThumb);
        bool needsExpand = false;
        const int rowH = MeasureRowHeight(rows[i], contentW, needsExpand);
        listContent->AddChild(BuildRow(rows[i], (int)i, contentW, needsExpand),
                              DuiLayout::Hint().Fixed(rowH));
    }
    return std::unique_ptr<DuiControl>(listContent.release());
}

void PopulatePagerButtons(DuiControl* pager, int currentPage, int pageCount)
{
    //pager 实为 DuiHBox（DuiLayout 子类）；取 DuiLayout 指针才能用带 Hint 的 AddChild。
    DuiLayout* box = dynamic_cast<DuiLayout*>(pager);
    if (box == NULL)
    {
        return;
    }
    typedef DuiLayout::Hint Hint;
    box->AddChild(MakeButton(_T("首页"), kIdHistoryPageFirst, DuiButton::Variant::Default),
                  Hint().Fixed(kPagerBtnW));
    box->AddChild(MakeButton(_T("<"), kIdHistoryPagePrev, DuiButton::Variant::Default),
                  Hint().Fixed(kPagerCellW));

    //页码按哪几格显示交给纯模型算（省略式，格子数有上限），这里只负责照着它建控件。
    //原先是 for (p = 1; p <= pageCount; ++p) 一路平铺 —— 5000 条消息的会话会在底部
    //排出 600 多个按钮，整行直接失控。
    const std::vector<HistoryPagerCell> cells = BuildPagerModel(currentPage, pageCount);
    for (size_t i = 0; i < cells.size(); ++i)
    {
        if (cells[i].m_isEllipsis)
        {
            //省略号是<u>不可点的标签</u>，不给 ctrlId —— 它不占页码 id 段，也就不会
            //把后面的页号挤到别的段里去。
            box->AddChild(MakeLabel(_T("…"), kColorSubtitle,
                                    DT_CENTER | DT_VCENTER | DT_SINGLELINE),
                          Hint().Fixed(kPagerCellW));
            continue;
        }

        CString num;
        num.Format(_T("%d"), cells[i].m_page);
        box->AddChild(MakeButton(num, (UINT)(kIdHistoryPageNumBase + (cells[i].m_page - 1)),
                                 cells[i].m_isCurrent ? DuiButton::Variant::Primary
                                                      : DuiButton::Variant::Default),
                      Hint().Fixed(kPagerCellW));
    }

    box->AddChild(MakeButton(_T(">"), kIdHistoryPageNext, DuiButton::Variant::Default),
                  Hint().Fixed(kPagerCellW));
    box->AddChild(MakeButton(_T("末页"), kIdHistoryPageLast, DuiButton::Variant::Default),
                  Hint().Fixed(kPagerBtnW));
}

std::unique_ptr<DuiControl> BuildHistoryWindowBody(const HistoryViewData& data)
{
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(0);
    root->SetGap(0);

    // 1. 标题区：仅“共 N 条消息”副行。窗口标题“消息记录 · <对话名>”已在标题栏显示，
    //    客户区不再重复（避免与标题栏重复）。
    std::unique_ptr<DuiVBox> header(new DuiVBox());
    header->SetPadding(kPadH, 8, kPadH, 6);
    header->SetGap(0);
    CString sub;
    sub.Format(_T("共 %d 条消息"), data.m_totalCount);
    header->AddChild(MakeLabel(sub, kColorSubtitle,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE, kIdHistorySubtitle),
                     Hint().Fixed(kSubLineH));
    root->AddChild(std::move(header), Hint().Fixed(kHeaderH));
    root->AddChild(MakeSeparator(), Hint().Fixed(1));

    // 2. 搜索行。
    std::unique_ptr<DuiHBox> searchRow(new DuiHBox());
    searchRow->SetPadding(kPadH, 8, kPadH, 6);
    std::unique_ptr<DuiSearchBox> search(new DuiSearchBox());
    search->SetCtrlId(kIdHistorySearch);
    search->SetPlaceholder(_T("搜索消息内容、文件名、@某人"));
    searchRow->AddChild(std::unique_ptr<DuiControl>(search.release()), Hint().Weight(1));
    root->AddChild(std::move(searchRow), Hint().Fixed(kSearchRowH));

    // 3. 筛选行：类型胶囊（左，撑满）+ 时间下拉（右，固定）。
    std::unique_ptr<DuiHBox> filterRow(new DuiHBox());
    filterRow->SetPadding(kPadH, 2, kPadH, 8);
    filterRow->SetGap(8);
    std::unique_ptr<DuiHBox> chips(new DuiHBox());
    chips->SetGap(8);
    chips->AddChild(MakeChip(_T("全部"), kIdHistoryChipAll, data.m_activeChip == 0), Hint().Fixed(kChipW));
    chips->AddChild(MakeChip(_T("文字"), kIdHistoryChipText, data.m_activeChip == 1), Hint().Fixed(kChipW));
    chips->AddChild(MakeChip(_T("图片"), kIdHistoryChipImage, data.m_activeChip == 2), Hint().Fixed(kChipW));
    chips->AddChild(MakeChip(_T("文件"), kIdHistoryChipFile, data.m_activeChip == 3), Hint().Fixed(kChipW));
    chips->AddChild(MakeChip(_T("语音"), kIdHistoryChipVoice, data.m_activeChip == 4), Hint().Fixed(kChipW));
    chips->AddChild(MakeChip(_T("链接"), kIdHistoryChipLink, data.m_activeChip == 5), Hint().Fixed(kChipW));
    //Pin 排在最后：它与前六枚不是一个维度（前六枚按内容类型分，它按"钉没钉住"分），
    //放末尾读起来最顺。胶囊单选，故选了 Pin 就是"本会话全部被钉的消息，不分类型"。
    chips->AddChild(MakeChip(_T("Pin"), kIdHistoryChipPin, data.m_activeChip == 6), Hint().Fixed(kChipW));
    filterRow->AddChild(std::move(chips), Hint().Weight(1));

    //时间下拉。各项的<u>顺序必须与 HistoryTimeRange 的枚举值一一对应</u>（0 起，
    //自定义排最后）—— 窗口侧的 TimeRangeFromComboIndex / ComboIndexFromRange 就是
    //照这个约定直接转换的，插项时两边要一起改。
    static const LPCTSTR kTimeComboItems[] =
    {
        _T("全部时间"), _T("近 7 天"), _T("近 30 天"),
        _T("近 3 个月"), _T("近半年"), _T("近一年"), _T("自定义..."),
    };
    const int kTimeComboItemCount = (int)(sizeof(kTimeComboItems) / sizeof(kTimeComboItems[0]));

    std::unique_ptr<DuiComboBox> timeCombo(new DuiComboBox());
    timeCombo->SetCtrlId(kIdHistoryTimeCombo);
    //下拉宽度按最宽的一项实测 —— 写死像素在高 DPI 下会把“全部时间”截成“全部时...”，
    //理由与自定义日期那几个下拉相同，见 MeasureDefaultFontWidth。
    int timeComboW = kTimeComboMinW;
    for (int i = 0; i < kTimeComboItemCount; ++i)
    {
        timeCombo->AddString(kTimeComboItems[i]);
        const int need = MeasureDefaultFontWidth(kTimeComboItems[i])
                       + kComboChromeW + kComboSlackW;
        if (need > timeComboW)
        {
            timeComboW = need;
        }
    }
    timeCombo->SetCurSel(data.m_activeTimeIndex, false);   //构建期设值不发通知，避免伪触发刷新
    filterRow->AddChild(std::unique_ptr<DuiControl>(timeCombo.release()),
                        Hint().Fixed(timeComboW));
    root->AddChild(std::move(filterRow), Hint().Fixed(kFilterRowH));

    // 3.5 自定义时间区间行（默认隐藏；时间下拉选“自定义”时窗口 SetVisible 显示并重排）。
    root->AddChild(BuildCustomDateRow(), Hint().Fixed(kCustomRowH));

    // 4. 列表：DuiScrollView 包一个承载各行的 vbox（vbox 由 BuildListContent 造，
    //    窗口局部刷新时复用同一函数换上新列表）。
    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    scroll->SetCtrlId(kIdHistoryScroll);
    scroll->SetAutoContentHeight(true);
    scroll->SetScrollBarWidth(10);
    //整窗构建时还不知道列表实际有多宽（窗口尚未布局），传 0 让 BuildListContent 用
    //兜底宽度。窗口在 InitUi / RefreshContent 里会用真实宽度重建一次列表。
    scroll->SetContent(BuildListContent(data.m_rows, data.m_emptyTitle,
                                        data.m_emptySubtitle, 0));
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));

    root->AddChild(MakeSeparator(), Hint().Fixed(1));

    // 5. 底部：左信息 + 中分页器 + 右导出。
    std::unique_ptr<DuiHBox> footer(new DuiHBox());
    footer->SetPadding(kPadH, 8, kPadH, 8);
    footer->SetGap(8);
    CString info;
    info.Format(_T("共 %d 条 · 第 %d/%d 页"), data.m_totalCount, data.m_currentPage, data.m_pageCount);
    footer->AddChild(MakeLabel(info, kColorSubtitle,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE, kIdHistoryFooterInfo),
                     Hint().Weight(1));
    std::unique_ptr<DuiHBox> pager(new DuiHBox());
    pager->SetCtrlId(kIdHistoryPager);
    pager->SetGap(4);
    PopulatePagerButtons(pager.get(), data.m_currentPage, data.m_pageCount);
    footer->AddChild(std::move(pager), Hint().Weight(2));

    //“跳至第 N 页”：省略式页码只给出当前页附近那几格，页数一多就没法直达中间某页
    //（比如 625 页里的第 300 页），故补一个直达入口。
    //用 balloonui 现成的 DuiSpinBox 而不是裸输入框：它自带范围夹取与 ▲▼ 步进，
    //越界不用自己校验。范围与当前值由窗口在每次刷新时同步（见 RefreshContent）。
    footer->AddChild(MakeLabel(_T("跳至"), kColorSubtitle,
                               DT_RIGHT | DT_VCENTER | DT_SINGLELINE),
                     Hint().Fixed(kJumpLabelW));
    std::unique_ptr<DuiSpinBox> jump(new DuiSpinBox());
    jump->SetCtrlId(kIdHistoryPageJump);
    jump->SetRange(1, (data.m_pageCount > 0) ? data.m_pageCount : 1);
    //初始化不发通知，否则建完控件就会自投一次翻页。
    jump->SetValue(data.m_currentPage, false);
    if (jump->GetEdit() != NULL)
    {
        //手输入的提交要在 EDIT 失焦时自己做，故给内部 EDIT 单独一个 id 认身份。
        jump->GetEdit()->SetCtrlId(kIdHistoryPageJumpEdit);
    }
    //FixedCross 把它的高度限住，否则会撑满整个底部行、比旁边的分页按钮高出一截。
    footer->AddChild(std::unique_ptr<DuiControl>(jump.release()),
                     Hint().Fixed(kJumpBoxW, kJumpBoxH));
    footer->AddChild(MakeLabel(_T("页"), kColorSubtitle,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                     Hint().Fixed(kJumpUnitW));

    footer->AddChild(MakeButton(_T("↓ 导出"), kIdHistoryExport, DuiButton::Variant::Outlined),
                     Hint().Fixed(kExportW));
    root->AddChild(std::move(footer), Hint().Fixed(kFooterH));

    return std::unique_ptr<DuiControl>(root.release());
}

void FillMockHistory(HistoryViewData& data)
{
    data.m_title = _T("消息记录 · 苏沐辰");
    data.m_totalCount = 27;
    data.m_currentPage = 1;
    data.m_pageCount = 3;
    data.m_activeChip = 0;
    data.m_activeTimeIndex = 0;

    const COLORREF kOrange = RGB(0xF2, 0x6B, 0x3E);
    const COLORREF kRed    = RGB(0xF2, 0x5C, 0x54);
    const COLORREF kGreen  = RGB(0x2F, 0xA4, 0x7A);

    struct MockRow
    {
        LPCTSTR  name;
        COLORREF color;
        LPCTSTR  time;
        LPCTSTR  preview;
        long long msgid;
    };
    const MockRow rows[] =
    {
        { _T("苏沐辰"), kOrange, _T("06-04 11:01"), _T("改了发你看下"),                 9001 },
        { _T("苏沐辰"), kOrange, _T("06-04 10:01"), _T("[表情] 赞"),                    9000 },
        { _T("林知夏"), kRed,    _T("06-04 09:01"), _T("左对齐更好，居中显得太弱了"),   8999 },
        { _T("苏沐辰"), kOrange, _T("06-04 08:01"), _T("你觉得这个排版怎么样？"),       8998 },
        { _T("苏沐辰"), kOrange, _T("06-04 07:01"), _T("[图片] 参考案例-banner"),       8997 },
        { _T("林知夏"), kRed,    _T("06-04 06:01"), _T("收到，很有意思"),               8996 },
        { _T("宋微之"), kGreen,  _T("06-04 05:01"), _T("[文件] 设计规范-v2.docx"),      8995 },
    };
    const int count = (int)(sizeof(rows) / sizeof(rows[0]));
    for (int i = 0; i < count; ++i)
    {
        HistoryRowView v;
        v.m_senderName = rows[i].name;
        v.m_avatarColor = rows[i].color;
        v.m_timeText = rows[i].time;
        v.m_previewText = rows[i].preview;
        v.m_msgid = rows[i].msgid;
        data.m_rows.push_back(v);
    }
}

} // namespace balloonhistory
