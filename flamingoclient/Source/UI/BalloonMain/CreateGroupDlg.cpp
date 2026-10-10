/**
 *  CreateGroupDlg.cpp —— "新建群聊"对话框实现。
 *
 *  控件树用 balloonui 现成控件组合而成（对照 design/新建群.png）：成员行 =
 *  DuiButton(复选框) + DuiAvatar(头像) + DuiLabel(姓名)；已选成员 = DuiAvatar +
 *  DuiLabel + DuiButton(✕)；按部门分组的分组标题 = DuiLabel。动态刷新（搜索过滤、
 *  勾选变化）用 DuiScrollView::SetContent 重建子树后 ForceLayout 重新布局。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#include "stdafx.h"
#include "CreateGroupDlg.h"
#include "SensitiveWordTips.h"   // CreateGroupFailureText（建群失败按错误码给提示）
#include "ContactRemarkModel.h"   // ResolveContactDisplayName（显示名口径全局唯一）
#include "MemberPickerTreeView.h"   // 左侧候选树（滚轮上抛 + 复选框点击判定）
#include "MemberPickerRow.h"        // 候选树的行内容（复选框 + 头像 + 姓名）
#include "MemberPickerLayout.h"     // BuildMemberPickerLayout：整棵控件树；邀请入群的同步选项取值规则

#include <memory>
#include <set>
#include <map>
#include <algorithm>

#include "../../Common/Log/IULog.h"
#include "../../Common/Util/EncodeUtil.h"
#include "TitleBarIcon.h"               // CreateProgramIconBitmap（标题栏左侧图标）
#include "../../App/FlamingoClient.h"          // CFlamingoClient::m_UserMgr
#include "../../net/ContactsProtocolClient.h"  // net::ContactItem / DepartmentItem / DeptMemberItem
#include "../../net/GroupProtocolClient.h"     // net::CreateGroupResponse
#include "../../net/ConversationsProtocolClient.h" // net::kGroupFollowMax（管理关注的成员的人数上限）
#include "../../net/IUSocket.h"            // CIUSocket::SendCreateGroup
#include "../../Session/UserSessionData.h" // FMG_MSG_TRANSFER_GROUP_OWNER
#include "GroupExitFlow.h"                 // TransferOwnerRequest / BuildTransferConfirmText
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"      // DuiVBox / DuiHBox / Hint
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

// ---- 对话框固定尺寸（逻辑像素）-----------------------------------------
// 含标题栏的整窗尺寸（逻辑像素）。由 MemberPickerLayout.h 统一定义，布局单测用的是同一组值
static const int kDlgWidth     = balloonmain::kPickerDlgWidth;
static const int kDlgHeight    = balloonmain::kPickerDlgHeight;
static const int kDlgMinWidth  = balloonmain::kPickerDlgMinWidth;
static const int kDlgMinHeight = balloonmain::kPickerDlgMinHeight;
static const int kDlgTitleBarH = balloonmain::kPickerDlgTitleBarH;

// 标题栏左侧图标边长（逻辑像素）。标题栏按 16x16 绘制，取同值免去缩放。
static const int kDlgTitleIconSize = 16;

// 标题栏实际显示的底色。本对话框未调 SetTitleBarTransparent，走 balloonui 的默认
// 不透明标题栏，其填充色见 DuiFrameWindow.cpp 的 Title strip background 段。图标须
// 合成到这个色上，否则其透明区在 SRCCOPY 搬运后会变成黑块。
static const COLORREF kDlgTitleBarBg = RGB(245, 246, 250);

// ---- 控件 id（对话框内唯一即可）----------------------------------------
// 骨架里的控件 id 由 MemberPickerLayout.h 统一定义（控件树在那里搭），这里取短名沿用
static const UINT kIdSearch       = balloonmain::kPickerIdSearch;    // 搜索联系人输入框
static const UINT kIdCancel       = balloonmain::kPickerIdCancel;    // 取消按钮
static const UINT kIdCreate       = balloonmain::kPickerIdConfirm;   // 确认按钮（创建群聊 / 邀请入群等）
// 成员行复选框 id 基址（+ 行序号）；与已选区 ✕ 按钮 id 基址分段，互不冲突。
static const UINT kCheckboxIdBase = 1000;
static const UINT kChipRemoveBase = 2000;

// ---- 行高 / 颜色 --------------------------------------------------------
static const int kHeaderRowH   = 26;   // 部门分组标题行高
static const int kChipRowH     = 36;   // 已选成员行高
static const int kAvatarSize   = 32;   // 成员行头像边长
static const int kChipAvatar   = 26;   // 已选成员头像边长
static const COLORREF kSectionColor = RGB(0x9A, 0x9A, 0x9A);   // 分组标题/计数 灰
static const COLORREF kErrorColor   = RGB(0xE0, 0x3E, 0x3E);   // 邀请入群：同步条数不合法时的错误提示（红）

// =======================================================================
// 文件作用域编码 / 配色辅助
// =======================================================================

// UTF-8 std::string → CString(UTF-16)。空串安全。
static CString Utf8ToCStr(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    std::vector<wchar_t> buf(utf8.size() + 1, 0);
    EncodeUtil::Utf8ToUnicode(utf8.c_str(), buf.data(), (int)buf.size());
    return CString(buf.data());
}

// CString(UTF-16) → UTF-8 std::string。空串安全。
static std::string CStrToUtf8(const CString& text)
{
    int wlen = text.GetLength();
    if (wlen <= 0)
    {
        return std::string();
    }
    std::vector<char> buf((size_t)wlen * 4 + 1, 0);
    EncodeUtil::UnicodeToUtf8(text.GetString(), buf.data(), (int)buf.size());
    return std::string(buf.data());
}

// 按 userid 从固定调色板挑头像底色（与联系人树/搜索结果同口径，保证同一人颜色稳定）。
static COLORREF AvatarColorForUser(int64_t userid)
{
    static const COLORREF kPalette[] =
    {
        RGB(0xFF, 0x7A, 0x45), RGB(0x3A, 0x86, 0xD9), RGB(0x2B, 0xB6, 0x73),
        RGB(0x8B, 0x5C, 0xF6), RGB(0xEF, 0x6C, 0x9A), RGB(0xF5, 0x9E, 0x0B),
        RGB(0x14, 0xB8, 0xA6), RGB(0xE1, 0x5B, 0x4C),
    };
    const int n = (int)(sizeof(kPalette) / sizeof(kPalette[0]));
    int idx = (int)(((userid % n) + n) % n);
    return kPalette[idx];
}

// 部门同级排序：deptorder 小者靠前，相等按 deptid 稳定。
static bool DeptOrderLess(const net::DepartmentItem* a, const net::DepartmentItem* b)
{
    if (a->m_deptorder != b->m_deptorder)
    {
        return a->m_deptorder < b->m_deptorder;
    }
    return a->m_deptid < b->m_deptid;
}

// 部门成员排序：memberorder 小者靠前，相等按 userid 稳定。
static bool MemberOrderLess(const net::DeptMemberItem& a, const net::DeptMemberItem& b)
{
    if (a.m_memberorder != b.m_memberorder)
    {
        return a.m_memberorder < b.m_memberorder;
    }
    return a.m_userid < b.m_userid;
}

// =======================================================================
// 构造 / 析构
// =======================================================================

CCreateGroupDlg::CCreateGroupDlg(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_selfUserId(0)
    , m_purpose(kPurposeCreateGroup)
    , m_pickSink(NULL)
    , m_groupId(0)
    , m_hNotifyTarget(NULL)
    , m_searchInput(NULL)
    , m_nameInput(NULL)
    , m_memberScroll(NULL)
    , m_memberTree(NULL)
    , m_chipScroll(NULL)
    , m_countLabel(NULL)
    , m_confirmButton(NULL)
    , m_groupAllowsHistory(true)
    , m_inviteConfirm(false)
    , m_builtPurpose(kPurposeCreateGroup)
    , m_threadGroupAvailable(false)
    , m_builtShowGroupType(false)
    , m_groupTypeNormal(NULL)
    , m_groupTypeThread(NULL)
    , m_built(false)
    , m_creating(false)
    , m_hTitleIcon(NULL)
{
}

CCreateGroupDlg::~CCreateGroupDlg()
{
    // 控件树与 HWND 由 DuiFrameWindow 基类析构兜底；本类只持借用指针。
    // 标题栏图标位图除外 —— DuiFrameWindow::SetIcon 只借用不释放，故本类收回。
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

// =======================================================================
// 打开
// =======================================================================

void CCreateGroupDlg::OpenForCreate(HWND owner, UINT selfUserId,
                                    const std::vector<net::ContactItem>& contacts,
                                    const std::vector<net::DepartmentItem>& departments)
{
    m_purpose = kPurposeCreateGroup;
    m_groupId = 0;
    OpenInternal(owner, selfUserId, contacts, departments);
}

void CCreateGroupDlg::OpenForPick(HWND owner, Purpose purpose, int64_t groupId,
                                  UINT selfUserId,
                                  const std::vector<net::ContactItem>& candidates,
                                  const std::vector<net::DepartmentItem>& departments)
{
    m_purpose = purpose;
    m_groupId = groupId;
    OpenInternal(owner, selfUserId, candidates, departments);
}

void CCreateGroupDlg::SetPickSink(IMemberPickSink* sink)
{
    m_pickSink = sink;
}

void CCreateGroupDlg::SetInitialSelection(const std::vector<int64_t>& userIds)
{
    m_initialSelection = userIds;
}

void CCreateGroupDlg::SetInviteOptions(bool groupAllowsHistory, bool inviteConfirm)
{
    m_groupAllowsHistory = groupAllowsHistory;
    m_inviteConfirm = inviteConfirm;
}

void CCreateGroupDlg::SetThreadGroupAvailable(bool available)
{
    m_threadGroupAvailable = available;
}

bool CCreateGroupDlg::ShowGroupType() const
{
    return m_purpose == kPurposeCreateGroup && m_threadGroupAvailable;
}

void CCreateGroupDlg::OpenInternal(HWND owner, UINT selfUserId,
                                   const std::vector<net::ContactItem>& contacts,
                                   const std::vector<net::DepartmentItem>& departments)
{
    // 只记下来备用（「转让群主并退出」确认后往这里投请求），不作为窗口的 owner，理由见下方 Create 处
    m_hNotifyTarget = owner;
    m_selfUserId  = selfUserId;
    m_contacts    = contacts;
    m_departments = departments;
    m_selected.clear();
    //预先勾选：只保留确实在候选里、且不是自己的人
    for (size_t i = 0; i < m_initialSelection.size(); ++i)
    {
        const UINT uid = (UINT)m_initialSelection[i];
        if (uid != m_selfUserId && FindContact(uid) != NULL
            && std::find(m_selected.begin(), m_selected.end(), uid) == m_selected.end())
        {
            m_selected.push_back(uid);
        }
    }
    m_initialSelection.clear();
    m_creating = false;

    //窗口还开着、但这次要用的用途与上次搭界面时的用途不同：必须销毁重建。
    //各用途的控件集合并不相同（建群多一个群名输入框），沿用上一次搭好的界面会出现
    //"顶着建群的样子走邀请入群的逻辑"这种错乱。
    //帖子群功能的可用状态变了（「群类型」选项组要加上或去掉）：同样销毁重建
    if (IsWindow() && m_built && (m_builtPurpose != m_purpose || m_builtShowGroupType != ShowGroupType()))
    {
        DestroyWindow();
    }

    if (!IsWindow())
    {
        // owner 传 NULL：DuiHost::SendNotify 以 GetParent(m_hWnd) 为通知目标，
        // 若有 owner 则子控件通知会被发到主面板而非本对话框；传 NULL 让通知自
        // 路由回本对话框（与单聊窗 / 搜索弹窗同一处理）。
        if (Create(NULL, CWindow::rcDefault, WindowTitleText(),
                   WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR(_T("新建群对话框 Create 失败。"));
            return;
        }
        SetTitle(WindowTitleText());
        SetTitleBarHeight(kDlgTitleBarH);

        // 标题栏左侧图标：用程序主图标。本对话框用的是默认的<u>不透明</u>标题栏，
        // balloonui 给它填的是 kDlgTitleBarBg 那个色，图标须合成到同色底上。
        if (m_hTitleIcon == NULL)
        {
            m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kDlgTitleIconSize, kDlgTitleBarBg);
        }
        if (m_hTitleIcon != NULL)
        {
            SetIcon(m_hTitleIcon);
        }
        //没有所有者、会出现在任务栏：任务栏与 Alt+Tab 也用程序图标（2026-10-07，UI 约定第 10 条）
        m_taskIcons.ApplyTaskIcons(m_hWnd);

        SetButtons(false, false, true);   // 只留关闭按钮（不需要最小化 / 最大化）
        SetMinSize(kDlgMinWidth, kDlgMinHeight);

        m_built = false;
        BuildUiOnce();
        ResizeClient(kDlgWidth, kDlgHeight);
        CenterWindow();
    }

    if (m_searchInput != NULL)
    {
        m_searchInput->SetText(_T(""));
    }
    if (m_nameInput != NULL)
    {
        m_nameInput->SetText(_T(""));
    }
    //窗口没关、上一次建群的应答还没回来时，按钮停在「正在创建…」：重新打开时恢复。放在重建界面之后：
    //窗口关过的话旧控件树已经释放，m_confirmButton 由 BuildUiOnce 换成新树里的按钮
    balloonmain::SetPickerConfirmBusy(m_confirmButton, false, ConfirmButtonText());
    //「群类型」每次打开都回到「普通群」
    if (m_groupTypeNormal != NULL && m_groupTypeThread != NULL)
    {
        m_groupTypeNormal->SetCheck(true, false);
        m_groupTypeThread->SetCheck(false, false);
    }
    RebuildMemberList(CString());
    RebuildSelected();
    //邀请入群：选项组回到初始状态，说明按本次的群设置与同意开关
    ResetInviteHistoryOptions();
    //SetInviteOptions 只对邀请入群那一次打开有意义；以别的用途打开时恢复为默认值，免得下次邀请沿用旧值
    if (m_purpose != kPurposeInvite)
    {
        m_groupAllowsHistory = true;
        m_inviteConfirm = false;
    }

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("选人类对话框已打开: purpose=%d, 候选 %u 人。",
             (int)m_purpose, (uint32_t)m_contacts.size());
}

// =======================================================================
// 骨架构建（一次）
// =======================================================================

void CCreateGroupDlg::BuildUiOnce()
{
    if (m_built)
    {
        return;
    }

    //重建骨架前先把上一轮的控件借用指针<u>全部</u>清空。
    //
    //这些指针指向的控件由控件树持有，窗口一销毁就跟着析构，本类留下的是悬空指针；
    //而各用途构建出来的控件集合并不相同（如群名输入框只有建群用途才建），若只靠
    //"各分支各自赋值"，换个用途重开时就会拿着上一轮的野指针去用 —— 曾因此崩溃过
    //（OpenInternal 里用到了随窗口销毁而释放掉的输入框）。统一在这里清零，后面
    //由 BuildMemberPickerLayout 的出参填回本次实际构建的控件。
    m_searchInput      = NULL;
    m_nameInput        = NULL;
    m_memberScroll     = NULL;
    m_memberTree       = NULL;
    m_chipScroll       = NULL;
    m_countLabel       = NULL;
    m_history          = balloonmain::InviteHistoryControls();
    m_groupTypeNormal  = NULL;
    m_groupTypeThread  = NULL;

    //记下这一轮界面是按哪种用途搭的，供 OpenInternal 判断要不要重建（见那里的说明）
    m_builtPurpose = m_purpose;

    //控件树由 BuildMemberPickerLayout 按用途搭出，布局单测调用的是同一个函数
    balloonmain::MemberPickerLayoutSpec spec;
    spec.m_showNameInput      = (m_purpose == kPurposeCreateGroup);
    spec.m_showInviteHistory  = (m_purpose == kPurposeInvite);
    spec.m_showGroupType      = ShowGroupType();
    spec.m_groupAllowsHistory = m_groupAllowsHistory;
    spec.m_inviteConfirm      = m_inviteConfirm;
    spec.m_confirmText        = ConfirmButtonText();
    balloonmain::MemberPickerLayoutRefs refs;
    std::unique_ptr<balloonwjui::DuiVBox> root = balloonmain::BuildMemberPickerLayout(spec, refs);

    m_searchInput      = refs.m_searchInput;
    m_nameInput        = refs.m_nameInput;
    m_memberScroll     = refs.m_memberScroll;
    m_memberTree       = refs.m_memberTree;
    m_chipScroll       = refs.m_chipScroll;
    m_countLabel       = refs.m_countLabel;
    m_confirmButton    = refs.m_confirmButton;
    m_history          = refs.m_history;
    m_groupTypeNormal  = refs.m_groupTypeNormal;
    m_groupTypeThread  = refs.m_groupTypeThread;
    m_builtShowGroupType = spec.m_showGroupType;

    SetClientContent(std::move(root));
    m_built = true;

    //计数文案按用途不同（"已选 N 人" / "已选 N 个接收者"），统一由 RebuildSelected
    //填，这里不写死初值。
}

void CCreateGroupDlg::ResetInviteHistoryOptions()
{
    if (!m_history.IsBuilt())
    {
        return;
    }
    //每次打开都回到「全部聊天记录」与默认条数：上一次邀请时的选择与条数不带到下一次
    CString defaultCount;
    defaultCount.Format(_T("%d"), balloonmain::kInviteHistoryDefaultCount);
    m_history.m_count->SetTextNoNotify(defaultCount);
    balloonmain::SelectInviteHistoryMode(m_history, balloonmain::kInviteHistoryModeAll, m_groupAllowsHistory);
    RestoreInviteHistoryHint();
}

void CCreateGroupDlg::RestoreInviteHistoryHint()
{
    if (m_history.m_hint == NULL)
    {
        return;
    }
    m_history.m_hint->SetText(balloonmain::InviteHistoryHintText(m_groupAllowsHistory, m_inviteConfirm));
    m_history.m_hint->SetTextColor(kSectionColor);
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

// =======================================================================
// 成员列表 / 已选区 重建
// =======================================================================

const net::ContactItem* CCreateGroupDlg::FindContact(UINT userId) const
{
    for (size_t i = 0; i < m_contacts.size(); ++i)
    {
        if ((UINT)m_contacts[i].m_userid == userId)
        {
            return &m_contacts[i];
        }
    }
    return NULL;
}

CString CCreateGroupDlg::DisplayNameOf(const net::ContactItem& c) const
{
    //「真名（备注）」，与通讯录树 / 会话列表 / 收藏区同一口径 —— 建群选人时看到的
    //名字必须与用户在通讯录里看到的一致，否则他按备注去找人会找不到。真名本身的
    //逐级兜底（昵称 → 账号 → "用户<id>"）由该纯函数内部完成。
    return balloonmain::ResolveContactDisplayName(
        Utf8ToCStr(c.m_remark), Utf8ToCStr(c.m_nickname),
        Utf8ToCStr(c.m_username), (UINT)c.m_userid);
}

// 成员行复选框：子类化 DuiButton，仅为屏蔽点击后四周的虚线焦点框
// （DuiControl::OnSetFocus 默认会置 m_bFocused=true，导致 DuiButton 画出
// DrawFocusRect 虚线框）。覆写为不接受可视获焦态即可，点击勾选逻辑不受影响。
class GroupPickCheckbox : public balloonwjui::DuiButton
{
public:
    bool OnSetFocus() override
    {
        return false;
    }
};

// 构造一行成员行（复选框 + 头像 + 姓名），并登记复选框 id↔userid / userid→控件。
static std::unique_ptr<balloonwjui::DuiControl> MakeMemberRow(
    UINT userId, const CString& display, COLORREF avatarColor, bool checked,
    UINT checkboxCtrlId,
    std::map<UINT, UINT>& ctrlToUser,
    std::map<UINT, balloonwjui::DuiButton*>& byUser)
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(8);

    std::unique_ptr<balloonwjui::DuiButton> box(new GroupPickCheckbox());
    box->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
    // Ghost(透明)变体：复选框只画 16px 的小勾选框 + 边框，不画占满整格的浅色
    // 背景方块（默认 Primary 变体会给 Checkbox 铺整格浅底，看起来像个大方块）。
    box->SetVariant(balloonwjui::DuiButton::Variant::Ghost);
    box->SetText(_T(""));
    box->SetCtrlId(checkboxCtrlId);
    box->SetCheck(checked, false);   // notify=false：设初值不触发 VALUECHANGED
    balloonwjui::DuiButton* boxRaw = box.get();
    row->AddChild(std::move(box), balloonwjui::DuiLayout::Hint().Fixed(24));

    // 头像：固定为 32×32 正方形并在行内垂直居中（Fixed 只给主轴=宽时，交叉轴=高
    // 会被默认 AlignFill 拉伸成长条；这里同时给交叉轴 32 + 居中，画出方形圆角框）。
    std::unique_ptr<balloonwjui::DuiAvatar> avatar(new balloonwjui::DuiAvatar());
    avatar->SetShape(balloonwjui::DuiAvatar::ShapeRoundRect);
    avatar->SetCornerRadius(8);
    avatar->SetFallbackBgColor(avatarColor);
    avatar->SetName(display.Left(1));   // 单字 monogram，与全局头像口径一致
    row->AddChild(std::move(avatar),
                  balloonwjui::DuiLayout::Hint().Fixed(kAvatarSize, kAvatarSize)
                      .AlignC(balloonwjui::DuiLayout::AlignCenter));

    std::unique_ptr<balloonwjui::DuiLabel> name(new balloonwjui::DuiLabel());
    name->SetText(display);
    name->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    row->AddChild(std::move(name), balloonwjui::DuiLayout::Hint().Weight(1));

    ctrlToUser[checkboxCtrlId] = userId;
    byUser[userId] = boxRaw;
    return std::unique_ptr<balloonwjui::DuiControl>(row.release());
}

void CCreateGroupDlg::RebuildMemberList(const CString& filter)
{
    if (m_memberTree == NULL)
    {
        return;
    }

    // 整表重建：清节点、清行装饰（基类 Clear 不认识子类的那几个映射），再清本类
    // 维护的"节点下标 <-> 树节点 id"两张表 —— 节点 id 会被复用，不清会串数据。
    m_memberTree->Clear();
    m_memberTree->ClearAllRowDecorations();
    m_treeNodes.clear();
    m_nodeIdByIndex.clear();
    m_indexByNodeId.clear();

    // 层级 / 排序 / 过滤全部交给纯函数，本函数只负责把结果喂给树。
    m_treeNodes = balloonmain::BuildMemberPickerTree(
        m_contacts, m_departments, m_selfUserId, filter);

    if (m_treeNodes.empty())
    {
        // 一个候选都没有：放一行提示，不可选中、也不挂行控件（于是没有复选框）。
        const int hintId = m_memberTree->AddRoot(
            filter.IsEmpty() ? CString(TR(L"暂无联系人")) : CString(TR(L"无匹配联系人")));
        m_memberTree->SetItemSelectable(hintId, false);
        SyncTreeHeight();
        if (IsWindow())
        {
            ::InvalidateRect(m_hWnd, NULL, FALSE);
        }
        return;
    }

    const std::set<UINT> selectedSet(m_selected.begin(), m_selected.end());

    m_nodeIdByIndex.assign(m_treeNodes.size(), -1);
    for (size_t i = 0; i < m_treeNodes.size(); ++i)
    {
        const balloonmain::MemberPickerNode& n = m_treeNodes[i];

        // 节点数组是"深度优先展开后的显示顺序"，父节点一定排在子节点前面，
        // 故逐个建时父节点的树 id 必已就绪。
        int nodeId = -1;
        if (n.m_parent < 0)
        {
            nodeId = m_memberTree->AddRoot(n.m_label);
        }
        else
        {
            nodeId = m_memberTree->AddChild(m_nodeIdByIndex[n.m_parent], n.m_label);
        }
        if (nodeId < 0)
        {
            continue;
        }
        m_nodeIdByIndex[i]      = nodeId;
        m_indexByNodeId[nodeId] = (int)i;

        // 默认全展开 —— 选人时用户要的是"一眼看到有谁"，而不是先自己点开各级部门。
        m_memberTree->SetExpanded(nodeId, true);

        // 行内容（复选框 + 头像 + 姓名）交给行控件画。部门行不画头像：传空头像字。
        std::unique_ptr<balloonmain::MemberPickerRow> row(new balloonmain::MemberPickerRow());
        CString  avatarText;
        COLORREF avatarColor = RGB(0xFF, 0x7A, 0x45);
        if (n.m_kind == balloonmain::kPickerNodeMember)
        {
            avatarText  = n.m_label.Left(1);
            avatarColor = AvatarColorForUser((int64_t)n.m_userId);
        }
        row->SetRowData(n.m_label, avatarText, avatarColor);
        row->SetCheckState(balloonmain::DeriveCheckState(m_treeNodes, (int)i, selectedSet));
        m_memberTree->SetItemCustomControl(
            nodeId, std::unique_ptr<balloonwjui::DuiControl>(row.release()));
    }

    SyncTreeHeight();
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CCreateGroupDlg::SyncTreeHeight()
{
    if (m_memberScroll == NULL || m_memberTree == NULL)
    {
        return;
    }
    int h = m_memberTree->GetContentHeight();
    if (h < 0)
    {
        h = 0;
    }
    m_memberScroll->SetContentHeight(h);

    RECT rc = m_memberScroll->GetRect();
    if (rc.right > rc.left && rc.bottom > rc.top)
    {
        m_memberScroll->ForceLayout(rc);
    }
}

void CCreateGroupDlg::RefreshTreeCheckStates()
{
    if (m_memberTree == NULL)
    {
        return;
    }
    const std::set<UINT> selectedSet(m_selected.begin(), m_selected.end());
    for (size_t i = 0; i < m_treeNodes.size() && i < m_nodeIdByIndex.size(); ++i)
    {
        const int nodeId = m_nodeIdByIndex[i];
        if (nodeId < 0)
        {
            continue;
        }
        balloonmain::MemberPickerRow* row = dynamic_cast<balloonmain::MemberPickerRow*>(
            m_memberTree->GetItemCustomControl(nodeId));
        if (row == NULL)
        {
            continue;
        }
        row->SetCheckState(balloonmain::DeriveCheckState(m_treeNodes, (int)i, selectedSet));
    }
    m_memberTree->Invalidate();
}

void CCreateGroupDlg::OnTreeCheckToggled(int nodeId)
{
    std::map<int, int>::const_iterator it = m_indexByNodeId.find(nodeId);
    if (it == m_indexByNodeId.end())
    {
        return;
    }
    const int index = it->second;
    if (index < 0 || index >= (int)m_treeNodes.size())
    {
        return;
    }

    const std::set<UINT> before(m_selected.begin(), m_selected.end());
    const int state = balloonmain::DeriveCheckState(m_treeNodes, index, before);

    if (m_treeNodes[index].m_kind == balloonmain::kPickerNodeMember)
    {
        const UINT uid = m_treeNodes[index].m_userId;
        if (state == balloonmain::kPickerChecked)
        {
            RemoveSelected(uid);
        }
        else
        {
            AddSelected(uid);
        }
    }
    else
    {
        // 部门节点：已全选则全部取消；未选或半选一律补齐成全选 —— 半选时再点一下，
        // 用户期待的是"把这个部门选满"，而不是把已经选好的那几个也清掉。
        std::vector<UINT> ids;
        balloonmain::CollectDeptMemberIds(m_treeNodes, index, ids);
        for (size_t k = 0; k < ids.size(); ++k)
        {
            if (state == balloonmain::kPickerChecked)
            {
                RemoveSelected(ids[k]);
            }
            else
            {
                AddSelected(ids[k]);
            }
        }
    }

    RefreshTreeCheckStates();
    RebuildSelected();
}

void CCreateGroupDlg::RebuildSelected()
{
    if (m_chipScroll != NULL)
    {
        m_chipRemoveCtrlToUser.clear();

        std::unique_ptr<balloonwjui::DuiVBox> content(new balloonwjui::DuiVBox());
        content->SetGap(4);

        UINT nextRemoveId = kChipRemoveBase;
        for (size_t i = 0; i < m_selected.size(); ++i)
        {
            UINT uid = m_selected[i];
            const net::ContactItem* c = FindContact(uid);
            CString display;
            if (c != NULL)
            {
                display = DisplayNameOf(*c);
            }
            else
            {
                display.Format(TR(L"用户%u"), uid);
            }

            std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
            row->SetGap(8);

            std::unique_ptr<balloonwjui::DuiAvatar> avatar(new balloonwjui::DuiAvatar());
            avatar->SetShape(balloonwjui::DuiAvatar::ShapeRoundRect);
            avatar->SetCornerRadius(8);
            avatar->SetFallbackBgColor(AvatarColorForUser(uid));
            avatar->SetName(display.Left(1));
            row->AddChild(std::move(avatar),
                          balloonwjui::DuiLayout::Hint().Fixed(kChipAvatar, kChipAvatar)
                              .AlignC(balloonwjui::DuiLayout::AlignCenter));

            std::unique_ptr<balloonwjui::DuiLabel> name(new balloonwjui::DuiLabel());
            name->SetText(display);
            name->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            row->AddChild(std::move(name), balloonwjui::DuiLayout::Hint().Weight(1));

            std::unique_ptr<balloonwjui::DuiButton> rm(new balloonwjui::DuiButton());
            rm->SetButtonType(balloonwjui::DuiButton::StylePushButton);
            rm->SetVariant(balloonwjui::DuiButton::Variant::Text);
            rm->SetCtrlId(nextRemoveId);
            rm->SetText(_T("✕"));
            row->AddChild(std::move(rm), balloonwjui::DuiLayout::Hint().Fixed(28));

            m_chipRemoveCtrlToUser[nextRemoveId] = uid;
            ++nextRemoveId;

            content->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kChipRowH));
        }

        m_chipScroll->SetContent(std::move(content));
        RECT rc = m_chipScroll->GetRect();
        if (rc.right > rc.left && rc.bottom > rc.top)
        {
            m_chipScroll->ForceLayout(rc);
        }
    }

    // 底部"已选 N 人"。
    if (m_countLabel != NULL)
    {
        CString text;
        text.Format(TR(L"已选 %d 人"), (int)m_selected.size());
        m_countLabel->SetText(text);
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CCreateGroupDlg::AddSelected(UINT userId)
{
    for (size_t i = 0; i < m_selected.size(); ++i)
    {
        if (m_selected[i] == userId)
        {
            return;   // 已在集合里
        }
    }
    m_selected.push_back(userId);
}

void CCreateGroupDlg::RemoveSelected(UINT userId)
{
    for (std::vector<UINT>::iterator it = m_selected.begin(); it != m_selected.end(); ++it)
    {
        if (*it == userId)
        {
            m_selected.erase(it);
            return;
        }
    }
}

// =======================================================================
// 发起建群 / 应答
// =======================================================================

void CCreateGroupDlg::DoCreate()
{
    if (m_creating)
    {
        return;   // 已发出请求、等待应答，避免重复提交
    }

    //"选人"用途走另一条路：不等应答、发完即关窗，界面由群管理窗按推送刷新
    if (m_purpose != kPurposeCreateGroup)
    {
        DoPickConfirm();
        return;
    }

    // 群名：取输入框文字并去掉首尾空白。未填写时发空串，由服务端按成员昵称生成自动
    // 群名，此后群名随成员增减、改昵称而重算；客户端自己拼出的名字会被服务端当成
    // 用户设定的群名，从此固定不变。
    std::string name;
    if (m_nameInput != NULL)
    {
        name = net::GroupProtocolClient::normalizeGroupNameInput(CStrToUtf8(m_nameInput->GetText()));
    }

    std::vector<int64_t> memberIds;
    for (size_t i = 0; i < m_selected.size(); ++i)
    {
        memberIds.push_back((int64_t)m_selected[i]);
    }

    //群类型：只有显示了「群类型」选项组且选中「帖子群」时才建帖子群
    const int groupType = balloonmain::SelectedGroupType(m_groupTypeNormal, m_groupTypeThread);
    m_creating = true;
    //成员多时服务端要处理一阵，按钮先变为不可点的「正在创建…」，免得看上去像卡住了
    balloonmain::SetPickerConfirmBusy(m_confirmButton, true, ConfirmButtonText());
    CIUSocket::GetInstance().SendCreateGroup(name, memberIds, groupType);
    LOG_INFO("create group sent: members=%u, grouptype=%d.", (uint32_t)memberIds.size(), groupType);
}

void CCreateGroupDlg::OnCreateGroupResult(const net::CreateGroupResponse& resp)
{
    m_creating = false;
    if (resp.m_code == 0)
    {
        LOG_INFO("建群成功: groupid=%lld。", (long long)resp.m_groupid);
        ::MessageBox(m_hWnd, TR(L"群聊创建成功。"), g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        if (IsWindow())
        {
            DestroyWindow();
        }
        return;
    }

    LOG_ERROR("建群失败: code=%d, msg=%s。", resp.m_code, resp.m_msg.c_str());
    //失败：按钮恢复为可点，用户改完可以再提交。窗口已经关掉时控件树已释放，不能再碰 m_confirmButton
    if (IsWindow())
    {
        balloonmain::SetPickerConfirmBusy(m_confirmButton, false, ConfirmButtonText());
    }
    //群名称命中敏感词有专门的说法，其余错误码带上错误码
    const CString tip = balloonmain::CreateGroupFailureText(resp.m_code);
    ::MessageBox(m_hWnd, tip, g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CCreateGroupDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                    LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 关闭对话框 = 取消建群：仅销毁本窗口（对象由主面板持有，可复用）。
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CCreateGroupDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                     LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // 搜索框文字变化 → 重建成员列表（按词过滤）。
    if (pNotify->ctrlId == kIdSearch && pNotify->code == balloonwjui::DUIN_VALUECHANGED)
    {
        RebuildMemberList(m_searchInput != NULL ? m_searchInput->GetText() : CString());
        return 0;
    }

    // 候选树里某行的复选框被点 → 切换该节点（成员切自己，部门全选 / 全不选）。
    if (pNotify->code == balloonmain::MemberPickerTreeView::DUIN_PICKER_CHECK_TOGGLE)
    {
        if (m_memberTree != NULL)
        {
            OnTreeCheckToggled(m_memberTree->GetToggledNodeId());
        }
        return 0;
    }

    // 邀请入群：点了「全部 / 最近 N 条 / 不同步」中的一项 → 取消另外两项、刷新条数输入框的可用状态，并清掉错误提示。
    // 三个单选按钮不在同一个父容器里，balloonui 不会替它们互斥，由 SelectInviteHistoryMode 处理
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && (pNotify->ctrlId == balloonmain::kPickerIdHistoryAll || pNotify->ctrlId == balloonmain::kPickerIdHistoryRecent
            || pNotify->ctrlId == balloonmain::kPickerIdHistoryNone))
    {
        int mode = balloonmain::kInviteHistoryModeAll;
        if (pNotify->ctrlId == balloonmain::kPickerIdHistoryRecent)
        {
            mode = balloonmain::kInviteHistoryModeRecent;
        }
        else if (pNotify->ctrlId == balloonmain::kPickerIdHistoryNone)
        {
            mode = balloonmain::kInviteHistoryModeNone;
        }
        balloonmain::SelectInviteHistoryMode(m_history, mode, m_groupAllowsHistory);
        RestoreInviteHistoryHint();
        return 0;
    }

    // 邀请入群：条数改了 → 清掉上一次提交时的错误提示，恢复平时的说明。
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == balloonmain::kPickerIdHistoryCount)
    {
        RestoreInviteHistoryHint();
        return 0;
    }

    // 用户展开 / 收起了某个部门 → 树的内容总高变了，重设滚动区的内容高度。
    if (pNotify->code == balloonmain::MemberPickerTreeView::DUIN_PICKER_HEIGHT_CHANGED)
    {
        SyncTreeHeight();
        if (IsWindow())
        {
            ::InvalidateRect(m_hWnd, NULL, FALSE);
        }
        return 0;
    }

    if (pNotify->code == balloonwjui::DUIN_CLICK)
    {
        // 已选区某成员的 ✕ → 移除该成员，并取消其在左侧的复选框。
        std::map<UINT, UINT>::iterator it = m_chipRemoveCtrlToUser.find(pNotify->ctrlId);
        if (it != m_chipRemoveCtrlToUser.end())
        {
            UINT uid = it->second;
            RemoveSelected(uid);
            // 左侧候选树里该成员、以及其各级祖先部门的勾选态都会跟着变，统一重算。
            RefreshTreeCheckStates();
            RebuildSelected();
            return 0;
        }

        if (pNotify->ctrlId == kIdCancel)
        {
            if (IsWindow())
            {
                DestroyWindow();
            }
            return 0;
        }
        if (pNotify->ctrlId == kIdCreate)
        {
            DoCreate();
            return 0;
        }
    }

    bHandled = FALSE;
    return 0;
}

// =======================================================================
// "选人"用途（邀请入群 / 添加禁言成员 / 转让群主）
// =======================================================================

LPCTSTR CCreateGroupDlg::WindowTitleText() const
{
    switch (m_purpose)
    {
    case kPurposeInvite:
        return TR(L"邀请成员入群");
    case kPurposeMute:
        return TR(L"选择要禁言的成员");
    case kPurposeTransfer:
        return TR(L"转让群主");
    //群主退群前选新群主
    case kPurposeTransferThenLeave:
        return TR(L"转让群主并退出");
    //文件共享窗口的「发起共享」
    case kPurposeShareTargets:
        return TR(L"选择共享对象");
    //群备忘编辑对话框的「选择成员」
    case kPurposeMemoTargets:
        return TR(L"选择提醒对象");
    //群聊窗的「语音通话」「视频通话」：选要振铃的人
    case kPurposeGroupCall:
        return TR(L"选择通话成员");
    //群通话窗的「邀请成员」
    case kPurposeGroupCallInvite:
        return TR(L"邀请成员加入通话");
    //群的「管理关注的成员…」
    case kPurposeGroupFollow:
        return TR(L"管理关注的成员");
    //个人机器人规则的「发送者 ▸ 指定的人」
    case kPurposeBotSenders:
        return TR(L"选择指定的人");
    case kPurposeCreateGroup:
    default:
        return TR(L"新建群聊");
    }
}

LPCTSTR CCreateGroupDlg::ConfirmButtonText() const
{
    switch (m_purpose)
    {
    case kPurposeInvite:
        return TR(L"邀请入群");
    case kPurposeMute:
        return TR(L"禁言");
    case kPurposeTransfer:
        return TR(L"转让");
    //群主退群前选新群主
    case kPurposeTransferThenLeave:
        return TR(L"转让并退出");
    //文件共享窗口的「发起共享」：选完人接着选共享内容
    case kPurposeShareTargets:
        return TR(L"下一步");
    //群备忘编辑对话框的「选择成员」：选完回到编辑对话框
    case kPurposeMemoTargets:
        return TR(L"确定");
    //群聊窗的「语音通话」「视频通话」：选完即发起
    case kPurposeGroupCall:
        return TR(L"开始通话");
    //群通话窗的「邀请成员」
    case kPurposeGroupCallInvite:
        return TR(L"邀请");
    //群的「管理关注的成员…」：选完即保存名单
    case kPurposeGroupFollow:
        return TR(L"确定");
    //个人机器人规则的「指定的人」：选完回到规则编辑窗
    case kPurposeBotSenders:
        return TR(L"确定");
    case kPurposeCreateGroup:
    default:
        return TR(L"创建群聊");
    }
}

void CCreateGroupDlg::DoPickConfirm()
{
    //选择共享对象、选择备忘提醒对象、选择群通话成员：不发协议，先关窗再把已选的人交出去 —— 接收方随即弹出
    //或回到自己的对话框，晚于它关窗会让系统把前台切到别的窗口
    if (m_purpose == kPurposeShareTargets || m_purpose == kPurposeMemoTargets || m_purpose == kPurposeGroupCall
        || m_purpose == kPurposeGroupCallInvite || m_purpose == kPurposeGroupFollow || m_purpose == kPurposeBotSenders)
    {
        //管理关注的成员：一个都不选表示清空名单，照常确认；其余用途没选人时什么都不做，让用户接着选
        if (m_selected.empty() && m_purpose != kPurposeGroupFollow)
        {
            return;
        }
        //关注名单有上限（与服务端一致），超出时留在本窗口提示，让用户去掉几位再确认
        if (m_purpose == kPurposeGroupFollow && m_selected.size() > net::kGroupFollowMax)
        {
            const std::wstring tip = TRF(L"每个群最多关注 {0} 位成员，请去掉几位再确定。", (int)net::kGroupFollowMax);
            ::MessageBox(m_hWnd, tip.c_str(), TR(L"管理关注的成员"), MB_OK | MB_ICONINFORMATION);
            return;
        }
        std::vector<int64_t> ids;
        for (size_t i = 0; i < m_selected.size(); ++i)
        {
            ids.push_back((int64_t)m_selected[i]);
        }
        IMemberPickSink* sink = m_pickSink;
        if (IsWindow())
        {
            DestroyWindow();
        }
        LOG_INFO("Pick targets confirmed: purpose=%d, count=%u.", (int)m_purpose, (uint32_t)ids.size());
        if (sink != NULL)
        {
            sink->OnMembersPicked(ids);
        }
        return;
    }

    if (m_groupId <= 0 || m_selected.empty())
    {
        //没选人就点确认：什么都不做，让用户接着选。按钮本身没做禁用态，
        //在这里挡一道即可。
        return;
    }

    CIUSocket& socket = CIUSocket::GetInstance();

    switch (m_purpose)
    {
    case kPurposeInvite:
    {
        //「入群后可查看的聊天记录」：条数不合法时在选项组下方就地提示，不发请求、不关窗
        int historyLimit = balloonmain::kInviteHistoryAll;
        if (m_history.IsBuilt())
        {
            const int err = balloonmain::ParseInviteHistoryLimit(m_groupAllowsHistory,
                                                                 balloonmain::InviteHistoryModeOf(m_history),
                                                                 m_history.m_count->GetText(), historyLimit);
            if (err != balloonmain::kInviteHistoryOk)
            {
                if (m_history.m_hint != NULL)
                {
                    m_history.m_hint->SetText(balloonmain::InviteHistoryErrorText(err));
                    m_history.m_hint->SetTextColor(kErrorColor);
                    ::InvalidateRect(m_hWnd, NULL, FALSE);
                }
                LOG_INFO("Invite to group rejected locally: invalid history count, error=%d.", err);
                return;
            }
        }

        std::vector<int64_t> ids;
        for (size_t i = 0; i < m_selected.size(); ++i)
        {
            ids.push_back((int64_t)m_selected[i]);
        }
        socket.SendInviteToGroup(m_groupId, ids, historyLimit);
        LOG_INFO("Invite to group: groupid=%lld, count=%u, historylimit=%d.",
                 (long long)m_groupId, (uint32_t)ids.size(), historyLimit);
        break;
    }

    case kPurposeMute:
    {
        //服务端一次只禁言一个人，逐个发。时长这里固定按"永久"发出 ——
        //带时长的禁言走成员卡右键那条路（有二级菜单可选档位）；这个入口
        //的定位是"批量拉一批人进禁言名单"，事后可在禁言页逐个调整。
        const int kMuteForever = 0;
        for (size_t i = 0; i < m_selected.size(); ++i)
        {
            socket.SendMuteMember(m_groupId, (int64_t)m_selected[i],
                                  true, kMuteForever, std::string());
        }
        LOG_INFO("批量禁言: groupid=%lld, count=%u。",
                 (long long)m_groupId, (uint32_t)m_selected.size());
        break;
    }

    case kPurposeTransfer:
    {
        //单选：即便界面上意外多选了，也只取第一个 —— 转让后本人就不再是群主、自己转不回来，
        //宁可少做也不能对多个人各发一次。
        const int64_t newOwner = (int64_t)m_selected[0];

        const net::ContactItem* c = FindContact(m_selected[0]);
        const CString name = (c != NULL) ? DisplayNameOf(*c) : CString();
        const std::wstring tip = balloonmain::BuildTransferConfirmText((LPCTSTR)name, false);
        if (::MessageBox(m_hWnd, tip.c_str(), TR(L"转让群主"),
                         MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        {
            return;   // 用户取消：窗口留着，让他重选或关掉
        }

        socket.SendTransferOwner(m_groupId, newOwner);
        LOG_INFO("Transfer group owner from member picker: groupid=%lld, newowner=%lld.",
                 (long long)m_groupId, (long long)newOwner);
        break;
    }

    //群主退群前选新群主：确认后交给主面板。主面板登记这次转让、成功后接着发退群，
    //失败时提示原因、不退群
    case kPurposeTransferThenLeave:
    {
        const int64_t newOwner = (int64_t)m_selected[0];
        const net::ContactItem* c = FindContact(m_selected[0]);
        const CString name = (c != NULL) ? DisplayNameOf(*c) : CString();
        const std::wstring tip = balloonmain::BuildTransferConfirmText((LPCTSTR)name, true);
        if (::MessageBox(m_hWnd, tip.c_str(), TR(L"转让群主并退出"),
                         MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        {
            return;   // 用户取消：窗口留着，让他重选或关掉
        }

        balloonmain::TransferOwnerRequest* req = new balloonmain::TransferOwnerRequest();
        req->m_groupId = m_groupId;
        req->m_newOwner = newOwner;
        req->m_newOwnerName = (LPCTSTR)name;
        req->m_thenLeave = true;
        if (!::IsWindow(m_hNotifyTarget)
            || !::PostMessage(m_hNotifyTarget, FMG_MSG_TRANSFER_GROUP_OWNER, 0, (LPARAM)req))
        {
            delete req;
            LOG_ERROR("Post transfer-then-leave request failed: groupid=%lld, newowner=%lld.",
                      (long long)m_groupId, (long long)newOwner);
            break;
        }
        LOG_INFO("Transfer-then-leave request posted to main panel: groupid=%lld, newowner=%lld.",
                 (long long)m_groupId, (long long)newOwner);
        break;
    }

    case kPurposeCreateGroup:
    default:
        return;
    }

    //这三种用途都不等应答改界面：服务端写成功后会推 1317 / 1318，群管理窗
    //统一刷新；失败也由它按 code 提示。这里发完就关窗。
    if (IsWindow())
    {
        DestroyWindow();
    }
}
