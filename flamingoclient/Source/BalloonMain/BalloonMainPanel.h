/**
 *  BalloonMainPanel.h
 *
 *  登录成功后的主面板 —— 基于 balloonui 的窄高主窗口，对照
 *  design/pc-snapshots/05-main-contacts.png 与 06-main-groups.png。
 *
 *  与现有 CMainDlg 的关系（已与用户确认的集成方式）：CMainDlg 体量庞大、
 *  同时是 IM 消息中枢（收发消息 / 托盘 / 管理聊天窗），不便整体替换。
 *  故新主面板做成一个独立的 balloonui 窗口：登录成功后由 CMainDlg 创建
 *  并显示本面板，同时把 CMainDlg 自身窗口隐藏 —— CMainDlg 退居幕后只
 *  作消息中枢。真实好友 / 群 / 消息数据的接入留作后续阶段，本阶段先用
 *  mock 数据。
 *
 *  P0 范围：骨架 —— 一个能创建 / 显示 / 关闭的空 DuiFrameWindow；真正的
 *  Header / 搜索 / Tab / 列表由后续 P1 阶段的 BalloonMainLayout 构建。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方必须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include <vector>
#include <map>
#include <set>
#include <utility>
#include <stdint.h>

#include "../../balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../balloonui/balloonui/DuiNotify.h"
#include "../BalloonChatWnd/BalloonChatWindow.h"
#include "../BalloonChatWnd/BalloonGroupChatWindow.h"
#include "../BalloonGroupManage/BalloonGroupManageWindow.h"  // 群管理对话框（成员指针需完整类型）
#include "../BalloonGroupManage/BalloonAnnouncePopupDlg.h"   // 新公告提示窗（成员按值持有，需完整类型）
#include "../BalloonContact/BalloonContactWindow.h"  // 联系人资料窗
#include "../BalloonSearch/SearchDropdown.h"       // 搜索结果下拉（主面板下方顶层浮窗）
#include "FavoritesModel.h"                        // 「我的收藏」模型（成员按值持有，需完整类型）
#include "StarredModel.h"                          // 「特别关注」模型（成员按值持有，需完整类型）
#include "../net/FavoriteProtocolClient.h"          // net::SetFavoriteResult（应答回调形参）
#include "../net/StarredProtocolClient.h"           // net::SetStarredResult（应答回调形参）
#include "../net/FriendProtocolClient.h"            // net::SetFriendRemarkResult（设置备注名应答回调形参）
#include "ContactRemarkModel.h"                    // 显示名解析 / 备注提交判定（纯函数）
#include "UnknownSenderRefreshPolicy.h"            // 陌生发送方补拉通讯录的节流器（成员按值持有，需完整类型）
#include "OnlineCapability.h"                      // kConnUiOnline（备注提交前的连接态门禁）
#include "CreateGroupDlg.h"                        // 新建群聊对话框（成员需完整类型）
#include "BalloonChangePwdDlg.h"                   // 修改密码对话框（成员需完整类型）
#include "BalloonModifyAvatarDlg.h"                // 修改头像对话框（成员需完整类型）
#include "BalloonClosePromptDlg.h"                 // 关闭确认对话框（成员需完整类型）
#include "BalloonAboutDlg.h"                       // 关于对话框（成员需完整类型）
#include "../BalloonSettings/BalloonSystemSettingDlg.h" // 系统设置对话框（成员需完整类型）
#include "../net/ContactsProtocolClient.h"        // net::ContactItem（m_contacts 成员需完整类型）
#include "../net/ConversationsProtocolClient.h"   // net::ConversationItem（m_conversations 成员需完整类型）
#include "../net/GroupProtocolClient.h"            // net::UserGroupItem（m_myGroups 成员需完整类型）
#include "../UserSessionData.h"                    // FMG_MSG_OPEN_*（消息映射用）
#include "BalloonProfileDlg.h"   // 头像点击弹出的个人信息对话框
#include "BalloonGroupInfoDlg.h"   // 群会话右键"查看群资料"弹出的群资料对话框
#include "SignatureEditPopup.h"  // 个性签名内联编辑弹层（成员需完整类型）
#include "../BalloonHistory/V2MsgStore.h"  // v2 历史消息本地库（成员需完整类型）
#include "../BalloonHistory/FileLocalStore.h"  // 文件本地落点登记库（单例，传输收尾时登记）
#include "../BalloonHistory/AnnounceStore.h"   // 平台公告本地库（单例，消息盒子的数据落点）
#include "../BalloonAnnounce/BalloonAnnounceBoxWindow.h"  // 消息盒子窗口（成员按值持有，需完整类型）
#include "../BalloonAnnounce/AnnouncePopupWindow.h"       // 新公告桌面提示窗（成员按值持有，需完整类型）
#include "StarredPopupWindow.h"                          // 特别关注上下线提示窗（成员按值持有，需完整类型）
#include "../BalloonHistory/BalloonHistoryWindow.h"  // 历史消息记录窗口（成员需完整类型）

class CFlamingoClient;

namespace balloonmain { class MainPanelSectionHeader; }
namespace balloonmain { class MainPanelTreeView; }   // 会话树（支持未读数字红点角标）
namespace balloonmain { class MainPanelScrollView; } // 列表面板滚动视口（悬浮滚动条）

//v2 通讯录 / 单聊消息投递载体（解析层结构体，定义在 net/*.h）。
namespace net { struct ContactsResult; struct MessageNotify; struct SendMessageResponse;
                struct DepartmentItem; struct ConversationsResult; struct CreateGroupResponse;
                struct GroupInfoResult; struct GroupMembersResult; struct UserGroupsResult;
                struct SyncMessagesResult; struct ChangePasswordResponse;
                struct UpdateProfileResponse; struct ChangePresenceResponse;
                struct ImageSendProgress;
                struct ImageSendResult; struct ImageDownloadResult;
                struct FileSendProgress; struct FileSendResult;
                struct FileDownloadProgress; struct FileDownloadResult;
                struct AvatarUploadResult; struct AvatarDownloadResult;
                struct UserInfoChangedNotify;
                struct GroupOpResult; struct GroupJoinRequestsResult; struct GroupJoinRequestItem;
                struct SensitiveWordsResult; struct GroupMemberChangedNotify;
                struct GroupInfoChangedNotify;
                struct GetGroupFilesResult; struct GroupFilesChangedNotify;
                struct GroupFileUploadResult; struct GroupFileDownloadResult;
                struct GroupFileUploadProgress; struct GroupFileDownloadProgress;
                struct GroupAnnouncementsResult; struct GroupAnnouncementNotify;
                //平台公告（消息盒子）；注意与上一行的<u>群</u>公告不是一回事
                struct AnnouncementsResult; struct AnnouncementNotify; struct AnnounceMarkReadResult;
                struct AnnouncementItem;
                struct TypingNotify; }
namespace balloonwjui { class DuiTreeView; }

/**
 *  CBalloonMainPanel
 *
 *  登录后主面板窗口。继承 balloonwjui::DuiFrameWindow —— 由它提供无边框
 *  自绘窗体、标题栏与 min/max/close 按钮。模式为<u>非模态</u>（主窗口），
 *  消息由进程主消息循环统一泵送。
 */
class CBalloonMainPanel : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象（消息中枢），本类只引用、不持有；
     *                本阶段为 mock 数据，暂不使用，留作后续真实数据接入。
     */
    explicit CBalloonMainPanel(CFlamingoClient* pFMGClient);
    ~CBalloonMainPanel();

    /**
     *  创建并显示主面板窗口。登录成功后由 CMainDlg 调用。
     *    hMainDlg：CMainDlg 的窗口句柄（隐藏的消息中枢）；关闭本面板时
     *              据此通知 CMainDlg 退出程序。
     */
    void CreateAndShow(HWND hMainDlg);

    // 下面三个 RefreshXxx 是给 CMainDlg 用的对外刷新入口。它们原先各自走 v1 的
    // CUserMgr 适配器（好友列表 / 群列表 / 最近会话），企业版删除好友子系统后
    // 数据源已全部换成 v2：通讯录来自 cmd_get_contacts 应答、群与会话来自会话
    // 同步。三个方法因此都只是转调真正的重建函数，保留是为了不改动调用方。

    // 重建「联系人」页的组织架构树（转调 RebuildContactsTree）。
    void RefreshContacts();

    // 重建「群」页的群列表（转调 FillGroupsTree，数据取自会话缓存与群资料缓存）。
    void RefreshGroups();

    // 重建「消息」页的会话列表（转调 FillConversationsTree，数据取自 m_conversations）。
    void RefreshMessages();

    /**
     *  一次性刷新三个 Tab。CMainDlg 登录成功 + 数据全部加载完毕时调用。
     */
    void RefreshAll();

    /**
     *  收到 v2 通讯录应答后，用真实联系人资料池填充 Contacts 页的
     *  DuiTreeView，并建立"树节点 → userid"映射（供点击开聊）。由 CMainDlg
     *  的 OnV2Contacts 调用。
     *    result：通讯录解析结果（仅 code==0 时调用）。
     */
    void OnContactsArrived(const net::ContactsResult& result);

    /**
     *  收到 v2 单聊新消息推送：按发送方 userid 找到（必要时创建）对应单聊
     *  窗口并渲染。由 CMainDlg 的 OnV2RecvChatMsg 调用。
     *    notify：消息推送解析结果。
     */
    void OnRecvChatMessage(const net::MessageNotify& notify);

    /**
     *  收到 v2 单聊发送回执：成功仅记日志；失败弹框提示。由 CMainDlg 的
     *  OnV2SendChatAck 调用。
     *    resp：发送回执解析结果。
     */
    void OnSendChatAck(const net::SendMessageResponse& resp);

    /**
     *  收到 v2 发图上传进度：按 (ctype,cid) 找到对应聊天窗，更新其"发送中"
     *  图片气泡的进度。由 CMainDlg 的 OnV2ImageSendProgress 调用。
     *    prog：发图进度（含 clientmsgid / ctype / cid / 已传 / 总字节）。
     */
    void OnImageSendProgress(const net::ImageSendProgress& prog);

    /**
     *  收到 v2 发图最终结果：按 (ctype,cid) 找到对应聊天窗，成功把气泡置正常态、
     *  失败标"发送失败"。由 CMainDlg 的 OnV2ImageSendResult 调用。
     *    result：发图结果（含 clientmsgid / ctype / cid / ok / 错误码）。
     */
    void OnImageSendResult(const net::ImageSendResult& result);

    /**
     *  收到 v2 收图下载结果：下载结果只带 msgid（无会话标识），故向所有已开的
     *  聊天窗按 msgid 广播——命中那条"下载中"气泡的窗口回填渲染 / 标失败。由
     *  CMainDlg 的 OnV2ImageDownloadResult 调用。
     *    result：下载结果（含 msgid / ok / 本地缓存路径）。
     */
    void OnImageDownloadResult(const net::ImageDownloadResult& result);

    // ---- v2 文件收发：路由到对应聊天窗（与图片镜像）----

    //发文件上传进度：clientmsgid 全局唯一，向所有已开聊天窗广播，命中者更新进度。
    void OnFileSendProgress(const net::FileSendProgress& prog);
    //发文件最终结果：按 (ctype,cid) 精确路由到对应聊天窗，置完成 / 失败态。
    void OnFileSendResult(const net::FileSendResult& result);
    //收文件下载进度：按 msgid 向所有已开聊天窗广播，命中那条气泡更新进度。
    void OnFileDownloadProgress(const net::FileDownloadProgress& prog);
    //收文件下载结果：按 msgid 向所有已开聊天窗广播，命中者回填渲染 / 标失败。
    void OnFileDownloadResult(const net::FileDownloadResult& result);
    //对方收取本端所发文件的状态（cmd_file_transfer_ack 推送）：按 msgid 向所有已开
    //聊天窗广播，命中那条本端发出的文件气泡、更新其状态行。
    void OnFileTransferAck(const net::FileTransferAckNotify& notify);

    /**
     *  对端「正在输入」状态变化（cmd_typing_notify 推送）：按 notify.m_cid（即正在
     *  输入那一方的 userid）精确找到对应单聊窗，刷新其头部提示。
     *
     *  该会话的窗口没打开时什么都不做 —— 这条提示只在聊天窗里显示，没开窗就无处可显，
     *  也不必为它记住状态（等窗口打开时对方多半已经把消息发出来了）。
     *    notify：推送解析结果。
     */
    void OnTypingNotify(const net::TypingNotify& notify);

    /**
     *  收到 v2 最近会话列表应答后，用真实会话填充"消息"页的 DuiTreeView。
     *  本轮只展示单聊会话（ctype==1，群聊会话延后）。由 CMainDlg 的
     *  OnV2Conversations 调用。
     *    result：会话列表解析结果（仅 code==0 时调用）。
     */
    void OnConversationsArrived(const net::ConversationsResult& result);

    /**
     *  收到 v2 群资料应答：写群信息缓存（群名 / 成员数），刷新"群"Tab 与"消息"Tab
     *  里该群的显示名。由 CMainDlg 的 OnV2GroupInfo 调用。
     *    info：群资料解析结果（仅 code==0 时调用）。
     */
    void OnGroupInfoArrived(const net::GroupInfoResult& info);

    /**
     *  收到 v2 群成员应答：路由到对应群聊窗，填成员侧栏并建"发送人映射"。
     *  由 CMainDlg 的 OnV2GroupMembers 调用。
     *    members：群成员解析结果（仅 code==0 时调用）。
     */
    void OnGroupMembersArrived(const net::GroupMembersResult& members);

    /**
     *  打开「群资料」对话框（群会话右键菜单"查看群资料"调用，全员可用，区别于群管理）。
     *  先用本地缓存（群名 / 成员数 / 免打扰 / 置顶）组 seed 打开，再补拉 cmd_get_group_info
     *  (1303) + cmd_get_group_members(1304)，应答到达后把公告 / 群主显示名 / 我的群名片
     *  刷进对话框。
     *    groupId：目标群 id（>0）。
     */
    void OpenGroupInfo(int64_t groupId);

    /**
     *  设置某群会话的置顶 / 免打扰（群资料对话框开关回投）。乐观更新本地会话缓存
     *  m_conversations（让"消息"Tab 即时反映置顶 / 免打扰），再发 cmd_set_conversation(1513)。
     *    groupId：群 id；
     *    hasMute：true=本次要改免打扰，按 mute 写入；
     *    mute：   免打扰目标值（hasMute 为 true 时有效）；
     *    hasPinned：true=本次要改置顶，按 pinned 写入；
     *    pinned： 置顶目标值（hasPinned 为 true 时有效）。
     */
    void SetGroupConversation(int64_t groupId, bool hasMute, bool mute,
                              bool hasPinned, bool pinned);

    /**
     *  退出某群（群资料对话框"退出群聊"回投）。发 cmd_leave_group(1309)，并乐观清理：
     *  从群列表 / 群信息缓存 / 会话缓存移除该群、刷新"群"与"消息"两 Tab、关闭该群已打开的
     *  群聊窗。若服务端最终拒绝（如群主须先转让），下一次同步会纠正。
     *    groupId：群 id。
     */
    void LeaveGroup(int64_t groupId);

    /**
     *  收到 cmd_set_member_card(1315) 应答：转交给群资料对话框处理(成功无动作;失败从
     *  快照回退我的群名片 + 弹提示)。由 CMainDlg 的 OnV2GroupWriteResult 在该应答到达时
     *  调用。
     *    code  结果码,0 成功;
     *    msg   结果描述(UTF-16)。
     */
    void OnGroupCardSaveResult(int code, const CString& msg);

    /**
     *  收到 v2 群文件列表应答：按 groupid 路由到对应群聊窗，由其累积分页 / 拉全后渲染。
     *  由 CMainDlg 的 OnV2GroupFiles 调用（成功 / 失败都转，群聊窗据 code 收尾）。
     *    result：群文件列表解析结果（含 groupid / 本页条目 / hasmore）。
     */
    void OnGroupFilesArrived(const net::GetGroupFilesResult& result);

    /**
     *  收到 v2 群文件变更推送：按 groupid 路由到对应群聊窗，由其增量增 / 删并重渲染。
     *  由 CMainDlg 的 OnV2GroupFilesChanged 调用。
     *    notify：群文件变更解析结果（changetype=新增带 file / 删除带 entryid）。
     */
    void OnGroupFilesChanged(const net::GroupFilesChangedNotify& notify);

    /**
     *  收到群文件上传结果：按 groupid 路由到对应群聊窗。由 CMainDlg 的
     *  OnV2GroupFileUploadResult 调用。
     *    result：群文件上传结果（成功含 fileid）。
     */
    void OnGroupFileUploadResult(const net::GroupFileUploadResult& result);

    /**
     *  收到群文件下载结果：按 groupid 路由到对应群聊窗。由 CMainDlg 的
     *  OnV2GroupFileDownloadResult 调用。
     *    result：群文件下载结果（成功含落地路径）。
     */
    void OnGroupFileDownloadResult(const net::GroupFileDownloadResult& result);

    /**
     *  收到群文件上传进度：按 groupid 路由到对应群聊窗，更新面板级上传进度条。
     *    prog：群文件上传进度（已传 / 总字节）。
     */
    void OnGroupFileUploadProgress(const net::GroupFileUploadProgress& prog);

    /**
     *  收到群文件下载进度：按 groupid 路由到对应群聊窗，按 entryid 更新那一行的进度。
     *    prog：群文件下载进度（含 entryid / 已收 / 总字节）。
     */
    void OnGroupFileDownloadProgress(const net::GroupFileDownloadProgress& prog);

    /**
     *  收到 v2"我的群列表"应答：缓存本人加入的全部群（含无消息空群）作为"群"Tab
     *  的群源，并把群名/成员数写进群信息缓存、刷新"群"Tab 与"消息"Tab。由 CMainDlg
     *  的 OnV2UserGroups 调用。
     *    result：群列表解析结果（仅 code==0 时调用）。
     */
    void OnUserGroupsArrived(const net::UserGroupsResult& result);

    /**
     *  收到 v2 历史消息应答：按 (ctype,cid) 路由到对应聊天窗渲染历史。由 CMainDlg
     *  的 OnV2SyncMessages 调用。
     *    result：历史消息解析结果（仅 code==0 时调用）。
     */
    void OnSyncMessagesArrived(const net::SyncMessagesResult& result);

    /**
     *  收到 v2 建群应答：转交新建群对话框提示结果（成功关闭并提示，失败提示
     *  错误）。由 CMainDlg 的 OnV2CreateGroupResult 调用。
     *    resp：建群应答解析结果。
     */
    void OnCreateGroupResult(const net::CreateGroupResponse& resp);

    /**
     *  收到 v2 修改密码应答：转交改密对话框提示结果（成功关闭、旧密码错 / 失败提示）。
     *  由 CMainDlg 的 OnV2ChangePwdResult 调用。
     *    resp：改密应答解析结果。
     */
    void OnChangePwdResult(const net::ChangePasswordResponse& resp);

    /**
     *  收到 v2 修改头像应答：转交修改头像对话框提示 / 关闭；成功则把新头像写回本端
     *  缓存并刷新 Banner 头像。由 CMainDlg 的 OnV2ChangeAvatarResult 调用。
     *    resp：改像应答解析结果（通用 {code,msg}）。
     */
    void OnChangeAvatarResult(const net::UpdateProfileResponse& resp);

    /**
     *  收到 v2 头像图片上传结果：转交修改头像对话框（成功则它接着发 cmd_change_avatar；
     *  失败就地提示）。由 CMainDlg 的 OnV2AvatarUploadResult 调用。
     *    result：头像上传结果（含 fileid / 本地缓存路径或失败码）。
     */
    void OnAvatarUploadResult(const net::AvatarUploadResult& result);

    /**
     *  收到 v2 头像图片下载结果：按 fileid 找到所有引用该头像的联系人 / 单聊会话节点，
     *  从缓存重生成真图并 SetItemIcon 刷新。由 CMainDlg 的 OnV2AvatarDownloadResult 调用。
     *    result：下载结果（含 fileid / 成功标志）。
     */
    void OnAvatarDownloadResult(const net::AvatarDownloadResult& result);

    /**
     *  收到 v2 更新资料（个性签名）应答：失败则回退乐观更新的签名并提示；成功
     *  无需额外动作（乐观更新已在提交时把新签名显示出来）。由 CMainDlg 的
     *  OnV2UpdateProfileResult 调用。
     *    resp：更新资料应答解析结果。
     */
    void OnUpdateProfileResult(const net::UpdateProfileResponse& resp);

    /**
     *  收到 v2 修改在线状态应答（cmd_change_presence）：失败则把状态胶囊回退到改之前
     *  的状态并提示；成功无需额外动作（乐观更新已在选中时把胶囊改成目标状态）。由
     *  CMainDlg 的 OnV2ChangePresenceResult 调用。仅"已连接时直接改状态"这条路径会
     *  收到本应答；"离线 / 重新上线"走 GoOffline / GoOnline，不产生本应答。
     *    resp：修改在线状态应答解析结果。
     */
    void OnChangePresenceResult(const net::ChangePresenceResponse& resp);

    /**
     *  取状态胶囊上当前显示的在线状态（v2 presence_type）。胶囊是"我当前的在线状态"
     *  在界面上的唯一呈现，托盘右键菜单也从这里取当前状态来标记菜单项，两处便不会
     *  出现一个说在线、另一个说离线。
     *
     *  返回：胶囊上的 presence；Header 控件尚未创建（理论上不会发生）时退回
     *        CIUSocket::GetRecordOnlineStatus() 的期望在线状态作兜底。
     */
    int  GetPresence();

    /**
     *  乐观更新状态胶囊 —— 先把改之前的状态存进 m_presenceBeforeChange 供应答失败时
     *  回退，再把胶囊改成目标状态并重绘 Header。
     *
     *  由 CMainDlg::OnChangeMyPresence <u>统一</u>调用，<u>不要</u>在各个发起点各调一次：
     *  切换在线状态有两个入口（主面板状态菜单、系统托盘右键菜单），它们都汇到
     *  OnChangeMyPresence，把胶囊更新放在那里两个入口的表现才一致 —— 原先这步写在
     *  主面板自己的 ShowStatusMenu 里，导致从托盘切状态时胶囊不跟着变。
     *
     *    targetPresence：目标在线状态（v2 presence_type）。
     *
     *  副作用：改写 m_presenceBeforeChange，并重绘 Header 控件。Header 尚未创建时静默返回。
     */
    void ApplyPresenceOptimistic(int targetPresence);

    /**
     *  应用当前连接态并<u>逐窗转发</u>给本面板持有的所有单聊 / 群聊窗口，由它们决定
     *  顶部提示条的显隐、输入框是否只读、发送按钮是否可用。
     *
     *  由 CMainDlg::BroadcastConnectionState 调用 —— CMainDlg 不直接持有聊天窗，
     *  聊天窗的所有权在本面板（m_chatWindows / m_groupChatWindows），故由本面板转发。
     *
     *    state：界面连接态，取值见 OnlineCapability.h 的 ConnectionUiState。
     *
     *  副作用：记住 state 供本面板自身的联网入口（加好友 / 建群 / 改资料等）做门禁，
     *          并调用每个已打开聊天窗的 ApplyConnectionState。
     */
    void ApplyConnectionState(int state);

    /**
     *  收到 cmd_set_favorite 应答后的处理（由 CMainDlg 转交）。
     *    result： 应答解析结果，含服务端原样回带的 ctype / cid / favorite。
     *  成功时只记日志 —— 乐观更新在发起时已经生效。失败时把那一条回滚回去并
     *  提示用户，随后重填收藏分区。
     */
    void OnSetFavoriteResult(const net::SetFavoriteResult& result);

    /**
     *  收到 cmd_set_starred 应答后的处理（由 CMainDlg 转交）。
     *    result： 应答解析结果，含服务端原样回带的 target / starred。
     *  成功时只记日志 —— 乐观更新在发起时已经生效。失败时把那一个人的关注状态
     *  回滚回去并提示用户（关注人数超限要给一句明确的话，否则用户只会看到
     *  "点了没反应"）。
     */
    void OnSetStarredResult(const net::SetStarredResult& result);

    /**
     *  查询某个联系人当前是否被特别关注。供群聊窗那两处右键菜单（成员列表、气泡里的
     *  @片段与发送者头像）在弹菜单前取文案用 —— 关注名单只有主面板这一份。
     *    userid： 联系人 userid。
     *  @return 已关注返回 true。
     */
    bool IsContactStarred(int64_t userid) const;

private:
    /**
     *  给刚建好的树节点打上「特别关注」星标（按名单现状）。填联系人行 / 会话行时调用。
     *    tv：     目标树；实际必须是 MainPanelTreeView，不是则安静跳过。
     *    node：   节点 id。
     *    userid： 该行对应的联系人 userid。
     */
    void ApplyStarredMark(balloonwjui::DuiTreeView* tv, int node, int64_t userid);

    /**
     *  某个人的关注状态变了之后，就地刷新树上所有对应行的星标。
     *
     *  不整树重建：星标不改变树的结构，只改一个标记，重建会白丢展开状态与滚动位置。
     *  做法与收到上下线推送时重着状态点一致（见 OnPresenceChanged）。
     *
     *  三处调用：ToggleStarred 的乐观更新、OnSetStarredResult 的失败回滚、以及
     *  群聊窗那三处菜单转过来的切换（它们最终也走 ToggleStarred）。
     *    userid： 关注状态发生变化的联系人；<= 0 时什么都不做。
     */
    void RefreshStarredMarks(int64_t userid);

    /**
     *  收到一条 presence 推送后，判断要不要为它弹「特别关注」的上下线提示，该弹就弹。
     *  由 OnPresenceChanged 调用；五条抑制条件（不在名单、是自己、不是上下线跳变、
     *  登录静默期内、勿扰）任意一条成立都不弹，理由逐条写在实现里。
     *    userid：     发生变化的联系人 userid。
     *    oldPresence：变化前的 presence（通讯录缓存里的旧值；查不到时调用方传 0）。
     *    newPresence：变化后的 presence（服务端推送的新值）。
     *    selfId：     当前登录用户的 userid，用来排除多设备回声。
     */
    void MaybeShowStarredPresencePopup(int64_t userid, int oldPresence,
                                       int newPresence, UINT selfId);

public:

    /**
     *  收到 cmd_set_friend_remark 应答后的处理（由 CMainDlg 转交）。
     *    result： 应答解析结果，含服务端原样回带的 userid。
     *  成功时丢掉回滚快照即可 —— 乐观更新在发起时已经生效。失败时按回带的 userid
     *  把<u>那一个人</u>的备注退回改动前的值并提示用户；老服务端不回带 userid
     *  （m_userid 为 0）时宁可不回滚，也好过把别人的备注退错。
     */
    void OnSetFriendRemarkResult(const net::SetFriendRemarkResult& result);

    /**
     *  提交某联系人的备注名 —— 备注功能的<u>唯一</u>执行入口，资料窗的行内编辑与
     *  右键菜单「修改备注」最终都汇到这里。
     *
     *  判定与执行都收在本方法：通讯录缓存（原值）、连接态与 socket 全在主面板手上，
     *  若让资料窗也判一份，两处规则日后必然走岔。判定本身由纯函数
     *  balloonmain::DecideRemarkCommit 给出，本方法只按结果分四条路走：
     *    · 无变化   —— 不发包，把资料窗刷回缓存值（顺带抹平首尾空白）；
     *    · 离线     —— 提示并还原，不能让界面停在"改好了"的样子；
     *    · 超长     —— 提示并还原（库里是 VARCHAR(32)，放过去会被悄悄截断）；
     *    · 该发     —— 乐观更新缓存 + 刷新三处显示名 + 发 cmd_set_friend_remark。
     *
     *    peerUserId： 目标联系人 userid；为 0 时本方法什么都不做。
     *    remark：     用户敲进去的原始文本（未 trim）；trim 后为空串表示清除备注，
     *                 是合法提交，不是"没改动"。
     *  副作用：真发包时把改动前的备注记进 m_pendingRemarkRollback 供应答失败时回滚。
     */
    void SetContactRemark(UINT peerUserId, const CString& remark);

    /**
     *  解析某联系人在界面上显示的名字（备注优先，其次昵称、账号，最后"用户<id>"）。
     *  通讯录树、会话列表、单聊窗标题三处都该走它，免得同一个人在三处叫三个名字。
     *    peerUserId： 联系人 userid。
     *  返回：显示名，保证非空；不在通讯录缓存里时返回"用户<id>"。
     */
    CString ContactDisplayName(UINT peerUserId) const;

    /**
     *  取本面板记住的当前连接态（ConnectionUiState）。供面板内各联网入口做门禁判断。
     */
    int  GetConnectionUiState() const { return m_connUiState; }

    /**
     *  本面板各联网入口的门禁：不可用时弹出提示并返回 false。
     *    action：操作类别，取值见 OnlineCapability.h 的 OnlineActionKind。
     *  返回：true 表示可以继续该操作。
     */
    bool CheckOnlineFor(int action);

    /**
     *  收到 v2 好友在线状态变更推送（cmd_presence_changed_notify）：更新通讯录缓存
     *  m_contacts 里该 userid 的 presence，并把联系人树上对应节点的状态点重着色 + 刷新，
     *  实现好友改状态后本端实时变色。由 CMainDlg 的 OnV2PresenceChanged 调用。
     *    userid    状态发生变更的好友用户ID。
     *    presence  新的在线状态（v2 presence_type）。
     */
    void OnPresenceChanged(int64_t userid, int presence);

    /**
     *  收到 v2 会话属性变更推送（cmd_conversation_changed_notify 1514）：本账号在另一端
     *  改了某会话的置顶 / 免打扰 / 删除，按推送里的<u>完整新状态</u>覆盖本地会话缓存并刷新
     *  "消息"Tab。由 CMainDlg 的 OnV2ConversationChanged 调用。
     *
     *  发起改动的那一端自己也会收到这条推送（服务端推给该用户的每个在线端）。那一端在
     *  发请求时已经乐观更新过，此处再覆盖一遍结果相同，是安全的重复操作。
     *
     *  本地没有这条会话时不会凭空造一条：会话行的内容（预览 / 时间 / 未读）推送里没有，
     *  造出来也是空壳；等下次会话同步或新消息到达时自然会出现。
     *    notify：推送解析结果。
     */
    void OnConversationChanged(const net::ConversationChangedNotify& notify);

    // ================================================================
    // 群管理（应答与推送的落点）
    // ================================================================
    //
    // 这一组方法由 CMainDlg 的同名消息处理函数调用。主面板本身不显示群管理
    // 内容，它的职责是<u>转交</u>：群管理窗口是它创建并持有的（m_pGroupManageWindow），
    // 而 socket 层的消息只投给 CMainDlg，所以必须经主面板这一跳。窗口没开着时
    // 直接丢弃即可 —— 下次打开会重新拉一遍数据。

    /**
     *  群管理写操作的应答（十来个操作共用）。成功时<u>不动界面</u> —— 服务端会推
     *  1317 / 1318，等推送统一刷新；只有失败才提示，因为失败没有推送。
     *    cmd：   对应的协议号（cmd_kick_group_member 等），用于决定提示文案；
     *    result：应答解析结果。
     */
    void OnGroupOpResult(int cmd, const net::GroupOpResult& result);

    /**
     *  待审批加群申请列表应答：填进群管理窗口的"加群审批"页。
     *    result：应答解析结果，含群ID与申请列表。
     */
    void OnGroupJoinRequests(const net::GroupJoinRequestsResult& result);

    /**
     *  群敏感词库应答：填进群权限页的词库管理。
     *    result：应答解析结果，含群ID与词条列表。
     */
    void OnSensitiveWords(const net::SensitiveWordsResult& result);

    /**
     *  群公告列表应答（1323）：填进群管理窗的「群公告」页与群聊窗的公告边栏。
     *  两个窗口可能同时开着且看的是同一个群，故两处都要转交。
     *    result：应答解析结果，含群ID、本页公告与总条数。
     */
    void OnGroupAnnouncements(const net::GroupAnnouncementsResult& result);

    /**
     *  群公告变更推送（1326）：刷新群管理窗与群聊窗的公告界面。
     *  changetype 为"新发布"且不是自己发的时，还要弹一次新公告提示。
     *    notify：推送解析结果，带完整的那条公告。
     */
    void OnAnnouncementChanged(const net::GroupAnnouncementNotify& notify);

    /**
     *  群成员变动推送（1317）：刷新群管理窗口的成员 / 禁言页、群聊窗的成员边栏。
     *  本人被移出、或本人角色变化时还要调整界面权限。
     *    notify：推送解析结果。
     */
    void OnGroupMemberChanged(const net::GroupMemberChangedNotify& notify);

    /**
     *  群资料变更推送（1318）：刷新群名（会话列表 / 群聊窗标题 / 群管理窗 Header）
     *  与群管理各页。changetype 为"已解散"时关掉该群的相关窗口并提示。
     *    notify：推送解析结果。
     */
    void OnGroupInfoChanged(const net::GroupInfoChangedNotify& notify);

    /**
     *  有人申请加群推送（1307）：仅群主与管理员会收到。群管理窗口开着时刷新审批页，
     *  否则只记日志 —— 待处理数量下次打开窗口时会重新拉到。
     *    item：推送解析结果（一条申请）。
     */
    void OnJoinGroupRequestNotify(const net::GroupJoinRequestItem& item);

    /**
     *  收到 v2 联系人资料变更推送（1108）：更新通讯录缓存里该 userid 的头像字段，并实时
     *  重画联系人树 / 单聊会话里该联系人的头像（字形即时生效；真图未缓存则触发下载）。
     *  由 CMainDlg 的 OnV2UserInfoChanged 调用。
     *    notify：资料变更解析结果（userid + 新 facetype/customface/nickname）。
     */
    void OnUserInfoChanged(const net::UserInfoChangedNotify& notify);

    /**
     *  收到 v2 组织架构变更推送（1404）：重新拉一次通讯录（cmd_get_contacts），应答
     *  照常经 OnContactsArrived 回来重建联系人树与缓存。由 CMainDlg 的
     *  OnV2OrgStructureChanged 调用。
     *
     *  之所以整份重拉而不做增量：推送体里只有"变更类型 + 部门ID"，没带变了什么，
     *  服务端也没有增量接口。组织架构的变更频率很低（有人入职 / 调岗 / 部门调整才发
     *  一次），整份重拉的代价可以接受。
     *    notify：变更解析结果，当前只用于日志，不参与刷新判断。
     */
    void OnOrgStructureChanged(const net::OrgStructureChangedNotify& notify);

    /**
     *  重置"陌生发送方补拉通讯录"的节流记账。由 CMainDlg 在每次登录成功后调用。
     *  掉线重连期间组织架构可能变过、推送也可能错过，把上一轮"这些人已经问过了"
     *  的记录清掉，本轮才能对同一个人再补拉一次。
     */
    void ResetUnknownSenderPolicy();

    /**
     *  打开（或前置）指定会话并把其未读本地清零 —— 供 CMainDlg 在用户单击闪烁的托盘
     *  图标时调用。单聊走 OpenSingleChat、群聊走 OpenGroupChat，随后 MarkConversationRead
     *  清未读角标（其内部会回投 FMG_MSG_TRAY_CLEAR 给 CMainDlg，使托盘停闪 / 切目标）。
     *    ctype：会话类型（1 单聊 cid=对端userid；2 群聊 cid=群id）。
     *    cid：  会话 ID。
     */
    void OpenConversation(int ctype, int64_t cid);

    /**
     *  打开某会话的历史消息记录窗（右键菜单"查看历史消息" / 聊天窗标题栏"消息记录"
     *  按钮转发到此）。解析对话名（群名 / 对端昵称）、确保本地库就绪，把窗口指向该
     *  会话并刷新置顶；已开则重定向到新会话。
     *    ctype：会话类型（1 单聊 cid=对端userid；2 群聊 cid=群id）。
     *    cid：  会话ID（我方视角）。
     *    initialTypeFilter：打开后预选哪个类型胶囊，取 balloonhistory::HistoryTypeFilter
     *                       的值。默认 0（kHistoryTypeAll）即"全部"，与老行为一致；
     *                       聊天窗的「已 Pin N 条」提示带传 6（kHistoryTypePinned），
     *                       让窗口直接开在 Pin 分类上。取值非法时按"全部"处理。
     */
    void OpenHistory(int ctype, int64_t cid, int initialTypeFilter = 0);

    /**
     *  按 userid 解析显示昵称（本人→本人昵称；联系人→缓存昵称 / 账号；都查不到→
     *  "用户<id>"）。供历史消息记录窗口给每行解析发送者昵称（public：被 CBalloonHistoryWindow
     *  跨类调用）。
     *    userid：用户 id。
     */
    CString ResolveSenderName(int64_t userid) const;

    /**
     *  定位到某会话里的某条消息（历史消息记录窗"定位"按钮调用）：打开 / 前置对应聊天窗，
     *  再让该窗滚动定位到 msgid（若历史在途则窗口侧记为待定，渲染完再补定位）。
     *    ctype：会话类型（1 单聊 cid=对端userid；2 群聊 cid=群id）。
     *    cid：  会话ID（我方视角）。
     *    msgid：目标消息的服务器 msgid。
     */
    void LocateMessageInChat(int ctype, int64_t cid, long long msgid);

    // WM_CLOSE 自行处理（关闭主面板 = 退出程序，转交 CMainDlg）；其余
    // 消息经 CHAIN 交回 DuiFrameWindow。
    BEGIN_MSG_MAP(CBalloonMainPanel)
        // 主面板是普通顶层窗口，每次显示都会被分到一个任务栏按钮，须逐次摘除
        // （不用 WS_EX_TOOLWINDOW 的缘由见 CreateAndShow 的注释）。
        MESSAGE_HANDLER(WM_SHOWWINDOW, OnShowWindowForTaskbar)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_SYSCOMMAND, OnSysCommand)
        // WM_NCHITTEST / WM_NCLBUTTONDBLCLK：让顶部 Banner 与底部栏的空白处也能
        // 按住拖动窗口。两个 handler 都排在 CHAIN_MSG_MAP 之前，且都会在"落点不属于
        // 可拖区"时把消息原样交回基类，不影响标题栏与缩放边框的既有行为。
        MESSAGE_HANDLER(WM_NCHITTEST, OnPanelNcHitTestMsg)
        MESSAGE_HANDLER(WM_NCLBUTTONDBLCLK, OnPanelNcLButtonDblClkMsg)
        // WM_MOVE / WM_SIZE：把独立顶层浮窗的搜索结果下拉拉回搜索框下方（两个
        // handler 都置 bHandled=FALSE，消息继续交给 DuiFrameWindow / DuiHost 走
        // 原有的最大化态维护与客户区重排）。
        MESSAGE_HANDLER(WM_MOVE, OnPanelMoveMsg)
        MESSAGE_HANDLER(WM_SIZE, OnPanelSizeMsg)
        // 悬浮滚动条的淡入 / 淡出 pulse（非本 id 的定时器会被放行）。
        MESSAGE_HANDLER(WM_TIMER, OnScrollBarAnimTimer)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(FMG_MSG_OPEN_CONTACT_PROFILE, OnOpenContactProfileMsg)
        MESSAGE_HANDLER(FMG_MSG_QUERY_STARRED, OnQueryStarredMsg)
        MESSAGE_HANDLER(FMG_MSG_TOGGLE_STARRED, OnToggleStarredMsg)
        MESSAGE_HANDLER(FMG_MSG_SET_CONTACT_REMARK, OnSetContactRemarkMsg)
        MESSAGE_HANDLER(FMG_MSG_OPEN_CHAT, OnOpenChatMsg)
        MESSAGE_HANDLER(FMG_MSG_CONV_SEND_BUMP, OnConvSendBumpMsg)
        MESSAGE_HANDLER(FMG_MSG_CONV_MARK_READ, OnConvMarkReadMsg)
        MESSAGE_HANDLER(FMG_MSG_OPEN_GROUP_MANAGE, OnOpenGroupManageMsg)
        MESSAGE_HANDLER(FMG_MSG_OPEN_MEMBER_PICKER, OnOpenMemberPickerMsg)
        MESSAGE_HANDLER(FMG_MSG_OPEN_GROUP_AVATAR_DLG, OnOpenGroupAvatarDlgMsg)
        MESSAGE_HANDLER(FMG_MSG_OPEN_GROUP_ANNOUNCE, OnOpenGroupAnnounceMsg)
        MESSAGE_HANDLER(FMG_MSG_OPEN_CHANGEPWD, OnOpenChangePwdMsg)
        MESSAGE_HANDLER(FMG_MSG_REFRESH_SELF_HEADER, OnRefreshSelfHeaderMsg)
        MESSAGE_HANDLER(FMG_MSG_LINK_OPTION_CHANGED, OnLinkOptionChangedMsg)
        MESSAGE_HANDLER(FMG_MSG_RD_START_INVITE, OnRdStartInviteMsg)
        MESSAGE_HANDLER(FMG_MSG_CALL_START_INVITE, OnCallStartInviteMsg)
        MESSAGE_HANDLER(FMG_MSG_CALL_START_VIDEO_INVITE, OnCallStartVideoInviteMsg)
        MESSAGE_HANDLER(FMG_MSG_OPEN_HISTORY, OnOpenHistoryMsg)
        MESSAGE_HANDLER(FMG_MSG_OPEN_ANNOUNCE_BOX, OnOpenAnnounceBoxMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

public:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端（消息中枢），引用不持有

private:
    /**
     *  WM_CLOSE 处理：通知 CMainDlg 退出程序，并销毁本窗口。
     */
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_SYSCOMMAND 处理：把 SC_MINIMIZE（标题栏最小化按钮、系统菜单"最小化"、
     *  Win+↓ 等都会发它）转成"隐藏到托盘"。
     *
     *  必须拦截的原因：主面板建窗时带 WS_EX_TOOLWINDOW（不进任务栏、不进
     *  Alt+Tab），工具窗口真被系统最小化时没有任务栏按钮可收纳，系统会退回
     *  只剩标题栏的老式最小化外观、停在屏幕左下角，看着像个残留窗口。
     *  其余 syscommand 交回基类 / DefWindowProc 按默认处理。
     */
    LRESULT OnSysCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_SHOWWINDOW 处理：主面板每次被显示时，把它的任务栏按钮摘掉。
     *
     *  主面板是<u>普通顶层窗口</u>（扩展样式为 0，不用 WS_EX_TOOLWINDOW，缘由见
     *  CreateAndShow 里的长注释与 `docs/主面板被其它窗口盖住问题排查.md`），所以
     *  系统每次显示它都会分配一个任务栏按钮，须逐次摘除。
     *  本 handler 只记录 / 摘除，不消费消息（bHandled 置 FALSE）。
     */
    LRESULT OnShowWindowForTaskbar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_MOVE 处理：主面板被拖动后，把搜索结果下拉同步挪到新的搜索框下方。
     *  下拉是独立的顶层浮窗（不是本窗口的子窗口），系统不会带着它一起移动，不同步
     *  就会出现"拖走主面板、下拉留在原地"的分离现象。
     *  本处理只摆位置、不重算结果，且置 bHandled=FALSE 让消息继续走原有链路。
     */
    LRESULT OnPanelMoveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_SIZE 处理：主面板尺寸变化后，把搜索结果下拉同步到新的搜索框下方并跟着改宽
     *  （下拉宽度 = 主面板客户区宽）。同样置 bHandled=FALSE，DuiFrameWindow 的最大化
     *  状态维护与 DuiHost 的客户区重排照常进行。
     */
    LRESULT OnPanelSizeMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_NCHITTEST 处理：把顶部渐变 Banner 与底部栏上的<u>空白处</u>判成 HTCAPTION，
     *  使按住那里可以拖动整个窗口（拖动本身交给系统，吸附 / 贴边等原生行为照旧）。
     *
     *  先调基类 DuiFrameWindow::OnNcHitTestMsg 取默认判定，只有结果是 HTCLIENT 时
     *  才有机会升级成 HTCAPTION —— 这样窗口最外圈缩放边框、标题栏、min/close 三按钮
     *  的判定原样保留，底边与左右下角照旧能拉伸。可拖区的具体范围见
     *  CollectDragZoneRects。
     *
     *  返回：HT* 命中码（直接作为 WM_NCHITTEST 的返回值）。
     */
    LRESULT OnPanelNcHitTestMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_NCLBUTTONDBLCLK 处理：吞掉落在可拖区里的双击。
     *
     *  空白处一旦判成 HTCAPTION，系统就会把那里的双击当成"双击标题栏"处理，进而
     *  最大化 / 还原窗口。主面板是固定窄高面板、连最大化按钮都没有（见 CreateAndShow
     *  的 SetButtons），被双击拉成全屏观感不对，故在此拦下。
     *  标题栏本身的双击不受影响：落点不在可拖区时置 bHandled=FALSE 原样放行。
     */
    LRESULT OnPanelNcLButtonDblClkMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  收集"按住可拖动窗口"的矩形清单（窗口客户区坐标）。
     *
     *  可拖区取两块容器控件的整块矩形：顶部渐变 Banner（kIdHeader）与底部栏
     *  （kIdBottomBar）；排除区是这两块里所有可点击的子区域 —— Banner 的头像 /
     *  昵称 / 个性签名 / 在线状态胶囊 / 消息盒子，底部栏的汉堡菜单按钮与主操作按钮。
     *
     *    dragRects     [出参]可拖矩形数组，由调用方提供、容量须 >= kMaxDragRects。
     *    dragCount     [出参]实际填入的可拖矩形条数。
     *    excludeRects  [出参]排除矩形数组，由调用方提供、容量须 >= kMaxDragExcludeRects。
     *    excludeCount  [出参]实际填入的排除矩形条数。
     *
     *  返回：true 表示至少收集到一块可拖区；false 表示界面尚未构建（客户区控件树
     *        还没建好），此时两个计数均为 0，调用方应视作"哪里都不能拖"。
     */
    bool CollectDragZoneRects(RECT* dragRects, int& dragCount,
                              RECT* excludeRects, int& excludeCount);

    /**
     *  判定某个<u>屏幕坐标</u>点是否落在主面板的可拖区内。内部换算到窗口客户区坐标
     *  后交给纯函数 balloonmain::IsPointInDragZone 判定。
     *    ptScreen：待判定的点（屏幕坐标，WM_NCHITTEST / WM_NCLBUTTONDBLCLK 的 lParam）。
     *  返回：true 表示按住该点可以拖动窗口。
     */
    bool IsPointInPanelDragZone(POINT ptScreen);

    /**
     *  WM_TIMER 处理：驱动三个列表面板悬浮滚动条的淡入 / 淡出与空闲自动隐藏。
     *
     *  一个 HWND 上同 id 的定时器只有一份，而三个面板共用同一个 id
     *  （balloonmain::kScrollbarAnimTimerId），所以只能由本窗口统一推进 —— 每次
     *  WM_TIMER 把三个面板各推进一帧，三个都报告"不需要下一帧"时 KillTimer。
     *  定时器由面板自己在需要动画时 SetTimer 起（见 MainPanelScrollView）。
     *  其余 id 的 WM_TIMER 置 bHandled=FALSE 放行，不影响别的定时器。
     */
    LRESULT OnScrollBarAnimTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  按控件 id 取某个 Tab 内容面板的滚动视口；找不到（界面尚未构建）返回 NULL。
     *    paneId：滚动视口的控件 id（kIdPaneMessages / Contacts / Groups）。
     */
    balloonmain::MainPanelScrollView* FindScrollPane(UINT paneId);

    /**
     *  把"悬浮滚动条要给窗口缩放边框让出多少像素"同步给三个列表面板。
     *
     *  DuiFrameWindow 让客户区铺满整个窗口矩形，又把最外侧 GetBorderPx() 个逻辑
     *  像素判为缩放边框；贴着右缘画的滚动条会整条落在那道带子里 —— 鼠标变成左右
     *  双向箭头、滑块抓不住。故让滚动条整体往里挪同样的宽度。
     *
     *  须在界面构建完成后调用一次，并在窗口尺寸 / DPI 变化后再调（边框宽度是
     *  96-dpi 逻辑像素，换显示器后折算出的物理像素会变）。
     */
    void RefreshScrollBarEdgeInset();

    /**
     *  隐藏主面板到托盘：收起附属浮层后隐藏窗口，并在该账号首次发生时请
     *  CMainDlg（托盘图标持有者）弹一次"已最小化到托盘"气泡提示，弹过即
     *  写入该账号配置不再重复。托盘图标常驻，用户可从托盘单击 / 托盘菜单
     *  "打开主面板"把它唤回。
     */
    void HideToTray();

    /**
     *  WM_DUI_NOTIFY 处理：接收客户区控件冒上来的 DUI 通知。当前只处理
     *  三个 Tab 的点击 —— 点击即切换 Tab。
     */
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  确保 v2 历史消息本地库已按当前登录账号打开（惰性打开 + 账号切换切库）。
     *  本地库文件为当前用户个人目录下的 ChatLog\msgv2.db；首次调用时建目录、
     *  打开并建表，之后复用；若检测到目标路径与已打开路径不同（账号切换），
     *  先关旧库再开新库。供两处入库收口点（OnRecvChatMessage / OnSyncMessages
     *  Arrived）在写库前调用。
     *  @return 本地库已就绪返回 true；客户端未就绪 / 打开失败返回 false
     */
    bool EnsureMsgStoreOpen();

    /**
     *  确保平台公告本地库 announce.db 已按当前登录账号打开（惰性打开 + 账号切换切库）。
     *  库文件与历史消息库同目录：<个人目录>\ChatLog\announce.db。
     *
     *  不必像 EnsureMsgStoreOpen 那样自己记住已开路径 —— CAnnounceStore::Open 本身就是
     *  幂等的：同一路径重复调直接返回成功，路径变了（账号切换）会先关旧库再开新库。
     *
     *  @return 本地库已就绪返回 true；客户端未就绪 / 打开失败返回 false
     */
    bool EnsureAnnounceStoreOpen();

    /**
     *  切换到指定 Tab：更新三个 Tab 的选中态、显隐对应内容面板、刷新
     *  底部主操作按钮文字（群页"建群"，其余"查找"），并重新布局重绘。
     *    tabId：目标 Tab 的控件 id（kIdTabMessages / Contacts / Groups）。
     */
    void SwitchTab(UINT tabId);

    /**
     *  展开 / 收起一个联系人分组：翻转该分组标题的展开态，并显隐其下属的
     *  列表项（该标题之后、下一个分组标题之前的所有控件），再重排重绘。
     *    sec：被点击的分组标题控件。
     */
    void ToggleSection(balloonmain::MainPanelSectionHeader* sec);

    /**
     *  构建并装载主面板客户区控件树。须在窗口 Create 之后调用。
     */
    void InitUi();

    /**
     *  把"我的好友"+"服务号"两个分组的 mock 数据填入 Contacts 页的
     *  DuiTreeView(kIdContactsTree)。19a 阶段:数据源仍是 mock(后续
     *  阶段会替换为从 CFlamingoClient 取真实好友 / 服务号)。须在 InitUi
     *  里 root 构建完之后调用,否则 FindCtrlById 拿不到 TreeView。
     */
    void FillContactsTree();

    /**
     *  把"我的群聊"分组的 mock 数据填入 Groups 页的 DuiTreeView
     *  (kIdGroupsTree)。19b 阶段:数据源仍是 mock(后续阶段从
     *  CFlamingoClient 取真实群列表)。
     */
    void FillGroupsTree();

    /**
     *  把"最近会话"列表的 mock 数据填入 Messages 页的 DuiTreeView
     *  (kIdMessagesTree)。会话是扁平结构(每条会话一个根节点,无层级)。
     *  19c 阶段:数据源仍是 mock(后续阶段从 CFlamingoClient 的 recent
     *  list 取真实会话)。
     */
    void FillMessagesTree();

    /**
     *  用 m_conversations（真实单聊会话）填充 Messages 页的 DuiTreeView
     *  (kIdMessagesTree)，并建立"树节点 → 对端 userid"映射（供点击开聊 / 清未读）。
     *  会话是扁平结构，每条一个根节点；置顶会话排在前面，其余按最后消息时间
     *  倒序。可反复调用：先清空旧节点 / 旧映射 / 旧头像位图再重建。须在
     *  m_conversations 排好序之后调用。
     */
    void FillConversationsTree();

    /**
     *  把一条会话冒泡到顶部并更新预览 —— 收发消息时调用。会话不存在则新建。
     *  更新最后消息预览 / 时间 / 发送方后整表重排重绘。会话用 (ctype,cid) 唯一标识。
     *    ctype        会话类型（1 单聊 cid=对端userid；2 群聊 cid=群id）。
     *    cid          会话ID。
     *    fromUserId   本条消息发送方 userid（用于会话记录，UI 暂不直接用）。
     *    digestUtf8   最后一条消息的预览文字（UTF-8）。
     *    msgType      消息内容类型（见 net 层 message_type；UI 暂存不直接用）。
     *    timestampMs  消息时间（Unix 毫秒）。
     *    msgid        本条消息的服务器 msgid，用于推进会话的已读上限锚点
     *                 m_lastmsgid（取较大值，msgid 全局自增）。供 MarkConversationRead
     *                 上报 cmd_mark_read 时取该锚点作 readmsgid。本端发出时 msgid
     *                 暂未知，传 0，本方法即不更新锚点。
     *    incUnread    是否未读数 +1（收到对端/群消息为 true；自己发出为 false）。
     */
    void BumpConversation(int ctype, int64_t cid, UINT fromUserId,
                          const std::string& digestUtf8, int msgType,
                          int64_t timestampMs, int64_t msgid, bool incUnread);

    /**
     *  把某会话标记为已读 —— 打开该会话窗口时调用。两件事：①本地未读清零并刷新
     *  该会话行右侧文字（去掉未读计数）；②向服务端上报已读位点（cmd_mark_read，
     *  readmsgid 取该会话的 m_lastmsgid），更新 t_conversation.f_readmsgid，使重新
     *  登录时未读数能正确清零。会话不存在时仅回投托盘清除、不上报。会话用
     *  (ctype,cid) 唯一标识。
     *    ctype  会话类型（1 单聊 2 群聊）。
     *    cid    会话ID（单聊=对端userid，群聊=群id）。
     */
    void MarkConversationRead(int ctype, int64_t cid);

    /**
     *  把"消息"Tab 的未读角标刷成真实未读总数 —— 取 m_conversations 各会话未读
     *  之和，set 到 kIdTabMessages 对应的 MainPanelTab 上（0 则不显示角标）。
     *  会话有任何变化（拉取/收发/打开清零）后调用；统一收口在 FillConversationsTree
     *  末尾，因为三处变更最终都会走它。
     */
    void RefreshMessagesTabBadge();

    /**
     *  在 m_contacts 缓存里按 userid 查联系人资料；找不到返回 NULL。返回的
     *  指针指向 m_contacts 内元素，调用方不得跨"下次通讯录刷新"持有。
     *    userId   联系人 userid。
     */
    const net::ContactItem* FindCachedContact(UINT userId) const;

public:
    /**
     *  处理服务端推来的"某条消息被撤回"通知（cmd_message_recalled_notify）。
     *  由 CMainDlg::OnV2MessageRecalled 转交。撤回者本人也会收到本通知，故
     *  据 notify.m_operator 与本端 userid 比较来决定提示文案。
     *
     *  做三件事：把本地消息库该条标记为已撤回（使"消息记录"窗口不再显示它）、
     *  把对应聊天窗里的气泡就地变成提示行、以及在被撤的是会话最新一条时把会话
     *  列表的预览文字改成同一句提示。
     *
     *  @param notify 撤回通知（ctype / cid / msgid / operator 均已校验为有效）
     */
    void OnMessageRecalled(const net::MessageRecalledNotify& notify);

    /**
     *  处理"某条消息被 Pin / 取消 Pin"的推送（cmd_message_pinned_notify）。
     *
     *  做四件事：把本地消息库该条的 Pin 标记落上 / 清掉；刷新已打开聊天窗里对应气泡的
     *  图钉标记；刷新正显示该会话的消息记录窗；本地压根没有这条消息（本机没同步过那段
     *  历史）时，向服务端补拉一次该会话的被 Pin 消息全集，把消息体要回来 —— 否则用户会
     *  "明明钉了却在 Pin 分类里找不到"。
     *
     *  会话列表预览<u>不动</u>：Pin 不改变"最后一条消息是什么"。
     *
     *    notify：推送内容（借用，本方法内用完即弃，不持有）。
     */
    void OnMessagePinned(const net::MessagePinnedNotify& notify);

    /**
     *  处理"某会话被 Pin 的消息全集"应答（cmd_get_pinned_messages）。
     *
     *  逐条把完整消息体落进本地库（补齐本地缺的行），再与这一份对账：把本地多出来的
     *  Pin 标记清掉。对账的时间下界跟着"本次有没有被截断"走 —— 服务端单次最多返回
     *  最近 PIN_FETCH_LIMIT 条，截断时那些更老的 Pin 不在返回集合里，全量清零会把它们
     *  错杀，故改用返回集合里最老一条消息的时间戳作下界。详见
     *  docs/消息Pin接入方案.md §4.3。
     *
     *    result：应答内容（借用，本方法内用完即弃，不持有）。
     */
    void OnPinnedMessagesArrived(const net::PinnedMessagesResult& result);

    // ================================================================
    // 平台公告（主面板 Banner 上的「消息盒子」）
    //
    // 公告由企业管理控制台发布、经 chatserver 投递，见 docs/消息盒子接入方案.md。
    // 它<u>不是</u>聊天消息：不进会话列表、不占会话未读数，自成一路，入口只有 Banner
    // 底部那个铃铛按钮。也注意与<u>群公告</u>（OnGroupAnnouncements 等）区分开——那个
    // 属于某个具体的群。
    // ================================================================

    /**
     *  把 Banner 底部那一行的未读公告角标刷成本地公告库里的真实未读数：有未读时显示
     *  「你有 N 条新公告」并画红色角标，没有时只显示「消息盒子」四个字。
     *
     *  公告有任何变动（拉取到一批、收到新发布推送、收到撤回推送、用户读了一条）之后
     *  都要调它。Header 尚未创建（InitUi 之前）时静默返回。
     */
    void RefreshAnnounceBadge();

    /**
     *  打开「消息盒子」窗口 —— 点 Banner 底部那一行、或系统菜单「公告与通知」时触发。
     *  两个入口指向同一个窗口，不各开一个。
     */
    void OpenAnnounceBox();

    /**
     *  在桌面右下角弹一次「新公告」提示窗。收到新发布推送时调用。
     *    item：公告内容（借用，本方法内用完即弃）。
     */
    void ShowAnnouncePopup(const net::AnnouncementItem& item);

    /**
     *  向服务端拉取发给本人的平台公告。登录成功后由 CMainDlg 调用一次。
     *
     *  内部先确保本地库 announce.db 已按当前账号打开，取出本地最大公告ID作增量游标
     *  ——本机已经有的那些不必再拉一遍。本地库打不开（磁盘故障等）时退化为全量拉取，
     *  只是多传些数据，功能不受影响。
     */
    void RequestAnnouncements();

    /**
     *  收到平台公告列表应答（cmd_get_announcements）：逐条落本地库、刷新铃铛角标；
     *  应答里 hasmore 为 true 时以本批最后一条的ID作新游标继续拉下一批。
     *  由 CMainDlg 的 OnV2Announcements 调用。
     *    result：公告列表解析结果（成功 / 失败都会转过来，本方法据 m_code 决定处理）。
     */
    void OnAnnouncementsArrived(const net::AnnouncementsResult& result);

    /**
     *  收到平台公告推送（cmd_announcement_notify）：新发布则落库并让铃铛角标亮起来，
     *  已撤回则把本地那条标掉、从消息盒子列表移除。由 CMainDlg 的 OnV2AnnouncementNotify 调用。
     *    notify：推送解析结果；撤回时其 m_item 只有 m_announcementId 有效。
     */
    void OnAnnouncementNotify(const net::AnnouncementNotify& notify);

    /**
     *  收到平台公告已读上报的应答（cmd_mark_announcement_read）。成功时<u>不动界面</u>
     *  ——已读在发起上报时就乐观写进本地库、角标也已经减掉了。失败只记日志留痕，供日后
     *  排查「这台机器上读过的公告，换台机器登录仍显示未读」。由 CMainDlg 的
     *  OnV2AnnMarkReadResult 调用。
     *    result：应答解析结果，其中 m_announcementId 由服务端原样回带。
     */
    void OnAnnounceMarkReadResult(const net::AnnounceMarkReadResult& result);

private:
    /**
     *  把一份被 Pin 消息全集的标记打到已打开聊天窗的气泡上，并校准该窗顶部
     *  「已 Pin N 条」提示带的条数。
     *
     *  聊天窗的历史是从网络渲染的，而 net::MessageNotify 里没有 Pin 信息，故刚开窗时
     *  那些被钉住的老消息不会带图钉 —— 靠本方法在全集应答到达时补上。会话的聊天窗
     *  没开时什么都不做（下次开窗会重新拉一遍）。
     *
     *  提示带的条数取应答里的 total 而非返回条数：单次拉取有上限（PIN_FETCH_LIMIT），
     *  被截断时用返回条数会少报。
     *
     *    result：被 Pin 消息全集应答（借用，不持有）。
     */
    void ChatWindowApplyPinnedSet(const net::PinnedMessagesResult& result);

public:

    /**
     *  把一条通话相关的系统提示追加到与某联系人的聊天窗里（居中提示行）。
     *  由 CMainDlg 在通话被拒 / 无应答 / 对方忙 / 通话结束时调用。
     *
     *  聊天窗未打开时会先建窗 —— 通话结果是用户关心的信息，不该因为没开窗就丢掉。
     *
     *  @param peerUserId 对端 userid
     *  @param tip        提示文字（如"对方拒绝了通话"、"通话结束 05:23"）
     */
    void AppendCallTipToChat(UINT peerUserId, const CString& tip);

private:

    /**
     *  把一条单聊会话渲染成 Messages 树的一个根节点（头像 + 昵称 + 预览 +
     *  时间 + 未读计数），并记录"节点 → 对端 userid"。供 FillConversationsTree
     *  复用。
     *    tv     Messages 树控件（MainPanelTreeView，支持未读红点角标）。
     *    conv   会话数据（须为单聊 ctype==1）。
     */
    void AddConversationNode(balloonmain::MainPanelTreeView* tv,
                             const net::ConversationItem& conv);

    /**
     *  把一个 Tab 内容面板的滚动视口内容高度同步成"实际内容高"。
     *    paneId           滚动视口（MainPanelScrollView）的控件 id。
     *    treeViewId       该 pane 内 DuiTreeView 的控件 id;
     *                     treeview->GetContentHeight() 反映所有可见行总高。
     *    paneTitleHeight  vbox 顶部"通讯录 · N 人"/"我的群聊 · N"标题行的
     *                     固定高度,逻辑像素;Messages 页没有标题时传 0。
     *
     *  方案 1a:scrollview 默认关 auto-content-height,本函数把 "paneTitle 高 +
     *  treeview 内容总高 + 内容 vbox 上下 padding" 喂给 scrollview->
     *  SetContentHeight,再 ForceLayout 让 nominal rect 跟上。须在 Fill*Tree
     *  填完节点之后调,以及节点展开/收缩/增删之后再调一次。
     */
    void SyncPaneContentHeight(UINT paneId, UINT treeViewId, int paneTitleHeight);

    /**
     *  FMG_MSG_SET_CONTACT_REMARK 处理：联系人资料窗提交了备注名。
     *    wParam：对端 userid。
     *    lParam：堆上的 std::string*，用户敲进去的原始文本（UTF-8，未 trim），
     *            本方法负责 delete。
     *  实际处理转交 SetContactRemark。
     */
    LRESULT OnSetContactRemarkMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  某联系人的显示名可能变了：重刷通讯录树与会话列表，并把已打开的单聊窗标题、
     *  正开着该人的资料窗一并更新。改备注（含失败回滚）后调用。
     *    peerUserId： 显示名发生变化的联系人 userid。
     */
    void RefreshContactDisplayName(UINT peerUserId);

    /**
     *  打开联系人资料窗并让「备注名」那一行直接进入编辑态 —— 右键菜单「修改备注」
     *  的落点。不另做输入对话框：资料窗已有一套完整的行内编辑（铅笔 / 编辑框 /
     *  失焦去抖提交），两个入口共用它，交互与校验才一致。
     *    peerUserId： 目标联系人 userid；不在通讯录缓存里时静默返回。
     */
    void BeginContactRemarkEdit(UINT peerUserId);

    /**
     *  打开(或前置)个人信息对话框 —— Header 头像点击时触发。已打开则
     *  前置;未打开则创建,数据取自 m_lpFMGClient->m_UserMgr.m_UserInfo。
     */
    void OpenProfileDlg();

    /**
     *  打开(或前置)修改头像对话框 —— Header 头像格点击时触发。已打开则前置；
     *  未打开则创建。进入时按当前用户已存的 (facetype, customface) 预选，数据取自
     *  m_lpFMGClient->m_UserMgr.m_UserInfo（m_nFace=facetype、m_strCustomFaceName=
     *  customface 的 UTF-8 字节）。
     */
    void OpenModifyAvatarDlg();

    /**
     *  按当前用户已存的 (facetype, customface) 重新注入 Banner 头像字 / 底色。
     *  改头像成功后调用，使主面板自己的头像即时更新。
     */
    void RefreshSelfAvatar();

    /**
     *  按当前用户头像类型给 Banner header 设置 / 清除"真实头像位图"：facetype==自定义图
     *  时按 fileid 从本地头像缓存载图设给 header（缓存缺失则清空、回退字形）；否则清空。
     *    facetype       当前用户头像类型（见 balloonmain::AvatarKind）。
     *    customfaceUtf8 当前用户 customface（UTF-8；自定义图时即 fileid）。
     *    userid         当前用户 id（定位头像缓存目录）。
     */
    void ApplySelfAvatarBitmap(int facetype, const std::string& customfaceUtf8, UINT userid);

    /**
     *  开始"个性签名就地编辑" —— 点 Header 签名区(MainPanelHeader 发
     *  DUIN_HEADER_SIGN_CLICK)时调用。取签名文字的命中矩形转屏幕坐标，把
     *  内联编辑弹层(m_signEditPopup)精确盖上去，带入当前签名并聚焦全选。
     */
    void BeginSignatureEdit();

    /**
     *  签名内联编辑提交回调(回车/失焦触发)。跑 DecideSignatureCommit 判定：无
     *  变化则不发；离线则提示不发；否则乐观更新(本地签名+刷新 Header)并发
     *  cmd_update_profile。
     *    newText：编辑框当前文本(未 trim)。
     */
    void OnSignatureCommitted(const CString& newText);

    // m_signEditPopup 的提交回调跳板(转本类 OnSignatureCommitted)。
    static void OnSignCommitThunk(void* userdata, const CString& newText);

    /**
     *  打开（或前置）与指定对端的单聊窗口 —— 点击联系人树节点时调用。
     *  按 userid 维护"一个对端一个窗口"：已开则前置，未开则新建。
     *    peerUserId：对端 userid（账号ID）。
     */
    void OpenSingleChat(UINT peerUserId);

    /**
     *  按对端 userid 取得其单聊窗口，必要时创建（但不前置/不抢焦点）——
     *  供收到消息时确保窗口存在。返回的窗口已绑定对端 userid 并显示。
     *    peerUserId：对端 userid。
     *  @return 对应单聊窗口指针；创建失败返回 NULL。
     */
    CBalloonChatWindow* EnsureChatWindow(UINT peerUserId);

    /**
     *  计算某会话的显示名 + monogram 头像首字 + 底色（与会话列表 AddConversationNode
     *  完全同口径：群名 / 昵称 / "用户<id>"；首字取显示名前 1 字；底色按 cid 稳定取色）。
     *  供会话列表渲染与"托盘闪头像"回投复用，保证托盘头像与列表头像同字同色。
     *    ctype     会话类型（1 单聊 2 群聊）。
     *    cid       会话 ID（单聊=对端 userid，群聊=群 id）。
     *    outName   [出参] 会话显示名。
     *    outGlyph  [出参] 头像首字（显示名前 1 字符）。
     *    outColor  [出参] 头像底色。
     */
    void ComputeConvDisplay(int ctype, int64_t cid, CString& outName,
                            CString& outGlyph, COLORREF& outColor);

    /**
     *  向 CMainDlg（隐藏的托盘宿主）回投一条"托盘闪头像"消息：算好该会话的显示名 /
     *  首字 / 底色，堆分配 TrayFlashData 经 FMG_MSG_TRAY_FLASH 发出（CMainDlg 处理后
     *  delete）。仅在收到的消息"应在托盘闪烁"（非抖动 / 对端发来 / 对应聊天窗未开）时调用。
     *    ctype       会话类型（1 单聊 2 群聊）。
     *    cid         会话 ID。
     *    digestUtf8  最新一条消息的预览文字（UTF-8，与会话列表 digest 同口径，如"[图片]"/正文），
     *                悬停弹窗副行显示，过长由行控件自动省略号截断。
     */
    void PostTrayFlash(int ctype, int64_t cid, const std::string& digestUtf8);

    /**
     *  向 CMainDlg 回投一条"托盘清除该会话"消息（FMG_MSG_TRAY_CLEAR）：某会话被读后，
     *  让 CMainDlg 把它移出托盘闪烁集合。会话 ID 经堆分配 long long 传递（Win32 下
     *  lParam 容不下 64 位），CMainDlg 处理后 delete。
     *    ctype  会话类型（1 单聊 2 群聊）。
     *    cid    会话 ID。
     */
    void PostTrayClear(int ctype, int64_t cid);

    /**
     *  递归把一个部门及其子部门、直属成员加进联系人树。供 OnContactsArrived
     *  按 parentdeptid 还原组织架构树时调用。
     *    tv                树控件。
     *    parentTreeNodeId  父树节点 id；<0 表示本部门作为根节点（AddRoot）。
     *    dept              当前部门。
     *    childrenByParent  "父部门id → 子部门指针列表" 索引（指针指向 result
     *                      内的部门，调用期间有效）。
     *    contactById       "userid → 联系人资料指针" 索引（同上，调用期间有效）。
     *    labelSuffix       追加在本部门节点名字后面的文字，NULL 表示不追加。目前
     *                      只有公司根部门用它挂人数（形如 " · 161 人"）；递归到
     *                      子部门时一律传 NULL，人数只出现在公司那一行。
     *
     *  成员列表<u>包含当前登录用户自己</u>——用户要能在树上看到自己处在哪个部门。
     *  自己那一行的"（我）"后缀由 AddContactLeaf 统一加。
     */
    void AddDepartmentSubtree(balloonwjui::DuiTreeView* tv, int parentTreeNodeId,
                              const net::DepartmentItem& dept,
                              const std::map<int64_t, std::vector<const net::DepartmentItem*> >& childrenByParent,
                              const std::map<int64_t, const net::ContactItem*>& contactById,
                              LPCTSTR labelSuffix);

    /**
     *  在联系人树上挂一个联系人叶子节点（带 monogram 头像，记录 node→userid）。
     *  供部门成员、"其他联系人"、"服务号" 三处复用。
     *    tv          树控件。
     *    parentNode  父节点 id。
     *    c           联系人资料。
     *
     *  若 c 就是当前登录用户，显示名会自动加上"（我）"后缀 —— 判定放在本方法
     *  内部而非各调用点，是因为挂叶子的地方有五处，逐处判会漏。
     */
    void AddContactLeaf(balloonwjui::DuiTreeView* tv, int parentNode,
                        const net::ContactItem& c);

    /**
     *  按某实体（联系人 / 单聊对端）的 (facetype, customface) 生成一张 sizePx 圆形头像
     *  位图（树图标用，调用方负责入池释放）：字形自定义 / 自动 → 现画 monogram；真图
     *  → 命中本地缓存即画真图，未命中则回退 monogram 并触发头像下载（下载完成经
     *  OnAvatarDownloadResult 刷新本节点）。
     *    id          实体 id（联系人 userid；自动取色 / 回退首字用）
     *    displayName 显示名（回退首字用）
     *    facetype    头像类型（见 balloonmain::AvatarKind）
     *    customface  头像标识（UTF-8）
     *    sizePx      位图边长（像素）
     *  返回：HBITMAP（所有权归调用方）；失败可能返回 NULL。
     */
    HBITMAP MakeEntityAvatarBitmap(int64_t id, const CString& displayName,
                                   int facetype, const std::string& customface, int sizePx);

    /**
     *  取某个在线状态的徽标位图，用于往 DuiTreeView 的行右端挂状态点
     *  （DuiTreeView::SetItemStatusIcon）。四种徽标是常量图形，本方法按 presence
     *  缓存复用，首次用到时才创建，位图归本类所有、析构时统一释放 —— 调用方
     *  <u>不得</u> DeleteObject，也不要存进 m_avatarBitmaps 那几个随刷新清空的池子里。
     *    presence：v2 在线状态（presence_type）；隐身与越界值按"离线"取，
     *              与 GetPresenceBadgeIconKind 的兜底口径一致。
     *  返回：徽标位图；创建失败返回 NULL（此时调用方跳过挂图即可，树会退回不画状态点）。
     */
    HBITMAP GetPresenceBadgeBitmap(int presence);

    /**
     *  打开（或前置）指定群的群聊窗口 —— 点击"群"Tab 群节点 / "消息"Tab 群会话时
     *  调用。按 groupid 维护"一个群一个窗口"：已开则前置，未开则新建。
     *    groupId：群 id。
     */
    void OpenGroupChat(int64_t groupId);

    /**
     *  按 groupid 取得其群聊窗口，必要时创建（但不前置/不抢焦点）—— 供收到群消息
     *  时确保窗口存在。返回的窗口已绑定 groupid / 群名并显示。
     *    groupId：群 id。
     *  @return 对应群聊窗口指针；创建失败返回 NULL。
     */
    CBalloonGroupChatWindow* EnsureGroupWindow(int64_t groupId);

    /**
     *  取当前打开着的、且绑定在指定群上的群管理窗口。
     *    groupId：要求窗口绑定的群ID；传 0 表示不校验群、只要窗口开着就行
     *             （写操作的应答体里没有 groupid，只能这样）。
     *  返回：满足条件的窗口指针；窗口没开、已销毁、或管的是别的群时返回 NULL。
     */
    CBalloonGroupManageWindow* GetLiveGroupManageWindow(int64_t groupId);

    /**
     *  本人已离开某群（主动退出 / 被移出 / 群被解散）之后的本地收尾：从"我的群"
     *  列表、群资料缓存、会话列表里去掉它，刷新两棵树，清掉托盘上属于它的未读
     *  提示，并关掉它的聊天窗。
     *    groupId：群ID；非正值直接返回。
     */
    void CleanupLeftGroup(int64_t groupId);

    /**
     *  群名变更后同步到所有显示它的地方：会话列表行、群资料缓存、开着的群聊窗
     *  标题、"群"Tab 的列表。
     *    groupId： 群ID；
     *    nameUtf8：新群名（UTF-8，来自协议层）；空串直接返回。
     */
    void UpdateGroupNameEverywhere(int64_t groupId, const std::string& nameUtf8);

    /**
     *  取某群的显示名：优先群信息缓存里的真实群名，无则占位"群聊(<id>)"。
     *    groupId：群 id。
     */
    CString GroupDisplayName(int64_t groupId) const;

    /**
     *  取本人在某群的角色（用于群管理入口的门控）。优先群资料缓存里的 m_myrole；
     *  未知时若群主 id 等于本人则判为群主，否则按普通成员处理。
     *    groupId：群 id。
     *  @return net::group_member_role（0成员 1管理员 2群主）。
     */
    int MyRoleInGroup(int64_t groupId) const;

    /**
     *  打开某群的群管理对话框（仅群主 / 管理员可打开，内部再次门控）。同一时刻
     *  只保留一个对话框：同群已开则前置，换群则关旧开新。本轮对话框各页仍为
     *  假数据，仅 Header 显示真实群名与本人角色。
     *    groupId：群 id。
     */
    void OpenGroupManage(int64_t groupId);

    /**
     *  若群资料对话框正为 groupId 打开，则把"群信息缓存 + 会话缓存 + 已到达的公告 /
     *  群主显示名 / 我的群名片"组成完整 GroupInfoViewData，调对话框 ApplyData 刷新。
     *  群资料(1303) / 群成员(1304) 应答到达、以及 OpenGroupInfo 组初值时都调它。
     *    groupId：要刷新的群 id（与当前对话框所属群不一致时本调用为空操作）。
     */
    void RefreshGroupInfoDialog(int64_t groupId);

    /**
     *  在鼠标处弹出群聊右键菜单（结构对照 design/群聊右键菜单.png）：进入群聊 /
     *  查看群资料 / 置顶群聊 / 消息免打扰 / 清空聊天记录 / 退出群聊；本人为群主或
     *  管理员时额外显示"群管理"。同步弹出，选中后按项分发：进入群聊→开群聊窗、
     *  群管理→开群管理对话框，其余功能待接后端，暂提示"开发中"。
     *    groupId：右键命中的群 id。
     */
    void ShowGroupContextMenu(int64_t groupId);

    /**
     *  在鼠标处弹出联系人右键菜单（发消息 / 语音 / 视频 / 查看资料 / 修改备注 /
     *  移动到分组 / 消息免打扰 / 加入黑名单 / 删除好友）。同步弹出，选中后按项
     *  分发：发消息→开单聊；其余功能待接后端，暂提示"开发中"。
     *    peerUserId：右键命中的联系人 userid。
     */
    void ShowContactContextMenu(UINT peerUserId);

    /**
     *  在鼠标处弹出"消息"Tab 会话列表的右键菜单（打开会话 / 标记已读 / 查看历史消息 /
     *  查看资料（群会话为查看群资料）/ 置顶（取消置顶）/ 消息免打扰（取消免打扰）/
     *  删除该会话 / 删除全部会话）。菜单行由 balloonmain::BuildConversationMenuModel
     *  按该会话当前状态生成，本函数只负责渲染、夹取落点与选中后的分发。同步弹出。
     *    ctype：右键命中会话的类型（1 单聊 cid=对端userid；2 群聊 cid=群id）。
     *    cid：  右键命中的会话 ID。
     */
    void ShowConversationContextMenu(int ctype, int64_t cid);

    /**
     *  切换某个对象的收藏状态：先做本地乐观更新并重填收藏分区，再发 cmd_set_favorite。
     *  三处右键菜单（联系人 / 群 / 会话）的"添加到收藏 / 取消收藏"都汇到这里。
     *    ctype： 1 联系人、2 群组。
     *    cid：   对象ID；<= 0 时本方法什么都不做。
     */
    void ToggleFavorite(int ctype, int64_t cid);

public:
    /**
     *  切换某个联系人的特别关注状态：先做本地乐观更新，再发 cmd_set_starred。
     *  四处右键菜单（主面板联系人 / 主面板会话 / 群成员列表 / 气泡的 @片段与
     *  发送者头像）的"特别关注 / 取消特别关注"都汇到这里。
     *
     *  公开而不是私有：后两处入口在群聊窗那边，只能经主面板 HWND 转过来调。
     *    userid： 被关注的联系人 userid；<= 0 或等于本人时本方法什么都不做
     *             （关注自己没有意义，服务端也会拒）。
     */
    void ToggleStarred(int64_t userid);

private:

    /**
     *  往「联系人」Tab 的树里插入「我的收藏」根节点及其子行。须在铺开部门树<u>之前</u>
     *  调用，收藏分区固定排在树顶。收藏为空时本方法什么都不做 —— 根节点整个不出现，
     *  而不是留一个空壳（下面紧跟着部门树，空节点很突兀）。
     *    tv： 目标 TreeView；空指针时直接返回。
     */
    void AddFavoritesSection(balloonwjui::DuiTreeView* tv);

    /**
     *  用缓存的通讯录应答重建「联系人」Tab 的 v2 组织架构树（含收藏分区）。
     *  收藏发生变化、以及群列表到达后需要补出群收藏行时调用。
     *
     *  RefreshContacts() 现在就是转调本方法（企业版删掉好友子系统后，它原先走的
     *  v1 好友列表适配器已成空实现），两者等价，调哪个都行。
     *
     *  重建前会先把收藏模型的现状写回缓存，避免重入时被服务端下发的旧值覆盖掉
     *  刚做的乐观更新。尚未收到过通讯录时本方法什么都不做。
     */
    void RebuildContactsTree();

    /**
     *  删除一条会话 —— 把它从"最近会话列表"移除，<u>不删除聊天消息本体</u>（历史消息
     *  记录窗仍能查到，服务端 t_chatmsg 不动）。先弹二次确认；确认后向服务端发
     *  cmd_set_conversation{deleted:true}，并乐观地从本地会话缓存移除、重填会话树、
     *  刷新未读角标、清掉该会话的托盘闪烁。
     *
     *  该会话之后再收到新消息时会重新出现在列表里（本端由 BumpConversation 立刻重建，
     *  服务端侧由删除时记下的 msgid 锚点判定）。
     *    ctype：会话类型（1 单聊 2 群聊）。
     *    cid：  会话 ID。
     */
    void DeleteConversation(int ctype, int64_t cid);

    /**
     *  删除全部会话 —— 语义同 DeleteConversation，作用于列表里的每一条会话。先弹
     *  二次确认（这一步不可逆，且一次影响所有会话），确认后逐条向服务端发删除请求
     *  （服务端没有批量删除命令，只能一条一条发），再清空本地会话缓存并重填会话树。
     *  会话列表为空时直接返回，不弹确认框。
     */
    void DeleteAllConversations();

    /**
     *  设置一条会话的置顶状态。乐观更新本地缓存（置顶会改变排序，故内部会重排 + 重填
     *  会话树），再发 cmd_set_conversation 上报服务端。
     *    ctype：  会话类型（1 单聊 2 群聊）。
     *    cid：    会话 ID。
     *    pinned： 目标置顶状态。
     */
    void SetConversationPinnedState(int ctype, int64_t cid, bool pinned);

    /**
     *  设置一条会话的免打扰状态。乐观更新本地缓存并重填会话树（行右下角的静音标记与
     *  未读胶囊配色要跟着变），再发 cmd_set_conversation 上报服务端。
     *
     *  免打扰生效后，该会话收到新消息时不播提示音、不闪托盘图标，但未读数照常累加。
     *    ctype： 会话类型（1 单聊 2 群聊）。
     *    cid：   会话 ID。
     *    mute：  目标免打扰状态。
     */
    void SetConversationMuteState(int ctype, int64_t cid, bool mute);

    /**
     *  打开一条会话对应的资料窗：单聊开联系人资料窗，群聊开群资料对话框。供会话
     *  右键菜单的"查看资料 / 查看群资料"分发。
     *    ctype： 会话类型（1 单聊 2 群聊）。
     *    cid：   会话 ID（单聊=对端 userid，群聊=群 id）。
     */
    void OpenConversationProfile(int ctype, int64_t cid);

    /**
     *  打开（或刷新前置）某联系人的资料窗口。从通讯录缓存取该联系人的真实
     *  资料（含职位 / 部门路径），灌入 CBalloonContactWindow 并显示。
     *    peerUserId：联系人 userid。
     */
    void OpenContactProfile(UINT peerUserId);

    /**
     *  判断某个 userid 是不是当前登录用户本人。
     *
     *  通讯录树里现在会显示自己（好让用户看清自己在组织架构中的位置），于是
     *  凡是"对某个联系人做点什么"的入口都要先问一句这是不是自己：与自己不能
     *  聊天，能做的只有查看资料。
     *
     *    userId：待判断的 userid；0 一律返回 false。
     *  返回：是本人返回 true；客户端对象为空（调试入口）时一律返回 false。
     */
    bool IsSelfUserId(UINT userId) const;

    /**
     *  取当前登录用户的 userid。客户端对象为空（调试入口用假数据起窗时）返回 0。
     *  返回：当前登录用户 userid。
     */
    UINT SelfUserId() const;

    /**
     *  从通讯录缓存为某联系人拼出资料窗用的 ContactModel（昵称 / 账号 / 地区 /
     *  职位 / 部门 / 签名 / 在线状态 / 分组）。OpenContactProfile 与 1108 资料变更
     *  后台刷新路径(OnUserInfoChanged)共用,避免两处重复拼装逻辑。
     *    peerUserId  联系人 userid。
     *    outModel    [出参] 填好的资料模型。
     *  返回：该 userid 在通讯录缓存里则 true；否则 false（outModel 不可用）。
     */
    bool BuildContactModelFromCache(UINT peerUserId, ballooncontact::ContactModel& outModel);

    /**
     *  据搜索框当前文字驱动结果下拉 —— 搜索框 DUIN_VALUECHANGED 时调用。绑定数据源
     *  （联系人 + 群 + 当前用户）、算锚点（搜索框矩形）与同宽，交给 m_searchDropdown
     *  按词展示 / 刷新（空词由下拉自行隐藏）。
     */
    void UpdateSearchDropdown();

    /**
     *  算出搜索结果下拉的落点 —— 即搜索框矩形（屏幕坐标）与主面板客户区宽。
     *  UpdateSearchDropdown（按词刷新）与 SyncSearchDropdownPos（跟随移动 / 改尺寸）
     *  共用，保证两条路径的定位口径一致。
     *    outAnchorScreen  [出参] 搜索框矩形，屏幕坐标。
     *    outPanelWidth    [出参] 主面板客户区宽度，逻辑像素（= 下拉宽度）。
     *  返回：客户区控件树或搜索框控件尚不可用时返回 false（两个出参不可用）。
     */
    bool ComputeSearchAnchor(RECT& outAnchorScreen, int& outPanelWidth);

    /**
     *  把搜索结果下拉同步到当前搜索框下方 —— 主面板移动 / 改变尺寸后调用。只摆位置
     *  与宽度，不重算命中结果；下拉未显示时只更新其记下的锚点。
     */
    void SyncSearchDropdownPos();

    /**
     *  把键盘焦点移到主面板搜索框 —— 底部"查找"按钮点击时调用。用户随即输入即
     *  弹出结果下拉。
     */
    void FocusSearchBox();

    /**
     *  打开"新建群聊"对话框 —— "群"页底部"建群"按钮点击时调用。把通讯录缓存
     *  （联系人 + 部门）与当前登录用户传入对话框，用于构建可勾选成员列表。
     */
    void OpenCreateGroupDlg();

    /**
     *  打开（或前置）修改密码对话框 —— 系统菜单"修改密码"项点击时调用。
     */
    void OpenChangePwdDlg();

    /**
     *  打开（或前置）系统设置对话框 —— 系统菜单"系统设置"项点击时调用。
     */
    void OpenSystemSettingDlg();

    /**
     *  打开（或前置）关于对话框 —— 系统菜单"关于 FlamingoIM"项点击时调用。
     */
    void OpenAboutDlg();

    /**
     *  弹出主面板系统菜单 —— 左下角"汉堡"按钮（kIdBottomMenu）点击时调用。菜单
     *  内容由 BuildSystemMenuModel() 给出（个人信息 / 修改密码 / 公告与通知 /
     *  系统设置 / 网络设置 / 登出 / 关于），各项带图标，菜单锚定在按钮<u>上方</u>
     *  弹出（同步阻塞）。选中后按项分发：个人信息 / 修改密码 / 系统设置 / 关于走
     *  本类自己的 OpenXxxDlg；网络设置与登出经 m_hMainDlg 转给 CMainDlg（消息中枢）
     *  处理；公告与通知暂提示"开发中"。
     */
    void ShowSystemMenu();

    /**
     *  弹出在线状态选择菜单 —— Banner 上"状态胶囊"（kIdHeader 内）点击时调用。菜单
     *  内容由 BuildStatusMenuModel() 给出（在线 / 离开 / 勿扰 / 离线，各带彩色圆点，
     *  <u>不含隐身</u>），其中当前所处的那一项带外环圆点与"（当前）"后缀（当前状态取
     *  胶囊上正在显示的那个），锚定在胶囊<u>正下方</u>弹出（同步阻塞）。选中后：先把胶囊乐观
     *  更新成目标状态，再把目标 presence（v2 presence_type）经 m_hMainDlg 投递
     *  FMG_MSG_CHANGE_MY_PRESENCE 给 CMainDlg，由其决定走 GoOffline / GoOnline /
     *  SendChangePresence；改状态失败时由 OnChangePresenceResult 回退胶囊。
     */
    void ShowStatusMenu();

    // 子窗口路由消息处理：打开联系人资料 / 打开单聊（wParam=userid）。
    LRESULT OnOpenContactProfileMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 群聊窗代问"某联系人是否被特别关注"（FMG_MSG_QUERY_STARRED，同步 SendMessage）。
    // lParam 指向调用方栈上的 int64_t userid；返回 1 已关注 / 0 未关注。
    LRESULT OnQueryStarredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 群聊窗请求切换某联系人的特别关注状态（FMG_MSG_TOGGLE_STARRED，wParam=userid）。
    LRESULT OnToggleStarredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnOpenChatMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    // 单聊窗发出消息后的会话冒泡：wParam=对端userid，lParam=堆上 std::string*（UTF-8 预览，处理后 delete）。
    LRESULT OnConvSendBumpMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    // 聊天窗被切到前台后请求把该会话未读清零：wParam=ctype（1单聊 2群聊），
    // lParam=堆上 long long* cid（处理后 delete）。补齐"消息到达时窗口不在前台、
    // 用户随后才切回来看"这条路径上的清零。
    LRESULT OnConvMarkReadMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    // FMG_MSG_OPEN_GROUP_MANAGE：群聊窗齿轮请求打开群管理对话框，lParam=堆上
    // int64_t* groupId（处理后 delete）。
    LRESULT OnOpenGroupManageMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  群管理窗请求打开"选人"对话框（邀请成员 / 添加禁言成员 / 转让群主）。
     *  按用途组装候选列表后交给 CCreateGroupDlg 的选人模式。
     *    wParam：用途，取值见 CCreateGroupDlg::Purpose；
     *    lParam：堆分配的 int64_t 群ID，本函数处理后 delete。
     */
    LRESULT OnOpenMemberPickerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    /**
     *  群管理窗请求打开「修改群头像」对话框：用本类持有的那一个对话框实例开，
     *  头像图片上传结果才能回到同一个实例（见 GroupAvatarDlgRequest.h 的说明）。
     *    lParam：堆分配的 balloongroupmanage::GroupAvatarDlgRequest*，本函数处理后 delete。
     */
    LRESULT OnOpenGroupAvatarDlgMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    /**
     *  新公告提示窗点了「查看全部」：打开该群的聊天窗并切到右侧边栏的「公告」Tab。
     *    wParam：群ID（int64_t）；无堆指针，无需 delete。
     */
    LRESULT OnOpenGroupAnnounceMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  取当前登录账号的配置文件（<账号>.cfg）全路径。
     *  新公告弹窗的"已弹过哪条"记在这个文件的一个专属小节里，重启后仍然有效。
     *  返回：全路径；未登录（账号为空）时返回空串，调用方据此跳过记账。
     */
    CString GetUserConfigPath() const;
    // FMG_MSG_OPEN_CHANGEPWD：系统设置「账号安全」页"修改密码"按钮请求打开改密对话框（无参）。
    LRESULT OnOpenChangePwdMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    // FMG_MSG_REFRESH_SELF_HEADER：个人信息对话框乐观保存后请求按 m_UserInfo 立即刷新本人
    // Header（昵称/签名/头像）。无参，直接转 RefreshSelfAvatar()。
    LRESULT OnRefreshSelfHeaderMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    // FMG_MSG_LINK_OPTION_CHANGED：系统设置里改了「自动识别链接」开关（wParam=1 开 / 0 关）。
    // 更新进程级开关，再让已打开的单聊 / 群聊窗重新测量重绘 —— 链接片段是在气泡测量阶段
    // 算出来的，不重新测量已开着的窗口就还是旧样子。
    LRESULT OnLinkOptionChangedMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    // 单聊窗显示器按钮发起远程桌面：单聊窗 m_hMainDlg 为 NULL，故经主面板转发给
    // CMainDlg 协调器。wParam=对端userid，lParam=堆 std::wstring* peerName（转交后由 CMainDlg delete）。
    LRESULT OnRdStartInviteMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    // 单聊窗"语音通话"按钮发起呼叫：单聊窗 m_hMainDlg 为 NULL，故经主面板转发给
    // CMainDlg 通话协调器。wParam=对端userid，lParam=堆 std::wstring* peerName（转交后由 CMainDlg delete）。
    LRESULT OnCallStartInviteMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 单聊窗"视频通话"按钮发起呼叫：同上两步路由，仅消息为视频发起，转发给 CMainDlg 协调器。
    LRESULT OnCallStartVideoInviteMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // FMG_MSG_OPEN_HISTORY：聊天窗"消息记录"按钮转发来——打开该会话的历史消息记录窗。
    // wParam=ctype（1单聊 2群聊），lParam=堆上 int64_t* cid（处理后 delete）。
    LRESULT OnOpenHistoryMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  FMG_MSG_OPEN_ANNOUNCE_BOX 处理：新公告提示窗点了「查看全部」，打开消息盒子。
     *  wParam / lParam 均为 0，无堆负载。
     */
    LRESULT OnOpenAnnounceBoxMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 从通讯录缓存计算某联系人的职位与部门路径（"产品部 / 设计组"）。
    //   userId       联系人 userid。
    //   outPosition  [出参] 职位（主部门里的职务），无则空。
    //   outDeptPath  [出参] 部门路径（自顶向下，" / " 连接），无则空。
    void ComputeContactDeptInfo(UINT userId, CString& outPosition, CString& outDeptPath);

    // 搜索结果下拉"选中联系人"回调：打开与该 userid 的单聊。
    static void OnSearchPick(void* userdata, UINT peerUserId);
    // 搜索结果下拉"选中群组"回调：打开该群 id 的群聊。
    static void OnSearchPickGroup(void* userdata, int64_t groupId);

    // 主面板搜索框内嵌 EDIT 的 Esc 拦截子类过程：按 Esc 收起结果下拉（EDIT 默认
    // 吞掉 Esc 使 host 收不到，故在 EDIT 层拦截）。ref 为本面板 this。
    static LRESULT CALLBACK SearchBoxEscSubclassProc(HWND hWnd, UINT msg,
                                                     WPARAM wParam, LPARAM lParam,
                                                     UINT_PTR idSubclass, DWORD_PTR ref);

private:
    HWND m_hMainDlg;                            // CMainDlg 窗口句柄，关闭主面板时通知其退出
    // 标题栏左侧图标位图（程序主图标 IDR_MAINFRAME 合成到标题栏底色上）。
    // DuiFrameWindow::SetIcon 只借用不释放，故由本类持有、析构时 DeleteObject。
    // 建窗时生成一次，之后不再变；生成失败时为 NULL（标题栏只显示文字）。
    HBITMAP m_hTitleIcon;
    // v2 历史消息本地库：把实时收到 / 同步回填的消息按会话落本地 SQLite，供历史
    // 消息记录窗口离线查询。惰性打开（EnsureMsgStoreOpen），按 m_msgStoreDbPath
    // 识别账号切换以便切库。
    balloonhistory::CV2MsgStore m_msgStore;
    std::wstring                m_msgStoreDbPath;  // 当前已打开的本地库全路径，空=未打开
    // 历史消息记录窗口：右键"查看历史消息" / 聊天窗"消息记录"按钮打开；惰性建窗，
    // 复用同一实例（换会话即重定向刷新）。
    CBalloonHistoryWindow       m_historyWindow;
    // 消息盒子窗口（平台公告收件箱）：点 Banner 底部那一行打开；惰性建窗，复用同一
    // 实例。数据直接从本地公告库 CAnnounceStore 查，故本类不必再存一份公告。
    CBalloonAnnounceBoxWindow   m_announceBoxWindow;
    // 新公告桌面提示窗：收到新公告推送时在桌面右下角弹一次。同一时刻只有一个实例，
    // 上一条还没关就来了新的即就地换内容（弹一摞窗口比漏掉一条更糟）。
    CAnnouncePopupWindow        m_announcePopup;
    // 特别关注的上下线提示窗：被关注的联系人上线 / 下线时在桌面右下角弹一次。同一
    // 时刻只有一个实例，多人同时变化时并进同一个窗口逐行列出（弹一摞窗口是骚扰）。
    CStarredPopupWindow         m_starredPopup;
    // 登录初期的静默截止时刻（GetTickCount 口径，毫秒）。这个时刻之前收到的 presence
    // 推送一律不弹特别关注提示 —— 通讯录还没拉回来时 OnPresenceChanged 对查不到的人
    // 一律按离线处理，此时来的推送会被当成"刚上线"，不静默就会在登录瞬间弹一大片。
    DWORD                       m_starredSilenceUntilTick;
    // 单聊窗口：一个对端一个，按 userid 索引。堆分配、本类持有，析构时统一
    // 释放；窗口被用户关闭后对象仍在表中（IsWindow()==false），下次开同一
    // 对端时惰性回收重建。
    std::map<UINT, CBalloonChatWindow*> m_chatWindows;
    // 联系人树"节点id → userid"映射，OnContactsArrived 填充，树节点点击时
    // 据此取对端 userid 开聊。
    std::map<int, UINT>     m_contactNodeUserId;
    // 缓存的联系人资料池（OnContactsArrived 存入），搜索弹窗据此本地过滤。
    std::vector<net::ContactItem> m_contacts;
    // 备注名乐观更新的回滚快照："userid → 改动前的备注名"。SetContactRemark 发请求
    // 前存入，应答到达时按服务端回带的 userid 取出：成功即丢弃，失败则写回缓存并
    // 重刷显示名。用 map 而不是单个成员，是因为用户可以连着改几个人、多条请求同时
    // 在途，只记"最后一次"会把别人的备注退错。
    std::map<UINT, CString> m_pendingRemarkRollback;
    // 缓存的部门列表（OnContactsArrived 存入），算联系人职位/部门路径用。
    std::vector<net::DepartmentItem> m_departments;
    // "收到陌生人消息就补拉通讯录"这个兜底动作的节流器。正常情况下新同事是靠
    // cmd_org_structure_notify 推送进来的，这里管的是那条推送没到时（掉线重连期间
    // 发生的变更、服务端漏推）的补救，故必须限流，免得一串陌生消息把通讯录请求
    // 打成风暴。生命周期与本面板一致。
    balloonmain::UnknownSenderRefreshPolicy m_unknownSenderPolicy;
    // 缓存的会话列表（OnConversationsArrived 存入，含 ctype==1 单聊与 ctype==2 群聊）。
    // 已按"置顶优先、其余按最后消息时间倒序"排好序，与 Messages 树呈现顺序一致；
    // 收发消息时就地冒泡更新。
    std::vector<net::ConversationItem> m_conversations;
    // Messages 树"节点id → (ctype, cid)"映射，FillConversationsTree 填充，树节点
    // 点击时据此分流：单聊开单聊窗、群聊开群聊窗，并清未读。
    std::map<int, std::pair<int, int64_t> > m_msgNodeConv;
    // "群"Tab 群树"节点id → 群id"映射，FillGroupsTree 填充，点群节点据此开群聊窗。
    std::map<int, int64_t>  m_groupNodeId;

    // 群信息缓存（群 id → 群名/成员数）。群会话同步只给群 id 不给群名，须靠
    // cmd_get_group_info 单独拉；本缓存供"群"Tab 与"消息"Tab 显示真实群名。
    struct GroupBrief
    {
        CString m_name;         // 群名称
        int     m_membercount;  // 群成员总数
        int     m_myrole;       // 本人在群内角色：见 net::group_member_role（0成员 1管理员 2群主）；-1=未知（群资料未到）
        int64_t m_ownerid;      // 群主 userid；0=未知
        // 群头像类型（0 自动 / 1 字形 / 2 自定义图，见 BalloonMain/AvatarSpec.h）。
        int     m_facetype;
        // 群头像标识（UTF-8）：字形头像为 "glyph|colorIdx"，自定义图为 fileid；空=自动。
        // 这两项是主面板侧群头像的<u>单一数据源</u> —— 群 Tab 树与会话列表都从这里取，
        // 由群列表应答(1320)、群资料应答(1303)、群资料变更推送(1318)三处写入。
        std::string m_customface;
        GroupBrief() : m_membercount(0), m_myrole(-1), m_ownerid(0), m_facetype(0) {}
    };
    std::map<int64_t, GroupBrief> m_groupInfo;
    // 已发出 cmd_get_group_info 的群 id 集合，避免对同一群重复请求群资料。
    std::set<int64_t>       m_groupInfoRequested;
    // 本人加入的全部群（OnUserGroupsArrived 存入，含无消息空群）。这是"群"Tab 的
    // 权威群源（会话同步只含有消息的群，不能作群列表）。
    std::vector<net::UserGroupItem> m_myGroups;
    // 上一次 cmd_get_contacts 的完整应答。留着是为了在"收藏变了""群列表到了"这些
    // 时刻能原样重建 v2 组织架构树 —— 树是在 OnContactsArrived 里按这份数据铺开的，
    // 没有它就只能退回旧 v1 好友列表（RefreshContacts 干的正是那件事，别拿来重建本树）。
    net::ContactsResult     m_lastContactsResult;
    // 收藏分区里<u>群行</u>的"节点id → 群id"映射。联系人行不用这张表 —— 它们直接
    // 登记进 m_contactNodeUserId，双击开聊、右键弹联系人菜单两条路都能白捡。
    std::map<int, int64_t>  m_favoriteGroupNodeId;
    // 「我的收藏」模型（OnContactsArrived 灌入）。三处右键菜单据它决定文案，
    // 「联系人」Tab 树顶的收藏分区据它出行。改动走乐观更新 + 应答失败回滚。
    balloonmain::FavoritesModel m_favorites;
    // 「特别关注」模型（OnContactsArrived 灌入）。四处右键菜单据它决定文案，收到
    // 上下线推送时据它决定要不要弹右下角提示。改动走乐观更新 + 应答失败回滚。
    balloonmain::StarredModel m_starred;
    // 群聊窗口：一个群一个，按 groupid 索引（类比 m_chatWindows）。堆分配、本类
    // 持有，析构统一释放；窗口被关闭后对象仍在表中，下次开同群惰性回收重建。
    std::map<int64_t, CBalloonGroupChatWindow*> m_groupChatWindows;
    // 群管理对话框：同一时刻只开一个。堆分配、本类持有，析构统一释放；按
    // groupid 去重，换群时关旧开新。窗口关闭后对象仍在，下次打开惰性回收。
    CBalloonGroupManageWindow* m_pGroupManageWindow;
    int64_t                    m_groupManageGroupId;   // 当前群管理对话框所属群 id；0=未打开
    // 新公告提示窗：全局只有一个（同一时刻只提示一条最新的公告）。按值持有，
    // 关闭时只隐藏不销毁，下条公告来了换内容再显示。
    CBalloonAnnouncePopupDlg   m_announcePopupDlg;
    CSearchDropdown         m_searchDropdown;   // 搜索结果下拉（紧贴搜索框下方的顶层浮窗）
    CBalloonContactWindow   m_contactWindow;    // 联系人资料窗（按 userid 复用）
    balloonmain::CBalloonProfileDlg m_profileDlg; // Header 头像点击弹出的个人信息对话框
    balloonmain::CBalloonGroupInfoDlg m_groupInfoDlg; // 群会话右键"查看群资料"弹出的群资料对话框（按 GroupId 复用）
    // 群资料对话框的"晚到字段"缓存：1303/1304 应答陆续到达时暂存，供 RefreshGroupInfoDialog
    // 组完整数据。仅对当前打开的那个群有效（m_groupInfoDlgGroupId 标记归属，换群时清空）。
    int64_t                 m_groupInfoDlgGroupId;       // 当前群资料对话框所属群 id；0=未打开
    CString                 m_groupInfoDlgAnnouncement;  // 该群公告（cmd_get_group_info 到达后填）
    CString                 m_groupInfoDlgOwnerName;     // 该群群主显示名（cmd_get_group_members 到达后填）
    CString                 m_groupInfoDlgMyCard;        // 我在该群的群名片（cmd_get_group_members 到达后填）
    CSignatureEditPopup     m_signEditPopup;    // Header 签名点击弹出的内联编辑弹层
    CString                 m_signBeforeUpdate; // 乐观更新前的签名快照，应答失败时回退用
    int                     m_presenceBeforeChange; // 改状态前的在线状态快照(v2 presence)，cmd_change_presence 应答失败时回退胶囊用
    // 当前连接态（ConnectionUiState：0=在线 1=正在连接 2=离线）。由 CMainDlg 经
    // ApplyConnectionState 推入，本面板据它给自身的联网入口做门禁，并逐窗转发给聊天窗。
    // 默认按"在线"起步 —— 主面板是登录成功后才创建的。
    int                     m_connUiState;
    CCreateGroupDlg         m_createGroupDlg;   // "群"页"建群"按钮弹出的新建群对话框
    CBalloonChangePwdDlg    m_changePwdDlg;     // 系统菜单"修改密码"弹出的改密对话框
    CBalloonModifyAvatarDlg m_modifyAvatarDlg;  // Header 头像点击弹出的修改头像对话框
    CBalloonClosePromptDlg  m_closePromptDlg;   // 点关闭按钮弹出的关闭确认对话框
    CBalloonSystemSettingDlg m_systemSettingDlg; // 系统菜单"系统设置"弹出的系统设置对话框
    CBalloonAboutDlg        m_aboutDlg;         // 系统菜单"关于 FlamingoIM"弹出的关于对话框
    // 当前选中的 Tab 控件 id（SwitchTab 更新）。底部"建群/查找"按钮按它分发：
    // 群页→建群、其余→查找。初值为登录后默认选中的"联系人"页。
    UINT                    m_currentTabId;

    // 三个 Tab 的 DuiTreeView 用到的 monogram avatar HBITMAP 池。
    // DuiTreeView::SetItemIcon contract:位图所有权由调用方持有(控件
    // 内部只存裸指针),所以本类持有,析构时统一 DeleteObject。每次
    // Refresh 重建 TreeView 节点前会先 ClearAvatarBitmaps_() 清空旧的。
    std::vector<HBITMAP> m_avatarBitmaps;

    // Messages 树（会话列表）专用的 monogram avatar HBITMAP 池。会话列表与
    // 通讯录是两条相互独立、各自异步到达的应答，各自重建各自的树；若共用一个
    // 位图池，一方刷新清池会误删另一方仍在引用的位图。故会话树单独用本池：
    // FillConversationsTree 重建前只清本池，析构时统一释放。
    std::vector<HBITMAP> m_msgAvatarBitmaps;

    // "群"Tab 群树专用的 monogram 头像位图池（同理与通讯录树池分开，避免一方
    // 刷新清池误删另一方仍引用的位图）。FillGroupsTree 重建前只清本池。
    std::vector<HBITMAP> m_groupAvatarBitmaps;

    // 状态徽标缓存的槽位数 —— 对应 presence_type 的 0..3（离线 / 在线 / 离开 / 忙碌）。
    // 隐身（4）与一切越界值都归到离线槽，故四个槽够用。
    static const int kPresenceBadgeSlotCount = 4;

    // 在线状态徽标位图缓存，按 presence 取值（0 离线 / 1 在线 / 2 离开 / 3 忙碌）直接
    // 下标。四种徽标是常量图形，全树共用四张即可 —— 树里几百个联系人时逐行造位图是
    // 几百次 DIB 分配，纯属浪费。首次用到时才创建（见 GetPresenceBadgeBitmap），
    // 析构时统一 DeleteObject。
    //
    // 与上面三个头像池的区别：头像池随刷新重建，本缓存<u>不随刷新清空</u>——
    // 徽标与具体是谁无关，重建树时继续借用同一批位图即可。
    HBITMAP m_presenceBadges[kPresenceBadgeSlotCount];
};
