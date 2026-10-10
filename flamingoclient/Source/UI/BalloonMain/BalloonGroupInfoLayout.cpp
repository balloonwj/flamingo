/**
 *  BalloonGroupInfoLayout.cpp
 *
 *  「群资料」对话框客户区控件树构造 + 数据注入 + 群名片双态切换的实现。
 *  程序化组合 balloonui 现成控件(DuiVBox/DuiHBox/DuiAvatar/DuiLabel/
 *  DuiSwitch/DuiEdit/DuiButton/DuiSeparator),与 BalloonSystemSettingDlg
 *  同套路;唯一的"差一小步"补齐是公告浅色填充框 —— 子类化 DuiVBox 覆写
 *  OnPaint 画圆角底(详见 CRoundedFillBox),不改 balloonui 库。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 两个工程共用;在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-06-08
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

#include "BalloonGroupInfoLayout.h"
#include "MainPanelUserData.h"   // GetUserAvatarColor
#include "MainPanelGroupMenu.h"  // FormatGroupNumber（点群号复制的文字，与右键菜单「复制群号」一致）
#include "ClickableLabel.h"      // CClickableLabel：群名片行可点击的纯文本标签
#include "NotifyModeMenu.h"      // 「消息提醒」下拉框的选项（与右键二级菜单同一份顺序和文字）
#include "../../net/GroupProtocolClient.h"   // net::GroupMemberItem(ResolveXxx 用)
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox / DuiHBox
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../Common/I18n/I18nLayout.h"   // GrowFixedWidthToText：译文较长时加宽行左标签
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

namespace {

// ---- 配色(逻辑像素 / RGB) ----
const COLORREF kClrText      = RGB(0x1A, 0x1A, 0x1A);   // 主文本黑
const COLORREF kClrMuted     = RGB(0x6B, 0x6B, 0x6B);   // 次要灰
const COLORREF kClrAccent    = RGB(0xF5, 0x6A, 0x3C);   // 品牌橙(开关 on / 与系统设置一致)
const COLORREF kClrNoticeBg  = RGB(0xFF, 0xF3, 0xEC);   // 公告框浅橙底
const COLORREF kClrNoticeText= RGB(0x6B, 0x52, 0x44);   // 公告正文暖棕

// ---- 尺寸(逻辑像素) ----
const int kBannerHeight   = 64;   // 顶部 banner = 头像边长
const int kAvatarSize     = 64;
const int kAvatarRadius   = 14;   // 圆角矩形头像圆角(同个人信息 22%×64)
const int kNoticeBoxHeight= 54;   // 公告填充框高度(约容 2 行)
const int kNoticeRadius   = 8;    // 公告框圆角
const int kRowHeight      = 42;   // 名片 / 开关行高
const int kFooterHeight   = 42;   // 底部按钮行高
const int kFooterBtnHeight= 34;   // 底部按钮高

// ---- 字号(pt) ----
const int kFontGroupName  = 13;   // 群名大字
const int kFontMeta       = 9;    // 群号 / 人数 / 群主 / "群公告"小标题
const int kFontBody       = 10;   // 公告正文 / 行标签 / 值 / 按钮

// 「我在本群的昵称」行左标签的设计宽度（像素）
const int kCardLabelWidth = 130;

// 该标签按译文加宽时，文字宽度之外留出的余量（像素），避免文字贴着右侧的值
const int kCardLabelPadW = 4;

// CRoundedFillBox —— 圆角填充背景容器(子类化 DuiVBox,覆写最少虚函数)。
// balloonui 容器无背景填充能力,DuiGroupBox 只描边不填充 —— 这里"差一小步":
// 子类化 DuiVBox,OnPaint 先用 GDI RoundRect 填一个无边圆角底,再链回基类
// 让子控件(公告正文 label)画在底色之上。仅用于公告框,不动 balloonui 库。
class CRoundedFillBox : public balloonwjui::DuiVBox
{
public:
    CRoundedFillBox()
        : m_fillColor(kClrNoticeBg)
        , m_radius(kNoticeRadius)
    {
    }

    // 设置填充色与圆角半径(像素)。
    void SetFill(COLORREF fill, int radius)
    {
        m_fillColor = fill;
        m_radius = radius;
    }

    void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        RECT rc = GetRect();
        // 无边圆角填充:画刷 = 填充色,画笔 = 同色(避免可见描边)。RoundRect 右 / 下
        // 边为开区间,补 1 像素填满整矩形。
        HBRUSH br = ::CreateSolidBrush(m_fillColor);
        HPEN   pen = ::CreatePen(PS_SOLID, 1, m_fillColor);
        HGDIOBJ ob = ::SelectObject(hdc, br);
        HGDIOBJ op = ::SelectObject(hdc, pen);
        ::RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom,
                    m_radius * 2, m_radius * 2);
        ::SelectObject(hdc, op);
        ::SelectObject(hdc, ob);
        ::DeleteObject(pen);
        ::DeleteObject(br);

        // 链回基类:画子控件(公告正文 label)。
        balloonwjui::DuiVBox::OnPaint(hdc, rcDirty);
    }

private:
    COLORREF m_fillColor;   // 圆角底填充色
    int      m_radius;      // 圆角半径(像素)
};

// 造一个纯文本 DuiLabel。ctrlId 为 0 时不设 id(装饰性固定文字)。
std::unique_ptr<balloonwjui::DuiLabel> MakeLabel(LPCTSTR text, UINT ctrlId,
                                                 int pt, bool bold,
                                                 COLORREF color, DWORD align)
{
    std::unique_ptr<balloonwjui::DuiLabel> lbl(new balloonwjui::DuiLabel());
    if (text != NULL)
    {
        lbl->SetText(text);
    }
    if (ctrlId != 0)
    {
        lbl->SetCtrlId(ctrlId);
    }
    // 按磅值设字号:标签绘制时按所在窗口的 DPI 现取字体,随 DPI 变化。
    lbl->SetTextPointSize(pt, bold);
    lbl->SetTextColor(color);
    lbl->SetTextAlign(align);
    return lbl;
}

// 同 MakeLabel,但造一个可点击的 CClickableLabel(点击发 DUIN_CLICK)。群名片行
// "整行点击就地编辑"的左标签 / 值都用它。
std::unique_ptr<balloonwjui::DuiLabel> MakeClickableLabel(LPCTSTR text, UINT ctrlId,
                                                          int pt, bool bold,
                                                          COLORREF color, DWORD align)
{
    std::unique_ptr<CClickableLabel> lbl(new CClickableLabel());
    if (text != NULL)
    {
        lbl->SetText(text);
    }
    if (ctrlId != 0)
    {
        lbl->SetCtrlId(ctrlId);
    }
    lbl->SetTextPointSize(pt, bold);
    lbl->SetTextColor(color);
    lbl->SetTextAlign(align);
    return std::unique_ptr<balloonwjui::DuiLabel>(lbl.release());
}

// 往 vbox 追加一条 1px 分隔线。
void AddSeparator(balloonwjui::DuiVBox* box)
{
    std::unique_ptr<balloonwjui::DuiSeparator> sep(new balloonwjui::DuiSeparator());
    box->AddChild(std::move(sep), balloonwjui::DuiLayout::Hint().Fixed(1));
}

// 往 vbox 追加一个固定高度的透明 spacer。
void AddSpacer(balloonwjui::DuiVBox* box, int h)
{
    std::unique_ptr<balloonwjui::DuiControl> sp(new balloonwjui::DuiControl());
    box->AddChild(std::move(sp), balloonwjui::DuiLayout::Hint().Fixed(h));
}

// 构造一行"左标签 + 右侧 DuiSwitch"。switchId 决定开关控件 id;on 为初值。
std::unique_ptr<balloonwjui::DuiHBox> MakeSwitchRow(LPCTSTR title, UINT switchId, bool on)
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(12);

    row->AddChild(MakeLabel(title, 0, kFontBody, false, kClrText,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                  balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiSwitch> sw(new balloonwjui::DuiSwitch());
    sw->SetCtrlId(switchId);
    sw->SetOnColor(kClrAccent);
    sw->SetChecked(on, /*animated*/false, /*notify*/false);
    row->AddChild(std::move(sw),
                  balloonwjui::DuiLayout::Hint().Fixed(46, 24)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));
    return row;
}

// "消息提醒"下拉框的尺寸(逻辑像素):宽度放得下最长的选项"收进消息盒子且不提醒"连同右侧的
// 下拉箭头(2026-10-03 联调时 176 在 125% 缩放下被截成省略号),高度与本窗口群名片编辑框目测一致
const int kNotifyComboWidth  = 208;
const int kNotifyComboHeight = 28;

// 构造一行"左标签 + 右侧只读下拉框"。选项是群聊可用的五种提醒模式,顺序与文字取自
// NotifyModeMenu,与右键菜单的"消息提醒设置"二级菜单一致。初始不选中任何项,由
// ApplyGroupInfoDataToView 按数据置位。
std::unique_ptr<balloonwjui::DuiHBox> MakeNotifyComboRow(LPCTSTR title, UINT comboId)
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(12);

    row->AddChild(MakeLabel(title, 0, kFontBody, false, kClrText,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                  balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiComboBox> combo(new balloonwjui::DuiComboBox());
    combo->SetCtrlId(comboId);
    combo->SetEditable(false);
    const std::vector<int> modes = GetNotifyModeChoices(/*isGroup*/true);
    for (size_t i = 0; i < modes.size(); ++i)
    {
        combo->AddString(GetNotifyModeLabel(modes[i]));
    }
    row->AddChild(std::move(combo),
                  balloonwjui::DuiLayout::Hint().Fixed(kNotifyComboWidth, kNotifyComboHeight)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));
    return row;
}

// dynamic_cast 找指定 ctrlId 控件;模板省去重复 cast。
template <typename T>
T* FindCtrl(balloonwjui::DuiControl* root, UINT ctrlId)
{
    if (root == NULL)
    {
        return NULL;
    }
    return dynamic_cast<T*>(root->FindCtrlById(ctrlId));
}

} // anonymous namespace

std::unique_ptr<balloonwjui::DuiControl> BuildGroupInfoBody()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(20, 16, 20, 16);
    root->SetGap(0);

    // ---- 顶部 Banner:头像 + (群名 / 群号人数 / 群主) ----
    {
        std::unique_ptr<balloonwjui::DuiHBox> banner(new balloonwjui::DuiHBox());
        banner->SetGap(14);

        std::unique_ptr<balloonwjui::DuiAvatar> avatar(new balloonwjui::DuiAvatar());
        avatar->SetCtrlId(kIdGroupAvatar);
        avatar->SetShape(balloonwjui::DuiAvatar::ShapeRoundRect);
        avatar->SetCornerRadius(kAvatarRadius);
        banner->AddChild(std::move(avatar),
                         balloonwjui::DuiLayout::Hint().Fixed(kAvatarSize));

        std::unique_ptr<balloonwjui::DuiVBox> info(new balloonwjui::DuiVBox());
        info->SetGap(4);
        // 群名、群号两行可点：点群名复制完整群名，点群号那一行复制纯数字群号（悬停时手形光标）
        info->AddChild(MakeClickableLabel(_T(""), kIdGroupName, kFontGroupName, true, kClrText,
                                          DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                       balloonwjui::DuiLayout::Hint().Fixed(26));
        info->AddChild(MakeClickableLabel(_T(""), kIdGroupMeta, kFontMeta, false, kClrMuted,
                                          DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                       balloonwjui::DuiLayout::Hint().Fixed(18));
        info->AddChild(MakeLabel(_T(""), kIdGroupOwner, kFontMeta, false, kClrMuted,
                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                       balloonwjui::DuiLayout::Hint().Fixed(18));
        // 弹性占位,把上面三行顶到 banner 顶部。
        info->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                       balloonwjui::DuiLayout::Hint().Weight(1));
        banner->AddChild(std::move(info), balloonwjui::DuiLayout::Hint().Weight(1));

        root->AddChild(std::move(banner), balloonwjui::DuiLayout::Hint().Fixed(kBannerHeight));
    }

    AddSpacer(root.get(), 12);
    AddSeparator(root.get());
    AddSpacer(root.get(), 10);

    // ---- 群简介:小标题 + 单行正文(放不下以省略号结尾;空时显占位)。群管理「基本信息」页说明群简介「显示在
    //      群资料页」,此前这里却没有这一段(2026-10-10 补上)。整段高度 kGroupInfoIntroBlockH,窗口随之加高 ----
    {
        const int kIntroTitleH = 18;    // 小标题行高
        const int kIntroTitleGap = 4;   // 小标题与正文的间距
        const int kIntroTextH = 20;     // 正文行高
        const int kIntroBottomGap = 10; // 正文与下方群公告小标题的间距
        root->AddChild(MakeLabel(TR(L"群简介"), 0, kFontMeta, false, kClrMuted,
                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                       balloonwjui::DuiLayout::Hint().Fixed(kIntroTitleH));
        AddSpacer(root.get(), kIntroTitleGap);
        root->AddChild(MakeLabel(_T(""), kIdGroupIntro, kFontBody, false, kClrText,
                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX),
                       balloonwjui::DuiLayout::Hint().Fixed(kIntroTextH));
        AddSpacer(root.get(), kIntroBottomGap);
    }

    // ---- 群公告:小标题 + 浅橙填充圆角框(内含 wordwrap 正文) ----
    {
        root->AddChild(MakeLabel(TR(L"群公告"), 0, kFontMeta, false, kClrMuted,
                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                       balloonwjui::DuiLayout::Hint().Fixed(18));
        AddSpacer(root.get(), 6);

        std::unique_ptr<CRoundedFillBox> notice(new CRoundedFillBox());
        notice->SetFill(kClrNoticeBg, kNoticeRadius);
        notice->SetPadding(12, 8, 12, 8);

        std::unique_ptr<balloonwjui::DuiLabel> noticeText(new balloonwjui::DuiLabel());
        noticeText->SetCtrlId(kIdGroupAnnouncement);
        noticeText->SetTextPointSize(kFontBody, false);
        noticeText->SetTextColor(kClrNoticeText);
        noticeText->SetWordWrap(true);   // 多行自动换行
        notice->AddChild(std::move(noticeText), balloonwjui::DuiLayout::Hint().Weight(1));

        root->AddChild(std::move(notice),
                       balloonwjui::DuiLayout::Hint().Fixed(kNoticeBoxHeight));
    }

    AddSpacer(root.get(), 10);
    AddSeparator(root.get());

    // ---- 我在本群的昵称(整行点击就地编辑) ----
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(12);

        // 左标签可点(整行点击进编辑的一半命中区)。用 CClickableLabel,否则 ModeText
        // 标签不发 DUIN_CLICK、点了没反应。
        row->AddChild(MakeClickableLabel(TR(L"我在本群的昵称"), kIdGroupCardLabel, kFontBody,
                                         false, kClrText, DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                      balloonwjui::DuiLayout::Hint().Fixed(kCardLabelWidth));
        //译文较长时标签按文字宽度只增不减地加宽（多语言第 4 期），简体中文仍是设计宽度
        i18n::GrowFixedWidthToText(row->FindCtrlById(kIdGroupCardLabel), TR(L"我在本群的昵称"),
                                   i18n::PointToPixelHeight(kFontBody), false, kCardLabelPadW);

        // 值 view(右对齐,可点;占满右侧,覆盖整行另一半)。
        row->AddChild(MakeClickableLabel(_T(""), kIdGroupCardView, kFontBody, false,
                                         kClrText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE),
                      balloonwjui::DuiLayout::Hint().Weight(1));

        // 值 edit(初始隐藏;进编辑态时显示并占满右侧)。
        std::unique_ptr<balloonwjui::DuiEdit> edit(new balloonwjui::DuiEdit());
        edit->SetCtrlId(kIdGroupCardEdit);
        edit->SetVisible(false);
        row->AddChild(std::move(edit),
                      balloonwjui::DuiLayout::Hint().Weight(1)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));

        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowHeight));
    }

    AddSeparator(root.get());

    // ---- 消息提醒(五种模式的下拉框;2026-10-03 起取代"消息免打扰"开关,行高不变) ----
    root->AddChild(MakeNotifyComboRow(TR(L"消息提醒"), kIdGroupNotifyCombo),
                   balloonwjui::DuiLayout::Hint().Fixed(kRowHeight));
    AddSeparator(root.get());

    // ---- 置顶群聊 ----
    root->AddChild(MakeSwitchRow(TR(L"置顶群聊"), kIdGroupPinSwitch, false),
                   balloonwjui::DuiLayout::Hint().Fixed(kRowHeight));

    AddSpacer(root.get(), 10);
    AddSeparator(root.get());
    AddSpacer(root.get(), 12);

    // ---- 底部按钮:查看消息记录 / 退出群聊 / 解散群聊(等宽并排;解散只对群主显示,
    //      隐藏时水平布局跳过它,另两个按钮各占一半) ----
    {
        std::unique_ptr<balloonwjui::DuiHBox> footer(new balloonwjui::DuiHBox());
        footer->SetGap(12);

        std::unique_ptr<balloonwjui::DuiButton> btnHistory(new balloonwjui::DuiButton());
        btnHistory->SetCtrlId(kIdGroupBtnHistory);
        btnHistory->SetText(TR(L"查看消息记录"));
        btnHistory->SetVariant(balloonwjui::DuiButton::Variant::Default);
        btnHistory->SetTextPointSize(kFontBody);
        footer->AddChild(std::move(btnHistory),
                         balloonwjui::DuiLayout::Hint().Weight(1).Fixed(-1, kFooterBtnHeight)
                             .AlignC(balloonwjui::DuiLayout::AlignCenter));

        std::unique_ptr<balloonwjui::DuiButton> btnQuit(new balloonwjui::DuiButton());
        btnQuit->SetCtrlId(kIdGroupBtnQuit);
        btnQuit->SetText(TR(L"退出群聊"));
        btnQuit->SetVariant(balloonwjui::DuiButton::Variant::Default);
        btnQuit->SetTextPointSize(kFontBody);
        // 危险红字(浅红底位图由对话框 EnsureVisualResources 精修;此处先红字兜底)。
        btnQuit->SetTextColor(RGB(0xE5, 0x39, 0x35));
        footer->AddChild(std::move(btnQuit),
                         balloonwjui::DuiLayout::Hint().Weight(1).Fixed(-1, kFooterBtnHeight)
                             .AlignC(balloonwjui::DuiLayout::AlignCenter));

        std::unique_ptr<balloonwjui::DuiButton> btnDismiss(new balloonwjui::DuiButton());
        btnDismiss->SetCtrlId(kIdGroupBtnDismiss);
        btnDismiss->SetText(TR(L"解散群聊"));
        btnDismiss->SetVariant(balloonwjui::DuiButton::Variant::Default);
        btnDismiss->SetTextPointSize(kFontBody);
        // 与"退出群聊"同一套危险红样式(浅红底位图同样由对话框精修)。
        btnDismiss->SetTextColor(RGB(0xE5, 0x39, 0x35));
        // 本人角色要等群资料应答到达才知道,先隐藏;ApplyGroupInfoDataToView 按 isOwner 显示。
        btnDismiss->SetVisible(false);
        footer->AddChild(std::move(btnDismiss),
                         balloonwjui::DuiLayout::Hint().Weight(1).Fixed(-1, kFooterBtnHeight)
                             .AlignC(balloonwjui::DuiLayout::AlignCenter));

        root->AddChild(std::move(footer), balloonwjui::DuiLayout::Hint().Fixed(kFooterHeight));
    }

    return std::unique_ptr<balloonwjui::DuiControl>(root.release());
}

bool ApplyGroupInfoDataToView(balloonwjui::DuiControl* root,
                              const GroupInfoViewData& data)
{
    if (root == NULL)
    {
        return false;
    }
    balloonwjui::DuiLabel* nameLbl = FindCtrl<balloonwjui::DuiLabel>(root, kIdGroupName);
    if (nameLbl == NULL)
    {
        return false;
    }

    // ---- 头像:字形取群名首字(单字,与全局头像口径一致,见 CreateGroupDlg.cpp:395、
    //      BalloonMainPanel 群树),底色按 groupid 哈希。DuiAvatar::SetName 会对多词名
    //      取首字母(如"FlamingoIM 项目组"→"F项"),故须先 Left(1) 截成单字。----
    balloonwjui::DuiAvatar* avatar = FindCtrl<balloonwjui::DuiAvatar>(root, kIdGroupAvatar);
    if (avatar != NULL)
    {
        avatar->SetName(data.groupName.Left(1));
        avatar->SetFallbackBgColor(GetUserAvatarColor((UINT)data.groupid));
    }

    // ---- 群名 ----
    nameLbl->SetText(data.groupName);

    // ---- "群号 N · M 人" ----
    CString meta;
    meta.Format(TR(L"群号 %I64d · %d 人"), data.groupid, data.memberCount);
    balloonwjui::DuiLabel* metaLbl = FindCtrl<balloonwjui::DuiLabel>(root, kIdGroupMeta);
    if (metaLbl != NULL)
    {
        metaLbl->SetText(meta);
    }

    // ---- 群主行(显示名未知时只显"群主:") ----
    CString owner;
    owner.Format(TR(L"群主：%s"), static_cast<LPCTSTR>(data.ownerName));
    balloonwjui::DuiLabel* ownerLbl = FindCtrl<balloonwjui::DuiLabel>(root, kIdGroupOwner);
    if (ownerLbl != NULL)
    {
        ownerLbl->SetText(owner);
    }

    // ---- 群简介(空→占位) ----
    balloonwjui::DuiLabel* introLbl = FindCtrl<balloonwjui::DuiLabel>(root, kIdGroupIntro);
    if (introLbl != NULL)
    {
        if (data.intro.IsEmpty())
        {
            introLbl->SetText(TR(L"暂无群简介"));
            introLbl->SetTextColor(kClrMuted);
        }
        else
        {
            //群简介可以带换行，这里只占一行：换行换成空格，放不下的部分以省略号结尾
            CString oneLine = data.intro;
            oneLine.Replace(_T("\r\n"), _T(" "));
            oneLine.Replace(_T("\r"), _T(" "));
            oneLine.Replace(_T("\n"), _T(" "));
            introLbl->SetText(oneLine);
            introLbl->SetTextColor(kClrText);
        }
    }

    // ---- 群公告(空→占位) ----
    balloonwjui::DuiLabel* noticeLbl = FindCtrl<balloonwjui::DuiLabel>(root, kIdGroupAnnouncement);
    if (noticeLbl != NULL)
    {
        if (data.announcement.IsEmpty())
        {
            noticeLbl->SetText(TR(L"暂无群公告"));
            noticeLbl->SetTextColor(kClrMuted);
        }
        else
        {
            noticeLbl->SetText(data.announcement);
            noticeLbl->SetTextColor(kClrNoticeText);
        }
    }

    // ---- 我的群名片:view 文字(空→占位) + edit 初值(始终用真实值,空则空串) ----
    balloonwjui::DuiLabel* cardView = FindCtrl<balloonwjui::DuiLabel>(root, kIdGroupCardView);
    if (cardView != NULL)
    {
        if (data.myCard.IsEmpty())
        {
            cardView->SetText(TR(L"未设置"));
            cardView->SetTextColor(kClrMuted);
        }
        else
        {
            cardView->SetText(data.myCard);
            cardView->SetTextColor(kClrText);
        }
    }
    balloonwjui::DuiEdit* cardEdit = FindCtrl<balloonwjui::DuiEdit>(root, kIdGroupCardEdit);
    if (cardEdit != NULL)
    {
        cardEdit->SetText(data.myCard);
    }

    // ---- 提醒模式下拉框与置顶开关初值(静默,不发通知) ----
    balloonwjui::DuiComboBox* notifyCombo = FindCtrl<balloonwjui::DuiComboBox>(root, kIdGroupNotifyCombo);
    if (notifyCombo != NULL)
    {
        notifyCombo->SetCurSel(NotifyModeChoiceIndex(/*isGroup*/true, data.notifyMode), /*notify*/false);
    }
    balloonwjui::DuiSwitch* pinSw = FindCtrl<balloonwjui::DuiSwitch>(root, kIdGroupPinSwitch);
    if (pinSw != NULL)
    {
        pinSw->SetChecked(data.pinned, /*animated*/false, /*notify*/false);
    }

    // ---- "解散群聊"按钮只对群主显示 ----
    // SetVisible 只置标志位、不重新排版,而水平布局排版时会跳过隐藏的子控件:按钮隐藏期间排过的
    // 那次版里它没有位置,显示出来时必须让底部按钮行按当前矩形重排一次,否则它会停在过时的矩形上。
    // 用 ForceLayout 而不是 SetRect:按钮行自身的矩形没有变,SetRect 会直接返回。
    balloonwjui::DuiButton* btnDismiss = FindCtrl<balloonwjui::DuiButton>(root, kIdGroupBtnDismiss);
    if (btnDismiss != NULL && btnDismiss->IsVisible() != data.isOwner)
    {
        btnDismiss->SetVisible(data.isOwner);
        balloonwjui::DuiControl* footer = btnDismiss->GetParent();
        if (footer != NULL)
        {
            footer->ForceLayout(footer->GetRect());
        }
    }

    return true;
}

bool SetGroupInfoCardEditMode(balloonwjui::DuiControl* root, bool edit)
{
    if (root == NULL)
    {
        return false;
    }
    balloonwjui::DuiControl* view = root->FindCtrlById(kIdGroupCardView);
    balloonwjui::DuiControl* editCtrl = root->FindCtrlById(kIdGroupCardEdit);
    if (view != NULL)
    {
        view->SetVisible(!edit);
    }
    if (editCtrl != NULL)
    {
        editCtrl->SetVisible(edit);
    }

    // 行容器 = view / edit 的父(同父),取任一非空者。逐个 ForceLayout 绕开
    // SetRect 在矩形未变时的提前返回(同 SetProfileEditMode 的坑规避),否则刚
    // 可见的编辑框 / view 停在 {0,0,0,0} 点不到、显示不出来。
    balloonwjui::DuiControl* row = NULL;
    if (view != NULL)
    {
        row = view->GetParent();
    }
    else if (editCtrl != NULL)
    {
        row = editCtrl->GetParent();
    }
    if (row != NULL)
    {
        row->ForceLayout(row->GetRect());
    }
    return true;
}

std::string ResolveGroupOwnerName(const std::vector<net::GroupMemberItem>& members,
                                  __int64 ownerid)
{
    for (size_t i = 0; i < members.size(); ++i)
    {
        if (members[i].m_userid == (int64_t)ownerid)
        {
            // 群名片优先,为空回退账号昵称。
            if (!members[i].m_card.empty())
            {
                return members[i].m_card;
            }
            return members[i].m_nickname;
        }
    }
    return std::string();
}

std::string ResolveMyGroupCard(const std::vector<net::GroupMemberItem>& members,
                               __int64 myUserid)
{
    for (size_t i = 0; i < members.size(); ++i)
    {
        if (members[i].m_userid == (int64_t)myUserid)
        {
            if (!members[i].m_card.empty())
            {
                return members[i].m_card;
            }
            return members[i].m_nickname;
        }
    }
    return std::string();
}

bool GroupInfoCopyText(const GroupInfoViewData& data, UINT ctrlId, CString& text)
{
    switch (ctrlId)
    {
    //点群名：复制完整群名（标签显示不下时会以省略号截断，复制的是全文）
    case kIdGroupName:
        if (data.groupName.IsEmpty())
        {
            return false;
        }
        text = data.groupName;
        return true;

    //点群号那一行：只复制数字群号，不带"群号"字样与人数
    case kIdGroupMeta:
    {
        const std::wstring number = FormatGroupNumber(data.groupid);
        if (number.empty())
        {
            return false;
        }
        text = number.c_str();
        return true;
    }

    //其余控件没有可复制的内容
    default:
        return false;
    }
}

bool GroupInfoHistoryTarget(const GroupInfoViewData& data, int& ctype, __int64& cid)
{
    //会话类型取值与协议一致：1 单聊、2 群聊
    const int kCtypeGroup = 2;
    if (data.groupid <= 0)
    {
        return false;
    }
    ctype = kCtypeGroup;
    cid = data.groupid;
    return true;
}

int GroupInfoToastTopOffset(balloonwjui::DuiControl* root)
{
    if (root == NULL)
    {
        return -1;
    }
    //头部资料区的下边取头像与群主两行中较低者：群名、群号、群主三行的总高略大于头像边长，
    //群主一行可能比头像更低
    balloonwjui::DuiControl* avatar = root->FindCtrlById(kIdGroupAvatar);
    balloonwjui::DuiControl* owner = root->FindCtrlById(kIdGroupOwner);
    if (avatar == NULL || owner == NULL)
    {
        return -1;
    }
    const RECT rcAvatar = avatar->GetRect();
    const RECT rcOwner = owner->GetRect();
    const LONG headerBottom = (rcOwner.bottom > rcAvatar.bottom) ? rcOwner.bottom : rcAvatar.bottom;
    return (int)(headerBottom - root->GetRect().top);
}

} // namespace balloonmain
