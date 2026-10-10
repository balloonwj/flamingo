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

#include "HistoryWindowLayout.h"
#include "HistoryLayoutKit.h"   // layoutkit：把本文件的几个小工具公开给文件 / 图片页布局
#include "HistoryLibraryLayout.h"   // kIdLibBody / kIdLibExpandLeft：分栏右侧的文件 / 图片页
#include "HistoryStorageLayout.h"   // kIdStorBody：分栏右侧的本机存储占用页
#include "HistoryTipSearchBox.h"    // 悬停时显示搜索语法说明的搜索框
#include "HistoryTimelineLayout.h"  // 时间轴导航条的内容与宽度
#include <set>
#include "HistoryImageGrid.h"   // GridColumnCount：「图片」分类网格每行几张
#include "HistoryView.h"              // BuildPagerModel / BuildEmptyStateText
#include "HistoryHighlightLabel.h"    // 预览文字的关键字高亮
#include "HistoryContentView.h"       // 行正文内容块（多行 + 表情 + 折叠展开）
#include "HistoryRow.h"               // 可悬停 / 可双击的行容器 + Pin 标记
#include "../BalloonChatWnd/ChatFileCard.h"          // FileCardHeight / kFileCardWidth
#include "../BalloonChatWnd/ChatFileCardControl.h"   // 聊天文件卡片控件（聊天记录查看窗的文件行）
#include "../BalloonChatWnd/ChatVoiceStrip.h"        // 语音条控件（消息记录窗与查看窗的语音行，语音消息第 4 期）
#include "../BalloonChatWnd/ChatCodeCardControl.h"   // 代码片段卡片控件（消息记录窗与查看窗的代码片段行）
#include "../BalloonChatWnd/ChatMarkdownCardControl.h"   // Markdown 卡片控件（消息记录窗与查看窗的 Markdown 消息行）
#include "../BalloonMain/MainPanelScrollView.h"   // 列表滚动视口：悬浮式自动隐藏的细滚动条
#include "../BalloonMain/MainPanelTreeView.h"     // 聊天记录管理器左栏的会话列表（与主面板会话列表同一控件）
#include "HistoryConversationList.h"              // 左栏类型筛选取值与范围按钮文字
#include "../BalloonMain/AvatarSpec.h"            // 范围弹层的首字头像：AvatarFallbackGlyph / AvatarColorForUser
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiSplitter.h"   // 聊天记录管理器左右分栏

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSpinBox.h"   // 「选日期」弹层的年月日微调框
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSearchBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSpinBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"   // 量文字宽度要用与控件同一支默认字体
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // 图片缩略图的悬停提示
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

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
const int kGridCellSize = 120;  // 「图片」分类网格的缩略图边长
const int kGridGap      = 8;    // 网格缩略图之间（横向与纵向）的间距
const int kGridPadV     = 8;    // 网格上下的内边距（左右与列表一样用 kListPadH）
const int kBatchBarH    = 40;   // 「文件」分类顶部「全部下载」提示条的高度
const int kBatchBtnW    = 84;   // 「全部下载」按钮宽
const int kPinMarkW     = 16;   // 已 Pin 行时间右侧那枚小图钉的占位宽
// 文件消息在昵称下方的那张卡片：左边一块类型色块，右边上下两行（文件名、大小）。
const int kFileCardBlockSize = 32;   // 卡片里类型色块的边长
const int kFileCardGap       = 8;    // 色块与右侧文字之间的间距
const int kFileNameLineH     = 20;   // 卡片里文件名那一行的高
const int kFileSizeLineH     = 16;   // 卡片里大小那一行的高
// 卡片总高：取色块边长与两行文字高度之和中的较大者（36）。
const int kFileCardH = (kFileCardBlockSize > kFileNameLineH + kFileSizeLineH)
                       ? kFileCardBlockSize : (kFileNameLineH + kFileSizeLineH);
// 聊天记录卡片色块里写的字与缺省标题。色块沿用文件卡片的画法，未知扩展名取默认底色。
const wchar_t* const kMergedCardBlockText    = TR_NOOP(L"记录");
const wchar_t* const kMergedCardDefaultTitle = TR_NOOP(L"聊天记录");
const int kTitleLineH   = 26;   // 标题文字高
const int kSubLineH     = 18;   // 副标题文字高
// 标题区那一行（「»」+ 会话名 +「共 N 条消息」）
const int kHeaderRowGap      = 8;     // 行内各控件之间的间距（像素）
const int kExpandLeftW       = 24;    //「»」按钮宽（像素）
const TCHAR* const kHistoryExpandLeftText = _T("»");   //「»」按钮的文字
const int kHeaderTitlePt     = 10;    // 会话名的字号（磅），粗体
const int kHeaderTitleMaxW   = 320;   // 会话名最多占多宽（像素），再长以省略号截断
const int kHeaderTitleSlackW = 6;     // 会话名量宽之外再留的余量（像素）
// 会话名是粗体、字号比默认字体大一磅，按默认字体量出的宽度要放大到这个百分比才放得下
const int kBoldWidthPercent  = 125;
const int kPercentBase       = 100;
const int kAvatarSize   = 36;   // 头像边长
const int kAvatarCorner = 8;    // 头像圆角
const int kTimeWidth    = 96;   // 行内时间列宽
const int kTextLineH    = 22;   // 行内单行文字高
const int kLocateW      = 56;   // “定位”按钮宽
const int kRowPinW      = 56;   // 行内“Pin / 取消”按钮宽（与“定位”同宽，两枚并排看着齐整）
const int kChipW        = 56;   // 类型胶囊宽（均为 2 字）
const int kTimeComboMinW = 120;  // 时间下拉的最小宽度（实测宽度不足这个数时按它取）
// 发送人下拉的宽度：按「全部成员」实测宽度与这个数取大者。昵称长短不一，不按最长的昵称
// 放宽（否则一个超长昵称会把搜索框挤得很窄），过长的昵称在框内截断显示
const int kSenderComboMinW = 140;
const int kSearchRowGap    = 8;    // 搜索行里搜索框与发送人下拉之间的间距
const int kDatePickPadW    = 24;   // 「选日期」按钮、日期区间标签的日期文字在文字宽度之外的左右留白合计
const TCHAR* const kHistoryDatePickText = TR_NOOP(L"选日期");     // 「选日期」按钮的文字
const TCHAR* const kHistoryDateRangeClearText = _T("×");   // 日期区间标签末尾关闭叉号的文字
const int kDateRangeClearW = 28;  // 日期区间标签末尾关闭叉号按钮的宽度（像素）
const int kPagerBtnW    = 48;   // 分页“首页/末页”按钮宽
const int kPagerCellW   = 34;   // 分页箭头 / 页码按钮宽
const int kExportW      = 96;   // 导出按钮宽
const int kClearConvW   = 112;  // 「清空聊天记录」按钮宽（六个汉字 + 左右内边距）
const int kSelectBoxW   = 20;   // 多选模式下行首复选框的边长（勾选框本身约 14，四周留点余量好点中）
const int kSelectColumnW = kSelectBoxW + 10;   // 复选框一列占掉的正文宽度：边长 + 行内列间距（与 BuildRow 的 SetGap 一致）
const int kSelectBtnW   = 84;   // 多选操作栏里「合并转发」「逐条转发」按钮宽
const int kSelectSmallBtnW = 64; // 多选操作栏里「删除」「取消」按钮宽
// “跳至”是两个汉字、“页”是一个，宽度分开给 —— 都按两个字算会让“页”左边空一块。
// 原先两个都用 28，两个汉字塞不下，“跳”被切掉了半个。
const int kJumpLabelW   = 40;   // “跳至” 标签宽（两个汉字 + 一点余量）
const int kJumpUnitW    = 22;   // “页” 标签宽（一个汉字）
const int kJumpBoxW     = 48;   // 跳页微调框宽（够放 3 位页码 + ▲▼ 按钮条）
const int kJumpBoxH     = 28;   // 跳页微调框高（与旁边分页按钮同高，不再撑满整行）
const int kEmptyTitleH  = 26;   // 空状态主文案行高
const int kEmptySubH    = 22;   // 空状态副文案行高
const int kPadH         = 20;   // 左右内边距
const int kExpandLinkH  = 20;   // 行内“展开 / 收起”链接那一行的高
const int kRowPadV      = 8;    // 行的上下内边距（与 BuildRow 里 SetPadding 的上下值一致）
// 行内 Pin / 定位按钮槽的高度（像素），取最小行高 kRowH 扣除上下内边距后的值（40）。
// 按钮槽固定这个高度并贴在行顶部，不随正文行数增加而拉高。
const int kRowActionH   = kRowH - kRowPadV * 2;
// 列表的滚动视口用 MainPanelScrollView：滚动条悬浮在内容上方、不占列表宽度，所以
// RowContentWidth 算正文可用宽度时不再为滚动条扣除一列（原先 DuiScrollView 自带的
// 滚动条占 10 像素，那时两处都要按它扣除）。
// 列表内容容器（BuildListContent 造的那个 vbox）的左右内边距（像素）。RowContentWidth
// 算正文可用宽度时必须扣除：原先漏扣这 2 × 8 像素，测量用的宽度比正文实际宽度多出
// 一截，长正文实际多折出几行、画出本行范围，压在下一行上（2026-09-30 修复）。
const int kListPadH = 8;
// 列表可用宽度未知时（单元测试直接调 BuildListContent、没有窗口）退而求其次用的宽度。
// 取一个中等窗口下的典型值：正文照它折行，行高照它算，视觉上不会离谱。
const int kListFallbackWidth = 860;

// ---- 时间轴模式（docs/聊天记录管理器方案.md §11.7，布局图见 tmp/histenhance/layout.html 图 4）----
// 每一行最前面是时间列与竖线节点列。竖线由每一行里竖向的 DuiSeparator 连接而成，所以时间轴样式的行
// 自身不留上下内边距（原来的上下内边距挪到内层容器上），各行的竖线首尾相接
const int kTimelineTimeW     = 52;   // 行首时间列宽（「HH:MM」靠右）
const int kTimelineRailW     = 28;   // 竖线节点列宽
const int kTimelineLeadW     = kTimelineTimeW + kTimelineRailW;   // 两列合计：正文列相应变窄
const int kTimelineLineThick = 2;    // 竖线粗细
const int kTimelineDotPt     = 7;    // 消息节点「●」的字号（磅）
const int kTimelineDotH      = 12;   // 消息节点那一格的高
const int kTimelineDayH      = 34;   // 日期大节点那一行的高
const int kTimelineDayDotPt  = 11;   // 日期大节点「●」的字号（磅）
const int kTimelineDayDotH   = 18;   // 日期大节点那一格的高
const int kTimelineDayGap    = 8;    // 日期文字与右侧说明之间的间距
const int kTimelineDayTextPt = 9;    // 日期文字的字号（磅），粗体
const COLORREF kColorTimelineLine = RGB(0xD5, 0xDC, 0xE8);   // 竖线
const COLORREF kColorTimelineDot  = RGB(0xB7, 0xC3, 0xD6);   // 普通消息节点
const COLORREF kColorTimelineHit  = RGB(0xE0, 0xA8, 0x00);   // 命中关键字的消息节点
const COLORREF kColorTimelineDay  = RGB(0x2D, 0x6C, 0xDF);   // 日期大节点
const TCHAR* const kTimelineDotText = _T("\x25CF");   // 节点用的实心圆「●」
const int kViewChipW   = 48;   // 搜索行最右边「列表」「时间轴」两枚胶囊的设计宽度；放不下文字时按文字加宽
const int kHitBarH     = 36;   // 命中条高
const int kHitBarPadV  = 4;    // 命中条上下内边距
const int kHitPosW     = 120;  // 命中条「第 x / N 处」那一列的宽
const int kHitBtnW     = 76;   // 命中条「▲ 上一处」「▼ 下一处」按钮的设计宽度
const COLORREF kColorHitBarBg   = RGB(0xFF, 0xF9, 0xE3);   // 命中条底色（浅黄，与定位行的高亮同一色系）
const COLORREF kColorHitBarText = RGB(0x5C, 0x4A, 0x12);   // 命中条文字
const COLORREF kColorNavBg      = RGB(0xF7, 0xF8, 0xFA);   // 时间轴导航条底色（与 HistoryTimelineLayout.cpp 一致）

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
 *  下拉框、按钮这类要容下文字的控件必须照实际字体定宽，不能写死像素：默认字体是按当前 DPI
 *  算出来的（DuiResMgr 里 lfHeight = -MulDiv(9, dpi, 72)），125% 缩放下比 100% 宽出
 *  四分之一，而 DuiLayout 的 Fixed() 给的是<u>不随 DPI 变化</u>的像素。原先自定义时间
 *  区间的“年”下拉写死 76px，100% 下勉强够、125% 下就把“2026年”截成了“202...”。
 *
 *    text：待测量的文字；空串返回 0。
 *  返回：文字宽度（像素）；取不到设备上下文或字体时返回 0（调用方据此退回保守宽度）。
 */
int MeasureDefaultFontWidth(LPCTSTR text);

//文字比设计宽度更宽时，在文字宽度之外加的左右留白合计（像素）：胶囊带圆角边框留得多，纯文字标签只留一点余量。
//两个值都保证简体中文的文字加上留白仍不超过原设计宽度，中文布局不变
const int kGrowChipPadW  = 20;
const int kGrowLabelPadW = 4;

/**
 *  按文字宽度「只增不减」地取控件宽度（多语言第 4 期）：文字放得下时保持设计宽度（简体中文布局不变），
 *  放不下时（英文等译文较长）加宽到文字宽度加留白。
 *  @param text        控件上的文字
 *  @param designWidth 原设计宽度（像素）
 *  @param padding     文字宽度之外的留白合计（像素），胶囊用 kGrowChipPadW，标签用 kGrowLabelPadW
 *  @return 控件宽度（像素）
 */
int GrowWidthToFitText(LPCTSTR text, int designWidth, int padding)
{
    const int need = MeasureDefaultFontWidth(text) + padding;
    return (need > designWidth) ? need : designWidth;
}

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

//「通配符」复选框的文字（右侧搜索行与左栏范围行共用，docs/聊天记录管理器方案.md §11.4）
const wchar_t* const kWildcardCheckText = TR_NOOP(L"通配符");
//复选框在文字宽度之外要占的宽度（像素）：勾选框左边距 6 + 勾选框 16 + 勾选框到文字 6
//（DuiButton.cpp 的 kGlyphPadPx、kGlyphSizePx），再留 6 像素余量
const int kWildcardCheckChromeW = 34;
//「通配符」复选框的悬停说明（2026-10-07 起）：两个通配字符的含义、几个典型写法，以及不勾选时的行为。
//规则与 HistoryWildcard.h 一致：仍是「包含」匹配，只忽略英文字母的大小写。多行文字，DuiToolTipMgr 按 '\n' 分行
const wchar_t* const kWildcardCheckTip = TR_NOOP(
    L"勾选后，关键字里的 * 和 ? 按通配符匹配：\n"
    L"    *  匹配任意多个字符（可以是零个）\n"
    L"    ?  匹配恰好一个字符\n"
    L"示例：\n"
    L"    会议*纪要    找到「会议纪要」「会议第二次纪要」\n"
    L"    张?    找到「张三」「张伟」\n"
    L"    202?年    找到「2025年」「2026年」\n"
    L"    报告*.pdf    找到名字里有「报告」且后面有「.pdf」的文件\n"
    L"仍按「包含」查找，不区分英文大小写；不勾选时 * 和 ? 按普通字符查找。");

/**
 *  造一个「通配符」复选框：透明底的 DuiButton 复选框，勾选框后面跟文字。用户点它时发
 *  DUIN_VALUECHANGED（extra 为 1 勾选、0 取消），宿主据此切换关键字的解释方式。
 *    ctrlId：控件编号（kIdHistoryWildcard 或 kIdMgrWildcard）。
 *    checked：建出来时是否勾选；构建期设值不发通知。
 *    outWidth：[出参] 按文字实际宽度算出的控件宽度（像素），调用方据此给 Hint().Fixed。
 *  @return 复选框控件，调用方拿到所有权
 */
std::unique_ptr<DuiControl> MakeWildcardCheckbox(UINT ctrlId, bool checked, int& outWidth)
{
    std::unique_ptr<DuiButton> box(new DuiButton());
    box->SetButtonType(DuiButton::StyleCheckbox);
    //透明底：默认样式会在整个控件矩形上画一圈圆角底框（与多选模式行首复选框同一设置）
    box->SetVariant(DuiButton::Variant::Ghost);
    box->SetText(TR(kWildcardCheckText));
    box->SetCtrlId(ctrlId);
    box->SetCheck(checked, false);
    outWidth = MeasureDefaultFontWidth(TR(kWildcardCheckText)) + kWildcardCheckChromeW;
    //悬停说明通配符的用法与示例；控件析构时基类会自动取消注册
    balloonwjui::DuiToolTipMgr::Inst().Register(box.get(), TR(kWildcardCheckTip));
    return std::unique_ptr<DuiControl>(box.release());
}

/**
 *  消息记录窗的底部一行：[共 N 条 · 第 x/y 页][分页栏][跳至][页码框][页][导出]。
 *
 *  分页栏按里面按钮的实际总宽占位（Hint().Auto()），左侧信息占剩下的宽度。窗口窄到
 *  整行放不下时，按下面的顺序隐藏，宽度恢复时再按相反顺序显示（回归用例见
 *  HistoryWindowLayoutTests 的 V2L20–V2L25、HWL-C2）：
 *    一、左侧信息文字放不下完整内容时整段隐藏 —— 它与顶部的"共 N 条消息"、分页栏里高亮
 *        的当前页、跳页框里的页号重复，不显示也不丢信息；
 *    二、仍放不下时隐藏「首页」「末页」—— 分页栏里第 1 页与最后一页的页码格一直都在；
 *    三、仍放不下时隐藏「跳至 _ 页」整组（2026-10-03 加入「清空聊天记录」按钮后与用户确认）——
 *        分页栏里当前页附近的页码格与前后翻页按钮仍可翻页。
 *  页码格、前后翻页按钮、「清空聊天记录」与导出按钮始终显示。
 *
 *  在 Layout 里决定显隐，是因为只有排版时才知道这一行实际能分到多宽；显隐改完紧接着
 *  按新的显隐状态排版，不会留下按旧状态排出的过时矩形（开发约定文档「UI 约定」第 5 条）。
 */
class HistoryFooterBar : public DuiHBox
{
public:
    // 按本次可用宽度决定左侧信息与首页 / 末页的显隐，再交给基类排版。
    //   rcAvail：本行可用的矩形（host 客户区坐标）。
    void Layout(const RECT& rcAvail) override;

private:
    // 设置控件显隐；状态未变时不调用 SetVisible，免得每次排版都触发一次重绘。
    //   ctrl：目标控件，NULL 时什么都不做；show：true 显示、false 隐藏。
    static void ShowIfChanged(DuiControl* ctrl, bool show);
};

void HistoryFooterBar::ShowIfChanged(DuiControl* ctrl, bool show)
{
    if (ctrl != NULL && ctrl->IsVisible() != show)
    {
        ctrl->SetVisible(show);
    }
}

void HistoryFooterBar::Layout(const RECT& rcAvail)
{
    DuiControl* info  = FindCtrlById(kIdHistoryFooterInfo);
    DuiControl* first = FindCtrlById(kIdHistoryPageFirst);
    DuiControl* last  = FindCtrlById(kIdHistoryPageLast);
    DuiControl* jumpLabel = FindCtrlById(kIdHistoryPageJumpLabel);
    DuiControl* jumpBox   = FindCtrlById(kIdHistoryPageJump);
    DuiControl* jumpUnit  = FindCtrlById(kIdHistoryPageJumpUnit);

    //先全部恢复显示，再按这次的可用宽度逐级隐藏：窗口由窄变宽时，被隐藏过的控件才能回来
    ShowIfChanged(info, true);
    ShowIfChanged(first, true);
    ShowIfChanged(last, true);
    ShowIfChanged(jumpLabel, true);
    ShowIfChanged(jumpBox, true);
    ShowIfChanged(jumpUnit, true);

    const int availW = rcAvail.right - rcAvail.left;

    //一、左侧信息按权重占位，期望宽度里只算了它的间距、没算文字，要另外量出文字宽度
    const DuiLabel* infoLabel = dynamic_cast<const DuiLabel*>(info);
    const int infoTextW = (infoLabel != NULL) ? MeasureDefaultFontWidth(infoLabel->GetText()) : 0;
    if (info != NULL && GetDesiredSize().cx + infoTextW > availW)
    {
        ShowIfChanged(info, false);
    }

    //二、隐藏左侧信息之后仍放不下：隐藏首页与末页（隐藏的子控件不计入期望宽度）
    if (GetDesiredSize().cx > availW)
    {
        ShowIfChanged(first, false);
        ShowIfChanged(last, false);
    }

    //三、还放不下：隐藏「跳至 _ 页」整组
    if (GetDesiredSize().cx > availW)
    {
        ShowIfChanged(jumpLabel, false);
        ShowIfChanged(jumpBox, false);
        ShowIfChanged(jumpUnit, false);
    }

    DuiHBox::Layout(rcAvail);

    //分页栏里的按钮会被整排替换（换页时窗口先清空再重填），而分页栏自己的矩形可能与上次
    //相同：基类给子控件定位用的 SetRect 在矩形未变时直接返回，不会往下排，新按钮的矩形就
    //一直是空的，表现为整排分页按钮消失（见 HistoryWindowLayoutTests 的 V2L25）。显隐变化
    //同理。故整行排完后再强制排一次分页栏
    DuiControl* pager = FindCtrlById(kIdHistoryPager);
    if (pager != NULL)
    {
        pager->ForceLayout(pager->GetRect());
    }
}

// 本行是不是图片消息（昵称下方显示预览图）。图片行即便算不出缓存路径（调试入口、消息
// 缺少 MD5）也要画一块占位，所以不能只看 m_thumbImagePath 是否为空。BuildRow 建控件与
// MeasureRowHeight 算行高都用它，两处的判断必须一致，否则行高会算错。
bool RowIsImage(const HistoryRowView& row)
{
    return row.m_hasImageThumb || !row.m_thumbImagePath.IsEmpty();
}

// 本行是不是语音消息（昵称下方显示语音条）。用途同上。
bool RowIsVoice(const HistoryRowView& row)
{
    return !RowIsImage(row) && row.m_isVoice;
}

// 本行是不是用聊天文件卡片显示的文件（聊天记录查看窗的文件行）。用途同上。
bool RowIsChatFileCard(const HistoryRowView& row)
{
    return !RowIsImage(row) && row.m_chatFileCard;
}

// 本行是不是文件消息（昵称下方显示"类型色块 + 文件名 + 大小"的卡片）。用途同上。
bool RowIsFile(const HistoryRowView& row)
{
    return !RowIsImage(row) && !RowIsChatFileCard(row) && !row.m_thumbFileExt.IsEmpty();
}

// 本行是不是合并转发的聊天记录（昵称下方显示"聊天记录卡片"）。用途同上。
bool RowIsMergedCard(const HistoryRowView& row)
{
    return !RowIsImage(row) && !RowIsFile(row) && row.m_isMergedCard;
}

// 本行是不是代码片段（昵称下方显示代码卡片，docs/代码片段方案.md 第 3.4 节）。用途同上。
bool RowIsCode(const HistoryRowView& row)
{
    return !RowIsImage(row) && !RowIsVoice(row) && !RowIsChatFileCard(row) && !RowIsFile(row)
        && !RowIsMergedCard(row) && row.m_isCode;
}

// 本行是不是 Markdown 消息（昵称下方显示 Markdown 卡片，docs/Markdown消息方案.md 第 12 节）。用途同上。
bool RowIsMarkdown(const HistoryRowView& row)
{
    return !RowIsImage(row) && !RowIsVoice(row) && !RowIsChatFileCard(row) && !RowIsFile(row)
        && !RowIsMergedCard(row) && !RowIsCode(row) && row.m_isMarkdown;
}

// 本行是不是带图片的帖子卡片或补充卡片（正文下面再排一行缩略图，docs/帖子群方案.md 14.5）。帖子卡片走正文内容块
// 那一支，所以先排除其余各种卡片。用途同上：BuildRow 与 MeasureRowHeight 共用。
bool RowHasCardThumbs(const HistoryRowView& row)
{
    return !RowIsImage(row) && !RowIsVoice(row) && !RowIsChatFileCard(row) && !RowIsFile(row)
        && !RowIsMergedCard(row) && !RowIsCode(row) && !RowIsMarkdown(row) && !row.m_cardThumbs.empty();
}

// 算出行内“内容列”能用多宽（像素）。
//
// 行是个 HBox：[头像][内容列(weight=1)][Pin 槽][定位槽]，外加左右内边距与各列之间的
// gap；行外面还有列表容器的左右内边距（kListPadH）。滚动条悬浮在内容上方、不占宽度。内容列
// 拿到的就是总宽减去以上各项。行高要按正文实际折行的行数算，而折行又取决于这个宽度，
// 所以它必须与 BuildRow 里的实际布局<u>严格对应</u> —— 这里少扣一列，行高就会算少，
// 正文最后一行会被下一行压住。
// 图片与文件不再在头像右侧单独占一列（2026-09-30 起都显示在昵称下方），各种行的内容列
// 宽度相同。
//   listWidth：列表（滚动视口内容）的总宽度；<=0 时用 kListFallbackWidth 兜底。
//   reserveRowActions：行内是否有 Pin / 定位两个按钮槽，必须与 BuildRow 收到的同一个值。
int RowContentWidth(int listWidth, bool reserveRowActions)
{
    if (listWidth <= 0)
    {
        listWidth = kListFallbackWidth;
    }

    //行内各列的宽度之和（不含内容列本身）。gap 数 = 列数 - 1。
    const int kRowPadH = 8;              //行左右内边距，与 BuildRow 的 SetPadding 一致
    const int kRowGap  = 10;             //行内列间距，与 BuildRow 的 SetGap 一致
    //列数：头像 + 内容列，预留操作列时再加 Pin 槽 + 定位槽
    const int kBaseColumns   = 2;
    const int kActionColumns = 2;
    const int columns = reserveRowActions ? (kBaseColumns + kActionColumns) : kBaseColumns;
    int used = kAvatarSize;
    if (reserveRowActions)
    {
        used += kRowPinW + kLocateW;
    }
    used += kRowPadH * 2;
    used += kRowGap * (columns - 1);
    used += kListPadH * 2;

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
    outNeedsExpand = false;
    int height = 0;
    if (RowIsImage(row))
    {
        //图片行：内容块就是预览图，高度按原图宽高等比算出，与图片下没下载无关。
        int dispW = 0;
        int dispH = 0;
        ComputeHistoryImageSize(row.m_imageWidth, row.m_imageHeight, dispW, dispH);
        height = dispH;
    }
    else if (RowIsVoice(row))
    {
        //语音行：内容块是一条固定高度的语音条。
        height = balloonchatwnd::kVoiceStripH;
    }
    else if (RowIsChatFileCard(row))
    {
        //聊天文件卡片：高度随状态变化（有进度条、有操作链接时更高），与聊天窗文件气泡一致。
        height = balloonchatwnd::FileCardHeight(row.m_fileCard);
    }
    else if (RowIsFile(row))
    {
        //文件行：内容块是一张固定高度的卡片。
        height = kFileCardH;
    }
    else if (RowIsMergedCard(row))
    {
        //聊天记录行：与文件卡片同一高度的卡片。
        height = kFileCardH;
    }
    else if (RowIsCode(row))
    {
        //代码片段：与聊天窗相同的代码卡片，高度按内容列宽度排版得出（超过折叠行数时只算前面几行）
        height = balloonchatwnd::ChatCodeCardControl::MeasureCard(std::wstring((LPCWSTR)row.m_fullText),
                                                                 row.m_codeLang, contentWidth, row.m_codeNotes).cy;
    }
    else if (RowIsMarkdown(row))
    {
        //Markdown 消息：与聊天窗相同的卡片，高度随摘要的行数变化（最多三行）
        height = balloonchatwnd::ChatMarkdownCardControl::MeasureCard(row.m_markdownCard.m_summary, contentWidth).cy;
    }
    else
    {
        //内容块的测量与绘制共用同一份实现，故这里临时造一个来问尺寸 —— 它很轻（只存
        //段序列与一个测量缓存），一次布局造一遍不构成负担。
        HistoryContentView probe;
        probe.SetContent(row.m_fullText);
        probe.SetExpanded(row.m_expanded);

        outNeedsExpand = probe.NeedsExpandLink(contentWidth);
        height = probe.MeasureHeight(contentWidth);
    }
    //帖子卡片的缩略图：正文下面再排一行，高度取显示出来的缩略图里最高的那张
    const bool hasCardThumbs = RowHasCardThumbs(row);
    if (hasCardThumbs)
    {
        height += HistoryCardThumbStripHeight(row);
    }
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
    if (hasCardThumbs)
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
//   reserveRowActions：是否建右侧的 Pin / 定位两个按钮槽；为 false 时整行只有头像与内容列。
// 造图片消息的预览图控件：按窗口算好的状态画图片或占位（需要下载 / 下载中 / 破图）。
// 带上缩略图段的 ctrlId，单击时窗口据此反算行下标、发起下载。尺寸由调用方按
// ComputeHistoryImageSize 的结果在布局提示里给定。
//   row：  行数据（取路径、状态、是否可点、悬停提示）。
//   index：本行在当前页的下标。
std::unique_ptr<DuiControl> BuildImagePreview(const HistoryRowView& row, int index)
{
    std::unique_ptr<HistoryRowThumb> thumb(new HistoryRowThumb());
    thumb->SetImage(std::wstring((LPCWSTR)row.m_thumbImagePath),
                    row.m_thumbState, row.m_thumbClickable);
    thumb->SetCtrlId((UINT)(kIdHistoryThumbBase + index));
    //悬停提示（"单击下载图片" / "下载失败，单击重试"）。控件析构时 DuiControl
    //会自动注销提示，列表重建不会留下指向旧控件的登记。
    if (!row.m_thumbTooltip.IsEmpty())
    {
        balloonwjui::DuiToolTipMgr::Inst().Register(thumb.get(), row.m_thumbTooltip);
    }
    return std::unique_ptr<DuiControl>(thumb.release());
}

// 造帖子卡片正文下面那一行缩略图（docs/帖子群方案.md 14.5）：最多 kHistoryCardMaxThumbs 张，各按原图宽高等比缩放、
// 贴顶排列；最后一张按需写「+N」。每张的编号 = kIdHistoryCardThumbBase + 行下标 × kHistoryCardMaxThumbs + 第几张。
//   row：  行数据（取各张的路径、状态、宽高与提示，以及「+N」的 N）。
//   index：本行在当前页的下标。
std::unique_ptr<DuiControl> BuildCardThumbStrip(const HistoryRowView& row, int index)
{
    std::unique_ptr<DuiHBox> strip(new DuiHBox());
    strip->SetGap(kHistoryCardThumbGap);
    const size_t count = (row.m_cardThumbs.size() < kHistoryCardMaxThumbs) ? row.m_cardThumbs.size()
                                                                           : kHistoryCardMaxThumbs;
    for (size_t k = 0; k < count; ++k)
    {
        const HistoryCardThumbView& view = row.m_cardThumbs[k];
        std::unique_ptr<HistoryCardThumb> thumb(new HistoryCardThumb());
        //一律可点：本机有原图时看图，没有时先下载
        thumb->SetImage(std::wstring((LPCWSTR)view.m_path), view.m_state, true);
        if (k + 1 == count)
        {
            thumb->SetExtraCount(row.m_cardThumbExtra);
        }
        thumb->SetCtrlId((UINT)(kIdHistoryCardThumbBase + (UINT)index * (UINT)kHistoryCardMaxThumbs + (UINT)k));
        if (!view.m_tooltip.IsEmpty())
        {
            balloonwjui::DuiToolTipMgr::Inst().Register(thumb.get(), view.m_tooltip);
        }
        int thumbW = 0;
        int thumbH = 0;
        ComputeHistoryCardThumbSize(view.m_width, view.m_height, thumbW, thumbH);
        strip->AddChild(std::unique_ptr<DuiControl>(thumb.release()),
                        Hint().Fixed(thumbW, thumbH).AlignC(DuiLayout::AlignNear));
    }
    //余下的空间留白，缩略图靠左
    strip->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(strip.release());
}

// 造文件消息的卡片：[类型色块][文件名 / 大小]。
//
// 文件名用正文控件（单行、放不下时末尾省略号）而不是普通标签：搜索文件名时命中处
// 要铺高亮底，文件名也要能拖选复制。高亮区间是按"文件名 · 大小"那段正文算的，文件名
// 排在最前面，故落在文件名范围内的区间可以原样使用，超出的部分截掉。
//   row：行数据（取扩展名、文件名、大小文字、高亮区间）。
std::unique_ptr<DuiControl> BuildFileCard(const HistoryRowView& row)
{
    std::unique_ptr<DuiHBox> card(new DuiHBox());
    card->SetGap(kFileCardGap);

    //类型色块：固定边长并贴在卡片顶部，不随卡片高度拉伸。
    std::unique_ptr<HistoryRowThumb> block(new HistoryRowThumb());
    block->SetFile(row.m_thumbFileExt);
    card->AddChild(std::unique_ptr<DuiControl>(block.release()),
                   Hint().Fixed(kFileCardBlockSize, kFileCardBlockSize).AlignC(DuiLayout::AlignNear));

    //文件名缺失（老数据）时退回整段正文，至少不会显示成空白。
    const CString name = row.m_fileName.IsEmpty() ? row.m_fullText : row.m_fileName;
    const int nameLen = name.GetLength();
    std::vector<HighlightRange> nameRanges;
    for (size_t i = 0; i < row.m_highlights.size(); ++i)
    {
        HighlightRange r = row.m_highlights[i];
        if (r.m_begin < 0 || r.m_len <= 0 || r.m_begin >= nameLen)
        {
            continue;
        }
        if (r.m_begin + r.m_len > nameLen)
        {
            r.m_len = nameLen - r.m_begin;
        }
        nameRanges.push_back(r);
    }

    std::unique_ptr<DuiVBox> texts(new DuiVBox());
    std::unique_ptr<HistoryContentView> nameView(new HistoryContentView());
    nameView->SetPlainContent(name);
    nameView->SetSingleLine(true);
    nameView->SetTextColor(kColorName);
    nameView->SetHighlightRanges(nameRanges);
    texts->AddChild(std::unique_ptr<DuiControl>(nameView.release()), Hint().Fixed(kFileNameLineH));
    if (!row.m_fileSizeText.IsEmpty())
    {
        texts->AddChild(MakeLabel(row.m_fileSizeText, kColorTime,
                                  DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                        Hint().Fixed(kFileSizeLineH));
    }
    card->AddChild(std::unique_ptr<DuiControl>(texts.release()), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(card.release());
}

// 造合并转发聊天记录的卡片：[色块 "记录"][标题链接 / "聊天记录（共 N 条）"]。
//
// 标题用链接模式的标签：单击以 kIdHistoryCardBase + index 发 DUIN_CLICK，窗口据此打开
// 查看窗。不用正文控件显示标题 —— 正文控件把单击、双击都当作文字选择处理，不往外发通知，
// 用户点在标题上就什么都不会发生。
//   row：  行数据（取标题与条数）。
//   index：本行在当前页的下标。
std::unique_ptr<DuiControl> BuildMergedCard(const HistoryRowView& row, int index)
{
    std::unique_ptr<DuiHBox> card(new DuiHBox());
    card->SetGap(kFileCardGap);

    //色块与文件卡片的类型色块同一尺寸、同一画法，里面写"记录"两个字
    std::unique_ptr<HistoryRowThumb> block(new HistoryRowThumb());
    block->SetFile(TR(kMergedCardBlockText));
    card->AddChild(std::unique_ptr<DuiControl>(block.release()),
                   Hint().Fixed(kFileCardBlockSize, kFileCardBlockSize).AlignC(DuiLayout::AlignNear));

    std::unique_ptr<DuiVBox> texts(new DuiVBox());
    std::unique_ptr<DuiLabel> title(new DuiLabel());
    title->SetText(row.m_mergedTitle.IsEmpty() ? CString(TR(kMergedCardDefaultTitle)) : row.m_mergedTitle);
    title->SetCtrlId((UINT)(kIdHistoryCardBase + index));
    //ModeLink 才会悬停变手型、点击发 DUIN_CLICK；不打开浏览器，点击交给窗口处理
    title->SetMode(DuiLabel::ModeLink);
    title->SetAutoNavigate(false);
    title->SetLinkColor(kColorName);
    title->SetHoverColor(kColorLocateText);
    title->SetVisitedColor(kColorName);
    title->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    texts->AddChild(std::unique_ptr<DuiControl>(title.release()), Hint().Fixed(kFileNameLineH));

    CString countText;
    countText.Format(TR(L"聊天记录（共 %d 条）"), row.m_mergedCount);
    texts->AddChild(MakeLabel(countText, kColorTime, DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                    Hint().Fixed(kFileSizeLineH));
    card->AddChild(std::unique_ptr<DuiControl>(texts.release()), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(card.release());
}

// 时间轴竖线的一段：竖向分隔线，画在所在格子的正中
std::unique_ptr<DuiControl> MakeTimelineLine()
{
    std::unique_ptr<DuiSeparator> sep(new DuiSeparator());
    sep->SetOrientation(DuiSeparator::Vertical);
    sep->SetColor(kColorTimelineLine);
    sep->SetThickness(kTimelineLineThick);
    return std::unique_ptr<DuiControl>(sep.release());
}

// 时间轴的竖线节点列：上段竖线 + 节点「●」+ 下段竖线，三者首尾相接。
//   dotTop：节点那一格离列顶的距离（像素），使节点与右侧名字那一行对齐；日期大节点传 -1 表示上下居中
//   dotH：  节点那一格的高；pointSize：「●」的字号；color：节点颜色
std::unique_ptr<DuiControl> BuildTimelineRail(int dotTop, int dotH, int pointSize, COLORREF color)
{
    std::unique_ptr<DuiVBox> rail(new DuiVBox());
    rail->SetGap(0);
    if (dotTop >= 0)
    {
        rail->AddChild(MakeTimelineLine(), Hint().Fixed(dotTop));
    }
    else
    {
        rail->AddChild(MakeTimelineLine(), Hint().Weight(1));
    }
    std::unique_ptr<DuiControl> dot = MakeLabel(kTimelineDotText, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DuiLabel* dotLabel = dynamic_cast<DuiLabel*>(dot.get());
    if (dotLabel != NULL)
    {
        dotLabel->SetTextPointSize(pointSize, false);
    }
    rail->AddChild(std::move(dot), Hint().Fixed(dotH));
    rail->AddChild(MakeTimelineLine(), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(rail.release());
}

// 时间轴样式一行最前面的时间列：时间靠右，与右侧名字那一行对齐
std::unique_ptr<DuiControl> BuildTimelineTimeColumn(const CString& timeText)
{
    std::unique_ptr<DuiVBox> col(new DuiVBox());
    col->SetGap(0);
    col->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(kRowPadV));
    col->AddChild(MakeLabel(timeText, kColorTime, DT_RIGHT | DT_VCENTER | DT_SINGLELINE), Hint().Fixed(kNameLineH));
    col->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(col.release());
}

// 时间轴样式的日期大节点那一行：空出时间列 + 大节点 + 日期（粗体）+ 灰色说明（星期几、当天条数）。
// 与日期分组头一样不给 ctrlId，窗口按落点找行时会跳过它
std::unique_ptr<DuiControl> BuildTimelineDayRow(const CString& label, const CString& detail)
{
    std::unique_ptr<DuiHBox> dayRow(new DuiHBox());
    dayRow->SetPadding(kRowPadV, 0, kRowPadV, 0);
    dayRow->SetGap(0);
    dayRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(kTimelineTimeW));
    dayRow->AddChild(BuildTimelineRail(-1, kTimelineDayDotH, kTimelineDayDotPt, kColorTimelineDay),
                     Hint().Fixed(kTimelineRailW));
    std::unique_ptr<DuiControl> dateLabel = MakeLabel(label, kColorName, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DuiLabel* date = dynamic_cast<DuiLabel*>(dateLabel.get());
    if (date != NULL)
    {
        date->SetTextPointSize(kTimelineDayTextPt, true);
    }
    //粗体比默认字体宽，按标题区会话名同一比例放宽
    const int dateW = MeasureDefaultFontWidth(label) * kBoldWidthPercent / kPercentBase + kHeaderTitleSlackW;
    dayRow->AddChild(std::move(dateLabel), Hint().Fixed(dateW));
    dayRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(kTimelineDayGap));
    dayRow->AddChild(MakeLabel(detail, kColorSubtitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS),
                     Hint().Weight(1));
    return std::unique_ptr<DuiControl>(dayRow.release());
}

std::unique_ptr<DuiControl> BuildRow(const HistoryRowView& row, int index,
                                     int contentWidth, bool needsExpandLink,
                                     bool reserveRowActions)
{
    std::unique_ptr<HistoryRow> line(new HistoryRow());
    line->SetCtrlId((UINT)(kIdHistoryRowBase + index));
    //各列加进 host：普通样式就是行本身；时间轴样式是行里时间列、竖线列之后的内层容器，原来的上下内边距
    //挪到它身上，行本身不留上下内边距，竖线才能贯穿整行、与上下两行连成一条
    DuiLayout* host = line.get();
    std::unique_ptr<DuiHBox> timelineBody;
    if (row.m_timeline)
    {
        line->SetPadding(kRowPadV, 0, kRowPadV, 0);
        line->SetGap(0);
        line->AddChild(BuildTimelineTimeColumn(row.m_timeText), Hint().Fixed(kTimelineTimeW));
        //消息节点与名字那一行的竖直中线对齐
        const int dotTop = kRowPadV + (kNameLineH - kTimelineDotH) / 2;
        line->AddChild(BuildTimelineRail(dotTop, kTimelineDotH, kTimelineDotPt,
                                         row.m_timelineHit ? kColorTimelineHit : kColorTimelineDot),
                       Hint().Fixed(kTimelineRailW));
        timelineBody.reset(new DuiHBox());
        timelineBody->SetPadding(0, kRowPadV, 0, kRowPadV);
        timelineBody->SetGap(10);
        host = timelineBody.get();
    }
    else
    {
        line->SetPadding(8, 8, 8, 8);
        line->SetGap(10);
    }

    //多选模式：行首一个复选框，贴在行顶部与头像对齐。构建期设值不发通知，避免伪触发选择变化
    if (row.m_selectMode)
    {
        std::unique_ptr<DuiButton> box(new DuiButton());
        box->SetButtonType(DuiButton::StyleCheckbox);
        //透明底、不带文字：只画勾选框本身。默认样式会在整个控件矩形上再画一圈圆角底框，
        //与勾选框错位叠在一起（2026-10-01 实测）。与转发目标选择窗的复选框同一设置
        box->SetVariant(DuiButton::Variant::Ghost);
        box->SetText(_T(""));
        box->SetCtrlId((UINT)(kIdHistorySelectBase + index));
        box->SetCheck(row.m_selected, false);
        host->AddChild(std::unique_ptr<DuiControl>(box.release()),
                       Hint().Fixed(kSelectBoxW, kSelectBoxW).AlignC(DuiLayout::AlignNear));
    }

    //折叠行（与上一行同发送者、同一天）省掉头像，但<u>照旧占住那一列的宽度</u>，
    //使预览文字与上一行严丝合缝地对齐 —— 不占位的话这一行会整体左移，读起来像是
    //另起了一段。
    if (row.m_compact)
    {
        host->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(kAvatarSize));
    }
    else
    {
        std::unique_ptr<DuiAvatar> avatar(new DuiAvatar());
        avatar->SetShape(DuiAvatar::ShapeRoundRect);
        avatar->SetCornerRadius(kAvatarCorner);
        avatar->SetName(row.m_senderName);
        avatar->SetFallbackBgColor(row.m_avatarColor);
        //宽高都固定并贴在行顶部，与昵称、时间那一行对齐。行是 DuiHBox，子控件在竖直
        //方向上默认撑满整行，只固定宽度的话，正文一长头像就被纵向拉成长条。
        //注意 fixedCross 只有在对齐方式不是 AlignFill 时才生效，两者必须一起设置。
        host->AddChild(std::unique_ptr<DuiControl>(avatar.release()),
                       Hint().Fixed(kAvatarSize, kAvatarSize).AlignC(DuiLayout::AlignNear));
    }

    //图片与文件不再在头像右侧单独占一列（2026-09-30 起）：那一列把昵称与内容整体推向
    //右边，与文字行对不齐，36px 的缩略图也小得看不清。现在它们与文字消息一样，都放在
    //下面内容列里、昵称那一行的下方。
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
    //时间轴样式的时间写在行首的时间列里，这里不再重复
    if (!row.m_timeline)
    {
        nameLine->AddChild(MakeLabel(row.m_timeText, kColorTime,
                                     DT_LEFT | DT_VCENTER | DT_SINGLELINE), Hint().Fixed(kTimeWidth));
    }

    //已 Pin 的行在时间右边点一枚小图钉。行内那枚常驻的「取消」按钮在最右侧，
    //扫一列行时不容易看出"这条钉着"；标记贴着名字与时间，纵向扫下来一眼能挑出来。
    //图钉本身也可以点击取消 Pin，窗口按它的 id（基址 + 行下标）认出是哪一行。
    if (row.m_pinned)
    {
        std::unique_ptr<HistoryPinMark> pinMark(new HistoryPinMark());
        pinMark->SetCtrlId((UINT)(kIdHistoryPinMarkBase + index));
        nameLine->AddChild(std::unique_ptr<DuiControl>(pinMark.release()),
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

    if (RowIsImage(row))
    {
        //图片消息：昵称下方直接显示预览图，按原图宽高等比缩放，左对齐（交叉方向固定
        //宽度、贴近左边，否则会被横向拉满整列）。
        int dispW = 0;
        int dispH = 0;
        ComputeHistoryImageSize(row.m_imageWidth, row.m_imageHeight, dispW, dispH);
        mid->AddChild(BuildImagePreview(row, index),
                      Hint().Fixed(dispH, dispW).AlignC(DuiLayout::AlignNear));
    }
    else if (RowIsVoice(row))
    {
        //语音行：与聊天窗语音气泡同款的语音条，单击播放 / 停止（本机没有文件时先下载）。宽高都固定并贴左
        std::unique_ptr<balloonchatwnd::ChatVoiceStrip> strip(new balloonchatwnd::ChatVoiceStrip());
        strip->SetVoiceMessage(row.m_voice, row.m_voiceShowDot);
        strip->SetCtrlId((UINT)(kIdHistoryVoiceBase + index));
        mid->AddChild(std::unique_ptr<DuiControl>(strip.release()),
                      Hint().Fixed(balloonchatwnd::kVoiceStripH, balloonchatwnd::VoiceStripWidth(row.m_voice))
                          .AlignC(DuiLayout::AlignNear));
    }
    else if (RowIsChatFileCard(row))
    {
        //聊天文件卡片：与聊天窗文件气泡同样的卡片，可以直接下载、打开。宽高都固定并贴左，
        //不随内容列拉伸（交叉方向固定宽度只在对齐方式不是撑满时生效）
        std::unique_ptr<balloonchatwnd::ChatFileCardControl> card(
            new balloonchatwnd::ChatFileCardControl());
        card->SetCardMessage(row.m_fileCard);
        card->SetCtrlId((UINT)(kIdHistoryFileCardBase + index));
        mid->AddChild(std::unique_ptr<DuiControl>(card.release()),
                      Hint().Fixed(balloonchatwnd::FileCardHeight(row.m_fileCard),
                                   balloonchatwnd::kFileCardWidth).AlignC(DuiLayout::AlignNear));
    }
    else if (RowIsFile(row))
    {
        //文件消息：昵称下方显示一张"类型色块 + 文件名 + 大小"的卡片。
        mid->AddChild(BuildFileCard(row), Hint().Fixed(kFileCardH));
    }
    else if (RowIsMergedCard(row))
    {
        //合并转发的聊天记录：昵称下方显示一张"色块 + 标题 + 条数"的卡片，单击标题打开查看窗。
        mid->AddChild(BuildMergedCard(row, index), Hint().Fixed(kFileCardH));
    }
    else if (RowIsCode(row))
    {
        //代码片段：与聊天窗相同的代码卡片，宽高按内容列宽度量出（与行高的计算相同）并贴左；点卡片打开代码查看窗
        const std::wstring code((LPCWSTR)row.m_fullText);
        const SIZE cardSize = balloonchatwnd::ChatCodeCardControl::MeasureCard(code, row.m_codeLang, contentWidth,
                                                                               row.m_codeNotes);
        std::unique_ptr<balloonchatwnd::ChatCodeCardControl> card(new balloonchatwnd::ChatCodeCardControl());
        card->SetCode(code, row.m_codeLang, (row.m_msgid > 0) ? std::to_string(row.m_msgid) : std::string(),
                      row.m_codeNotes);
        card->SetCtrlId((UINT)(kIdHistoryCodeCardBase + index));
        mid->AddChild(std::unique_ptr<DuiControl>(card.release()),
                      Hint().Fixed(cardSize.cy, cardSize.cx).AlignC(DuiLayout::AlignNear));
    }
    else if (RowIsMarkdown(row))
    {
        //Markdown 消息：与聊天窗相同的卡片，宽高按内容列宽度量出（与行高的计算相同）并贴左；点卡片打开 Markdown 预览窗口
        const SIZE cardSize = balloonchatwnd::ChatMarkdownCardControl::MeasureCard(row.m_markdownCard.m_summary,
                                                                                   contentWidth);
        std::unique_ptr<balloonchatwnd::ChatMarkdownCardControl> card(new balloonchatwnd::ChatMarkdownCardControl());
        card->SetCard(row.m_markdownCard);
        card->SetCtrlId((UINT)(kIdHistoryMarkdownCardBase + index));
        mid->AddChild(std::unique_ptr<DuiControl>(card.release()),
                      Hint().Fixed(cardSize.cy, cardSize.cx).AlignC(DuiLayout::AlignNear));
    }
    else
    {
        //正文内容块：多行、把 /f["<id>"] 画成表情、搜索命中处铺一层淡黄底。
        //长文默认折叠（最多 kHistoryCollapsedMaxLines 行），由下面那枚链接展开。
        std::unique_ptr<HistoryContentView> content(new HistoryContentView());
        content->SetContent(row.m_fullText);
        content->SetTextColor(kColorPreview);
        content->SetHighlightRanges(row.m_highlights);
        content->SetExpanded(row.m_expanded);
        const int contentH = content->MeasureHeight(contentWidth);
        mid->AddChild(std::unique_ptr<DuiControl>(content.release()), Hint().Fixed(contentH));
        //帖子卡片与补充卡片带图片时，正文下面排一行缩略图（docs/帖子群方案.md 14.5）
        if (RowHasCardThumbs(row))
        {
            mid->AddChild(BuildCardThumbStrip(row, index), Hint().Fixed(HistoryCardThumbStripHeight(row)));
        }
    }

    //「展开 / 收起」链接：只有正文真的超出折叠行数时才建 —— 内容本来就画得下却挂着
    //一个「展开」，点下去毫无变化，比不给还糟。
    if (needsExpandLink)
    {
        std::unique_ptr<DuiLabel> expandLink(new DuiLabel());
        expandLink->SetText(row.m_expanded ? TR(L"收起") : TR(L"展开"));
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

    host->AddChild(std::move(mid), Hint().Weight(1));

    //不预留操作列（聊天记录查看窗）：不建 Pin / 定位两个按钮槽，正文列用满整行。
    //RowContentWidth 收到的是同一个值，测量宽度与实际宽度一致
    if (!reserveRowActions)
    {
        if (timelineBody)
        {
            line->AddChild(std::unique_ptr<DuiControl>(timelineBody.release()), Hint().Weight(1));
        }
        return std::unique_ptr<DuiControl>(line.release());
    }

    //“Pin / 取消”按钮：与“定位”同一套次操作风格，排在它左边。文案随本条当前是否
    //被钉住取反 —— 不新增第二枚按钮，一枚按钮按状态切换含义，与气泡右键菜单里那一项
    //口径一致。点下去只发请求，界面等服务端推送回来再刷。
    std::unique_ptr<DuiButton> pin(new DuiButton());
    pin->SetButtonType(DuiButton::StylePushButton);
    pin->SetText(row.m_pinned ? TR(L"取消") : _T("Pin"));
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
    //按钮槽的高度同样固定并贴在行顶部：按钮在槽内撑满，槽若随行高拉伸，悬停时显示
    //出来的按钮就是一个与整行等高的长条。
    host->AddChild(std::unique_ptr<DuiControl>(pinSlot.release()),
                   Hint().Fixed(kRowPinW, kRowActionH).AlignC(DuiLayout::AlignNear));

    //“定位”按钮：次操作风格（白底 + 1px 灰边 + 柔和灰字、字号略小），轻量不抢眼，
    //对齐设计稿的浅灰小药丸；不用 Outlined（那是品牌蓝边 + 蓝字，过于扎眼）。
    std::unique_ptr<DuiButton> locate(new DuiButton());
    locate->SetButtonType(DuiButton::StylePushButton);
    locate->SetText(TR(L"定位"));
    locate->SetCtrlId((UINT)(kIdHistoryLocateBase + index));
    locate->SetVariant(DuiButton::Variant::Default);
    locate->SetTextColor(kColorLocateText);
    locate->SetTextPointSize(9);
    DuiControl* locateRaw = locate.get();
    std::unique_ptr<DuiHBox> locateSlot(new DuiHBox());
    locateSlot->AddChild(std::unique_ptr<DuiControl>(locate.release()), Hint().Weight(1));
    //与 Pin 按钮槽同理，固定高度并贴在行顶部。
    host->AddChild(std::unique_ptr<DuiControl>(locateSlot.release()),
                   Hint().Fixed(kLocateW, kRowActionH).AlignC(DuiLayout::AlignNear));

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

    //时间轴样式：内层容器排在时间列、竖线列之后，占满余下宽度
    if (timelineBody)
    {
        line->AddChild(std::unique_ptr<DuiControl>(timelineBody.release()), Hint().Weight(1));
    }
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

/**
 *  造时间范围下拉（全部时间 / 近 7 天 / 近 30 天 / 近 3 个月 / 近半年 / 近一年）。各项的<u>顺序必须与
 *  HistoryTimeRange 的枚举值一一对应</u>（0 起）—— 窗口侧的 TimeRangeFromComboIndex / ComboIndexFromRange
 *  就是照这个约定直接转换的，插项时两边要一起改。自定义起止不在这里，由「选日期」弹层设置。
 *  聊天记录页与文件 / 图片页共用。
 *    ctrlId：控件编号。selIndex：选中项下标，构建期设值不发通知。
 *    outWidth：[出参] 按最宽的一项实测出的下拉宽度（像素）—— 写死像素在高 DPI 下会把「全部时间」截成
 *              「全部时...」，理由见 MeasureDefaultFontWidth。
 *  @return 下拉框，调用方拿到所有权
 */
std::unique_ptr<DuiControl> MakeTimeCombo(UINT ctrlId, int selIndex, int& outWidth)
{
    static const wchar_t* const kTimeComboItems[] =
    {
        TR_NOOP(L"全部时间"), TR_NOOP(L"近 7 天"), TR_NOOP(L"近 30 天"),
        TR_NOOP(L"近 3 个月"), TR_NOOP(L"近半年"), TR_NOOP(L"近一年"),
    };
    const int kTimeComboItemCount = (int)(sizeof(kTimeComboItems) / sizeof(kTimeComboItems[0]));

    std::unique_ptr<DuiComboBox> timeCombo(new DuiComboBox());
    timeCombo->SetCtrlId(ctrlId);
    outWidth = kTimeComboMinW;
    for (int i = 0; i < kTimeComboItemCount; ++i)
    {
        const LPCTSTR item = TR(kTimeComboItems[i]);
        timeCombo->AddString(item);
        const int need = MeasureDefaultFontWidth(item) + kComboChromeW + kComboSlackW;
        if (need > outWidth)
        {
            outWidth = need;
        }
    }
    timeCombo->SetCurSel(selIndex, false);
    return std::unique_ptr<DuiControl>(timeCombo.release());
}

} // anonymous namespace

// ---- 公开给文件 / 图片页布局（HistoryLibraryLayout.cpp）复用的小工具，见 HistoryLayoutKit.h ----
namespace layoutkit {

COLORREF TitleColor()
{
    return kColorTitle;
}

COLORREF SubtitleColor()
{
    return kColorSubtitle;
}

COLORREF NameColor()
{
    return kColorName;
}

COLORREF TimeColor()
{
    return kColorTime;
}

COLORREF EmptyTitleColor()
{
    return kColorEmptyTitle;
}

std::unique_ptr<DuiControl> Label(LPCTSTR text, COLORREF color, DWORD dtAlign, UINT ctrlId)
{
    return MakeLabel(text, color, dtAlign, ctrlId);
}

std::unique_ptr<DuiControl> Button(LPCTSTR text, UINT ctrlId, DuiButton::Variant variant)
{
    return MakeButton(text, ctrlId, variant);
}

std::unique_ptr<DuiControl> Chip(LPCTSTR text, UINT ctrlId, bool active)
{
    return MakeChip(text, ctrlId, active);
}

std::unique_ptr<DuiControl> Separator()
{
    return MakeSeparator();
}

std::unique_ptr<DuiControl> WildcardCheckbox(UINT ctrlId, bool checked, int& outWidth)
{
    return MakeWildcardCheckbox(ctrlId, checked, outWidth);
}

std::unique_ptr<DuiControl> TimeCombo(UINT ctrlId, int selIndex, int& outWidth)
{
    return MakeTimeCombo(ctrlId, selIndex, outWidth);
}

int TextWidth(LPCTSTR text)
{
    return MeasureDefaultFontWidth(text);
}

int GrowWidth(LPCTSTR text, int designWidth, int padding)
{
    return GrowWidthToFitText(text, designWidth, padding);
}

int ChipPadWidth()
{
    return kGrowChipPadW;
}

int ComboChromeWidth()
{
    return kComboChromeW + kComboSlackW;
}

} // namespace layoutkit

void ComputeHistoryCardThumbSize(int realW, int realH, int& outW, int& outH)
{
    //宽高缺失：取方框本身，占位与下载下来的图同样大
    if (realW <= 0 || realH <= 0)
    {
        outW = kHistoryCardThumbPx;
        outH = kHistoryCardThumbPx;
        return;
    }
    //按长边缩进方框，小图不放大；四舍五入到整像素，至少 1 像素
    const int longSide = (realW > realH) ? realW : realH;
    const double scale = (longSide > kHistoryCardThumbPx) ? ((double)kHistoryCardThumbPx / (double)longSide) : 1.0;
    const double kRoundHalf = 0.5;
    outW = (int)(realW * scale + kRoundHalf);
    outH = (int)(realH * scale + kRoundHalf);
    if (outW < 1)
    {
        outW = 1;
    }
    if (outH < 1)
    {
        outH = 1;
    }
}

std::vector<HistoryRowView> ExpandHistoryGridCells(const std::vector<HistoryRowView>& rows, size_t capacity,
                                                   std::vector<HistoryGridCellRef>& refsOut)
{
    refsOut.clear();
    std::vector<HistoryRowView> cells;
    for (size_t i = 0; i < rows.size() && cells.size() < capacity; ++i)
    {
        const HistoryRowView& row = rows[i];
        //图片消息：原样一格
        if (row.m_cardThumbs.empty())
        {
            cells.push_back(row);
            HistoryGridCellRef ref;
            ref.m_row = (int)i;
            refsOut.push_back(ref);
            continue;
        }
        //帖子卡片：每张图片一格，格子上画那张图；分组头只跟着第一格
        for (size_t k = 0; k < row.m_cardThumbs.size() && cells.size() < capacity; ++k)
        {
            const HistoryCardThumbView& thumb = row.m_cardThumbs[k];
            HistoryRowView cell = row;
            cell.m_cardThumbs.clear();
            cell.m_cardThumbExtra = 0;
            if (k > 0)
            {
                cell.m_groupLabel.Empty();
            }
            cell.m_hasImageThumb = true;
            cell.m_thumbImagePath = thumb.m_path;
            cell.m_thumbState = thumb.m_state;
            cell.m_thumbClickable = true;
            cell.m_thumbTooltip = thumb.m_tooltip;
            cell.m_imageWidth = thumb.m_width;
            cell.m_imageHeight = thumb.m_height;
            cells.push_back(cell);
            HistoryGridCellRef ref;
            ref.m_row = (int)i;
            ref.m_image = (int)k;
            refsOut.push_back(ref);
        }
    }
    return cells;
}

int HistoryCardThumbStripHeight(const HistoryRowView& row)
{
    const size_t count = (row.m_cardThumbs.size() < kHistoryCardMaxThumbs) ? row.m_cardThumbs.size()
                                                                           : kHistoryCardMaxThumbs;
    int tallest = 0;
    for (size_t k = 0; k < count; ++k)
    {
        int w = 0;
        int h = 0;
        ComputeHistoryCardThumbSize(row.m_cardThumbs[k].m_width, row.m_cardThumbs[k].m_height, w, h);
        if (h > tallest)
        {
            tallest = h;
        }
    }
    return tallest;
}

bool ResolveHistoryCardThumbClick(UINT id, int& rowOut, int& thumbOut)
{
    if (id < (UINT)kIdHistoryCardThumbBase || id >= (UINT)kIdHistoryCardThumbBase + kIdHistoryCardThumbCapacity)
    {
        return false;
    }
    const UINT offset = id - (UINT)kIdHistoryCardThumbBase;
    rowOut = (int)(offset / (UINT)kHistoryCardMaxThumbs);
    thumbOut = (int)(offset % (UINT)kHistoryCardMaxThumbs);
    return true;
}

void ComputeHistoryImageSize(int realW, int realH, int& outW, int& outH)
{
    //宽高缺失（老消息、字段不全）：取默认尺寸，占位与之后下载下来的图同样大。
    if (realW <= 0 || realH <= 0)
    {
        outW = kHistoryImageDefaultW;
        outH = kHistoryImageDefaultH;
        return;
    }

    //先让宽不超过上限，再看高：高仍超出就改按高定比例。两步取的都是更小的那个比例，
    //结果必然同时落在宽、高两个上限之内。与聊天窗 ComputeImageDisplaySize 的算法一致。
    double scale = 1.0;
    if (realW > kHistoryImageMaxW)
    {
        scale = (double)kHistoryImageMaxW / (double)realW;
    }
    if (realH * scale > kHistoryImageMaxH)
    {
        scale = (double)kHistoryImageMaxH / (double)realH;
    }

    //四舍五入到整像素；极窄或极扁的图缩完可能不足 1 像素，至少留 1 像素。
    const double kRoundHalf = 0.5;
    outW = (int)(realW * scale + kRoundHalf);
    outH = (int)(realH * scale + kRoundHalf);
    if (outW < 1)
    {
        outW = 1;
    }
    if (outH < 1)
    {
        outH = 1;
    }
}

int HistoryRowIndexOfControl(const DuiControl* ctrl)
{
    //沿父链往上找第一个行容器。行容器之外（分组头、筛选栏、分页器）一路找到根也
    //找不到，返回 -1。
    const DuiControl* node = ctrl;
    while (node != NULL)
    {
        if (dynamic_cast<const HistoryRow*>(node) != NULL)
        {
            const UINT id = node->GetCtrlId();
            if (id >= (UINT)kIdHistoryRowBase
                && id < (UINT)kIdHistoryRowBase + kIdHistoryRowCapacity)
            {
                return (int)(id - (UINT)kIdHistoryRowBase);
            }
            return -1;
        }
        node = node->GetParent();
    }
    return -1;
}

bool ResolveHistoryPinClick(UINT id, const std::vector<bool>& pagePinned,
                            int& rowOut, bool& targetPinOut)
{
    int  row       = -1;
    bool targetPin = false;
    if (id >= (UINT)kIdHistoryPinMarkBase
        && id < (UINT)kIdHistoryPinMarkBase + kIdHistoryPinMarkCapacity)
    {
        //图钉：只出现在已 Pin 的行上，点它就是取消
        row       = (int)(id - (UINT)kIdHistoryPinMarkBase);
        targetPin = false;
    }
    else if (id >= (UINT)kIdHistoryPinBase
             && id < (UINT)kIdHistoryPinBase + kIdHistoryPinCapacity)
    {
        //行内按钮：按该行当前状态取反，行下标越界时下面统一拒绝
        row = (int)(id - (UINT)kIdHistoryPinBase);
        if (row < (int)pagePinned.size())
        {
            targetPin = !pagePinned[(size_t)row];
        }
    }
    else
    {
        return false;
    }

    //行下标超出当前页：通常是翻页、刷新之后旧控件的通知迟到，不能按新页的行去改
    if (row < 0 || row >= (int)pagePinned.size())
    {
        return false;
    }
    rowOut       = row;
    targetPinOut = targetPin;
    return true;
}

std::unique_ptr<DuiControl> BuildListContent(const std::vector<HistoryRowView>& rows,
                                             LPCTSTR emptyTitle, LPCTSTR emptySubtitle,
                                             int listWidth, bool reserveRowActions,
                                             int pendingDownloadCount)
{
    std::unique_ptr<DuiVBox> listContent(new DuiVBox());
    listContent->SetCtrlId(kIdHistoryList);
    listContent->SetPadding(kListPadH, 0, kListPadH, 0);
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

    //「文件」分类：本页有没下载的文件时，最前面放一条「本页有 N 个文件还没有下载 [全部下载]」。
    //它不是 HistoryRow，窗口按落点找行时会跳过它
    if (pendingDownloadCount > 0)
    {
        std::unique_ptr<DuiHBox> bar(new DuiHBox());
        bar->SetPadding(0, 6, 0, 6);
        bar->SetGap(8);
        CString info;
        info.Format(TR(L"本页有 %d 个文件还没有下载"), pendingDownloadCount);
        bar->AddChild(MakeLabel(info, kColorSubtitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kIdHistoryBatchInfo),
                      DuiLayout::Hint().Weight(1));
        bar->AddChild(MakeButton(TR(L"全部下载"), kIdHistoryBatchDownload, DuiButton::Variant::Outlined),
                      DuiLayout::Hint().Fixed(kBatchBtnW));
        listContent->AddChild(std::unique_ptr<DuiControl>(bar.release()), DuiLayout::Hint().Fixed(kBatchBarH));
    }

    for (size_t i = 0; i < rows.size(); ++i)
    {
        //日期分组头：不可点、不给 ctrlId —— 它不是一行消息，不该占行按钮的 id 段，
        //否则后面各行的"定位 / Pin"按钮下标就与 m_model.Records() 的下标对不上了。
        //时间轴样式的分组头画成竖线上的日期大节点（docs/聊天记录管理器方案.md §11.7），同样不给 ctrlId
        if (!rows[i].m_groupLabel.IsEmpty() && rows[i].m_timeline)
        {
            listContent->AddChild(BuildTimelineDayRow(rows[i].m_groupLabel, rows[i].m_groupDetail),
                                  DuiLayout::Hint().Fixed(kTimelineDayH));
        }
        else if (!rows[i].m_groupLabel.IsEmpty())
        {
            listContent->AddChild(MakeLabel(rows[i].m_groupLabel, kColorSubtitle,
                                            DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                                  DuiLayout::Hint().Fixed(kGroupHeaderH));
        }

        //行高按正文实际折行的行数算，不再是编译期常量 —— 长消息折叠态最多 5 行，
        //展开后要多高就多高。宽度与"要不要展开链接"两处都由同一次测量给出，
        //保证测量与随后的 BuildRow 用的是同一套结论。
        int contentW = RowContentWidth(listWidth, reserveRowActions);
        if (rows[i].m_selectMode)
        {
            contentW -= kSelectColumnW;
        }
        //时间轴样式的行首多了时间列与竖线列；行的上下内边距挪到了内层容器上，行高不变
        if (rows[i].m_timeline)
        {
            contentW -= kTimelineLeadW;
        }
        bool needsExpand = false;
        const int rowH = MeasureRowHeight(rows[i], contentW, needsExpand);
        listContent->AddChild(BuildRow(rows[i], (int)i, contentW, needsExpand, reserveRowActions),
                              DuiLayout::Hint().Fixed(rowH));
    }
    return std::unique_ptr<DuiControl>(listContent.release());
}

std::unique_ptr<DuiControl> BuildImageGridContent(const std::vector<HistoryRowView>& rows,
                                                  LPCTSTR emptyTitle, LPCTSTR emptySubtitle,
                                                  int listWidth, UINT listCtrlId, UINT cellIdBase)
{
    //空列表：与逐行列表的空状态完全一样
    if (rows.empty())
    {
        return BuildListContent(rows, emptyTitle, emptySubtitle, listWidth);
    }
    if (listWidth <= 0)
    {
        listWidth = kListFallbackWidth;
    }
    HistoryGridMetrics metrics;
    metrics.m_cellSize = kGridCellSize;
    metrics.m_gap = kGridGap;
    metrics.m_headerH = kGroupHeaderH;
    metrics.m_padding = kListPadH;
    const int columns = GridColumnCount(listWidth, metrics);

    std::unique_ptr<DuiVBox> grid(new DuiVBox());
    grid->SetCtrlId(listCtrlId);
    grid->SetPadding(kListPadH, kGridPadV, kListPadH, kGridPadV);
    grid->SetGap(0);

    //逐组排：遇到日期分组头（m_groupLabel 非空）就收掉上一行、放标题；一行满 columns 张换行
    std::unique_ptr<DuiHBox> line;
    int inLine = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const HistoryRowView& row = rows[i];
        const bool newGroup = !row.m_groupLabel.IsEmpty();
        if ((newGroup || inLine == columns) && line)
        {
            //行与行之间留出间距；补一个弹性占位，让不满的一行靠左
            line->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), DuiLayout::Hint().Weight(1));
            grid->AddChild(std::unique_ptr<DuiControl>(line.release()), DuiLayout::Hint().Fixed(kGridCellSize));
            grid->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), DuiLayout::Hint().Fixed(kGridGap));
            inLine = 0;
        }
        if (newGroup)
        {
            grid->AddChild(MakeLabel(row.m_groupLabel, kColorSubtitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                           DuiLayout::Hint().Fixed(kGroupHeaderH));
        }
        if (!line)
        {
            line.reset(new DuiHBox());
            line->SetGap(kGridGap);
        }

        std::unique_ptr<HistoryRowThumb> thumb(new HistoryRowThumb());
        //网格里单击一律是"看大图"（本地没有时先下载），故恒为可点击
        thumb->SetImage(std::wstring((LPCWSTR)row.m_thumbImagePath), row.m_thumbState, true);
        thumb->SetCtrlId((UINT)(cellIdBase + i));
        CString tip = row.m_senderName + _T(" · ") + row.m_timeText;
        if (!row.m_thumbTooltip.IsEmpty())
        {
            tip += _T(" · ") + row.m_thumbTooltip;
        }
        balloonwjui::DuiToolTipMgr::Inst().Register(thumb.get(), tip);
        line->AddChild(std::unique_ptr<DuiControl>(thumb.release()),
                       DuiLayout::Hint().Fixed(kGridCellSize, kGridCellSize));
        ++inLine;
    }
    if (line)
    {
        line->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), DuiLayout::Hint().Weight(1));
        grid->AddChild(std::unique_ptr<DuiControl>(line.release()), DuiLayout::Hint().Fixed(kGridCellSize));
    }
    return std::unique_ptr<DuiControl>(grid.release());
}

std::unique_ptr<DuiControl> BuildMultiSelectBar()
{
    std::unique_ptr<DuiHBox> bar(new DuiHBox());
    bar->SetCtrlId(kIdHistorySelectBar);
    //内边距与底部分页栏一致，两者换来换去时按钮位置不跳
    bar->SetPadding(kPadH, 8, kPadH, 8);
    bar->SetGap(8);
    bar->AddChild(MakeLabel(TR(L"已选 0 条"), kColorSubtitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE,
                            kIdHistorySelectInfo),
                  Hint().Weight(1));
    bar->AddChild(MakeButton(TR(L"合并转发"), kIdHistorySelectMerge, DuiButton::Variant::Primary),
                  Hint().Fixed(kSelectBtnW));
    bar->AddChild(MakeButton(TR(L"逐条转发"), kIdHistorySelectEach, DuiButton::Variant::Outlined),
                  Hint().Fixed(kSelectBtnW));
    bar->AddChild(MakeButton(TR(L"删除"), kIdHistorySelectDelete, DuiButton::Variant::Outlined),
                  Hint().Fixed(kSelectSmallBtnW));
    bar->AddChild(MakeButton(TR(L"取消"), kIdHistorySelectCancel, DuiButton::Variant::Default),
                  Hint().Fixed(kSelectSmallBtnW));
    return std::unique_ptr<DuiControl>(bar.release());
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
    box->AddChild(MakeButton(TR(L"首页"), kIdHistoryPageFirst, DuiButton::Variant::Default),
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
    box->AddChild(MakeButton(TR(L"末页"), kIdHistoryPageLast, DuiButton::Variant::Default),
                  Hint().Fixed(kPagerBtnW));
}

std::unique_ptr<DuiControl> BuildHistoryWindowBody(const HistoryViewData& data,
                                                   int listRightInsetPx)
{
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetCtrlId(kIdHistoryBody);
    root->SetBgColor(kColorWhite);
    root->SetPadding(0);
    root->SetGap(0);

    // 1. 标题区一行：「»」（聊天记录管理器左栏折叠时才显示）+ 会话名 + “共 N 条消息”。
    //    会话名与「»」的显隐、宽度由 ApplyHistoryHeaderTitle 设置。
    std::unique_ptr<DuiVBox> header(new DuiVBox());
    header->SetPadding(kPadH, 8, kPadH, 6);
    header->SetGap(0);
    std::unique_ptr<DuiHBox> headerRow(new DuiHBox());
    headerRow->SetCtrlId(kIdHistoryHeaderRow);
    headerRow->SetGap(kHeaderRowGap);
    headerRow->AddChild(MakeButton(kHistoryExpandLeftText, kIdHistoryExpandLeft, DuiButton::Variant::Ghost),
                        Hint().Fixed(kExpandLeftW));
    std::unique_ptr<DuiControl> title = MakeLabel(_T(""), kColorTitle,
                                                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                                  kIdHistoryTitle);
    DuiLabel* titleLabel = dynamic_cast<DuiLabel*>(title.get());
    if (titleLabel != NULL)
    {
        titleLabel->SetTextPointSize(kHeaderTitlePt, true);
    }
    headerRow->AddChild(std::move(title), Hint().Fixed(0));
    CString sub;
    sub.Format(TR(L"共 %d 条消息"), data.m_totalCount);
    headerRow->AddChild(MakeLabel(sub, kColorSubtitle,
                                  DT_LEFT | DT_VCENTER | DT_SINGLELINE, kIdHistorySubtitle),
                        Hint().Weight(1));
    header->AddChild(std::move(headerRow), Hint().Fixed(kSubLineH));
    root->AddChild(std::move(header), Hint().Fixed(kHeaderH));
    root->AddChild(MakeSeparator(), Hint().Fixed(1));

    // 2. 搜索行：搜索框（左，撑满）+「通配符」复选框 + 发送人下拉（右，固定）。
    std::unique_ptr<DuiHBox> searchRow(new DuiHBox());
    searchRow->SetPadding(kPadH, 8, kPadH, 6);
    searchRow->SetGap(kSearchRowGap);
    //悬停时显示搜索语法说明（docs/聊天记录管理器方案.md §12.3）
    std::unique_ptr<HistoryTipSearchBox> search(new HistoryTipSearchBox(HistorySearchSyntaxTip()));
    search->SetCtrlId(kIdHistorySearch);
    search->SetPlaceholder(TR(L"搜索消息内容、文件名、@某人"));
    searchRow->AddChild(std::unique_ptr<DuiControl>(search.release()), Hint().Weight(1));
    int wildcardW = 0;
    std::unique_ptr<DuiControl> wildcard = MakeWildcardCheckbox(kIdHistoryWildcard, data.m_wildcard, wildcardW);
    searchRow->AddChild(std::move(wildcard), Hint().Fixed(wildcardW));
    //这里只放「全部成员」一项，发过言的人由窗口查本地库后填入（见
    //CBalloonHistoryWindow::RefreshSenderCombo）——名字要经主面板解析，布局函数拿不到
    std::unique_ptr<DuiComboBox> senderCombo(new DuiComboBox());
    senderCombo->SetCtrlId(kIdHistorySenderCombo);
    senderCombo->AddString(TR(kHistorySenderAllText));
    senderCombo->SetCurSel(0, false);   //构建期设值不发通知，避免伪触发刷新
    int senderComboW = MeasureDefaultFontWidth(TR(kHistorySenderAllText)) + kComboChromeW + kComboSlackW;
    if (senderComboW < kSenderComboMinW)
    {
        senderComboW = kSenderComboMinW;
    }
    searchRow->AddChild(std::unique_ptr<DuiControl>(senderCombo.release()), Hint().Fixed(senderComboW));
    //「列表 | 时间轴」切换（docs/聊天记录管理器方案.md §11.7）：两枚胶囊紧挨着排，选中的一枚是主色。
    //筛选行在最小宽度下已经放不下更多控件（七枚类型胶囊、时间下拉与「选日期」合计超过可用宽度），
    //故放在搜索行最右边，搜索框照样撑满余下的宽度
    std::unique_ptr<DuiHBox> viewSeg(new DuiHBox());
    viewSeg->SetGap(0);
    const int viewListW = GrowWidthToFitText(TR(L"列表"), kViewChipW, kGrowChipPadW);
    const int viewTimelineW = GrowWidthToFitText(TR(L"时间轴"), kViewChipW, kGrowChipPadW);
    viewSeg->AddChild(MakeChip(TR(L"列表"), kIdHistoryViewList, !data.m_timeline), Hint().Fixed(viewListW));
    viewSeg->AddChild(MakeChip(TR(L"时间轴"), kIdHistoryViewTimeline, data.m_timeline), Hint().Fixed(viewTimelineW));
    searchRow->AddChild(std::unique_ptr<DuiControl>(viewSeg.release()), Hint().Fixed(viewListW + viewTimelineW));
    root->AddChild(std::move(searchRow), Hint().Fixed(kSearchRowH));

    // 3. 筛选行：类型胶囊（左，撑满）+ 时间区域（右）：时间下拉或日期区间标签二者显示其一，
    //    最右是「选日期」按钮。
    std::unique_ptr<DuiHBox> filterRow(new DuiHBox());
    filterRow->SetCtrlId(kIdHistoryFilterRow);
    filterRow->SetPadding(kPadH, 2, kPadH, 8);
    filterRow->SetGap(8);
    std::unique_ptr<DuiHBox> chips(new DuiHBox());
    chips->SetGap(8);
    chips->AddChild(MakeChip(TR(L"全部"), kIdHistoryChipAll, data.m_activeChip == 0), Hint().Fixed(GrowWidthToFitText(TR(L"全部"), kChipW, kGrowChipPadW)));
    chips->AddChild(MakeChip(TR(L"文字"), kIdHistoryChipText, data.m_activeChip == 1), Hint().Fixed(GrowWidthToFitText(TR(L"文字"), kChipW, kGrowChipPadW)));
    chips->AddChild(MakeChip(TR(L"图片"), kIdHistoryChipImage, data.m_activeChip == 2), Hint().Fixed(GrowWidthToFitText(TR(L"图片"), kChipW, kGrowChipPadW)));
    chips->AddChild(MakeChip(TR(L"文件"), kIdHistoryChipFile, data.m_activeChip == 3), Hint().Fixed(GrowWidthToFitText(TR(L"文件"), kChipW, kGrowChipPadW)));
    chips->AddChild(MakeChip(TR(L"语音"), kIdHistoryChipVoice, data.m_activeChip == 4), Hint().Fixed(GrowWidthToFitText(TR(L"语音"), kChipW, kGrowChipPadW)));
    chips->AddChild(MakeChip(TR(L"链接"), kIdHistoryChipLink, data.m_activeChip == 5), Hint().Fixed(GrowWidthToFitText(TR(L"链接"), kChipW, kGrowChipPadW)));
    //Pin 排在最后：它与前六枚不是一个维度（前六枚按内容类型分，它按"钉没钉住"分），
    //放末尾读起来最顺。胶囊单选，故选了 Pin 就是"本会话全部被钉的消息，不分类型"。
    chips->AddChild(MakeChip(TR(L"Pin"), kIdHistoryChipPin, data.m_activeChip == 6),
                    Hint().Fixed(GrowWidthToFitText(TR(L"Pin"), kChipW, kGrowChipPadW)));
    filterRow->AddChild(std::move(chips), Hint().Weight(1));

    //时间下拉（各项顺序与 HistoryTimeRange 一一对应，见 MakeTimeCombo）。自定义起止不在这里，由「选日期」
    //弹层的「只看这段时间」设置。构建期设值不发通知，避免伪触发刷新
    int timeComboW = 0;
    std::unique_ptr<DuiControl> timeCombo = MakeTimeCombo(kIdHistoryTimeCombo, data.m_activeTimeIndex, timeComboW);
    filterRow->AddChild(std::move(timeCombo), Hint().Fixed(timeComboW));

    //日期区间标签：「只看这段时间」生效后替下时间下拉，左边是日期文字（点它重新打开弹层），
    //右边是关闭叉号。文字与宽度由 ApplyHistoryDateRangeChip 按当前筛选设置，这里先建成隐藏的
    std::unique_ptr<DuiHBox> rangeChip(new DuiHBox());
    rangeChip->SetCtrlId(kIdHistoryDateRangeChip);
    rangeChip->SetGap(0);
    rangeChip->AddChild(MakeButton(_T(""), kIdHistoryDateRangeLabel, DuiButton::Variant::Outlined),
                        Hint().Fixed(kDatePickPadW));
    rangeChip->AddChild(MakeButton(kHistoryDateRangeClearText, kIdHistoryDateRangeClear,
                                   DuiButton::Variant::Ghost),
                        Hint().Fixed(kDateRangeClearW));
    rangeChip->SetVisible(false);
    filterRow->AddChild(std::unique_ptr<DuiControl>(rangeChip.release()),
                        Hint().Fixed(kDatePickPadW + kDateRangeClearW));

    //「选日期」：宽度按文字实测，理由同时间下拉（写死像素在高 DPI 下会截字）
    const int datePickW = MeasureDefaultFontWidth(TR(kHistoryDatePickText)) + kDatePickPadW;
    filterRow->AddChild(MakeButton(TR(kHistoryDatePickText), kIdHistoryDatePick, DuiButton::Variant::Outlined),
                        Hint().Fixed(datePickW));
    root->AddChild(std::move(filterRow), Hint().Fixed(kFilterRowH));

    // 3.5 命中条（时间轴模式且有关键字时显示，内容由 ApplyHistoryHitBar 填）：
    //     左侧说明 +「第 x / N 处」+「▲ 上一处」「▼ 下一处」
    //     只有 DuiVBox 能画底色，故外层是一个带底色的竖向容器，里面一行横排各控件
    std::unique_ptr<DuiVBox> hitBar(new DuiVBox());
    hitBar->SetCtrlId(kIdHistoryHitBar);
    hitBar->SetBgColor(kColorHitBarBg);
    hitBar->SetPadding(kPadH, kHitBarPadV, kPadH, kHitBarPadV);
    hitBar->SetGap(0);
    std::unique_ptr<DuiHBox> hitLine(new DuiHBox());
    hitLine->SetGap(8);
    hitLine->AddChild(MakeLabel(_T(""), kColorHitBarText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                kIdHistoryHitInfo),
                      Hint().Weight(1));
    hitLine->AddChild(MakeLabel(_T(""), kColorHitBarText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, kIdHistoryHitPos),
                      Hint().Fixed(kHitPosW));
    hitLine->AddChild(MakeButton(TR(L"▲ 上一处"), kIdHistoryHitPrev, DuiButton::Variant::Default),
                      Hint().Fixed(GrowWidthToFitText(TR(L"▲ 上一处"), kHitBtnW, kGrowChipPadW)));
    hitLine->AddChild(MakeButton(TR(L"▼ 下一处"), kIdHistoryHitNext, DuiButton::Variant::Default),
                      Hint().Fixed(GrowWidthToFitText(TR(L"▼ 下一处"), kHitBtnW, kGrowChipPadW)));
    hitBar->AddChild(std::unique_ptr<DuiControl>(hitLine.release()), Hint().Weight(1));
    hitBar->SetVisible(false);
    root->AddChild(std::unique_ptr<DuiControl>(hitBar.release()), Hint().Fixed(kHitBarH));

    // 4. 列表：滚动视口包一个承载各行的 vbox（vbox 由 BuildListContent 造，
    //    窗口局部刷新时复用同一函数换上新列表）。
    //    滚动视口用主面板与公告窗同款的 MainPanelScrollView，滚动条是 balloonui 统一的悬浮式细滑块：
    //    浮在内容上方、不占列表宽度，滚动时淡入、停下后淡出，可拖动（开发约定文档「UI 约定」第 1 条）。
    //    命中带宽度保持默认值，置 0 就没有滚动条了。
    std::unique_ptr<balloonmain::MainPanelScrollView> scroll(new balloonmain::MainPanelScrollView());
    scroll->SetCtrlId(kIdHistoryScroll);
    scroll->SetAutoContentHeight(true);
    //整窗构建时还不知道列表实际有多宽（窗口尚未布局），传 0 让 BuildListContent 用
    //兜底宽度。窗口在 InitUi / RefreshContent 里会用真实宽度重建一次列表。
    scroll->SetContent(BuildListContent(data.m_rows, data.m_emptyTitle,
                                        data.m_emptySubtitle, 0));
    //列表与时间轴导航条排在同一行：导航条平时隐藏，时间轴模式下出现在列表右侧（ApplyHistoryTimelineMode）。
    //导航条左边一条竖向分隔线，里面是自己的滚动视口，内容由窗口按导航数据填（BuildTimelineNavContent）
    std::unique_ptr<DuiHBox> listHost(new DuiHBox());
    listHost->SetCtrlId(kIdHistoryListHost);
    listHost->SetGap(0);
    listHost->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));
    //导航条外层是带底色的竖向容器（只有 DuiVBox 能画底色），内容比视口矮时下面一截也是导航条的底色
    std::unique_ptr<DuiVBox> nav(new DuiVBox());
    nav->SetCtrlId(kIdHistoryNav);
    nav->SetGap(0);
    nav->SetBgColor(kColorNavBg);
    std::unique_ptr<DuiHBox> navLineRow(new DuiHBox());
    navLineRow->SetGap(0);
    std::unique_ptr<DuiSeparator> navLine(new DuiSeparator());
    navLine->SetOrientation(DuiSeparator::Vertical);
    navLine->SetColor(kColorSepLine);
    navLineRow->AddChild(std::unique_ptr<DuiControl>(navLine.release()), Hint().Fixed(1));
    std::unique_ptr<balloonmain::MainPanelScrollView> navScroll(new balloonmain::MainPanelScrollView());
    navScroll->SetCtrlId(kIdHistoryNavScroll);
    navScroll->SetAutoContentHeight(true);
    navScroll->SetContent(BuildTimelineNavContent(std::vector<HistoryTimelineYear>(), 0, std::set<int>()));
    navLineRow->AddChild(std::unique_ptr<DuiControl>(navScroll.release()), Hint().Weight(1));
    nav->AddChild(std::unique_ptr<DuiControl>(navLineRow.release()), Hint().Weight(1));
    nav->SetVisible(data.m_timeline);
    listHost->AddChild(std::unique_ptr<DuiControl>(nav.release()), Hint().Fixed(kHistoryNavW));
    //右侧给窗口缩放边框让出位置：窗口最外侧一条会被判为缩放边框，滚动条若贴着
    //窗口右缘，鼠标移到滑块上只会变成左右双向箭头、无法拖动。让出的宽度由窗口
    //按 DPI 算好传入（见头文件中本函数的说明）；负值按 0 处理，不把视口撑出客户区。
    //导航条出现时它在最右边，故让出的宽度加在整行上
    const int rightInset = (listRightInsetPx > 0) ? listRightInsetPx : 0;
    root->AddChild(std::unique_ptr<DuiControl>(listHost.release()),
                   Hint().Weight(1).Margin(0, 0, rightInset, 0));

    root->AddChild(MakeSeparator(), Hint().Fixed(1));

    // 5. 底部：左信息 + 中分页器 + 右侧「清空聊天记录」与导出。宽度不够时的取舍见 HistoryFooterBar。
    std::unique_ptr<DuiHBox> footer(new HistoryFooterBar());
    footer->SetPadding(kPadH, 8, kPadH, 8);
    footer->SetGap(8);
    CString info;
    info.Format(TR(L"共 %d 条 · 第 %d/%d 页"), data.m_totalCount, data.m_currentPage, data.m_pageCount);
    footer->AddChild(MakeLabel(info, kColorSubtitle,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE, kIdHistoryFooterInfo),
                     Hint().Weight(1));
    std::unique_ptr<DuiHBox> pager(new DuiHBox());
    pager->SetCtrlId(kIdHistoryPager);
    pager->SetGap(4);
    PopulatePagerButtons(pager.get(), data.m_currentPage, data.m_pageCount);
    //按里面按钮的实际总宽占位（DuiHBox 的期望宽度 = 可见固定宽度子控件之和 + 间距）。原先按
    //权重分宽度，页码格多时按钮总宽超出分到的宽度，「末页」画到了右侧的"跳至"上（见 V2L20）
    footer->AddChild(std::move(pager), Hint().Auto());

    //“跳至第 N 页”：省略式页码只给出当前页附近那几格，页数一多就没法直达中间某页
    //（比如 625 页里的第 300 页），故补一个直达入口。
    //用 balloonui 现成的 DuiSpinBox 而不是裸输入框：它自带范围夹取与 ▲▼ 步进，
    //越界不用自己校验。范围与当前值由窗口在每次刷新时同步（见 RefreshContent）。
    footer->AddChild(MakeLabel(TR(L"跳至"), kColorSubtitle,
                               DT_RIGHT | DT_VCENTER | DT_SINGLELINE, kIdHistoryPageJumpLabel),
                     Hint().Fixed(GrowWidthToFitText(TR(L"跳至"), kJumpLabelW, kGrowLabelPadW)));
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
    footer->AddChild(MakeLabel(TR(L"页"), kColorSubtitle,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE, kIdHistoryPageJumpUnit),
                     Hint().Fixed(GrowWidthToFitText(TR(L"页"), kJumpUnitW, kGrowLabelPadW)));

    //「清空聊天记录」放在「导出」左边：两个都是针对整个会话的操作（2026-10-03 用户选定的位置）
    if (data.m_showClearConversation)
    {
        footer->AddChild(MakeButton(TR(L"清空聊天记录"), kIdHistoryClearConversation, DuiButton::Variant::Outlined),
                         Hint().Fixed(kClearConvW));
    }
    footer->AddChild(MakeButton(TR(L"↓ 导出"), kIdHistoryExport, DuiButton::Variant::Outlined),
                     Hint().Fixed(kExportW));
    root->AddChild(std::move(footer), Hint().Fixed(kFooterH));

    // 6. 多选操作栏：与底部栏同高，平时隐藏，进入多选后由窗口换上来（见 BuildMultiSelectBar）
    std::unique_ptr<DuiControl> selectBar = BuildMultiSelectBar();
    selectBar->SetVisible(false);
    root->AddChild(std::move(selectBar), Hint().Fixed(kFooterH));

    //按当前是否有日期筛选，决定筛选行显示时间下拉还是日期区间标签
    ApplyHistoryDateRangeChip(root.get(), data.m_dateRangeLabel);
    //标题区的会话名与「»」
    ApplyHistoryHeaderTitle(root.get(), data.m_conversationName, data.m_showExpandLeft);

    return std::unique_ptr<DuiControl>(root.release());
}

void ApplyHistoryHeaderTitle(DuiControl* root, const CString& conversationName, bool showExpandLeft)
{
    if (root == NULL)
    {
        return;
    }
    DuiHBox* row = dynamic_cast<DuiHBox*>(root->FindCtrlById(kIdHistoryHeaderRow));
    DuiLabel* title = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdHistoryTitle));
    DuiControl* expand = root->FindCtrlById(kIdHistoryExpandLeft);
    if (row == NULL || title == NULL || expand == NULL)
    {
        return;
    }

    title->SetText(conversationName);
    //会话名用粗体画，默认字体量出的宽度偏窄，按比例放宽再留一点余量；过长的名字不超过上限，
    //末尾以省略号截断，免得把「共 N 条消息」挤没
    int titleW = 0;
    if (!conversationName.IsEmpty())
    {
        titleW = MeasureDefaultFontWidth(conversationName) * kBoldWidthPercent / kPercentBase + kHeaderTitleSlackW;
        if (titleW > kHeaderTitleMaxW)
        {
            titleW = kHeaderTitleMaxW;
        }
    }
    row->SetHint(title, DuiLayout::Hint().Fixed(titleW));
    title->SetVisible(!conversationName.IsEmpty());
    expand->SetVisible(showExpandLeft);
    //SetVisible 只置标志、不重排；矩形没变时 SetRect 也直接返回，故用 ForceLayout
    row->ForceLayout(row->GetRect());
    row->Invalidate();
}

void ApplyHistoryDateRangeChip(DuiControl* root, const std::wstring& label)
{
    if (root == NULL)
    {
        return;
    }
    DuiHBox* filterRow = dynamic_cast<DuiHBox*>(root->FindCtrlById(kIdHistoryFilterRow));
    DuiHBox* chip = dynamic_cast<DuiHBox*>(root->FindCtrlById(kIdHistoryDateRangeChip));
    DuiButton* labelBtn = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdHistoryDateRangeLabel));
    DuiControl* combo = root->FindCtrlById(kIdHistoryTimeCombo);
    if (filterRow == NULL || chip == NULL || labelBtn == NULL || combo == NULL)
    {
        return;
    }

    const bool active = !label.empty();
    if (active)
    {
        //标签宽度按日期文字实测：写死像素在高 DPI 下会把文字截成省略号
        labelBtn->SetText(label.c_str());
        const int labelW = MeasureDefaultFontWidth(label.c_str()) + kDatePickPadW;
        chip->SetHint(labelBtn, DuiLayout::Hint().Fixed(labelW));
        filterRow->SetHint(chip, DuiLayout::Hint().Fixed(labelW + kDateRangeClearW));
    }
    combo->SetVisible(!active);
    chip->SetVisible(active);
    //SetVisible 只置标志、不重排；矩形没变时 SetRect 也直接返回，故用 ForceLayout
    filterRow->ForceLayout(filterRow->GetRect());
}

// i18n-ignore-begin: 调试入口的模拟消息记录
void FillMockHistory(HistoryViewData& data)
{
    data.m_title = L"消息记录 · 苏沐辰";
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
        { L"苏沐辰", kOrange, _T("06-04 11:01"), L"改了发你看下",                 9001 },
        { L"苏沐辰", kOrange, _T("06-04 10:01"), L"[表情] 赞",                    9000 },
        { L"林知夏", kRed,    _T("06-04 09:01"), L"左对齐更好，居中显得太弱了",   8999 },
        { L"苏沐辰", kOrange, _T("06-04 08:01"), L"你觉得这个排版怎么样？",       8998 },
        { L"苏沐辰", kOrange, _T("06-04 07:01"), L"[图片] 参考案例-banner",       8997 },
        { L"林知夏", kRed,    _T("06-04 06:01"), L"收到，很有意思",               8996 },
        { L"宋微之", kGreen,  _T("06-04 05:01"), L"[文件] 设计规范-v2.docx",      8995 },
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
// i18n-ignore-end

namespace {

//「选日期」弹层里月、日的取值范围。日一律放到 31，这一天是否存在由调用方判断
const int kDatePopupMonthMin = 1;
const int kDatePopupMonthMax = 12;
const int kDatePopupDayMin   = 1;
const int kDatePopupDayMax   = 31;
//年份微调框建好时的初始范围；每次打开弹层都会按实际范围重设
const int kDatePopupYearMin = 1970;
const int kDatePopupYearMax = 9999;

//弹层里各控件的尺寸（像素）。年要放下四位数字，月、日两位
const int kDatePopupPadding    = 9;    //弹层四周内边距
const int kDatePopupGap        = 4;    //同一行里控件的间距
const int kDatePopupRowGap     = 8;    //行与行之间的间距
const int kDatePopupRowH       = 28;   //每一行的高度
const int kDatePopupFromToW    = 20;   //「从」「到」两个字的宽度
const int kDatePopupYearSpinW  = 76;   //年微调框宽
const int kDatePopupMdSpinW    = 56;   //月、日微调框宽
const int kDatePopupUnitW      = 18;   //「年」「月」「日」文字宽
const int kDatePopupJumpBtnW   = 96;   //「跳到这一天」按钮宽
const int kDatePopupFilterBtnW = 110;  //「只看这段时间」按钮宽

std::unique_ptr<DuiControl> MakeDatePopupSpin(UINT ctrlId, int minV, int maxV)
{
    std::unique_ptr<balloonwjui::DuiSpinBox> spin(new balloonwjui::DuiSpinBox());
    spin->SetCtrlId(ctrlId);
    spin->SetRange(minV, maxV);
    if (spin->GetEdit() != NULL)
    {
        spin->GetEdit()->SetCtrlId(ctrlId + kIdDatePopupEditOffset);
    }
    return std::unique_ptr<DuiControl>(spin.release());
}

//造一行「从 / 到 [年][月][日]」。yearId 为这一行年微调框的编号，月、日依次加一。
std::unique_ptr<DuiControl> MakeDatePopupDateRow(LPCTSTR caption, UINT yearId)
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(kDatePopupGap);
    row->AddChild(MakeLabel(caption, kColorSubtitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                  Hint().Fixed(kDatePopupFromToW));
    row->AddChild(MakeDatePopupSpin(yearId, kDatePopupYearMin, kDatePopupYearMax),
                  Hint().Fixed(kDatePopupYearSpinW));
    row->AddChild(MakeLabel(TR(L"年"), kColorSubtitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE),
                  Hint().Fixed(kDatePopupUnitW));
    row->AddChild(MakeDatePopupSpin(yearId + 1, kDatePopupMonthMin, kDatePopupMonthMax),
                  Hint().Fixed(kDatePopupMdSpinW));
    row->AddChild(MakeLabel(TR(L"月"), kColorSubtitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE),
                  Hint().Fixed(kDatePopupUnitW));
    row->AddChild(MakeDatePopupSpin(yearId + 2, kDatePopupDayMin, kDatePopupDayMax),
                  Hint().Fixed(kDatePopupMdSpinW));
    row->AddChild(MakeLabel(TR(L"日"), kColorSubtitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE),
                  Hint().Fixed(kDatePopupUnitW));
    return std::unique_ptr<DuiControl>(row.release());
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildHistoryDatePopupContent()
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetPadding(kDatePopupPadding);
    root->SetGap(kDatePopupRowGap);
    root->AddChild(MakeDatePopupDateRow(TR(L"从"), kIdDatePopupFromYear), Hint().Fixed(kDatePopupRowH));
    root->AddChild(MakeDatePopupDateRow(TR(L"到"), kIdDatePopupToYear), Hint().Fixed(kDatePopupRowH));

    //按钮行靠右：左侧放一个撑满的空控件把两个按钮推到右边
    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetGap(kDatePopupGap * 2);
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    buttons->AddChild(MakeButton(TR(L"跳到这一天"), kIdDatePopupJump, DuiButton::Variant::Outlined),
                      Hint().Fixed(kDatePopupJumpBtnW));
    buttons->AddChild(MakeButton(TR(L"只看这段时间"), kIdDatePopupFilter, DuiButton::Variant::Primary),
                      Hint().Fixed(kDatePopupFilterBtnW));
    root->AddChild(std::move(buttons), Hint().Fixed(kDatePopupRowH));
    return std::unique_ptr<DuiControl>(root.release());
}

int HistoryDatePopupActionFromNotify(UINT code, UINT ctrlId)
{
    if (code == balloonwjui::DUIN_CLICK && ctrlId == (UINT)kIdDatePopupJump)
    {
        return kHistoryDatePopupJump;
    }
    if (code == balloonwjui::DUIN_CLICK && ctrlId == (UINT)kIdDatePopupFilter)
    {
        return kHistoryDatePopupFilter;
    }
    //任一输入框里按回车：等同点「跳到这一天」，与合并前的「跳到日期」弹层一致
    const UINT firstEdit = (UINT)(kIdDatePopupFromYear + kIdDatePopupEditOffset);
    const UINT lastEdit = (UINT)(kIdDatePopupToDay + kIdDatePopupEditOffset);
    if (code == (UINT)balloonwjui::DuiEdit::DUIN_EDIT_ENTER && ctrlId >= firstEdit && ctrlId <= lastEdit)
    {
        return kHistoryDatePopupJump;
    }
    return kHistoryDatePopupNone;
}

// =====================================================================
// 聊天记录管理器的左栏
// =====================================================================

namespace {

// 左栏底色：比右侧的白底略灰，两栏一眼分得开
const COLORREF kColorMgrPaneBg = RGB(0xF7, 0xF7, 0xF8);
const int kMgrPadH        = 10;   // 左栏各行的左右内边距（像素）
const int kMgrPadTop      = 8;    // 左栏顶部内边距（像素）
const int kMgrRowGap      = 6;    // 左栏各行之间、行内控件之间的间距（像素）
const int kMgrSearchRowH  = 32;   // 搜索框那一行的高（像素）
const int kMgrScopeRowH   = 28;   // 范围按钮那一行的高（像素）
const int kMgrTypeRowH    = 28;   // 类型筛选那一行的高（像素）
const int kMgrCollapseW   = 28;   //「«」按钮宽（像素）
const int kMgrScopeClearW = 28;   // 范围「×」按钮宽（像素）
const int kMgrTypeChipW   = 56;   // 类型筛选胶囊宽（像素，均为两个汉字）
const int kMgrTreePadH    = 4;    // 两棵会话树的左右外边距（像素），与主面板会话列表的内边距相近
const int kMgrEmptyH      = 60;   // 空提示所占的高（像素）
const TCHAR* const kMgrCollapseText   = _T("«");   //「«」按钮的文字
const TCHAR* const kMgrScopeClearText = _T("×");   // 范围「×」按钮的文字
const int kMgrTabRowH     = 28;   // 左栏最上面标签行的高（像素）
const int kMgrStorageRowH   = 38;   // 左栏最下面「本机存储占用」那一行的高（像素，含下内边距）
const int kMgrStoragePadBot = 6;    // 那一行的下内边距（像素），按钮不贴着窗口底边
//标签在文字宽度之外的左右留白合计（像素）。按文字定宽、靠左排；2026-10-06 加「链接」标签时由 16 减到 12，
//最小左栏宽度下四个都放得下（docs/聊天记录管理器方案.md §12.2）
const int kMgrTabPadW     = 12;
//左栏标签的文字，下标与 HistoryManagerTab 的 0～3 一一对应。第一个由「聊天记录」改短为「聊天」（同上）
const wchar_t* const kMgrTabTexts[] = { TR_NOOP(L"聊天"), TR_NOOP(L"文件"), TR_NOOP(L"图片"), TR_NOOP(L"链接") };
const int kMgrTabCount = kHistoryManagerVisibleTabCount;

//造一棵左栏用的会话树：行高、头像尺寸与圆形头像的设置与主面板会话列表相同
std::unique_ptr<DuiControl> MakeManagerTree(UINT ctrlId)
{
    std::unique_ptr<balloonmain::MainPanelTreeView> tree(new balloonmain::MainPanelTreeView());
    tree->SetCtrlId(ctrlId);
    tree->SetRowHeight(kMgrRowHeight);
    tree->SetIconSize(kMgrAvatarSize);
    tree->SetIconCornerRadius(kMgrAvatarSize / 2);
    tree->SetIconUsesAlpha(true);
    return std::unique_ptr<DuiControl>(tree.release());
}

//造一段列表的标题（「会话」「聊天记录 · 仅搜索本机已保存的记录」），平时隐藏
std::unique_ptr<DuiControl> MakeManagerSectionHeader(UINT ctrlId)
{
    std::unique_ptr<DuiControl> label = MakeLabel(_T(""), kColorSubtitle,
                                                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                                  ctrlId);
    label->SetVisible(false);
    return label;
}

//在左右分栏的根容器上套用折叠或展开的参数（分隔条宽度、两侧最小宽度、分隔位置、左栏显隐），不重排
void SetManagerSplitParams(balloonwjui::DuiSplitter* split, bool collapsed, int paneWidth)
{
    const int width = (paneWidth > 0) ? paneWidth : kMgrPaneDefaultW;
    DuiControl* pane = split->GetPane(0);
    if (collapsed)
    {
        split->SetBarThickness(kMgrSplitBarCollapsedW);
        split->SetMinSizes(0, kMgrBodyMinW);
        split->SetSplitPx(0);
    }
    else
    {
        split->SetBarThickness(kMgrSplitBarW);
        split->SetMinSizes(kMgrPaneMinW, kMgrBodyMinW);
        split->SetSplitPx(width);
    }
    if (pane != NULL)
    {
        pane->SetVisible(!collapsed);
    }
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildHistoryManagerPane(int typeFilter, size_t scopeCount, bool wildcard, int tab)
{
    std::unique_ptr<DuiVBox> pane(new DuiVBox());
    pane->SetCtrlId(kIdMgrPane);
    pane->SetBgColor(kColorMgrPaneBg);
    pane->SetPadding(0, kMgrPadTop, 0, 0);
    pane->SetGap(kMgrRowGap);

    // 0. 标签：聊天 / 文件 / 图片 / 链接（docs/聊天记录管理器方案.md §11.5、§12.2），按文字定宽、靠左排，右边留空
    std::unique_ptr<DuiHBox> tabRow(new DuiHBox());
    tabRow->SetCtrlId(kIdMgrTabRow);
    tabRow->SetPadding(kMgrPadH, 0, kMgrPadH, 0);
    tabRow->SetGap(kMgrRowGap);
    for (int i = 0; i < kMgrTabCount; ++i)
    {
        const LPCTSTR text = TR(kMgrTabTexts[i]);
        tabRow->AddChild(MakeChip(text, (UINT)(kIdMgrTabChat + i), false),
                         Hint().Fixed(MeasureDefaultFontWidth(text) + kMgrTabPadW));
    }
    tabRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    pane->AddChild(std::move(tabRow), Hint().Fixed(kMgrTabRowH));

    // 1. 搜索框 +「«」
    std::unique_ptr<DuiHBox> searchRow(new DuiHBox());
    searchRow->SetPadding(kMgrPadH, 0, kMgrPadH, 0);
    searchRow->SetGap(kMgrRowGap);
    //悬停时显示搜索语法说明；文件 / 图片 / 链接页只按名字找会话，不显示（见 ApplyHistoryManagerTab）
    std::unique_ptr<HistoryTipSearchBox> search(new HistoryTipSearchBox(HistorySearchSyntaxTip()));
    search->SetCtrlId(kIdMgrSearch);
    search->SetPlaceholder(TR(L"搜索会话或聊天内容"));
    searchRow->AddChild(std::unique_ptr<DuiControl>(search.release()), Hint().Weight(1));
    searchRow->AddChild(MakeButton(kMgrCollapseText, kIdMgrCollapse, DuiButton::Variant::Ghost),
                        Hint().Fixed(kMgrCollapseW));
    pane->AddChild(std::move(searchRow), Hint().Fixed(kMgrSearchRowH));

    // 2. 范围按钮 +「×」+「通配符」复选框（复选框放在这一行，不额外占一行）
    std::unique_ptr<DuiHBox> scopeRow(new DuiHBox());
    scopeRow->SetCtrlId(kIdMgrScopeRow);
    scopeRow->SetPadding(kMgrPadH, 0, kMgrPadH, 0);
    scopeRow->SetGap(kMgrRowGap);
    std::unique_ptr<DuiControl> scopeBtn = MakeButton(_T(""), kIdMgrScope, DuiButton::Variant::Default);
    DuiButton* scopeButton = dynamic_cast<DuiButton*>(scopeBtn.get());
    if (scopeButton != NULL)
    {
        //左栏取最小宽度、界面字体又按系统缩放放大时，「范围：全部会话」放不下（控件宽度是不随缩放变化的固定像素）：
        //以省略号收尾，不让文字两头被裁掉（2026-10-06 实机核对时 125% 缩放下发现）
        scopeButton->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    scopeRow->AddChild(std::move(scopeBtn), Hint().Weight(1));
    scopeRow->AddChild(MakeButton(kMgrScopeClearText, kIdMgrScopeClear, DuiButton::Variant::Ghost),
                       Hint().Fixed(kMgrScopeClearW));
    int wildcardW = 0;
    std::unique_ptr<DuiControl> wildcardBox = MakeWildcardCheckbox(kIdMgrWildcard, wildcard, wildcardW);
    scopeRow->AddChild(std::move(wildcardBox), Hint().Fixed(wildcardW));
    pane->AddChild(std::move(scopeRow), Hint().Fixed(kMgrScopeRowH));

    // 3. 类型筛选：全部 / 单聊 / 群聊，靠左排，右边留空
    std::unique_ptr<DuiHBox> typeRow(new DuiHBox());
    typeRow->SetPadding(kMgrPadH, 0, kMgrPadH, 0);
    typeRow->SetGap(kMgrRowGap);
    //左栏较窄，「单聊 / 群聊」用带上下文的 TRC，译文可取比别处更短的写法（多语言第 4 期）
    typeRow->AddChild(MakeChip(TR(L"全部"), kIdMgrTypeAll, false),
                      Hint().Fixed(GrowWidthToFitText(TR(L"全部"), kMgrTypeChipW, kGrowChipPadW)));
    typeRow->AddChild(MakeChip(TRC(L"聊天记录管理器左栏胶囊", L"单聊"), kIdMgrTypeSingle, false),
                      Hint().Fixed(GrowWidthToFitText(TRC(L"聊天记录管理器左栏胶囊", L"单聊"), kMgrTypeChipW, kGrowChipPadW)));
    typeRow->AddChild(MakeChip(TRC(L"聊天记录管理器左栏胶囊", L"群聊"), kIdMgrTypeGroup, false),
                      Hint().Fixed(GrowWidthToFitText(TRC(L"聊天记录管理器左栏胶囊", L"群聊"), kMgrTypeChipW, kGrowChipPadW)));
    typeRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    pane->AddChild(std::move(typeRow), Hint().Fixed(kMgrTypeRowH));
    pane->AddChild(MakeSeparator(), Hint().Fixed(1));

    // 4. 列表：滚动视口里依次是「会话」标题、会话树、「聊天记录」标题、命中树、空提示。
    //    滚动视口与右侧消息列表同款（悬浮式、自动隐藏的细滚动条），内容高度按内容 vbox 的期望高度
    std::unique_ptr<balloonmain::MainPanelScrollView> scroll(new balloonmain::MainPanelScrollView());
    scroll->SetCtrlId(kIdMgrScroll);
    scroll->SetAutoContentHeight(true);
    std::unique_ptr<DuiVBox> box(new DuiVBox());
    box->SetCtrlId(kIdMgrListBox);
    box->SetPadding(0, kMgrListPadV / 2, 0, kMgrListPadV / 2);
    box->SetGap(0);
    box->AddChild(MakeManagerSectionHeader(kIdMgrConvHeader),
                  Hint().Fixed(kMgrSectionHeaderH).Margin(kMgrPadH, 0, kMgrPadH, 0));
    box->AddChild(MakeManagerTree(kIdMgrConvTree), Hint().Fixed(0).Margin(kMgrTreePadH, 0, kMgrTreePadH, 0));
    box->AddChild(MakeManagerSectionHeader(kIdMgrHitHeader),
                  Hint().Fixed(kMgrSectionHeaderH).Margin(kMgrPadH, 0, kMgrPadH, 0));
    std::unique_ptr<DuiControl> hitTree = MakeManagerTree(kIdMgrHitTree);
    hitTree->SetVisible(false);
    box->AddChild(std::move(hitTree), Hint().Fixed(0).Margin(kMgrTreePadH, 0, kMgrTreePadH, 0));
    std::unique_ptr<DuiControl> empty = MakeLabel(_T(""), kColorSubtitle,
                                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                                  kIdMgrEmpty);
    empty->SetVisible(false);
    box->AddChild(std::move(empty), Hint().Fixed(kMgrEmptyH).Margin(kMgrPadH, 0, kMgrPadH, 0));
    scroll->SetContent(std::unique_ptr<DuiControl>(box.release()));
    pane->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));

    // 5. 最下面一行：「本机存储占用」入口（docs/聊天记录管理器方案.md §12.6），各标签都显示
    pane->AddChild(MakeSeparator(), Hint().Fixed(1));
    std::unique_ptr<DuiHBox> storageRow(new DuiHBox());
    storageRow->SetCtrlId(kIdMgrStorageRow);
    storageRow->SetPadding(kMgrPadH, 0, kMgrPadH, kMgrStoragePadBot);
    //文字居中：选中时按钮是蓝底，靠左排的文字会贴着底色的左边缘（2026-10-06 实机核对时发现）
    std::unique_ptr<DuiControl> storageBtn = MakeButton(TR(L"本机存储占用"), kIdMgrStorage, DuiButton::Variant::Ghost);
    DuiButton* storageButton = dynamic_cast<DuiButton*>(storageBtn.get());
    if (storageButton != NULL)
    {
        storageButton->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    storageRow->AddChild(std::move(storageBtn), Hint().Weight(1));
    pane->AddChild(std::unique_ptr<DuiControl>(storageRow.release()), Hint().Fixed(kMgrStorageRowH));

    ApplyHistoryManagerTypeChips(pane.get(), typeFilter);
    ApplyHistoryManagerScope(pane.get(), scopeCount);
    ApplyHistoryManagerTab(pane.get(), tab);
    return std::unique_ptr<DuiControl>(pane.release());
}

std::unique_ptr<DuiControl> BuildHistoryManagerRoot(std::unique_ptr<DuiControl> pane,
                                                    std::unique_ptr<DuiControl> body,
                                                    std::unique_ptr<DuiControl> library,
                                                    int paneWidth, bool collapsed)
{
    if (!library)
    {
        return BuildHistoryManagerRoot(std::move(pane), std::move(body), paneWidth, collapsed);
    }
    //右侧容器：聊天记录页与文件 / 图片页上下叠放、只显示其一；隐藏的那一页不参与排版
    std::unique_ptr<DuiVBox> host(new DuiVBox());
    host->SetCtrlId(kIdMgrRightHost);
    host->SetPadding(0);
    host->SetGap(0);
    DuiControl* libExpand = library->FindCtrlById(kIdLibExpandLeft);
    if (libExpand != NULL)
    {
        libExpand->SetVisible(collapsed);
    }
    library->SetVisible(false);
    host->AddChild(std::move(body), Hint().Weight(1));
    host->AddChild(std::move(library), Hint().Weight(1));
    return BuildHistoryManagerRoot(std::move(pane), std::unique_ptr<DuiControl>(host.release()), paneWidth, collapsed);
}

void ApplyHistoryManagerTab(DuiControl* root, int tab)
{
    if (root == NULL)
    {
        return;
    }
    if (tab < kHistoryManagerTabChat || tab > kHistoryManagerTabMentions)
    {
        tab = kHistoryManagerTabChat;
    }
    //与我有关没有自己的标签，「聊天」标签显示为选中
    const int chipTab = (tab == kHistoryManagerTabMentions) ? kHistoryManagerTabChat : tab;
    for (int i = 0; i < kMgrTabCount; ++i)
    {
        DuiButton* chip = dynamic_cast<DuiButton*>(root->FindCtrlById((UINT)(kIdMgrTabChat + i)));
        if (chip != NULL)
        {
            chip->SetVariant((i == chipTab) ? DuiButton::Variant::Primary : DuiButton::Variant::Default);
            chip->Invalidate();
        }
    }

    //文件、图片、链接三页：范围行（跨会话搜内容的范围与通配符）不适用，隐藏；搜索框只按名字找会话。
    //与我有关时左栏与「聊天」标签相同
    const bool chatList = (chipTab == kHistoryManagerTabChat);
    const bool chat = (tab == kHistoryManagerTabChat);
    DuiControl* scopeRow = root->FindCtrlById(kIdMgrScopeRow);
    if (scopeRow != NULL)
    {
        scopeRow->SetVisible(chatList);
    }
    DuiSearchBox* search = dynamic_cast<DuiSearchBox*>(root->FindCtrlById(kIdMgrSearch));
    if (search != NULL)
    {
        search->SetPlaceholder(chatList ? TR(L"搜索会话或聊天内容") : TR(L"搜索会话"));
    }
    HistoryTipSearchBox* tipSearch = dynamic_cast<HistoryTipSearchBox*>(search);
    if (tipSearch != NULL)
    {
        tipSearch->SetTipText(chatList ? HistorySearchSyntaxTip() : _T(""));
    }
    DuiControl* pane = (root->GetCtrlId() == (UINT)kIdMgrPane) ? root : root->FindCtrlById(kIdMgrPane);
    if (pane != NULL)
    {
        pane->ForceLayout(pane->GetRect());
        pane->Invalidate();
    }

    //左栏最下面的「本机存储占用」入口恢复为普通外观（切标签就离开了存储页）
    DuiButton* storageEntry = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdMgrStorage));
    if (storageEntry != NULL)
    {
        storageEntry->SetVariant(DuiButton::Variant::Ghost);
        storageEntry->Invalidate();
    }

    //右侧容器：显示对应的那一页，存储页（有的话）隐藏
    DuiControl* host = root->FindCtrlById(kIdMgrRightHost);
    if (host != NULL)
    {
        DuiControl* body = host->FindCtrlById(kIdHistoryBody);
        DuiControl* library = host->FindCtrlById(kIdLibBody);
        DuiControl* storage = host->FindCtrlById(kIdStorBody);
        if (storage != NULL)
        {
            storage->SetVisible(false);
        }
        if (body != NULL && library != NULL)
        {
            body->SetVisible(chat);
            library->SetVisible(!chat);
            host->ForceLayout(host->GetRect());
            host->Invalidate();
        }
    }
}

void ApplyHistoryManagerStorage(DuiControl* root, bool active, int tab)
{
    if (root == NULL)
    {
        return;
    }
    if (!active)
    {
        ApplyHistoryManagerTab(root, tab);
        return;
    }
    DuiControl* host = root->FindCtrlById(kIdMgrRightHost);
    DuiControl* storage = (host != NULL) ? host->FindCtrlById(kIdStorBody) : NULL;
    if (storage == NULL)
    {
        return;
    }
    DuiControl* body = host->FindCtrlById(kIdHistoryBody);
    DuiControl* library = host->FindCtrlById(kIdLibBody);
    if (body != NULL)
    {
        body->SetVisible(false);
    }
    if (library != NULL)
    {
        library->SetVisible(false);
    }
    storage->SetVisible(true);
    host->ForceLayout(host->GetRect());
    host->Invalidate();
    DuiButton* storageEntry = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdMgrStorage));
    if (storageEntry != NULL)
    {
        storageEntry->SetVariant(DuiButton::Variant::Primary);
        storageEntry->Invalidate();
    }
}

std::unique_ptr<DuiControl> BuildHistoryManagerRoot(std::unique_ptr<DuiControl> pane,
                                                    std::unique_ptr<DuiControl> body,
                                                    int paneWidth, bool collapsed)
{
    std::unique_ptr<balloonwjui::DuiSplitter> split(new balloonwjui::DuiSplitter());
    split->SetCtrlId(kIdMgrSplitter);
    split->SetOrientation(balloonwjui::DuiSplitter::Vertical);
    split->SetPane(0, std::move(pane));
    split->SetPane(1, std::move(body));
    SetManagerSplitParams(split.get(), collapsed, paneWidth);
    //右侧标题区的「»」跟随折叠状态；会话名由窗口切换会话时设置
    DuiControl* bodyRoot = split->GetPane(1);
    if (bodyRoot != NULL)
    {
        DuiControl* expand = bodyRoot->FindCtrlById(kIdHistoryExpandLeft);
        if (expand != NULL)
        {
            expand->SetVisible(collapsed);
        }
    }
    return std::unique_ptr<DuiControl>(split.release());
}

void ApplyHistoryManagerCollapsed(DuiControl* root, bool collapsed, int paneWidth,
                                  const CString& conversationName)
{
    if (root == NULL)
    {
        return;
    }
    balloonwjui::DuiSplitter* split = dynamic_cast<balloonwjui::DuiSplitter*>(root);
    if (split == NULL)
    {
        split = dynamic_cast<balloonwjui::DuiSplitter*>(root->FindCtrlById(kIdMgrSplitter));
    }
    if (split == NULL)
    {
        return;
    }
    SetManagerSplitParams(split, collapsed, paneWidth);
    ApplyHistoryHeaderTitle(split->GetPane(1), conversationName, collapsed);
    //文件 / 图片页的标题区也有一个「»」，跟着折叠状态显隐并重排那一行
    DuiControl* right = split->GetPane(1);
    DuiControl* libExpand = (right != NULL) ? right->FindCtrlById(kIdLibExpandLeft) : NULL;
    DuiControl* libHeader = (right != NULL) ? right->FindCtrlById(kIdLibHeaderRow) : NULL;
    if (libExpand != NULL && libHeader != NULL)
    {
        libExpand->SetVisible(collapsed);
        libHeader->ForceLayout(libHeader->GetRect());
    }
    //本机存储占用页（窗口加进右侧容器之后才有）同样有一个「»」
    DuiControl* storExpand = (right != NULL) ? right->FindCtrlById(kIdStorExpandLeft) : NULL;
    DuiControl* storHeader = (right != NULL) ? right->FindCtrlById(kIdStorHeaderRow) : NULL;
    if (storExpand != NULL && storHeader != NULL)
    {
        storExpand->SetVisible(collapsed);
        storHeader->ForceLayout(storHeader->GetRect());
    }
    //左栏宽度变了，两侧都要按新宽度重新排（右侧整套界面的宽度跟着变）。SetSplitPx 已按新位置
    //给两侧 SetRect，但矩形没变的子容器不会往下排，显隐刚变的左栏要强制重排一次
    DuiControl* pane = split->GetPane(0);
    if (pane != NULL && pane->IsVisible())
    {
        pane->ForceLayout(pane->GetRect());
    }
    split->Invalidate();
}

void ApplyHistoryManagerTypeChips(DuiControl* root, int typeFilter)
{
    if (root == NULL)
    {
        return;
    }
    const int kChipCount = 3;
    //下标与 ManagerTypeFilter 的取值一一对应
    const UINT kChipIds[kChipCount] = { kIdMgrTypeAll, kIdMgrTypeSingle, kIdMgrTypeGroup };
    for (int i = 0; i < kChipCount; ++i)
    {
        DuiButton* chip = dynamic_cast<DuiButton*>(root->FindCtrlById(kChipIds[i]));
        if (chip != NULL)
        {
            chip->SetVariant((i == typeFilter) ? DuiButton::Variant::Primary : DuiButton::Variant::Default);
            chip->Invalidate();
        }
    }
}

void ApplyHistoryManagerScope(DuiControl* root, size_t scopeCount)
{
    if (root == NULL)
    {
        return;
    }
    DuiButton* scope = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdMgrScope));
    DuiControl* clear = root->FindCtrlById(kIdMgrScopeClear);
    DuiControl* row = root->FindCtrlById(kIdMgrScopeRow);
    if (scope == NULL || clear == NULL || row == NULL)
    {
        return;
    }
    const std::wstring text = BuildManagerScopeButtonText(scopeCount);
    scope->SetText(text.c_str());
    clear->SetVisible(scopeCount > 0);
    //「×」显隐变化后不重排的话，范围按钮仍停在旧宽度上（开发约定文档 UI 约定第 5 条）
    row->ForceLayout(row->GetRect());
    row->Invalidate();
}

void ApplyHistoryManagerListView(DuiControl* root, const HistoryManagerListView& view)
{
    if (root == NULL)
    {
        return;
    }
    DuiVBox* box = dynamic_cast<DuiVBox*>(root->FindCtrlById(kIdMgrListBox));
    DuiLabel* convHeader = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdMgrConvHeader));
    DuiControl* convTree = root->FindCtrlById(kIdMgrConvTree);
    DuiLabel* hitHeader = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdMgrHitHeader));
    DuiControl* hitTree = root->FindCtrlById(kIdMgrHitTree);
    DuiLabel* empty = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdMgrEmpty));
    DuiControl* scroll = root->FindCtrlById(kIdMgrScroll);
    if (box == NULL || convHeader == NULL || convTree == NULL || hitHeader == NULL
        || hitTree == NULL || empty == NULL || scroll == NULL)
    {
        return;
    }

    const int convRows = (view.m_convRows > 0) ? view.m_convRows : 0;
    const int hitRows = (view.m_searching && view.m_hitRows > 0) ? view.m_hitRows : 0;

    convHeader->SetText(TR(L"会话"));
    convHeader->SetVisible(view.m_searching && convRows > 0);
    convTree->SetVisible(convRows > 0);
    box->SetHint(convTree, DuiLayout::Hint().Fixed(convRows * kMgrRowHeight)
                                            .Margin(kMgrTreePadH, 0, kMgrTreePadH, 0));

    hitHeader->SetText(view.m_hitHeader);
    hitHeader->SetVisible(hitRows > 0);
    hitTree->SetVisible(hitRows > 0);
    box->SetHint(hitTree, DuiLayout::Hint().Fixed(hitRows * kMgrRowHeight)
                                           .Margin(kMgrTreePadH, 0, kMgrTreePadH, 0));

    empty->SetText(view.m_emptyText);
    empty->SetVisible(convRows == 0 && hitRows == 0 && !view.m_emptyText.IsEmpty());

    //显隐与高度都变了：滚动视口重新询问内容高度，再把内容 vbox 按新高度排一遍
    scroll->ForceLayout(scroll->GetRect());
    box->ForceLayout(box->GetRect());
    scroll->Invalidate();
}

// =====================================================================
// 聊天记录管理器的搜索范围弹层的内容
// =====================================================================

namespace {

const int kScopePopupPad       = 10;   // 弹层四周内边距（像素）
const int kScopePopupGap       = 8;    // 各行之间的间距（像素）
const int kScopePopupSearchH   = 30;   // 搜索框高（像素）
const int kScopePopupFooterH   = 30;   // 底部一行高（像素）
const int kScopePopupAllW      = 80;   //「全部会话」按钮宽（像素，四个汉字）
const int kScopePopupOkW       = 64;   //「确定」按钮宽（像素）
const int kScopePopupCheckW    = 22;   // 复选框一列的宽（像素）
const int kScopePopupRowGap    = 8;    // 行内复选框、头像、名字之间的间距（像素）
const int kScopePopupAvatarCorner = 6; // 头像圆角半径（像素）

//范围弹层的复选框：子类化 DuiButton，只为点击时不抢走搜索框的焦点、也不画虚线焦点框
//（做法与转发目标选择窗的 ForwardPickCheckbox 相同）
class ScopePickCheckbox : public DuiButton
{
public:
    bool OnSetFocus() override
    {
        return false;
    }
};

} // anonymous namespace

std::unique_ptr<DuiControl> BuildHistoryScopePopupContent()
{
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kScopePopupPad);
    root->SetGap(kScopePopupGap);

    std::unique_ptr<DuiSearchBox> search(new DuiSearchBox());
    search->SetCtrlId(kIdScopePopupSearch);
    search->SetPlaceholder(TR(L"搜索会话"));
    root->AddChild(std::unique_ptr<DuiControl>(search.release()), Hint().Fixed(kScopePopupSearchH));

    //列表内容由弹层按会话填入（BuildHistoryScopePopupRow）；滚动条用 DuiScrollView 内置的自动隐藏
    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    scroll->SetCtrlId(kIdScopePopupScroll);
    scroll->SetAutoContentHeight(true);
    // 滚动条用 balloonui 统一的悬浮式细滑块与默认命中带宽（2026-10-04 起不再单独设宽）
    scroll->SetContent(std::unique_ptr<DuiControl>(new DuiVBox()));
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));

    std::unique_ptr<DuiHBox> footer(new DuiHBox());
    footer->SetGap(kScopePopupGap);
    footer->AddChild(MakeLabel(_T(""), kColorSubtitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                               kIdScopePopupInfo),
                     Hint().Weight(1));
    footer->AddChild(MakeButton(TR(L"全部会话"), kIdScopePopupAll, DuiButton::Variant::Default),
                     Hint().Fixed(kScopePopupAllW));
    footer->AddChild(MakeButton(TR(L"确定"), kIdScopePopupOk, DuiButton::Variant::Primary),
                     Hint().Fixed(kScopePopupOkW));
    root->AddChild(std::move(footer), Hint().Fixed(kScopePopupFooterH));
    return std::unique_ptr<DuiControl>(root.release());
}

std::unique_ptr<DuiControl> BuildHistoryScopePopupRow(UINT checkId, bool checked, const CString& name,
                                                      HBITMAP avatar, int64_t cid)
{
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(kScopePopupRowGap);

    std::unique_ptr<DuiButton> box(new ScopePickCheckbox());
    box->SetButtonType(DuiButton::StyleCheckbox);
    //Ghost 变体：复选框只画小勾选框，不铺整格的浅色底（与转发目标选择窗相同）
    box->SetVariant(DuiButton::Variant::Ghost);
    box->SetText(_T(""));
    box->SetCtrlId(checkId);
    box->SetCheck(checked, false);
    row->AddChild(std::unique_ptr<DuiControl>(box.release()), Hint().Fixed(kScopePopupCheckW));

    std::unique_ptr<DuiAvatar> av(new DuiAvatar());
    av->SetShape(DuiAvatar::ShapeRoundRect);
    av->SetCornerRadius(kScopePopupAvatarCorner);
    av->SetFallbackBgColor(balloonmain::AvatarColorForUser(cid));
    av->SetName(balloonmain::AvatarFallbackGlyph(name));
    if (avatar != NULL)
    {
        av->SetBitmap(avatar);
    }
    //头像固定为正方形并在行内垂直居中：只给主轴尺寸时交叉轴会被拉伸成长条
    row->AddChild(std::unique_ptr<DuiControl>(av.release()),
                  Hint().Fixed(kHistoryScopePopupAvatarSize, kHistoryScopePopupAvatarSize)
                        .AlignC(DuiLayout::AlignCenter));

    row->AddChild(MakeLabel(name, kColorName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS),
                  Hint().Weight(1));
    return std::unique_ptr<DuiControl>(row.release());
}

balloonchatwnd::ChatVoiceStrip* FindHistoryVoiceStrip(DuiControl* root, long long msgid)
{
    if (root == NULL || msgid == 0)
    {
        return NULL;
    }
    //语音条按「基址 + 行下标」编号，只有语音行才有；一页最多 kHistoryPageSize 行，逐个编号比对 msgid。
    //记录窗播放时约每 100 毫秒按 msgid 找一次语音条，故只查一页的行数，不查整个编号段
    for (UINT i = 0; i < (UINT)kHistoryPageSize; ++i)
    {
        balloonchatwnd::ChatVoiceStrip* strip =
            dynamic_cast<balloonchatwnd::ChatVoiceStrip*>(root->FindCtrlById((UINT)kIdHistoryVoiceBase + i));
        if (strip != NULL && strip->VoiceMessage().msgid == msgid)
        {
            return strip;
        }
    }
    return NULL;
}

bool MarkHistoryVoicePlayed(DuiControl* root, int viewCtype, int64_t viewCid,
                            int ctype, int64_t cid, long long msgid)
{
    //记录窗只显示一个会话；别的会话里的同号消息不会出现在当前页
    if (viewCtype != ctype || viewCid != cid)
    {
        return false;
    }
    balloonchatwnd::ChatVoiceStrip* strip = FindHistoryVoiceStrip(root, msgid);
    if (strip == NULL)
    {
        return false;
    }
    const balloonchatwnd::ChatMessage& m = strip->VoiceMessage();
    if (m.fromMe || m.voicePlayed)
    {
        return false;
    }
    balloonchatwnd::ChatMessage played = m;
    played.voicePlayed = true;
    //记录窗的语音条都按已听状态画红点（开关为 true）；改为已听后不再画红点
    strip->SetVoiceMessage(played, true);
    return true;
}

} // namespace balloonhistory
