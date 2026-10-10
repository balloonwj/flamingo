/**
 *  ThreadMemberPanel.h
 *
 *  帖子群窗口右侧的群成员面板（docs/帖子群方案.md 14.3，第 5e 步）。帖子群窗口在 FlamingoThreadGroup.dll 里，
 *  成员列表的控件（GroupMemberList、GroupMemberItem、按部门的部门树）在 exe 里，两边的 balloonui 控件树不能混用，所以
 *  面板由 exe 建成帖子群窗口的子窗口，盖在窗口右侧：
 *    - CThreadMemberPanel：容器子窗口（父窗口是 DLL 的帖子群窗口），负责位置、滑入动画，并接收面板里控件的通知
 *      （DuiHost::SendNotify 把通知发给宿主的父窗口，所以通知落在 exe 的这个容器上，不会进入 DLL 的窗口）；
 *    - CThreadMemberPanelHost：容器里的 balloonui 宿主（子窗口），挂着面板的控件树；成员行的右键菜单与双击投来的
 *      WM_CHAT_MENTION_ACTION 由它接住，按 Esc 时收起面板。
 *  内容与普通群群聊窗的「成员」页签相同：标题（群成员人数）与关闭按钮、搜索框、「平铺 / 按部门」切换、成员列表；
 *  成员行的建法、排序、部门树与右键菜单动作都经 GroupMemberPanelKit 与群聊窗共用，只是右键菜单里没有「@TA」。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  典型用法（主面板）：
 *      std::unique_ptr<CThreadMemberPanel>& panel = m_threadMemberPanels[groupId];
 *      if (!panel) panel.reset(new CThreadMemberPanel(groupId, this, m_hWnd));
 *      panel->Show(threadWindow, rc);                      //帖子群窗口点了「成员」
 *      panel->SetMembers(members.m_members, selfId, role); //1304 应答到达
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <map>
#include <memory>
#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../App/CustomMsgDef.h"   // WM_CHAT_MENTION_ACTION：成员行的右键动作
#include "GroupMemberPanelKit.h"
#include "GroupMentionMenu.h"   // MentionActionData
#include "../../net/GroupProtocolClient.h"      // net::GroupMemberItem（成员需完整类型）
#include "../../net/ContactsProtocolClient.h"   // net::DepartmentItem（成员需完整类型）

namespace balloonchatwnd {

class GroupMemberList;

/**
 *  群成员面板的所有者（主面板实现）。只在界面线程上调用。
 */
class IThreadMemberPanelOwner
{
public:
    virtual ~IThreadMemberPanelOwner()
    {
    }

    //列表 / 按部门的当前选择：与群聊窗「成员」页签共用同一个配置项（[GroupDlg] MemberView）
    virtual bool IsMemberTreeView() const = 0;

    //用户在面板里切换了列表 / 按部门：写进配置
    virtual void SetMemberTreeView(bool treeMode) = 0;

    //面板自己收起了（点了关闭按钮或按了 Esc）：所有者转告帖子群窗口，更新「成员」按钮的状态
    virtual void OnMemberPanelClosed(int64_t groupId) = 0;
};

/**
 *  成员面板里的 balloonui 宿主（子窗口）：接住成员行投来的 WM_CHAT_MENTION_ACTION，按 Esc 时请求收起面板。
 */
class CThreadMemberPanelHost : public balloonwjui::DuiHost
{
public:
    /**
     *  成员行的右键菜单或双击选了一项。
     *  @param user SetHandlers 时传入的指针
     *  @param data 选中的动作（借用，回调返回后由本宿主释放）
     */
    typedef void (*ActionFn)(void* user, const MentionActionData& data);

    //按了 Esc
    typedef void (*EscapeFn)(void* user);

    CThreadMemberPanelHost();

    //登记两个回调（user 借用）
    void SetHandlers(ActionFn action, EscapeFn escape, void* user);

    BEGIN_MSG_MAP(CThreadMemberPanelHost)
        MESSAGE_HANDLER(WM_CHAT_MENTION_ACTION, OnMentionAction)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        CHAIN_MSG_MAP(balloonwjui::DuiHost)
    END_MSG_MAP()

private:
    //WM_CHAT_MENTION_ACTION：lParam 为堆上的 MentionActionData*，交给回调后释放
    LRESULT OnMentionAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_KEYDOWN：Esc 交给回调，其余按键照常交给控件
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    ActionFn m_actionFn;   //右键动作回调；未登记时动作被丢弃
    EscapeFn m_escapeFn;   //Esc 回调；未登记时 Esc 照常交给控件
    void*    m_user;       //两个回调的 user（借用）
};

/**
 *  CThreadMemberPanel —— 一个帖子群的群成员面板（容器子窗口）。只在界面线程上使用，由主面板持有。
 */
class CThreadMemberPanel : public CWindowImpl<CThreadMemberPanel>
{
public:
    DECLARE_WND_CLASS(L"FlamingoThreadMemberPanel")

    /**
     *  @param groupId   帖子群
     *  @param owner     所有者（主面板，借用）
     *  @param hostPanel 主面板窗口：成员动作（发消息、资料、特别关注、转让群主等）与关注名单的查询都经它
     */
    CThreadMemberPanel(int64_t groupId, IThreadMemberPanelOwner* owner, HWND hostPanel);
    ~CThreadMemberPanel();

    //帖子群
    int64_t GroupId() const;

    /**
     *  显示面板：第一次时在 parent 下建子窗口；从右边缘滑入到 rc。已经显示时只调整位置。
     *  @param parent 帖子群窗口
     *  @param rc     面板在帖子群窗口客户区里的矩形（贴右、标题栏以下到底部）
     *  @return 窗口已显示返回 true
     */
    bool Show(HWND parent, const RECT& rc);

    //帖子群窗口改变了大小：面板跟随到新的矩形（滑入途中改为直接到位）
    void Move(const RECT& rc);

    //收起面板（不通知所有者；所有者或帖子群窗口要求收起时用）
    void HidePanel();

    //面板是否正显示着
    bool IsShown() const;

    /**
     *  换上成员名单（1304 应答到达）。
     *  @param members 成员，按服务端给的顺序
     *  @param selfId  本人 userid
     *  @param myRole  本人在本群的角色（决定右键菜单里的管理项）
     */
    void SetMembers(const std::vector<net::GroupMemberItem>& members, int64_t selfId, int myRole);

    //企业的部门数组（按部门显示用）；按部门显示且成员已到时重建
    void SetDepartments(const std::vector<net::DepartmentItem>& depts);

    //某个成员的在线状态变了
    void SetMemberPresence(int64_t userId, int presence);

    /**
     *  有头像照片下载完成：有成员用的正是这张照片时，按成员的头像设置重新取路径并重建成员行。
     *  @param fileid 下载完成的头像 fileid（UTF-8）；为空时无条件重建
     */
    void RefreshAvatars(const std::string& fileid);

    BEGIN_MSG_MAP(CThreadMemberPanel)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()

private:
    //WM_DUI_NOTIFY：搜索框文字变了就过滤；切换按钮换视图；关闭按钮收起面板（通知码与控件 id 一起判，UI 约定第 4 条）
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_SIZE：balloonui 宿主铺满容器
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_TIMER：滑入动画的一步
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_ERASEBKGND：整块由 balloonui 宿主画，不擦背景，避免闪烁
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DESTROY：面板随帖子群窗口一起销毁（用户关掉了帖子群窗口）时，控件树也随之释放：清空借用的控件指针，
    //之后名单再到达只更新数据，下次 Show 时重建子窗口
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //建子窗口与控件树（只做一次）
    bool CreateOnce(HWND parent, const RECT& rc);

    //按当前的成员、视图与上下文重建成员行，并刷新标题里的人数
    void RebuildRows();

    //建成员行用的上下文
    GroupMemberRowContext RowContext();

    //用户自己收起（关闭按钮、Esc）：收起并通知所有者
    void CloseByUser();

    //两个宿主回调（user 为本面板）
    static void ActionThunk(void* user, const MentionActionData& data);
    static void EscapeThunk(void* user);

    //右键菜单前问主面板「是否特别关注」「是否关注其在本群的发言」的钩子（userdata 为本面板）
    static bool StarredThunk(void* userdata, long long uid);
    static bool GroupFollowThunk(void* userdata, long long uid);

private:
    int64_t                     m_groupId;      //帖子群
    IThreadMemberPanelOwner*    m_owner;        //所有者（借用）
    HWND                        m_hostPanel;    //主面板窗口（借用）
    CThreadMemberPanelHost      m_host;         //balloonui 宿主（子窗口），本对象持有
    GroupMemberList*            m_list;         //成员列表（归控件树所有，宿主窗口销毁后失效）
    balloonwjui::DuiControl*    m_title;        //标题「群成员（N）」（同上）
    std::vector<net::GroupMemberItem> m_members;          //成员（最近一次 SetMembers 的）
    std::map<int64_t, GroupMemberRowInfo> m_rowInfo;      //成员的显示名与头像
    std::map<int64_t, int>      m_presence;     //userid → 在线状态
    std::vector<net::DepartmentItem> m_depts;   //企业的部门数组
    int64_t                     m_selfId;       //本人 userid
    int                         m_myRole;       //本人在本群的角色
    bool                        m_membersLoaded;//成员名单是否已到（之前显示「正在加载成员…」的空列表）
    RECT                        m_targetRect;   //面板应在的位置（帖子群窗口客户区坐标）
    int                         m_slideX;       //滑入动画当前的左边位置；动画结束后等于 m_targetRect.left
};

} // namespace balloonchatwnd
