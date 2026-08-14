/**
 *  BalloonGroupManageWindow.h
 *
 *  新版群管理窗口 —— 基于 balloonui，对照 design/群管理1.png ~ 群管理6.png。
 *  顶部 Header（群头像 + 群名 + 角色副标题）+ 左侧导航（成员管理 / 加群审批
 *  / 禁言管理 / 基本信息 / 群权限 / 危险区域）+ 右侧可滚动内容区。点击左侧
 *  导航项切换右侧页面。
 *
 *  本轮只做群主全功能视图、数据为本地假数据（GroupManageMockData），接入
 *  v2 群管理协议与管理员受限视图留待后续。调试期由 CMainDlg 的
 *  --groupmanage 命令行入口直接弹出。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "GroupManageModel.h"

#include <string>

class CFlamingoClient;

//v2 群组应答 / 推送的解析层结构体（定义在 net/GroupProtocolClient.h）
namespace net { struct GroupInfoResult; struct GroupMembersResult;
                struct GroupJoinRequestsResult; struct SensitiveWordsResult;
                struct GroupMemberChangedNotify; struct GroupInfoChangedNotify;
                struct GroupAnnouncementsResult; struct GroupAnnouncementNotify; }

namespace balloonwjui {
class DuiScrollView;
}

/**
 *  CBalloonGroupManageWindow
 *
 *  群管理窗口。继承 balloonwjui::DuiFrameWindow，由它提供无边框自绘窗体
 *  与标题栏（只保留关闭按钮）。窗口可缩放，设最小尺寸。
 */
class CBalloonGroupManageWindow : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象（消息中枢），本类只引用、不持有；
     *                本轮假数据阶段未使用，留作后续接入真实数据用。
     */
    explicit CBalloonGroupManageWindow(CFlamingoClient* pFMGClient);
    ~CBalloonGroupManageWindow();

    /**
     *  创建并显示群管理窗口（调试入口用，非模态、无父窗口）。
     *    hMainDlg：CMainDlg 的窗口句柄（隐藏的消息中枢）；关闭本窗口时
     *              据此通知 CMainDlg 收尾。
     */
    void CreateAndShow(HWND hMainDlg);

    /**
     *  绑定本对话框到某个真实群：写入群 id、真实群名与本人角色，供 Header 显示
     *  真实群名与"我是群主/管理员"。须在 CreateAndShow 之前调用。
     *  本轮各页内容仍为假数据，仅 Header 取真实值。
     *    groupId：群 id；
     *    name：   真实群名；
     *    myRole： 本人在群内角色（0成员 1管理员 2群主，见 group_member_role）。
     */
    void SetGroupContext(int64_t groupId, LPCTSTR name, int myRole);

    // ================================================================
    // 真实数据接入（应答与推送的落点）
    // ================================================================
    //
    // 窗口打开时会并发发出 1303 / 1304 / 1319 三条请求，三份应答各自到达、
    // 各填模型的一部分，谁都不清掉别人的结果，最后各自触发一次重建当前页。
    // 由 CBalloonMainPanel 在收到对应消息时调用；调用前会先比对 groupid，
    // 不是本窗口绑定的那个群就不会调过来。

    /**
     *  本窗口绑定的群ID。主面板据它判断某条应答 / 推送是不是给本窗口的。
     *  返回：群ID；0 表示未绑定（调试入口打开的假数据窗口）。
     */
    int64_t GetGroupId() const { return m_groupId; }

    /**
     *  向服务端并发发出 1303 / 1304 / 1319 三条请求，拉齐本群的全部管理数据。
     *  未绑定真实群（GetGroupId() 为 0）时什么都不做，界面停留在假数据上。
     *
     *  由创建本窗口的一方在 CreateAndShow 之后调用。
     */
    void RequestGroupData();

    /**
     *  记下宿主主面板的窗口句柄。
     *
     *  成员右键菜单里的"发消息""查看资料"要打开单聊窗 / 资料窗，而这两类窗口
     *  归主面板管（窗口集合在它手里），本窗口只能发一条 FMG_MSG_OPEN_CHAT /
     *  FMG_MSG_OPEN_CONTACT_PROFILE 请它代劳。m_hMainDlg 不能用 —— 那是
     *  CMainDlg 的句柄，且从主面板打开时传的是 NULL（表示关窗不退程序）。
     *    hPanel：主面板的 HWND；传 NULL 表示没有宿主，相关菜单项点了不生效。
     */
    void SetHostPanel(HWND hPanel) { m_hHostPanel = hPanel; }

    /**
     *  某张头像图片下载完成（由主面板转发，见 CBalloonMainPanel::OnAvatarDownloadResult）。
     *
     *  本窗口只关心本群那张：命中则重新解析出本地路径并重建界面，把先前的字形
     *  占位换成真图；不是本群的图直接返回，不做任何事。
     *
     *    fileId：下好的那张图的文件ID（UTF-8）。
     */
    void OnAvatarDownloaded(const std::string& fileId);

    /**
     *  群资料应答到达（1303）：填 Header 与基本信息 / 群权限页，并按本人角色
     *  重建左侧导航（角色是从这条应答里才知道的）。
     *    info：群资料应答解析结果。
     */
    void OnGroupInfoArrived(const net::GroupInfoResult& info);

    /**
     *  群成员应答到达（1304）：填成员管理页与禁言管理页。
     *    members：群成员应答解析结果。
     */
    void OnGroupMembersArrived(const net::GroupMembersResult& members);

    /**
     *  待审批加群申请应答到达（1319）：填加群审批页。
     *    requests：加群申请应答解析结果。
     */
    void OnJoinRequestsArrived(const net::GroupJoinRequestsResult& requests);

    /**
     *  群敏感词库应答到达（1321）：填群权限页的词库管理。
     *    words：敏感词库应答解析结果。
     */
    void OnSensitiveWordsArrived(const net::SensitiveWordsResult& words);

    /**
     *  按页号向服务端拉一页公告，并记下本次的分页参数供应答认领。
     *    page：页号（0 起）。
     */
    void RequestAnnouncementPage(int page);

    /**
     *  群公告列表应答到达（1323）：填「群公告」页的历史公告列表。
     *    result：公告列表应答解析结果。
     */
    void OnAnnouncementsArrived(const net::GroupAnnouncementsResult& result);

    /**
     *  群公告变更推送到达（1326）：重新拉一次公告列表并刷新当前页。
     *
     *  这里选择"重拉"而不是按推送里那条公告在本地增删：推送只带变动的这一条，
     *  而列表还要跟着变顺序（新公告排最前）、变分页、变计数徽章；本地维护这些
     *  增量的分支比重拉一次多得多，而公告是低频操作，不在热路径上。
     *    notify：推送解析结果。
     */
    void OnAnnouncementChanged(const net::GroupAnnouncementNotify& notify);

    /**
     *  群管理写操作的应答到达：成功时什么都不做（等 1317 / 1318 推送统一刷新），
     *  失败时弹提示并把界面回退到操作前状态。
     *    cmd：   对应的协议号，用于决定提示文案；
     *    code：  结果码，0 成功；
     *    msgUtf8：服务端给的诊断信息（UTF-8），仅在没有对应文案时兜底显示。
     */
    void OnGroupOpResult(int cmd, int code, const std::string& msgUtf8);

    /**
     *  群成员变动推送到达（1317）：重新拉一次成员列表并刷新相关页。
     *
     *  这里选择"重拉"而不是按 changetype 在本地增删改：推送只带了变动的 userid
     *  列表，不带这些人的昵称 / 群名片 / 角色，本地要么已有（那也未必是最新的）、
     *  要么根本没有（新加入的成员）。重拉一次的代价是一个来回，换来的是列表
     *  必定与服务端一致，比拼命维护本地增量靠谱。
     *    notify：推送解析结果。
     */
    void OnMemberChanged(const net::GroupMemberChangedNotify& notify);

    /**
     *  群资料变更推送到达（1318）：按推送里的完整新资料刷新各页。
     *  changetype 为"已解散"时提示并关窗。
     *    notify：推送解析结果。
     *  返回：群已解散、本窗口已自行关闭时返回 true，调用方不要再访问本对象。
     */
    bool OnInfoChanged(const net::GroupInfoChangedNotify& notify);

    /**
     *  有人申请加群推送到达（1307）：重新拉一次待审批列表，刷新审批页与其计数徽章。
     */
    void OnJoinRequestNotify();

    BEGIN_MSG_MAP(CBalloonGroupManageWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    // 创建窗口、配置外观、构建客户区并显示。成功返回 true。
    bool BuildWindow(HWND hWndParent);

    // 构建客户区控件树（Header + 分隔线 + 左导航 + 右滚动内容区），并把
    // 初始页设为「成员管理」。
    void InitUi();

    /**
     *  右侧内容区要给窗口缩放边框让出多少像素。
     *
     *  DuiFrameWindow 让客户区铺满整个窗口矩形，又把最外侧若干像素判为缩放边框；
     *  贴着右缘的滚动条会被那条带子盖住、抓不动（鼠标变双向箭头）。故把滚动视口
     *  往里缩这么多。
     *  返回：让路宽度（物理像素）；窗口不可缩放时为 0。
     */
    int ResizeBorderInsetPx() const;

    // 切换右侧内容到指定页（重建内容、就绪输入框、刷新导航选中态）。
    //   pageId：目标页 id，取值见 balloongroupmanage::GmPageId。
    void SwitchToPage(int pageId);

    // 为某页内容里的真 Win32 输入框补 EnsureCreated（builder 不建 EDIT）。
    //   pageId：当前页 id。
    void EnsureEditsForPage(int pageId);

    // 按当前页刷新左侧各导航项的选中态。
    //   pageId：当前页 id。
    void UpdateNavActive(int pageId);

    // 数据变化后整体重建客户区（Header + 左导航 + 当前页），并尽量保持停留在
    // 原来那一页。之所以整体重建而不是局部改：Header 的群名、导航各项的计数
    // 徽章、导航里哪些项可见（随角色变），全都要跟着数据走，一个个改的分支
    // 比重建一次多得多，而这件事只在数据到达 / 推送到达时发生，不在热路径上。
    void RefreshNavAndPage();

    // 按本人角色挑一个当前可见的页面。角色变化（如被撤销管理员）后，原先停留
    // 的页面可能已经不可见，此时要落回一个还看得见的页，而不是显示空白。
    //   preferredPageId：优先保留的页面 id。
    // 返回：该角色下可见的页面 id。
    int PickVisiblePage(int preferredPageId);

    // 当前登录者的 userid。成员右键菜单据它判断"这一行是不是我自己"。
    // 取不到（未登录）时返回 0。
    int64_t GetMyUserId() const;

    /**
     *  在某张成员卡上右键：弹出该成员的操作菜单并执行选中项。
     *  可用项按"登录者角色 × 目标成员角色 × 目标是否已禁言 × 是否是自己"计算
     *  （见 balloongroupmanage::BuildMemberMenuModel），落点经
     *  ClampPopupMenuToWorkArea 夹进显示器工作区。
     *    memberIndex：该成员在 m_model.m_members 里的下标（由卡片 ctrl id 减去
     *                 kIdMemberCardBase 得到）；越界时什么都不做。
     */
    void ShowMemberContextMenu(int memberIndex);

    /**
     *  弹出禁言时长的二级菜单并发出禁言请求。
     *  禁言是个高频轻操作，用二级菜单选时长比弹对话框轻。
     *    userId：目标成员userid；
     *    anchor：菜单锚点（屏幕坐标），取右键发生的位置。
     */
    void ShowMuteDurationMenu(int64_t userId, POINT anchor);

    /**
     *  处理页面上各控件的交互（按钮点击、开关拨动、下拉框选择）。
     *    ctrlId：控件的 ctrl id，取值见 GroupManageLayout.h 的 GmCtrlId；
     *    extra： 通知里的附加值。开关为 1(on)/0(off)，下拉框为选中项下标，
     *            按钮点击无意义（传 0 即可）。
     *  返回：已处理返回 true；不是本函数管的 id 返回 false。
     */
    bool HandlePageButton(UINT ctrlId, LPARAM extra);

    /**
     *  群权限页上某一项被改动：只把这一项发出去。
     *
     *  六个开关与两个下拉各自独立发送，而不是攒着等一个"保存"按钮 —— 这一页
     *  设计上就没有保存按钮，拨动即生效。只发改动的那一项也避免了多端同时
     *  编辑时把别人刚改的项按本端旧值覆盖回去。
     *    ctrlId：被改动的控件 id；
     *    value： 新取值（开关为 0/1，下拉为选中项下标）。
     *  返回：该 ctrlId 属于群权限页、已发出请求返回 true。
     */
    bool SendPermChange(UINT ctrlId, int value);

    /**
     *  弹一个"确定 / 取消"确认框。危险且不可撤销的操作（移出成员、解散群、
     *  转让群主）在发请求前都要过一道。
     *    title：  提示标题；
     *    message：提示正文。
     *  返回：用户点了确定返回 true。
     */
    bool ConfirmAction(LPCTSTR title, LPCTSTR message);

    /**
     *  收集基本信息页上被改动过的字段并发出修改请求。
     *  只把与模型当前值不同的项标成待更新 —— 没动过的不发，避免多端同时编辑
     *  时把别人刚改的项按本端的旧值覆盖回去。一项都没改时提示后返回。
     */
    void SaveBasicInfo();

    // 请主面板打开「修改群头像」对话框（PostMessage 一份堆载荷过去，见
    // GroupAvatarDlgRequest.h 里对"为什么不自己 new 一个"的说明）。
    // 无权限 / 没有宿主主面板时就地提示或记日志，不发消息。
    void RequestOpenAvatarDlg();

    // 按模型里的群头像 fileid 解析出真图的本地路径填进 m_groupAvatarImagePath：
    // 缓存命中就填路径，没命中则留空并发起下载（下好后经 OnAvatarDownloaded 回来）。
    // 每次群资料到达 / 变更后调用一次。
    void ResolveGroupAvatarImage();

    /**
     *  把群号复制到剪贴板（基本信息页的"复制"按钮）。
     */
    void CopyGroupNumberToClipboard();

    /**
     *  读管理词库页的输入框，把里面的词发出去（"添加"按钮）。
     *  词为空、或本地词库里已有同一个词时不发，直接给提示。
     */
    void AddSensitiveWordFromInput();

    /**
     *  读「群公告」页的表单，发布一条新公告（"发布公告"按钮）。
     *  标题去掉首尾空白后为空时不发，直接给提示 —— 服务端也会拒，但提前挡掉
     *  能立刻给反馈，而不是点了半天没动静。
     */
    void PublishAnnouncementFromInput();

    /**
     *  把「群公告」页的标题与正文输入框都倒空（"清空"按钮）。
     */
    void ClearAnnouncementDraft();

    /**
     *  处理「群公告」页上的按钮（优先级三选一、清空、发布、逐条重新发布 / 撤回）。
     *    ctrlId：控件的 ctrl id，取值见 GroupManageLayout.h 的 GmCtrlId。
     *  返回：已处理返回 true；不是本页的 id 返回 false。
     */
    bool HandleAnnouncementButton(UINT ctrlId);

    /**
     *  请主面板打开"选人"对话框（邀请成员 / 添加禁言成员 / 转让群主共用）。
     *  候选来源要用到主面板手里的通讯录缓存，故由它组装，本窗口只把"哪个群、
     *  干什么"传过去。
     *    purpose：用途，取值见 CCreateGroupDlg::Purpose。
     */
    void OpenMemberPicker(int purpose);

    // WM_CLOSE：调试入口下关闭即退出程序，转交 CMainDlg。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_DUI_NOTIFY：处理左侧导航项点击（切页）；其余通知交回基类链。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端对象（引用，不持有）
    HWND             m_hMainDlg;      // CMainDlg 句柄（关闭时通知其收尾）
    HWND             m_hHostPanel;    // 宿主主面板句柄；成员菜单借它打开单聊 / 资料窗。NULL=无宿主

    balloongroupmanage::GroupManageModel m_model;   // 群管理数据（本轮假数据）

    balloonwjui::DuiScrollView* m_pContentScroll;   // 右侧滚动视口（裸指针，所有权在控件树）
    balloonwjui::DuiControl*    m_pNav;             // 左侧导航容器（裸指针，所有权在控件树）
    int                         m_currentPage;      // 当前显示页 id

    // 本窗口最近一次拉公告时发出去的分页参数。1323 应答由主面板同时投给本窗口与
    // 群聊窗的公告边栏，而两边要的页大小不同（这里每页 8 条，边栏一次 1 条），
    // 靠它认领属于自己的那一份，否则后到的那份会把本页列表覆盖成对方要的条数。
    int                         m_announceReqOffset;
    int                         m_announceReqCount;
    int64_t                     m_groupId;          // 绑定的真实群 id；0=未绑定（调试入口走假数据）

    HBITMAP                     m_hTitleIcon;       // 标题栏左侧图标位图（程序图标合成到白底，本类持有，析构释放）
};
