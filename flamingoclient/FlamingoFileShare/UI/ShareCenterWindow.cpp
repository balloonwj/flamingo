/**
 *  「文件共享」窗口 CShareCenterWindow 的实现，见 ShareCenterWindow.h。
 *  balloonwj@qq.com   2026-10-02（第 17 步 2026-10-03 改为正式窗口）
 */
#include "stdafx.h"
#include "ShareCenterWindow.h"

#include <memory>

#include "../Engine/ShareBrowseModel.h"
#include "../Engine/ShareCenterModel.h"
#include "../Engine/ShareConversationModel.h"
#include "../Engine/ShareCreateModel.h"
#include "../Engine/ShareWinPath.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/Graphics/ShellTypeIcon.h"
#include "ShareLocalOpen.h"
#include "ShareWindowPlacement.h"   //窗口位置与大小的保存和恢复
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiListBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSearchBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"
#include "../../../third-party/balloonui/balloonui/DuiPaintAA.h"   //FillRoundRect：列表行的圆角底色与状态胶囊
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"   //详情小节标题下方的细分隔线
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiProgressBar.h"   //访问者行的传输进度条
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiToast.h"   //操作结果与出错的浮层提示
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   //最近操作文字的悬停提示
#include "../../Source/Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiButton;
using balloonwjui::DuiSearchBox;
using balloonwjui::DuiScrollView;
using balloonwjui::DuiVirtualList;

namespace fileshare {

namespace {

// ---- 尺寸（像素）----
const int kContentWidth     = 1080;  //标题栏以下内容区的默认宽度（整窗宽度与之相同）：左侧列表约 480 加右侧详情 560
const int kContentHeight    = 620;   //标题栏以下内容区的默认高度
const int kMinWindowWidth   = 820;   //整窗最小宽度：详情 560 之外左侧列表还留有 220 左右
const int kMinWindowHeight  = 420;   //整窗最小高度（含标题栏）
const int kTitleBarHeight   = 32;    //标题栏高，与客户端其余窗口一致
const int kPadding          = 12;    //四周内边距
const int kTopBarHeight     = 44;    //顶栏（胶囊页签、搜索框、「发起共享」「刷新」）的行高
const int kTopControlHeight = 30;    //顶栏里各控件的高度，在顶栏里垂直居中
const int kPillPadding      = 14;    //胶囊页签与顶栏按钮的文字左右两侧的内边距
const int kTopGap           = 8;     //顶栏里相邻控件之间的间距
const int kSearchWidth      = 220;   //顶栏搜索框的宽度
const int kCardRadius       = 8;     //左侧列表、右侧详情两张白色卡片的圆角半径
const int kListCardPadding  = 6;     //左侧列表卡片的内边距
const int kDetailCardPadding = 16;   //右侧详情卡片的内边距
const int kButtonWidth      = 72;    //「打开」等普通按钮宽
const int kWideButtonWidth  = 96;    //「在本机打开」「调整成员」「定位到会话」按钮宽
const int kGap              = 12;    //左侧列表与右侧详情之间的间距
const int kDetailWidth      = 560;   //右侧详情的宽度：最近操作一行（时间、操作人、动作、路径与结果）通常能完整显示
const int kDetailExtraDiv   = 2;     //窗口比默认宽度宽时，多出的宽度分给右侧详情的比例的分母（一半给详情，其余给列表）
const int kRowHeight        = 64;    //列表每行高（两行文字）
const int kRowInset         = 3;     //列表行的选中 / 悬停底色比行矩形四边各收进的距离
const int kRowRadius        = 6;     //列表行选中 / 悬停底色的圆角半径
const int kAccentBarWidth   = 3;     //选中行左侧蓝色竖条的宽度
const int kChipHeight       = 20;    //行内状态胶囊的高度
const int kChipPadding      = 8;     //状态胶囊文字两侧的内边距
const int kChipGap          = 6;     //两个胶囊之间、胶囊与名称之间的间距
const int kOwnerAvatarPx    = 16;    //「共享给我的」第二行行首分享者小头像的边长
const int kOwnerAvatarGap   = 6;     //分享者小头像与文字之间的间距
const int kEmptyIconSize    = 48;    //空列表提示的大图标边长
const int kEmptyLineH       = 24;    //空列表提示每行文字的行高
const int kEmptyTopDivisor  = 3;     //空列表提示放在列表高度的几分之一处（3 即三分之一，比正中略偏上）
const BYTE kOpaqueAlpha     = 255;   //AlphaBlend 的整体不透明度：按位图自带的透明通道贴，不再额外变淡
const int kRowIconSize      = 32;    //列表行首系统类型图标的边长
const int kRowIconGap       = 10;    //行首图标与文字之间的间距
const int kCellPadding      = 10;    //列表行内左右内边距
const int kLineHalf         = 2;     //行高的一半：两行文字各占一半
const int kActionRowH       = 34;    //操作按钮行高
const int kPersonRowH       = 40;    //详情里人的一行（成员、访问者、分享者）的行高：头像加上下留白，名字与说明各占一半
const int kDetailAvatarPx   = 28;    //详情里头像的边长（圆形）
const int kDetailAvatarGap  = 8;     //详情里头像与名字之间的间距
const int kDetailHeaderH    = 52;    //详情头部（图标、名称与路径、状态胶囊）的行高
const int kDetailIconPx     = 40;    //详情头部类型图标的边长
const int kDetailIconGap    = 12;    //详情头部图标与名称之间的间距
const int kIconCornerRadius = 1;     //类型图标所在头像控件的圆角半径：取 1 而不是 0（为 0 时位图画不出来，见 bugs.md BUG-70）
const int kDetailTitlePt    = 12;    //详情头部名称的字号（磅，加粗）
const int kDetailBlockGap   = 10;    //详情卡片里头部、操作按钮行、下面各小节之间的间距
const int kActionButtonH    = 30;    //详情操作按钮的高度（在按钮行里垂直居中）
const int kFieldH           = 26;    //详情信息行（与小节里的空提示）的行高
const int kFieldLabelW      = 64;    //详情信息行左列标签的最小宽度；实际宽度按字体量出（最宽的标签加 kColumnGap）
const int kColumnGap        = 12;    //详情里左列（标签、时间）文字与右列内容之间的最小间距
const int kSectionH         = 36;    //详情小节标题的行高（含下方细分隔线）
const int kSectionTitlePt   = 10;    //详情小节标题的字号（磅，加粗）
const int kSeparatorH       = 7;     //小节标题下方细分隔线所在的高度（线在其中垂直居中）
const int kPermChipH        = 18;    //成员权限小胶囊的高度
const int kPermChipPad      = 6;     //成员权限小胶囊文字两侧的内边距
const int kPermChipGap      = 4;     //相邻两个权限小胶囊之间的间距
const int kProgressW        = 72;    //访问者行右边传输进度条的宽度
const int kProgressH        = 6;     //传输进度条的高度
const int kActivityH        = 24;    //最近操作每行的行高
const int kActivityTimeW    = 118;   //最近操作左列时间的最小宽度（96 DPI 下放得下「2026-10-03 08:50」）；实际宽度按字体量出
const int kLinkPad          = 4;     //「定位到会话」链接文字右侧留出的余量
const int kPercentFull      = 100;   //传输进度条的满值（百分比）
const int kNoProgress       = -1;    //人的一行不画进度条
const int kToastMs          = 2000;  //普通浮层提示的显示时长（毫秒，不含淡入淡出）
const int kErrorToastMs     = 3000;  //出错浮层提示的显示时长（毫秒）：文字通常更长，多留一秒读完
const int kToastMaxWidth    = 560;   //浮层提示的最大宽度，单位：像素；更长的文字截断加省略号
const int kToastTop         = 64;    //浮层提示距内容区顶边的距离，单位：像素：四周内边距 12 + 顶栏 44 + 间距 8，正好在顶栏下方

//「正在访问的人」的刷新间隔与定时器编号
const UINT_PTR kVisitorTimerId = 1;
const UINT kVisitorTimerMs = 2000;

//窗口重新激活时，距离上一次拉取不到这么久就不再拉取，单位：毫秒
const int64_t kActivateRefreshGapMs = 3000;

//详情里最近操作最多列出的条数
const size_t kActivityLines = 20;

// ---- 控件 id（只在本窗口内唯一）----
//401 原为 DuiTab 页签，2026-10-03 改为两个胶囊页签（kIdTabMine、kIdTabShared）后不再使用，不复用
const UINT kIdRefresh   = 402;   //「刷新」
const UINT kIdOpenLocal = 403;   //「在本机打开」
const UINT kIdOpen      = 404;   //「打开」
const UINT kIdEdit      = 405;   //「调整成员」
const UINT kIdCancel    = 406;   //「取消共享」
const UINT kIdLocate    = 407;   //「定位到会话」
const UINT kIdNewShare  = 408;   //「发起共享」：选择共享对象后发起（2026-10-03 起）
const UINT kIdTabMine   = 409;   //胶囊页签「我的共享」
const UINT kIdTabShared = 410;   //胶囊页签「共享给我的」
const UINT kIdSearch    = 411;   //顶栏的搜索框
const UINT kIdToast     = 412;   //浮层提示（只用来定位控件）
const UINT kIdDetailAvatarBase = 1000;   //详情里头像的 id：kIdDetailAvatarBase + 第几个人（从 0 起，按显示顺序）；只用来定位控件

// ---- 配色 ----
const COLORREF kBgColor       = RGB(0xFF, 0xFF, 0xFF);   //卡片的白底
const COLORREF kWindowBg      = RGB(0xF5, 0xF6, 0xF8);   //窗口的浅灰底（两张卡片之外的部分）
const COLORREF kTextColor     = RGB(0x1F, 0x1F, 0x1F);   //名称与小节标题
const COLORREF kWeakTextColor = RGB(0x9A, 0x98, 0x93);   //第二行文字与提示
const COLORREF kLineTextColor = RGB(0x4A, 0x4A, 0x4A);   //详情里的普通行
const COLORREF kInvalidColor  = RGB(0xB0, 0xB0, 0xB0);   //已失效的名称
const COLORREF kErrorColor    = RGB(0xD9, 0x3B, 0x3B);   //错误提示
const COLORREF kSelectedBg    = RGB(0xEA, 0xF1, 0xFE);   //列表里选中行的底色
const COLORREF kHoverBg       = RGB(0xF5, 0xF7, 0xFA);   //列表里鼠标所在行的底色
const COLORREF kAccentColor   = RGB(45, 108, 223);       //品牌蓝：选中行左侧竖条（与 DuiButton 的 Primary 实心按钮同色）
const COLORREF kChipOkBg      = RGB(0xE7, 0xF6, 0xEC);   //「可用」胶囊底色（浅绿）
const COLORREF kChipOkText    = RGB(0x1E, 0x8E, 0x4E);   //「可用」胶囊文字（深绿）
const COLORREF kChipWarnBg    = RGB(0xFD, 0xF1, 0xDF);   //「暂不可用」胶囊底色（浅橙）
const COLORREF kChipWarnText  = RGB(0xB4, 0x6A, 0x00);   //「暂不可用」胶囊文字（深橙）
const COLORREF kChipGreyBg    = RGB(0xF0, 0xF0, 0xF0);   //「已失效」与本机标注胶囊的底色（浅灰）
const COLORREF kChipGreyText  = RGB(0x8A, 0x8A, 0x8A);   //「已失效」与本机标注胶囊的文字（灰）
const COLORREF kSeparatorColor = RGB(0xEE, 0xEE, 0xEE);  //详情小节标题下方的细分隔线
const COLORREF kToastInfoBg    = RGB(0x32, 0x32, 0x32);  //普通浮层提示的深灰底（白字）
const COLORREF kToastTextColor = RGB(0xFF, 0xFF, 0xFF);  //浮层提示的文字

// ---- 文字 ----
const wchar_t* const kWindowTitle    = TR_NOOP(L"文件共享");
const wchar_t* const kTabMine        = TR_NOOP(L"我的共享");
const wchar_t* const kTabShared      = TR_NOOP(L"共享给我的");
const wchar_t* const kRefreshText    = TR_NOOP(L"刷新");
const wchar_t* const kNewShareText   = TR_NOOP(L"+ 发起共享");
const wchar_t* const kSearchHintMine   = TR_NOOP(L"搜索名称、路径、会话");     //「我的共享」页签的搜索提示
const wchar_t* const kSearchHintShared = TR_NOOP(L"搜索名称、分享者、会话");   //「共享给我的」页签的搜索提示：这里的条目不带真实路径
const wchar_t* const kTabCountFormat = L"%s %u";   //带个数的页签文字：页签名、空格、个数
const wchar_t* const kPickFailText   = TR_NOOP(L"现在无法选择共享对象：请确认已经登录、通讯录已经加载后重试");
const wchar_t* const kOpenLocalText  = TR_NOOP(L"在本机打开");
const wchar_t* const kOpenText       = TR_NOOP(L"打开");
const wchar_t* const kEditText       = TR_NOOP(L"调整成员");
const wchar_t* const kCancelText     = TR_NOOP(L"取消共享");
const wchar_t* const kLocateText     = TR_NOOP(L"定位到会话");
const wchar_t* const kNoSelection    = TR_NOOP(L"选中左侧的一个共享查看详情");
const wchar_t* const kLoadingText    = TR_NOOP(L"正在加载…");
const wchar_t* const kLoadFailText   = TR_NOOP(L"加载失败，请稍后点「刷新」重试");
const wchar_t* const kEmptyMine      = TR_NOOP(L"你还没有发起过共享");
const wchar_t* const kEmptyShared    = TR_NOOP(L"还没有人共享给你");
const wchar_t* const kEmptyMineHint  = TR_NOOP(L"点右上角「发起共享」，把本机的磁盘、文件夹或文件共享给同事");
const wchar_t* const kEmptySharedHint = TR_NOOP(L"同事共享给你的内容会出现在这里");
const wchar_t* const kNoMatchText    = TR_NOOP(L"没有找到匹配的共享");
const wchar_t* const kNoMatchHint    = TR_NOOP(L"换个关键字试试，可以搜索名称、路径或会话");
const wchar_t* const kCancelingText  = TR_NOOP(L"正在取消…");
const wchar_t* const kCanceledText   = TR_NOOP(L"已取消共享");
const wchar_t* const kNoResponseText = TR_NOOP(L"服务器没有响应，请检查网络后重试");
const wchar_t* const kOpenLocalFail  = TR_NOOP(L"本机打不开这个共享：记录不存在或共享的内容已被移走");
const wchar_t* const kSecMembers     = TR_NOOP(L"成员与权限");
const wchar_t* const kSecVisitors    = TR_NOOP(L"正在访问");
const wchar_t* const kSecActivities  = TR_NOOP(L"最近操作");
const wchar_t* const kSecInfo        = TR_NOOP(L"基本信息");
const wchar_t* const kSectionCountFormat = L"%s（%u）";   //带个数的小节标题：小节名、个数
const wchar_t* const kFieldKind      = TR_NOOP(L"类型");
const wchar_t* const kFieldConv      = TR_NOOP(L"所在会话");
const wchar_t* const kFieldState     = TR_NOOP(L"状态");
const wchar_t* const kFieldPerms     = TR_NOOP(L"我的权限");
const wchar_t* const kNoMembers      = TR_NOOP(L"没有成员");
const wchar_t* const kNoVisitors     = TR_NOOP(L"没有人正在访问");
const wchar_t* const kNoActivities   = TR_NOOP(L"还没有操作记录");
const wchar_t* const kOwnerPrefix    = TR_NOOP(L"分享者：");
const wchar_t* const kOwnerSub       = TR_NOOP(L"分享者");
const wchar_t* const kPermsPrefix    = TR_NOOP(L"我的权限：");
const wchar_t* const kStatePrefix    = TR_NOOP(L"状态：");
const wchar_t* const kKindPrefix     = TR_NOOP(L"类型：");
const wchar_t* const kPathPrefix     = TR_NOOP(L"路径：");
const wchar_t* const kConvPrefix     = TR_NOOP(L"所在会话：");
const wchar_t* const kConfirmTitle   = TR_NOOP(L"取消共享");
const wchar_t* const kConfirmFormat  = TR_NOOP(L"确定取消共享「%s」吗？\n\n取消后所有成员都无法再访问，正在进行的浏览与传输会立即结束。");

//确认框文字缓冲区的字符数
const size_t kConfirmBufChars = 512;

//UTF-8 转成界面用的宽字符串
std::wstring Wide(const std::string& utf8)
{
    std::wstring w;
    ShareUtf8ToWide(utf8, w);
    return w;
}

//一段文字的宽度，单位：像素。font 为文字所用的字体，为 NULL 时按设备上下文的默认字体测量
int MeasureTextWidth(HFONT font, const std::wstring& text)
{
    HDC hdc = ::GetDC(NULL);
    HGDIOBJ oldFont = (font != NULL) ? ::SelectObject(hdc, font) : NULL;
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    if (oldFont != NULL)
        ::SelectObject(hdc, oldFont);
    ::ReleaseDC(NULL, hdc);
    return size.cx;
}

//正好放下文字的按钮宽度：文字宽度加两侧内边距。font 为按钮文字的字体
int FitButtonWidth(HFONT font, const std::wstring& text)
{
    return MeasureTextWidth(font, text) + 2 * kPillPadding;
}

//胶囊页签的文字：列表到达之后带上个数（如「我的共享 9」），之前只有页签名
std::wstring TabText(const wchar_t* name, size_t count, bool known)
{
    //页签文字的缓冲区字符数：页签名不超过 5 个字，加上个数绰绰有余
    const size_t kTabTextBufChars = 32;
    if (!known)
        return name;
    wchar_t buf[kTabTextBufChars] = { 0 };
    _snwprintf_s(buf, kTabTextBufChars, _TRUNCATE, kTabCountFormat, name, static_cast<unsigned>(count));
    return buf;
}

//在 right 左边画一个胶囊（圆角底色 + 居中文字），顶边在 top，用 hdc 当前选入的字体。返回胶囊的左边缘；
//文字为空时什么也不画，原样返回 right
int DrawChip(HDC hdc, const std::wstring& text, COLORREF bg, COLORREF fg, int right, int top)
{
    if (text.empty())
        return right;
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    RECT rc = { right - size.cx - 2 * kChipPadding, top, right, top + kChipHeight };
    balloonwjui::DuiAA::FillRoundRect(hdc, rc, bg, kChipHeight / 2);
    ::SetTextColor(hdc, fg);
    ::DrawTextW(hdc, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return rc.left;
}

//左侧列表：DuiVirtualList 总是给自己画一圈 1 像素灰边，放进白色圆角卡片里显得多余。本类在基类画完之后用卡片的白色
//重画这一圈，使列表与卡片融为一体。只覆写 OnPaint，行为与 DuiVirtualList 完全相同（不改 balloonui）
class CBorderlessList : public DuiVirtualList
{
public:
    //列表为空时的绘制回调：user 为登记时传入的指针，listRect 为列表矩形
    typedef void (*EmptyPaintFn)(void* user, HDC hdc, const RECT& listRect);

    CBorderlessList()
        : m_emptyFn(NULL)
        , m_emptyUser(NULL)
    {
    }

    /**
     *  登记列表为空时的绘制回调（画在列表中央的提示）。
     *  @param fn   回调，可为 NULL（不画）
     *  @param user 回调的第一个参数（不拥有）
     */
    void SetEmptyPaintCallback(EmptyPaintFn fn, void* user)
    {
        m_emptyFn = fn;
        m_emptyUser = user;
    }

    /**
     *  绘制：先交给 DuiVirtualList 画底色、边框与各行，再用卡片底色盖住四条边框线；没有行时再画空列表的提示。
     *  @param hdc     宿主的背缓冲 DC
     *  @param rcDirty 需要重画的区域
     */
    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        DuiVirtualList::OnPaint(hdc, rcDirty);
        if (!IsVisible())
            return;
        const RECT rc = GetRect();
        HPEN pen = ::CreatePen(PS_SOLID, 1, kBgColor);
        HGDIOBJ oldPen = ::SelectObject(hdc, pen);
        HGDIOBJ oldBrush = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
        ::Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        ::SelectObject(hdc, oldBrush);
        ::SelectObject(hdc, oldPen);
        ::DeleteObject(pen);
        if (GetRowCount() == 0 && m_emptyFn != NULL)
            m_emptyFn(m_emptyUser, hdc, rc);
    }

private:
    EmptyPaintFn m_emptyFn;     //列表为空时的绘制回调；为 NULL 时不画
    void*        m_emptyUser;   //回调的第一个参数（窗口，不拥有）
};

//建一个详情操作按钮：variant 为样式（主操作品牌蓝实心、次操作白底灰边、取消共享红色实心）
std::unique_ptr<DuiButton> MakeActionButton(const wchar_t* text, UINT id, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetText(text);
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetTabStop(false);
    return button;
}

//建一个单行文字标签
std::unique_ptr<DuiLabel> MakeTextLabel(const std::wstring& text, COLORREF color, DWORD align)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetText(text.c_str());
    label->SetTextColor(color);
    label->SetTextAlign(align);
    return label;
}

//建一个胶囊：圆角底色的容器里放一行居中的文字（只有 DuiVBox 能画底色与圆角）。width 出参为正好放下文字的宽度（文字宽度加两侧内边距），
//font 为文字所用的字体（为 NULL 时按设备上下文的默认字体测量），由调用方据此放进布局
std::unique_ptr<DuiVBox> MakeChip(const std::wstring& text, COLORREF bg, COLORREF fg, int height, int padding, HFONT font,
                                  int& width)
{
    std::unique_ptr<DuiVBox> chip(new DuiVBox());
    chip->SetBgColor(bg);
    chip->SetCornerRadius(height / 2);
    chip->AddChild(std::unique_ptr<DuiControl>(MakeTextLabel(text, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE).release()),
                   DuiLayout::Hint().Weight(1));
    width = MeasureTextWidth(font, text) + 2 * padding;
    return chip;
}

//状态胶囊的配色：可用绿、暂不可用橙、已失效与其它灰
void StateChipColors(int state, COLORREF& bg, COLORREF& fg)
{
    if (state == share_state_available)
    {
        bg = kChipOkBg;
        fg = kChipOkText;
    }
    else if (state == share_state_unavailable)
    {
        bg = kChipWarnBg;
        fg = kChipWarnText;
    }
    else
    {
        bg = kChipGreyBg;
        fg = kChipGreyText;
    }
}

//带个数的小节标题，如「成员与权限（2）」
std::wstring SectionTitle(const wchar_t* name, size_t count)
{
    //小节标题的缓冲区字符数：小节名不超过 6 个字，加上个数绰绰有余
    const size_t kSectionBufChars = 32;
    wchar_t buf[kSectionBufChars] = { 0 };
    _snwprintf_s(buf, kSectionBufChars, _TRUNCATE, kSectionCountFormat, name, static_cast<unsigned>(count));
    return buf;
}

//右侧详情的滚动视口：DuiScrollView 总是先给自己铺一层浅灰底，内容比视口矮时下面会露出灰色，放进白色卡片里显得突兀。
//本类让内容至少与视口一样高（内容根容器自带白底，于是铺满整个视口），内容更高时照常滚动。只覆写 Layout（不改 balloonui）。
//2026-10-04 起滚动条为悬浮式、浮在内容之上，不再有单独的一列，此前把那一列铺成白底的 OnPaint 已删除
class CWhiteScrollView : public DuiScrollView
{
public:
    CWhiteScrollView()
        : m_naturalHeight(0)
    {
    }

    /**
     *  设置内容实际需要的高度：内容比视口矮时按视口高度铺满，否则按实际高度（可以滚动）。
     *  @param height 内容各行高度之和，单位：像素
     */
    void SetNaturalHeight(int height)
    {
        m_naturalHeight = height;
        const RECT rc = GetRect();
        const int viewH = rc.bottom - rc.top;
        SetContentHeight(height > viewH ? height : viewH);
    }

    /**
     *  排版：先按新的视口高度定内容高度，再交给 DuiScrollView 排版（窗口改变大小后内容仍铺满视口）。
     *  @param rcAvail 分到的矩形
     */
    virtual void Layout(const RECT& rcAvail) override
    {
        const int viewH = rcAvail.bottom - rcAvail.top;
        SetContentHeight(m_naturalHeight > viewH ? m_naturalHeight : viewH);
        DuiScrollView::Layout(rcAvail);
    }

private:
    int m_naturalHeight;   //内容各行高度之和，单位：像素；SetNaturalHeight 设置
};

//「我的共享」详情里会随时间变化的内容的摘要：每名正在访问的人（userid、正在做什么与进度）与最近操作（条数、最新一条的 id）。
//定时刷新时摘要没有变化就不重建详情
std::wstring LiveSignature(const std::vector<ShareActiveVisitor>& visitors, const std::vector<ShareActivityRecord>& activities)
{
    std::wstring out;
    for (size_t i = 0; i < visitors.size(); ++i)
    {
        out += std::to_wstring(static_cast<long long>(visitors[i].m_visitorId));
        out += L"|";
        out += ShareCenterModel::VisitorText(visitors[i], std::wstring());
        out += L"\n";
    }
    out += L"#";
    out += std::to_wstring(static_cast<unsigned long long>(activities.size()));
    if (!activities.empty())
    {
        out += L"@";
        out += std::to_wstring(static_cast<long long>(activities[0].m_id));
    }
    return out;
}

} // namespace

CShareCenterWindow::CShareCenterWindow()
    : m_service(NULL)
    , m_host(NULL)
    , m_ui(NULL)
    , m_tab(share_center_tab_mine)
    , m_selected(-1)
    , m_mineSeq(0)
    , m_sharedSeq(0)
    , m_cancelId(0)
    , m_lastRefreshMs(0)
    , m_modal(false)
    , m_focusShareId(0)
    , m_launchCtype(0)
    , m_launchCid(0)
    , m_detailShareId(0)
    , m_minDetailWidth(kDetailWidth)
    , m_detailBuildCount(0)
    , m_headerIconBmp(NULL)
    , m_topBar(NULL)
    , m_tabMine(NULL)
    , m_tabShared(NULL)
    , m_search(NULL)
    , m_btnNewShare(NULL)
    , m_btnRefresh(NULL)
    , m_list(NULL)
    , m_body(NULL)
    , m_detailCard(NULL)
    , m_detailHeader(NULL)
    , m_detailIcon(NULL)
    , m_detailTitle(NULL)
    , m_detailPath(NULL)
    , m_detailChip(NULL)
    , m_detailChipText(NULL)
    , m_actionRow(NULL)
    , m_btnOpenLocal(NULL)
    , m_btnOpen(NULL)
    , m_btnEdit(NULL)
    , m_btnCancel(NULL)
    , m_detailScroll(NULL)
    , m_toast(NULL)
    , m_hasNormalRect(false)
{
    ::SetRectEmpty(&m_lastNormalRect);
}

CShareCenterWindow::~CShareCenterWindow()
{
    Close();
}

bool CShareCenterWindow::Open(HWND owner, CShareService* service, IFileShareHost* host, IShareUiHost* ui)
{
    if (service == NULL || host == NULL || ui == NULL)
        return false;
    bool showMaximized = false;   //新建的窗口按上次关闭时的状态以最大化显示
    if (!IsWindow())
    {
        m_service = service;
        m_host = host;
        m_ui = ui;
        m_tab = share_center_tab_mine;
        m_selected = -1;
        m_mine.clear();
        m_shared.clear();
        m_visible.clear();
        m_filter.clear();
        m_convNames.clear();
        //父窗口传 NULL：作为没有所有者的独立顶层窗口；owner 只用来居中。控件通知发回本窗口，与有无所有者无关
        //（2026-10-14 起，见开发约定文档 UI 约定第 11 条）
        if (Create(NULL, CWindow::rcDefault, TR(kWindowTitle), WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR("share center window: Create failed, error: %lu.", ::GetLastError());
            return false;
        }
        SetTitle(TR(kWindowTitle));
        SetTitleBarHeight(kTitleBarHeight);
        SetButtons(true, true, true);
        //程序图标：标题栏左侧的位图与任务栏 / Alt-Tab 图标（图标资源在 exe 里，见 TitleBarIcon.h）
        SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
        m_programIcons.ApplyTaskIcons(m_hWnd);
        SetResizable(true);
        SetMinSize(kMinWindowWidth, kMinWindowHeight);
        BuildContent();
        //DuiFrameWindow 的 ResizeClient 设的是整窗尺寸，标题栏也在其内（UI 约定第 7 条）
        ResizeClient(kContentWidth, kContentHeight + kTitleBarHeight);
        HWND anchor = (owner != NULL && ::IsWindow(owner)) ? owner : NULL;
        showMaximized = ApplySavedPlacement(anchor);
        ::SetTimer(m_hWnd, kVisitorTimerId, kVisitorTimerMs, NULL);
        RebuildDetail();
        LOG_INFO("share center window created.");
    }
    //先拉取再显示：显示时窗口被激活，激活处理见到刚刚拉取过就不再重复拉取
    Refresh();
    //已经开着的窗口最小化了就还原；新建的窗口上次关闭时处于最大化则以最大化显示
    if (showMaximized)
        ShowWindow(SW_SHOWMAXIMIZED);
    else if (IsIconic())
        ShowWindow(SW_RESTORE);
    else
        ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    return true;
}

void CShareCenterWindow::Close()
{
    if (m_service != NULL)
    {
        m_service->DetachListObserver(this);
        m_service->DetachManageObserver(this);
    }
    m_mineSeq = 0;
    m_sharedSeq = 0;
    m_cancelId = 0;
    m_focusShareId = 0;
    if (IsWindow())
    {
        ::KillTimer(m_hWnd, kVisitorTimerId);
        SavePlacement();
        DestroyWindow();
        LOG_INFO("share center window destroyed.");
    }
    //头像控件随窗口一起销毁之后再释放它们借用的位图
    m_avatars.Clear();
    m_topBar = NULL;
    m_tabMine = NULL;
    m_tabShared = NULL;
    m_search = NULL;
    m_btnNewShare = NULL;
    m_btnRefresh = NULL;
    m_list = NULL;
    m_body = NULL;
    m_detailCard = NULL;
    m_detailHeader = NULL;
    m_detailIcon = NULL;
    m_detailTitle = NULL;
    m_detailPath = NULL;
    m_detailChip = NULL;
    m_detailChipText = NULL;
    m_actionRow = NULL;
    m_btnOpenLocal = NULL;
    m_btnOpen = NULL;
    m_btnEdit = NULL;
    m_btnCancel = NULL;
    m_detailScroll = NULL;
    m_toast = NULL;
    //详情头部的图标控件已随窗口销毁，释放它借用的位图
    if (m_headerIconBmp != NULL)
    {
        ::DeleteObject(m_headerIconBmp);
        m_headerIconBmp = NULL;
    }
}

void CShareCenterWindow::Refresh()
{
    if (m_service == NULL)
        return;
    m_lastRefreshMs = static_cast<int64_t>(::GetTickCount64());
    m_mineSeq = m_service->RequestList(kShareListScopeMine, 0, 0, this);
    m_sharedSeq = m_service->RequestList(kShareListScopeSharedMe, 0, 0, this);
    //加载中由列表里的「正在加载…」表示（见 PaintEmpty），只有请求发不出去时弹出提示
    if (m_mineSeq == 0 || m_sharedSeq == 0)
        ShowToast(TR(kLoadFailText), true);
    //加载中的页签不带个数
    UpdateTabButtons();
}

void CShareCenterWindow::OnShareListResult(uint32_t requestId, int scope, int code, const std::vector<ShareListEntry>& shares)
{
    if (requestId != m_mineSeq && requestId != m_sharedSeq)
        return;
    (void)scope;
    const bool mineList = (requestId == m_mineSeq);
    //选中的共享在列表刷新之后仍选中它：由 RebuildVisible 按 shareid 保持
    if (requestId == m_mineSeq)
    {
        m_mineSeq = 0;
        if (code == error_code_ok)
            m_mine = shares;
    }
    else
    {
        m_sharedSeq = 0;
        if (code == error_code_ok)
            m_shared = shares;
    }
    if (code != error_code_ok)
    {
        ShowToast(TR(kLoadFailText), true);
        LOG_WARNING("share center window: list failed, scope: %d, code: %d.", scope, code);
    }
    RebuildVisible();
    UpdateTabButtons();
    RefreshList();
    RebuildDetail();
    //通知里点「查看」时窗口还在加载：「我的共享」到达后选中那个共享（第 4 期起）
    if (mineList && code == error_code_ok && m_focusShareId != 0 && m_tab == share_center_tab_mine)
    {
        const int64_t shareId = m_focusShareId;
        m_focusShareId = 0;
        for (size_t i = 0; i < m_visible.size(); ++i)
        {
            if (m_visible[i].m_shareId == shareId)
            {
                SelectRow(static_cast<int>(i));
                break;
            }
        }
    }
}

void CShareCenterWindow::FocusShare(int64_t shareId)
{
    if (shareId <= 0)
        return;
    //关键字可能把它滤掉：先清空搜索框（不触发文字变化通知），再按空关键字重算
    if (m_search != NULL)
        m_search->SetTextNoNotify(L"");
    SetFilter(std::wstring());
    if (m_tab != share_center_tab_mine)
        SetTab(share_center_tab_mine);
    for (size_t i = 0; i < m_visible.size(); ++i)
    {
        if (m_visible[i].m_shareId == shareId)
        {
            SelectRow(static_cast<int>(i));
            //列表随后可能还会刷新一次：RebuildVisible 按 shareid 保持选中，不必再等
            m_focusShareId = 0;
            LOG_INFO("share center window focused share %lld.", static_cast<long long>(shareId));
            return;
        }
    }
    //列表还没到：到达后再选中
    m_focusShareId = shareId;
    LOG_INFO("share center window will focus share %lld after the list arrives.", static_cast<long long>(shareId));
}

void CShareCenterWindow::OnShareManageResult(const ShareManageResult& result)
{
    if (result.m_requestId != m_cancelId || m_cancelId == 0)
        return;
    m_cancelId = 0;
    if (result.m_code != error_code_ok)
    {
        ShowToast(result.m_code == kShareManageNoResponse ? std::wstring(TR(kNoResponseText))
                                                          : ShareCreateModel::CreateErrorText(result.m_code), true);
        return;
    }
    LOG_INFO("share center window: share canceled, shareid: %lld.", static_cast<long long>(result.m_shareId));
    ShowToast(TR(kCanceledText), false);
    Refresh();
}

void CShareCenterWindow::SetTab(int tab)
{
    if (tab != share_center_tab_mine && tab != share_center_tab_shared)
        return;
    m_tab = tab;
    m_selected = -1;
    RebuildVisible();
    UpdateTabButtons();
    RefreshList();
    RebuildDetail();
}

void CShareCenterWindow::SelectRow(int row)
{
    m_selected = (row >= 0 && static_cast<size_t>(row) < Rows().size()) ? row : -1;
    if (m_list != NULL)
        m_list->SetCurSel(m_selected, false);
    RebuildDetail();
}

bool CShareCenterWindow::DoAction(int action, bool confirmed)
{
    if (m_selected < 0 || static_cast<size_t>(m_selected) >= Rows().size() || (GetActionMask() & action) == 0)
        return false;
    const ShareListEntry entry = Rows()[static_cast<size_t>(m_selected)];

    switch (action)
    {
    //在本机打开共享的文件夹
    case share_center_action_open_local:
        if (!OpenLocal(entry.m_shareId))
        {
            ShowToast(TR(kOpenLocalFail), true);
            return false;
        }
        return true;

    //打开浏览窗口（共享给我的）
    case share_center_action_open:
        m_ui->UiOpenShare(m_hWnd, entry.m_shareId);
        return true;

    //调整成员（我的共享）
    case share_center_action_edit:
        m_ui->UiEditMembers(m_hWnd, entry);
        return true;

    //取消共享：确认后发 2202，结果到达后重新拉取
    case share_center_action_cancel:
    {
        if (m_cancelId != 0)
            return false;
        if (!confirmed)
        {
            wchar_t text[kConfirmBufChars] = { 0 };
            _snwprintf_s(text, kConfirmBufChars, _TRUNCATE, TR(kConfirmFormat), Wide(entry.m_name).c_str());
            m_modal = true;
            const int answer = ::MessageBoxW(m_hWnd, text, TR(kConfirmTitle), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
            m_modal = false;
            if (answer != IDOK)
                return false;
        }
        m_cancelId = m_service->CancelShare(entry.m_shareId, this);
        if (m_cancelId == 0)
        {
            ShowToast(TR(kNoResponseText), true);
            return false;
        }
        ShowToast(TR(kCancelingText), false);
        return true;
    }

    //定位到会话：请宿主打开聊天窗（会话 id 换成本人视角）
    case share_center_action_locate:
        return m_host->OpenConversation(entry.m_ctype, ShareCenterModel::ViewerCid(entry, m_service->GetUserId()));

    //其余取值不是合法的操作
    default:
        return false;
    }
}

int CShareCenterWindow::GetTab() const
{
    return m_tab;
}

size_t CShareCenterWindow::GetRowCount(int tab) const
{
    return (tab == share_center_tab_mine) ? m_mine.size() : m_shared.size();
}

const ShareListEntry& CShareCenterWindow::GetRow(int tab, size_t row) const
{
    return (tab == share_center_tab_mine) ? m_mine[row] : m_shared[row];
}

int CShareCenterWindow::GetSelectedRow() const
{
    return m_selected;
}

int CShareCenterWindow::GetActionMask() const
{
    if (m_selected < 0 || static_cast<size_t>(m_selected) >= Rows().size() || m_service == NULL)
        return 0;
    const ShareListEntry& e = Rows()[static_cast<size_t>(m_selected)];
    if (m_tab == share_center_tab_mine)
        return ShareCenterModel::MineActions(e, m_service->GetLocalStatus(e.m_shareId));
    return ShareCenterModel::SharedActions(e);
}

std::vector<std::wstring> CShareCenterWindow::GetDetailLines() const
{
    std::vector<std::wstring> lines;
    for (size_t i = 0; i < m_detailRows.size(); ++i)
        lines.push_back(m_detailRows[i].m_text);
    return lines;
}

void CShareCenterWindow::AddHeaderOnly(const std::wstring& text)
{
    DetailRow row;
    row.m_kind = detail_row_header;
    row.m_text = text;
    m_detailRows.push_back(row);
}

void CShareCenterWindow::AddField(const std::wstring& label, const std::wstring& value, bool link, const std::wstring& text)
{
    DetailRow row;
    row.m_kind = detail_row_field;
    row.m_text = text;
    row.m_label = label;
    row.m_value = value;
    row.m_link = link;
    m_detailRows.push_back(row);
}

void CShareCenterWindow::AddSection(const std::wstring& title)
{
    DetailRow row;
    row.m_kind = detail_row_section;
    row.m_text = title;
    m_detailRows.push_back(row);
}

void CShareCenterWindow::AddEmpty(const std::wstring& text)
{
    DetailRow row;
    row.m_kind = detail_row_empty;
    row.m_text = text;
    m_detailRows.push_back(row);
}

void CShareCenterWindow::AddPerson(int64_t userId, const std::wstring& name, const std::wstring& sub,
                                   const std::vector<std::wstring>& chips, int progress, const std::wstring& text)
{
    //userid 不合法（服务端没给）时退回小节里的弱色文字，不画头像
    if (userId <= 0)
    {
        AddEmpty(text);
        return;
    }
    DetailRow row;
    row.m_kind = detail_row_person;
    row.m_text = text;
    row.m_userId = userId;
    row.m_name = name;
    row.m_sub = sub;
    row.m_chips = chips;
    row.m_progress = progress;
    m_detailRows.push_back(row);
}

void CShareCenterWindow::AddActivity(const std::wstring& time, const std::wstring& content, bool failed, const std::wstring& text)
{
    DetailRow row;
    row.m_kind = detail_row_activity;
    row.m_text = text;
    row.m_label = time;
    row.m_value = content;
    row.m_failed = failed;
    m_detailRows.push_back(row);
}

void CShareCenterWindow::RefreshAvatars()
{
    if (!IsWindow())
        return;
    RebuildDetail();
    //「共享给我的」列表行首的分享者小头像：重画时按新头像取位图
    if (m_list != NULL)
        m_list->Invalidate();
}

std::wstring CShareCenterWindow::GetToastText() const
{
    return m_toastText;
}

bool CShareCenterWindow::IsLoading() const
{
    return m_mineSeq != 0 || m_sharedSeq != 0;
}

int CShareCenterWindow::GetDetailBuildCount() const
{
    return m_detailBuildCount;
}

LRESULT CShareCenterWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    Close();
    return 0;
}

LRESULT CShareCenterWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //搜索框文字变了：按关键字过滤当前页签（通知码与控件 id 一起判，UI 约定第 4 条）
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kIdSearch && m_search != NULL)
    {
        SetFilter(std::wstring(m_search->GetText().GetString()));
        return 0;
    }
    if (pNotify->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    switch (pNotify->ctrlId)
    {
    //胶囊页签「我的共享」
    case kIdTabMine:
        SetTab(share_center_tab_mine);
        return 0;
    //胶囊页签「共享给我的」
    case kIdTabShared:
        SetTab(share_center_tab_shared);
        return 0;
    //「刷新」
    case kIdRefresh:
        Refresh();
        return 0;
    //「发起共享」：选择共享对象
    case kIdNewShare:
        StartDirectShare();
        return 0;
    //「在本机打开」
    case kIdOpenLocal:
        DoAction(share_center_action_open_local, false);
        return 0;
    //「打开」
    case kIdOpen:
        DoAction(share_center_action_open, false);
        return 0;
    //「调整成员」
    case kIdEdit:
        DoAction(share_center_action_edit, false);
        return 0;
    //「取消共享」
    case kIdCancel:
        DoAction(share_center_action_cancel, false);
        return 0;
    //「定位到会话」
    case kIdLocate:
        DoAction(share_center_action_locate, false);
        return 0;
    //其余控件的点击：交给基类
    default:
        bHandled = FALSE;
        return 0;
    }
}

LRESULT CShareCenterWindow::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam != kVisitorTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    //只有「我的共享」选中了一行时才有「正在访问的人」要刷新；内容没有变化时不重建详情（重建会让悬停提示消失）
    if (m_tab != share_center_tab_mine || m_selected < 0 || m_service == NULL || m_detailShareId == 0)
        return 0;
    std::vector<ShareActivityRecord> activities;
    m_service->ListActivities(m_detailShareId, kActivityLines, activities);
    if (LiveSignature(m_service->GetActiveVisitors(m_detailShareId), activities) != m_liveSignature)
        RebuildDetail();
    return 0;
}

LRESULT CShareCenterWindow::OnActivateMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    if (LOWORD(wParam) == WA_INACTIVE || m_modal || m_service == NULL)
        return 0;
    const int64_t now = static_cast<int64_t>(::GetTickCount64());
    if (now - m_lastRefreshMs >= kActivateRefreshGapMs && m_mineSeq == 0 && m_sharedSeq == 0)
        Refresh();
    return 0;
}

void CShareCenterWindow::BuildContent()
{
    typedef DuiLayout::Hint Hint;

    //窗口浅灰底；左侧列表与右侧详情各放在一张白色圆角卡片里
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kWindowBg);
    root->SetPadding(kPadding, kPadding, kPadding, kPadding);

    //顶栏：两个胶囊页签 | 空白 | 搜索框 | 「发起共享」 | 「刷新」。各控件高 kTopControlHeight，在顶栏里垂直居中；
    //页签与按钮的宽度按字体量出（见 UpdateButtonWidths），这里先给一个占位宽度
    std::unique_ptr<DuiHBox> top(new DuiHBox());
    top->SetGap(kTopGap);
    std::unique_ptr<DuiButton> tabMine(new DuiButton());
    tabMine->SetCtrlId(kIdTabMine);
    tabMine->SetTabStop(false);
    m_tabMine = tabMine.get();
    top->AddChild(std::unique_ptr<DuiControl>(tabMine.release()),
                  Hint().Fixed(kWideButtonWidth, kTopControlHeight).AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiButton> tabShared(new DuiButton());
    tabShared->SetCtrlId(kIdTabShared);
    tabShared->SetTabStop(false);
    m_tabShared = tabShared.get();
    top->AddChild(std::unique_ptr<DuiControl>(tabShared.release()),
                  Hint().Fixed(kWideButtonWidth, kTopControlHeight).AlignC(DuiLayout::AlignCenter));
    top->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    std::unique_ptr<DuiSearchBox> search(new DuiSearchBox());
    search->SetPlaceholder(TR(m_tab == share_center_tab_mine ? kSearchHintMine : kSearchHintShared));
    search->SetCtrlId(kIdSearch);
    m_search = search.get();
    top->AddChild(std::unique_ptr<DuiControl>(search.release()),
                  Hint().Fixed(kSearchWidth, kTopControlHeight).AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiButton> newShare(new DuiButton());
    newShare->SetText(TR(kNewShareText));
    newShare->SetVariant(DuiButton::Variant::Primary);
    newShare->SetCtrlId(kIdNewShare);
    newShare->SetTabStop(false);
    m_btnNewShare = newShare.get();
    top->AddChild(std::unique_ptr<DuiControl>(newShare.release()),
                  Hint().Fixed(kWideButtonWidth, kTopControlHeight).AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiButton> refresh(new DuiButton());
    refresh->SetText(TR(kRefreshText));
    refresh->SetVariant(DuiButton::Variant::Default);
    refresh->SetCtrlId(kIdRefresh);
    refresh->SetTabStop(false);
    m_btnRefresh = refresh.get();
    top->AddChild(std::unique_ptr<DuiControl>(refresh.release()),
                  Hint().Fixed(kButtonWidth, kTopControlHeight).AlignC(DuiLayout::AlignCenter));
    m_topBar = top.get();
    root->AddChild(std::unique_ptr<DuiControl>(top.release()), Hint().Fixed(kTopBarHeight));

    //中部：左侧列表卡片 | 右侧详情卡片
    std::unique_ptr<DuiHBox> body(new DuiHBox());
    body->SetGap(kGap);
    m_body = body.get();
    std::unique_ptr<DuiVBox> listCard(new DuiVBox());
    listCard->SetBgColor(kBgColor);
    listCard->SetCornerRadius(kCardRadius);
    listCard->SetPadding(kListCardPadding, kListCardPadding, kListCardPadding, kListCardPadding);
    std::unique_ptr<CBorderlessList> list(new CBorderlessList());
    list->SetRowHeight(kRowHeight);
    list->SetPaintRowCallback(&CShareCenterWindow::PaintRowThunk, this);
    list->SetRowClickCallback(&CShareCenterWindow::RowClickThunk, this);
    list->SetEmptyPaintCallback(&CShareCenterWindow::PaintEmptyThunk, this);
    m_list = list.get();
    listCard->AddChild(std::unique_ptr<DuiControl>(list.release()), Hint().Weight(1));
    body->AddChild(std::unique_ptr<DuiControl>(listCard.release()), Hint().Weight(1));

    //右侧详情卡片：头部（类型图标 | 名称与路径 | 状态胶囊）、操作按钮行、下面各小节的滚动区
    std::unique_ptr<DuiVBox> detail(new DuiVBox());
    detail->SetBgColor(kBgColor);
    detail->SetCornerRadius(kCardRadius);
    detail->SetPadding(kDetailCardPadding, kDetailCardPadding, kDetailCardPadding, kDetailCardPadding);
    detail->SetGap(kDetailBlockGap);
    std::unique_ptr<DuiHBox> header(new DuiHBox());
    std::unique_ptr<balloonwjui::DuiAvatar> icon(new balloonwjui::DuiAvatar());
    //头像控件按透明度绘制位图；系统图标四角本来透明，1 像素的圆角看不出来
    icon->SetShape(balloonwjui::DuiAvatar::ShapeRoundRect);
    icon->SetCornerRadius(kIconCornerRadius);
    m_detailIcon = icon.get();
    header->AddChild(std::unique_ptr<DuiControl>(icon.release()),
                     Hint().Fixed(kDetailIconPx, kDetailIconPx).Margin(0, 0, kDetailIconGap, 0).AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiVBox> headerTexts(new DuiVBox());
    std::unique_ptr<DuiLabel> title = MakeTextLabel(std::wstring(), kTextColor, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
    title->SetTextPointSize(kDetailTitlePt, true);
    m_detailTitle = title.get();
    headerTexts->AddChild(std::unique_ptr<DuiControl>(title.release()), Hint().Weight(1));
    std::unique_ptr<DuiLabel> path = MakeTextLabel(std::wstring(), kWeakTextColor, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_PATH_ELLIPSIS);
    m_detailPath = path.get();
    headerTexts->AddChild(std::unique_ptr<DuiControl>(path.release()), Hint().Weight(1));
    header->AddChild(std::unique_ptr<DuiControl>(headerTexts.release()), Hint().Weight(1));
    int chipWidth = 0;
    std::unique_ptr<DuiVBox> chip = MakeChip(std::wstring(), kChipGreyBg, kChipGreyText, kChipHeight, kChipPadding, NULL, chipWidth);
    m_detailChip = chip.get();
    m_detailChipText = static_cast<DuiLabel*>(chip->Children()[0].get());
    header->AddChild(std::unique_ptr<DuiControl>(chip.release()),
                     Hint().Fixed(chipWidth, kChipHeight).AlignC(DuiLayout::AlignCenter));
    m_detailHeader = header.get();
    detail->AddChild(std::unique_ptr<DuiControl>(header.release()), Hint().Fixed(kDetailHeaderH));

    //操作按钮行：主操作（品牌蓝实心）| 调整成员（白底灰边）| 空白 | 取消共享（红色实心，放在最右）。按钮宽度按字体量出
    //（见 UpdateButtonWidths），这里先给一个占位宽度。「定位到会话」是信息表里所在会话一行右边的链接
    std::unique_ptr<DuiHBox> actions(new DuiHBox());
    actions->SetGap(kTopGap);
    std::unique_ptr<DuiButton> openLocal = MakeActionButton(TR(kOpenLocalText), kIdOpenLocal, DuiButton::Variant::Primary);
    m_btnOpenLocal = openLocal.get();
    actions->AddChild(std::unique_ptr<DuiControl>(openLocal.release()),
                      Hint().Fixed(kWideButtonWidth, kActionButtonH).AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiButton> open = MakeActionButton(TR(kOpenText), kIdOpen, DuiButton::Variant::Primary);
    m_btnOpen = open.get();
    actions->AddChild(std::unique_ptr<DuiControl>(open.release()),
                      Hint().Fixed(kButtonWidth, kActionButtonH).AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiButton> edit = MakeActionButton(TR(kEditText), kIdEdit, DuiButton::Variant::Default);
    m_btnEdit = edit.get();
    actions->AddChild(std::unique_ptr<DuiControl>(edit.release()),
                      Hint().Fixed(kWideButtonWidth, kActionButtonH).AlignC(DuiLayout::AlignCenter));
    actions->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    std::unique_ptr<DuiButton> cancel = MakeActionButton(TR(kCancelText), kIdCancel, DuiButton::Variant::Danger);
    m_btnCancel = cancel.get();
    actions->AddChild(std::unique_ptr<DuiControl>(cancel.release()),
                      Hint().Fixed(kWideButtonWidth, kActionButtonH).AlignC(DuiLayout::AlignCenter));
    m_actionRow = actions.get();
    detail->AddChild(std::unique_ptr<DuiControl>(actions.release()), Hint().Fixed(kActionRowH));
    //详情滚动区：balloonui 统一的悬浮式细滚动条，滚动或鼠标移到滚动条上时淡入、停止后淡出（UI 约定第 1 条），
    //宽度与自动隐藏都用库的默认值
    std::unique_ptr<CWhiteScrollView> scroll(new CWhiteScrollView());
    m_detailScroll = scroll.get();
    detail->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));
    m_detailCard = detail.get();
    body->AddChild(std::unique_ptr<DuiControl>(detail.release()), Hint().Fixed(kDetailWidth));
    root->AddChild(std::unique_ptr<DuiControl>(body.release()), Hint().Weight(1));

    //浮层提示：挂在内容根容器的最后（按子控件顺序绘制，最后一个在最上层）；布局给 0 高度、不占位置，
    //DuiToast 自己按根容器的矩形定在顶栏下方水平居中；不参与点击命中，点击落到下面的控件上
    std::unique_ptr<balloonwjui::DuiToast> toast(new balloonwjui::DuiToast());
    toast->SetTextColor(kToastTextColor);
    toast->SetTopOffset(kToastTop);
    toast->SetMaxWidth(kToastMaxWidth);
    toast->SetCtrlId(kIdToast);
    m_toast = toast.get();
    root->AddChild(std::unique_ptr<DuiControl>(toast.release()), Hint().Fixed(0));

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
    //控件挂到窗口上之后才拿得到窗口 DPI 下的字体，这时再定页签与按钮的文字和宽度
    UpdateTabButtons();
}

void CShareCenterWindow::UpdateButtonWidths(HFONT font)
{
    typedef DuiLayout::Hint Hint;
    //顶栏：两个胶囊页签、「发起共享」「刷新」。SetHint 改完会让所在的行立即重新排版
    if (m_topBar != NULL && m_tabMine != NULL && m_tabShared != NULL && m_btnNewShare != NULL && m_btnRefresh != NULL)
    {
        DuiButton* buttons[] = { m_tabMine, m_tabShared, m_btnNewShare, m_btnRefresh };
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
        {
            const int width = FitButtonWidth(font, std::wstring(buttons[i]->GetText().GetString()));
            m_topBar->SetHint(buttons[i], Hint().Fixed(width, kTopControlHeight).AlignC(DuiLayout::AlignCenter));
        }
    }
    //详情的操作按钮；同时算出「我的共享」按钮最多的一组（在本机打开、调整成员、取消共享）连同间距需要的宽度。
    //「共享给我的」只有一个「打开」，比这一组窄
    int widestRow = 0;
    if (m_actionRow != NULL && m_btnOpenLocal != NULL && m_btnOpen != NULL && m_btnEdit != NULL && m_btnCancel != NULL)
    {
        DuiButton* actions[] = { m_btnOpenLocal, m_btnOpen, m_btnEdit, m_btnCancel };
        for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i)
        {
            const int width = FitButtonWidth(font, std::wstring(actions[i]->GetText().GetString()));
            m_actionRow->SetHint(actions[i], Hint().Fixed(width, kActionButtonH).AlignC(DuiLayout::AlignCenter));
            if (actions[i] != m_btnOpen)
                widestRow += width + ((widestRow > 0) ? kTopGap : 0);
        }
    }
    //详情头部的状态胶囊
    if (m_detailHeader != NULL && m_detailChip != NULL && m_detailChipText != NULL)
    {
        const int width = MeasureTextWidth(font, std::wstring(m_detailChipText->GetText().GetString())) + 2 * kChipPadding;
        m_detailHeader->SetHint(m_detailChip, Hint().Fixed(width, kChipHeight).AlignC(DuiLayout::AlignCenter));
    }
    //详情卡片的最小宽度：设计宽度放得下按钮最多的一组时为设计宽度，否则加宽到正好放下
    const int needed = widestRow + 2 * kDetailCardPadding;
    m_minDetailWidth = (needed > kDetailWidth) ? needed : kDetailWidth;
    UpdateDetailCardWidth();
}

void CShareCenterWindow::UpdateDetailCardWidth()
{
    typedef DuiLayout::Hint Hint;
    if (m_body == NULL || m_detailCard == NULL || !::IsWindow(m_hWnd))
        return;
    //窗口比默认宽度宽时，多出的宽度一部分分给详情，最近操作等长文字能显示得更完整；不比默认宽时保持最小宽度
    RECT rc;
    GetClientRect(&rc);
    const int extra = (rc.right - rc.left) - kContentWidth;
    const int width = m_minDetailWidth + ((extra > 0) ? extra / kDetailExtraDiv : 0);
    m_body->SetHint(m_detailCard, Hint().Fixed(width));
}

LRESULT CShareCenterWindow::OnSizeMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //只更新详情卡片的宽度 hint，不拦截消息：基类（DuiHost）随后按新尺寸整体重新布局。最小化时客户区为 0，不必处理
    bHandled = FALSE;
    if (wParam != SIZE_MINIMIZED)
        UpdateDetailCardWidth();
    RememberNormalRect();
    return 0;
}

LRESULT CShareCenterWindow::OnMoveMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    RememberNormalRect();
    return 0;
}

void CShareCenterWindow::RememberNormalRect()
{
    if (!IsWindow() || IsIconic() || IsZoomed())
        return;
    RECT rc;
    if (!GetWindowRect(&rc) || rc.right <= rc.left || rc.bottom <= rc.top)
        return;
    m_lastNormalRect = rc;
    m_hasNormalRect = true;
}

bool CShareCenterWindow::ApplySavedPlacement(HWND anchor)
{
    m_hasNormalRect = false;
    m_placementIni.clear();
    FileShareSessionInfo info;
    ::ZeroMemory(&info, sizeof(info));
    info.m_cbSize = sizeof(info);
    if (m_host != NULL && m_host->GetSessionInfo(&info))
        m_placementIni = SharePlacementIniPath(info.m_personalDir);

    winplace::WindowPlacementPrefs saved;
    LoadSharePlacement(m_placementIni.c_str(), kShareCenterPlacementSection, saved);
    //默认大小取 ResizeClient 之后的整窗大小；最小大小与 SetMinSize 一致，都是物理像素
    RECT current;
    GetWindowRect(&current);
    SIZE defaultSize;
    defaultSize.cx = current.right - current.left;
    defaultSize.cy = current.bottom - current.top;
    SIZE minSize;
    minSize.cx = kMinWindowWidth;
    minSize.cy = kMinWindowHeight;
    const RECT rc = ResolveSharePlacementRect(saved, defaultSize, minSize, anchor);
    SetWindowPos(NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
    LOG_INFO("share center window: placement saved=%d -> (%ld,%ld %ldx%ld), maximized %d.",
             saved.m_hasSaved ? 1 : 0, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
             saved.m_maximized ? 1 : 0);
    return saved.m_hasSaved && saved.m_maximized;
}

void CShareCenterWindow::SavePlacement()
{
    if (!m_hasNormalRect || m_placementIni.empty())
        return;
    winplace::WindowPlacementPrefs prefs;
    prefs.m_hasSaved = true;
    prefs.m_rect = m_lastNormalRect;
    //最小化时关窗（例如在任务栏上右键关闭）：按最小化之前是否最大化来记
    WINDOWPLACEMENT wp;
    wp.length = sizeof(wp);
    const bool restoreToMax = IsIconic() && GetWindowPlacement(&wp) && (wp.flags & WPF_RESTORETOMAXIMIZED) != 0;
    prefs.m_maximized = (IsZoomed() != FALSE) || restoreToMax;
    const bool ok = SaveSharePlacement(m_placementIni.c_str(), kShareCenterPlacementSection, prefs);
    LOG_INFO("share center window: save placement (%ld,%ld %ldx%ld), maximized %d, ok %d.",
             prefs.m_rect.left, prefs.m_rect.top, prefs.m_rect.right - prefs.m_rect.left,
             prefs.m_rect.bottom - prefs.m_rect.top, prefs.m_maximized ? 1 : 0, ok ? 1 : 0);
}

void CShareCenterWindow::UpdateTabButtons()
{
    if (m_tabMine == NULL || m_tabShared == NULL)
        return;
    //列表到达之后（请求编号清零）才带个数，加载中不显示可能过时的 0
    m_tabMine->SetText(TabText(TR(kTabMine), m_mine.size(), m_mineSeq == 0 && m_service != NULL).c_str());
    m_tabShared->SetText(TabText(TR(kTabShared), m_shared.size(), m_sharedSeq == 0 && m_service != NULL).c_str());
    m_tabMine->SetVariant(m_tab == share_center_tab_mine ? DuiButton::Variant::Primary : DuiButton::Variant::Default);
    m_tabShared->SetVariant(m_tab == share_center_tab_shared ? DuiButton::Variant::Primary : DuiButton::Variant::Default);
    //搜索提示随页签切换：「共享给我的」里的条目不带分享者的真实路径，提示里不写「路径」
    if (m_search != NULL)
        m_search->SetPlaceholder(TR(m_tab == share_center_tab_mine ? kSearchHintMine : kSearchHintShared));
    //文字（个数）变了宽度也要跟着变
    UpdateButtonWidths(m_tabMine->GetDefaultFont());
}

LRESULT CShareCenterWindow::OnDpiChangedMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //基类（DuiHost）随后把新 DPI 通知给各控件并整体重新布局，这里只更新各按钮与详情卡片的宽度 hint，不拦截消息。
    //系统给出的横纵两个 DPI 相同，取横向的那个
    bHandled = FALSE;
    const int dpi = LOWORD(wParam);
    if (dpi > 0)
        UpdateButtonWidths(balloonwjui::DuiResMgr::Inst().GetDefaultFontForDpi(dpi));
    return 0;
}

const std::vector<ShareListEntry>& CShareCenterWindow::Rows() const
{
    return m_visible;
}

void CShareCenterWindow::RebuildVisible()
{
    //按 shareid 记住当前选中的共享，重算之后它仍在结果里就继续选中
    const int64_t selectedId = (m_selected >= 0 && static_cast<size_t>(m_selected) < m_visible.size())
        ? m_visible[static_cast<size_t>(m_selected)].m_shareId : 0;
    const std::vector<ShareListEntry>& source = (m_tab == share_center_tab_mine) ? m_mine : m_shared;
    const int64_t selfId = (m_service != NULL) ? m_service->GetUserId() : 0;
    m_visible.clear();
    for (size_t i = 0; i < source.size(); ++i)
    {
        const ShareListEntry& e = source[i];
        //没有关键字时不必取会话名与分享者名
        if (!m_filter.empty())
        {
            const std::wstring conv = ConversationName(e.m_ctype, ShareCenterModel::ViewerCid(e, selfId));
            const std::wstring owner = (m_tab == share_center_tab_shared) ? UserName(e.m_ownerId) : std::wstring();
            if (!ShareCenterModel::MatchesFilter(e, conv, owner, m_filter))
                continue;
        }
        m_visible.push_back(e);
    }
    m_selected = -1;
    for (size_t i = 0; i < m_visible.size() && selectedId != 0; ++i)
    {
        if (m_visible[i].m_shareId == selectedId)
            m_selected = static_cast<int>(i);
    }
}

void CShareCenterWindow::SetFilter(const std::wstring& keyword)
{
    if (keyword == m_filter)
        return;
    m_filter = keyword;
    RebuildVisible();
    RefreshList();
    RebuildDetail();
}

size_t CShareCenterWindow::GetVisibleCount() const
{
    return m_visible.size();
}

void CShareCenterWindow::RefreshList()
{
    if (m_list == NULL)
        return;
    m_list->SetRowCount(static_cast<int>(Rows().size()));
    m_list->SetCurSel(m_selected, false);
    //设了行数之后按当前矩形重新排版，滚动条才会按新的行数摆放（balloonui 2026-10-03 已在设行数时自行处理，这里保留无害）
    m_list->ForceLayout(m_list->GetRect());
    m_list->Invalidate();
}

std::wstring CShareCenterWindow::ConversationName(int ctype, int64_t cid)
{
    const std::pair<int, int64_t> key(ctype, cid);
    std::map<std::pair<int, int64_t>, std::wstring>::const_iterator it = m_convNames.find(key);
    if (it != m_convNames.end())
        return it->second;
    char buf[kFileShareTextBytes] = { 0 };
    std::wstring name;
    if (m_host != NULL && m_host->GetConversationName(ctype, cid, buf, kFileShareTextBytes))
        name = Wide(buf);
    //取不到时不缓存：群资料可能稍后才到
    if (!name.empty())
        m_convNames[key] = name;
    return name;
}

std::wstring CShareCenterWindow::UserName(int64_t userId)
{
    char buf[kFileShareTextBytes] = { 0 };
    if (m_host != NULL && m_host->GetUserDisplayName(userId, buf, kFileShareTextBytes))
        return Wide(buf);
    return std::wstring();
}

void CShareCenterWindow::RebuildDetail()
{
    m_detailRows.clear();
    m_avatars.SetHost(m_host);
    const bool hasRow = (m_selected >= 0 && static_cast<size_t>(m_selected) < Rows().size());
    const int mask = GetActionMask();

    //操作按钮：按可做的操作显隐，显隐变化后对按钮所在的行重排（UI 约定第 5 条）
    DuiButton* buttons[] = { m_btnOpenLocal, m_btnOpen, m_btnEdit, m_btnCancel };
    const int flags[] = { share_center_action_open_local, share_center_action_open, share_center_action_edit,
                          share_center_action_cancel };
    bool changed = false;
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i)
    {
        if (buttons[i] == NULL)
            continue;
        const bool show = (mask & flags[i]) != 0;
        if (buttons[i]->IsVisible() != show)
        {
            buttons[i]->SetVisible(show);
            changed = true;
        }
    }
    if (changed && m_actionRow != NULL)
        m_actionRow->ForceLayout(m_actionRow->GetRect());

    const ShareListEntry* e = hasRow ? &Rows()[static_cast<size_t>(m_selected)] : NULL;
    const int64_t shownId = (e != NULL) ? e->m_shareId : 0;
    const bool otherShare = (shownId != m_detailShareId);
    m_detailShareId = shownId;
    //「我的共享」里选中的共享下面会重新算出摘要；其余情况没有需要定时刷新的内容
    m_liveSignature.clear();
    std::vector<ShareActiveVisitor> visitorsForSignature;
    UpdateDetailHeader(e);
    if (e != NULL)
    {
        const std::wstring conv = ConversationName(e->m_ctype, ShareCenterModel::ViewerCid(*e, m_service->GetUserId()));
        const bool canLocate = (mask & share_center_action_locate) != 0;
        const std::wstring kind = ShareCenterModel::KindText(e->m_kind);
        //路径显示在头部名称下面
        if (!e->m_displayPath.empty())
            AddHeaderOnly(std::wstring(TR(kPathPrefix)) + Wide(e->m_displayPath));

        //基本信息：「共享给我的」先列分享者（头像、名字，名字下面注明「分享者」），再是两列的信息行
        AddSection(TR(kSecInfo));
        if (m_tab == share_center_tab_shared)
        {
            const std::wstring owner = UserName(e->m_ownerId);
            AddPerson(e->m_ownerId, owner, TR(kOwnerSub), std::vector<std::wstring>(), kNoProgress,
                      std::wstring(TR(kOwnerPrefix)) + owner);
        }
        AddField(TR(kFieldKind), kind, false, std::wstring(TR(kKindPrefix)) + kind);
        if (!conv.empty())
            AddField(TR(kFieldConv), conv, canLocate, std::wstring(TR(kConvPrefix)) + conv);
        std::wstring state = ShareConversationModel::StatusText(e->m_state, e->m_reason);
        if (m_tab == share_center_tab_mine)
        {
            const std::wstring local = ShareCenterModel::LocalStatusText(m_service->GetLocalStatus(e->m_shareId));
            if (!local.empty())
                state += L"（" + local + L"）";
        }
        else
        {
            const std::wstring perms = ShareCenterModel::PermsText(e->m_myPerms, e->m_kind);
            AddField(TR(kFieldPerms), perms, false, std::wstring(TR(kPermsPrefix)) + perms);
        }
        AddField(TR(kFieldState), state, false, std::wstring(TR(kStatePrefix)) + state);

        if (m_tab == share_center_tab_mine)
        {
            //成员与权限：头像、名字，名字下面是权限胶囊
            AddSection(SectionTitle(TR(kSecMembers), e->m_members.size()));
            if (e->m_members.empty())
                AddEmpty(TR(kNoMembers));
            for (size_t i = 0; i < e->m_members.size(); ++i)
            {
                const ShareMemberPerm& m = e->m_members[i];
                const std::wstring name = UserName(m.m_userId);
                AddPerson(m.m_userId, name, std::wstring(), ShareCenterModel::PermChipTexts(m.m_perms, e->m_kind), kNoProgress,
                          name + L"：" + ShareCenterModel::PermsText(m.m_perms, e->m_kind));
            }

            //正在访问的人：头像、名字，名字下面是正在做什么，有传输进度时右边画进度条
            const std::vector<ShareActiveVisitor> visitors = m_service->GetActiveVisitors(e->m_shareId);
            visitorsForSignature = visitors;
            AddSection(SectionTitle(TR(kSecVisitors), visitors.size()));
            if (visitors.empty())
                AddEmpty(TR(kNoVisitors));
            for (size_t i = 0; i < visitors.size(); ++i)
            {
                const ShareActiveVisitor& v = visitors[i];
                const std::wstring known = UserName(v.m_visitorId);
                const std::wstring name = known.empty() ? Wide(v.m_visitorName) : known;
                int progress = kNoProgress;
                if (v.m_bytesTotal > 0)
                {
                    const int64_t done = (v.m_bytesDone < v.m_bytesTotal) ? v.m_bytesDone : v.m_bytesTotal;
                    progress = static_cast<int>(done * kPercentFull / v.m_bytesTotal);
                }
                AddPerson(v.m_visitorId, name, ShareCenterModel::VisitorStatusText(v), std::vector<std::wstring>(), progress,
                          ShareCenterModel::VisitorText(v, known));
            }

            //最近操作：左列时间、右列内容，失败的红字
            AddSection(TR(kSecActivities));
            std::vector<ShareActivityRecord> activities;
            m_service->ListActivities(e->m_shareId, kActivityLines, activities);
            m_liveSignature = LiveSignature(visitorsForSignature, activities);
            if (activities.empty())
                AddEmpty(TR(kNoActivities));
            for (size_t i = 0; i < activities.size(); ++i)
            {
                const ShareActivityRecord& rec = activities[i];
                const std::wstring who = UserName(rec.m_visitorId);
                const std::wstring time = ShareBrowseModel::FormatTime(rec.m_time);
                AddActivity(time, ShareCenterModel::ActivityText(rec, who, std::wstring()), rec.m_result != error_code_ok,
                            ShareCenterModel::ActivityText(rec, who, time));
            }
        }
    }
    BuildDetailControls();
    //换了一个共享时回到顶部；同一个共享定时刷新（正在访问的人）时保持用户滚到的位置
    if (otherShare && m_detailScroll != NULL)
        m_detailScroll->SetScrollPos(0);
}

void CShareCenterWindow::UpdateDetailHeader(const ShareListEntry* e)
{
    typedef DuiLayout::Hint Hint;
    if (m_detailHeader == NULL || m_detailIcon == NULL || m_detailTitle == NULL || m_detailPath == NULL || m_detailChip == NULL
        || m_detailChipText == NULL)
        return;
    //换图标：先让控件不再借用旧位图，再释放它
    m_detailIcon->SetBitmap(NULL);
    if (m_headerIconBmp != NULL)
    {
        ::DeleteObject(m_headerIconBmp);
        m_headerIconBmp = NULL;
    }
    const bool has = (e != NULL);
    if (has)
    {
        //类型图标：单个文件按显示路径（没有时按名称）的扩展名取，磁盘与文件夹与名字无关
        m_headerIconBmp = shellicon::CreateItemIconBitmap(e->m_kind, Wide(e->m_displayPath.empty() ? e->m_name : e->m_displayPath),
                                                          kDetailIconPx);
        m_detailIcon->SetBitmap(m_headerIconBmp);
        m_detailTitle->SetText(Wide(e->m_name).c_str());
        m_detailTitle->SetTextPointSize(kDetailTitlePt, true);
        m_detailTitle->SetTextColor(e->m_state == share_state_invalid ? kInvalidColor : kTextColor);
        m_detailPath->SetText(Wide(e->m_displayPath).c_str());
        COLORREF bg = kChipGreyBg;
        COLORREF fg = kChipGreyText;
        StateChipColors(e->m_state, bg, fg);
        const std::wstring chip = ShareCenterModel::StateChipText(e->m_state);
        m_detailChipText->SetText(chip.c_str());
        m_detailChipText->SetTextColor(fg);
        m_detailChip->SetBgColor(bg);
        const int width = MeasureTextWidth(m_detailChipText->GetDefaultFont(), chip) + 2 * kChipPadding;
        m_detailHeader->SetHint(m_detailChip, Hint().Fixed(width, kChipHeight).AlignC(DuiLayout::AlignCenter));
    }
    else
    {
        //没有选中时是一句提示，不加粗
        m_detailTitle->SetText(TR(kNoSelection));
        m_detailTitle->SetTextPointSize(kDetailTitlePt, false);
        m_detailTitle->SetTextColor(kWeakTextColor);
        m_detailPath->SetText(L"");
    }
    //图标与状态胶囊只在选中时显示；显隐变化后对头部这一行重排（UI 约定第 5 条）
    DuiControl* parts[] = { m_detailIcon, m_detailChip };
    bool changed = false;
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i)
    {
        if (parts[i]->IsVisible() != has)
        {
            parts[i]->SetVisible(has);
            changed = true;
        }
    }
    if (changed)
        m_detailHeader->ForceLayout(m_detailHeader->GetRect());
    m_detailHeader->Invalidate();
}

void CShareCenterWindow::BuildDetailControls()
{
    typedef DuiLayout::Hint Hint;
    if (m_detailScroll == NULL)
        return;
    ++m_detailBuildCount;
    const HFONT font = m_detailScroll->GetDefaultFont();
    //内容根容器自带白底，CWhiteScrollView 让它至少铺满视口
    std::unique_ptr<DuiVBox> lines(new DuiVBox());
    lines->SetBgColor(kBgColor);
    int contentHeight = 0;
    //左列宽度按字体量出：DPI 较高时字体变大，固定宽度会让右列内容紧贴左列文字或把时间截断
    int labelWidth = kFieldLabelW;
    int timeWidth = kActivityTimeW;
    for (size_t i = 0; i < m_detailRows.size(); ++i)
    {
        const DetailRow& row = m_detailRows[i];
        if (row.m_kind != detail_row_field && row.m_kind != detail_row_activity)
            continue;
        const int width = MeasureTextWidth(font, row.m_label) + kColumnGap;
        int& column = (row.m_kind == detail_row_field) ? labelWidth : timeWidth;
        if (width > column)
            column = width;
    }
    UINT avatarId = kIdDetailAvatarBase;
    for (size_t i = 0; i < m_detailRows.size(); ++i)
    {
        const DetailRow& row = m_detailRows[i];
        switch (row.m_kind)
        {
        //只在头部显示的内容（路径）：滚动区里不画
        case detail_row_header:
            break;

        //信息行：左列灰色标签 | 右列深色值 | 所在会话一行右边的「定位到会话」链接
        case detail_row_field:
        {
            std::unique_ptr<DuiHBox> field(new DuiHBox());
            field->AddChild(std::unique_ptr<DuiControl>(
                                MakeTextLabel(row.m_label, kWeakTextColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                            Hint().Fixed(labelWidth));
            field->AddChild(std::unique_ptr<DuiControl>(
                                MakeTextLabel(row.m_value, kTextColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS).release()),
                            Hint().Weight(1));
            if (row.m_link)
            {
                std::unique_ptr<DuiLabel> link = MakeTextLabel(TR(kLocateText), kTextColor, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                link->SetMode(DuiLabel::ModeLink);
                link->SetCtrlId(kIdLocate);
                field->AddChild(std::unique_ptr<DuiControl>(link.release()),
                                Hint().Fixed(MeasureTextWidth(font, TR(kLocateText)) + kLinkPad));
            }
            lines->AddChild(std::unique_ptr<DuiControl>(field.release()), Hint().Fixed(kFieldH));
            contentHeight += kFieldH;
            break;
        }

        //小节标题：加粗的标题，下方一条细分隔线
        case detail_row_section:
        {
            std::unique_ptr<DuiVBox> section(new DuiVBox());
            std::unique_ptr<DuiLabel> title = MakeTextLabel(row.m_text, kTextColor, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
            title->SetTextPointSize(kSectionTitlePt, true);
            section->AddChild(std::unique_ptr<DuiControl>(title.release()), Hint().Weight(1));
            std::unique_ptr<balloonwjui::DuiSeparator> line(new balloonwjui::DuiSeparator());
            line->SetColor(kSeparatorColor);
            section->AddChild(std::unique_ptr<DuiControl>(line.release()), Hint().Fixed(kSeparatorH));
            lines->AddChild(std::unique_ptr<DuiControl>(section.release()), Hint().Fixed(kSectionH));
            contentHeight += kSectionH;
            break;
        }

        //小节里没有内容：弱色提示
        case detail_row_empty:
            lines->AddChild(std::unique_ptr<DuiControl>(
                                MakeTextLabel(row.m_text, kWeakTextColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS).release()),
                            Hint().Fixed(kFieldH));
            contentHeight += kFieldH;
            break;

        //人的一行：头像（正方形、垂直居中）| 间距 | 名字在上、权限胶囊或说明在下 | 传输进度条
        case detail_row_person:
        {
            std::unique_ptr<DuiHBox> person(new DuiHBox());
            std::unique_ptr<balloonwjui::DuiAvatar> avatar = m_avatars.MakeAvatar(row.m_userId, kDetailAvatarPx);
            avatar->SetCtrlId(avatarId++);
            person->AddChild(std::unique_ptr<DuiControl>(avatar.release()),
                             Hint().Fixed(kDetailAvatarPx, kDetailAvatarPx).AlignC(DuiLayout::AlignCenter));
            person->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Fixed(kDetailAvatarGap));
            std::unique_ptr<DuiVBox> texts(new DuiVBox());
            texts->AddChild(std::unique_ptr<DuiControl>(
                                MakeTextLabel(row.m_name, kTextColor, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS).release()),
                            Hint().Weight(1));
            if (!row.m_chips.empty())
            {
                std::unique_ptr<DuiHBox> chips(new DuiHBox());
                chips->SetGap(kPermChipGap);
                for (size_t c = 0; c < row.m_chips.size(); ++c)
                {
                    int width = 0;
                    std::unique_ptr<DuiVBox> chip = MakeChip(row.m_chips[c], kChipGreyBg, kChipGreyText, kPermChipH, kPermChipPad,
                                                             font, width);
                    chips->AddChild(std::unique_ptr<DuiControl>(chip.release()),
                                    Hint().Fixed(width, kPermChipH).AlignC(DuiLayout::AlignCenter));
                }
                chips->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
                texts->AddChild(std::unique_ptr<DuiControl>(chips.release()), Hint().Weight(1));
            }
            else
            {
                texts->AddChild(std::unique_ptr<DuiControl>(
                                    MakeTextLabel(row.m_sub, kWeakTextColor, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS).release()),
                                Hint().Weight(1));
            }
            person->AddChild(std::unique_ptr<DuiControl>(texts.release()), Hint().Weight(1));
            if (row.m_progress >= 0)
            {
                std::unique_ptr<balloonwjui::DuiProgressBar> bar(new balloonwjui::DuiProgressBar());
                bar->SetRange(0, kPercentFull);
                bar->SetPos(row.m_progress, false);
                person->AddChild(std::unique_ptr<DuiControl>(bar.release()),
                                 Hint().Fixed(kProgressW, kProgressH).AlignC(DuiLayout::AlignCenter));
            }
            lines->AddChild(std::unique_ptr<DuiControl>(person.release()), Hint().Fixed(kPersonRowH));
            contentHeight += kPersonRowH;
            break;
        }

        //最近操作：左列时间（弱色）| 右列内容（失败的红字）
        case detail_row_activity:
        {
            std::unique_ptr<DuiHBox> activity(new DuiHBox());
            activity->AddChild(std::unique_ptr<DuiControl>(
                                   MakeTextLabel(row.m_label, kWeakTextColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE).release()),
                               Hint().Fixed(timeWidth));
            std::unique_ptr<DuiLabel> content = MakeTextLabel(row.m_value, row.m_failed ? kErrorColor : kLineTextColor,
                                                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            //路径很长时这一行放不下、末尾显示省略号，鼠标停在上面时显示全文（控件销毁时 DuiControl 自动取消注册）
            balloonwjui::DuiToolTipMgr::Inst().Register(content.get(), row.m_value.c_str());
            activity->AddChild(std::unique_ptr<DuiControl>(content.release()), Hint().Weight(1));
            lines->AddChild(std::unique_ptr<DuiControl>(activity.release()), Hint().Fixed(kActivityH));
            contentHeight += kActivityH;
            break;
        }

        //不认识的种类：不画
        default:
            break;
        }
    }
    CWhiteScrollView* scroll = static_cast<CWhiteScrollView*>(m_detailScroll);
    scroll->SetContent(std::unique_ptr<DuiControl>(lines.release()));
    scroll->SetNaturalHeight(contentHeight);
    scroll->ForceLayout(scroll->GetRect());
    scroll->Invalidate();
}

bool CShareCenterWindow::StartDirectShare()
{
    if (m_ui == NULL)
        return false;
    //从聊天窗打开的：直接在那个会话里发起，不弹选人窗口、不建群；成功后前置那个聊天窗
    if (m_launchCid > 0)
    {
        m_ui->UiOpenCreateShare(m_hWnd, m_launchCtype, m_launchCid, true);
        LOG_INFO("share center window: start share in launch conversation, ctype: %d, cid: %lld.", m_launchCtype,
                 static_cast<long long>(m_launchCid));
        return true;
    }
    const bool ok = m_ui->UiStartDirectShare(m_hWnd);
    LOG_INFO("share center window: start direct share, ok: %d.", ok ? 1 : 0);
    if (!ok)
        ShowToast(TR(kPickFailText), true);
    return ok;
}

void CShareCenterWindow::SetLaunchConversation(int ctype, int64_t cid)
{
    m_launchCtype = (cid > 0) ? ctype : 0;
    m_launchCid = (cid > 0) ? cid : 0;
}

int64_t CShareCenterWindow::GetLaunchCid() const
{
    return m_launchCid;
}

bool CShareCenterWindow::OpenLocal(int64_t shareId)
{
    //与点本人的共享卡片共用同一份实现；打不开时由调用方弹出浮层提示说明原因
    return OpenShareLocally(m_hWnd, m_service, shareId) == share_local_open_ok;
}

void CShareCenterWindow::ShowToast(const std::wstring& text, bool isError)
{
    m_toastText = text;
    if (m_toast == NULL)
        return;
    m_toast->SetBgColor(isError ? kErrorColor : kToastInfoBg);
    m_toast->SetDurationMs(isError ? kErrorToastMs : kToastMs);
    m_toast->Show(text.c_str());
}

void CShareCenterWindow::PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover)
{
    //选中与悬停的底色由画行回调自己画（DuiVirtualList 只告诉回调这两种状态）：比行矩形四边各收进一点的圆角底色，
    //选中时左侧再加一条品牌蓝竖条
    if (selected || hover)
    {
        const RECT bg = { rowRect.left + kRowInset, rowRect.top + kRowInset, rowRect.right - kRowInset, rowRect.bottom - kRowInset };
        balloonwjui::DuiAA::FillRoundRect(hdc, bg, selected ? kSelectedBg : kHoverBg, kRowRadius);
        if (selected)
        {
            const RECT bar = { bg.left, bg.top + kRowRadius, bg.left + kAccentBarWidth, bg.bottom - kRowRadius };
            balloonwjui::DuiAA::FillRoundRect(hdc, bar, kAccentColor, kAccentBarWidth / 2);
        }
    }
    static_cast<CShareCenterWindow*>(user)->PaintRow(hdc, rowIndex, rowRect);
}

void CShareCenterWindow::RowClickThunk(void* user, int rowIndex, bool isDoubleClick)
{
    CShareCenterWindow* self = static_cast<CShareCenterWindow*>(user);
    self->SelectRow(rowIndex);
    //「共享给我的」双击直接打开
    if (isDoubleClick && self->m_tab == share_center_tab_shared)
        self->DoAction(share_center_action_open, false);
}

void CShareCenterWindow::PaintRow(HDC hdc, int rowIndex, const RECT& rowRect)
{
    if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= Rows().size() || m_list == NULL || m_service == NULL)
        return;
    const ShareListEntry& e = Rows()[static_cast<size_t>(rowIndex)];
    const bool mine = (m_tab == share_center_tab_mine);
    const std::wstring conv = ConversationName(e.m_ctype, ShareCenterModel::ViewerCid(e, m_service->GetUserId()));
    const std::wstring owner = mine ? std::wstring() : UserName(e.m_ownerId);
    const std::wstring sub = mine ? ShareCenterModel::MineRowSubtitle(e, conv) : ShareCenterModel::SharedRowSubtitle(e, owner);

    //行首的系统类型图标：磁盘、文件夹，单个文件按显示路径（没有时按名称）的扩展名取
    const int iconTop = rowRect.top + (rowRect.bottom - rowRect.top - kRowIconSize) / 2;
    RECT icon = { rowRect.left + kCellPadding, iconTop, rowRect.left + kCellPadding + kRowIconSize, iconTop + kRowIconSize };
    shellicon::DrawItemIcon(hdc, e.m_kind, Wide(e.m_displayPath.empty() ? e.m_name : e.m_displayPath), icon);
    const int textLeft = icon.right + kRowIconGap;

    HFONT oldFont = static_cast<HFONT>(::SelectObject(hdc, m_list->GetDefaultFont()));
    const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
    const int mid = rowRect.top + (rowRect.bottom - rowRect.top) / kLineHalf;

    //第一行右端的胶囊，从右往左排：状态，再是「我的共享」的本机标注（位于另一台电脑、本机缺少记录）
    int chipRight = rowRect.right - kCellPadding;
    const int chipTop = mid - kChipHeight;
    const bool invalid = (e.m_state == share_state_invalid);
    COLORREF stateBg = kChipGreyBg;
    COLORREF stateFg = kChipGreyText;
    StateChipColors(e.m_state, stateBg, stateFg);
    int left = DrawChip(hdc, ShareCenterModel::StateChipText(e.m_state), stateBg, stateFg, chipRight, chipTop);
    if (left != chipRight)
        chipRight = left - kChipGap;
    if (mine)
    {
        const std::wstring local = ShareCenterModel::LocalStatusText(m_service->GetLocalStatus(e.m_shareId));
        left = DrawChip(hdc, local, kChipGreyBg, kChipGreyText, chipRight, chipTop);
        if (left != chipRight)
            chipRight = left - kChipGap;
    }

    //名称：画到胶囊左边为止，已失效的用浅灰
    RECT nameRect = { textLeft, rowRect.top, chipRight, mid };
    ::SetTextColor(hdc, invalid ? kInvalidColor : kTextColor);
    const std::wstring name = Wide(e.m_name);
    ::DrawTextW(hdc, name.c_str(), -1, &nameRect, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    //第二行：「共享给我的」行首先画分享者的小头像（位图归 m_avatars 所有，这里只借用）
    int subLeft = textLeft;
    if (!mine && e.m_ownerId > 0)
    {
        HBITMAP avatar = m_avatars.GetBitmap(e.m_ownerId, kOwnerAvatarPx);
        if (avatar != NULL)
        {
            const int avatarTop = mid + (rowRect.bottom - mid - kOwnerAvatarPx) / 2 - kRowInset;
            HDC mem = ::CreateCompatibleDC(hdc);
            HGDIOBJ oldBmp = ::SelectObject(mem, avatar);
            BLENDFUNCTION blend = { AC_SRC_OVER, 0, kOpaqueAlpha, AC_SRC_ALPHA };
            ::AlphaBlend(hdc, subLeft, avatarTop, kOwnerAvatarPx, kOwnerAvatarPx, mem, 0, 0, kOwnerAvatarPx, kOwnerAvatarPx,
                         blend);
            ::SelectObject(mem, oldBmp);
            ::DeleteDC(mem);
            subLeft += kOwnerAvatarPx + kOwnerAvatarGap;
        }
    }
    RECT subRect = { subLeft, mid, rowRect.right - kCellPadding, rowRect.bottom };
    ::SetTextColor(hdc, kWeakTextColor);
    ::DrawTextW(hdc, sub.c_str(), -1, &subRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    ::SetBkMode(hdc, oldMode);
    ::SelectObject(hdc, oldFont);
}

void CShareCenterWindow::PaintEmptyThunk(void* user, HDC hdc, const RECT& listRect)
{
    static_cast<CShareCenterWindow*>(user)->PaintEmpty(hdc, listRect);
}

void CShareCenterWindow::PaintEmpty(HDC hdc, const RECT& listRect)
{
    //三种情况：列表还在加载；有搜索关键字但没有匹配；当前页签确实没有共享
    const bool mine = (m_tab == share_center_tab_mine);
    const bool loading = (mine ? m_mineSeq : m_sharedSeq) != 0;
    const wchar_t* title = loading ? TR(kLoadingText) : (!m_filter.empty() ? TR(kNoMatchText) : (mine ? TR(kEmptyMine) : TR(kEmptyShared)));
    const wchar_t* hint = loading ? NULL : (!m_filter.empty() ? TR(kNoMatchHint) : (mine ? TR(kEmptyMineHint) : TR(kEmptySharedHint)));

    //图标、标题、说明三段作为一块，放在列表高度的三分之一处（视觉上略偏上，比正中更自然）；加载中只有一行字
    const int iconBlock = loading ? 0 : kEmptyIconSize;
    const int blockH = iconBlock + kEmptyLineH + ((hint != NULL) ? kEmptyLineH : 0);
    int top = listRect.top + (listRect.bottom - listRect.top - blockH) / kEmptyTopDivisor;
    const int centerX = (listRect.left + listRect.right) / 2;
    if (!loading)
    {
        RECT icon = { centerX - kEmptyIconSize / 2, top, centerX + kEmptyIconSize / 2, top + kEmptyIconSize };
        shellicon::DrawItemIcon(hdc, share_kind_folder, std::wstring(), icon);
        top += kEmptyIconSize;
    }
    HFONT oldFont = static_cast<HFONT>(::SelectObject(hdc, m_list->GetDefaultFont()));
    const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
    RECT titleRect = { listRect.left + kCellPadding, top, listRect.right - kCellPadding, top + kEmptyLineH };
    ::SetTextColor(hdc, kTextColor);
    ::DrawTextW(hdc, title, -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (hint != NULL)
    {
        RECT hintRect = { titleRect.left, titleRect.bottom, titleRect.right, titleRect.bottom + kEmptyLineH };
        ::SetTextColor(hdc, kWeakTextColor);
        ::DrawTextW(hdc, hint, -1, &hintRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    ::SetBkMode(hdc, oldMode);
    ::SelectObject(hdc, oldFont);
}

} // namespace fileshare
