/**
 *  ContactLayout.cpp
 *
 *  联系人资料对话框客户区控件树的构造实现。布局以内嵌 XML 常量为唯一
 *  真值源（随 exe 出货、0 部署步骤），交给 balloonui 的 DuiXmlBuilder
 *  解析。各项尺寸严格照设计稿 dialogs.jsx 的 ContactInfoDialog 与
 *  components.jsx 的 Modal / Btn / FieldRow / Switch。
 *
 *  本文件还就地实现若干仅本布局自用的小控件：白底根面板、单行文本、
 *  头部多行资料块、标签流式容器与它外面的滚动视口、「+ 添加标签」虚线按钮、
 *  标签已达上限的说明文字、铅笔图标按钮、FieldRow 行容器、底部操作按钮。
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

#include <string>
#include <map>
#include <vector>
#include <stdlib.h>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "ContactLayout.h"
#include "ContactModel.h"       // ApplyContactAvatar 读取的资料模型
#include "../BalloonMain/AvatarGray.h"   // MakeGrayColor：离线头像的灰色底色
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"   // 头部头像控件
#include "TagChip.h"
#include "ContactTagModel.h"    // kContactTagMaxCount（"标签最多 N 个"提示文字里的 N）
#include "ContactIcons.h"
#include "../BalloonMain/NotifyModeMenu.h"   // 「消息提醒」下拉框的选项（与右键二级菜单同一份顺序和文字）
#include "../../Common/Util/ClipboardText.h"   // 点击姓名 / 账号 / 部门路径复制到剪贴板
#include "../../Common/Log/IULog.h"           // LOG_INFO（标签区行数与滚动范围的排查线索）
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"  // DuiScrollView（标签区滚动视口）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiToast.h"      // 复制成功的轻量提示
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h" // 悬停提示"点击复制"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"                      // GetHost()->m_hWnd（ClientToScreen）
#include "../../../third-party/balloonui/balloonui/DuiXmlBuilder.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Common/I18n/I18nLayout.h"   // GrowFixedWidthToText：译文较长时加宽固定宽度的按钮
#include "../../Common/I18n/I18nXml.h"   // TranslateXml：翻译内嵌 XML 里的界面文字
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"   // 「展开全部 / 收起」链接
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"     // 「个人资料」格的就地编辑输入框
#include "../../Session/ProfileFields.h"   // 性别下拉框的选项与序号；公开范围开关的文字与菜单项
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"   // 公开范围菜单
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"               // 公开范围开关量宽时的默认字体

namespace ballooncontact {

using balloonwjui::DuiControl;
using balloonwjui::DuiXmlBuilder;
using balloonwjui::DuiComboBox;
using balloonwjui::DuiToast;
using balloonwjui::DuiToolTipMgr;
using balloonwjui::DuiScrollView;
using balloonwjui::DuiLabel;

// =====================================================================
// 全局：主题色板（RGB），照任务说明的色板表
// =====================================================================

namespace {

const COLORREF kColorAccent     = RGB(0xFF, 0x7A, 0x45);   // 主题橙 #FF7A45
const COLORREF kColorAccentSoft = RGB(0xFF, 0xE6, 0xD6);   // 浅橙 #FFE6D6
const COLORREF kColorText       = RGB(0x1A, 0x1A, 0x1A);   // 主文字 #1A1A1A
const COLORREF kColorTextMuted  = RGB(0x6B, 0x6B, 0x6B);   // 次文字 #6B6B6B
const COLORREF kColorTextFaint  = RGB(0x9A, 0x9A, 0x9A);   // 弱文字 #9A9A9A
const COLORREF kColorBorder     = RGB(0xED, 0xED, 0xED);   // 分隔线 ≈ED
const COLORREF kColorBorderStrong = RGB(0xE0, 0xE0, 0xE0); // 强描边 ≈E0
const COLORREF kColorWhite      = RGB(0xFF, 0xFF, 0xFF);   // 白

// =====================================================================
// 部门路径区块 / 客户区高度的几何常量（逻辑像素）
// =====================================================================

// 部门路径每一行的行高。取值与头部第 2/3/4 行行高（ContactHeaderInfo::OnPaint
// 里的 kSubRowH）相同，使两块文字的行距观感一致。
const int kDeptPathLineHeight = 17;

// 部门路径区块与其上方头部行之间的间距。
const int kDeptPathTopGap = 6;

// 部门路径的文字字号。与头部第 2/3/4 行同字号（ContactHeaderInfo 的 kSubSize）。
const int kDeptPathFontSize = 12;

// 部门层级之间的分隔符。必须与拼接侧（BalloonMainPanel.cpp 的
// CBalloonMainPanel::ComputeContactDeptInfo）用的分隔串完全一致。
const TCHAR* const kDeptPathSeparator = _T(" / ");

// 中段省略用的省略号 —— 行数超上限时垫在最后一行中间，保留路径首尾。
const TCHAR* const kDeptPathEllipsis = _T("…");

// GetTextExtentExPointW 的 nMaxExtent 取"不限"值：这里只借它取每个字符的
// 累计宽度，不需要它按宽度截断（截断由调用方自己算）。
const int kMeasureNoWidthLimit = 0x7FFFFFFF;

// =====================================================================
// 「个人资料」区块的排版常量（逻辑像素，2026-10-04）
// =====================================================================

const int kProfileBlockPaddingV  = 12;   // 区块上下内边距
const int kProfileTitleHeight    = 16;   // 「个人资料」小标题行高
const int kProfileTitleGap       = 6;    // 小标题与第一行格子之间的间距
const int kProfileLabelHeight    = 16;   // 格子里字段名一行的高度
const int kProfileCellInnerGap   = 2;    // 字段名与值之间的间距
const int kProfileValueHeight    = 22;   // 格子里值一行的设计高度（下限）；实际高度见 ContactProfileValueHeight
const int kProfileEditBorderPx   = 1;    // 就地编辑输入框的边框宽度，与 balloonui DuiEdit.cpp 的 kBorderPx 相同
const int kProfileEditSlackPx    = 2;    // 输入框里一行文字上下各留的余量（DuiEdit 原有的上下内边距）
const int kProfileRowGap         = 8;    // 行与行之间的间距
const int kProfileColumnGap      = 24;   // 两列之间的间距
const int kProfilePencilSize     = 16;   // 编辑按钮的边长
const int kProfileToggleWidth    = 96;   // 「展开全部 / 收起」链接的宽度，英文「Show all」也放得下
const int kProfileToggleFontPt   = 9;    // 「展开全部 / 收起」链接的字号（磅）
const int kProfileVisibilityFontPt   = 9;    // 公开范围开关（「仅自己可见 ▾」链接）的字号（磅），与「展开全部」相同
const int kProfileVisibilitySlackW   = 6;    // 公开范围开关量出的文字宽度之外再留的余量（像素），兜住 DrawText 与测量之间的出入
const int kProfileVisibilityDefaultW = 84;   // 取不到设备上下文时公开范围开关的保守宽度（像素），按中文「仅自己可见 ▾」定
const int kProfileVisibilityMenuGap  = 2;    // 公开范围菜单与开关下边之间的间隙（像素）
const int kProfileSeparatorHeight = 1;   // 区块下方分隔线的高度

// 看本人时整块隐藏的区块高度（逻辑像素），与内嵌 XML 里各自的 fixedHeight 一致
const int kRemarkSectionHeight    = 132;  // 备注名 + 分组两行所在的 vbox
const int kNotifySectionHeight    = 118;  // 消息提醒 + 添加到收藏两行所在的 vbox
const int kSectionSeparatorHeight = 1;    // 区块之间的分隔线
// 两列各自的宽度：客户区去掉左右内边距，再去掉两列之间的间距后平分（600 宽时为 268）
const int kProfileColumnWidth = (kContactBodyWidth - kContactBodyPaddingH * 2 - kProfileColumnGap) / 2;

// 没有部门路径区块、且标签只占一行时的客户区固定高度（逻辑像素），<u>不含</u>
// 快捷操作行本身与它下方那一段间距 —— 那两段由 ContactBodyHeight 用
// kContactQuickRowHeight / kContactQuickRowGap 另行加上，见该函数。
// 按内嵌 XML 各区块自上而下精确累计：
//   客户区上内边距 18
//   头部行 84 + 头部下边距 spacer 14 + 分隔线 1
//   标签区 vbox 76 + 分隔线 1
//   备注名 / 分组区 vbox 132 + 分隔线 1
//   消息提醒 / 收藏区 vbox 118 + 分隔线 1
//   底部按钮行 58
//   客户区下内边距 18
// 合计 = 18 + 99 + 77 + 133 + 119 + 58 + 18 = 522。
// 2026-10-03 之前这一区有「消息免打扰 / 添加到收藏 / 特别关注」三行开关（vbox 170），
// 免打扰与特别关注合并成「消息提醒」下拉框后少了一行 52，合计由 574 变为 522。
// 头部行取 84 而非 64：设计稿头部 4 行文字块（姓名 + 职务 + 账号 + 签名）整体
// 约 82 像素，比头像 64 更高 —— 设计稿 alignItems:center 让头像在该高度内垂直
// 居中，行高不足会裁掉第 4 行签名。
const int kContactBodyBaseHeight = 522;

// =====================================================================
// 标签区的几何常量（逻辑像素）
// =====================================================================

// 一枚标签胶囊的高度，也就是标签区每一行的行高（设计稿 dialogs.jsx 的标签
// 胶囊 height:24）。与内嵌 XML 里 contact-tagscroll 的 row-height 属性同值。
const int kTagRowHeight = 24;

// 相邻两枚胶囊之间的间距，水平与垂直同值（设计稿 gap:6）。与内嵌 XML 里
// contact-tagscroll 的 gap 属性同值。
const int kTagRowGap = 6;

// 标签区 vbox 的上下内边距（单边）。与内嵌 XML 的 padding="0,14,0,14" 一致。
const int kTagBlockPaddingV = 14;

// 标签区小标题「标签」那一行的高度。与内嵌 XML 里该 contact-text 的
// fixedHeight="16" 一致。
const int kTagTitleHeight = 16;

// 小标题与下方胶囊容器之间的间距。与内嵌 XML 里标签区 vbox 的 gap="8" 一致。
const int kTagTitleGap = 8;

// 标签正好一行时标签区整块的高度。等于 14 + 16 + 8 + 24 + 14 = 76，也就是
// 内嵌 XML 里标签区 vbox 写死的那个 fixedHeight —— 单独取个名字是为了让
// ContactBodyHeight 能把"标签多出来的那几行"作为增量加到 kContactBodyBaseHeight
// 上，而不必把 522 拆开重算。
const int kTagBlockOneRowHeight = kTagBlockPaddingV + kTagTitleHeight + kTagTitleGap
                                + kTagRowHeight + kTagBlockPaddingV;

// =====================================================================
// 可点击复制的三处热区：悬停提示与复制成功提示的文案
// =====================================================================

const TCHAR* const kTipCopyName        = TR_NOOP(L"点击复制姓名");
const TCHAR* const kTipCopyAccount     = TR_NOOP(L"点击复制账号");
const TCHAR* const kTipCopyDeptPath    = TR_NOOP(L"点击复制部门路径");
const TCHAR* const kToastCopiedName    = TR_NOOP(L"已复制姓名");
const TCHAR* const kToastCopiedAccount = TR_NOOP(L"已复制账号");
const TCHAR* const kToastCopiedDept    = TR_NOOP(L"已复制部门路径");

// 复制成功提示的显示时长（毫秒，不含淡入淡出）。DuiToast 默认 3000 对
// "已复制 xxx"这种看完即走的短句偏长，缩到 1.6 秒。
const int kCopyToastDurationMs = 1600;

// =====================================================================
// 公共绘制小工具
// =====================================================================

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

// 用指定字号 / 字重创建一个微软雅黑字体。
HFONT CreateYaHeiFont(int pixelHeight, bool bold)
{
    return ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
}

// 用 GDI 绘制单行文字。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = CreateYaHeiFont(pixelHeight, bold);
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

// 测量单行文字宽度（像素）。
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight, bool bold)
{
    if (hdc == NULL || text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    HFONT hFont = CreateYaHeiFont(pixelHeight, bold);
    if (hFont == NULL)
    {
        return 0;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    SIZE  sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz.cx;
}

// 量出 text 每个字符处的累计宽度：outDx[i] 是前 i+1 个字符连起来的总宽度
// （像素）。按字符折行与中段省略都要按字符位置反查宽度，逐字符调
// GetTextExtentPoint32W 太慢，故一次性取回整条累计宽度表。
void MeasureCharExtents(HDC hdc, LPCWSTR text, int pixelHeight, bool bold,
                        std::vector<int>& outDx)
{
    outDx.clear();
    if (hdc == NULL || text == NULL || text[0] == L'\0')
    {
        return;
    }
    const int len = (int)::wcslen(text);
    HFONT hFont = CreateYaHeiFont(pixelHeight, bold);
    if (hFont == NULL)
    {
        return;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    outDx.resize((size_t)len, 0);
    int  fit = 0;
    SIZE sz  = { 0, 0 };
    ::GetTextExtentExPointW(hdc, text, len, kMeasureNoWidthLimit,
                            &fit, &outDx[0], &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

// 取 dx 表里第 [begin, end) 段字符的宽度（像素）。dx 是 MeasureCharExtents
// 的累计宽度表；begin >= end 返回 0。
int CharRangeWidth(const std::vector<int>& dx, int begin, int end)
{
    if (dx.empty() || begin >= end || end > (int)dx.size() || begin < 0)
    {
        return 0;
    }
    const int head = (begin > 0) ? dx[(size_t)begin - 1] : 0;
    return dx[(size_t)end - 1] - head;
}

// 把 text 做中段省略，使其宽度不超过 availWidth：保留开头与结尾，中间垫省略号。
// 用中段省略而不是末尾省略，是因为部门路径末尾那一级（人所在的最小部门）信息量
// 最大，不能被截掉。text 本身没超宽时原样返回。
CString MiddleEllipsisText(HDC hdc, LPCTSTR text, int availWidth, int pixelHeight)
{
    CString source = (text != NULL) ? text : _T("");
    if (source.IsEmpty())
    {
        return source;
    }
    if (MeasureTextWidth(hdc, (LPCWSTR)source, pixelHeight, false) <= availWidth)
    {
        return source;
    }

    const int ellipsisW = MeasureTextWidth(hdc, (LPCWSTR)kDeptPathEllipsis,
                                           pixelHeight, false);
    if (availWidth <= ellipsisW)
    {
        return CString(kDeptPathEllipsis);
    }

    std::vector<int> dx;
    MeasureCharExtents(hdc, (LPCWSTR)source, pixelHeight, false, dx);
    if (dx.empty())
    {
        return source;
    }
    const int total = (int)dx.size();

    // 省略号左右各分一半预算：先从头取尽量多的字符，再从尾取尽量多的字符。
    const int budget = availWidth - ellipsisW;
    int headCount = 0;
    while (headCount < total && CharRangeWidth(dx, 0, headCount + 1) <= budget / 2)
    {
        ++headCount;
    }
    int tailCount = 0;
    while (headCount + tailCount < total
           && CharRangeWidth(dx, total - tailCount - 1, total)
              <= budget - CharRangeWidth(dx, 0, headCount))
    {
        ++tailCount;
    }

    CString result = source.Left(headCount);
    result += kDeptPathEllipsis;
    result += source.Right(tailCount);
    return result;
}

// 把 text 按字符切成若干行追加进 outLines，每行宽度不超过 availWidth。
// maxLines 是 outLines <u>总共</u>允许有多少行（含调用前已有的行）；行数用完时
// 最后一行改为"剩余全部文字的中段省略"，保证末级部门可见。
// 只有单个部门名自身就比整行还宽这种退化数据才会走到本函数。
void WrapTextByChars(HDC hdc, LPCTSTR text, int availWidth, int maxLines,
                     int pixelHeight, std::vector<CString>& outLines)
{
    CString source = (text != NULL) ? text : _T("");
    if (source.IsEmpty() || maxLines < 1)
    {
        return;
    }

    std::vector<int> dx;
    MeasureCharExtents(hdc, (LPCWSTR)source, pixelHeight, false, dx);
    const int total = (int)dx.size();
    if (total <= 0)
    {
        return;
    }

    int start = 0;
    while (start < total)
    {
        // 已经排到允许的最后一行：剩下的文字整段做中段省略收尾。
        if ((int)outLines.size() + 1 >= maxLines)
        {
            CString rest = source.Mid(start);
            outLines.push_back(MiddleEllipsisText(hdc, rest, availWidth, pixelHeight));
            return;
        }

        // 本行尽量多放字符；一个都放不下时也强行放一个，否则死循环。
        int end = start;
        while (end < total && CharRangeWidth(dx, start, end + 1) <= availWidth)
        {
            ++end;
        }
        if (end == start)
        {
            end = start + 1;
        }
        outLines.push_back(source.Mid(start, end - start));
        start = end;
    }
}

// 把 [begin, end) 这几段部门名用分隔符连成一行文本。
CString JoinDeptSegments(const std::vector<CString>& segments,
                         size_t begin, size_t end)
{
    CString line;
    for (size_t i = begin; i < end && i < segments.size(); ++i)
    {
        if (i > begin)
        {
            line += kDeptPathSeparator;
        }
        line += segments[i];
    }
    return line;
}

// 把完整部门路径折成逐行的显示文本。
//
// 分两步：先按层级边界贪心装箱（PackDeptPathLines，纯整数运算），常态到此为止；
// 若某一行装完仍比可用宽度宽（只可能是某个部门名自身就超宽的退化数据），整条路径
// 改走按字符切分，最后一行做中段省略。
//   hdc：只用于量文字宽度，函数内不绘制；
//   deptPath：完整部门路径；
//   availWidth：单行可用宽度（像素）；
//   outLines：出参，逐行显示文本；函数内先清空。
void BuildDeptPathLines(HDC hdc, LPCTSTR deptPath, int availWidth,
                        std::vector<CString>& outLines)
{
    outLines.clear();

    std::vector<CString> segments;
    SplitDeptPathSegments(deptPath, segments);
    if (segments.empty())
    {
        return;
    }

    // 逐段量宽 + 量一次分隔符宽，交给纯函数装箱。
    std::vector<int> segWidths;
    segWidths.reserve(segments.size());
    for (size_t i = 0; i < segments.size(); ++i)
    {
        segWidths.push_back(MeasureTextWidth(hdc, (LPCWSTR)segments[i],
                                             kDeptPathFontSize, false));
    }
    const int sepWidth = MeasureTextWidth(hdc, (LPCWSTR)kDeptPathSeparator,
                                          kDeptPathFontSize, false);

    std::vector<int> lineEndSeg;
    PackDeptPathLines(segWidths, sepWidth, availWidth,
                      kContactDeptPathMaxLines, lineEndSeg);

    size_t start = 0;
    for (size_t k = 0; k < lineEndSeg.size(); ++k)
    {
        if ((int)outLines.size() >= kContactDeptPathMaxLines)
        {
            break;   // 行数已满（装箱最后一行做过并入，正常到不了这里）
        }
        const size_t end = (size_t)lineEndSeg[k];
        CString lineText = JoinDeptSegments(segments, start, end);
        start = end;

        if (MeasureTextWidth(hdc, (LPCWSTR)lineText, kDeptPathFontSize, false)
            <= availWidth)
        {
            outLines.push_back(lineText);
            continue;
        }

        // 这一行按层级切完仍超宽：要么某个部门名自身就比整行还宽，要么它是
        // 装箱时"剩余各段并入"的最后一行。两种都交给按字符切分处理，行数
        // 用完时由它做中段省略收尾。
        WrapTextByChars(hdc, lineText, availWidth, kContactDeptPathMaxLines,
                        kDeptPathFontSize, outLines);
    }
}

// 沿父链走到客户区根控件（contact-root）。from 为 NULL 时返回 NULL。
DuiControl* FindContactBodyRoot(DuiControl* from)
{
    DuiControl* node = from;
    while (node != NULL && node->GetParent() != NULL)
    {
        node = node->GetParent();
    }
    return node;
}

// 显示一条"已复制 xxx"的轻量提示。提示控件挂在客户区根的末尾（见内嵌 XML 的
// contact-toast 节点）；找不到它就什么都不做 —— 复制动作本身已经完成，缺一条
// 提示不影响功能。
void ShowContactCopyToast(DuiControl* from, LPCTSTR text)
{
    // 先把"点击复制 xxx"的悬停提示收掉：它就浮在鼠标附近，不收的话正好压在
    // 提示条上，用户看不清刚复制了什么。用 OnLeave 而不是 HideNow —— 后者只关
    // 已经弹出来的浮窗，不清等待中的计时，于是点击后半秒提示又自己冒出来压上去。
    DuiToolTipMgr::Inst().OnLeave(from);

    DuiControl* root = FindContactBodyRoot(from);
    if (root == NULL)
    {
        return;
    }
    DuiToast* toast = dynamic_cast<DuiToast*>(root->FindCtrlById((UINT)kIdCopyToast));
    if (toast != NULL)
    {
        toast->Show(text);
    }
}

// 把 ctrl 当前的悬停提示切换成 text，并按当前鼠标位置重新开始计时。
//
// DuiToolTipMgr 的注册粒度是"一个控件一段文字"，而头部资料块这一个控件里
// 姓名与账号是两块不同热区，文字得随热区切换。OnHover 对同一控件有"已在等待
// 中则直接返回"的短路，因此必须先 OnLeave 复位再 Register 新文字，最后 OnHover
// 重新计时。做法与 ImageViewerToolbar::UpdateTooltip 一致。
//   ctrl：目标控件，本函数不持有；
//   text：提示文字；空指针 / 空串表示当前位置没有可提示的内容，只做复位；
//   ptHost：鼠标位置（host 客户区坐标），用于换算浮窗弹出的屏幕坐标。
void SwitchHoverTooltip(DuiControl* ctrl, LPCTSTR text, POINT ptHost)
{
    if (ctrl == NULL)
    {
        return;
    }
    DuiToolTipMgr& tips = DuiToolTipMgr::Inst();
    tips.OnLeave(ctrl);
    if (text == NULL || text[0] == _T('\0'))
    {
        return;
    }
    tips.Register(ctrl, text);

    POINT screen = ptHost;
    balloonwjui::DuiHost* host = ctrl->GetHost();
    if (host != NULL)
    {
        ::ClientToScreen(host->m_hWnd, &screen);
    }
    tips.OnHover(ctrl, screen);
}

// 把 UTF-8 字节串转成 CString（宽字符）。内嵌 XML 是 u8 字面量（UTF-8），
// 属性里的中文必须走本函数 —— 不能用 CA2T（CA2T 按系统 ANSI 代码页解释，
// 中文系统会把 UTF-8 字节当 GBK 读，导致界面文字乱码）。
CString Utf8ToCString(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    const int wlen = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (wlen <= 0)
    {
        return CString();
    }
    CString result;
    LPWSTR buf = result.GetBuffer(wlen);
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                          buf, wlen);
    result.ReleaseBuffer(wlen);
    return result;
}

// 读取节点属性值；属性不存在时返回 def。
std::string GetNodeAttr(const DuiXmlBuilder::Node& n, const char* key,
                        const char* def = "")
{
    std::map<std::string, std::string>::const_iterator it = n.attrs.find(key);
    return (it != n.attrs.end()) ? it->second : std::string(def);
}

// 解析 "r,g,b" 形式的颜色属性（复用 builder 的 ParseColor 不便，它在 builder 的匿名命名空间里）。
// 各分量超过 255 时按 255 处理；属性为空或一个分量都没有时返回 def；只写了部分分量时其余分量取 def 的值。
COLORREF ParseRgbAttr(const std::string& text, COLORREF def)
{
    const int kComponentCount = 3;
    const int kComponentMax = 255;
    const int kDecimal = 10;
    int rgb[kComponentCount] = { GetRValue(def), GetGValue(def), GetBValue(def) };
    int idx = 0;
    int cur = 0;
    bool hasDigit = false;
    for (size_t i = 0; i <= text.size() && idx < kComponentCount; ++i)
    {
        const char ch = (i < text.size()) ? text[i] : ',';
        if (ch >= '0' && ch <= '9')
        {
            cur = cur * kDecimal + (ch - '0');
            hasDigit = true;
        }
        else if (ch == ',')
        {
            if (hasDigit)
            {
                rgb[idx] = (cur > kComponentMax) ? kComponentMax : cur;
                ++idx;
            }
            cur = 0;
            hasDigit = false;
        }
    }
    return RGB(rgb[0], rgb[1], rgb[2]);
}

// 把 net-text 的 align 属性翻译成 DrawText 对齐标志。
UINT TextAlignFromAttr(const std::string& align)
{
    if (align == "center")
    {
        return DT_CENTER;
    }
    if (align == "right")
    {
        return DT_RIGHT;
    }
    return DT_LEFT;
}

// =====================================================================
// 布局自用小控件
// =====================================================================

/**
 *  ContactRootPanel —— 联系人资料对话框客户区根面板。铺纯白底，再交
 *  基类（DuiVBox）纵向排布子控件。
 */
class ContactRootPanel : public balloonwjui::DuiVBox
{
public:
    void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        HBRUSH white = ::CreateSolidBrush(kColorWhite);
        ::FillRect(hdc, &m_rcItem, white);
        ::DeleteObject(white);
        DuiVBox::OnPaint(hdc, rcDirty);
    }
};

/**
 *  ContactText —— 单行文本控件。字号 / 颜色 / 粗细 / 对齐由 XML 属性
 *  决定，用于小标题与 FieldRow 标签。纯绘制控件。
 */
class ContactText : public balloonwjui::DuiControl
{
public:
    ContactText()
        : m_size(13)
        , m_color(kColorText)
        , m_bold(false)
        , m_align(DT_LEFT)
    {
    }

    // 配置文本内容与样式。
    void Configure(LPCTSTR text, int size, COLORREF color, bool bold, UINT align)
    {
        m_text  = (text != NULL) ? text : _T("");
        m_size  = size;
        m_color = color;
        m_bold  = bold;
        m_align = align;
    }

    // 修改文本内容（保持样式不变）。供「备注名」当前值刷新等场景用。
    void SetTextContent(LPCTSTR text)
    {
        m_text = (text != NULL) ? text : _T("");
        Invalidate();
    }

    // 修改文字颜色（保持其余样式不变）。供「个人资料」区块的「未填写」改用弱色等场景用。
    void SetTextColorValue(COLORREF color)
    {
        m_color = color;
        Invalidate();
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        DrawGdiText(hdc, (LPCWSTR)m_text, m_rcItem, m_size, m_bold, m_color,
                    m_align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                    | DT_END_ELLIPSIS);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    CString  m_text;    // 文本内容
    int      m_size;    // 字号（像素）
    COLORREF m_color;   // 文字颜色
    bool     m_bold;    // 是否粗体
    UINT     m_align;   // 水平对齐（DT_LEFT / DT_CENTER / DT_RIGHT）
};

/**
 *  ContactHeaderInfo —— 头部右侧的多行资料块。一控件画四行：
 *    第 1 行：姓名（18 粗体）+ 备注名（12 弱色，括号）；
 *    第 2 行：职务（12，accent 色，字重 500）；
 *    第 3 行：账号 + 地区（12，text-muted）；
 *    第 4 行：个性签名（12，text-muted）。
 *  四行的字号 / 颜色 / 行距严格照设计稿 ContactInfoDialog 的 header。
 *
 *  部门路径原先与职务挤在第 2 行（形如「职务 · 部门路径」），头部右侧可用宽度
 *  只有 302 像素，四级以上的部门路径必被省略号截断；2026-08-13 起把它拆到独立
 *  的整宽区块 ContactDeptPath 里显示，本控件第 2 行只留职务。
 *
 *  第 1 行姓名与第 3 行账号两块文字可点击复制：命中哪一块由 OnPaint 记下的两个
 *  热区矩形判定，悬停时给出对应的提示文字，点击后把内容写入剪贴板并弹一条轻量
 *  提示。按产品要求，悬停时<u>不</u>把光标变成手型。
 */
class ContactHeaderInfo : public balloonwjui::DuiControl
{
public:
    // 头部里可点击复制的热区编号。OnPaint 记下各热区矩形，鼠标事件按它判定命中。
    enum CopyZone
    {
        kZoneNone    = 0,   // 当前位置不在任何可复制的文字上
        kZoneName    = 1,   // 第 1 行姓名（不含后面括号里的备注名）
        kZoneAccount = 2,   // 第 3 行账号（含"账号 "前缀与地区的整行）
    };

    ~ContactHeaderInfo() override
    {
        // 必须注销：管理器按裸指针记录注册项，控件析构后若还留着记录，
        // 下一次悬停就会拿到已销毁的指针。
        DuiToolTipMgr::Inst().Unregister(this);
    }

    // 用模型数据配置四行内容。
    //   primaryName：第 1 行姓名（真名）；点姓名时复制的就是它；
    //   parenText：第 1 行姓名后紧跟的括号内容（当前放备注名），无则空；
    //   title：第 2 行职务，无职务则空（部门路径不在这一行）；
    //   accountRegion：第 3 行「账号 XXX · 地区」展示文本；
    //   signature：第 4 行个性签名；
    //   account：裸账号(点账号行复制它到剪贴板,不含"账号 "前缀与地区)。
    void Configure(LPCTSTR primaryName, LPCTSTR parenText,
                   LPCTSTR title, LPCTSTR accountRegion,
                   LPCTSTR signature, LPCTSTR account)
    {
        m_primaryName   = (primaryName != NULL) ? primaryName : _T("");
        m_parenText = (parenText != NULL) ? parenText : _T("");
        m_title         = (title != NULL) ? title : _T("");
        m_accountRegion = (accountRegion != NULL) ? accountRegion : _T("");
        m_signature     = (signature != NULL) ? signature : _T("");
        m_account       = (account != NULL) ? account : _T("");
        Invalidate();
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int x = m_rcItem.left;
        const int w = m_rcItem.right - m_rcItem.left;
        if (w <= 0)
        {
            return;
        }

        // ---- 行几何常量（逻辑像素，照设计稿 header）----
        const int kNameSize     = 18;   // 主名字号
        const int kRealNameSize = 12;   // 括号真名字号
        const int kSubSize      = 12;   // 第 2/3/4 行字号
        const int kNameRowH     = 24;   // 第 1 行行高
        const int kSubRowH      = 17;   // 第 2/3/4 行行高
        const int kGapTitle     = 3;    // 第 1 行到第 2 行的上间距
        const int kGapAccount   = 2;    // 第 2 行到第 3 行的上间距
        const int kGapSign      = 2;    // 第 3 行到第 4 行的上间距
        const int kNameGapParen = 6;    // 主名与括号真名之间的间距

        // 四行内容整体在控件高度内垂直居中（头像 64 高，文字块通常更矮）。
        int totalH = kNameRowH;
        if (!m_title.IsEmpty())
        {
            totalH += kGapTitle + kSubRowH;
        }
        totalH += kGapAccount + kSubRowH;   // 账号行始终有
        if (!m_signature.IsEmpty())
        {
            totalH += kGapSign + kSubRowH;
        }
        const int ctrlH = m_rcItem.bottom - m_rcItem.top;
        int y = m_rcItem.top + ((ctrlH > totalH) ? (ctrlH - totalH) / 2 : 0);

        // ---- 第 1 行：主名（粗体）+ 括号真名（弱色）----
        {
            const int nameW = MeasureTextWidth(hdc, (LPCWSTR)m_primaryName,
                                               kNameSize, true);
            RECT nameRc = { x, y, x + nameW, y + kNameRowH };
            // 主名宽度可能超出控件，dt 不裁切但限制在控件右界内。
            if (nameRc.right > m_rcItem.right)
            {
                nameRc.right = m_rcItem.right;
            }
            // 记下姓名矩形(host 坐标),供鼠标事件判定命中:点姓名复制姓名。
            m_nameRect = nameRc;
            DrawGdiText(hdc, (LPCWSTR)m_primaryName, nameRc, kNameSize, true,
                        kColorText,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                        | DT_END_ELLIPSIS);

            if (!m_parenText.IsEmpty())
            {
                const int parenX = nameRc.right + kNameGapParen;
                RECT parenRc = { parenX, y, m_rcItem.right, y + kNameRowH };
                DrawGdiText(hdc, (LPCWSTR)m_parenText, parenRc,
                            kRealNameSize, false, kColorTextFaint,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                            | DT_END_ELLIPSIS);
            }
            y += kNameRowH;
        }

        // ---- 第 2 行：职务（accent 色）----
        if (!m_title.IsEmpty())
        {
            y += kGapTitle;
            RECT rc = { x, y, m_rcItem.right, y + kSubRowH };
            DrawGdiText(hdc, (LPCWSTR)m_title, rc, kSubSize, false,
                        kColorAccent,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                        | DT_END_ELLIPSIS);
            y += kSubRowH;
        }

        // ---- 第 3 行：账号 · 地区（text-muted）----
        {
            y += kGapAccount;
            RECT rc = { x, y, m_rcItem.right, y + kSubRowH };
            // 记下账号行矩形(host 坐标),供 OnLButtonUp/OnSetCursor 命中判断:点该行复制账号。
            m_accountRect = rc;
            DrawGdiText(hdc, (LPCWSTR)m_accountRegion, rc, kSubSize, false,
                        kColorTextMuted,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                        | DT_END_ELLIPSIS);
            y += kSubRowH;
        }

        // ---- 第 4 行：个性签名（text-muted）----
        if (!m_signature.IsEmpty())
        {
            y += kGapSign;
            RECT rc = { x, y, m_rcItem.right, y + kSubRowH };
            DrawGdiText(hdc, (LPCWSTR)m_signature, rc, kSubSize, false,
                        kColorTextMuted,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                        | DT_END_ELLIPSIS);
        }
    }

    // 点姓名(第 1 行)复制姓名、点账号行(第 3 行)复制裸账号,并各弹一条轻量提示。
    // 落在两块热区之外的点击不拦,继续按未处理上冒。
    bool OnLButtonUp(POINT pt, UINT /*mkFlags*/) override
    {
        const int zone = HitCopyZone(pt);
        if (zone == kZoneName)
        {
            flamingo::CopyTextToClipboard(m_primaryName);
            ShowContactCopyToast(this, TR(kToastCopiedName));
            return true;
        }
        if (zone == kZoneAccount)
        {
            flamingo::CopyTextToClipboard(m_account);
            ShowContactCopyToast(this, TR(kToastCopiedAccount));
            return true;
        }
        return false;
    }

    // 鼠标移动:热区变了就把悬停提示换成对应文字并重新计时。
    bool OnMouseMove(POINT pt, UINT /*mkFlags*/) override
    {
        const int zone = HitCopyZone(pt);
        if (zone != m_hoverZone)
        {
            m_hoverZone = zone;
            SwitchHoverTooltip(this, TooltipForZone(zone), pt);
        }
        return false;
    }

    // 鼠标离开整个资料块:清掉热区与待显示的提示。
    bool OnMouseLeave() override
    {
        m_hoverZone = kZoneNone;
        DuiToolTipMgr::Inst().OnLeave(this);
        return false;
    }

    // 按产品要求,悬停在可复制文字上<u>不</u>变手型,一律维持默认箭头 ——
    // 返回 false 让 host 去设 IDC_ARROW。
    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    // 判定 pt(host 坐标)落在哪一块可复制的文字上；内容为空的那块不参与判定。
    int HitCopyZone(POINT pt) const
    {
        if (!m_primaryName.IsEmpty() && ::PtInRect(&m_nameRect, pt))
        {
            return kZoneName;
        }
        if (!m_account.IsEmpty() && ::PtInRect(&m_accountRect, pt))
        {
            return kZoneAccount;
        }
        return kZoneNone;
    }

    // 各热区对应的悬停提示文字；kZoneNone 返回空串（表示此处不提示）。
    static LPCTSTR TooltipForZone(int zone)
    {
        switch (zone)
        {
        //姓名热区：提示点击可复制姓名
        case kZoneName:
            return TR(kTipCopyName);

        //账号热区：提示点击可复制账号
        case kZoneAccount:
            return TR(kTipCopyAccount);

        //其余位置（含热区之外的空白）不给提示
        default:
            return _T("");
        }
    }

private:
    CString m_primaryName;     // 第 1 行姓名（真名），点击复制的就是它
    CString m_parenText;       // 第 1 行姓名后的括号内容（当前是备注名），可空
    CString m_title;           // 第 2 行职务，可空（部门路径已独立成块，不在这里）
    CString m_accountRegion;   // 第 3 行账号 · 地区（展示文本）
    CString m_account;         // 裸账号（点击复制用，不含"账号 "前缀与地区）
    CString m_signature;       // 第 4 行个性签名，可空
    RECT    m_nameRect = { 0, 0, 0, 0 };      // 姓名矩形(OnPaint 填,host 坐标),点击命中用
    RECT    m_accountRect = { 0, 0, 0, 0 };   // 账号行矩形(OnPaint 填,host 坐标),点击命中用
    int     m_hoverZone = kZoneNone;          // 当前鼠标所在热区，见 CopyZone
};

/**
 *  ContactDeptPath —— 部门路径整宽区块（自绘）。
 *
 *  独占客户区一整行的宽度（380 像素），层级多时按 " / " 的层级边界折行，
 *  最多 kContactDeptPathMaxLines 行；单个部门名自身就超宽这种退化数据按字符
 *  再切，行数还是不够时最后一行做中段省略，保证末级部门可见。
 *
 *  区块高度由行数决定，由 ConfigureContactDeptPath 写回父容器的布局提示，
 *  窗口再按同一行数调整自身高度（见 ContactBodyHeight）。
 *
 *  整块文字可点击复制：复制的是<u>原始完整路径</u>，不是折行后带换行的版本，
 *  也不含职务。悬停时给提示但不变手型（与头部资料块一致）。
 */
class ContactDeptPath : public balloonwjui::DuiControl
{
public:
    ~ContactDeptPath() override
    {
        // 同 ContactHeaderInfo：注册表按裸指针记录，析构时必须注销。
        DuiToolTipMgr::Inst().Unregister(this);
    }

    // 设置要显示的完整部门路径并就地算好折行结果。
    //   deptPath：完整部门路径；nullptr / 空串 / 只有分隔符时视为没有部门。
    //   availWidth：单行可用宽度（像素）。
    //   返回：折行后的行数（0 表示没有部门路径，本控件不占位）。
    int SetDeptPath(LPCTSTR deptPath, int availWidth)
    {
        m_path = (deptPath != NULL) ? deptPath : _T("");
        m_path.Trim();
        m_lines.clear();

        if (!m_path.IsEmpty())
        {
            // 只测文字不绘制，用桌面 DC 即可（与 ContactTagFlow 的测量口径一致）。
            HDC hdcMeasure = ::GetDC(NULL);
            BuildDeptPathLines(hdcMeasure, m_path, availWidth, m_lines);
            if (hdcMeasure != NULL)
            {
                ::ReleaseDC(NULL, hdcMeasure);
            }
        }

        // 路径为空时连提示也要撤掉，否则鼠标扫过空区块还会弹出"点击复制部门路径"。
        if (m_lines.empty())
        {
            DuiToolTipMgr::Inst().Unregister(this);
        }
        else
        {
            DuiToolTipMgr::Inst().Register(this, TR(kTipCopyDeptPath));
        }

        Invalidate();
        return (int)m_lines.size();
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int w = m_rcItem.right - m_rcItem.left;
        if (w <= 0 || m_lines.empty())
        {
            return;
        }

        int y = m_rcItem.top + kDeptPathTopGap;
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            RECT rc = { m_rcItem.left, y, m_rcItem.right, y + kDeptPathLineHeight };
            DrawGdiText(hdc, (LPCWSTR)m_lines[i], rc, kDeptPathFontSize, false,
                        kColorAccent,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            y += kDeptPathLineHeight;
        }
    }

    // 点区块内的文字区域 → 把原始完整路径复制到剪贴板并弹一条轻量提示。
    bool OnLButtonUp(POINT pt, UINT /*mkFlags*/) override
    {
        if (m_path.IsEmpty() || !::PtInRect(&m_rcItem, pt))
        {
            return false;
        }
        flamingo::CopyTextToClipboard(m_path);
        ShowContactCopyToast(this, TR(kToastCopiedDept));
        return true;
    }

    // 与头部资料块一致：悬停不变手型。
    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    CString m_path;                 // 原始完整部门路径（点击复制的就是它），可空
    std::vector<CString> m_lines;   // 折行后逐行的显示文本；空表示本区块不占位
};

/**
 *  ContactAvatar —— 头部头像（DuiAvatar 的子类，2026-10-07）。
 *
 *  DuiAvatar 本身是纯绘制控件：悬停时不变光标，松开左键时由基类 DuiControl 发出通用的 DUIN_CLICK。
 *  资料窗看本人时头像要能点击打开修改头像对话框，看别人时不能点，故子类化补两件事：
 *    · 可点击时：悬停显示手形光标与提示文字（如「修改头像」），松开左键发 DUIN_CLICK；
 *    · 不可点击时：不变光标，也不发点击通知，看别人时点头像不会被当成「修改头像」。
 *  本控件不画悬停效果，故不必在鼠标进出时请求重画（UI 约定第 9 条）。
 */
class ContactAvatar : public balloonwjui::DuiAvatar
{
public:
    ContactAvatar()
        : m_clickable(false)
    {
    }

    ~ContactAvatar() override
    {
        // 提示管理器按裸指针记录注册项，控件析构时必须注销
        DuiToolTipMgr::Inst().Unregister(this);
    }

    // 设置是否可点击与可点击时的悬停提示文字（NULL 或空串表示不提示）。
    void SetClickable(bool clickable, LPCTSTR tooltip)
    {
        m_clickable = clickable;
        DuiToolTipMgr::Inst().Unregister(this);
        if (clickable && tooltip != NULL && tooltip[0] != _T('\0'))
        {
            DuiToolTipMgr::Inst().Register(this, tooltip);
        }
    }

    // 当前是否可点击。
    bool IsClickable() const
    {
        return m_clickable;
    }

    // 可点击时显示手形光标；不可点击时交给宿主设默认箭头。
    bool OnSetCursor(POINT /*pt*/) override
    {
        if (!m_clickable)
        {
            return false;
        }
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }

    // 可点击且松开在头像之内时发 DUIN_CLICK；否则不处理，也不发通用点击通知。
    bool OnLButtonUp(POINT pt, UINT /*mkFlags*/) override
    {
        if (!m_clickable || !IsEnabled() || !::PtInRect(&m_rcItem, pt))
        {
            return false;
        }
        NotifyParent(balloonwjui::DUIN_CLICK);
        return true;
    }

private:
    bool m_clickable;   // 是否可点击：资料窗看本人时为 true，由 SetContactAvatarClickable 设置
};

/**
 *  ContactTagFlow —— 标签流式容器。子控件（标签胶囊 + 「+ 添加标签」
 *  按钮）按可用宽度从左到右排布，排不下自动换行。每个子控件的宽 / 高
 *  由其自身决定 —— 容器只负责定位。
 */
class ContactTagFlow : public balloonwjui::DuiControl
{
public:
    ContactTagFlow()
        : m_gap(6)
        , m_rowHeight(24)
    {
    }

    // 设置子控件之间的水平 / 垂直间距（设计稿 gap:6）。
    void SetGap(int gap) { m_gap = (gap < 0) ? 0 : gap; }

    // 设置每行（即每枚胶囊）的统一高度（设计稿胶囊高 24）。
    void SetRowHeight(int h) { m_rowHeight = (h < 1) ? 1 : h; }

    // 容器在当前子控件下排满需要的总高度（逻辑像素）。滚动视口按它设置滚动
    // 范围，窗口按行数决定标签区要占多高。
    //   availWidth：单行可用宽度（像素）。
    int CalcDesiredHeight(int availWidth) const
    {
        const int rows = CountRows(availWidth);
        if (rows <= 0)
        {
            return m_rowHeight;
        }
        return rows * m_rowHeight + (rows - 1) * m_gap;
    }

    // 在给定可用宽度下，按当前子控件排布需要多少行。子控件一个都没有时返回 1
    //（标签区始终要留出一行位置给「+ 添加标签」按钮）。
    //   availWidth：单行可用宽度（像素）；<= 0 时按 1 行处理。
    int CountRows(int availWidth) const;

    void Layout(const RECT& rcAvail) override
    {
        m_rcItem = rcAvail;

        const int availW = rcAvail.right - rcAvail.left;
        if (availW <= 0)
        {
            return;
        }

        // 测量子控件宽度需要一个 DC —— 用桌面 DC（只测文字，不绘制）。
        HDC hdcMeasure = ::GetDC(NULL);

        int curX = rcAvail.left;
        int curY = rcAvail.top;
        for (const auto& child : m_children)
        {
            DuiControl* c = child.get();
            if (c == NULL || !c->IsVisible())
            {
                continue;
            }

            const int childW = MeasureChildWidth(c, hdcMeasure);

            // 本行放不下（且不是行首）→ 换行。
            if (curX > rcAvail.left && curX + childW > rcAvail.right)
            {
                curX = rcAvail.left;
                curY += m_rowHeight + m_gap;
            }

            RECT childRc;
            childRc.left   = curX;
            childRc.top    = curY;
            childRc.right  = curX + childW;
            childRc.bottom = curY + m_rowHeight;
            // 必须用 SetRect 而非 Layout —— DuiControl::Layout 的默认实现
            // 只递归子节点、不写 m_rcItem；唯有 SetRect / ForceLayout 才会
            // 真正把矩形落到控件上（与 balloonui 各容器的约定一致）。若此
            // 处只调 Layout，标签胶囊 / 「+ 添加标签」按钮的 m_rcItem 恒为
            // 全零，OnPaint 在 w<=0 处直接返回，整片标签区都不显示。
            c->SetRect(childRc);

            curX += childW + m_gap;
        }

        if (hdcMeasure != NULL)
        {
            ::ReleaseDC(NULL, hdcMeasure);
        }
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    // 测量一个子控件的理想宽度。标签胶囊 / 「+ 添加标签」按钮 / "标签已达上限"
    // 说明文字各自提供宽度算法；其它控件回退到 GetDesiredSize().cx。
    int MeasureChildWidth(DuiControl* c, HDC hdcMeasure) const;

private:
    int m_gap;         // 子控件间距（水平 + 垂直）
    int m_rowHeight;   // 每行高度
};

/**
 *  ContactAddTagButton —— 标签区末尾的「+ 添加标签」按钮（自绘）。
 *  虚线边框、全圆角、透明底、accent 文字。点击向父冒 DUIN_CLICK，由
 *  窗口弹出标签输入。继承 DuiButton 复用点击 / 通知，只覆写 OnPaint。
 */
class ContactAddTagButton : public balloonwjui::DuiButton
{
public:
    // 「+ 添加标签」按钮的固定宽度（设计稿 width:90）。
    static const int kFixedWidth = 90;

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

        // 全圆角虚线边（设计稿 1px dashed border-strong，borderRadius:999）。
        const Gdiplus::REAL r = (Gdiplus::REAL)h / 2.0f - 0.5f;
        const Gdiplus::REAL d = r * 2.0f;
        Gdiplus::GraphicsPath path;
        path.AddArc((Gdiplus::REAL)x + 0.5f,             (Gdiplus::REAL)y + 0.5f,
                    d, d, 90.0f, 180.0f);
        path.AddArc((Gdiplus::REAL)x + w - d - 0.5f,     (Gdiplus::REAL)y + 0.5f,
                    d, d, 270.0f, 180.0f);
        path.CloseFigure();

        Gdiplus::Pen pen(Gdiplus::Color(255,
            GetRValue(kColorBorderStrong), GetGValue(kColorBorderStrong),
            GetBValue(kColorBorderStrong)), 1.0f);
        pen.SetDashStyle(Gdiplus::DashStyleDash);
        g.DrawPath(&pen, &path);

        // 文字「+ 添加标签」—— accent 色、字号 11.5（取整 12）。
        DrawGdiText(hdc, (LPCWSTR)GetText(), m_rcItem, 12, false, kColorAccent,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }
};

/**
 *  ContactTagScrollView —— 标签区专用的滚动视口。
 *
 *  与 balloonui 的 DuiScrollView 只差一点：基类为了让"内容之外的空白"看得出来，
 *  会给整个视口铺一层 RGB(252,252,252) 的浅底；而本对话框客户区是纯白的，那层
 *  浅底会在标签行的位置留下一条横贯整宽的浅灰带子。本子类只把这层底色改成白色，
 *  其余绘制逻辑（内容裁剪、滚动条）与基类完全一致。
 *
 *  之所以重写整个 OnPaint 而不是"先铺白再调基类"：基类的铺底在它自己的 OnPaint
 *  开头，先铺白也会被它随后铺的浅灰盖掉。基类的成员是私有的，这里改用它公开的
 *  GetContent / GetScrollBar / GetScrollBarWidth 取同样的东西。
 */
class ContactTagScrollView : public DuiScrollView
{
public:
    void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        if (!IsVisible())
        {
            return;
        }

        // 视口底色取客户区的白，与四周融为一体。
        HBRUSH hBrush = ::CreateSolidBrush(kColorWhite);
        ::FillRect(hdc, &m_rcItem, hBrush);
        ::DeleteObject(hBrush);

        // 内容的绘制要裁到视口内，否则滚动时超出部分会画到视口外面去。2026-10-04 起
        // 滚动条为悬浮式、浮在内容之上，裁剪范围就是整个视口（此前要扣掉滚动条那一列）。
        // 用 SaveDC + IntersectClipRect + RestoreDC 是相交语义，与基类的做法一致。
        int dcSaved = ::SaveDC(hdc);
        ::IntersectClipRect(hdc, m_rcItem.left, m_rcItem.top, m_rcItem.right, m_rcItem.bottom);

        DuiControl* content = GetContent();
        if (content != NULL)
        {
            RECT inter;
            if (::IntersectRect(&inter, &content->GetRect(), &rcDirty))
            {
                content->OnPaint(hdc, inter);
            }
        }

        ::RestoreDC(hdc, dcSaved);

        // 滚动条浮在内容右缘之上，最后画；它本来就在视口内，不必裁剪。
        balloonwjui::DuiScrollBar* scrollBar = GetScrollBar();
        if (scrollBar != NULL && scrollBar->IsVisible())
        {
            RECT inter;
            if (::IntersectRect(&inter, &scrollBar->GetRect(), &rcDirty))
            {
                scrollBar->OnPaint(hdc, inter);
            }
        }
    }
};

/**
 *  ContactTagLimitHint —— 标签数达上限时顶替「+ 添加标签」按钮的说明文字。
 *
 *  只有一行弱色小字（形如「标签最多 10 个」），无边框、无底色、不可点击。之所以
 *  单独做一个控件而不是复用 ContactText：流式容器要按子控件的理想宽度排布，而
 *  ContactText 没有实现宽度测量，塞进去会被算成 0 宽，几枚胶囊会叠在它上面。
 */
class ContactTagLimitHint : public balloonwjui::DuiControl
{
public:
    // 设置说明文字。文字由 CreateTagLimitHint 按标签数上限拼好后传进来。
    //   text：说明文字；nullptr 视为空。
    void SetHintText(LPCTSTR text)
    {
        m_text = (text != NULL) ? text : _T("");
        Invalidate();
    }

    // 计算说明文字在当前字号下的理想宽度（逻辑像素）。流式容器按它排布。
    //   hdcMeasure：用于测量文字的 DC。
    //   返回：文字宽度（像素）。
    int CalcDesiredWidth(HDC hdcMeasure) const
    {
        return MeasureTextWidth(hdcMeasure, (LPCWSTR)m_text, kTagHintFontSize, false);
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        DrawGdiText(hdc, (LPCWSTR)m_text, m_rcItem, kTagHintFontSize, false,
                    kColorTextFaint,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    // 说明文字的字号（像素）。与「+ 添加标签」按钮上的文字同字号，两者会在
    // 同一位置交替出现，字号不同会看出跳变。
    static const int kTagHintFontSize = 12;

private:
    CString m_text;   // 说明文字内容
};

/**
 *  ContactIconButton —— 「备注名」行的铅笔编辑小按钮（自绘）。透明底，
 *  只画一个铅笔图标；hover 时画一个极浅的圆角底。点击向父冒 DUIN_CLICK。
 */
class ContactIconButton : public balloonwjui::DuiButton
{
public:
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

        // hover 浅灰底（设计稿 IconBtn hover 用 --hover 色）。
        if (IsHover())
        {
            Gdiplus::GraphicsPath bg;
            AddRoundRect(bg, (Gdiplus::REAL)x, (Gdiplus::REAL)y,
                         (Gdiplus::REAL)w, (Gdiplus::REAL)h, 6.0f);
            Gdiplus::SolidBrush brush(Gdiplus::Color(255, 0xF2, 0xF1, 0xEE));
            g.FillPath(&brush, &bg);
        }

        // 铅笔图标 —— 设计稿 Icon.edit size 13，居中绘制。
        const Gdiplus::REAL kGlyph = 13.0f;
        const Gdiplus::REAL gx = (Gdiplus::REAL)x + (w - kGlyph) / 2.0f;
        const Gdiplus::REAL gy = (Gdiplus::REAL)y + (h - kGlyph) / 2.0f;
        DrawContactIcon(g, kContactIconEdit, gx, gy, kGlyph,
                        Gdiplus::Color(255,
                            GetRValue(kColorTextMuted),
                            GetGValue(kColorTextMuted),
                            GetBValue(kColorTextMuted)),
                        1.6f);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }
};

/**
 *  ContactFieldRow —— 设计稿 FieldRow 的等价行容器。继承 DuiHBox 做
 *  「左标签 + 右控件」横向排布；divider=true 时在行底部画一条 1px
 *  分隔线。子控件由 XML 提供（标签 ContactText、右侧控件 / 容器）。
 */
class ContactFieldRow : public balloonwjui::DuiHBox
{
public:
    ContactFieldRow() : m_divider(true) {}

    // 设置本行底部是否画分隔线。
    void SetDivider(bool b) { m_divider = b; }

    void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        // 先让基类排布并绘制左右子控件。
        DuiHBox::OnPaint(hdc, rcDirty);

        // 再在行底画 1px 分隔线（divider=true 时）。
        if (m_divider)
        {
            RECT line;
            line.left   = m_rcItem.left;
            line.right  = m_rcItem.right;
            line.bottom = m_rcItem.bottom;
            line.top    = m_rcItem.bottom - 1;
            HBRUSH br = ::CreateSolidBrush(kColorBorder);
            ::FillRect(hdc, &line, br);
            ::DeleteObject(br);
        }
    }

private:
    bool m_divider;   // 行底是否画分隔线
};

/**
 *  ContactActionButton —— 操作按钮（自绘）。底部按钮行与快捷操作行共用这一个
 *  类型：primary=true 为 accent 实心主按钮（「发消息」），false 为白底描边
 *  ghost 按钮（「关闭」以及快捷操作行的四个按钮）。文字左侧可选画一个图标，
 *  画哪一个由 SetIconKind 指定。继承 DuiButton 复用点击 / 通知，只覆写 OnPaint。
 */
class ContactActionButton : public balloonwjui::DuiButton
{
public:
    // SetIconKind 的特殊取值：本按钮只画文字、不画图标。ContactIconKind 从 0 起
    // 连续编号，取 -1 与任何有效图标都区分得开。
    static const int kNoIcon = -1;

    ContactActionButton()
        : m_primary(false)
        , m_iconKind(kNoIcon)
    {
    }

    // 设为主按钮（accent 实心）或 ghost 按钮（白底描边）。
    void SetPrimary(bool primary)
    {
        m_primary = primary;
        Invalidate();
    }

    // 设置按钮文字左侧要绘制的图标。
    //   kind：图标类型，取值见 ContactIconKind；传 kNoIcon 表示不画图标。
    void SetIconKind(int kind)
    {
        m_iconKind = kind;
        Invalidate();
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

        // 圆角 10（设计稿 Btn size md borderRadius:10）。
        Gdiplus::GraphicsPath path;
        AddRoundRect(path, (Gdiplus::REAL)x + 0.5f, (Gdiplus::REAL)y + 0.5f,
                     (Gdiplus::REAL)w - 1.0f, (Gdiplus::REAL)h - 1.0f, 10.0f);

        COLORREF textColor;
        if (m_primary)
        {
            // accent 实心主按钮，hover 略深。
            const Gdiplus::Color fill = IsHover()
                ? Gdiplus::Color(255, 0xFF, 0x6A, 0x30)
                : Gdiplus::Color(255, 0xFF, 0x7A, 0x45);
            Gdiplus::SolidBrush brush(fill);
            g.FillPath(&brush, &path);
            Gdiplus::Pen border(Gdiplus::Color(255, 0xFF, 0x7A, 0x45), 1.0f);
            g.DrawPath(&border, &path);
            textColor = kColorWhite;
        }
        else
        {
            // ghost 按钮：白底（hover 浅灰）+ border-strong 描边。
            const Gdiplus::Color fill = IsHover()
                ? Gdiplus::Color(255, 0xF6, 0xF5, 0xF2)
                : Gdiplus::Color(255, 0xFF, 0xFF, 0xFF);
            Gdiplus::SolidBrush brush(fill);
            g.FillPath(&brush, &path);
            Gdiplus::Pen border(Gdiplus::Color(255,
                GetRValue(kColorBorderStrong), GetGValue(kColorBorderStrong),
                GetBValue(kColorBorderStrong)), 1.0f);
            g.DrawPath(&border, &path);
            textColor = kColorText;
        }

        // 文字字号 13.5（取整 14，照 Btn size md）。
        const int kTextSize = 14;

        if (m_iconKind != kNoIcon)
        {
            // 图标 + 文字整体水平居中。设计稿 Icon size 14、图标与文字 gap 6。
            const int kIconSize = 14;
            const int kIconGap  = 6;
            const int textW = MeasureTextWidth(hdc, (LPCWSTR)GetText(),
                                               kTextSize, true);
            const int blockW = kIconSize + kIconGap + textW;
            int bx = x + (w - blockW) / 2;
            if (bx < x)
            {
                bx = x;
            }
            const int iconY = y + (h - kIconSize) / 2;
            DrawContactIcon(g, m_iconKind,
                            (Gdiplus::REAL)bx, (Gdiplus::REAL)iconY,
                            (Gdiplus::REAL)kIconSize,
                            Gdiplus::Color(255,
                                GetRValue(textColor), GetGValue(textColor),
                                GetBValue(textColor)),
                            1.7f);

            RECT textRc = { bx + kIconSize + kIconGap, y, x + w, y + h };
            DrawGdiText(hdc, (LPCWSTR)GetText(), textRc, kTextSize, true,
                        textColor,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        else
        {
            DrawGdiText(hdc, (LPCWSTR)GetText(), m_rcItem, kTextSize, true,
                        textColor,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    bool m_primary;    // true=accent 主按钮，false=ghost 按钮
    int  m_iconKind;   // 文字左侧要画的图标类型（ContactIconKind）；kNoIcon 表示不画
};

// 把 <contact-actionbtn> 的 icon 属性翻译成 ContactIconKind。放在类外，因为它
// 要用到 ContactActionButton::kNoIcon。
//   icon：属性原值（"chat" / "phone" / "video" / "history" / "locate"）；
//         空串与一切无法识别的值都按"不画图标"处理。
//   返回：对应的 ContactIconKind；不画图标时返回 ContactActionButton::kNoIcon。
int ContactIconKindFromAttr(const std::string& icon)
{
    if (icon == "chat")
    {
        return kContactIconChat;
    }
    if (icon == "phone")
    {
        return kContactIconPhone;
    }
    if (icon == "video")
    {
        return kContactIconVideo;
    }
    if (icon == "history")
    {
        return kContactIconHistory;
    }
    if (icon == "locate")
    {
        return kContactIconLocate;
    }
    return ContactActionButton::kNoIcon;
}

// ContactTagFlow::MeasureChildWidth —— 放在类外，因为它要用到 TagChip /
// ContactAddTagButton / ContactTagLimitHint 的完整定义。
int ContactTagFlow::MeasureChildWidth(DuiControl* c, HDC hdcMeasure) const
{
    // 标签胶囊：按其文字算理想宽度。
    TagChip* chip = dynamic_cast<TagChip*>(c);
    if (chip != NULL)
    {
        return chip->CalcDesiredWidth(hdcMeasure);
    }
    // 「+ 添加标签」按钮：固定宽度。
    ContactAddTagButton* addBtn = dynamic_cast<ContactAddTagButton*>(c);
    if (addBtn != NULL)
    {
        return ContactAddTagButton::kFixedWidth;
    }
    // "标签已达上限"说明文字：按文字宽度。
    ContactTagLimitHint* hint = dynamic_cast<ContactTagLimitHint*>(c);
    if (hint != NULL)
    {
        return hint->CalcDesiredWidth(hdcMeasure);
    }
    // 其它控件：回退到 GetDesiredSize。
    SIZE sz = c->GetDesiredSize();
    return (sz.cx > 0) ? sz.cx : 60;
}

// ContactTagFlow::CountRows —— 同样放在类外，它要经 MeasureChildWidth 用到上面
// 那几个控件的完整定义。
int ContactTagFlow::CountRows(int availWidth) const
{
    if (availWidth <= 0)
    {
        return 1;
    }
    HDC hdcMeasure = ::GetDC(NULL);
    int rows = 1;
    int curX = 0;
    for (const auto& child : m_children)
    {
        DuiControl* c = child.get();
        if (c == NULL || !c->IsVisible())
        {
            continue;
        }
        const int childW = MeasureChildWidth(c, hdcMeasure);
        // 本行放不下（且不是行首）→ 换行。与 Layout 里的换行判定必须完全一致，
        // 否则算出来的行数与实际排布对不上，窗口高度就会差一行。
        if (curX > 0 && curX + childW > availWidth)
        {
            ++rows;
            curX = 0;
        }
        curX += childW + m_gap;
    }
    if (hdcMeasure != NULL)
    {
        ::ReleaseDC(NULL, hdcMeasure);
    }
    return rows;
}

// =====================================================================
// 内嵌 XML 布局
// =====================================================================
//
// contact-root（白底 vbox，padding 20,18,20,18）自上而下：
//   · 头部行（头像 + 多行资料块），fixedHeight 84；
//   · 部门路径整宽区块（fixedHeight 先写 0，真实高度运行期由
//     ConfigureContactDeptPath 按折行行数改写；没有部门时保持 0 且不可见）；
//   · 头部下边距 spacer（14，对应设计稿 header paddingBottom）；
//   · 快捷操作行（语音通话 / 视频通话 / 历史消息 / 组织架构四个等宽 ghost
//     按钮），fixedHeight 36。四个按钮同时写了 fixedWidth 134 与 weight 1：
//     DuiHBox 排版时，带 weight 的子控件仍以自身期望宽度打底，再去分剩余空间
//     （见 DuiLayout.cpp 的 HintFixedMainPx）。只写 weight 的话，四个按钮按
//     文字量算出的期望宽度加起来超过可用宽度，剩余空间为 0，最后一个按钮会被
//     挤出客户区右边界。134 的来历：客户区名义宽 600 减左右内边距各 20 得 560，
//     减去三条 8 像素间隔得 536，四等分即 134（2026-10-04 加宽之前是 420 宽、每个 89）；
//   · 快捷操作行下边距 spacer（14）；
//   · 分隔线；
//   · 「个人资料」区块与它下方的分隔线（2026-10-04）：XML 里写的是占位标记
//     <contact-profile-block/>，BuildContactBody 解析前把它替换成 BuildContactProfileBlockXml
//     生成的片段。出场时高度为 0、不可见，由 ConfigureContactProfile 按字段灌数据并改写高度；
//   · 标签区（小标题 + 装着流式胶囊容器的滚动视口）。它的 fixedHeight 写的是
//     "标签只有一行"时的高度，真实高度在运行期由 SyncContactTagArea 按行数改写；
//   · 分隔线；
//   · 备注名 + 分组两行 FieldRow；
//   · 分隔线；
//   · 消息提醒（下拉框）+ 添加到收藏（开关）两行 FieldRow；
//   · 弹性占位 control（weight=1，把底部按钮推到底）；
//   · 底部分隔线 + 按钮行（关闭 / 发消息）；
//   · 复制成功的轻量提示（DuiToast）。放在<u>末尾</u>是因为基类按子控件顺序
//     绘制，末尾即最上层；fixedHeight 0 使它不占纵向空间，落点由它自己算。
//
// 说明：XML 的 margin 属性只取首个整数（balloonui builder 限制），无法
// 表达"单边 margin"，因此区块间的纵向间距一律用独立 spacer control 行
// 显式给出，不依赖 margin。
//
// 数字像素全部照 dialogs.jsx ContactInfoDialog；不出现裸魔数 —— XML 里
// 的尺寸是设计稿的"真值数据"，等同配置常量。
// =====================================================================
const char* const kContactBodyXml = u8R"xml(
<contact-root padding="20,18,20,18" gap="0">

  <hbox fixedHeight="84" gap="14">
    <contact-avatar id="1000" name="江" shape="round-rect" corner-radius="9"
                    fallback-bg-color="59,130,246" initials-color="255,255,255"
                    fixedWidth="64" fixedHeight="64" alignCross="center"/>
    <contact-header id="1001" weight="1"/>
  </hbox>

  <contact-deptpath id="1002" fixedHeight="0"/>

  <control fixedHeight="14"/>

  <hbox id="1046" fixedHeight="36" gap="8">
    <contact-actionbtn id="1042" text="语音通话" primary="false" icon="phone"
                       weight="1" fixedWidth="134" fixedHeight="36"/>
    <contact-actionbtn id="1043" text="视频通话" primary="false" icon="video"
                       weight="1" fixedWidth="134" fixedHeight="36"/>
    <contact-actionbtn id="1044" text="历史消息" primary="false" icon="history"
                       weight="1" fixedWidth="134" fixedHeight="36"/>
    <contact-actionbtn id="1045" text="组织架构" primary="false" icon="locate"
                       weight="1" fixedWidth="134" fixedHeight="36"/>
  </hbox>

  <control fixedHeight="14"/>
  <separator fixedHeight="1" color="237,237,237"/>

  <contact-profile-block/>

  <vbox id="1012" fixedHeight="76" padding="0,14,0,14" gap="8">
    <contact-text text="标签" size="11" color="107,107,107" fixedHeight="16"/>
    <contact-tagscroll id="1013" weight="1" gap="6" row-height="24"/>
  </vbox>

  <separator id="1015" fixedHeight="1" color="237,237,237"/>

  <vbox id="1016" fixedHeight="132" padding="0,14,0,14" gap="0">
    <field-row fixedHeight="52" divider="false" gap="16">
      <contact-text text="备注名" size="14" color="26,26,26" weight="1"/>
      <contact-valuetext id="1020" text="" size="13"
                         color="26,26,26" align="right"
                         fixedWidth="150" alignCross="center"/>
      <edit id="1022" fixedWidth="140" fixedHeight="28" alignCross="center"/>
      <contact-iconbtn id="1021" fixedWidth="22" fixedHeight="22"
                       alignCross="center"/>
    </field-row>
    <field-row fixedHeight="52" divider="false" gap="16">
      <contact-text text="分组" size="14" color="26,26,26" weight="1"/>
      <combobox id="1023" fixedWidth="208" fixedHeight="28"
                alignCross="center"/>
    </field-row>
  </vbox>

  <separator id="1017" fixedHeight="1" color="237,237,237"/>

  <vbox id="1018" fixedHeight="118" padding="0,14,0,0" gap="0">
    <field-row fixedHeight="52" divider="true" gap="16">
      <contact-text text="消息提醒" size="14" color="26,26,26" weight="1"/>
      <contact-notifycombo id="1035" fixedWidth="208" fixedHeight="28"
                           alignCross="center"/>
    </field-row>
    <field-row fixedHeight="52" divider="false" gap="16">
      <contact-text text="添加到收藏" size="14" color="26,26,26" weight="1"/>
      <switch id="1033" fixedWidth="36" fixedHeight="20" alignCross="center"
              on-color="255,122,69" off-color="216,213,208"/>
    </field-row>
  </vbox>

  <control weight="1"/>

  <separator fixedHeight="1" color="237,237,237"/>

  <hbox fixedHeight="58" padding="0,12,0,12" gap="8">
    <control weight="1"/>
    <contact-actionbtn id="1040" text="关闭"   primary="false"
                       fixedWidth="76" fixedHeight="34" alignCross="center"/>
    <contact-actionbtn id="1041" text="发消息" primary="true" icon="chat"
                       fixedWidth="104" fixedHeight="34" alignCross="center"/>
  </hbox>

  <contact-toast id="1003" fixedHeight="0"/>

</contact-root>
)xml";

// =====================================================================
// 「个人资料」区块的 XML 生成
// =====================================================================

// 内嵌 XML 里「个人资料」区块的占位标记，BuildContactBody 解析前替换成生成的片段。
const char* const kContactProfileBlockMarker = "<contact-profile-block/>";

// 生成一个格子槽位的 XML：字段名与编辑按钮一行，值一行。
//   slot：槽位，取值 [0, kContactProfileSlotCount)。
//   fullWidth：true 时占满整行（昵称、个性签名），false 时按两列之一的固定宽度。
std::string BuildContactProfileCellXml(int slot, bool fullWidth)
{
    std::string xml;
    xml += "        <vbox id=\"" + std::to_string((unsigned)ContactProfileCtrlId(slot, kProfilePartCell)) + "\"";
    if (fullWidth)
    {
        xml += " weight=\"1\"";
    }
    else
    {
        xml += " fixedWidth=\"" + std::to_string(kProfileColumnWidth) + "\"";
    }
    xml += " gap=\"" + std::to_string(kProfileCellInnerGap) + "\">\n";
    xml += "          <hbox fixedHeight=\"" + std::to_string(kProfileLabelHeight) + "\" gap=\"4\">\n";
    xml += "            <contact-text id=\"" + std::to_string((unsigned)ContactProfileCtrlId(slot, kProfilePartLabel))
         + "\" text=\"\" size=\"12\" color=\"107,107,107\" weight=\"1\"/>\n";
    // 公开范围开关：出场时宽 0、不可见，只有看本人时的手机号、座机两格由 FillContactProfileSlot 设宽并显示
    xml += "            <label id=\"" + std::to_string((unsigned)ContactProfileCtrlId(slot, kProfilePartVisibility))
         + "\" fixedWidth=\"0\"/>\n";
    xml += "            <contact-iconbtn id=\"" + std::to_string((unsigned)ContactProfileCtrlId(slot, kProfilePartPencil))
         + "\" fixedWidth=\"" + std::to_string(kProfilePencilSize) + "\" fixedHeight=\""
         + std::to_string(kProfilePencilSize) + "\" alignCross=\"center\"/>\n";
    xml += "          </hbox>\n";
    xml += "          <contact-valuetext id=\"" + std::to_string((unsigned)ContactProfileCtrlId(slot, kProfilePartValue))
         + "\" text=\"\" size=\"13\" color=\"26,26,26\" fixedHeight=\"" + std::to_string(ContactProfileValueHeight()) + "\"/>\n";
    // 就地编辑用的输入框与性别下拉框，与值占同一个位置、同样高（ContactProfileValueHeight，按字体算），出场时隐藏（BuildContactBody）
    xml += "          <edit id=\"" + std::to_string((unsigned)ContactProfileCtrlId(slot, kProfilePartEdit))
         + "\" fixedHeight=\"" + std::to_string(ContactProfileValueHeight()) + "\"/>\n";
    xml += "          <combobox id=\"" + std::to_string((unsigned)ContactProfileCtrlId(slot, kProfilePartCombo))
         + "\" fixedHeight=\"" + std::to_string(ContactProfileValueHeight()) + "\"/>\n";
    xml += "        </vbox>\n";
    return xml;
}

// 生成「个人资料」区块与它下方分隔线的 XML。格子较多、结构重复，故用代码生成而不是逐个手写；
// 出场时区块与分隔线的高度都是 0，由 ConfigureContactProfile 改写。
std::string BuildContactProfileBlockXml()
{
    std::string xml;
    xml += "  <vbox id=\"" + std::to_string((int)kIdProfileBlock) + "\" fixedHeight=\"0\" padding=\"0,"
         + std::to_string(kProfileBlockPaddingV) + ",0," + std::to_string(kProfileBlockPaddingV)
         + "\" gap=\"" + std::to_string(kProfileTitleGap) + "\">\n";
    // 小标题行：「个人资料」+ 右侧的「展开全部 / 收起」链接（文字由 ConfigureContactProfile 设置）
    xml += "    <hbox fixedHeight=\"" + std::to_string(kProfileTitleHeight) + "\" gap=\"8\">\n";
    xml += "      <contact-text text=\"";
    xml += TR_NOOP(u8"个人资料");
    xml += "\" size=\"11\" color=\"107,107,107\" weight=\"1\"/>\n";
    xml += "      <label id=\"" + std::to_string((int)kIdProfileToggle) + "\" fixedWidth=\""
         + std::to_string(kProfileToggleWidth) + "\"/>\n";
    xml += "    </hbox>\n";
    // 各行：0 行放昵称（0 号槽位）、1 行放个性签名（1 号槽位），各占整行；其后每行两个槽位
    xml += "    <vbox weight=\"1\" gap=\"" + std::to_string(kProfileRowGap) + "\">\n";
    for (int row = 0; row < kContactProfileRowSlots; ++row)
    {
        xml += "      <hbox id=\"" + std::to_string((int)kIdProfileRowBase + row) + "\" fixedHeight=\""
             + std::to_string(ContactProfileRowHeight()) + "\" gap=\"" + std::to_string(kProfileColumnGap) + "\">\n";
        int firstSlot = 0;
        int lastSlot = 0;
        ContactProfileRowSlotRange(row, firstSlot, lastSlot);
        const bool fullWidth = (firstSlot == lastSlot && firstSlot < kContactProfileFirstGridSlot);
        for (int slot = firstSlot; slot <= lastSlot; ++slot)
        {
            xml += BuildContactProfileCellXml(slot, fullWidth);
        }
        xml += "      </hbox>\n";
    }
    xml += "    </vbox>\n";
    xml += "  </vbox>\n";
    xml += "  <separator id=\"" + std::to_string((int)kIdProfileSeparator)
         + "\" fixedHeight=\"0\" color=\"237,237,237\"/>\n";
    return xml;
}

// 「个人资料」区块里哪些字段的值可能很长、需要悬停显示全文（单行显示，超出部分以省略号结尾）
bool IsLongProfileField(int field)
{
    return field == kProfileFieldNickname || field == kProfileFieldSignature || field == kProfileFieldAddress
        || field == kProfileFieldPost || field == kProfileFieldMail;
}

// 把一个格子的数据灌进 slot 号槽位；cell 为 NULL 表示该槽位空着（整格不可见）。
void FillContactProfileSlot(DuiControl* root, int slot, const ContactProfileCell* cell)
{
    DuiControl* cellCtrl = root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartCell));
    if (cellCtrl == NULL)
    {
        return;
    }
    cellCtrl->SetVisible(cell != NULL);
    ContactText* value = dynamic_cast<ContactText*>(root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartValue)));
    if (value != NULL)
    {
        // 先撤掉上一次注册的悬停全文：槽位换了字段或字段被清空时不能留着旧文字
        DuiToolTipMgr::Inst().Unregister(value);
    }
    if (cell == NULL)
    {
        return;
    }
    ContactText* label = dynamic_cast<ContactText*>(root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartLabel)));
    if (label != NULL)
    {
        label->SetTextContent(cell->m_label);
    }
    if (value != NULL)
    {
        value->SetTextContent(cell->m_value);
        value->SetTextColorValue(cell->m_isEmpty ? kColorTextFaint : kColorText);
        if (!cell->m_isEmpty && IsLongProfileField(cell->m_field))
        {
            DuiToolTipMgr::Inst().Register(value, cell->m_value);
        }
    }
    DuiControl* pencil = root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartPencil));
    if (pencil != NULL)
    {
        pencil->SetVisible(cell->m_editable);
    }
    // 公开范围开关：只有看本人时的手机号、座机两格有。宽度按两种文字中较宽的一种设定，切换取值时不变
    DuiLabel* visibility = dynamic_cast<DuiLabel*>(
        root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartVisibility)));
    if (visibility != NULL)
    {
        visibility->SetVisible(cell->m_hasVisibility);
        if (cell->m_hasVisibility)
        {
            visibility->SetText(profilefields::VisibilityLinkText(cell->m_visibility).c_str());
            balloonwjui::DuiLayout* labelRow = dynamic_cast<balloonwjui::DuiLayout*>(visibility->GetParent());
            if (labelRow != NULL)
            {
                balloonwjui::DuiLayout::Hint hint = labelRow->GetHint(visibility);
                hint.Fixed(ProfileVisibilityLinkWidth(visibility));
                labelRow->SetHint(visibility, hint);
            }
        }
    }
    // 重新灌数据时一律退出就地编辑：值可见，输入框与下拉框隐藏
    if (value != NULL)
    {
        value->SetVisible(true);
    }
    DuiControl* edit = root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartEdit));
    if (edit != NULL)
    {
        edit->SetVisible(false);
    }
    DuiControl* combo = root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartCombo));
    if (combo != NULL)
    {
        combo->SetVisible(false);
    }
}

// =====================================================================
// 自定义标签工厂
// =====================================================================
//
// 接 contact-root / contact-text / contact-header / contact-deptpath /
// contact-toast / contact-tagscroll / contact-valuetext / contact-avatar /
// contact-iconbtn / field-row / contact-actionbtn / combobox / contact-notifycombo /
// control；
// 其余标签（vbox / hbox / edit / switch / separator / label）走 builder
// 内置处理。头部头像 2026-10-07 起改用 contact-avatar（可点击的 DuiAvatar 子类）。
std::unique_ptr<DuiControl> CreateContactCustomControl(
    const DuiXmlBuilder::Node& n)
{
    if (n.tag == "contact-root")
    {
        return std::unique_ptr<DuiControl>(new ContactRootPanel());
    }

    if (n.tag == "contact-text" || n.tag == "contact-valuetext")
    {
        std::unique_ptr<ContactText> text(new ContactText());
        const CString     strText = Utf8ToCString(GetNodeAttr(n, "text"));
        const std::string strSize = GetNodeAttr(n, "size", "13");
        const std::string strColor = GetNodeAttr(n, "color", "26,26,26");

        const bool bold = (GetNodeAttr(n, "bold") == "true");
        const UINT align = TextAlignFromAttr(GetNodeAttr(n, "align"));
        text->Configure(strText, atoi(strSize.c_str()),
                        ParseRgbAttr(strColor, kColorText), bold, align);
        return std::unique_ptr<DuiControl>(text.release());
    }

    if (n.tag == "contact-avatar")
    {
        // 头部头像：DuiAvatar 的子类（见 ContactAvatar）。自定义标签不经 builder 内置的 avatar 解析，
        // 这里按同名属性就地设置；状态点不在 XML 里写，由 ApplyContactAvatar 按模型设置
        std::unique_ptr<ContactAvatar> avatar(new ContactAvatar());
        avatar->SetName(Utf8ToCString(GetNodeAttr(n, "name")));
        avatar->SetShape(GetNodeAttr(n, "shape") == "round-rect" ? balloonwjui::DuiAvatar::ShapeRoundRect
                                                                 : balloonwjui::DuiAvatar::ShapeCircle);
        avatar->SetCornerRadius(atoi(GetNodeAttr(n, "corner-radius", "8").c_str()));
        avatar->SetFallbackBgColor(ParseRgbAttr(GetNodeAttr(n, "fallback-bg-color"), kContactAvatarBgColor));
        avatar->SetInitialsColor(ParseRgbAttr(GetNodeAttr(n, "initials-color"), kColorWhite));
        return std::unique_ptr<DuiControl>(avatar.release());
    }

    if (n.tag == "contact-header")
    {
        return std::unique_ptr<DuiControl>(new ContactHeaderInfo());
    }

    if (n.tag == "contact-deptpath")
    {
        return std::unique_ptr<DuiControl>(new ContactDeptPath());
    }

    if (n.tag == "contact-toast")
    {
        // 复制成功的轻量提示。深底白字是 DuiToast 的默认视觉，这里只把显示
        // 时长调短一些 —— "已复制 xxx"是一句看完即走的话，默认 3 秒偏长。
        std::unique_ptr<DuiToast> toast(new DuiToast());
        toast->SetDurationMs(kCopyToastDurationMs);
        return std::unique_ptr<DuiControl>(toast.release());
    }

    if (n.tag == "contact-tagscroll")
    {
        // 标签区滚动视口：balloonui 的 DuiScrollView 装一个 ContactTagFlow 作内容，
        // 内置的 auto-hide 滚动条由它自己负责（UI 约定第 1 条「复用 balloonui 的
        // 滚动控件时沿用其内置行为」）。标签行数最多把窗口撑高到
        // kContactTagMaxRows 行，再多的行就靠滚动看。
        //
        // 关掉自动测高：DuiScrollView 的自动测高走 GetDesiredSize()，而流式容器
        // 的高度取决于"按多宽的可用宽度换行"，光靠 GetDesiredSize() 传不进这个
        // 宽度。内容总高度改由 SyncContactTagArea 在每次增删标签后显式同步。
        std::unique_ptr<ContactTagScrollView> scroll(new ContactTagScrollView());
        std::unique_ptr<ContactTagFlow> flow(new ContactTagFlow());
        flow->SetCtrlId((UINT)kIdTagFlow);
        const std::string strGap = GetNodeAttr(n, "gap", "6");
        const std::string strRow = GetNodeAttr(n, "row-height", "24");
        flow->SetGap(atoi(strGap.c_str()));
        flow->SetRowHeight(atoi(strRow.c_str()));
        scroll->SetContent(std::unique_ptr<DuiControl>(flow.release()));
        scroll->SetAutoContentHeight(false);
        // 滚动条用 balloonui 统一的悬浮式细滑块与默认命中带宽（2026-10-04 起不再单独设宽）
        return std::unique_ptr<DuiControl>(scroll.release());
    }

    if (n.tag == "contact-iconbtn")
    {
        std::unique_ptr<ContactIconButton> btn(new ContactIconButton());
        btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        return std::unique_ptr<DuiControl>(btn.release());
    }

    if (n.tag == "field-row")
    {
        std::unique_ptr<ContactFieldRow> row(new ContactFieldRow());
        row->SetDivider(GetNodeAttr(n, "divider", "true") != "false");
        const std::string strGap = GetNodeAttr(n, "gap", "0");
        row->SetGap(atoi(strGap.c_str()));
        return std::unique_ptr<DuiControl>(row.release());
    }

    if (n.tag == "contact-actionbtn")
    {
        std::unique_ptr<ContactActionButton> btn(new ContactActionButton());
        const CString btnText = Utf8ToCString(GetNodeAttr(n, "text"));
        btn->SetText(btnText);
        btn->SetPrimary(GetNodeAttr(n, "primary") == "true");
        btn->SetIconKind(ContactIconKindFromAttr(GetNodeAttr(n, "icon")));
        return std::unique_ptr<DuiControl>(btn.release());
    }

    if (n.tag == "combobox")
    {
        // DuiComboBox 暂未原生支持 XML —— 在此就地构造。选项 / 选中项由
        // 窗口在拿到 ContactModel 后填入；这里只造空 combo。
        std::unique_ptr<DuiComboBox> combo(new DuiComboBox());
        combo->SetEditable(false);          // read-only 下拉
        return std::unique_ptr<DuiControl>(combo.release());
    }

    if (n.tag == "contact-notifycombo")
    {
        // 「消息提醒」下拉框：选项固定为单聊可用的四种提醒模式（没有屏蔽消息），顺序与
        // 文字取自 NotifyModeMenu，与右键菜单的「消息提醒设置」二级菜单一致。选中项由
        // 窗口按 ContactModel::NotifyMode 置位。
        std::unique_ptr<DuiComboBox> combo(new DuiComboBox());
        combo->SetEditable(false);
        const std::vector<int> modes = balloonmain::GetNotifyModeChoices(/*isGroup*/false);
        for (size_t i = 0; i < modes.size(); ++i)
        {
            combo->AddString(balloonmain::GetNotifyModeLabel(modes[i]));
        }
        return std::unique_ptr<DuiControl>(combo.release());
    }

    if (n.tag == "control")
    {
        return std::unique_ptr<DuiControl>(new DuiControl());
    }

    return nullptr;
}

} // anonymous namespace

// =====================================================================
// 对外接口
// =====================================================================

std::unique_ptr<DuiControl> BuildContactBody()
{
    // 先把「个人资料」区块的占位标记换成生成的片段
    std::string xml = kContactBodyXml;
    const size_t marker = xml.find(kContactProfileBlockMarker);
    if (marker != std::string::npos)
    {
        xml.replace(marker, strlen(kContactProfileBlockMarker), BuildContactProfileBlockXml());
    }
    // 内嵌 XML 里的界面文字先经 TranslateXml 换成当前语言的译文；FromString 在调用内解析完毕，临时串的生命周期足够
    std::unique_ptr<DuiControl> root = DuiXmlBuilder::FromString(i18n::TranslateXml(xml.c_str()).c_str(),
                                                                 &CreateContactCustomControl);
    if (root)
    {
        // 「展开全部 / 收起」链接：主题色文字、靠右，点击发 DUIN_CLICK（窗口按 kIdProfileToggle 处理）；
        // 出场时不可见，由 ConfigureContactProfile 按行数决定显隐与文字
        DuiLabel* toggle = dynamic_cast<DuiLabel*>(root->FindCtrlById((UINT)kIdProfileToggle));
        if (toggle != NULL)
        {
            toggle->SetMode(DuiLabel::ModeLink);
            toggle->SetLinkColor(kColorAccent);
            toggle->SetHoverColor(kColorAccent);
            toggle->SetVisitedColor(kColorAccent);
            toggle->SetTextAlign(DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            toggle->SetTextPointSize(kProfileToggleFontPt, false);
            toggle->SetVisible(false);
        }
        // 各格子的就地编辑输入框与性别下拉框：画边框、限制长度、填好选项，出场时隐藏
        for (int slot = 0; slot < kContactProfileSlotCount; ++slot)
        {
            // 公开范围开关：与「展开全部」同样的主题色链接、靠右，点击发 DUIN_CLICK（窗口按部件
            // kProfilePartVisibility 处理）；出场时不可见，由 FillContactProfileSlot 决定显隐与文字
            DuiLabel* visibility = dynamic_cast<DuiLabel*>(
                root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartVisibility)));
            if (visibility != NULL)
            {
                visibility->SetMode(DuiLabel::ModeLink);
                visibility->SetLinkColor(kColorAccent);
                visibility->SetHoverColor(kColorAccent);
                visibility->SetVisitedColor(kColorAccent);
                visibility->SetTextAlign(DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                visibility->SetTextPointSize(kProfileVisibilityFontPt, false);
                visibility->SetVisible(false);
            }
            balloonwjui::DuiEdit* edit = dynamic_cast<balloonwjui::DuiEdit*>(
                root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartEdit)));
            if (edit != NULL)
            {
                edit->SetShowBorder(true);
                edit->SetMaxLength(profilefields::kProfileLongTextMaxChars);
                edit->SetVisible(false);
            }
            DuiComboBox* combo = dynamic_cast<DuiComboBox*>(
                root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartCombo)));
            if (combo != NULL)
            {
                for (int i = 0; i < profilefields::kGenderComboCount; ++i)
                {
                    combo->AddString(profilefields::GenderText(profilefields::ComboIndexToGender(i)));
                }
                combo->SetVisible(false);
            }
        }
        // 个人资料区块与它下方的分隔线出场时不可见（高度已是 0）
        DuiControl* profileBlock = root->FindCtrlById((UINT)kIdProfileBlock);
        if (profileBlock != NULL)
        {
            profileBlock->SetVisible(false);
        }
        DuiControl* profileSeparator = root->FindCtrlById((UINT)kIdProfileSeparator);
        if (profileSeparator != NULL)
        {
            profileSeparator->SetVisible(false);
        }

        // 底部两个按钮按译文宽度只增不减地加宽（多语言第 4 期，简体中文不变）：
        // 按钮文字 14 像素半粗体（ContactActionButton::OnPaint）；左右各留 12 像素，「发消息」另有 14 像素图标与 6 像素间距
        const int kFooterTextPx = 14;
        const int kFooterPadPx = 24;
        const int kFooterIconPx = 14 + 6;
        i18n::GrowFixedWidthToText(root->FindCtrlById(kIdBtnClose), TR(L"关闭"), kFooterTextPx, true, kFooterPadPx);
        i18n::GrowFixedWidthToText(root->FindCtrlById(kIdBtnSend), TR(L"发消息"), kFooterTextPx, true,
                                   kFooterPadPx + kFooterIconPx);
    }
    return root;
}

std::unique_ptr<DuiControl> CreateTagChip(LPCTSTR text, UINT ctrlId)
{
    std::unique_ptr<TagChip> chip(new TagChip());
    chip->SetTagText(text);
    chip->SetCtrlId(ctrlId);
    return std::unique_ptr<DuiControl>(chip.release());
}

std::unique_ptr<DuiControl> CreateAddTagButton()
{
    std::unique_ptr<ContactAddTagButton> btn(new ContactAddTagButton());
    btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    btn->SetText(TR(L"+ 添加标签"));
    btn->SetCtrlId((UINT)kIdAddTagButton);
    return std::unique_ptr<DuiControl>(btn.release());
}

std::unique_ptr<DuiControl> CreateTagLimitHint()
{
    std::unique_ptr<ContactTagLimitHint> hint(new ContactTagLimitHint());
    // 文字里的条数由常量拼出来，改上限时不必再来这里改一遍文案。
    CString text;
    text.Format(TR(L"标签最多 %d 个"), kContactTagMaxCount);
    hint->SetHintText(text);
    hint->SetCtrlId((UINT)kIdTagLimitHint);
    return std::unique_ptr<DuiControl>(hint.release());
}

int SyncContactTagArea(DuiControl* root)
{
    if (root == NULL)
    {
        return 0;
    }

    ContactTagFlow* flow = dynamic_cast<ContactTagFlow*>(
        root->FindCtrlById((UINT)kIdTagFlow));
    if (flow == NULL)
    {
        return 0;
    }

    DuiScrollView* scroll = dynamic_cast<DuiScrollView*>(
        root->FindCtrlById((UINT)kIdTagScroll));

    // 可用宽度取滚动视口<u>实际</u>布局出来的宽度，而不是照客户区宽度算出来的
    // kContactTagFlowAvailWidth。2026-10-02 之前两者并不相等：窗口客户区比
    // kContactBodyWidth 宽出一圈系统边框，按常量算会多数出一行来，窗口就比该有的高度
    // 高一截；现在两者一致，仍以实测为准，免得日后窗口宽度再有变化时两处不同步。
    int measuredWidth = 0;
    if (scroll != NULL)
    {
        const RECT rcScroll = scroll->GetRect();
        measuredWidth = rcScroll.right - rcScroll.left;
    }

    // 但测量值不是什么时候都可信，只有落在 (0, kContactTagFlowMaxTrustedWidth]
    // 区间内才采用（判据见 ContactLayout.h 上该常量的注释）：
    //   · 0 表示控件树还没布局过（界面刚 BuildContactBody 出来）；
    //   · 大于上限表示控件树停在窗口的默认尺寸上 —— 窗口是先以 CWindow::rcDefault
    //     建出来、再 ResizeClient 到真实尺寸的，首次构建界面时量到的是一千三百多
    //     像素（实测 1382），据此数出来的行数会把十来个标签算成一行。
    // 两种情况都退回按对话框宽度算，它至多比真实可用宽度窄二十来像素，宁可多数出
    // 一行（界面高一点）也不能少数出行来（标签直接看不到、也滚不到）。
    const bool widthTrusted = (measuredWidth > 0)
                           && (measuredWidth <= kContactTagFlowMaxTrustedWidth);
    int contentWidth = widthTrusted ? measuredWidth : kContactTagFlowAvailWidth;

    // 按整宽数行数。2026-10-04 起滚动条为悬浮式、不占内容宽度，超过行数上限出现滚动条时
    // 内容宽度也不变，不必再按缩窄后的宽度重数一遍。
    const int rows = flow->CountRows(contentWidth);

    int contentHeight = 0;
    if (scroll != NULL)
    {
        contentHeight = flow->CalcDesiredHeight(contentWidth);
        scroll->SetContentHeight(contentHeight);
    }

    // 行数与滚动范围是按测量宽度推出来的，出问题时光看界面分不清是"量错了宽度"
    // 还是"排错了行"，故把测量值、是否采信、结果一并记下来。
    LOG_INFO("contact dialog: tag area synced, measured scroll width %d (%s), "
             "counted %d row(s) at width %d, content height %d.",
             measuredWidth, widthTrusted ? "trusted" : "untrusted, using dialog width",
             rows, contentWidth, contentHeight);

    // 可见行数：超过上限的部分不撑高窗口，交给滚动视口。
    int visibleRows = rows;
    if (visibleRows > kContactTagMaxRows)
    {
        visibleRows = kContactTagMaxRows;
    }
    if (visibleRows < 1)
    {
        visibleRows = 1;
    }

    // 把标签区整块的高度提示改写成可见行数对应的高度。父容器就是客户区根
    //（contact-root，DuiVBox），改 hint 即改这一块在纵向上占多少像素。
    DuiControl* block = root->FindCtrlById((UINT)kIdTagBlock);
    if (block != NULL)
    {
        balloonwjui::DuiLayout* parent =
            dynamic_cast<balloonwjui::DuiLayout*>(block->GetParent());
        if (parent != NULL)
        {
            balloonwjui::DuiLayout::Hint hint = parent->GetHint(block);
            hint.Fixed(ContactTagBlockHeight(visibleRows));
            parent->SetHint(block, hint);
        }
    }

    return visibleRows;
}

void ShowContactTipToast(DuiControl* root, LPCTSTR text)
{
    if (root == NULL || text == NULL || text[0] == _T('\0'))
    {
        return;
    }
    DuiToast* toast = dynamic_cast<DuiToast*>(
        root->FindCtrlById((UINT)kIdCopyToast));
    if (toast != NULL)
    {
        toast->Show(text);
    }
}

void ApplyContactAvatar(balloonwjui::DuiAvatar* avatar, const ContactModel& model)
{
    if (avatar == NULL)
    {
        return;
    }
    // 带头像来源时占位字形与底色取自来源（与主面板列表同一份解码结果），否则取姓名首字与固定底色
    const ContactAvatarSource& source = model.AvatarSource();
    avatar->SetName(source.m_valid ? (LPCTSTR)source.m_glyph : (LPCTSTR)model.AvatarLetter());

    // 模型状态映射到 DuiAvatar 的状态点。
    switch (model.Status())
    {
    // 在线：绿色状态点
    case kContactStatusOnline:
        avatar->SetStatus(balloonwjui::DuiAvatar::StatusOnline);
        break;
    // 离开：橙色状态点
    case kContactStatusAway:
        avatar->SetStatus(balloonwjui::DuiAvatar::StatusAway);
        break;
    // 忙碌：红色状态点
    case kContactStatusBusy:
        avatar->SetStatus(balloonwjui::DuiAvatar::StatusBusy);
        break;
    // 离线：灰色状态点
    case kContactStatusOffline:
        avatar->SetStatus(balloonwjui::DuiAvatar::StatusOffline);
        break;
    // 未登记的取值：不画状态点
    default:
        avatar->SetStatus(balloonwjui::DuiAvatar::StatusNone);
        break;
    }

    // 对方离线时头像画成灰度：字形头像只需把底色换成等亮度的灰色，字仍是白色
    const COLORREF bg = source.m_valid ? source.m_color : kContactAvatarBgColor;
    avatar->SetFallbackBgColor(model.IsAvatarGray() ? balloonmain::MakeGrayColor(bg) : bg);
}

void SetContactAvatarClickable(balloonwjui::DuiAvatar* avatar, bool clickable, LPCTSTR tooltip)
{
    ContactAvatar* contactAvatar = dynamic_cast<ContactAvatar*>(avatar);
    if (contactAvatar != NULL)
    {
        contactAvatar->SetClickable(clickable, tooltip);
    }
}

bool IsContactAvatarClickable(const balloonwjui::DuiAvatar* avatar)
{
    const ContactAvatar* contactAvatar = dynamic_cast<const ContactAvatar*>(avatar);
    return (contactAvatar != NULL) && contactAvatar->IsClickable();
}

void ConfigureContactHeader(DuiControl* root,
                            LPCTSTR primaryName, LPCTSTR parenText,
                            LPCTSTR title, LPCTSTR accountRegion,
                            LPCTSTR signature, LPCTSTR account)
{
    if (root == NULL)
    {
        return;
    }
    ContactHeaderInfo* header = dynamic_cast<ContactHeaderInfo*>(
        root->FindCtrlById((UINT)kIdHeaderInfo));
    if (header != NULL)
    {
        header->Configure(primaryName, parenText, title,
                          accountRegion, signature, account);
    }
}

int ConfigureContactProfile(DuiControl* root, const std::vector<ContactProfileCell>& cells,
                            bool collapsible, bool expanded, std::vector<int>& outSlotFields)
{
    outSlotFields.assign(kContactProfileSlotCount, -1);
    if (root == NULL)
    {
        return 0;
    }
    DuiControl* block = root->FindCtrlById((UINT)kIdProfileBlock);
    if (block == NULL)
    {
        return 0;
    }

    // 格子分配到槽位：昵称进 0 号槽位、个性签名进 1 号槽位，其余按顺序进 2～8 号槽位
    std::vector<const ContactProfileCell*> slotCells(kContactProfileSlotCount, (const ContactProfileCell*)NULL);
    const int kNicknameSlot = 0;
    const int kSignatureSlot = 1;
    int nextGridSlot = kContactProfileFirstGridSlot;
    for (size_t i = 0; i < cells.size(); ++i)
    {
        if (cells[i].m_field == kProfileFieldNickname)
        {
            slotCells[kNicknameSlot] = &cells[i];
        }
        else if (cells[i].m_field == kProfileFieldSignature)
        {
            slotCells[kSignatureSlot] = &cells[i];
        }
        else if (nextGridSlot < kContactProfileSlotCount)
        {
            slotCells[nextGridSlot] = &cells[i];
            ++nextGridSlot;
        }
    }

    const int totalRows = ContactProfileRowCount(cells);
    const int shownRows = ContactProfileShownRows(totalRows, collapsible, expanded);

    // 逐行决定显隐：有格子的行按先后计数，超出 shownRows 的行收起
    int filledRows = 0;
    for (int row = 0; row < kContactProfileRowSlots; ++row)
    {
        int firstSlot = 0;
        int lastSlot = 0;
        ContactProfileRowSlotRange(row, firstSlot, lastSlot);
        const bool hasCell = (slotCells[firstSlot] != NULL);
        const bool show = hasCell && filledRows < shownRows;
        if (hasCell)
        {
            ++filledRows;
        }
        DuiControl* rowCtrl = root->FindCtrlById((UINT)(kIdProfileRowBase + row));
        if (rowCtrl != NULL)
        {
            rowCtrl->SetVisible(show);
        }
        for (int slot = firstSlot; slot <= lastSlot && slot < kContactProfileSlotCount; ++slot)
        {
            const ContactProfileCell* cell = show ? slotCells[slot] : NULL;
            FillContactProfileSlot(root, slot, cell);
            if (cell != NULL)
            {
                outSlotFields[slot] = cell->m_field;
            }
        }
    }

    // 「展开全部 / 收起」链接：只在可收起且行数多于默认行数时出现
    DuiLabel* toggle = dynamic_cast<DuiLabel*>(root->FindCtrlById((UINT)kIdProfileToggle));
    if (toggle != NULL)
    {
        const bool showToggle = collapsible && totalRows > kContactProfileCollapsedRows;
        toggle->SetVisible(showToggle);
        toggle->SetText(expanded ? TR(L"收起") : TR(L"展开全部"));
    }

    // 区块与分隔线的显隐及高度。分隔线另算 1 像素，ContactProfileBlockHeight 已把它算在内
    block->SetVisible(shownRows > 0);
    balloonwjui::DuiLayout* parent = dynamic_cast<balloonwjui::DuiLayout*>(block->GetParent());
    DuiControl* separator = root->FindCtrlById((UINT)kIdProfileSeparator);
    if (parent != NULL)
    {
        balloonwjui::DuiLayout::Hint hint = parent->GetHint(block);
        hint.Fixed(shownRows > 0 ? ContactProfileBlockHeight(shownRows) - kProfileSeparatorHeight : 0);
        parent->SetHint(block, hint);
        if (separator != NULL)
        {
            balloonwjui::DuiLayout::Hint sepHint = parent->GetHint(separator);
            sepHint.Fixed(shownRows > 0 ? kProfileSeparatorHeight : 0);
            parent->SetHint(separator, sepHint);
        }
    }
    if (separator != NULL)
    {
        separator->SetVisible(shownRows > 0);
    }
    return shownRows;
}

void SetContactProfileSlotEditing(DuiControl* root, int slot, int field, bool editing,
                                  LPCTSTR text, int gender)
{
    if (root == NULL || slot < 0 || slot >= kContactProfileSlotCount)
    {
        return;
    }
    DuiControl* value = root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartValue));
    DuiControl* pencil = root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartPencil));
    balloonwjui::DuiEdit* edit = dynamic_cast<balloonwjui::DuiEdit*>(
        root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartEdit)));
    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(root->FindCtrlById(ContactProfileCtrlId(slot, kProfilePartCombo)));
    const bool isGender = (field == kProfileFieldGender);

    if (value != NULL)
    {
        value->SetVisible(!editing);
    }
    if (pencil != NULL)
    {
        pencil->SetVisible(!editing);
    }
    if (edit != NULL)
    {
        if (editing && !isGender)
        {
            edit->SetText(text != NULL ? text : _T(""));
        }
        edit->SetVisible(editing && !isGender);
    }
    if (combo != NULL)
    {
        if (editing && isGender)
        {
            // 程序设置选中项，不触发 DUIN_VALUECHANGED（否则一进入编辑就被当成选好了）
            combo->SetCurSel(profilefields::GenderToComboIndex(gender), false);
        }
        combo->SetVisible(editing && isGender);
    }
}

int ProfileVisibilityLinkWidth(const DuiLabel* link)
{
    if (link == NULL)
    {
        return 0;
    }
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return kProfileVisibilityDefaultW;
    }
    // 用开关自己绘制时的字体量（SetTextPointSize 设的磅值、按控件所在窗口的 DPI），没设字体时用默认字体
    HFONT font = link->GetFont();
    if (font == NULL)
    {
        font = balloonwjui::DuiResMgr::Inst().GetDefaultFont();
    }
    HFONT oldFont = (font != NULL) ? (HFONT)::SelectObject(hdc, font) : NULL;
    int widest = 0;
    const int kValues[] = { profile_visibility_self_only, profile_visibility_colleagues };
    for (size_t i = 0; i < sizeof(kValues) / sizeof(kValues[0]); ++i)
    {
        const std::wstring text = profilefields::VisibilityLinkText(kValues[i]);
        SIZE size = { 0, 0 };
        if (::GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &size) && size.cx > widest)
        {
            widest = size.cx;
        }
    }
    if (oldFont != NULL)
    {
        ::SelectObject(hdc, oldFont);
    }
    ::ReleaseDC(NULL, hdc);
    return (widest > 0) ? widest + kProfileVisibilitySlackW : kProfileVisibilityDefaultW;
}

UINT TrackProfileVisibilityMenu(HWND owner, const RECT& anchorScreen, int current)
{
    balloonwjui::DuiMenu menu;
    const std::vector<profilefields::VisibilityMenuEntry> entries = profilefields::BuildVisibilityMenu(current);
    for (size_t i = 0; i < entries.size(); ++i)
    {
        menu.AppendChecked(entries[i].m_id, entries[i].m_label.c_str(), entries[i].m_checked);
    }
    // 落点超出工作区时由 DuiMenu 自己翻向上方 / 左方（UI 约定第 3 条），这里只给期望位置
    const UINT chosen = menu.TrackPopup(anchorScreen.left, anchorScreen.bottom + kProfileVisibilityMenuGap, owner);
    LOG_INFO("profile visibility menu: current=%d, chosen id=%u.", current, chosen);
    return chosen;
}

int ConfigureContactDeptPath(DuiControl* root, LPCTSTR deptPath)
{
    if (root == NULL)
    {
        return 0;
    }
    ContactDeptPath* block = dynamic_cast<ContactDeptPath*>(
        root->FindCtrlById((UINT)kIdDeptPath));
    if (block == NULL)
    {
        return 0;
    }

    const int lineCount = block->SetDeptPath(deptPath, kContactDeptPathAvailWidth);

    // 没有部门路径时整块不可见 —— 光把高度改成 0 还不够：DuiVBox 仍会把它当一个
    // 可见子控件参与 gap 计算，而且它还会照常参与命中测试。
    block->SetVisible(lineCount > 0);

    // 真实高度写回父容器的布局提示。父容器就是客户区根（contact-root），它是
    // DuiVBox，改 hint 即改这一块在纵向上占多少像素。
    balloonwjui::DuiLayout* parent =
        dynamic_cast<balloonwjui::DuiLayout*>(block->GetParent());
    if (parent != NULL)
    {
        balloonwjui::DuiLayout::Hint hint = parent->GetHint(block);
        hint.Fixed(ContactDeptBlockHeight(lineCount));
        parent->SetHint(block, hint);
    }
    return lineCount;
}

void SetContactText(DuiControl* root, UINT ctrlId, LPCTSTR text)
{
    if (root == NULL)
    {
        return;
    }
    ContactText* ct = dynamic_cast<ContactText*>(root->FindCtrlById(ctrlId));
    if (ct != NULL)
    {
        ct->SetTextContent(text);
    }
}

namespace {

// 自上而下，对 node 以及它所有可见的后代逐个按各自当前的矩形调用 ForceLayout。
//
// 只对根控件调一次 ForceLayout 是不够的：根控件排版时经 SetRect 摆放子容器，
// 子容器矩形没有变化时 SetRect 直接返回，更深层的容器便不会重新排版。只要某个
// 控件在隐藏期间经历过一次重排（容器排版时跳过不可见的子控件），它的矩形就已经
// 过时，重新显示后若所在容器没有重新排版，它就按过时的矩形绘制。2026-09-30 的
// 「备注名」文本框错位即由此而来：文本框位于根控件往下第三层，中间两层容器的
// 矩形都没有变化。
//
// 父控件先排版、子控件后排版，于是每一层拿到的都是父控件刚算出的矩形。不可见的
// 子树整棵跳过 —— 它的矩形本就无效，等它重新显示后由下一次重排算出。
void ForceLayoutVisibleSubtree(DuiControl* node)
{
    if (node == NULL || !node->IsVisible())
    {
        return;
    }
    node->ForceLayout(node->GetRect());
    const std::vector<std::unique_ptr<DuiControl>>& children = node->Children();
    for (size_t i = 0; i < children.size(); ++i)
    {
        ForceLayoutVisibleSubtree(children[i].get());
    }
}

} // anonymous namespace

void RelayoutContactBody(DuiControl* root)
{
    if (root == NULL)
    {
        return;
    }
    // 以根控件当前矩形强制重排整树 —— 标签流式容器据此重新换行排布，显隐刚发生
    // 变化的控件（如备注名的当前值、铅笔按钮与文本框）也据此拿到正确的矩形。
    ForceLayoutVisibleSubtree(root);
    root->Invalidate();
}

// =====================================================================
// 部门路径折行与高度 —— 纯函数，不碰控件与 GDI，单元测试直接调
// =====================================================================

void SplitDeptPathSegments(LPCTSTR deptPath, std::vector<CString>& outSegments)
{
    outSegments.clear();
    if (deptPath == NULL)
    {
        return;
    }

    const CString path(deptPath);
    const int length = path.GetLength();
    int start = 0;
    while (start < length)
    {
        int cut = path.Find(_T('/'), start);
        if (cut < 0)
        {
            cut = length;
        }
        CString segment = path.Mid(start, cut - start);
        segment.Trim();
        // 空段直接丢弃 —— 于是首尾多余的分隔符、连续的分隔符都不会产生空段。
        if (!segment.IsEmpty())
        {
            outSegments.push_back(segment);
        }
        start = cut + 1;
    }
}

int PackDeptPathLines(const std::vector<int>& segWidths, int sepWidth,
                      int availWidth, int maxLines,
                      std::vector<int>& outLineEndSeg)
{
    outLineEndSeg.clear();
    if (segWidths.empty())
    {
        return 0;
    }
    if (maxLines < 1)
    {
        maxLines = 1;
    }
    if (sepWidth < 0)
    {
        sepWidth = 0;
    }

    const int count    = (int)segWidths.size();
    int       lineStart = 0;    // 当前行的首段下标
    int       lineWidth = 0;    // 当前行已占宽度（像素）
    for (int i = 0; i < count; ++i)
    {
        const int segWidth = (segWidths[i] > 0) ? segWidths[i] : 0;

        // 行首那一段无条件放进本行：即便它自己就比整行还宽也得先放，
        // 否则可用宽度极小时永远换行、排不完（绘制层再按字符切开它）。
        if (i == lineStart)
        {
            lineWidth = segWidth;
            continue;
        }

        // 同一行内两段之间要插一个分隔符，故要连分隔符一起算得下才放得下。
        const int wanted = lineWidth + sepWidth + segWidth;
        if (wanted <= availWidth)
        {
            lineWidth = wanted;
            continue;
        }

        // 本行放不下这一段。已经排到允许的最后一行时不再换行 —— 剩下的段
        // 全部并入本行，由绘制层做中段省略收尾。
        if ((int)outLineEndSeg.size() + 1 >= maxLines)
        {
            break;
        }
        outLineEndSeg.push_back(i);
        lineStart = i;
        lineWidth = segWidth;
    }

    // 收尾：最后一行覆盖到末段。
    outLineEndSeg.push_back(count);
    return (int)outLineEndSeg.size();
}

int ContactDeptBlockHeight(int lineCount)
{
    if (lineCount <= 0)
    {
        return 0;
    }
    return kDeptPathTopGap + lineCount * kDeptPathLineHeight;
}

int ContactTagBlockHeight(int tagRowCount)
{
    int rows = tagRowCount;
    // 一个标签都没有时也要留出一行位置给「+ 添加标签」按钮，故下限是 1；
    // 上限是 kContactTagMaxRows，再多的行由滚动视口承接、不撑高窗口。
    if (rows < 1)
    {
        rows = 1;
    }
    if (rows > kContactTagMaxRows)
    {
        rows = kContactTagMaxRows;
    }
    return kTagBlockPaddingV + kTagTitleHeight + kTagTitleGap
         + rows * kTagRowHeight + (rows - 1) * kTagRowGap
         + kTagBlockPaddingV;
}

int ContactProfileValueHeightForDpi(int dpi)
{
    //就地编辑输入框（DuiEdit）只在「一行的高度 < 控件高 - 上下边框」时把单行文字垂直居中（DuiEdit::ApplyTextInsets），
    //否则从顶部排、超出部分在底部被裁掉。一行的高度按 DuiRichEdit::GetLineHeight 的算法：默认字体的 tmHeight 加行间距
    HFONT font = balloonwjui::DuiResMgr::Inst().GetDefaultFontForDpi(dpi);
    if (font == NULL)
    {
        return kProfileValueHeight;
    }
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return kProfileValueHeight;
    }
    HFONT old = (HFONT)::SelectObject(hdc, font);
    TEXTMETRICW tm;
    memset(&tm, 0, sizeof(tm));
    const BOOL ok = ::GetTextMetricsW(hdc, &tm);
    ::SelectObject(hdc, old);
    ::ReleaseDC(NULL, hdc);
    if (!ok || tm.tmHeight <= 0)
    {
        return kProfileValueHeight;
    }
    const int need = tm.tmHeight + tm.tmExternalLeading + 2 * (kProfileEditBorderPx + kProfileEditSlackPx);
    return (need > kProfileValueHeight) ? need : kProfileValueHeight;
}

int ContactProfileValueHeight()
{
    //balloonui 的字体按窗口所在显示器的 DPI 取，控件尺寸不随 DPI 缩放：按本机所有显示器中最高的 DPI 算，
    //窗口放在哪块显示器上都放得下（2026-10-07，用户在 125% 缩放的显示器上看到输入框里的文字下半截被裁掉）
    return ContactProfileValueHeightForDpi(i18n::MaxMonitorDpi());
}

int ContactProfileRowHeight()
{
    return kProfileLabelHeight + kProfileCellInnerGap + ContactProfileValueHeight();
}

int ContactProfileBlockHeight(int rowCount)
{
    if (rowCount <= 0)
    {
        return 0;
    }
    return kProfileBlockPaddingV + kProfileTitleHeight + kProfileTitleGap
         + rowCount * ContactProfileRowHeight() + (rowCount - 1) * kProfileRowGap
         + kProfileBlockPaddingV + kProfileSeparatorHeight;
}

int ContactProfileShownRows(int totalRows, bool collapsible, bool expanded)
{
    if (totalRows <= 0)
    {
        return 0;
    }
    if (collapsible && !expanded && totalRows > kContactProfileCollapsedRows)
    {
        return kContactProfileCollapsedRows;
    }
    return totalRows;
}

void ApplyContactSelfView(DuiControl* root, bool self)
{
    if (root == NULL)
    {
        return;
    }
    // 看本人时隐藏的控件：两个通话按钮、发消息按钮，以及标签 / 备注分组 / 提醒收藏三块与它们下方的分隔线
    const UINT kHiddenForSelf[] =
    {
        (UINT)kIdBtnVoice, (UINT)kIdBtnVideo, (UINT)kIdBtnSend,
        (UINT)kIdTagBlock, (UINT)kIdTagSeparator,
        (UINT)kIdRemarkSection, (UINT)kIdRemarkSeparator,
        (UINT)kIdNotifySection,
    };
    for (size_t i = 0; i < sizeof(kHiddenForSelf) / sizeof(kHiddenForSelf[0]); ++i)
    {
        DuiControl* c = root->FindCtrlById(kHiddenForSelf[i]);
        if (c != NULL)
        {
            c->SetVisible(!self);
        }
    }
}

int ContactBodyHeight(int deptLineCount, int tagRowCount, int profileRowCount, bool selfView)
{
    // kContactBodyBaseHeight 里已经含了"标签正好一行"的那一份高度，故这里加的是
    // 标签多出来那几行的<u>增量</u>，不能把整块高度再加一遍。
    const int tagExtra = ContactTagBlockHeight(tagRowCount) - kTagBlockOneRowHeight;

    // 快捷操作行本身，加上它<u>下方</u>那一个 spacer。上方那个 spacer 原先就在
    // （它是头部与分隔线之间的间距），已经算进 kContactBodyBaseHeight 里了，
    // 所以这里只加一份间距而不是两份。
    const int quickRowExtra = kContactQuickRowHeight + kContactQuickRowGap;

    const int common = kContactBodyBaseHeight + quickRowExtra
                     + ContactDeptBlockHeight(deptLineCount)
                     + ContactProfileBlockHeight(profileRowCount);
    if (selfView)
    {
        // 看本人：标签区（按一行计，基准高度里含的就是这一份）、备注名与分组、消息提醒与收藏三块
        // 连同两条分隔线都不显示，从基准高度里减掉；标签多出来的行也就不再计入
        const int hidden = kTagBlockOneRowHeight + kSectionSeparatorHeight
                         + kRemarkSectionHeight + kSectionSeparatorHeight
                         + kNotifySectionHeight;
        return common - hidden;
    }
    return common + tagExtra;
}

} // namespace ballooncontact
