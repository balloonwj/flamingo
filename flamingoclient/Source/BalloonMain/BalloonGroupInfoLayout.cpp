/**
 *  BalloonGroupInfoLayout.cpp
 *
 *  「群资料」对话框客户区控件树构造 + 数据注入 + 群名片双态切换的实现。
 *  程序化组合 balloonui 现成控件(DuiVBox/DuiHBox/DuiAvatar/DuiLabel/
 *  DuiSwitch/DuiEditHost/DuiButton/DuiSeparator),与 BalloonSystemSettingDlg
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

#include "BalloonGroupInfoLayout.h"
#include "MainPanelUserData.h"   // GetUserAvatarColor
#include "../net/GroupProtocolClient.h"   // net::GroupMemberItem(ResolveXxx 用)
#include "../../balloonui/balloonui/DuiResMgr.h"   // GetFontByPointSize
#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox / DuiHBox
#include "../../balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../balloonui/balloonui/Controls/Input/DuiEditHost.h"

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

// CClickableLabel —— 可点击的纯文本标签(子类化 DuiLabel,覆写最少虚函数)。
// 坑:DuiLabel 覆写了 OnLButtonUp,在非 ModeLink(即 ModeText)时直接 return false、
// <u>不发 DUIN_CLICK</u>(见 DuiLabel.cpp:213),而宿主点击不冒泡(DuiHost.cpp:603),
// 故 ModeText 标签点了"没反应"。本子类把 OnLButtonUp 直接转回祖父 DuiControl 的实现
// (它命中即发 DUIN_CLICK),让标签既保持普通文本外观(不像 ModeLink 那样带下划线 / 蓝色),
// 又能被点击触发父对话框的 DUIN_CLICK —— 群名片行"整行点击就地编辑"靠它。不动 balloonui 库。
class CClickableLabel : public balloonwjui::DuiLabel
{
public:
    bool OnLButtonUp(POINT pt, UINT mkFlags) override
    {
        // 跳过 DuiLabel 的 ModeText 抑制,用祖父 DuiControl 的实现:命中且启用即
        // NotifyParent(DUIN_CLICK)。
        return balloonwjui::DuiControl::OnLButtonUp(pt, mkFlags);
    }

    bool OnSetCursor(POINT pt) override
    {
        // 悬停时给手型光标,提示"可点"。
        RECT rc = GetRect();
        if (::PtInRect(&rc, pt))
        {
            ::SetCursor(::LoadCursor(NULL, IDC_HAND));
            return true;
        }
        return false;
    }
};

// 取一个指向点尺寸字体的 HFONT(caller 不持有,DuiResMgr 缓存)。
HFONT Font(int pt, bool bold)
{
    return balloonwjui::DuiResMgr::Inst().GetFontByPointSize(pt, bold);
}

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
    lbl->SetFont(Font(pt, bold));
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
    lbl->SetFont(Font(pt, bold));
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
        info->AddChild(MakeLabel(_T(""), kIdGroupName, kFontGroupName, true, kClrText,
                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                       balloonwjui::DuiLayout::Hint().Fixed(26));
        info->AddChild(MakeLabel(_T(""), kIdGroupMeta, kFontMeta, false, kClrMuted,
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

    // ---- 群公告:小标题 + 浅橙填充圆角框(内含 wordwrap 正文) ----
    {
        root->AddChild(MakeLabel(_T("群公告"), 0, kFontMeta, false, kClrMuted,
                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                       balloonwjui::DuiLayout::Hint().Fixed(18));
        AddSpacer(root.get(), 6);

        std::unique_ptr<CRoundedFillBox> notice(new CRoundedFillBox());
        notice->SetFill(kClrNoticeBg, kNoticeRadius);
        notice->SetPadding(12, 8, 12, 8);

        std::unique_ptr<balloonwjui::DuiLabel> noticeText(new balloonwjui::DuiLabel());
        noticeText->SetCtrlId(kIdGroupAnnouncement);
        noticeText->SetFont(Font(kFontBody, false));
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
        row->AddChild(MakeClickableLabel(_T("我在本群的昵称"), kIdGroupCardLabel, kFontBody,
                                         false, kClrText, DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                      balloonwjui::DuiLayout::Hint().Fixed(130));

        // 值 view(右对齐,可点;占满右侧,覆盖整行另一半)。
        row->AddChild(MakeClickableLabel(_T(""), kIdGroupCardView, kFontBody, false,
                                         kClrText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE),
                      balloonwjui::DuiLayout::Hint().Weight(1));

        // 值 edit(初始隐藏;进编辑态时显示并占满右侧)。HWND 寄宿控件,子 EDIT
        // 由对话框 EnsureCreated。
        std::unique_ptr<balloonwjui::DuiEditHost> edit(new balloonwjui::DuiEditHost());
        edit->SetCtrlId(kIdGroupCardEdit);
        edit->SetVisible(false);
        row->AddChild(std::move(edit),
                      balloonwjui::DuiLayout::Hint().Weight(1)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));

        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowHeight));
    }

    AddSeparator(root.get());

    // ---- 消息免打扰 ----
    root->AddChild(MakeSwitchRow(_T("消息免打扰"), kIdGroupMuteSwitch, false),
                   balloonwjui::DuiLayout::Hint().Fixed(kRowHeight));
    AddSeparator(root.get());

    // ---- 置顶群聊 ----
    root->AddChild(MakeSwitchRow(_T("置顶群聊"), kIdGroupPinSwitch, false),
                   balloonwjui::DuiLayout::Hint().Fixed(kRowHeight));

    AddSpacer(root.get(), 10);
    AddSeparator(root.get());
    AddSpacer(root.get(), 12);

    // ---- 底部按钮:查看消息记录 / 退出群聊(等宽并排) ----
    {
        std::unique_ptr<balloonwjui::DuiHBox> footer(new balloonwjui::DuiHBox());
        footer->SetGap(12);

        std::unique_ptr<balloonwjui::DuiButton> btnHistory(new balloonwjui::DuiButton());
        btnHistory->SetCtrlId(kIdGroupBtnHistory);
        btnHistory->SetText(_T("查看消息记录"));
        btnHistory->SetVariant(balloonwjui::DuiButton::Variant::Default);
        btnHistory->SetTextPointSize(kFontBody);
        footer->AddChild(std::move(btnHistory),
                         balloonwjui::DuiLayout::Hint().Weight(1).Fixed(-1, kFooterBtnHeight)
                             .AlignC(balloonwjui::DuiLayout::AlignCenter));

        std::unique_ptr<balloonwjui::DuiButton> btnQuit(new balloonwjui::DuiButton());
        btnQuit->SetCtrlId(kIdGroupBtnQuit);
        btnQuit->SetText(_T("退出群聊"));
        btnQuit->SetVariant(balloonwjui::DuiButton::Variant::Default);
        btnQuit->SetTextPointSize(kFontBody);
        // 危险红字(浅红底位图由对话框 EnsureVisualResources 精修;此处先红字兜底)。
        btnQuit->SetTextColor(RGB(0xE5, 0x39, 0x35));
        footer->AddChild(std::move(btnQuit),
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
    meta.Format(_T("群号 %I64d · %d 人"), data.groupid, data.memberCount);
    balloonwjui::DuiLabel* metaLbl = FindCtrl<balloonwjui::DuiLabel>(root, kIdGroupMeta);
    if (metaLbl != NULL)
    {
        metaLbl->SetText(meta);
    }

    // ---- 群主行(显示名未知时只显"群主:") ----
    CString owner;
    owner.Format(_T("群主：%s"), static_cast<LPCTSTR>(data.ownerName));
    balloonwjui::DuiLabel* ownerLbl = FindCtrl<balloonwjui::DuiLabel>(root, kIdGroupOwner);
    if (ownerLbl != NULL)
    {
        ownerLbl->SetText(owner);
    }

    // ---- 群公告(空→占位) ----
    balloonwjui::DuiLabel* noticeLbl = FindCtrl<balloonwjui::DuiLabel>(root, kIdGroupAnnouncement);
    if (noticeLbl != NULL)
    {
        if (data.announcement.IsEmpty())
        {
            noticeLbl->SetText(_T("暂无群公告"));
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
            cardView->SetText(_T("未设置"));
            cardView->SetTextColor(kClrMuted);
        }
        else
        {
            cardView->SetText(data.myCard);
            cardView->SetTextColor(kClrText);
        }
    }
    balloonwjui::DuiEditHost* cardEdit = FindCtrl<balloonwjui::DuiEditHost>(root, kIdGroupCardEdit);
    if (cardEdit != NULL)
    {
        cardEdit->SetText(data.myCard);
    }

    // ---- 两个开关初值(静默,不发通知) ----
    balloonwjui::DuiSwitch* muteSw = FindCtrl<balloonwjui::DuiSwitch>(root, kIdGroupMuteSwitch);
    if (muteSw != NULL)
    {
        muteSw->SetChecked(data.mute, /*animated*/false, /*notify*/false);
    }
    balloonwjui::DuiSwitch* pinSw = FindCtrl<balloonwjui::DuiSwitch>(root, kIdGroupPinSwitch);
    if (pinSw != NULL)
    {
        pinSw->SetChecked(data.pinned, /*animated*/false, /*notify*/false);
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

} // namespace balloonmain
