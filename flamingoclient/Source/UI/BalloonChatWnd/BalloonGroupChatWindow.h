/**
 *  BalloonGroupChatWindow.h
 *
 *  新版群聊窗口 —— 基于 balloonui 的气泡式群聊窗，对照
 *  design/pc-snapshots/08-chat-group.png。
 *
 *  与单聊窗 CBalloonChatWindow 的关系：群聊窗 = 单聊窗 + 右侧 240px 边栏
 *  （成员 / 群文件 / 公告）。两者大量复用同一批 Chat* 自绘控件
 *  （ChatMessageList / ChatBubble / ChatIconButton / ChatSendButton /
 *  ChatRootPanel / ChatIcons），仅窗口类、布局与边栏不同。
 *
 *  G-P0 范围：骨架 —— 一个能创建 / 显示 / 关闭的空 DuiFrameWindow；真正的
 *  头部 / 消息区 / 输入区 / 边栏由后续 G-P1 阶段的 GroupChatWindowLayout
 *  构建。本阶段用 mock 群数据，真实数据接入留作后续。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方必须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY / DuiNotify
#include "../../App/CustomMsgDef.h"                        // WM_VIEW_CHAT_IMAGE（双击图片打开看图窗）
#include "FontSettingPopup.h"                       // 字体设置浮层（成员需完整类型；含 net::TextFont）
#include "FaceImagePickerPopup.h"                   // Bin/Face 图片表情浮层（成员需完整类型）
#include "GroupFileLibrary.h"                       // 群文件库（边栏「群文件」面板的数据模型，成员需完整类型）
#include "GroupFileUploadTracker.h"                 // 群文件「正在上传」行跟踪器（成员需完整类型）
#include "GroupAtDropdown.h"                        // @ 成员选择浮层（成员需完整类型）
#include "GroupMentionModel.h"                      // MentionCandidate（成员需完整类型）
#include "ChatDropController.h"                     // 拖文件进窗口发送（成员需完整类型）
#include "ChatHistoryPaging.h"                      // HistoryPagingState（向上翻页，成员需完整类型）
#include "../../net/GroupProtocolClient.h"             // net::GroupAnnouncementItem（成员需完整类型）

#include <map>
#include <vector>
#include <string>
#include <stdint.h>

class CFlamingoClient;

//轻量提示条（拖放里"这些没处理"的汇总提示）。只按指针持有，前向声明即可。
namespace balloonwjui { class DuiToast; }

//v2 消息推送 / 群成员 / 历史消息投递载体（解析层结构体，定义在 net/*.h）。
namespace net { struct MessageNotify; struct GroupMembersResult; struct SyncMessagesResult;
                struct GroupFileUploadResult; struct GroupFileDownloadResult;
                struct GroupFileUploadProgress; struct GroupFileDownloadProgress; }

// 气泡消息列表控件（自绘，定义在 ChatMessageList.h；本头只需前置声明）。
namespace balloonchatwnd {
class ChatInputRichEdit; class ChatMessageList; }

/**
 *  CBalloonGroupChatWindow
 *
 *  新版气泡式群聊窗口。继承 balloonwjui::DuiFrameWindow —— 由它提供无
 *  边框自绘窗体、标题栏与 min/max/close 按钮。非模态窗口，消息由进程
 *  主消息循环统一泵送。
 */
class CBalloonGroupChatWindow : public balloonwjui::DuiFrameWindow,
                                public balloonchatwnd::IChatDropSink
{
public:
    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象，本类只引用、不持有；本阶段为 mock
     *                数据，暂不使用，留作后续真实消息接入。
     */
    explicit CBalloonGroupChatWindow(CFlamingoClient* pFMGClient);
    ~CBalloonGroupChatWindow();

    /**
     *  创建并显示群聊窗口。
     *    hMainDlg：主面板的窗口句柄；为 NULL 时关闭本窗口只销毁自身、
     *              不退出程序（从主面板打开时如此）；非 NULL 时（调试
     *              入口）关闭即请求退出程序。
     */
    void CreateAndShow(HWND hMainDlg);

    /**
     *  设置本群会话的群 id。须在 CreateAndShow 之前调用 —— 决定真实收发归属、
     *  以及窗口起始是否预填 mock（绑定了群 id 即真实群，不预填 mock）。
     *    groupId：群 id。
     */
    void SetGroupId(int64_t groupId);

    /**
     *  设置群名（窗口标题）。可在 CreateAndShow 前或显示后调用（群资料晚到时
     *  主面板会再调一次刷新标题）。
     *    name：群名。
     */
    void SetGroupName(LPCTSTR name);

    /**
     *  设置本群的群头像并即时重建标题栏 / 任务栏图标。
     *
     *  可在 CreateAndShow 前或显示后调用：群资料(1303)到达、以及群头像被改后的
     *  群资料变更推送(1318)，主面板都会调一次，故开着窗时换群头像也能立刻反映。
     *  真图未下到本地时先画字形占位并入队下载，下好后经 OnAvatarDownloaded 回刷。
     *
     *    facetype：       头像类型（0 自动 / 1 字形 / 2 自定义图，见 BalloonMain/AvatarSpec.h）。
     *    customfaceUtf8： 头像标识（UTF-8）：字形头像为 "glyph|colorIdx"，自定义图为 fileid。
     */
    void SetGroupAvatar(int facetype, const std::string& customfaceUtf8);

    /**
     *  设置主面板 HWND，发消息后经它通知主面板冒泡该群会话。
     */
    void SetHostPanelHwnd(HWND hostPanel);

    /**
     *  取本群 id，供主面板按群路由窗口。
     */
    int64_t GetGroupId() const { return m_groupId; }

    /**
     *  某条群消息被撤回：把消息区里对应的气泡就地变成居中提示行。
     *  由主面板收到 cmd_message_recalled_notify 推送后路由进来。
     *  @param msgid   被撤消息的服务器 msgid；本窗口没有该条时安静忽略
     *  @param tipText 提示文案（见 balloonchat::BuildRecallTipText）
     */
    void OnMessageRecalled(long long msgid, const CString& tipText);

    /**
     *  某条消息被 Pin / 取消 Pin（收到服务端推送后由主面板调用）：把列表里对应那行的
     *  图钉标记置上 / 清掉。列表里没有该 msgid（该消息尚未加载进当前视图）时安静忽略 ——
     *  本地库那份标记已经落好，下次翻历史渲染出来时自然带着。
     *    msgid： 目标消息的服务器 msgid。
     *    pinned：true 钉住、false 取消。
     */
    void OnMessagePinned(long long msgid, bool pinned);

    /**
     *  设置本群当前被 Pin 的消息条数，并据此刷新头部信息行下方的「已 Pin N 条」提示带
     *  （0 条时整条隐藏、不占位）。与单聊窗 CBalloonChatWindow::SetPinnedCount 同形，
     *  口径说明见那里。
     *    count：被 Pin 的消息条数，负数按 0 处理。
     *  副作用：提示带显隐变化时会补一次客户区布局并投递 kRelayoutMsg 重排。
     */
    void SetPinnedCount(int count);

    /**
     *  在当前记录的 Pin 条数上增减并刷新提示带（收到单条 Pin / 取消 Pin 推送时用）。
     *    delta：增减量，通常为 +1（钉住）或 -1（取消）。结果被夹到不小于 0。
     */
    void AdjustPinnedCount(int delta);

    /**
     *  发送回执到达：把服务器分配的 msgid 与消息时间回填到对应的"刚发出"气泡上。
     *  clientMsgId 全局唯一，主面板向所有聊天窗广播，非本窗口的回执会被安静忽略。
     *  @param clientMsgId 本端发送时生成的客户端消息ID
     *  @param msgid       服务器分配的消息ID
     *  @param timestampMs 服务器消息时间（Unix 毫秒）
     */
    void OnSendChatAckBackfill(const std::string& clientMsgId, long long msgid,
                               long long timestampMs);

    /**
     *  WM_CHAT_RECALL_REQUEST 处理：消息列表里某条气泡右键选了「撤回」。
     *  lParam=堆上 long long*（被撤消息的服务器 msgid），处理后 delete。
     *  只负责把请求连同本窗口的会话身份（群聊 ctype=2、cid=群 id）发给服务端；
     *  界面改动等服务端推回 cmd_message_recalled_notify 后再做。
     */
    LRESULT OnChatRecallRequest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_PIN_REQUEST 处理：消息列表里某条气泡右键选了「Pin / 取消 Pin」。
     *  wParam=目标状态（1 钉住、0 取消），lParam=堆上 long long*（目标消息的服务器
     *  msgid），处理后 delete。只负责把请求连同本窗口的会话身份（群聊 ctype=2、
     *  cid=群 id）发给服务端；标记改动等服务端推回 cmd_message_pinned_notify 后再做。
     */
    LRESULT OnChatPinRequest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_MENTION_ACTION 处理：气泡里某个 @某人 片段的右键菜单被选中了。
     *  lParam=堆上 balloonchatwnd::MentionActionData*（被 @ 成员 uid / 显示名 +
     *  选中的菜单项），处理后 delete。四个动作分别是：打开与该成员的单聊窗、
     *  打开其资料窗（这两项都经主面板路由）、在输入框光标处插入「@显示名 」、
     *  把显示名放进剪贴板。
     */
    LRESULT OnChatMentionAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_IMAGE_ACTION 处理：图片气泡右键选了「图片另存为」/「再次发送」。
     *  lParam=堆上 balloonchatwnd::ChatImageActionData*（图片本地路径 + 另存为默认
     *  文件名 + 选中的菜单项），处理后 delete。另存为按原文件字节整体拷贝（无损、
     *  保持原格式），与单聊窗共用 balloonchatwnd::SaveChatImageAs；再次发送把同一
     *  张图作为<u>一条新消息</u>重新发到本群，走的是与选图发送同一条 SendImageFile
     *  链路（新的 clientMsgId、重新上传），不是重发原来那条。
     */
    LRESULT OnChatImageAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_LOAD_OLDER 处理：消息区滚到接近顶部，判断要不要拉更早的一页历史。
     *  真正的判定在 ShouldLoadOlderHistory（纯逻辑、可单测），本函数只负责把当前
     *  滚动状态喂给它，并在它点头时发请求、置"在途"标志。
     */
    LRESULT OnChatLoadOlder(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_REPLY_REQUEST 处理：气泡右键选了「引用回复」。
     *  lParam=堆上 long long*（被引用消息的服务器 msgid），处理后 delete。
     *  按 msgid 从消息列表取回那条消息，连同发送人显示名组装成引用快照存进
     *  m_pendingQuote —— 下一次发送文字时随 content.quote 一起发出去。
     */
    LRESULT OnChatReplyRequest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_LOCATE_QUOTED 处理：用户点了气泡里的引用块，滚动定位到被引用的原消息。
     *  lParam=堆上 long long*（原消息的服务器 msgid），处理后 delete。
     *  原消息不在已加载范围内（翻不到那么早）时安静忽略 —— 自动回拉历史再定位属于
     *  后续阶段，不在本期范围。
     */
    LRESULT OnChatLocateQuoted(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  ChatMessageList 的"成员是否仍在群里"查询钩子（静态转发到实例）。
     *    userdata：本窗口 this。
     *    uid：     待查的 userid。
     *  @return 仍在本群成员表里返回 true。
     */
    static bool IsMemberInGroupThunk(void* userdata, long long uid);

    /**
     *  ChatMessageList 的"查成员在本群角色"查询钩子（静态转发到实例）。
     *    userdata：本窗口 this。
     *    uid：     待查的 userid。
     *  @return 网络侧角色取值（0成员 1管理员 2群主）；不在群或查不到返回 -1。
     */
    static int MemberRoleThunk(void* userdata, long long uid);

    /**
     *  ChatMessageList / GroupMemberItem 的"该联系人是否被特别关注"查询钩子
     *  （静态转发到实例）。实例内部再经 FMG_MSG_QUERY_STARRED 同步问主面板 ——
     *  关注名单只有主面板那一份。
     *    userdata：本窗口 this。
     *    uid：     待查的 userid。
     *  @return 已关注返回 true；拿不到窗口 / 没有宿主主面板（调试入口）返回 false。
     */
    static bool StarredThunk(void* userdata, long long uid);

    /**
     *  设置本人在本群的角色（0成员 1管理员 2群主，见 group_member_role）。决定
     *  右上角"群管理"齿轮按钮是否显示：仅群主 / 管理员显示。可在 CreateAndShow
     *  前后调用（前调只记录，InitUi 据此设显隐；后调即时刷新齿轮显隐并重排头部）。
     *    role：本人角色。
     */
    void SetMyRole(int role);

    /**
     *  收到本群的一条新消息：渲染为对方气泡（带真实发送人昵称/头像）。由主面板
     *  在收到 cmd_message_notify（ctype==2）、按 cid=群 id 找到本窗口后调用。
     *    notify：消息推送解析结果（本轮仅处理文本）。
     */
    void OnRecvGroupMessage(const net::MessageNotify& notify);

    /**
     *  收到本群成员列表：填右侧成员侧栏，并建"发送人 userid → 昵称/头像色"映射，
     *  供气泡显示真实发送人。由主面板的 OnGroupMembersArrived 路由调用。
     *    members：群成员解析结果。
     */
    void OnGroupMembersArrived(const net::GroupMembersResult& members);

    /**
     *  收到本群文件列表的一页应答：累积进拉取缓冲；若 hasmore 为真则继续拉下一页，
     *  否则把累积结果灌进群文件库并渲染面板（Option B 客户端全量缓存）。失败应答
     *  （code!=0）则收尾本次拉取、用已累积的渲染。由主面板 OnGroupFilesArrived 路由调用。
     *    result：cmd_get_group_files 应答解析结果（本页条目 + hasmore + 回显 offset）。
     */
    void OnGroupFilesArrived(const net::GetGroupFilesResult& result);

    /**
     *  收到本群公告列表应答：填边栏「公告」面板并渲染。
     *  失败应答同样要落下"已加载"标记，否则面板会一直停在"正在加载"那句话上。
     *    result：cmd_get_group_announcements 应答解析结果。
     */
    void OnAnnouncementsArrived(const net::GroupAnnouncementsResult& result);

    /**
     *  收到本群公告变更推送：重拉一次公告列表刷新边栏。
     *
     *  与群管理窗同样选择"重拉"而不是本地增删：撤回一条之后当前停在第几页、
     *  总共几条都要跟着变，本地维护这些增量的分支比重拉一次多得多，而公告是
     *  低频操作，不在热路径上。公告面板没打开过（还没拉过）时什么都不做 ——
     *  等用户真去点那个 Tab 时自然会拉到最新的。
     *    notify：cmd_group_announcement_notify 解析结果。
     */
    void OnAnnouncementChanged(const net::GroupAnnouncementNotify& notify);

    /**
     *  设置头部那行公告摘要（最新一条已发布公告的标题）。
     *  数据来自群资料里的公告快照（announcetitle），随 1303 / 1318 到达。
     *    title：公告标题；空串表示本群没有公告，该行不画。
     */
    void SetAnnouncementTitle(LPCTSTR title);

    /**
     *  设置头部那行的成员数「(N 人)」。
     *    count：成员数；<= 0 时不画这一段。
     */
    void SetHeaderMemberCount(int count);

    /**
     *  把右侧边栏切到「公告」Tab（并在需要时拉一次公告）。
     *  供主面板在用户点新公告提示窗的「查看全部」时调用 —— 那条路径要"打开群聊窗
     *  并直接停在公告上"，而切 Tab 的实现是私有的。
     */
    void ActivateNoticeTab();

    /**
     *  收到本群文件库变更推送：按 changetype 在群文件库里增量增 / 删，再重渲染面板
     *  （仅当库已加载）。由主面板 OnGroupFilesChanged 路由调用。
     *    notify：cmd_group_files_changed_notify 解析结果。
     */
    void OnGroupFilesChanged(const net::GroupFilesChangedNotify& notify);

    /**
     *  群文件上传结果：成功则据 fileid 发 cmd_add_group_file 把文件登记进群文件库
     *  （服务端随后推 1806，本端经 OnGroupFilesChanged Upsert 刷新）；失败记日志。
     *  由主面板 OnGroupFileUploadResult 路由调用。
     *    result：群文件上传结果。
     */
    void OnGroupFileUploaded(const net::GroupFileUploadResult& result);

    /**
     *  群文件下载结果：成功则用系统默认程序打开落地文件；失败记日志。由主面板
     *  OnGroupFileDownloadResult 路由调用。
     *    result：群文件下载结果（成功含落地路径）。
     */
    void OnGroupFileDownloaded(const net::GroupFileDownloadResult& result);

    /**
     *  群文件上传进度：按 uploadId 在文件列表顶部找到那一行「上传中」，就地更新其
     *  百分比与进度条。由主面板 OnGroupFileUploadProgress 路由调用。
     *    prog：上传进度（含 uploadId / 已传 / 总字节）。
     */
    void OnGroupFileUploadProgress(const net::GroupFileUploadProgress& prog);

    /**
     *  群文件下载进度：按 entryid 在文件列表里找到那一行，在行上就地显示下载进度
     *  （进度条 + 百分比）。由主面板 OnGroupFileDownloadProgress 路由调用。
     *    prog：下载进度（含 entryid / 已收 / 总字节）。
     */
    void OnGroupFileDownloadProgress(const net::GroupFileDownloadProgress& prog);

    /**
     *  某真图头像下载完成后由主面板广播给所有打开的群聊窗。本窗口若有成员（或本人）的真图
     *  头像 customface 等于该 fileid，则刷新成员侧栏与气泡里该发送者的头像真图。不匹配则
     *  为 no-op。
     *    fileid 刚下载完成的头像文件ID（UTF-8）。
     */
    void OnAvatarDownloaded(const std::string& fileid);

    /**
     *  渲染本群的历史消息：打开窗口、群成员就绪后拉到的最近若干条。服务端按
     *  "最新在前"返回，本函数倒序遍历成时间正序逐条追加（自己的在右、其余按群成员
     *  资料显示真实发送人在左），末尾滚到底。由主面板的 OnSyncMessagesArrived 路由调用。
     *    result：历史消息解析结果。
     */
    void OnHistoryMessages(const net::SyncMessagesResult& result);

    /**
     *  定位到本群里某条消息（历史消息记录窗"定位"按钮经主面板转发到此）：滚动消息区到
     *  该 msgid 行附近。若该行此刻还未加载（历史在途），记为待定，等历史渲染完再补定位。
     *    msgid：目标消息的服务器 msgid。
     */
    void LocateMessage(long long msgid);

    /**
     *  群发图上传进度回调：更新本群"发送中"图片气泡的进度。由主面板的
     *  OnImageSendProgress 按 clientmsgid 广播到各已打开窗口调用（命中者更新，
     *  其余静默忽略）。
     *    clientMsgId：发送时生成的客户端消息 id；
     *    sent / total：已传字节 / 总字节。
     */
    void OnImageSendProgress(const std::string& clientMsgId, long long sent, long long total);

    /**
     *  群发图结果回调：成功则把图片气泡标记为已发，失败标记为发送失败。由主面板的
     *  OnImageSendResult 在 ctype==2 时按 cid=群 id 路由调用。
     *    ok：是否成功；clientMsgId：对应消息。
     */
    void OnImageSendResult(bool ok, const std::string& clientMsgId,
                           const std::string& fileid = std::string(),
                           const std::string& md5 = std::string());

    /**
     *  收图下载成功回调：把对应 msgid 的"下载中"图片气泡回填为本地原图。由主面板的
     *  OnImageDownloadResult 按 msgid 广播到各已开窗口调用（命中者回填，其余忽略）。
     *    msgid：消息 id；localPath：下载落地的本地原图路径。
     */
    void OnImageDownloaded(long long msgid, const std::wstring& localPath);

    /**
     *  收图下载失败回调：把对应 msgid 的图片气泡标记为下载失败。
     */
    void OnImageDownloadFailed(long long msgid);

    // ---- 文件消息（msgtype=3，与图片镜像）。由主面板按会话 / 全局路由调用 ----
    //更新本端"发送中"文件气泡的上传进度（按 clientMsgId 定位）。
    void OnFileSendProgress(const std::string& clientMsgId, long long sent, long long total);
    //收尾本端"发送中"文件气泡（按 clientMsgId 定位）：成功置完成态；失败标失败；
    //cancelled 为 true（用户点了「取消」）则置「已取消」态而非失败态。
    void OnFileSendResult(bool ok, bool cancelled, const std::string& clientMsgId);
    //更新收到的某条文件的下载进度（按服务器 msgid 定位）。
    void OnFileDownloadProgress(long long msgid, long long received, long long total);
    //收到的某条文件下载完成：按 msgid 填本地落地路径并渲染。
    void OnFileDownloaded(long long msgid, const std::wstring& localPath);
    //收到的某条文件下载失败：按 msgid 标失败；cancelled 为 true 则标「已取消」。
    void OnFileDownloadFailed(long long msgid, bool cancelled);

    BEGIN_MSG_MAP(CBalloonGroupChatWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        // 窗口被切到前台时通知主面板清该群会话未读（handler 置 bHandled=FALSE，
        // 消息继续沿消息映射交给 DuiFrameWindow 链）。
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivateMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(kRelayoutMsg, OnRelayout)
        MESSAGE_HANDLER(kSendOnEnterMsg, OnSendOnEnter)
        MESSAGE_HANDLER(kMentionChangedMsg, OnMentionChanged)
        MESSAGE_HANDLER(kMentionNavMsg, OnMentionNav)
        MESSAGE_HANDLER(kMentionWheelMsg, OnMentionWheel)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDownForMention)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivateForMention)
        MESSAGE_HANDLER(kOpenSendMenuMsg, OnOpenSendMenu)
        MESSAGE_HANDLER(kDoScreenshotMsg, OnDoScreenshot)
        MESSAGE_HANDLER(WM_VIEW_CHAT_IMAGE, OnViewImageMsg)
        MESSAGE_HANDLER(WM_CHAT_IMAGE_READY, OnImageReady)
        MESSAGE_HANDLER(WM_CHAT_OPEN_LINK, OnChatOpenLink)
        MESSAGE_HANDLER(WM_CHAT_COPY_SELECTION, OnChatCopySelection)
        MESSAGE_HANDLER(WM_CHAT_CANCEL_LINK_OPEN, OnChatCancelLinkOpen)
        MESSAGE_HANDLER(WM_CHAT_FOCUS_AREA, OnChatFocusArea)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDownMsg)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDownMsg)
        MESSAGE_HANDLER(WM_GROUP_FILE_DOWNLOAD, OnGroupFileDownloadMsg)
        MESSAGE_HANDLER(WM_GROUP_FILE_UPLOAD_CANCEL, OnGroupFileUploadCancelMsg)
        MESSAGE_HANDLER(WM_GROUP_FILE_DOWNLOAD_CANCEL, OnGroupFileDownloadCancelMsg)
        MESSAGE_HANDLER(WM_GROUP_FILE_DELETE, OnGroupFileDeleteMsg)
        MESSAGE_HANDLER(WM_CHAT_RECALL_REQUEST, OnChatRecallRequest)
        MESSAGE_HANDLER(WM_CHAT_PIN_REQUEST, OnChatPinRequest)
        MESSAGE_HANDLER(WM_CHAT_MENTION_ACTION, OnChatMentionAction)
        MESSAGE_HANDLER(WM_CHAT_IMAGE_ACTION, OnChatImageAction)
        MESSAGE_HANDLER(WM_CHAT_LOAD_OLDER, OnChatLoadOlder)
        MESSAGE_HANDLER(WM_CHAT_REPLY_REQUEST, OnChatReplyRequest)
        MESSAGE_HANDLER(WM_CHAT_LOCATE_QUOTED, OnChatLocateQuoted)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

public:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端，引用不持有

    /**
     *  应用当前连接态：顶部提示条的显隐与文字、输入框是否只读、发送按钮是否可用。
     *  做法与单聊窗 CBalloonChatWindow::ApplyConnectionState 完全一致。
     *    state：界面连接态，取值见 BalloonMain/OnlineCapability.h 的 ConnectionUiState。
     */
    void ApplyConnectionState(int state);

    /**
     *  「自动识别链接」开关变化后，让本窗口的消息行重新测量并重绘。由主面板在收到
     *  FMG_MSG_LINK_OPTION_CHANGED 后逐窗调用（进程级开关须先设好再调本方法）。
     *  窗口尚未初始化完成时为空操作。
     */
    void RefreshLinkRendering();

private:
    /**
     *  发送前的连接态门禁：不可发送时弹出提示并返回 false。
     *    action：操作类别，取值见 BalloonMain/OnlineCapability.h 的 OnlineActionKind。
     *  返回：true 表示可以继续该操作。
     */
    bool CheckOnlineFor(int action);

    /**
     *  请主面板打开本群的历史消息记录窗。头部「消息记录」按钮与「已 Pin N 条」提示带
     *  共用本函数，区别只在预选哪个类型胶囊。
     *    initialTypeFilter：打开后预选的类型胶囊，取 balloonhistory::HistoryTypeFilter
     *                       的值（0=全部，6=Pin）。
     *  副作用：向主面板投递 FMG_MSG_OPEN_HISTORY（群 id 走堆 int64_t*，由主面板释放）。
     *          未绑定群 / 主面板已销毁时什么都不做。
     */
    void RequestOpenHistory(int initialTypeFilter);

    /**
     *  按 m_pinnedCount 刷新「已 Pin N 条」提示带的显隐与文字。SetPinnedCount 与
     *  AdjustPinnedCount 改完计数后统一调它。
     *  副作用：显隐真的变了才补布局（理由见单聊窗同名函数）。
     */
    void RefreshPinBanner();

public:

private:
    /**
     *  WM_CLOSE 处理：销毁本窗口；m_hMainDlg 非空时同时请求
     *  退出程序。
     */
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_ACTIVATE 处理：本群聊窗被切到前台时，向主面板投 FMG_MSG_CONV_MARK_READ，
     *  请它把本群会话的未读清零。
     *
     *  存在理由：收消息那一路只在"消息到达的当下窗口就已在前台"时才算已读；用户
     *  在别处忙、事后才切回这个窗口来看的情形由本处补齐，否则消息看过了红点还亮着。
     *  不消费该消息（bHandled=FALSE），仍交 DuiFrameWindow 链继续处理。
     */
    LRESULT OnActivateMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_VIEW_CHAT_IMAGE：群聊气泡双击图片冒泡上来，lParam 为图片本地路径
     *  （LPCTSTR）。调进程内共享看图窗（CBalloonImageViewerWindow::OpenShared）放大查看。
     */
    LRESULT OnViewImageMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_IMAGE_READY：ChatImageCache 后台把某张图片缩放完成后投递，重绘消息区让
     *  图片气泡再画一次（查缓存命中、换成真图）。wParam / lParam 未用。
     */
    LRESULT OnImageReady(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_OPEN_LINK 处理：气泡里的超链接被点击（或右键选了「在浏览器中打开」）。
     *  lParam 为堆上的 std::wstring*（规范化后的 URL），本函数负责 delete。
     *  打开前先过 balloonchatwnd::IsSafeLinkUrl 的协议白名单 —— 只放行 http / https。
     */
    LRESULT OnChatOpenLink(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_COPY_SELECTION 处理：把消息区里当前选中的那段文字复制到剪贴板
     *  （口径与气泡右键「复制」一致）。
     *  @return 有选区且已复制返回 1；没有选区返回 0。
     */
    LRESULT OnChatCopySelection(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_CANCEL_LINK_OPEN 处理：撤销上一条 WM_CHAT_OPEN_LINK 排队的打开动作
     *  （气泡双击时投来）。与单聊窗同一套做法。
     */
    LRESULT OnChatCancelLinkOpen(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_FOCUS_AREA 处理：记下用户当前在哪一块操作（wParam 见 CustomMsgDef.h
     *  的 CHAT_FOCUS_*）。Ctrl+C 据它路由。与单聊窗同一套做法。
     */
    LRESULT OnChatFocusArea(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_LBUTTONDOWN 旁听：落点在消息区里就把逻辑焦点记成"接收框"。
     *  只旁听、不消费（恒置 bHandled=FALSE）。
     */
    LRESULT OnLButtonDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  真正打开排队中的那个链接（延时定时器到点时调）。打开前过协议白名单。
     */
    void OpenPendingLink();

    /**
     *  WM_KEYDOWN 处理：焦点不在输入框时按 Ctrl+C 同样复制消息区选区。
     *  非 Ctrl+C 一律不消费。与单聊窗同一套做法。
     */
    LRESULT OnKeyDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_DUI_NOTIFY 处理：识别「发送」按钮点击触发发消息、边栏 Tab
     *  点击触发切换。
     */
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_TIMER 处理：mock 自动回复定时器到点后，追加一条群成员回复。
     */
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  构建并装载客户区控件树。须在窗口 Create 之后调用。
     */
    void InitUi();

    /**
     *  发消息：取输入框图文混排内容，按文档顺序拆成"文字 / 内联截图"序列逐段发出
     *  （文字段→群文本消息，内联图→群图片消息）。调试入口（无群 id）退回拼接文字
     *  做 mock 回显 + 自动回复。
     */
    void DoSend();

    /**
     *  发送一段文字到本群（乐观回显气泡 + 网络发文本 + 冒泡会话）。供 DoSend 的
     *  有序发送逐段调用。
     *    text：本段文字（非空）。
     */
    void SendGroupTextPart(const CString& text);

    /**
     *  发送第 imageIndex 个内联截图（文档顺序↔m_pendingShotPaths 插入顺序）：直接发
     *  截图浮层存下的原 PNG 文件。序号越界则记日志跳过。
     */
    void SendGroupImagePart(int imageIndex);

    /**
     *  选图并作为群图片消息发送：弹文件选择对话框选一张图片，交给 SendImageFile 走
     *  "乐观回显 + 上传 + 发消息"链路。点输入工具栏「图片」按钮时调用。仅真实群会话
     *  （m_groupId != 0）下生效。
     */
    void PickAndSendImage();

    /**
     *  把一张图片文件作为群图片消息发出：乐观回显"上传中"气泡 + 交图片传输线程上传
     *  （scene=2、cid=群 id）+ 冒泡会话。截图「发送」与选图发送共用本函数。
     *    path：图片文件全路径（UTF-16）。
     */
    void SendImageFile(const std::wstring& path);

    /**
     *  把收到 / 历史里的一条群图片消息渲染成图片气泡：缓存命中直接显原图，否则插
     *  "下载中"气泡并触发原图下载（结果按 msgid 回投 OnImageDownloaded/Failed）。
     *    list：消息列表控件；fromMe：是否本人所发（决定左右与头像）；notify：消息解析结果。
     */
    void RenderIncomingGroupImage(balloonchatwnd::ChatMessageList* list,
                                  bool fromMe, const net::MessageNotify& notify);

    /**
     *  发起截图：模态弹出进程内全屏截图浮层。复制 / 保存由浮层内部完成；「发送」立即
     *  把截图作为群图片消息发出，「发送到对话框」把截图内联插入输入框（见下两个回调）。
     */
    void DoScreenshot();

    // 截图「发送」回调（C 风格，ctx=本窗口 this）：把临时 PNG 作为群图片立即发出。
    static void OnScreenshotSend(void* ctx, const wchar_t* pngPath);
    // 截图「发送到对话框」回调：把临时 PNG 内联插入输入框，并记入 m_pendingShotPaths。
    static void OnScreenshotStage(void* ctx, const wchar_t* pngPath);

    /**
     *  切换右侧边栏 Tab：把被点的 Tab 设为选中态、其余取消，并切换下部内容容器
     *  （成员 / 群文件）的显隐。切到群文件时按需触发拉取并渲染。
     *    tabId：被点 Tab 的控件 id（kGIdTabMembers / Files / Notice）。
     */
    void SwitchSideTab(UINT tabId);

    /**
     *  按 m_notice / m_noticeIndex / m_noticeTotal 刷新边栏「公告」面板：写标题 /
     *  正文 / 发布人时间、更新「x/y」与翻页按钮可用性；一条都没有时改显示空状态。
     */
    void RenderNoticePanel();

    /**
     *  公告面板的翻页：换下标并向服务端要那一条。
     *    delta：+1 下一条、-1 上一条；越界时就地夹住，不循环 —— 循环翻页在
     *           只有两三条时会让人分不清自己看到第几条了。
     */
    void StepNotice(int delta);

    /**
     *  向服务端要第 index 条公告（offset=index、count=1），同时拿回 total。
     *    index：要看的第几条，0 起；负数按 0 处理。
     */
    void RequestNoticeAt(int index);

    /**
     *  把群文件库当前状态（分类 m_fileCategory / 搜索 m_fileSearch / 页 m_filePage）
     *  渲染进边栏「群文件」面板：取本页条目重建文件行、同步滚动视口内容高度、刷新
     *  分页信息文字。每次分类 / 搜索 / 翻页 / 数据变化后调用。
     */
    void RenderFilePanel();

    /**
     *  刷新右侧边栏「成员」/「群文件」两个 Tab 上的数量文案（"成员 12" / "群文件 8"）。
     *  数量口径一律是全量总数：成员取 m_memberInfo 的规模，群文件取库里的全量条数，
     *  都不随下方的分类 chip / 搜索框筛选而变（筛选后的条数由面板底部"共 N · 第 x/y 页"
     *  那行负责）。数量尚未从服务端取到时（成员映射为空 / m_fileLoaded 为 false）
     *  Tab 只显示光秃秃的标题，以便与"确实是 0"区分。
     *  在成员到达、文件库拉全、文件变更推送之后调用。
     */
    void RefreshSideTabCounts();

    /**
     *  发起「拉全量群文件」：从 offset=0 起逐页拉 cmd_get_group_files，应答经
     *  OnGroupFilesArrived 累积，拉全后灌进 m_fileLibrary 并刷新面板与 Tab 数量。
     *  幂等：已拉过（m_fileLoaded）或正在拉（m_fileLoading）时什么都不做；调试入口
     *  （m_groupId == 0，库已用 mock 预填）同样不做。
     *  窗口一打开就调用一次（Tab 上的文件数量要立刻是真的），首次切到群文件 Tab 时
     *  再调用一次兜底（那时多半已经拉完，于是直接返回）。
     */
    void StartLoadGroupFiles();

    /**
     *  设置分类筛选 chip 的选中态：把 category 对应的 chip 置选中（品牌实心），其余
     *  置未选（白底灰边），并把 m_fileCategory 更新为 category。
     *    category：目标分类，取值见 balloonchatwnd::GroupFileChipCategory。
     */
    void ApplyFileChipSelection(int category);

    /**
     *  WM_GROUP_FILE_DOWNLOAD 处理：群文件行下载箭头被点击，lParam 为堆上 CString*
     *  （entryid）。据 entryid 在群文件库查完整条目，交 FileTransfer 发起群文件下载，
     *  完成后经 OnGroupFileDownloaded 打开文件。处理后 delete lParam。
     */
    LRESULT OnGroupFileDownloadMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_GROUP_FILE_UPLOAD_CANCEL 处理：某「上传中」行的 ✕ 被点击，lParam 为该笔
     *  uploadId（int）。把它从上传跟踪器移除、重渲染面板（行消失），并调
     *  FileTransfer::CancelGroupFileUpload 真正取消传输。
     */
    LRESULT OnGroupFileUploadCancelMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_GROUP_FILE_DOWNLOAD_CANCEL 处理：某「下载中」行的 ✕ 被点击，lParam 为堆上
     *  CString*（entryid）。把它移出「下载中」表、调 FileTransfer::CancelGroupFileDownload
     *  真正取消，并把该行就地恢复成下载箭头。处理后 delete lParam。
     */
    LRESULT OnGroupFileDownloadCancelMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_GROUP_FILE_DELETE 处理：某文件行的垃圾桶被点击，lParam 为堆上 CString*（entryid）。
     *  据 entryid 查库取文件名 + 复检删除权限（群主 / 管理员 / 上传者本人），弹确认框，
     *  确认后发 cmd_delete_group_file；删成功后服务端推 1806(removed)使该行在全群消失。
     *  处理后 delete lParam。
     */
    LRESULT OnGroupFileDeleteMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  「上传文件」按钮点击：弹系统选文件框选一个文件，交 FileTransfer 群文件上传
     *  （scene=群文件）。每点一次发起一笔并发上传（最多 N 笔并发、超出排队），在面板
     *  顶部新增一行「上传中」；上传完成经 OnGroupFileUploaded 发 cmd_add_group_file 登记。
     *  仅真实群（m_groupId != 0）可用；调试入口（mock）无服务器，点击仅记日志。
     *
     *  输入区工具栏那个「文件」图标按钮走的也是本函数（群聊里发文件即上传到群文件库，
     *  不发文件消息气泡），它据返回值决定要不要顺带把边栏切到「群文件」Tab。
     *
     *  @return 确实发起了一笔上传返回 true；用户取消选择、或调试入口无群绑定返回 false。
     */
    bool PickAndUploadGroupFile();

    /**
     *  把一个本地文件作为群文件上传（入队 + 在面板顶部登记一行「上传中」）。
     *  PickAndUploadGroupFile 选完文件后调它，拖放进来的文件也走同一条路，两处
     *  共用同一段登记逻辑，不各写一份。
     *    path：本地文件绝对路径。
     *  @return 确实入队返回 true；调试入口无群绑定、或路径为空返回 false。
     */
    bool UploadGroupFile(const std::wstring& path);

    // ---- balloonchatwnd::IChatDropSink 实现（把文件拖进窗口）----

    /**
     *  IChatDropSink 覆写：落点是否落在发送框（输入区块）内。拖文件只在发送框接收。
     *    ptClient：落点（本窗口客户区坐标）。
     */
    bool IsChatDropZone(const POINT& ptClient) override;

    /**
     *  IChatDropSink 覆写：拖到发送框上 / 离开时开关提示（发送框描边 + 左下角提示文字）。
     *    visible：true 显示、false 收起并还原原提示文案。
     */
    void OnChatDropHintChanged(bool visible) override;

    /**
     *  取「发送框」控件 —— 即输入框所在的那个 vbox。拖放落点判定与提示描边都用它。
     *  @return 发送框控件；界面尚未构建时返回 NULL。
     */
    balloonwjui::DuiVBox* InputBlockCtrl();

    /**
     *  IChatDropSink 覆写：文件被放下。图片作为群图片消息发出；其余文件<u>上传到
     *  群文件库</u>并把边栏切到「群文件」—— 与工具栏「文件」按钮的语义一致，群里
     *  发文件就是入群文件库，不在消息区发文件气泡。
     *
     *  <u>文件夹一律不处理</u>：文件夹传输一期只做单聊，这里拖进来的文件夹只会得到
     *  一句「群聊暂不支持发送文件夹」的提示。归类模块（ChatDropModel）是两个聊天窗
     *  共用的，它只把文件夹拣出来，拦不拦由各窗口自己决定。
     *    paths：      拖进来的路径列表，可以混着文件与文件夹。
     *    forceAsFile：落手时按住了 Shift —— 图片也按群文件上传（不影响文件夹）。
     */
    void OnChatFilesDropped(const std::vector<std::wstring>& paths,
                            bool forceAsFile) override;

    /**
     *  WM_SIZE 处理：窗口尺寸变化后投递 kRelayoutMsg；不消费 WM_SIZE。
     */
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kRelayoutMsg 处理：强制客户区控件树重新布局 —— 修复窗口缩放后
     *  消息区未跟随重排、气泡内容溢出到输入区的问题。
     */
    LRESULT OnRelayout(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kSendOnEnterMsg 处理：群聊输入框按 Enter 触发发送（由输入框控件
     *  ChatInputRichEdit 投递），转调 DoSend()。
     */
    LRESULT OnSendOnEnter(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kMentionChangedMsg 处理：输入框内容 / 光标变了，重算光标前的 @ 前缀，
     *  据此显示、刷新或收起 @ 浮层。
     */
    LRESULT OnMentionChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kMentionNavMsg 处理：浮层显示时接管方向键 / 回车 / Tab / Esc。
     *  @return 非 0 表示本次按键已被浮层消费（钩子据此吞键），0 表示放行给 EDIT。
     */
    LRESULT OnMentionNav(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kMentionWheelMsg 处理：浮层显示时接管鼠标滚轮，滚动候选列表。
     *  wParam 为滚轮增量（zDelta，正数向上）。
     *  @return 非 0 表示这次滚轮已被浮层消费（钩子据此吞掉，输入框自身不滚），
     *          0 表示放行给 EDIT。
     */
    LRESULT OnMentionWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_LBUTTONDOWN：点在输入框之外的 DUI 区域就收起 @ 浮层；点在输入框内部则
     *  保留浮层，改为投递一次 kMentionChangedMsg，让本窗口按点击之后的光标位置重算。
     *
     *  这条路径的前提换过一次。输入框还是内嵌真 Win32 子窗口的时候，点它的鼠标消息
     *  由系统直送该子窗口、不经本窗口，所以本消息到达就等于"点的是别处"，不必判落点。
     *  输入框改成无窗口控件（ChatInputRichEdit 继承自 DuiRichEdit）之后这条前提不再
     *  成立：点输入框的鼠标消息同样会到达本窗口，于是改为在这里自己判落点。
     *
     *  不消费该消息（bHandled 置 FALSE），点击照常交给 DuiHost 派发。
     */
    LRESULT OnLButtonDownForMention(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_ACTIVATE：本窗口失活（切到别的窗口）时收起 @ 浮层 —— 浮层是独立顶层
     *  窗口，不跟着宿主一起隐藏，不收起会一直浮在桌面上。
     */
    LRESULT OnActivateForMention(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  取消息输入框控件（找不到返回 NULL）。@提醒 要直接读它的文本与光标位置、
     *  并对选区设字符格式，故单独收口一个取控件的小函数。
     */
    balloonchatwnd::ChatInputRichEdit* GetInputEdit();

    /**
     *  从输入框当前文本与光标位置解出"光标前正在 @ 的前缀"。
     *    prefixOut：[出参] 前缀（不含 '@'）。
     *    atPosOut： [出参] 那个 '@' 在文本里的字符下标。
     *  @return 光标确实处在一个 @ 片段里返回 true；否则 false（此时应收起浮层）。
     */
    bool ResolveMentionPrefix(CString& prefixOut, int& atPosOut);

    /**
     *  把选中的成员插进输入框：用 `@显示名 ` 替换掉光标前那段 `@前缀`，
     *  给这段设主题橙、并把插入点之后的格式恢复默认，最后记一条候选。
     *    uid：  选中成员 userid。
     *    name： 选中成员显示名（不含 '@'）。
     */
    void InsertMention(int64_t uid, const CString& name);

    /**
     *  把某个成员以 `@显示名 ` 的形式插到输入框<u>当前光标处</u>（有选区则替换选区）。
     *
     *  与 InsertMention 的区别：那个是 @ 浮层选中成员时用的，要把用户已经键入的
     *  `@前缀` 一并替换掉，因此依赖 m_mentionAtPos；本函数供「气泡里 @某人 → 右键
     *  → 再 @ 一次 TA」使用，此时用户根本没在输入框里打过 '@'，没有任何东西要替换。
     *
     *  会在必要时自动补一个前导空格：发送前的重扫只认「位于文本开头、或紧跟空白之后」
     *  的 '@'（见 GroupMentionModel 的 RescanMentions），若光标前一个字符不是空白就
     *  直接插，插出来的 `@张三` 不会被识别成 @提醒，发出去只是一段普通文字。
     *    uid： 成员 userid。
     *    name：成员显示名（不含 '@'）。
     */
    void InsertMentionAtCaret(int64_t uid, const CString& name);

    /**
     *  上面两个插入口的共同实现：把 [begin, end) 这段替换成 `@显示名 `，给名字设主题橙、
     *  把其后的格式恢复默认、光标停在尾随空格之后，并记一条待发候选。
     *    uid / name：成员 userid 与显示名。
     *    begin / end：要被替换掉的字符区间（输入框内的字符下标）；两者相等表示纯插入。
     */
    void InsertMentionInRange(int64_t uid, const CString& name, int begin, int end);

    // @ 浮层选中成员的回调跳板（浮层是 C 风格回调，这里转回成员函数）。
    static void OnAtPickThunk(void* userdata, int64_t uid, const CString& name);

    /**
     *  kOpenSendMenuMsg 处理：发送按钮「▼」被点后弹出「Enter 发送 / Ctrl+Enter
     *  发送」切换菜单；选中后切换全局发送模式并刷新提示与 placeholder。
     */
    LRESULT OnOpenSendMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kDoScreenshotMsg 处理：在 DUI 鼠标事件之外发起截图（DoScreenshot），避免模态
     *  截图浮层在鼠标事件派发栈内运行导致的捕获 / 重入问题。
     */
    LRESULT OnDoScreenshot(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  按当前全局发送模式刷新输入区左下角提示文字与输入框 placeholder。
     */
    void RefreshSendModeUi();

    /**
     *  按 m_myRole 设置右上角"群管理"齿轮按钮(kGIdHdrAdmin)的显隐：仅群主 /
     *  管理员显示，普通成员隐藏。窗口已就绪时一并重排头部并重绘。
     */
    void ApplyAdminGearVisibility();

    /**
     *  打开字体设置浮层 —— 点击工具栏「AF」按钮时调用。锚定在按钮上方，面板内
     *  任意设置变化即经 OnFontChanged 回调更新当前字体（影响后续发送 + 输入框
     *  所见即所得）。
     */
    void OpenFontPanel();

    // 字体设置变化回调（C 风格函数指针，userdata 为本窗口 this）：更新当前字体、
    // 应用到输入框、写回 flamingo.ini。
    static void OnFontChanged(void* userdata, const net::TextFont& font);

    /**
     *  打开表情选择面板 —— 点击工具栏"表情"按钮时调用。锚定在表情按钮上方，
     *  弹出 Bin/Face 图片表情浮层，选中某表情经回调以"带 faceId 标记的内联图"
     *  插入输入框（与单聊窗同构）。
     */
    void OpenEmojiPicker();

    /**
     *  把一个系统表情以"带 faceId 标记的内联图"插入输入框当前光标处（所见即所得）。
     *  供图片表情浮层选中回调使用。
     *    faceId：  系统表情 id（Face/FaceConfig.xml 的 id）。
     *    facePath：该表情 PNG 的绝对路径。
     */
    void InsertFaceToInput(int faceId, const wchar_t* facePath);

    // 图片表情面板"选中"回调（C 风格函数指针，userdata 为本窗口 this）。
    static void OnFacePicked(void* userdata, int faceId, const wchar_t* facePath);

private:
    // 以下七条都是本窗口投给自己的私有消息，取 WM_APP 段而不是公共的 WM_USER 段。
    //
    // 理由：WM_USER 段被 UserSessionData.h / CustomMsgDef.h 那两批跨窗口消息占着，
    // 号是手工分配的，群聊窗在里面另起一段只会增加撞号面。这几条既不经
    // CFlamingoClient::ProxyWndProc 中转、也不受 FMG_MSG_FIRST / FMG_MSG_LAST 闸门
    // 约束，与那两批消息没有任何交集，放进 WM_APP 段更贴合它们的实际作用域。
    //
    // 取值段说明：本窗口用 WM_APP + 0x50 起，单聊窗（BalloonChatWindow）用
    // WM_APP + 0x40 起，两边刻意错开。窗口私有消息即便同号也互不干扰（各发各的窗口），
    // 错开只是为了排查时一眼能看出某个号属于哪个窗口。仓库里其它窗口已占用的
    // WM_APP 偏移是 1~3、0x21、0x22、0x31、0x32、0x137~0x13A，均已避开。
    // 号段规约见开发约定文档「UI 约定」第 6 条，重号由
    // Source\Tests\MessageIdRegistryTests.cpp 的守卫用例把关。

    // 自定义消息：窗口缩放后投递，触发客户区重新布局。
    static const UINT kRelayoutMsg = WM_APP + 0x50;

    // 自定义消息：群聊输入框按 Enter 时投递，触发发送（Ctrl+Enter 则在框内换行）。
    static const UINT kSendOnEnterMsg = WM_APP + 0x51;

    // 自定义消息：发送按钮「▼」被点时投递，弹出发送模式切换菜单。
    static const UINT kOpenSendMenuMsg = WM_APP + 0x52;

    // 自定义消息：截图按钮被点时投递，延后到 DUI 鼠标事件之外再发起模态截图浮层，避免
    // 捕获 / 重入问题导致截图后发送按钮等点击失灵（与发送▼菜单同理）。
    static const UINT kDoScreenshotMsg = WM_APP + 0x53;

    // 自定义消息：输入框内容 / 光标可能变化时由 @提醒 钩子<u>投递</u>，
    // 本窗口据此重算"光标前是不是正在 @ 某个前缀"，显示 / 更新 / 收起浮层。
    static const UINT kMentionChangedMsg = WM_APP + 0x54;

    // 自定义消息：方向键 / 回车 / Tab / Esc 时由 @提醒 钩子<u>同步发送</u>，
    // wParam 为虚拟键码；本窗口返回非 0 表示"浮层消费了这次按键"，钩子据此吞键。
    static const UINT kMentionNavMsg = WM_APP + 0x55;

    // 自定义消息：鼠标滚轮时由 @提醒 钩子<u>同步发送</u>，wParam 为滚轮增量
    // （zDelta）；本窗口返回非 0 表示"浮层消费了这次滚轮"，钩子据此吞掉、
    // 不让输入框自己跟着滚。
    static const UINT kMentionWheelMsg = WM_APP + 0x56;

    HWND    m_hMainDlg;     // 主面板窗口句柄（为 NULL 时关闭不退出程序）
    int     m_replyIndex;   // mock 自动回复轮转下标（仅无群 id 的调试入口下用）
    // 当前连接态（ConnectionUiState：0=在线 1=正在连接 2=离线）。由 ApplyConnectionState
    // 写入，发送路径读它做门禁。默认按"在线"起步 —— 窗口能被打开说明此刻本就在线。
    int     m_connUiState;

    int64_t m_groupId;      // 本群 id；0 表示未绑定（调试入口，走 mock）
    int     m_myRole;       // 本人在本群角色（0成员 1管理员 2群主）；决定"群管理"齿轮显隐，默认成员
    CString m_groupName;    // 群名（窗口标题）
    HWND    m_hostPanel;    // 主面板 HWND，发消息后经它通知主面板冒泡群会话；0 表示调试入口
    long long m_pendingLocateMsgid;   // 待定定位的目标 msgid（历史在途时暂存，渲染后补定位）；0=无
    // 本群当前被 Pin 的消息条数，驱动头部下方那条「已 Pin N 条」提示带的显隐与文字。
    // 开窗时为 0（提示带隐藏），随后服务端返回的被 Pin 消息全集经 SetPinnedCount 校准；
    // 之后每收到一条 Pin / 取消 Pin 推送由 AdjustPinnedCount 就地 ±1。窗口级、不落盘。
    int     m_pinnedCount;

    // 本会话历史的向上翻页状态（最早已加载 msgid / 还有没有更早的 / 是否在途）。
    // 判定逻辑在 ChatHistoryPaging（纯逻辑、可单测），本窗口只持有状态并按它的
    // 结论发请求。m_loading 同时用来区分应答是首屏还是翻页 —— 服务端的应答里
    // 不回显 before，客户端只能自己记。
    balloonchatwnd::HistoryPagingState m_paging;

    // 待发的引用快照：用户点了「引用回复」后暂存在这里，下一次发送文字时随消息一起
    // 发出并清空。m_valid 为假表示当前没有待发引用（正常状态）。
    net::ReplyQuote m_pendingQuote;

    /**
     *  按 m_pendingQuote 刷新输入框上方的引用条：有待发引用就显示成
     *  「回复 张三：你好」，没有就收起。设置 / 清空 m_pendingQuote 之后都要调一次。
     */
    void RefreshReplyBar();

    /**
     *  取消当前的引用（点引用条上的 ✕、或发送完成后）。清空 m_pendingQuote 并收起引用条。
     */
    void CancelPendingReply();

    /**
     *  按当前 m_groupId / m_groupName / 群头像两项重建标题栏图标与任务栏 / Alt-Tab
     *  图标：真图命中本地缓存就画圆形真图，否则画字形 monogram（真图解码失败也
     *  回退字形）。先释放旧位图 / 图标再重建。调试入口（m_groupId==0）下不设。
     *  与单聊窗的 RebuildPeerIcons 同一套做法。
     */
    void RebuildGroupIcons();

    HBITMAP m_titleIconBmp; // 标题栏图标用的群头像位图（真图或 monogram），本类持有，析构释放
    HICON   m_hTaskIcon;    // 任务栏/Alt-Tab 图标（由上述位图转换），本类持有，析构销毁

    int         m_groupFacetype;   // 群头像类型（0 自动 / 1 字形 / 2 自定义图）
    std::string m_groupCustomface; // 群头像标识（UTF-8）：字形为 "glyph|colorIdx"，自定义图为 fileid

    // 群成员显示信息（userid → 昵称 + 头像底色），OnGroupMembersArrived 填充，
    // 渲染收到的群消息气泡时据发送方 userid 取真实昵称/头像。
    struct MemberInfo
    {
        CString     m_name;        // 显示名（群名片优先→账号昵称→"用户<id>"）
        CString     m_glyph;       // 头像字形（按成员 (facetype,customface) 经 DecodeAvatar 解出；字形头像显示其选定值，否则首字）
        COLORREF    m_color;       // 头像底色
        int         m_facetype;    // 头像类型（0 自动 / 1 字形 / 2 真图），见 AvatarSpec.h
        std::string m_customface;  // 头像标识（UTF-8）：字形="glyph|colorIdx"，真图=fileid
        CString     m_imagePath;   // 真图头像本地路径（空=画字形，非空=画真图）
        int         m_role;        // 该成员在本群的角色（0成员 1管理员 2群主，见 group_member_role）；-1=未知
        MemberInfo() : m_color(0), m_facetype(0), m_role(-1) {}
    };
    std::map<int64_t, MemberInfo> m_memberInfo;

    // 用户在输入框里插入过的 @ 记录（按插入顺序）。发送时<u>不</u>直接用它，而是以
    // 输入框最终文本为准调 RescanMentions 重扫一遍 —— 插入之后用户还可能把
    // "@张三" 删掉一半或在中间插字，插入时记的偏移就不准了。扫不到的记录自动作废。
    // 每次 DoSend 全部发完后清空。见 docs/群聊@提醒方案.md §6。
    std::vector<balloonchatwnd::MentionCandidate> m_pendingMentions;

    // 「@ 成员选择」浮层。惰性创建、跨多次 @ 复用。
    balloonchatwnd::CGroupAtDropdown m_atDropdown;

    // 当前正在编辑的那个 @ 片段里，'@' 在输入框文本中的字符下标；-1 表示当前
    // 不在 @ 状态。插入成员时要用它定位"从哪儿开始替换"。
    int m_mentionAtPos;

    // 截图「发送到对话框」插入输入框的内联截图，按插入顺序记下的源 PNG 路径。
    // 发送时第 k 个内嵌图即取本表第 k 个源文件直接发（不抽 OLE，详见 OnScreenshotStage）。
    // 每次 DoSend 全部发完后清空。
    std::vector<std::wstring> m_pendingShotPaths;

    // 当前聊天字体（影响后续发送的群消息与输入框外观）。InitUi 时从 flamingo.ini
    // 载入，面板改动后写回；m_valid 为 false 表示尚未设过、消息不带字体。
    net::TextFont m_currentFont;
    // 输入框（ChatInputRichEdit）当前生效的自定义字体句柄。富文本控件的
    // SetDefaultFontFromHFONT 不接管 HFONT 所有权，故由本窗口持有：每次应用新字体
    // 前先 DeleteObject 上一只再换新，析构时释放，避免 GDI 句柄泄漏。NULL=用默认字体。
    HFONT m_hInputFont;
    // 字体设置浮层（按需打开，本类持有）。
    CFontSettingPopup m_fontPopup;
    // Bin/Face 图片表情浮层（"表情"按钮弹出，本类持有）。
    CFaceImagePickerPopup m_facePopup;

    // ---- 边栏「群文件」面板状态 ----
    // 群文件库数据模型（拉全量后缓存）。分类 / 搜索 / 翻页都对它做纯查询。
    balloonchatwnd::GroupFileLibrary m_fileLibrary;
    // 当前分类筛选 chip（取值见 GroupFileChipCategory），默认「全部」。
    int m_fileCategory;
    // 当前文件名搜索关键字（UTF-8；空=不按名筛）。搜索框每次变化时更新。
    std::string m_fileSearch;
    // 当前页码（0-based）。分类 / 搜索变化时重置为 0；翻页按钮增减。
    int m_filePage;
    // 群文件库是否已拉取（懒加载标志）：首次切到群文件 Tab 时拉一次；mock 调试入口
    // 在 InitUi 即置 true。避免每次切 Tab 重复拉。
    bool m_fileLoaded;
    // 一次「拉全量」过程中是否正在拉取（防重入：拉取期间再切 Tab 不重复发起）。
    bool m_fileLoading;
    // 拉全量的分页累积缓冲：逐页应答把本页条目追加进来，拉到 !hasmore 后整批灌进库。
    std::vector<net::GroupFileEntry> m_fileLoadBuffer;

    // 正在下载的群文件：entryid → 进度百分比(0~100)。RenderFilePanel 据它给重建出来的
    // 文件行恢复"下载中"态（否则换页 / 推送触发重渲染会丢进度态）；下载结束时移除。
    std::map<std::string, int> m_downloadingFiles;
    // 正在上传的群文件跟踪器（可多笔并发）：每笔一行钉在文件列表顶部显示「上传中 NN%」+ ✕。
    // RenderFilePanel 据它在库文件行之前补出这些行；上传结束 / 取消时移除对应笔。
    balloonchatwnd::GroupFileUploadTracker m_uploadTracker;
    // 用户当前在哪一块操作（取 CustomMsgDef.h 的 CHAT_FOCUS_*）。Ctrl+C 据它路由。
    // 窗口刚打开时焦点在输入框，故初值为发送框。
    int m_focusArea;
    // 排队待打开的链接 URL：点了链接先记在这里、等一个双击间隔再真打开（见
    // kLinkOpenTimerId 的注释）；期间收到双击就清空。空串表示当前没有排队的链接。
    std::wstring m_pendingLinkUrl;
    // 拖文件进窗口的接收器。InitUi 末尾绑定到本窗口与输入框，OnCloseMsg 解绑。
    balloonchatwnd::ChatDropController m_dropCtrl;
    // 轻量提示条，汇总"哪些拖进来的东西没处理"。所有权在控件树（客户区根容器的
    // 最后一个子），本指针只作访问用；窗口未初始化完成时为 NULL。
    balloonwjui::DuiToast* m_dropToast;

    // ---- 公告边栏 ----
    //
    // 边栏一次只显示一条公告，所以这里也<u>只留那一条</u>：翻页就按新的下标向服务端
    // 要一条（offset=下标、count=1）。之所以不像群文件那样把整份缓存在本地：公告
    // 可能有几百上千条，全拉回来既慢又占内存，而边栏这种"一次看一条"的浏览方式
    // 本来就不需要手里攥着全部。代价是每翻一页一个来回，但公告是低频浏览。

    // 当前显示的这一条；m_noticeTotal 为 0 时其内容无意义。
    net::GroupAnnouncementItem m_notice;
    // 当前看的是第几条（0 起）。翻页按钮增减，应答回来后夹回合法范围。
    int m_noticeIndex;
    // 本群公告总条数（服务端给的 total）。「x/y」的分母、翻页按钮的可用性都用它。
    int m_noticeTotal;
    // 是否已拉取过（懒加载标志）：首次切到公告 Tab 时拉一次，之后靠翻页与 1326 推送更新。
    bool m_noticeLoaded;
    // 是否正在拉取（防重入：拉取期间再点翻页不重复发起）。
    bool m_noticeLoading;
    // 本边栏最近一次拉公告时发出去的 offset。1323 应答由主面板同时投给本窗口与
    // 群管理窗，而两边的页大小不同（这里一次 1 条、那边每页 8 条），靠它认领
    // 属于自己的那一份；-1 表示还没发过请求。
    int m_noticeReqOffset;
};
