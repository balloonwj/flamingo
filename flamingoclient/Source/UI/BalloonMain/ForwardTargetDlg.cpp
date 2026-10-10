/**
 *  ForwardTargetDlg.cpp —— 转发目标选择窗的实现，说明见 ForwardTargetDlg.h。
 *
 *  控件组合方式与「新建群聊」对话框（CreateGroupDlg.cpp）一致：静态骨架只建一次，
 *  候选行、已选区等动态内容用 DuiScrollView::SetContent 整块重建后重排。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "ForwardTargetDlg.h"

#include <memory>
#include <vector>

#include "../../Common/Log/IULog.h"
#include "../../Common/Util/EncodeUtil.h"
#include "../../net/Protocol.h"                // conversation_single
#include "../BalloonSearch/SearchFilterTab.h"  // 页签
#include "AvatarSpec.h"                        // DecodeAvatar：首字头像的字形与底色
#include "ContactRemarkModel.h"                // ResolveContactDisplayName（显示名口径全局唯一）
#include "MemberPickerTreeView.h"              // 「联系人」页的组织架构树
#include "MemberPickerRow.h"                   // 组织架构树的行内容
#include "TitleBarIcon.h"                      // CreateProgramIconBitmap（标题栏左侧图标）
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

namespace {

// ---- 窗口尺寸（逻辑像素）----
// 含标题栏的整窗初始尺寸（逻辑像素）。2026-10-02 修复 ResizeClient 多叠加系统边框的问题时由 720×560 加上
// 修复前多叠加的 18×47（120 DPI 实测），保持修复前的窗口大小。
const int kDlgWidth     = 738;   // 初始宽
const int kDlgHeight    = 607;   // 初始高
const int kDlgMinWidth  = 600;   // 最小宽：再窄左右两列就放不下候选名字
const int kDlgMinHeight = 460;   // 最小高
const int kDlgTitleBarH = 36;    // 标题栏高，与其它对话框一致

// 标题栏左侧图标边长。标题栏按 16x16 绘制，取同值免去缩放
const int kDlgTitleIconSize = 16;
// 标题栏底色：本窗口用 balloonui 默认的不透明标题栏，图标须合成到同一底色上，
// 否则透明区域搬运后会变成黑块（与 CreateGroupDlg 同一取值）
const COLORREF kDlgTitleBarBg = RGB(245, 246, 250);

// ---- 控件 id（窗口内唯一）----
// 从 20 起编：1 / 2 / 3 是 DuiFrameWindow 标题栏最小化、最大化、关闭按钮的控件号，点击通知同样先到本窗口的
// OnDuiNotify。2026-10-05 之前从 1 起编，「联系人」页签为 3，点标题栏关闭按钮被当成点了页签，窗口关不掉
// （开发约定文档「UI 约定」第 4 条）
const UINT kIdSearch      = 20;   // 搜索框
const UINT kIdTabRecent   = 21;   // 「最近会话」页签
const UINT kIdTabContacts = 22;   // 「联系人」页签
const UINT kIdTabGroups   = 23;   // 「群组」页签
const UINT kIdListScroll  = 24;   // 「最近会话」「群组」页的候选滚动区
const UINT kIdTreeScroll  = 25;   // 「联系人」页的滚动区
const UINT kIdTree        = 26;   // 组织架构树
const UINT kIdChipScroll  = 27;   // 已选区
const UINT kIdPreview     = 28;   // 转发内容预览
const UINT kIdCount       = 29;   // 底部计数
const UINT kIdCancel      = 30;   // 「取消」
const UINT kIdSend        = 31;   // 「发送」
// 候选行复选框 id 基址（+ 行序号）与已选区 ✕ 按钮 id 基址，两段互不重叠
const UINT kRowCheckBase   = 1000;
const UINT kChipRemoveBase = 2000;

// ---- 行高、尺寸与颜色 ----
const int kEditRowH    = 34;    // 搜索框高
const int kTabRowH     = 30;    // 页签行高
const int kTabWidth    = 84;    // 每个页签宽
const int kRowH        = 44;    // 候选行高
const int kTreeRowH    = 44;    // 组织架构树行高，与候选行一致
const int kAvatarSize  = 32;    // 候选行头像边长
const int kAvatarRadius = 8;    // 候选行与已选区头像的圆角半径
const int kCheckColW   = 24;    // 候选行复选框列宽
const int kChipRowH    = 36;    // 已选区行高
const int kChipAvatar  = 26;    // 已选区头像边长
const int kChipRemoveW = 28;    // 已选区 ✕ 按钮宽
const int kSectionH    = 20;    // 小标题行高（「发送给」「转发内容」）
const int kPreviewH    = 44;    // 转发内容预览高，容纳两行
const int kBottomH     = 40;    // 底部栏高
const int kCancelW     = 88;    // 「取消」按钮宽
const int kSendW       = 110;   // 「发送」按钮宽
const int kRootPadding = 16;    // 根容器内边距
const int kRootGap     = 12;    // 根容器子项间距
const int kColumnGap   = 16;    // 左右两列间距
const int kInnerGap    = 8;     // 列内子项间距
const int kTabGap      = 4;     // 页签间距
const int kChipGap     = 4;     // 已选区行间距

const COLORREF kSectionColor = RGB(0x9A, 0x9A, 0x9A);   // 小标题、计数、空列表提示的灰色
const COLORREF kWhite        = RGB(255, 255, 255);       // 搜索框底色

// UTF-8 std::string 转 CString（UTF-16）。空串安全
CString Utf8ToCStr(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    std::vector<wchar_t> buf(utf8.size() + 1, 0);
    EncodeUtil::Utf8ToUnicode(utf8.c_str(), buf.data(), (int)buf.size());
    return CString(buf.data());
}

// 候选行复选框：子类化 DuiButton，只为屏蔽点击后四周的虚线焦点框
// （做法与 CreateGroupDlg 的 GroupPickCheckbox 相同）
class ForwardPickCheckbox : public balloonwjui::DuiButton
{
public:
    bool OnSetFocus() override
    {
        return false;
    }
};

// 构造一个头像控件：有借用的位图就画位图，否则按 DecodeAvatar 画首字头像
std::unique_ptr<balloonwjui::DuiAvatar> MakeAvatar(const ForwardTarget& t)
{
    std::unique_ptr<balloonwjui::DuiAvatar> avatar(new balloonwjui::DuiAvatar());
    avatar->SetShape(balloonwjui::DuiAvatar::ShapeRoundRect);
    avatar->SetCornerRadius(kAvatarRadius);
    const AvatarSpec spec = DecodeAvatar(0, std::string(), t.m_name.c_str(), t.m_cid);
    avatar->SetFallbackBgColor(spec.m_color);
    avatar->SetName(spec.m_glyph);
    if (t.m_avatar != NULL)
    {
        avatar->SetBitmap(t.m_avatar);
    }
    return avatar;
}

} // anonymous namespace

// =======================================================================
// 构造 / 析构 / 打开
// =======================================================================

CForwardTargetDlg::CForwardTargetDlg()
    : m_sink(NULL)
    , m_targetTitle(NULL)
    , m_contentTitle(NULL)
    , m_selfUserId(0)
    , m_selection(0)
    , m_tab(kTabRecent)
    , m_searchInput(NULL)
    , m_listScroll(NULL)
    , m_treeScroll(NULL)
    , m_tree(NULL)
    , m_chipScroll(NULL)
    , m_previewLabel(NULL)
    , m_countLabel(NULL)
    , m_sendButton(NULL)
    , m_hTitleIcon(NULL)
{
    m_tabs[kTabRecent] = NULL;
    m_tabs[kTabContacts] = NULL;
    m_tabs[kTabGroups] = NULL;
}

CForwardTargetDlg::~CForwardTargetDlg()
{
    // 控件树与 HWND 由 DuiFrameWindow 基类析构处理；标题栏图标位图基类只借用，由本类释放
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

void CForwardTargetDlg::Open(IForwardTargetSink* sink, int64_t selfUserId,
                             const ForwardRequest& request,
                             const std::vector<ForwardTarget>& recent,
                             const std::vector<ForwardTarget>& groups,
                             const std::vector<net::ContactItem>& contacts,
                             const std::vector<net::DepartmentItem>& departments)
{
    m_sink        = sink;
    m_selfUserId  = selfUserId;
    m_request     = request;
    m_recent      = recent;
    m_groups      = groups;
    m_contacts    = contacts;
    m_departments = departments;
    m_selection   = ForwardTargetSelection(selfUserId);

    if (!IsWindow())
    {
        // 父窗口传 NULL：DuiHost::SendNotify 以 GetParent(m_hWnd) 为通知目标，有父窗口时
        // 控件通知会发到主面板而不是本窗口（与 CreateGroupDlg 同一处理）
        //可以最小化（2026-10-05 起）：没有属主的工具性窗口默认也有任务栏按钮，这里显式加上，最小化后从任务栏找回
        if (Create(NULL, CWindow::rcDefault, TR(L"转发"), WS_OVERLAPPEDWINDOW, WS_EX_APPWINDOW) == NULL)
        {
            LOG_ERROR("Forward picker: Create failed.");
            return;
        }
        SetTitle(TR(L"转发"));
        SetTitleBarHeight(kDlgTitleBarH);
        if (m_hTitleIcon == NULL)
        {
            m_hTitleIcon = CreateProgramIconBitmap(kDlgTitleIconSize, kDlgTitleBarBg);
        }
        if (m_hTitleIcon != NULL)
        {
            SetIcon(m_hTitleIcon);
        }
        //最小化、最大化、关闭三个按钮，可拖拽改大小（2026-10-05 用户要求），最小为 kDlgMinWidth × kDlgMinHeight
        SetButtons(true, true, true);
        SetResizable(true);
        SetMinSize(kDlgMinWidth, kDlgMinHeight);
        m_taskIcons.ApplyTaskIcons(m_hWnd);

        BuildUi();
        ResizeClient(kDlgWidth, kDlgHeight);
        CenterWindow();
    }

    if (m_searchInput != NULL)
    {
        m_searchInput->SetText(_T(""));
    }
    if (m_previewLabel != NULL)
    {
        m_previewLabel->SetText(PreviewText());
    }
    //同一个窗口也用来发送本地文件（录屏完成后的「发送到会话…」）与主菜单发起通话：标题、按钮、说明按用途区分
    SetTitle(ForwardPickerTitle(m_request).c_str());
    if (m_sendButton != NULL)
    {
        m_sendButton->SetText(ForwardPickerConfirmText(m_request).c_str());
    }
    if (m_targetTitle != NULL)
    {
        m_targetTitle->SetText(ForwardPickerTargetTitle(m_request).c_str());
    }
    if (m_contentTitle != NULL)
    {
        m_contentTitle->SetText(ForwardPickerContentTitle(m_request).c_str());
    }
    SwitchTab(kTabRecent);
    RebuildSelected();

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("Forward picker opened, merge=%d, messages=%u, recent=%u, groups=%u, contacts=%u.",
             m_request.m_merge ? 1 : 0, (unsigned)m_request.m_msgids.size(),
             (unsigned)m_recent.size(), (unsigned)m_groups.size(), (unsigned)m_contacts.size());
}

// =======================================================================
// 骨架
// =======================================================================

void CForwardTargetDlg::BuildUi()
{
    // 窗口每次重建都会重建控件树，上一轮的借用指针随窗口销毁而失效，先全部清空
    m_searchInput = NULL;
    m_tabs[kTabRecent] = NULL;
    m_tabs[kTabContacts] = NULL;
    m_tabs[kTabGroups] = NULL;
    m_listScroll = NULL;
    m_treeScroll = NULL;
    m_tree = NULL;
    m_chipScroll = NULL;
    m_previewLabel = NULL;
    m_countLabel = NULL;
    m_sendButton = NULL;

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kRootPadding);
    root->SetGap(kRootGap);

    std::unique_ptr<balloonwjui::DuiHBox> top(new balloonwjui::DuiHBox());
    top->SetGap(kColumnGap);

    // ---- 左列：搜索框 + 页签 + 候选区 ----
    {
        std::unique_ptr<balloonwjui::DuiVBox> left(new balloonwjui::DuiVBox());
        left->SetGap(kInnerGap);

        std::unique_ptr<balloonwjui::DuiEdit> search(new balloonwjui::DuiEdit());
        search->SetCtrlId(kIdSearch);
        search->SetPlaceholder(TR(L"搜索"));
        search->SetBgColor(kWhite);
        m_searchInput = search.get();
        left->AddChild(std::move(search), balloonwjui::DuiLayout::Hint().Fixed(kEditRowH));

        std::unique_ptr<balloonwjui::DuiHBox> tabs(new balloonwjui::DuiHBox());
        tabs->SetGap(kTabGap);
        const UINT tabIds[] = { kIdTabRecent, kIdTabContacts, kIdTabGroups };
        const LPCTSTR tabLabels[] = { TR(L"最近会话"), TR(L"联系人"), TR(L"群组") };
        for (int i = 0; i < 3; ++i)
        {
            std::unique_ptr<balloonsearch::SearchFilterTab> tab(new balloonsearch::SearchFilterTab());
            tab->SetCtrlId(tabIds[i]);
            tab->SetLabel(tabLabels[i]);
            m_tabs[i] = tab.get();
            tabs->AddChild(std::move(tab), balloonwjui::DuiLayout::Hint().Fixed(kTabWidth));
        }
        std::unique_ptr<balloonwjui::DuiLabel> tabSpacer(new balloonwjui::DuiLabel());
        tabs->AddChild(std::move(tabSpacer), balloonwjui::DuiLayout::Hint().Weight(1));
        left->AddChild(std::move(tabs), balloonwjui::DuiLayout::Hint().Fixed(kTabRowH));

        // 「最近会话」「群组」页共用一个候选区，切页时重建内容
        std::unique_ptr<balloonwjui::DuiScrollView> list(new balloonwjui::DuiScrollView());
        list->SetCtrlId(kIdListScroll);
        list->SetAutoContentHeight(true);
        list->SetContent(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiVBox()));
        m_listScroll = list.get();
        left->AddChild(std::move(list), balloonwjui::DuiLayout::Hint().Weight(1));

        // 「联系人」页：组织架构树。树自己不滚、也不报内容高度，由 SyncTreeHeight 显式设
        std::unique_ptr<balloonwjui::DuiScrollView> treeScroll(new balloonwjui::DuiScrollView());
        treeScroll->SetCtrlId(kIdTreeScroll);
        treeScroll->SetAutoContentHeight(false);
        std::unique_ptr<MemberPickerTreeView> tree(new MemberPickerTreeView());
        tree->SetCtrlId(kIdTree);
        tree->SetRowHeight(kTreeRowH);
        m_tree = tree.get();
        treeScroll->SetContent(std::unique_ptr<balloonwjui::DuiControl>(tree.release()));
        treeScroll->SetVisible(false);
        m_treeScroll = treeScroll.get();
        left->AddChild(std::move(treeScroll), balloonwjui::DuiLayout::Hint().Weight(1));

        top->AddChild(std::move(left), balloonwjui::DuiLayout::Hint().Weight(1));
    }

    // ---- 右列：已选目标 + 转发内容 ----
    {
        std::unique_ptr<balloonwjui::DuiVBox> right(new balloonwjui::DuiVBox());
        right->SetGap(kInnerGap);

        std::unique_ptr<balloonwjui::DuiLabel> toTitle(new balloonwjui::DuiLabel());
        toTitle->SetText(TR(L"发送给"));
        toTitle->SetTextColor(kSectionColor);
        toTitle->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        m_targetTitle = toTitle.get();
        right->AddChild(std::move(toTitle), balloonwjui::DuiLayout::Hint().Fixed(kSectionH));

        std::unique_ptr<balloonwjui::DuiScrollView> chip(new balloonwjui::DuiScrollView());
        chip->SetCtrlId(kIdChipScroll);
        chip->SetAutoContentHeight(true);
        chip->SetContent(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiVBox()));
        m_chipScroll = chip.get();
        right->AddChild(std::move(chip), balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiLabel> contentTitle(new balloonwjui::DuiLabel());
        contentTitle->SetText(TR(L"转发内容"));
        m_contentTitle = contentTitle.get();
        contentTitle->SetTextColor(kSectionColor);
        contentTitle->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        right->AddChild(std::move(contentTitle), balloonwjui::DuiLayout::Hint().Fixed(kSectionH));

        std::unique_ptr<balloonwjui::DuiLabel> preview(new balloonwjui::DuiLabel());
        preview->SetCtrlId(kIdPreview);
        preview->SetWordWrap(true);
        preview->SetTextAlign(DT_LEFT | DT_TOP);
        m_previewLabel = preview.get();
        right->AddChild(std::move(preview), balloonwjui::DuiLayout::Hint().Fixed(kPreviewH));

        top->AddChild(std::move(right), balloonwjui::DuiLayout::Hint().Weight(1));
    }

    root->AddChild(std::move(top), balloonwjui::DuiLayout::Hint().Weight(1));

    // ---- 底部栏：已选计数 | 取消 | 发送 ----
    {
        std::unique_ptr<balloonwjui::DuiHBox> bottom(new balloonwjui::DuiHBox());
        bottom->SetGap(kInnerGap);

        std::unique_ptr<balloonwjui::DuiLabel> count(new balloonwjui::DuiLabel());
        count->SetCtrlId(kIdCount);
        count->SetTextColor(kSectionColor);
        count->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        m_countLabel = count.get();
        bottom->AddChild(std::move(count), balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiButton> cancel(new balloonwjui::DuiButton());
        cancel->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        cancel->SetVariant(balloonwjui::DuiButton::Variant::Default);
        cancel->SetCtrlId(kIdCancel);
        cancel->SetText(TR(L"取消"));
        bottom->AddChild(std::move(cancel), balloonwjui::DuiLayout::Hint().Fixed(kCancelW));

        std::unique_ptr<balloonwjui::DuiButton> send(new balloonwjui::DuiButton());
        send->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        send->SetVariant(balloonwjui::DuiButton::Variant::Primary);
        send->SetCtrlId(kIdSend);
        send->SetText(TR(L"发送"));
        m_sendButton = send.get();
        bottom->AddChild(std::move(send), balloonwjui::DuiLayout::Hint().Fixed(kSendW));

        root->AddChild(std::move(bottom), balloonwjui::DuiLayout::Hint().Fixed(kBottomH));
    }

    SetClientContent(std::move(root));
}

// =======================================================================
// 页签与候选
// =======================================================================

void CForwardTargetDlg::SwitchTab(Tab tab)
{
    m_tab = tab;
    for (int i = 0; i < 3; ++i)
    {
        if (m_tabs[i] != NULL)
        {
            m_tabs[i]->SetActive(i == (int)tab);
        }
    }

    const bool contacts = (tab == kTabContacts);
    if (m_listScroll != NULL)
    {
        m_listScroll->SetVisible(!contacts);
    }
    if (m_treeScroll != NULL)
    {
        m_treeScroll->SetVisible(contacts);
    }

    // SetVisible 只改标志位、不重排；而竖直布局排版时跳过不可见的子控件。两个候选区
    // 此刻互换了显隐，必须重排一次，否则刚显示出来的那个停在过时（或从未设过）的矩形上
    // （开发约定文档「UI 约定」第 5 条）。
    //
    // 重排的对象必须是两个候选区的<u>直接父容器</u>（左列），不能是客户区根容器：
    // 根容器 ForceLayout 只强制它自己重排，给子容器调的是 SetRect，而左列的矩形没变，
    // SetRect 直接返回，左列里的子控件不会重排。实测只重排根容器时，「联系人」页的滚动区
    // 矩形一直是 (0,0,0,0)，整页空白。
    // 先重排再重建内容：内容重建要用到滚动区的最新矩形
    balloonwjui::DuiControl* column = (m_listScroll != NULL) ? m_listScroll->GetParent() : NULL;
    if (column != NULL)
    {
        const RECT rc = column->GetRect();
        if (rc.right > rc.left && rc.bottom > rc.top)
        {
            column->ForceLayout(rc);
        }
    }

    if (contacts)
    {
        RebuildContactTree();
    }
    else
    {
        RebuildListRows();
    }

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, TRUE);
    }
}

void CForwardTargetDlg::RebuildListRows()
{
    if (m_listScroll == NULL)
    {
        return;
    }
    m_rowIndexByCheckId.clear();
    m_checkById.clear();

    const std::vector<ForwardTarget>& source = (m_tab == kTabGroups) ? m_groups : m_recent;
    const std::wstring query = CurrentQuery();
    const std::vector<size_t> shown = FilterForwardTargets(source, query);

    std::unique_ptr<balloonwjui::DuiVBox> content(new balloonwjui::DuiVBox());
    if (shown.empty())
    {
        // 一个候选都没有：放一行灰色提示
        std::unique_ptr<balloonwjui::DuiLabel> empty(new balloonwjui::DuiLabel());
        const bool filtering = (FilterForwardTargets(source, std::wstring()).size() != 0);
        if (filtering)
        {
            empty->SetText(TR(L"无匹配结果"));
        }
        else
        {
            empty->SetText((m_tab == kTabGroups) ? TR(L"暂无群组") : TR(L"暂无最近会话"));
        }
        empty->SetTextColor(kSectionColor);
        empty->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        content->AddChild(std::move(empty), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    }

    UINT checkId = kRowCheckBase;
    for (size_t k = 0; k < shown.size(); ++k)
    {
        const ForwardTarget& t = source[shown[k]];

        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kInnerGap);

        std::unique_ptr<balloonwjui::DuiButton> box(new ForwardPickCheckbox());
        box->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
        // Ghost 变体：复选框只画小勾选框，不铺整格的浅色底（与 CreateGroupDlg 相同）
        box->SetVariant(balloonwjui::DuiButton::Variant::Ghost);
        box->SetText(_T(""));
        box->SetCtrlId(checkId);
        box->SetCheck(m_selection.IsSelected(t.m_ctype, t.m_cid), false);
        m_checkById[checkId] = box.get();
        m_rowIndexByCheckId[checkId] = shown[k];
        row->AddChild(std::move(box), balloonwjui::DuiLayout::Hint().Fixed(kCheckColW));

        // 头像固定为正方形并在行内垂直居中：只给主轴尺寸时交叉轴会被拉伸成长条
        row->AddChild(std::unique_ptr<balloonwjui::DuiControl>(MakeAvatar(t).release()),
                      balloonwjui::DuiLayout::Hint().Fixed(kAvatarSize, kAvatarSize)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));

        std::unique_ptr<balloonwjui::DuiLabel> name(new balloonwjui::DuiLabel());
        name->SetText(t.m_name.c_str());
        name->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        row->AddChild(std::move(name), balloonwjui::DuiLayout::Hint().Weight(1));

        content->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
        ++checkId;
    }

    m_listScroll->SetContent(std::move(content));
    const RECT rc = m_listScroll->GetRect();
    if (rc.right > rc.left && rc.bottom > rc.top)
    {
        m_listScroll->ForceLayout(rc);
    }
}

void CForwardTargetDlg::RebuildContactTree()
{
    if (m_tree == NULL)
    {
        return;
    }

    // 整表重建：清节点与行控件，再清本类维护的节点映射（节点 id 会被复用，不清会串数据）
    m_tree->Clear();
    m_tree->ClearAllRowDecorations();
    m_treeNodes.clear();
    m_nodeIdByIndex.clear();
    m_indexByNodeId.clear();

    m_treeNodes = BuildMemberPickerTree(m_contacts, m_departments, (UINT)m_selfUserId,
                                        CString(CurrentQuery().c_str()));
    if (m_treeNodes.empty())
    {
        const int hintId = m_tree->AddRoot(
            CurrentQuery().empty() ? CString(TR(L"暂无联系人")) : CString(TR(L"无匹配结果")));
        m_tree->SetItemSelectable(hintId, false);
        SyncTreeHeight();
        return;
    }

    m_nodeIdByIndex.assign(m_treeNodes.size(), -1);
    for (size_t i = 0; i < m_treeNodes.size(); ++i)
    {
        const MemberPickerNode& n = m_treeNodes[i];

        // 节点数组按显示顺序排列，父节点一定排在子节点前面，父节点的树 id 必已就绪
        int nodeId = -1;
        if (n.m_parent < 0)
        {
            nodeId = m_tree->AddRoot(n.m_label);
        }
        else
        {
            nodeId = m_tree->AddChild(m_nodeIdByIndex[n.m_parent], n.m_label);
        }
        if (nodeId < 0)
        {
            continue;
        }
        m_nodeIdByIndex[i] = nodeId;
        m_indexByNodeId[nodeId] = (int)i;
        m_tree->SetExpanded(nodeId, true);

        // 部门行不能作为转发目标：不画头像、不画复选框
        std::unique_ptr<MemberPickerRow> row(new MemberPickerRow());
        if (n.m_kind == kPickerNodeMember)
        {
            const AvatarSpec spec = DecodeAvatar(0, std::string(), n.m_label, (int64_t)n.m_userId);
            row->SetRowData(n.m_label, spec.m_glyph, spec.m_color);
            row->SetCheckState(m_selection.IsSelected(conversation_single, (int64_t)n.m_userId)
                               ? kPickerChecked : kPickerUnchecked);
        }
        else
        {
            row->SetRowData(n.m_label, _T(""), RGB(0, 0, 0));
            row->SetCheckVisible(false);
        }
        m_tree->SetItemCustomControl(nodeId, std::unique_ptr<balloonwjui::DuiControl>(row.release()));
    }
    SyncTreeHeight();

    const RECT view = (m_treeScroll != NULL) ? m_treeScroll->GetRect() : RECT();
    LOG_INFO("Forward picker: contact tree rebuilt, nodes=%u, contentH=%d, view=(%d,%d,%d,%d).",
             (unsigned)m_treeNodes.size(), m_tree->GetContentHeight(),
             view.left, view.top, view.right, view.bottom);
}

void CForwardTargetDlg::SyncTreeHeight()
{
    if (m_treeScroll == NULL || m_tree == NULL)
    {
        return;
    }
    int h = m_tree->GetContentHeight();
    if (h < 0)
    {
        h = 0;
    }
    m_treeScroll->SetContentHeight(h);
    const RECT rc = m_treeScroll->GetRect();
    if (rc.right > rc.left && rc.bottom > rc.top)
    {
        m_treeScroll->ForceLayout(rc);
    }
}

// =======================================================================
// 选择
// =======================================================================

void CForwardTargetDlg::ToggleTarget(const ForwardTarget& target)
{
    //发起通话只能选一个：勾选新的目标时先清掉原先选的那个
    if (ForwardPickerSingleSelect(m_request) && !m_selection.IsSelected(target.m_ctype, target.m_cid))
    {
        m_selection.Clear();
    }
    const ForwardToggleResult result = m_selection.Toggle(target);
    if (result == kForwardToggleFull)
    {
        CString tip;
        tip.Format(TR(L"最多只能选择 %u 个聊天。"), (unsigned)kForwardMaxTargets);
        ::MessageBox(m_hWnd, tip, TR(L"转发"), MB_OK | MB_ICONINFORMATION);
    }
    // 复选框点击时已经自行翻转了勾选态；选满或不能选时要恢复，统一按已选集合重设
    RefreshCheckStates();
    RebuildSelected();
}

void CForwardTargetDlg::OnTreeCheckToggled(int nodeId)
{
    std::map<int, int>::const_iterator it = m_indexByNodeId.find(nodeId);
    if (it == m_indexByNodeId.end() || it->second < 0 || it->second >= (int)m_treeNodes.size())
    {
        return;
    }
    const MemberPickerNode& n = m_treeNodes[it->second];
    // 部门行不能作为转发目标（也不画复选框），点到复选框条带时忽略
    if (n.m_kind != kPickerNodeMember)
    {
        return;
    }

    const net::ContactItem* c = FindContact(n.m_userId);
    if (c != NULL)
    {
        ToggleTarget(TargetFromContact(*c));
        return;
    }
    // 通讯录里查不到（不应发生）：用树上的显示名兜底
    ForwardTarget t;
    t.m_ctype = conversation_single;
    t.m_cid = (int64_t)n.m_userId;
    t.m_name = (LPCWSTR)n.m_label;
    ToggleTarget(t);
}

void CForwardTargetDlg::RefreshCheckStates()
{
    // 候选行复选框
    const std::vector<ForwardTarget>& source = (m_tab == kTabGroups) ? m_groups : m_recent;
    for (std::map<UINT, size_t>::const_iterator it = m_rowIndexByCheckId.begin();
         it != m_rowIndexByCheckId.end(); ++it)
    {
        std::map<UINT, balloonwjui::DuiButton*>::iterator box = m_checkById.find(it->first);
        if (box == m_checkById.end() || box->second == NULL || it->second >= source.size())
        {
            continue;
        }
        const ForwardTarget& t = source[it->second];
        box->second->SetCheck(m_selection.IsSelected(t.m_ctype, t.m_cid), false);
    }

    // 组织架构树里的成员行
    if (m_tree != NULL)
    {
        for (size_t i = 0; i < m_treeNodes.size() && i < m_nodeIdByIndex.size(); ++i)
        {
            if (m_treeNodes[i].m_kind != kPickerNodeMember || m_nodeIdByIndex[i] < 0)
            {
                continue;
            }
            MemberPickerRow* row = dynamic_cast<MemberPickerRow*>(
                m_tree->GetItemCustomControl(m_nodeIdByIndex[i]));
            if (row == NULL)
            {
                continue;
            }
            row->SetCheckState(m_selection.IsSelected(conversation_single,
                                                      (int64_t)m_treeNodes[i].m_userId)
                               ? kPickerChecked : kPickerUnchecked);
        }
        m_tree->Invalidate();
    }
}

void CForwardTargetDlg::RebuildSelected()
{
    const std::vector<ForwardTarget>& selected = m_selection.Selected();

    if (m_chipScroll != NULL)
    {
        m_removeIndexById.clear();
        std::unique_ptr<balloonwjui::DuiVBox> content(new balloonwjui::DuiVBox());
        content->SetGap(kChipGap);

        UINT removeId = kChipRemoveBase;
        for (size_t i = 0; i < selected.size(); ++i)
        {
            const ForwardTarget& t = selected[i];
            std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
            row->SetGap(kInnerGap);

            row->AddChild(std::unique_ptr<balloonwjui::DuiControl>(MakeAvatar(t).release()),
                          balloonwjui::DuiLayout::Hint().Fixed(kChipAvatar, kChipAvatar)
                              .AlignC(balloonwjui::DuiLayout::AlignCenter));

            std::unique_ptr<balloonwjui::DuiLabel> name(new balloonwjui::DuiLabel());
            name->SetText(t.m_name.c_str());
            name->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            row->AddChild(std::move(name), balloonwjui::DuiLayout::Hint().Weight(1));

            std::unique_ptr<balloonwjui::DuiButton> rm(new balloonwjui::DuiButton());
            rm->SetButtonType(balloonwjui::DuiButton::StylePushButton);
            rm->SetVariant(balloonwjui::DuiButton::Variant::Text);
            rm->SetCtrlId(removeId);
            rm->SetText(_T("✕"));
            row->AddChild(std::move(rm), balloonwjui::DuiLayout::Hint().Fixed(kChipRemoveW));

            m_removeIndexById[removeId] = i;
            ++removeId;
            content->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kChipRowH));
        }

        m_chipScroll->SetContent(std::move(content));
        const RECT rc = m_chipScroll->GetRect();
        if (rc.right > rc.left && rc.bottom > rc.top)
        {
            m_chipScroll->ForceLayout(rc);
        }
    }

    if (m_countLabel != NULL)
    {
        CString text;
        if (ForwardPickerSingleSelect(m_request))
        {
            text = selected.empty() ? CString(TR(L"请选择一个联系人或群")) : CString(TR(L"已选 1 个"));
        }
        else
        {
            text.Format(TR(L"已选 %u 个（最多 %u 个）"), (unsigned)selected.size(),
                        (unsigned)kForwardMaxTargets);
        }
        m_countLabel->SetText(text);
    }
    if (m_sendButton != NULL)
    {
        m_sendButton->SetEnabled(!selected.empty());
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

// =======================================================================
// 发送 / 关闭
// =======================================================================

void CForwardTargetDlg::DoSend()
{
    if (m_selection.Selected().empty())
    {
        return;
    }

    // 先复制一份再关窗：CloseDialog 之后本类成员仍在，但接收方可能在回调里重新打开本窗口
    const ForwardRequest request = m_request;
    const std::vector<ForwardTarget> targets = m_selection.Selected();
    IForwardTargetSink* sink = m_sink;
    CloseDialog();

    LOG_INFO("Forward picker confirmed, merge=%d, messages=%u, targets=%u.",
             request.m_merge ? 1 : 0, (unsigned)request.m_msgids.size(), (unsigned)targets.size());
    if (sink != NULL)
    {
        sink->OnForwardTargetsConfirmed(request, targets);
    }
}

void CForwardTargetDlg::CloseDialog()
{
    m_sink = NULL;
    if (IsWindow())
    {
        DestroyWindow();
    }
}

LRESULT CForwardTargetDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                      LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 关闭窗口即取消转发：只销毁窗口，对象由主面板持有、下次复用
    LOG_INFO("Forward picker cancelled.");
    CloseDialog();
    return 0;
}

LRESULT CForwardTargetDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                       LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // 搜索词变化：重建当前页的候选
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kIdSearch)
    {
        if (m_tab == kTabContacts)
        {
            RebuildContactTree();
        }
        else
        {
            RebuildListRows();
        }
        if (IsWindow())
        {
            ::InvalidateRect(m_hWnd, NULL, FALSE);
        }
        return 0;
    }

    // 组织架构树里某行的复选框被点。自定义通知码按控件各自编号，须连 ctrlId 一起判
    // （开发约定文档「UI 约定」第 4 条）
    if (pNotify->code == (UINT)MemberPickerTreeView::DUIN_PICKER_CHECK_TOGGLE
        && pNotify->ctrlId == kIdTree)
    {
        if (m_tree != NULL)
        {
            OnTreeCheckToggled(m_tree->GetToggledNodeId());
        }
        return 0;
    }

    // 用户展开 / 收起了某个部门：树的内容总高变了
    if (pNotify->code == (UINT)MemberPickerTreeView::DUIN_PICKER_HEIGHT_CHANGED
        && pNotify->ctrlId == kIdTree)
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
        // 页签
        if (pNotify->ctrlId == kIdTabRecent)
        {
            SwitchTab(kTabRecent);
            return 0;
        }
        if (pNotify->ctrlId == kIdTabContacts)
        {
            SwitchTab(kTabContacts);
            return 0;
        }
        if (pNotify->ctrlId == kIdTabGroups)
        {
            SwitchTab(kTabGroups);
            return 0;
        }

        // 候选行的复选框
        std::map<UINT, size_t>::const_iterator row = m_rowIndexByCheckId.find(pNotify->ctrlId);
        if (row != m_rowIndexByCheckId.end())
        {
            const std::vector<ForwardTarget>& source = (m_tab == kTabGroups) ? m_groups : m_recent;
            if (row->second < source.size())
            {
                ToggleTarget(source[row->second]);
            }
            return 0;
        }

        // 已选区的 ✕
        std::map<UINT, size_t>::const_iterator rm = m_removeIndexById.find(pNotify->ctrlId);
        if (rm != m_removeIndexById.end())
        {
            const std::vector<ForwardTarget>& selected = m_selection.Selected();
            if (rm->second < selected.size())
            {
                const ForwardTarget t = selected[rm->second];
                m_selection.Remove(t.m_ctype, t.m_cid);
                RefreshCheckStates();
                RebuildSelected();
            }
            return 0;
        }

        if (pNotify->ctrlId == kIdCancel)
        {
            LOG_INFO("Forward picker cancelled.");
            CloseDialog();
            return 0;
        }
        if (pNotify->ctrlId == kIdSend)
        {
            DoSend();
            return 0;
        }
    }

    bHandled = FALSE;
    return 0;
}

// =======================================================================
// 辅助
// =======================================================================

std::wstring CForwardTargetDlg::CurrentQuery() const
{
    if (m_searchInput == NULL)
    {
        return std::wstring();
    }
    const CString text = m_searchInput->GetText();
    return std::wstring((LPCWSTR)text);
}

CString CForwardTargetDlg::PreviewText() const
{
    //组装规则在 ForwardTargetModel 里（纯函数，有单测 FWDD4）
    return CString(BuildForwardPreviewText(m_request).c_str());
}

ForwardTarget CForwardTargetDlg::TargetFromContact(const net::ContactItem& c) const
{
    ForwardTarget t;
    t.m_ctype = conversation_single;
    t.m_cid = (int64_t)c.m_userid;
    // 「真名（备注）」，与通讯录、会话列表同一口径
    const CString display = ResolveContactDisplayName(
        Utf8ToCStr(c.m_remark), Utf8ToCStr(c.m_nickname), Utf8ToCStr(c.m_username), (UINT)c.m_userid);
    t.m_name = (LPCWSTR)display;
    t.m_account = (LPCWSTR)Utf8ToCStr(c.m_username);
    // 已在「最近会话」里的联系人借用那里的头像位图，已选区显示真实头像
    for (size_t i = 0; i < m_recent.size(); ++i)
    {
        if (m_recent[i].m_ctype == t.m_ctype && m_recent[i].m_cid == t.m_cid)
        {
            t.m_avatar = m_recent[i].m_avatar;
            break;
        }
    }
    return t;
}

const net::ContactItem* CForwardTargetDlg::FindContact(UINT userId) const
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

} // namespace balloonmain
