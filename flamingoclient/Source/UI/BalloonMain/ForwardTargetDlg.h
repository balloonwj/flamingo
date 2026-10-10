/**
 *  ForwardTargetDlg.h —— 转发目标选择窗。
 *
 *  聊天窗在多选操作条上点「逐条转发」或「合并转发」后，由主面板打开本窗口：
 *  左侧是搜索框与「最近会话 / 联系人 / 群组」三个页签，右侧是已选目标与转发内容预览，
 *  底部是「取消 / 发送」。用户点「发送」后，本窗口把转发请求与选定的目标交给
 *  IForwardTargetSink（主面板），自己关闭；实际发出 cmd_forward_message 的是主面板的
 *  转发调度器。交互见 docs/聊天消息转发方案.md §3.2。
 *
 *  界面骨架与「新建群聊」对话框（CCreateGroupDlg）相同，控件全部是 balloonui 现成控件
 *  与客户端已有的组合：
 *    · 「最近会话」「群组」两页的行 = DuiButton(复选框) + DuiAvatar(头像) + DuiLabel(名字)；
 *    · 「联系人」页 = MemberPickerTreeView + MemberPickerRow（组织架构树），部门行不给复选框；
 *    · 页签 = SearchFilterTab；已选区 = DuiAvatar + DuiLabel + DuiButton(✕)。
 *  已选集合与候选过滤的规则在纯逻辑模型 ForwardTargetModel 里，有单元测试。
 *
 *  典型用法（主面板）：
 *      m_forwardDlg.Open(this, selfId, request, recent, groups, contacts, departments);
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <map>
#include <set>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../net/ContactsProtocolClient.h"   // net::ContactItem / net::DepartmentItem
#include "ForwardRequest.h"
#include "TitleBarIcon.h"   // ProgramWindowIcons（任务栏图标）
#include "ForwardTargetModel.h"
#include "MemberPickerTreeModel.h"             // MemberPickerNode

namespace balloonwjui {
class DuiEdit;
class DuiLabel;
class DuiButton;
class DuiScrollView;
class DuiVBox;
}
namespace balloonsearch {
class SearchFilterTab;
}

namespace balloonmain {

class MemberPickerTreeView;

/**
 *  目标选择窗的结果接收方。主面板实现本接口；调试入口可以传 NULL，此时点「发送」只记日志。
 */
class IForwardTargetSink
{
public:
    virtual ~IForwardTargetSink()
    {
    }

    /**
     *  用户点了「发送」。调用发生在选择窗关闭之前，参数在调用期间有效，接收方需要时自行复制。
     *    request： 打开选择窗时传入的转发请求（源会话、msgid、模式、标题等）。
     *    targets： 选定的目标，按勾选先后排列，1 ~ kForwardMaxTargets 个。
     */
    virtual void OnForwardTargetsConfirmed(const ForwardRequest& request,
                                           const std::vector<ForwardTarget>& targets) = 0;
};

/**
 *  CForwardTargetDlg —— 转发目标选择窗（非模态浮窗，由主面板长期持有、反复打开）。
 */
class CForwardTargetDlg : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  左侧的三个页签。
     */
    enum Tab
    {
        kTabRecent   = 0,   // 最近会话：会话列表里的单聊与群聊
        kTabContacts = 1,   // 联系人：按组织架构分层的通讯录
        kTabGroups   = 2    // 群组：本人所在的群
    };

    CForwardTargetDlg();
    ~CForwardTargetDlg();

    /**
     *  打开选择窗；已打开时以新的请求与数据重置并前置。每次打开都清空已选目标与搜索词，
     *  并回到「最近会话」页。
     *    sink：        结果接收方，只引用不持有，须比本窗口活得久；可为 NULL（调试入口）。
     *    selfUserId：  本人 userid，候选里排除自己。
     *    request：     转发请求（本类复制一份）。
     *    recent：      「最近会话」候选，调用方已按 BuildRecentForwardTargets 整理（本类复制）。
     *    groups：      「群组」候选（本类复制）。
     *    contacts：    通讯录联系人（本类复制），用于「联系人」页的组织架构树。
     *    departments： 部门列表（本类复制）。
     *  候选里 ForwardTarget::m_avatar 位图只借用，须在本窗口关闭前保持有效。
     */
    void Open(IForwardTargetSink* sink, int64_t selfUserId, const ForwardRequest& request,
              const std::vector<ForwardTarget>& recent,
              const std::vector<ForwardTarget>& groups,
              const std::vector<net::ContactItem>& contacts,
              const std::vector<net::DepartmentItem>& departments);

    BEGIN_MSG_MAP(CForwardTargetDlg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 构建一次静态骨架（搜索框 / 页签 / 两个候选滚动区 / 已选区 / 预览 / 底部栏）。
    void BuildUi();

    // 切换到某个页签：刷新页签的选中态，显示对应的候选区并重建其内容，然后整体重排
    // （SetVisible 不会触发重排，见开发约定文档「UI 约定」第 5 条）。
    void SwitchTab(Tab tab);

    // 用当前搜索词重建「最近会话」或「群组」页的候选行（m_listScroll 的内容）。
    void RebuildListRows();

    // 用当前搜索词重建「联系人」页的组织架构树。
    void RebuildContactTree();

    // 把树在滚动区里的高度设为它的内容总高（树自己不滚，靠外层滚动区滚）。
    void SyncTreeHeight();

    // 按已选集合刷新候选行复选框与树里成员行的勾选态，不重建控件。
    void RefreshCheckStates();

    // 用已选集合重建右侧已选区，并刷新底部计数与「发送」按钮的可用状态。
    void RebuildSelected();

    // 切换某个目标的选中状态；选满时提示并把复选框恢复为未选。
    //   target：要切换的目标。
    void ToggleTarget(const ForwardTarget& target);

    // 处理组织架构树里某个节点的复选框被点：成员节点切换选中，部门节点忽略。
    //   nodeId：被点中的树节点 id。
    void OnTreeCheckToggled(int nodeId);

    // 点「发送」：把请求与已选目标交给 sink，然后关闭窗口。
    void DoSend();

    // 当前搜索词（去掉首尾空白前的原文）。
    std::wstring CurrentQuery() const;

    // 右侧「转发内容」一行的文字：合并转发为「[聊天记录] 标题」，逐条转发为「逐条转发 N 条消息」。
    CString PreviewText() const;

    // 把一个通讯录联系人转成转发目标（单聊）。
    ForwardTarget TargetFromContact(const net::ContactItem& c) const;

    // 在通讯录缓存里按 userid 取联系人；找不到返回 NULL。
    const net::ContactItem* FindContact(UINT userId) const;

    // 关闭并重置状态：销毁窗口，清空 sink，防止之后误用。
    void CloseDialog();

private:
    IForwardTargetSink*             m_sink;         // 结果接收方（借用）；NULL 表示调试入口
    balloonwjui::DuiLabel*          m_targetTitle;  // 右侧已选列表上方的小标题（借用，控件树持有；BuildUi 之前为 NULL）：按用途写「发送给」或「通话对象」
    balloonwjui::DuiLabel*          m_contentTitle; // 右侧说明区的小标题（借用，控件树持有；BuildUi 之前为 NULL）：按用途写「转发内容」或「说明」
    int64_t                         m_selfUserId;   // 本人 userid，每次 Open 时更新
    ForwardRequest                  m_request;      // 本次的转发请求（复制）
    std::vector<ForwardTarget>      m_recent;       // 「最近会话」候选（复制）
    std::vector<ForwardTarget>      m_groups;       // 「群组」候选（复制）
    std::vector<net::ContactItem>   m_contacts;     // 通讯录联系人（复制）
    std::vector<net::DepartmentItem> m_departments; // 部门列表（复制）
    ForwardTargetSelection          m_selection;    // 已选目标（三个页签共用）
    Tab                             m_tab;          // 当前页签

    // 当前「最近会话」或「群组」页上显示的候选：复选框控件 id → 候选在 m_recent / m_groups
    // 里的下标。每次重建候选行时刷新
    std::map<UINT, size_t>          m_rowIndexByCheckId;
    // 同上，复选框控件 id → 复选框控件（借用，候选区的内容容器持有），用于刷新勾选态
    std::map<UINT, balloonwjui::DuiButton*> m_checkById;
    // 已选区 ✕ 按钮控件 id → 目标在已选列表里的下标。每次重建已选区时刷新
    std::map<UINT, size_t>          m_removeIndexById;

    // 组织架构树的节点序列（BuildMemberPickerTree 的结果）与节点 id 的双向映射
    std::vector<MemberPickerNode>   m_treeNodes;
    std::vector<int>                m_nodeIdByIndex;
    std::map<int, int>              m_indexByNodeId;

    // 控件指针（借用，控件树持有；BuildUi 时赋值，窗口销毁后作废）
    balloonwjui::DuiEdit*           m_searchInput;  // 搜索框
    balloonsearch::SearchFilterTab* m_tabs[3];      // 三个页签，下标即 Tab 取值
    balloonwjui::DuiScrollView*     m_listScroll;   // 「最近会话」「群组」页的候选滚动区
    balloonwjui::DuiScrollView*     m_treeScroll;   // 「联系人」页的滚动区
    MemberPickerTreeView*           m_tree;         // 「联系人」页的组织架构树
    balloonwjui::DuiScrollView*     m_chipScroll;   // 右侧已选区
    balloonwjui::DuiLabel*          m_previewLabel; // 右侧转发内容预览
    balloonwjui::DuiLabel*          m_countLabel;   // 底部「已选 N 个」
    balloonwjui::DuiButton*         m_sendButton;   // 底部「发送」

    // 标题栏左侧图标位图。DuiFrameWindow::SetIcon 只借用不释放，故本类持有、析构时释放
    HBITMAP                         m_hTitleIcon;
    // 任务栏 / Alt-Tab 图标（2026-10-05 起窗口可最小化，最小化后从任务栏找回）；本对象持有，窗口先于它销毁
    ProgramWindowIcons              m_taskIcons;
};

} // namespace balloonmain
