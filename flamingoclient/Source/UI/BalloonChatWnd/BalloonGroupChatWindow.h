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
#include "FaceImagePickerPopup.h"                   // 程序目录/Face 图片表情浮层（成员需完整类型）
#include "GroupFileLibrary.h"                       // 群文件库（边栏「群文件」面板的数据模型，成员需完整类型）
#include "GroupFileUploadTracker.h"                 // 群文件「正在上传」行跟踪器（成员需完整类型）
#include "GroupFileChatPoster.h"                    // 发送录像等：上传群文件后在群里发文件消息（2026-10-05）
#include "GroupAtDropdown.h"                        // @ 成员选择浮层（成员需完整类型）
#include "GroupMentionModel.h"                      // MentionCandidate（成员需完整类型）
#include "ChatDropController.h"                     // 拖文件进窗口发送（成员需完整类型）
#include "ChatHistoryPaging.h"                      // HistoryPagingState（向上翻页，成员需完整类型）
#include "ChatMarkdown.h"                           // MarkdownEarlySendTracker（成员需完整类型）
#include "../../net/GroupProtocolClient.h"             // net::GroupAnnouncementItem（成员需完整类型）
#include "../../Common/Util/PerfStopWatch.h"           // 打开窗口到首屏历史渲染完成的计时（成员需完整类型）
#include "GroupSenderRefresh.h"                        // GroupSenderRender（发送者显示信息的解析与刷新）
#include "GroupMemberPanelKit.h"                       // GroupMemberRowInfo（成员显示信息，成员需完整类型）与成员列表的共用步骤
#include "ChatMessageList.h"                           // WM_CHAT_DELETE_REQUEST（消息映射里要用到消息号）
#include "ChatRelayCard.h"                             // IRelayNameResolver（接龙卡片名字解析的嵌套实现类）
#include "ChatSendBumpQueue.h"                         // 发送后待冒泡的队列（成员需完整类型）
#include "../BalloonCard/RelayComposeDlg.h"            // 「发起接龙」窗口（成员需完整类型）
#include "../BalloonGroupManage/BalloonAnnouncePopupDlg.h"   // 点群公告卡片打开的公告详情窗（成员需完整类型）
#include "GroupQaEditDialog.h"                         // 「新建问答」「修改问答」对话框（成员需完整类型）
#include "BotCardWindow.h"                             // 机器人资料卡小窗（成员需完整类型）
#include "../../net/GroupQaProtocolClient.h"           // net::GroupQaItem / GroupQaResult（成员需完整类型）
#include "../../net/BotProtocolClient.h"              // net::GroupBotInfo / GroupBotsResult（机器人列表成员需完整类型）
#include "../../Session/UserSessionData.h"             // FMG_MSG_MEMO_CHANGED（消息映射里要用到消息号）
#include "../../net/ContactsProtocolClient.h"          // net::ContactItem（选择备忘提醒对象的候选人）
#include <memory>

#include <map>
#include <vector>
#include <string>
#include <stdint.h>
#include "VoiceRecordSession.h"   // 录音会话与录音条（语音消息第 3b、6g 步，成员需完整类型）
#include "../BalloonCode/CodeComposeWindow.h"   // 「发送代码」窗口与它交回结果的消息
#include "ChatCodeMessage.h"                    // kMsgChatCodePaste（消息映射里要用到消息号）

class CCreateGroupDlg;     // 选人对话框（群通话选人用，定义在 BalloonMain/CreateGroupDlg.h）；成员以 unique_ptr 持有，析构在 .cpp 里
class GroupCallPickSink;   // 群通话选人结果的接收方，定义在 BalloonGroupChatWindow.cpp

class CFlamingoClient;

//轻量提示条（拖放里"这些没处理"的汇总提示）。只按指针持有，前向声明即可。
namespace balloonwjui { class DuiToast; }

//v2 消息推送 / 群成员 / 历史消息投递载体（解析层结构体，定义在 net/*.h）。
namespace net { struct MessageNotify; struct GroupMembersResult; struct SyncMessagesResult;
                struct GroupFileUploadResult; struct GroupFileDownloadResult;
                struct GroupFileUploadProgress; struct GroupFileDownloadProgress; }

// 输入框控件的前置声明。消息列表控件 ChatMessageList 的完整定义已随上面的 ChatMessageList.h
// 包含进来（消息映射要用它定义的 WM_CHAT_DELETE_REQUEST），这里的前置声明保留、不影响编译。
namespace balloonchatwnd {
class ChatInputRichEdit; class ChatMessageList; struct VoicePlayedQuery; struct ChatFileActionData;
class GroupMemberItem; }   // 成员侧栏的一行，完整定义经 GroupMemberPanelKit.h 包含

/**
 *  CBalloonGroupChatWindow
 *
 *  新版气泡式群聊窗口。继承 balloonwjui::DuiFrameWindow —— 由它提供无
 *  边框自绘窗体、标题栏与 min/max/close 按钮。非模态窗口，消息由进程
 *  主消息循环统一泵送。
 */
class CBalloonGroupChatWindow : public balloonwjui::DuiFrameWindow,
                                public balloonchatwnd::IChatDropSink,
                                public balloonvoice::IVoicePlayListener,
                                public balloonchatwnd::IVoiceRecordSessionHost
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
     *  请本窗口发起一次截图（主面板收到截图全局快捷键、且前台正是本窗口时调用）。
     *  只投递 kDoScreenshotMsg，等当前消息处理完再由本窗口发起，效果与点工具栏剪刀相同。
     *  @param fullScreen true=选区直接为鼠标所在的整块显示器（截取全屏）
     */
    void RequestScreenshot(bool fullScreen);

    /**
     *  某个共享的状态变了（主面板收到文件共享宿主的通知后逐窗转来）：把本群里这个共享的卡片就地重填。
     *    shareId：共享 id；state / reason：新状态与原因（share_state 及其原因）。
     *  @return 重填了的卡片张数
     */
    int OnShareCardStateChanged(long long shareId, int state, int reason);

    /**
     *  设置头部文件共享按钮上的角标（主面板收到本群可用共享的个数后转来，2026-10-03 起）。
     *    count：本群里可用的共享个数；0 时角标消失。
     */
    void SetShareBadgeCount(int count);

    // 头部文件共享按钮上角标的数字（单元测试用）；没有角标或找不到按钮时为 0
    int GetShareBadgeCount();

    /**
     *  本群的屏幕共享状态变了（主面板收到 FlamingoMedia.dll 的回报后转来）：本群里的屏幕共享卡片按屏幕共享中心
     *  记录的最新状态就地重填（正在共享、已结束）。
     *  @return 重填了的卡片张数
     */
    int OnScreenShareStatusChanged();

    /**
     *  发起人在共享中改了标题与描述（第 15 期，cmd_screenshare_info_notify，由主面板改写本地库之后调用）：
     *  列表里那张共享卡片换成新的标题与描述并重画。卡片尚未加载进当前视图时安静忽略，下次从服务端取回的已是新内容。
     *  @param msgid       那张卡片的服务器 msgid
     *  @param contentJson 改写后的卡片 content 原文
     *  @return 列表里找到并刷新了这张卡片返回 true
     */
    bool OnScreenShareInfoChanged(int64_t msgid, const std::string& contentJson);

    /**
     *  把一个本地文件传到本群的群文件（录屏完成后的「发送给 某某」，主面板转来；与工具栏「文件」按钮同一条路），
     *  上传成功后再在群里发一条文件消息（2026-10-05 起，见 SendUploadedFileMessage），群成员在聊天里就能看到。
     *  离线时提示并放弃。
     *  @param path 文件完整路径
     */
    void SendLocalFile(const std::wstring& path);

    /**
     *  把一张本地图片按群图片消息发出（2026-10-07：屏幕共享标注「保存为图片」后的「发送到本会话」，主面板转来）。
     *  与选图、截图发送走同一条 SendImageFile 链路。
     *  @param path 图片完整路径
     */
    void SendLocalImage(const std::wstring& path);

    /**
     *  以普通文字消息发到本群（屏幕共享第 25 期：主持人「发送邀请到…」把分享链接发到选中的会话）。
     *  与输入框里点「发送」一样有乐观气泡、回执与会话冒泡，但不带正在引用的消息与 @ 提醒，也不清掉输入框与引用条。
     *  @param text 要发的文字；为空时什么也不做
     */
    void SendPlainText(const std::wstring& text);

    /**
     *  发送一条快捷回复（发送按钮 ▾ 菜单里点了它，docs/快捷回复方案.md §3.3）：先做与发送键相同的在线检查，
     *  再按输入框的规则把原文编码成发送正文，经 SendPlainText 发出。输入框、待发引用与 @ 都不动。
     *    reply：快捷回复原文；为空时什么都不做。
     */
    void SendQuickReply(const CString& reply);

    /**
     *  把 Markdown 编辑器里的一篇文档作为 content.format = "markdown" 的文字消息发到本群（主面板转来，
     *  docs/Markdown消息方案.md 第 11.3 节）：生成卡片形式的乐观气泡、登记回执后的会话冒泡；不带 @。
     *  体积与在线状态由编辑器先检查。
     *  @param sourceUtf8 源文本（UTF-8，换行为 \n），非空
     *  @return 已发出返回 true；窗口未绑定群或源文本为空时返回 false
     */
    bool SendMarkdownDocument(const std::string& sourceUtf8);

    /**
     *  按屏幕共享中心记录的本群状态刷新头部下方的屏幕共享横幅：有进行中的共享就显示「某某 正在共享屏幕，点击加入 ›」
     *  （本人在共享时写「你正在共享屏幕」），没有就收起；显隐变化后重排。开窗时与状态变化时调用。
     */
    void RefreshScreenShareBanner();

    /**
     *  本群的群通话状态变了（主面板收到 FlamingoMedia.dll 的回报后转来，docs/群音视频通话方案.md 6.3）：刷新群通话横幅，
     *  本群里的群通话卡片按群通话中心记录的最新状态就地重填（进行中的人数、已结束）。
     *  @return 重填了的卡片张数
     */
    int OnGroupCallStatusChanged();

    /**
     *  群通话结束时服务端改写了通话卡片（由主面板改写本地库之后调用）：列表里那张卡片按新内容重填（显示时长、去掉按钮）。
     *  卡片尚未加载进当前视图时安静忽略，下次从本地库或服务端取回的已是新内容。
     *  @param msgid       那张卡片的服务器 msgid
     *  @param contentJson 改写后的卡片 content 原文
     *  @return 列表里找到并刷新了这张卡片返回 true
     */
    bool OnGroupCallCardChanged(int64_t msgid, const std::string& contentJson);

    /**
     *  按群通话中心记录的本群状态刷新头部下方的群通话横幅：有进行中的群通话就显示「群语音通话进行中 · N 人，点击加入 ›」，
     *  没有就收起；显隐变化后重排。开窗时与状态变化时调用。
     */
    void RefreshGroupCallBanner();

    /**
     *  群通话窗的「邀请成员」（FlamingoMedia.dll 经主面板请求）：以本群成员去掉 exclude 里的人为候选弹出选人框，
     *  确认后经群通话中心追加邀请。成员列表还没加载（主面板刚为此打开本窗口）时先记下请求，成员表到达后再弹出。
     *  @param exclude 已在通话里的人（已加入或振铃中）
     */
    void OpenGroupCallInvitePicker(const std::vector<int64_t>& exclude);

    /**
     *  某个成员在本群里的角色（群通话窗据此决定显不显示「成员管理」）。
     *  @return 0 成员、1 管理员、2 群主；成员资料还没加载或不是成员时为 -1
     */
    int GetMemberRole(int64_t userId) const;

    /**
     *  在窗口顶部居中弹出一条轻量提示，几秒后自动消失（与拖放文件的跳过提示共用同一个
     *  提示条）。主面板用它提示「已转发」这类不需要用户确认的结果。
     *    text：提示文字，调用期间有效。
     */
    void ShowToast(LPCTSTR text);

    /**
     *  某条群消息被撤回：把消息区里对应的气泡就地变成居中提示行。
     *  由主面板收到 cmd_message_recalled_notify 推送后路由进来。撤回者是本人、被撤的是本人发出的文字类消息时，
     *  提示行在 5 分钟内带「编辑」（docs/撤回后重新编辑方案.md），并启动到期定时器。
     *  @param msgid        被撤消息的服务器 msgid；本窗口没有该条时安静忽略
     *  @param tipText      提示文案（见 balloonchat::BuildRecallTipText）
     *  @param recalledByMe 撤回者是否本人（群主、管理员撤回别人的消息时为 false 或撤回者不是消息发送人，都不给「编辑」）
     *  @param nowMs        撤回推送到达的时间（Unix 毫秒）
     *  @return 生成了「编辑」时返回快照 JSON，由主面板存进本机库；否则返回空串
     */
    std::string OnMessageRecalled(long long msgid, const CString& tipText, bool recalledByMe, long long nowMs);

    /**
     *  系统设置里改了「发送消息快捷键」（BUG-119）：按新的发送键刷新左下角提示与输入框占位文字。
     */
    void OnSendModeChanged();

    /**
     *  移除本群里已被本人删除的消息行。收到 cmd_messages_deleted_notify（单条形式）后由
     *  主面板调用；列表里没有的 msgid（尚未加载进当前视图）安静忽略。
     *    msgids：被删除的服务器 msgid
     */
    void OnMessagesDeleted(const std::vector<long long>& msgids);

    /**
     *  丢弃当前列表，重新拉取最新一页历史。按时间范围清理、或本人清空了本会话的聊天记录之后
     *  由主面板调用：被删的消息可能是列表里任意多行，逐行移除不如整页重载 —— 效果等同于重新
     *  打开本窗口。
     *  应答照常经 OnHistoryMessages 按"首屏"处理（先清空再渲染）。调试入口（没有群）
     *  下什么都不做。
     */
    void ReloadLatestMessages();

    /**
     *  本窗口的消息列表里有没有 msgids 中的任何一条。按改写序号同步之后，主面板据此决定要不要
     *  重新加载最新一页：被改写的消息不在当前视图里时不打乱用户的滚动位置。
     *  @param msgids 消息 msgid
     *  @return 至少有一条正在列表里返回 true；列表未就绪返回 false
     */
    bool IsShowingAnyMessage(const std::vector<int64_t>& msgids);

    /**
     *  某条消息被 Pin / 取消 Pin（收到服务端推送后由主面板调用）：把列表里对应那行的
     *  图钉标记置上 / 清掉。列表里没有该 msgid（该消息尚未加载进当前视图）时安静忽略 ——
     *  本地库那份标记已经落好，下次翻历史渲染出来时自然带着。
     *    msgid： 目标消息的服务器 msgid。
     *    pinned：true 钉住、false 取消。
     */
    void OnMessagePinned(long long msgid, bool pinned);

    /**
     *  收到本群一张接龙卡片的变更推送（cmd_relay_changed_notify，由主面板合并进本地库之后调用）：
     *  把新名单与状态合并进列表里那张卡片并重画。卡片尚未加载进当前视图、或已被撤回变成了提示行
     *  时安静忽略 —— 本地库那份已经更新，下次翻到它时自然是最新内容。
     *    notify：已解析的推送；调用期间有效，本方法不保存它。
     *  @return 列表里找到并刷新了这张卡片返回 true。
     */
    bool OnRelayChanged(const net::RelayChangedNotify& notify);

    /**
     *  取某人在本群接龙卡片与详情窗里显示的名字：群名片 → 通讯录 → 「用户<id>」，与气泡上的
     *  发送人名同一套（ResolveSender）。主面板给详情窗解析名字时调用。
     *    userid：用户 userid。
     *  @return 显示名，保证非空
     */
    CString ResolveRelayMemberName(int64_t userid) const;

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
    /**
     *  发送回执失败（cmd_send_message 应答 code 非 0）：若是本窗口「发起接龙」窗口发出的那张卡片，
     *  交给它在窗口里提示原因并返回 true；否则按 clientMsgId 找到那条「刚发出」的气泡，标为发送失败
     *  并在下方写原因（SendFailureText），找到时同样返回 true。主面板据返回值决定还要不要弹通用的
     *  失败提示框。两种情况下该条都不再冒泡。
     *  @param clientMsgId 回执里的客户端消息ID
     *  @param code        回执结果码，取值见 enum error_code
     */
    bool OnSendChatAckFailed(const std::string& clientMsgId, int code);

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
     *  WM_CHAT_DELETE_REQUEST 处理：消息列表里有服务器 msgid 的行被右键「删除」或多选「删除 N」。
     *  lParam=堆上 std::vector<long long>*（要删除的服务器 msgid），处理后 delete。
     *  弹确认框，确认后逐条发 cmd_delete_message（该命令不带会话身份，只认 msgid）；
     *  删除只对本人生效，群里其他成员不受影响。气泡等服务端推回 cmd_messages_deleted_notify
     *  后经 OnMessagesDeleted 移除。调试入口没有服务端可问，确认后直接在本窗口移除。
     */
    LRESULT OnChatDeleteRequest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_FORWARD_REQUEST 处理：多选操作条上点了「逐条转发」或「合并转发」。
     *  lParam=堆上 balloonchatwnd::ChatForwardRequest*，处理后 delete。补上源会话（本群）、
     *  本窗口句柄与合并标题后，以 FMG_MSG_OPEN_FORWARD_PICKER 投给主面板打开目标选择窗。
     *  调试入口没有主面板，只记日志。
     */
    LRESULT OnChatForwardRequest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_REEDIT_REQUEST 处理：发送失败的文字气泡右键选了「重新编辑」。
     *  lParam=堆上 balloonchatwnd::ChatReeditData*，处理后 delete。把原文插到输入框光标处，正文里的 @
     *  重新登记进 m_pendingMentions，并把焦点交给输入框。
     */
    LRESULT OnChatReeditRequest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_ADD_TO_MEMO 处理：文字消息右键选了「添加到我的备忘」或「添加到群备忘」（docs/备忘与提醒方案.md §8）。
     *  按「来自 群名 / 发送人，时间」补上来源，生成预填的标题与备注，打开新建本人备忘或群备忘的窗口。
     *  lParam 为堆上的 ChatAddToMemoRequest，本函数 delete。
     */
    LRESULT OnChatAddToMemo(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_VOICE_ACTION 处理（语音消息第 3c 步）：语音气泡上点了发送失败的红色感叹号或右键「重新发送」时重发；
     *  点气泡（播放 / 停止）在第 3e 步接入，目前只记日志。wParam 为 ChatVoiceAction，lParam 为堆上的
     *  balloonchatwnd::VoiceActionRequest*，处理后 delete。
     */
    LRESULT OnChatVoiceAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 把一条待冒泡的发送通知给主面板（FMG_MSG_CONV_SEND_BUMP），没有主面板时什么都不做。
    void PostSendBump(const ConvSendBumpData& data);

    /**
     *  WM_CHAT_OPEN_MERGED_RECORD 处理：单击了合并卡片。lParam=堆上 std::string*（合并消息的
     *  content 原文）。转投给主面板打开聊天记录查看窗（FMG_MSG_OPEN_MERGED_VIEWER，所有权随之
     *  转交）；调试入口没有主面板，记日志后 delete。
     */
    LRESULT OnChatOpenMergedRecord(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 共享卡片被单击（WM_CHAT_OPEN_SHARE）：lParam 为堆上的共享 id，经文件共享宿主打开后释放；
    // wParam 为 kChatOpenShareOwn 时是本人的共享，在本机打开，否则打开浏览窗口
    LRESULT OnChatOpenShare(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_DESTROY：向文件共享宿主注销本群的可用共享个数；bHandled 置 FALSE，基类照常处理
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // ---------------- 边栏「备忘」页签（备忘与提醒，docs/备忘与提醒方案.md 1.2）----------------

    /**
     *  FMG_MSG_MEMO_CHANGED：备忘簿有变化。wParam 为受影响的群 id（0 表示全量替换或个人备忘），
     *  与本群有关时重画「备忘」面板与页签角标。
     */
    LRESULT OnMemoChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  按备忘簿重画「备忘」面板：备忘卡片、空状态提示（拉取超时时提示没有加载出来），以及「＋ 新建群备忘」
     *  按本人角色显隐、拉取超时时置灰。
     */
    void RenderMemoPanel();

    /**
     *  刷新「备忘」页签：标题写成「备忘 N」（备忘簿还没拉到时只写「备忘」）；角标为新到、还没点开过页签的条数，
     *  页签正选中时把新到的计为已看过、角标为 0。
     */
    void RefreshMemoTabBadge();

    /**
     *  打开群备忘的编辑对话框。
     *    memoid：0 表示新建（开始时间取今天的下一个整点，提醒全体成员，只有群主、管理员能新建）；否则打开
     *            这一条，群主、管理员可以修改，普通成员只能查看。
     */
    void OpenGroupMemoEditor(int64_t memoid);

    /**
     *  新建群备忘并预填标题与备注（聊天消息右键「添加到群备忘」，docs/备忘与提醒方案.md §8）。其余字段与
     *  OpenGroupMemoEditor(0) 相同（今天的下一个整点、提醒全体成员）；权限与备忘列表的检查也相同，不满足时不打开。
     *    titleUtf8 / noteUtf8：预填的标题与备注（UTF-8，已按字数上限截断）。
     */
    void OpenGroupMemoEditorWithPrefill(const std::string& titleUtf8, const std::string& noteUtf8);

    /**
     *  备忘卡片的右键菜单：群主、管理员有「修改」「删除」，普通成员只有「查看」。
     *    memoid：被右键的那张卡片的备忘 ID。
     */
    void ShowGroupMemoMenu(int64_t memoid);

    /**
     *  记下第 cardIndex 张备忘卡片的备忘 ID，向自己投一条 kMemoCardActionMsg，等当前的控件通知处理完再做。
     *    action：MemoCardAction 的取值；cardIndex：卡片序号，无效时什么都不做。
     */
    void PostMemoCardAction(int action, int cardIndex);

    /**
     *  kMemoCardActionMsg：执行卡片上的操作。wParam 为 MemoCardAction，备忘 ID 取自 m_pendingMemoCardId。
     */
    LRESULT OnMemoCardActionMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kMustReadConfirmMsg：入群必读弹窗里点了「我已阅读」，发送确认请求（1333）。界面等服务端推 1317 后再恢复。
     */
    LRESULT OnMustReadConfirmMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kMustReadShowMsg：输入区的提示行或必读卡片被点了，弹出入群必读公告（等当前的控件通知处理完再弹）。
     */
    LRESULT OnMustReadShowMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 以「入群必读」模式弹出必读公告（标题「入群必读」，按钮「我已阅读」）；必读公告的内容还没取到时什么都不做
    void ShowMustReadPopup();

    // 发 1336 拉一次本群问答，面板置为「正在加载」（真实群才拉）
    void RequestGroupQa();

    // 按当前的问答列表、搜索框文字与本人角色重建「问答」面板的卡片、空状态提示与「＋ 新建问答」；
    // 面板还没排过版（宽度为 0）时什么都不做，切到问答 Tab 时再画
    void RenderQaPanel();

    /**
     *  记下第 cardIndex 张问答卡片对应的问答，向自己投一条 kQaCardActionMsg，等当前的控件通知处理完再做。
     *    action：QaCardAction 的取值；cardIndex：卡片序号，无效时什么都不做（kQaCardActionAdd 不看序号）。
     */
    void PostQaCardAction(int action, int cardIndex);

    /**
     *  kQaCardActionMsg：执行问答卡片上的操作或「＋ 新建问答」。wParam 为 QaCardAction，问答取自 m_pendingQaItem。
     */
    LRESULT OnQaCardActionMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //机器人区域的延后操作（kGroupBotActionMsg）：弹资料卡或打开群机器人管理窗
    LRESULT OnGroupBotActionMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kQaEditResultMsg：问答编辑窗点了「保存」且校验通过，lParam 为堆上的 balloonchatwnd::QaEditResult*（这里 delete），
     *  发送 cmd_save_group_qa。
     */
    LRESULT OnQaEditResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_MOUSEMOVE / WM_MOUSELEAVE：按光标位置更新备忘卡片的悬停态（理由见 BalloonMemo/MemoCardList.h）。
     *  只旁听、不消费，bHandled 置 FALSE。
     */
    LRESULT OnMemoMouseMoveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMemoMouseLeaveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  备忘卡片重建之后按当前光标位置补一次悬停判定（光标停着不动时不会有 WM_MOUSEMOVE）。
     */
    void RefreshMemoHoverAtCursor();

    /**
     *  选择提醒对象用的候选人：本群成员（取自成员列表），不含本人。
     */
    std::vector<net::ContactItem> BuildMemoCandidates() const;

    // 屏幕共享卡片被单击（WM_CHAT_JOIN_SCREENSHARE）：lParam 为堆上的 ScreenShareJoinRequest，经屏幕共享中心交给
    // FlamingoMedia.dll 加入共享后释放
    LRESULT OnChatJoinScreenShare(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_RELAY_ACTION：消息列表里点了接龙卡片、卡片底部按钮或带 cardmsgid 的接龙系统提示行。
     *  wParam 为 balloonchatwnd::ChatRelayAction；lParam 为堆上的 long long*（卡片 msgid），本函数 delete。
     *  卡片在当前视图里时带上它的内容与发起人，否则留空由主面板从本地库补齐；卡片已撤回（列表里
     *  那一行已变成提示行）时不打开。请求经 FMG_MSG_OPEN_RELAY_DETAIL 投给主面板。
     */
    LRESULT OnChatRelayAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

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
     *  WM_CHAT_FILE_ACTION 处理：文件气泡底部按钮被点。群里的文件消息来自转发，或来自宿主「发送到会话」
     *  （先上传群文件，成功后才发消息，见 SendUploadedFileMessage，所以本人气泡一出现就是已完成态），
     *  没有接收 / 拒绝流程，也没有本端上传中的那一笔，
     *  所以只有四类动作：打开 / 打开所在文件夹 / 另存为、下载与重新下载、取消下载；本人发出、被服务端拒绝的那条
     *  点「重新发送」时重发文件消息（见 ResendGroupFileMessage）。
     *  lParam=堆上 balloonchatwnd::ChatFileActionData*，处理后 delete。
     */
    LRESULT OnChatFileAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

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
     *  ChatMessageList / GroupMemberItem 的「本人是否在本群关注了 TA 的发言」查询钩子（静态转发到实例），
     *  实例内部经 FMG_MSG_QUERY_GROUP_FOLLOW 同步问主面板 —— 群关注名单只有主面板那一份
     *  （docs/消息提醒模式与消息盒子方案.md §12）。
     *    userdata：本窗口 this。
     *    uid：     待查的 userid。
     *  @return 已关注返回 true；拿不到窗口 / 没有宿主主面板（调试入口）返回 false。
     */
    static bool GroupFollowThunk(void* userdata, long long uid);

    /**
     *  设置本人在本群的角色（0成员 1管理员 2群主，见 group_member_role）。决定
     *  右上角"群管理"齿轮按钮是否显示：仅群主 / 管理员显示。可在 CreateAndShow
     *  前后调用（前调只记录，InitUi 据此设显隐；后调即时刷新齿轮显隐并重排头部）。
     *    role：本人角色。
     */
    void SetMyRole(int role);

    /**
     *  设置本群的仅通知模式开关（原称全员禁言，docs/群管理接入方案.md §14），来自群列表、群资料应答与资料变更推送。
     *  本人是普通成员且开关开着时，输入区换成灰字提示行；开关关闭或本人是群主、管理员时恢复输入框。
     *  与 SetMyRole 一样可在 CreateAndShow 前后调用（前调只记录，InitUi 据此设显隐）。
     *    muteall：非 0 为开启。
     */
    void SetMuteAll(int muteall);

    /**
     *  设置本群的「谁可以 @ 全体」，来自群资料应答、群列表（沿用缓存）与资料变更推送。只记下来：
     *  下次弹 @ 浮层时据此与本人角色一起决定有没有「所有人」（CanMentionAll）。可在 CreateAndShow 前后调用。
     *    scope：0 仅群主和管理员，1 所有人（net::GroupInfoResult::m_atallscope）。
     */
    void SetAtAllScope(int scope);

    /**
     *  本人在本群的角色（0成员 1管理员 2群主，见 group_member_role）；成员列表到达之前为默认的普通成员。
     *  备忘中枢据此判断本人能不能修改群备忘。
     */
    int GetMyRole() const
    {
        return m_myRole;
    }

    /**
     *  更新某个群成员的在线状态，成员侧栏里该成员头像右下角的徽标随之重画。由主面板在三个
     *  时机调用：成员列表到达后逐个补上、联系人在线状态推送到达、本人切换在线状态。
     *    userid：  成员 userid。
     *    presence：在线状态，取值同协议的 presence_type（0 离线 / 1 在线 / 2 离开 / 3 忙碌）；
     *              balloonchatwnd::kMemberPresenceUnknown（-1）表示查不到，不画徽标。
     *  取值记在窗口里，成员侧栏之后重建时据此恢复；该成员此刻不在侧栏里时只记录。
     */
    void SetMemberPresence(int64_t userid, int presence);

    /**
     *  收到本群的一条新消息：渲染为对方气泡（带真实发送人昵称/头像）。由主面板
     *  在收到 cmd_message_notify（ctype==2）、按 cid=群 id 找到本窗口后调用。
     *    notify：消息推送解析结果（本轮仅处理文本）。
     */
    void OnRecvGroupMessage(const net::MessageNotify& notify);

    /**
     *  按本群桌面字幕的状态高亮或恢复头部「字幕模式」按钮（docs/聊天字幕模式方案.md 第 1 节第 11 条）：
     *  高亮时只把图标画成蓝色，悬停提示同时换成「字幕已开启」，关着时为「字幕已关闭」。
     *  字幕由主面板持有、不随本窗口关闭；主面板在字幕开关时、以及建出本窗口后调用。窗口还没建好时什么都不做。
     *  @param active true 字幕开着（按钮高亮），false 关着
     */
    void SetSubtitleHighlight(bool active);

    /**
     *  收到本群成员列表：填右侧成员侧栏，并建"发送人 userid → 昵称/头像色"映射，
     *  供气泡显示真实发送人。由主面板的 OnGroupMembersArrived 路由调用。
     *    members：群成员解析结果。
     */
    void OnGroupMembersArrived(const net::GroupMembersResult& members);

    /**
     *  设置企业部门数组，供「成员」页签的部门树视图使用（docs/群成员部门树方案.md）。主面板新开群聊窗时、以及
     *  收到通讯录（1201）时推给已打开的群聊窗。当前是部门树视图且成员已到齐时按新部门数组重建成员列表。
     *    departments：1201 应答的部门数组（本窗口复制一份）。
     */
    void SetOrgDepartments(const std::vector<net::DepartmentItem>& departments);

    /**
     *  收到本群文件列表的一页应答：累积进拉取缓冲；若 hasmore 为真则继续拉下一页，
     *  否则把累积结果灌进群文件库并渲染面板（Option B 客户端全量缓存）。失败应答
     *  （code!=0）则收尾本次拉取、用已累积的渲染。由主面板 OnGroupFilesArrived 路由调用。
     *    result：cmd_get_group_files 应答解析结果（本页条目 + hasmore + 回显 offset）。
     */
    void OnGroupFilesArrived(const net::GetGroupFilesResult& result);

    /**
     *  切到右侧边栏「群文件」Tab 并定位到某个文件：把分类重置为「全部」、清空文件名搜索、
     *  翻到该文件所在的页、以淡黄底标出它并滚动到可见。主面板全局搜索点群文件结果时调用。
     *  群文件库还没拉全（刚开窗）时先记下，拉全后在 OnGroupFilesArrived 里再定位；库里
     *  找不到这个文件（已被删除）时只切到群文件 Tab，记一条日志。
     *    entryId：群文件条目 id（字符串编码的 int64）；空串时只切 Tab。
     */
    void ShowGroupFileEntry(const std::string& entryId);

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
     *  收到查询入群必读的应答（cmd_get_must_read=1335，主面板按 groupid 转来，docs/群公告接入方案.md §14）：
     *  记下本人是否待确认与必读公告的内容，按结果切换输入区；本人待确认且本窗口还没弹过时自动弹出必读公告。
     *  失败应答只记日志，输入区保持原状。
     *    result：1335 应答解析结果。
     */
    void OnMustReadArrived(const net::MustReadResult& result);

    /**
     *  重新查询一次本人在本群的入群必读状态（发 1335）。主面板收到本群的 1317（changetype 7）或 1318 推送时调用。
     */
    void RefreshMustRead();

    /**
     *  收到本群问答（cmd_get_group_qa=1336，主面板按 groupid 转来，docs/群问答方案.md）：替换本地列表并重画「问答」面板。
     *  失败应答把面板置为「没有加载出来」。
     *    result：1336 应答解析结果。
     */
    void OnGroupQaArrived(const net::GroupQaResult& result);

    /**
     *  本群问答变了（cmd_group_qa_changed_notify=1339）：拉过问答的窗口重拉一次；没打开过「问答」页签的不拉，
     *  等用户点开时自然拉到最新的。
     */
    void OnGroupQaChanged();

    /**
     *  收到本群可用的机器人（cmd_get_group_bots=2801，主面板按 groupid 转来，docs/机器人方案.md §4.2）：替换本地列表，
     *  刷新已显示消息的发送者名字与「机器人」标识、成员页签的机器人分组与 @ 候选。失败应答保留原列表。
     *    result：2801 应答解析结果。
     */
    void OnGroupBotsArrived(const net::GroupBotsResult& result);

    /**
     *  本群可用的机器人有变化（cmd_group_bots_changed_notify=2820）：重拉 2801。
     */
    void OnGroupBotsChanged();

    /**
     *  本群的机器人列表（含停用的与在本群关闭了的），2801 到达之前为空。供主面板与插件宿主查询。
     */
    const std::vector<net::GroupBotInfo>& GetGroupBots() const;

    /**
     *  设置头部那行群简介（标题栏下方、图标按钮左侧，一行显示，过长时以省略号截断）。
     *  数据来自群资料里的 intro 字段，随 1303 应答与 1318 资料变更推送到达。
     *  2026-10-04 之前这一行显示的是最新公告标题，现已改为群简介。
     *    intro：群简介原文（可含换行，显示时整理成一行）；空串表示本群没有填写群简介，该行不画。
     */
    void SetGroupIntro(LPCTSTR intro);

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
    //cancelled 为 true（用户点了「取消」）则置「已取消」态而非失败态。失败时 errcode 为失败错误码，
    //气泡下方据此写原因（如文件名包含敏感词，见 FileSendFailureText）。
    void OnFileSendResult(bool ok, bool cancelled, const std::string& clientMsgId, int errcode = 0);
    //更新收到的某条文件的下载进度（按服务器 msgid 定位）。
    void OnFileDownloadProgress(long long msgid, long long received, long long total);
    //收到的某条文件下载完成：按 msgid 填本地落地路径并渲染。
    void OnFileDownloaded(long long msgid, const std::wstring& localPath);
    //收到的某条文件下载失败：按 msgid 标失败；cancelled 为 true 则标「已取消」。
    void OnFileDownloadFailed(long long msgid, bool cancelled);
    /**
     *  一段语音下载有了结果（主面板按 md5 广播）：把本窗口里该段语音的气泡改为就绪或下载失败（语音消息第 3d-3 步）。
     *  @param md5       语音文件内容 MD5
     *  @param ok        是否下载成功
     *  @param localPath 成功时文件的本地完整路径
     */
    void OnVoiceDownloadResult(const std::string& md5, bool ok, const std::wstring& localPath);
    /**
     *  发语音的上传结果（主面板按会话路由来，语音消息第 3c 步）。成功时不改气泡，等发送回执回填 msgid；
     *  失败时把那条语音气泡标为发送失败（红色感叹号，气泡下方写原因），并取消这次发送的会话冒泡。
     *  @param ok          上传并发出是否成功
     *  @param clientMsgId 发送时的客户端消息ID，定位那条语音气泡
     *  @param errcode     失败错误码（见 enum error_code），据此写失败原因
     */
    void OnVoiceSendResult(bool ok, const std::string& clientMsgId, int errcode);
    /**
     *  一条语音在别处（本窗口自己、或消息记录窗）播放过了（主面板写库后按会话转来，语音消息第 6a 步）：
     *  把本窗口里同一条语音的未听红点去掉。本窗口自己播放时气泡早已记为已听，再调一次不会有变化。
     *  @param msgid 那条语音的服务器 msgid
     */
    void OnVoicePlayedElsewhere(long long msgid);

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
        MESSAGE_HANDLER(kOpenRecordMenuMsg, OnOpenRecordMenu)
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
        MESSAGE_HANDLER(WM_CHAT_DELETE_REQUEST, OnChatDeleteRequest)
        MESSAGE_HANDLER(WM_CHAT_FORWARD_REQUEST, OnChatForwardRequest)
        MESSAGE_HANDLER(WM_CHAT_REEDIT_REQUEST, OnChatReeditRequest)
        MESSAGE_HANDLER(WM_CHAT_ADD_TO_MEMO, OnChatAddToMemo)
        MESSAGE_HANDLER(WM_CHAT_VOICE_ACTION, OnChatVoiceAction)
        MESSAGE_HANDLER(WM_CHAT_OPEN_MERGED_RECORD, OnChatOpenMergedRecord)
        MESSAGE_HANDLER(WM_CHAT_OPEN_SHARE, OnChatOpenShare)
        MESSAGE_HANDLER(WM_CHAT_JOIN_SCREENSHARE, OnChatJoinScreenShare)
        //点了群公告卡片：打开公告详情窗
        MESSAGE_HANDLER(WM_CHAT_OPEN_ANNOUNCEMENT_CARD, OnChatOpenAnnouncementCard)
        //点了群备忘卡片：打开这条备忘
        MESSAGE_HANDLER(WM_CHAT_OPEN_MEMO_CARD, OnChatOpenMemoCard)
        MESSAGE_HANDLER(WM_CHAT_OPEN_CLOUD_RECORD_CARD, OnChatOpenCloudRecordCard)
        //点了代码片段卡片：打开代码查看窗
        MESSAGE_HANDLER(WM_CHAT_OPEN_CODE_VIEWER, OnChatOpenCodeViewer)
        MESSAGE_HANDLER(WM_CHAT_OPEN_MARKDOWN_PREVIEW, OnChatOpenMarkdownPreview)
        //「发送代码」窗口交回要发送的代码
        MESSAGE_HANDLER(ballooncode::kMsgCodeComposeSend, OnCodeComposeSend)
        //输入框的粘贴识别通知：显示或收起「以代码片段发送」提示条
        MESSAGE_HANDLER(balloonchatwnd::kMsgChatCodePaste, OnCodePasteNotify)
        //公告详情窗上点了「查看全部」：切到右侧「公告」页签
        MESSAGE_HANDLER(FMG_MSG_OPEN_GROUP_ANNOUNCE, OnOpenGroupAnnounceMsg)
        //点了接龙卡片、卡片底部按钮或接龙系统提示行：请主面板打开详情窗
        MESSAGE_HANDLER(WM_CHAT_RELAY_ACTION, OnChatRelayAction)
        MESSAGE_HANDLER(WM_CHAT_MENTION_ACTION, OnChatMentionAction)
        MESSAGE_HANDLER(WM_CHAT_IMAGE_ACTION, OnChatImageAction)
        MESSAGE_HANDLER(WM_CHAT_FILE_ACTION, OnChatFileAction)
        MESSAGE_HANDLER(WM_CHAT_LOAD_OLDER, OnChatLoadOlder)
        MESSAGE_HANDLER(WM_CHAT_REPLY_REQUEST, OnChatReplyRequest)
        MESSAGE_HANDLER(WM_CHAT_LOCATE_QUOTED, OnChatLocateQuoted)
        //备忘中枢的备忘簿有变化：重画「备忘」面板与页签角标
        MESSAGE_HANDLER(FMG_MSG_MEMO_CHANGED, OnMemoChanged)
        //备忘卡片上的操作（「修改」「删除」、双击、右键），由卡片的通知投给自己后在这里执行
        MESSAGE_HANDLER(kMemoCardActionMsg, OnMemoCardActionMsg)
        //入群必读弹窗里点了「我已阅读」：发送确认请求
        MESSAGE_HANDLER(kMustReadConfirmMsg, OnMustReadConfirmMsg)
        //输入区提示行或必读卡片被点：弹出必读公告
        MESSAGE_HANDLER(kMustReadShowMsg, OnMustReadShowMsg)
        //问答卡片上的「发到群里」「修改」「删除」与「＋ 新建问答」，由控件通知投给自己后在这里执行
        MESSAGE_HANDLER(kQaCardActionMsg, OnQaCardActionMsg)
        MESSAGE_HANDLER(kGroupBotActionMsg, OnGroupBotActionMsg)
        //问答编辑窗交回的问题与答案：发送保存请求
        MESSAGE_HANDLER(kQaEditResultMsg, OnQaEditResultMsg)
        //备忘卡片的悬停按光标位置判定，只旁听、不消费
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMemoMouseMoveMsg)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMemoMouseLeaveMsg)
        //窗口销毁：注销本群的可用共享个数（不拦截，交给基类继续处理）
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

public:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端，引用不持有

    /**
     *  应用当前连接态：顶部提示条的显隐与文字、发送按钮是否可用（离线时输入框仍可编辑）。
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

    /**
     *  全局默认聊天字体改了之后，若本群没有单独设过字体，就改用新的默认字体（输入框
     *  随之更新，之后发出的消息也用它）；单独设过字体的群不受影响。由主面板在收到
     *  FMG_MSG_DEFAULT_CHAT_FONT_CHANGED 后逐窗调用，新字体从账号配置里读。
     */
    void ApplyDefaultChatFontIfNotOverridden();

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
    // 按文件共享模块是否可用（已加载且本企业的功能开关打开）显示或隐藏工具栏的「文件共享」按钮；显隐变化后
    // 对按钮所在的行重排（UI 约定第 5 条）。窗口建好时调一次；运行中收到 1013 功能开关推送后由主面板再调
    void ApplyShareButtonVisibility();

    // 按群通话是否可用（FlamingoMedia.dll 已加载且本企业的功能开关打开）显示或隐藏头部的「语音通话」「视频通话」按钮；
    // 显隐变化后对按钮所在的行重排（UI 约定第 5 条）。窗口建好时调一次；运行中收到 1013 功能开关推送后由主面板再调
    void ApplyGroupCallButtonVisibility();

private:
    /**
     *  首屏历史渲染之前发出的 Markdown 文档都已确认入库、且首屏已渲染时，重新拉一次最新一页，
     *  让被首屏渲染清掉的卡片按服务端快照显示出来（判断见 balloonchatwnd::MarkdownEarlySendTracker）。
     *  条件不满足时什么都不做。
     */
    void ReloadIfMarkdownEarlySendsDone();

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
     *    withQuote：是否带上正在引用的消息与 @ 提醒（并在发出后收起引用条）；SendPlainText 传 false，
     *               引用条与输入框保持原样（屏幕共享第 25 期：分享链接的邀请不该带上用户正在编辑的引用）
     */
    void SendGroupTextPart(const CString& text, bool withQuote = true);

    // 打开 Markdown 编辑器，发送对象预选本群（工具栏的 Markdown 编辑器按钮、Markdown 消息的「重新编辑」共用；
    // docs/Markdown消息方案.md 第 11.3 节）。调试入口没有群 id 时不预选发送对象。
    //   sourceUtf8：预填的 Markdown 源文本（UTF-8）；为空时打开空白文档
    void OpenMarkdownEditor(const std::string& sourceUtf8);

    // 打开「发送代码」窗口（工具栏「代码」按钮、粘贴识别提示条、重新编辑、发送被拒后放回代码共用）。
    //   initialCode：预填的代码；为空时取输入框里选中的文字
    //   langId：     预选的语言标识（如 "cpp"）；为空时选「自动识别」
    //   notes：      预填的批注（重新编辑、发送被拒后放回时带回），行号指 initialCode 的行；可以为空
    void OpenCodeCompose(const std::wstring& initialCode, const std::string& langId,
                         const std::vector<codesnippet::CodeNote>& notes = std::vector<codesnippet::CodeNote>());

    // 「发送代码」窗口上次手选的语言标识（用户配置）；没有手选过或没有登录时为空串
    std::string RememberedCodeLang() const;

    // 发出一段代码片段：乐观气泡按代码卡片显示，网络层按文字消息加 format="code" 与 lang 发出，带上待发的引用。
    // 离线时提示并放弃，发出去会超过单条消息上限时同样提示并放弃。调试入口（没有真实会话）下只在本地显示。
    //   code：规范化后的代码；lang：语言，取值见 codesnippet::CodeLanguage
    //   notes：批注（docs/代码片段方案.md 第 10.5 节），行号指 code 的行，已整理；没有批注时为空
    //   quote：带上的引用；为 NULL 时用输入框里待发的引用、发出后清掉它；不为 NULL 时用它（追加批注时引用原消息，
    //          docs/代码片段方案.md 第 11.3 节），输入框里待发的引用保持不动
    //   返回：已发出（调试入口下为已在本地显示）时为 true；离线、超限而没有发出时为 false
    bool SendCodeSnippet(const std::wstring& code, int lang,
                         const std::vector<codesnippet::CodeNote>& notes = std::vector<codesnippet::CodeNote>(),
                         const net::ReplyQuote* quote = NULL);

    // 打开一条代码片段的代码查看窗（docs/代码片段方案.md 第 12 节）：已有服务器 msgid 的可以在查看窗里追加批注，
    // 发送时查看窗把结果经 kMsgCodeComposeSend 交回本窗口。
    //   code、lang、notes：这条消息的代码、语言与批注（notes 里作者为 0 的视为这条消息的发送者）
    //   key：消息标识（msgid 的十进制串或 clientMsgId），查看窗据此复用已打开的窗口
    //   msgid：服务器 msgid；不大于 0 时只能查看
    //   senderId：这条消息的发送者 userid；fromMe：是不是本人发的
    //   openNotesPane：打开时展开批注栏（聊天卡片右键「添加批注…」、没能发出、重新编辑时为 true）
    //   editedNotes：没能发出或重新编辑时放回的批注；为空时从这条消息的批注开始
    void ShowCodeViewer(const std::wstring& code, int lang, const std::vector<codesnippet::CodeNote>& notes,
                        const std::string& key, long long msgid, long long senderId, bool fromMe, bool openNotesPane,
                        const std::vector<codesnippet::CodeNote>& editedNotes = std::vector<codesnippet::CodeNote>());

    // 代码片段批注作者的显示名（userid 到名字），群聊里按群成员表取名字（取不到时写「用户<id>」）。代码查看窗与批注模式的批注栏据此写作者
    //   notes：批注，其中各条的作者都取名字；baseSenderId：代码片段的发送者 userid，也取名字
    std::map<long long, std::wstring> CodeNoteAuthorNames(const std::vector<codesnippet::CodeNote>& notes,
                                                          long long baseSenderId) const;

    // 本人 userid；没有登录态时为 0
    long long SelfUserIdForCode() const;

    // 追加批注时引用原消息：按 msgid 在消息列表里找到它并生成引用；找不到时返回无效的引用（m_valid 为 false），
    // 此时不带引用照常发出
    net::ReplyQuote CodeBaseQuote(long long baseMsgId);

    // 显示或收起输入框上方的「以代码片段发送」提示条，并对它所在的输入区块补一次布局（SetVisible 不会重排）。
    //   show：true 显示；false 收起，同时清掉显示时记下的输入框文字
    void ShowCodePasteBar(bool show);


    /**
     *  发送输入框里的一张内联图：直接发 m_pendingShotPaths[imageIndex] 这个源文件。
     *  imageIndex 由 BuildOutgoingParts 从图片标记读回。下标越界则记日志跳过。
     */
    void SendGroupImagePart(int imageIndex);

    /**
     *  把一张本地图片以"带源文件标记的内联图"插入输入框当前光标处，并登记它的源文件，
     *  发送时按标记取回（见 ChatFaceTag.h 的 ImageTagFromIndex）。截图「发送到对话框」、
     *  粘贴与拖入图片都走这里。
     *    path：图片本地文件完整路径。
     *  @return 插入成功返回 true；输入框不存在、文件解不开时返回 false，此时不登记。
     *  副作用：成功时 m_pendingShotPaths 末尾追加 path。
     */
    bool StageImageInInput(const std::wstring& path);

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
     *  用一条本人发出的群文字消息构造右侧的本端气泡（群聊本端气泡不显示发送人名）：带上
     *  msgid、时间、字体、@ 高亮与引用块，头像取本端真实头像。历史消息与实时回显共用，
     *  两处还原出的气泡完全一致。
     *    m：    消息解析结果，发送方须是本人。
     *    text： 气泡正文（非文字消息由调用方换成占位文字）。
     *    list： 消息列表控件，用于解析引用块的缩略图路径；可为 NULL。
     *  @return 气泡数据，由调用方追加进列表
     */
    balloonchatwnd::ChatMessage MakeSelfTextMessage(const net::MessageNotify& m, const CString& text,
                                                    balloonchatwnd::ChatMessageList* list);

    /**
     *  用一条合并转发的聊天记录（msgtype=15）构造卡片气泡：本人发出的画在右侧（同本端
     *  文字气泡），别人发出的画在左侧并带发送人名与头像；卡片内容由 ApplyMergedCard 填写。
     *  历史消息与实时推送共用。
     *    m：      消息解析结果，m_mergedContent 为 content 原文。
     *    fromMe： 发送方是否本人。
     *    list：   消息列表控件，供本端气泡解析引用缩略图路径；可为 NULL。
     *  @return 气泡数据，由调用方追加进列表
     */
    // 共享卡片：发送人的填法同 MakeGroupMergedMessage，再套用卡片内容（分享者名取发送人名，含 DLL 手里的最新状态）
    balloonchatwnd::ChatMessage MakeGroupShareMessage(const net::MessageNotify& m, bool fromMe,
                                                      balloonchatwnd::ChatMessageList* list);

    // 工具栏「文件共享」：在按钮旁弹出本会话的共享列表浮层（FlamingoFileShare.dll 提供）
    void OpenConversationShares();

    // 屏幕共享卡片：发送人（发起人）的填法同 MakeGroupShareMessage，再套用卡片内容与屏幕共享中心记录的状态
    /**
     *  构造一张群通话卡片（msgtype=16，复用屏幕共享卡片的版式），实时推送与历史加载共用。
     *  @param m      消息（m_groupCallContent 为卡片 content）
     *  @param fromMe 是否本人发起
     *  @param list   消息列表（取本人头像等）
     */
    balloonchatwnd::ChatMessage MakeGroupCallCardMessage(const net::MessageNotify& m, bool fromMe,
                                                         balloonchatwnd::ChatMessageList* list);

    /**
     *  头部「语音通话」「视频通话」按钮：本群已有进行中的群通话时直接加入那一场；否则以本群成员为候选弹出选人框，
     *  选好后发起（docs/群音视频通话方案.md 第 2 节第 1 条「选人振铃」）。离线、模块没有加载时提示并放弃。
     *  @param media 1 群语音、2 群视频
     */
    void StartGroupCallFromHeader(int media);

    /**
     *  群通话选人框确认（选人框的 IMemberPickSink 转来）：发起时交给群通话中心发起，通话中邀请时交给它追加邀请。
     *  @param userIds 已选的人
     */
    void OnGroupCallMembersPicked(const std::vector<int64_t>& userIds);

    balloonchatwnd::ChatMessage MakeGroupScreenShareMessage(const net::MessageNotify& m, bool fromMe,
                                                            balloonchatwnd::ChatMessageList* list);

    // ---- 群公告卡片（msgtype=22）与群备忘卡片（msgtype=23），docs/群公告接入方案.md §13.4、docs/备忘与提醒方案.md §7.3 ----
    /**
     *  构造一条群公告卡片或群备忘卡片的气泡：本人发的画在右侧，别人发的带发送人名字与头像；卡片内容由
     *  ApplyAnnouncementCard / ApplyMemoCard 套用。群备忘卡片另按备忘簿设好可用状态。实时推送与翻历史共用。
     *    m：消息（msgtype 须为 22 或 23）；fromMe：是否本人发的；list：消息列表，取本人头像用。
     */
    balloonchatwnd::ChatMessage MakeGroupInfoCardMessage(const net::MessageNotify& m, bool fromMe,
                                                         balloonchatwnd::ChatMessageList* list);

    /**
     *  构造一条云端录像卡片（msgtype=24，docs/屏幕共享与录制方案.md 12.6.5）的气泡：本人发的画在右侧，别人发的带发送人
     *  名字与头像；卡片内容由 ApplyCloudRecordCard 套用。实时推送与翻历史共用。
     *    m：消息（msgtype 须为 24）；fromMe：是否本人发的；list：消息列表，取本人头像用。
     */
    balloonchatwnd::ChatMessage MakeGroupCloudRecordMessage(const net::MessageNotify& m, bool fromMe,
                                                            balloonchatwnd::ChatMessageList* list);

    /**
     *  卡片副标题里写的操作人名字：成员表或通讯录里查得到时用那里的显示名（群名片优先），查不到返回空串，
     *  卡片随之退回服务端写进卡片里的昵称，不显示「用户<id>」。
     *    userId：发布人或备忘的操作人，即卡片的发送者。
     */
    CString InfoCardActorName(int64_t userId) const;

    /**
     *  公告被删除（1326 带 cardmsgid 与改写后的卡片）：把列表里那张公告卡片换成已删除形态。列表里没有这一条时什么都不做。
     *    cardMsgId：卡片的 msgid；cardContent：改写后的卡片 content。
     */
    void ApplyAnnouncementCardDeleted(int64_t cardMsgId, const std::string& cardContent);

    /**
     *  按备忘簿重新判定列表里每张群备忘卡片是否可用（备忘被删除、本人被移出提醒对象后失效），有变化的就地刷新。
     *  @return 刷新了几张卡片
     */
    int RefreshMemoCardsAvailability();

    /**
     *  WM_CHAT_OPEN_ANNOUNCEMENT_CARD：点了可点的群公告卡片，用卡片里的内容打开公告详情窗（m_announceCardDlg）。
     *  lParam 为堆上的 InfoCardOpenRequest，处理后 delete。
     */
    LRESULT OnChatOpenAnnouncementCard(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 代码片段卡片请求打开代码查看窗（WM_CHAT_OPEN_CODE_VIEWER）：lParam 为堆上的 CodeViewerOpenRequest，
    // 按消息标识打开或调出查看窗后释放（docs/代码片段方案.md 第 3.3 节）
    LRESULT OnChatOpenCodeViewer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // Markdown 卡片请求打开预览窗口（WM_CHAT_OPEN_MARKDOWN_PREVIEW）：lParam 为堆上的 MarkdownPreviewOpenRequest，
    // 交给 Markdown 编辑器管理器打开或调出预览窗口后释放（docs/Markdown消息方案.md 第 11.3 节）
    LRESULT OnChatOpenMarkdownPreview(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 「发送代码」窗口把要发送的代码交回本窗口（ballooncode::kMsgCodeComposeSend）：lParam 为堆上的 CodeComposeResult，
    // 手选的语言记进用户配置，代码按代码片段发出后释放（docs/代码片段方案.md 第 3.1 节）
    LRESULT OnCodeComposeSend(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 输入框的粘贴识别通知（balloonchatwnd::kMsgChatCodePaste，docs/代码片段方案.md 第 3.1 节）：wParam 为 1 表示
    // 刚往空输入框里粘贴了文字，像代码时显示「以代码片段发送」提示条；为 0 表示内容有变化，提示条显示之后
    // 内容与显示时不同（继续输入、删改、发送）就收起
    LRESULT OnCodePasteNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_OPEN_MEMO_CARD：点了可点的群备忘卡片，按 OpenGroupMemoEditor 打开备忘簿里的最新版本
     *  （群主、管理员修改，其他人只读）；备忘簿里已经没有这一条时把卡片转为失效。lParam 处理同上。
     */
    LRESULT OnChatOpenMemoCard(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_OPEN_CLOUD_RECORD_CARD：点了云端录像卡片的「播放」，交给屏幕共享中心转 FlamingoMedia.dll 下载并打开
     *  （docs/屏幕共享与录制方案.md 12.6.5）。lParam 为堆上的 CloudRecordOpenRequest，本函数 delete。
     */
    LRESULT OnChatOpenCloudRecordCard(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  FMG_MSG_OPEN_GROUP_ANNOUNCE：公告详情窗（m_announceCardDlg，宿主是本窗口）上点了「查看全部」，切到右侧「公告」页签。
     *  wParam 为群 id，不是本群时忽略。
     */
    LRESULT OnOpenGroupAnnounceMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 按 FlamingoMedia.dll 是否已加载显示或隐藏头部的「共享屏幕」按钮；显隐变化后对按钮所在的行重排（UI 约定第 5 条）
    void ApplyScreenShareButtonVisibility();

    // ---- 语音消息（第 3b 步建立，第 6g 步改由录音会话驱动，交互见 docs/语音消息方案.md 第 3.1 节）----
    // 点了工具栏「语音消息」：交给录音会话开始录音，成功时显示录音条，失败时（麦克风被通话占用、打不开等）提示原因
    void StartVoiceRecording();

    // 以下为 balloonchatwnd::IVoiceRecordSessionHost 的实现：录音会话请本窗口做的事
    /**
     *  显示或收起录音条：录音时隐藏输入框与底部发送行；收起后把焦点还给输入框。
     *  @param show true 显示录音条，false 恢复输入框
     */
    virtual void ShowVoiceRecordBarUi(bool show) override;
    /**
     *  在窗口上显示一条录音相关的提示（如「说话时间太短」）。
     *  @param text 提示文字
     */
    virtual void ShowVoiceRecordToast(const std::wstring& text) override;
    /**
     *  发送一段录好的语音：在列表末尾插一条发送中的语音气泡，再交给文件传输线程上传并发到本群（第 3c 步）。
     *  @param clientMsgId 这条语音消息的客户端消息 id
     *  @param out         录音结果（文件已在 ChatVoice\<md5>.ogg）
     */
    virtual void SendRecordedVoice(const std::string& clientMsgId,
                                   const balloonvoice::VoiceRecordOutput& out) override;
    /**
     *  把一条语音交给文件传输线程上传并发出，并登记回执成功后的会话冒泡（语音消息第 3c 步）。首次发送与重发共用。
     *  @param msg 语音气泡上的消息：取 clientMsgId、录音文件路径、时长与波形
     */
    void StartVoiceUpload(const balloonchatwnd::ChatMessage& msg);
    /**
     *  重发一条发送失败的语音：能重发时把气泡改回发送中并重新上传；录音文件已不在时提示「语音文件已不存在」。
     *  @param clientMsgId 发送时的客户端消息ID
     */
    void ResendVoice(const std::string& clientMsgId);
    /**
     *  点了一条语音气泡（语音消息第 3e 步）：按 DecideVoiceClick 播放、停止、提示正在下载或重新下载。
     *  @param msgid       气泡上的服务器消息ID；大于 0 时按它定位
     *  @param clientMsgId 气泡上的客户端消息ID；msgid 为 0（本人还没回执的语音）时按它定位
     */
    void HandleVoiceClick(long long msgid, const std::string& clientMsgId);
    /**
     *  播放一条语音：交给语音中心播放，列表上进入播放中；对方发来、还没听过的同时请主面板在本地库标为已听。
     *  播放失败时提示「语音播放失败」。
     *  @param msg 气泡上的消息（取 msgid、clientMsgId、本地路径、发送方、已听）
     */
    void StartVoicePlayback(const balloonchatwnd::ChatMessage& msg);

    // 以下为 balloonvoice::IVoicePlayListener 的实现
    /**
     *  播放进度：更新这条语音气泡的喇叭图标帧。
     *  @param key      正在播的那条语音
     *  @param playedMs 已播放的毫秒数
     *  @param totalMs  总毫秒数
     */
    virtual void OnVoicePlayProgress(const balloonvoice::VoicePlayKey& key, int64_t playedMs,
                                     int64_t totalMs) override;
    /**
     *  播放结束：气泡退出播放中；播到结尾时接着播紧挨着的下一条未听的语音。
     *  @param key    结束的那条语音
     *  @param reason 取值见 balloonvoice::VoicePlayEnd
     */
    virtual void OnVoicePlayEnded(const balloonvoice::VoicePlayKey& key, int reason) override;
    // 按 FlamingoMedia.dll 是否已加载显示或隐藏工具栏的「语音消息」按钮；显隐变化后对按钮所在的行重排
    void ApplyVoiceButtonVisibility();

    balloonchatwnd::ChatMessage MakeGroupMergedMessage(const net::MessageNotify& m, bool fromMe,
                                                       balloonchatwnd::ChatMessageList* list);

    /**
     *  用一张群接龙卡片（msgtype=11）构造卡片气泡：本人发出的画在右侧（同本端文字气泡），别人
     *  发出的画在左侧并带发送人名与头像；卡片内容由 ApplyRelayCard 填写，接龙里的名字按本群
     *  群名片、通讯录解析（RelayNameResolver）。历史消息与实时推送共用。
     *  顺带登记卡片的截止时间，到时把卡片重画为已截止（见 TrackRelayDeadline）。
     *    m：      消息解析结果，m_relayContent 为 content 原文（历史里已合并名单与状态）。
     *    fromMe： 发送方是否本人。
     *    list：   消息列表控件，供本端气泡解析引用缩略图路径；可为 NULL。
     *  @return 气泡数据，由调用方追加进列表
     */
    balloonchatwnd::ChatMessage MakeGroupRelayMessage(const net::MessageNotify& m, bool fromMe,
                                                      balloonchatwnd::ChatMessageList* list);

    /**
     *  登记一张接龙卡片的截止时间：卡片还在进行中且有截止时间时记进 m_relayDeadlines，并重新
     *  安排截止定时器；否则把它从登记表里去掉。卡片每次构造或重填之后调用。
     *    card：卡片气泡数据（kind 不是 kMsgRelay 时什么都不做）。
     */
    void TrackRelayDeadline(const balloonchatwnd::ChatMessage& card);

    /**
     *  按 m_relayDeadlines 里最早的截止时间安排一次性定时器 kRelayExpiryTimerId；登记表为空时
     *  关掉定时器。离截止时间太远时最多等 kRelayExpiryMaxDelayMs 再检查一次，免得本机时间被
     *  调整之后错过。
     */
    void ScheduleRelayExpiryTimer();

    /**
     *  截止定时器到期：把已到截止时间的卡片按当前时间重填（显示为已截止、按钮消失），从登记表
     *  里去掉，再安排下一次。列表里已经找不到的卡片（重新加载了历史）一并去掉。
     */
    void OnRelayExpiryTimer();

    /**
     *  把列表里所有接龙卡片按 msgid 重新填一遍（成员表到达之后调用：历史可能先于成员表渲染，
     *  卡片上的名字当时还是「用户<id>」）。
     */
    void RefreshAllRelayCards();

    /**
     *  接龙卡片上名字的解析：按本群群名片、通讯录依次查找，与气泡上的发送人名同一套
     *  （ResolveSender），查不到时 ResolveSender 已经写成「用户<id>」。嵌套类可以访问
     *  本窗口的私有成员；生命周期不长于所引用的窗口，只在构造、刷新卡片的调用期间存在。
     */
    class RelayNameResolver : public balloonchatwnd::IRelayNameResolver
    {
    public:
        /**
         *  @param wnd 所属群聊窗；本对象只保存引用，不持有
         */
        explicit RelayNameResolver(const CBalloonGroupChatWindow& wnd)
            : m_wnd(wnd)
        {
        }

        /**
         *  取某个用户在本群接龙卡片上显示的名字。
         *  @param userid 用户 userid
         *  @return 显示名，保证非空
         */
        CString ResolveName(int64_t userid) const override
        {
            return m_wnd.ResolveSender(userid).m_name;
        }

    private:
        const CBalloonGroupChatWindow& m_wnd;   // 所属群聊窗，引用，不持有
    };

    /**
     *  把收到 / 历史里的一条群图片消息渲染成图片气泡：缓存命中直接显原图，否则插
     *  "下载中"气泡并触发原图下载（结果按 msgid 回投 OnImageDownloaded/Failed）。
     *    list：消息列表控件；fromMe：是否本人所发（决定左右与头像）；notify：消息解析结果。
     */
    void RenderIncomingGroupImage(balloonchatwnd::ChatMessageList* list,
                                  bool fromMe, const net::MessageNotify& notify);

    /**
     *  把收到 / 历史里的一条群文件消息（msgtype=3，转发进群的文件）渲染成文件气泡：本地
     *  已有完整文件时显示完成态，否则显示「未下载」并给一个「下载」按钮，不自动下载。
     *    list：消息列表控件，可为 NULL（直接返回）；fromMe：是否本人所发（决定左右与头像）；
     *    notify：消息解析结果。
     */
    void RenderIncomingGroupFile(balloonchatwnd::ChatMessageList* list,
                                 bool fromMe, const net::MessageNotify& notify);

    /**
     *  把收到 / 历史里的一条群语音消息（msgtype=4）渲染成语音气泡（语音消息第 3d-3 步）：本机缓存里有这段语音时
     *  直接就绪，否则显示下载中并在后台下载（方案 V5）；别人发来且未听的画红点（方案 V7）。
     *    list：消息列表控件，可为 NULL（直接返回）；fromMe：是否本人所发（决定左右、头像与发送人名）；
     *    notify：消息解析结果；played：本条所在批次的已听查询结果（QueryVoicePlayed 填好的）。
     */
    void RenderIncomingGroupVoice(balloonchatwnd::ChatMessageList* list, bool fromMe,
                                  const net::MessageNotify& notify, const balloonchatwnd::VoicePlayedQuery& played);

    /**
     *  发起截图：模态弹出进程内全屏截图浮层。复制 / 保存由浮层内部完成；「发送」立即
     *  把截图作为群图片消息发出，「发送到对话框」把截图内联插入输入框（见下两个回调）。
     *  「截图时隐藏当前窗口」开启时（CUserConfig::IsScreenshotHideWindow）本窗口不出现在截图里。
     *    fullScreen：true=选区直接为鼠标所在的整块显示器（截取全屏）。
     */
    void DoScreenshot(bool fullScreen);

    /**
     *  在剪刀与 ▾ 下方弹出截图选项菜单（见 ChatScreenshotMenu.h），按选择发起截图或切换
     *  「截图时隐藏当前窗口」并存盘。须在 DUI 鼠标事件之外调用（由 kDoScreenshotMsg 驱动）。
     */
    void OpenScreenshotMenu();

    /**
     *  按当前的截图快捷键刷新剪刀按钮的悬停提示：「截图（Alt+Z）」，快捷键停用时为「截图」。
     *  窗口建好时与每次截图、弹出截图菜单时调用，设置页改了快捷键之后下一次就会更新。
     */
    void RefreshScreenshotTip();

    // 截图「发送」回调（C 风格，ctx=本窗口 this）：把临时 PNG 作为群图片立即发出。
    static void OnScreenshotSend(void* ctx, const wchar_t* pngPath);
    // 截图「发送到对话框」回调：把临时 PNG 内联插入输入框，并记入 m_pendingShotPaths。
    static void OnScreenshotStage(void* ctx, const wchar_t* pngPath);

    // 输入框「插入图片」回调（ChatInputRichEdit::StageImageFn，ctx=本窗口 this）：粘贴或
    // 拖入的聊天内容里有图片时，转给 StageImageInInput 登记源文件并插入。
    //   返回：插入成功返回 true。
    static bool StageImageThunk(void* ctx, const std::wstring& path);

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
     *  执行 m_pendingLocateEntryId 记下的定位（库已拉全时调用）：重置分类与搜索、翻页、
     *  设高亮并重渲染，再把那一行滚动到可见。没有待定位的条目时什么也不做。
     */
    void ApplyPendingFileLocate();

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
     *    uploadIdOut：[出参，可为 NULL] 入队成功时写入这一笔的上传 id（2026-10-05 起，发送录像时据此登记）。
     *  @return 确实入队返回 true；调试入口无群绑定、或路径为空返回 false。
     */
    bool UploadGroupFile(const std::wstring& path, int* uploadIdOut = NULL);

    /**
     *  宿主「发送到会话」发来的文件上传成功后，在群里发一条文件消息（2026-10-05）：列表末尾插一条本人的文件气泡，
     *  再经网络层发出（msgtype 3，内容为这个群文件的 fileid、文件名、大小、md5）。
     *    post：上传成功的群文件
     */
    void SendUploadedFileMessage(const balloonchatwnd::GroupFileChatPost& post);

    /**
     *  经网络层发出一条群文件消息（msgtype 3），并登记回执成功后的会话列表冒泡（摘要「[文件]」）。首次发送与重新发送共用。
     *    clientMsgId：客户端消息 ID（UTF-8）；重发时沿用原来的，被服务端拒绝的消息没有入库，不会重复
     *    fileId / fileNameUtf8 / sizeBytes / md5：群文件的文件 id、文件名（UTF-8）、字节数与内容 MD5
     */
    void SendGroupFileMessage(const std::string& clientMsgId, const std::string& fileId, const std::string& fileNameUtf8,
                              long long sizeBytes, const std::string& md5);

    /**
     *  重发一条被服务端拒绝的本人文件消息（2026-10-05）：气泡清掉失败标记、回到已完成，再用原 clientMsgId 重发文件消息，
     *  不再上传。气泡不是本人发送失败的那条时什么都不做。
     *    d：文件气泡的按钮负载（clientMsgId、fileId、文件名、大小、md5）
     */
    void ResendGroupFileMessage(const balloonchatwnd::ChatFileActionData& d);

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
     *  IChatDropSink 覆写：从聊天记录里拖出来的内容（文字、表情、图片）被放进了发送框。
     *  插入输入框当前光标处，不直接发送；随后把本窗口提到前台、焦点交给输入框，方便接着
     *  编辑（拖放的源头多半是另一个聊天窗）。
     *    segs：聊天内容片段，非空。
     */
    void OnChatContentDropped(const std::vector<balloonchatwnd::ChatClipSegment>& segs) override;

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
     *  kDoScreenshotMsg 处理：在 DUI 鼠标事件之外发起截图（DoScreenshot）或弹出截图选项菜单
     *  （OpenScreenshotMenu），避免模态截图浮层或菜单在鼠标事件派发栈内运行导致的捕获 / 重入问题。
     *  wParam 为 balloonchatwnd::ChatScreenshotRequest。
     */
    LRESULT OnDoScreenshot(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kOpenRecordMenuMsg 处理：工具栏「屏幕录制」被点后，在按钮下方弹出录制菜单（开始录制 / 停止录制、录制管理），
     *  按选择交给屏幕共享中心（docs/语音字幕方案.md 第 2 节第 10 条）。
     */
    LRESULT OnOpenRecordMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  按当前全局发送模式刷新输入区左下角提示文字与输入框 placeholder。
     */
    void RefreshSendModeUi();

    /**
     *  撤回提示行「编辑」的到期处理（docs/撤回后重新编辑方案.md §3.3）：去掉已到期的「编辑」，再按最早的到期时刻
     *  重新定 kRecallEditTimerId；一个都不剩时停掉定时器。收到本人撤回、拉完历史、定时器触发时调用。
     */
    void RefreshRecallEditTimer();

    /**
     *  按仅通知模式开关与本人角色切换输入区（docs/群管理接入方案.md §14）：不能发言时把输入框、工具栏换成灰字提示行，
     *  收起待发的引用并把焦点移开；能发言时恢复。状态没变时什么都不做。SetMuteAll、SetMyRole、InitUi 时调用。
     */
    void ApplyNotifyOnlyState();

    /**
     *  本人此刻是不是受仅通知模式限制（开关开着、本人不是群主或管理员）。发送被拒时据此选择提示文字。
     */
    bool IsNotifyOnlyForMe() const;

    /**
     *  按 m_myRole 设置右上角"群管理"齿轮按钮(kGIdHdrAdmin)的显隐：仅群主 /
     *  管理员显示，普通成员隐藏。窗口已就绪时一并重排头部并重绘。
     */
    void ApplyAdminGearVisibility();

    /**
     *  把全部已知成员的头像灰彩告知消息列表（离线用户头像置灰，2026-10-03）：别人的气泡按
     *  m_memberPresence 里记下的在线状态（离线置灰、查不到不置灰），本人的气泡按本端连接态。
     *  窗口尚未建好（取不到消息列表）时安全返回。InitUi 末尾与 ApplyConnectionState 调用；
     *  单个成员的状态变化走 SetMemberPresence，只更新那一个人。
     */
    void ApplyAvatarGrayToMessageList();

    /**
     *  打开字体设置浮层 —— 点击工具栏「AF」按钮时调用。锚定在按钮上方，面板内
     *  任意设置变化即经 OnFontChanged 回调更新当前字体（影响后续发送 + 输入框
     *  所见即所得）。
     */
    void OpenFontPanel();

    /**
     *  点了工具栏的「发起接龙」按钮：打开（或前置）本群的「发起接龙」窗口。调试入口（没有真实群）
     *  下什么都不做，只记日志。
     */
    void OpenRelayCompose();

    // 字体设置变化回调（C 风格函数指针，userdata 为本窗口 this）：更新当前字体、
    // 应用到输入框，并存为本群的自定义字体（账号配置，重启后仍然有效）。
    static void OnFontChanged(void* userdata, const net::TextFont& font);

    // 字体浮层「使用默认字体」回调（userdata 为本窗口 this）：删掉本群的自定义
    // 字体、改用全局默认，应用到输入框并存盘，浮层随之回显默认字体。
    static void OnUseDefaultFont(void* userdata);

    /**
     *  取本群实际生效的聊天字体：本群自定义 > 全局默认 > 未设置（见
     *  Session/ChatFontPrefs.h）。调试入口（没有登录账号或未绑定群）下恒为未设置。
     */
    net::TextFont ResolveChatFont() const;

    /**
     *  把 m_currentFont 应用到输入框：设置过就换成该字体，未设置就恢复输入框的默认
     *  外观。负责替换并释放 m_hInputFont。
     */
    void ApplyCurrentFontToInput();

    /**
     *  把字体偏好存盘（写回账号配置）。调试入口下没有账号，什么都不做。
     */
    void SaveChatFontPrefs();

    /**
     *  打开表情选择面板 —— 点击工具栏"表情"按钮时调用。锚定在表情按钮上方，
     *  弹出 程序目录/Face 图片表情浮层，选中某表情经回调以"带 faceId 标记的内联图"
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
    // 以下八条都是本窗口投给自己的私有消息，取 WM_APP 段而不是公共的 WM_USER 段。
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
    // 2026-10-05 起 wParam 为 balloonchatwnd::ChatScreenshotRequest：普通截图、截取全屏，
    // 或弹出 ▾ 截图选项菜单（菜单同样不能在鼠标事件里弹）。
    static const UINT kDoScreenshotMsg = WM_APP + 0x53;

    // 自定义消息：工具栏「屏幕录制」被点时投递，延后到鼠标事件之外弹出录制菜单（菜单不能在鼠标事件里弹）。
    // 0x55～0x57 已有用途，取 0x58
    static const UINT kOpenRecordMenuMsg = WM_APP + 0x58;

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

    // 自定义消息：备忘卡片上的「修改」「删除」、双击、右键先投给自己，等当前的控件通知处理完再执行。
    // 打开对话框或弹菜单期间备忘簿若有变化会重建卡片，发出通知的那个控件会被销毁。wParam 为 MemoCardAction。
    static const UINT kMemoCardActionMsg = WM_APP + 0x57;

    // 自定义消息：入群必读弹窗里点了「我已阅读」，由弹窗投给本窗口（弹窗的宿主是本窗口），本窗口发送确认请求
    static const UINT kMustReadConfirmMsg = WM_APP + 0x59;

    // 自定义消息：输入区「点此阅读」提示行或必读卡片被点时投给自己，等当前的控件通知处理完再弹出必读公告
    static const UINT kMustReadShowMsg = WM_APP + 0x5A;

    // 自定义消息：问答卡片上的按钮或「＋ 新建问答」被点时投给自己，等当前的控件通知处理完再执行（打开对话框或
    // 重建卡片会销毁发出通知的控件）。wParam 为 QaCardAction
    static const UINT kQaCardActionMsg = WM_APP + 0x5B;

    // 自定义消息：问答编辑窗点了「保存」，把结果投给本窗口，lParam 为堆上的 balloonchatwnd::QaEditResult*
    static const UINT kQaEditResultMsg = WM_APP + 0x5C;

    // 自定义消息：成员页签机器人区域里的机器人行或「管理」被点时投给自己，等当前的控件通知处理完再弹资料卡或打开
    // 群机器人管理窗（弹框期间 2801 到达会重建这些行，docs/机器人方案.md §4.2）。wParam 为 GroupBotAction，lParam 为行号
    static const UINT kGroupBotActionMsg = WM_APP + 0x5D;

    // 机器人区域的操作，作为 kGroupBotActionMsg 的 wParam
    enum GroupBotAction
    {
        //点了某个机器人：弹出资料卡（简介、用法、来源），lParam 为行号
        kGroupBotActionCard   = 1,
        //2 曾是点了「管理」打开群机器人管理窗；2026-10-08 起入口移到群管理窗口，该取值作废、不再复用
    };

    // 问答面板上的操作，作为 kQaCardActionMsg 的 wParam
    enum QaCardAction
    {
        kQaCardActionSend   = 1,   //「发到群里」：把问答以文字消息发到本群
        kQaCardActionEdit   = 2,   //「修改」：打开编辑窗
        kQaCardActionDelete = 3,   //「删除」：确认后删除
        kQaCardActionAdd    = 4,   //「＋ 新建问答」：打开空白的编辑窗
    };

    // 备忘卡片上的操作，作为 kMemoCardActionMsg 的 wParam
    enum MemoCardAction
    {
        kMemoCardActionOpen   = 1,   //「修改」或双击：群主、管理员打开修改，普通成员打开查看
        kMemoCardActionDelete = 2,   //「删除」：确认后删除
        kMemoCardActionMenu   = 3,   //右键：在光标处弹出菜单
    };

    std::vector<int64_t> m_memoCardIds;       // 各备忘卡片对应的备忘 ID，下标即卡片序号；RenderMemoPanel 时整体替换
    int64_t              m_pendingMemoCardId; // 投出 kMemoCardActionMsg 时记下的备忘 ID，处理时取出并清零

    // 点群公告卡片时打开的公告详情窗（与主面板的新公告提示窗是同一个类，宿主是本窗口：窗口居中到本窗口，
    // 「查看全部」发回本窗口切到公告页签）。第一次点卡片时创建，本窗口销毁时一并销毁
    CBalloonAnnouncePopupDlg m_announceCardDlg;

    //---- 入群必读（docs/群公告接入方案.md §14）----
    bool                       m_mustReadPending;     // 本人是否尚未确认入群必读：为 true 时输入区换成「点此阅读」提示行
    std::vector<net::GroupBotInfo> m_groupBots;       // 本群的机器人（2801，含停用的与在本群关闭了的）；用于发送者名字、
                                                      // 「机器人」徽章、@ 候选与成员页签的机器人分组（docs/机器人方案.md §4.2）
    net::GroupBotCaps          m_groupBotCaps;        // 2801 附带的能力说明：是不是群主、企业是否允许群机器人与外部服务
    bool                       m_groupBotsLoaded;     // 2801 是否已到达（到达前机器人分组不显示）
    balloonchatwnd::CBotCardWindow m_botCard;         // 点了成员页签里的机器人后弹出的资料卡小窗，宿主是本窗口；
                                                      // 第一次点时创建，本窗口销毁时一并关闭
    int64_t                    m_mustReadId;          // 本群被设为入群必读的公告ID；0 表示没有设置
    bool                       m_mustReadHasItem;     // 是否已取到必读公告的内容（m_mustReadItem 有效）
    net::GroupAnnouncementItem m_mustReadItem;        // 必读公告的内容，弹窗显示它；随每次 1335 应答更新
    bool                       m_mustReadPopupShown;  // 本窗口打开后是否已自动弹过必读公告（只自动弹一次）
    CBalloonAnnouncePopupDlg   m_mustReadDlg;         // 入群必读弹窗：宿主是本窗口，第一次弹出时创建，本窗口销毁时一并销毁

    //---- 群问答（docs/群问答方案.md）----
    std::vector<net::GroupQaItem> m_qaItems;          // 本群问答（本群问答在前、企业问答在后），随每次 1336 应答整体替换
    int                           m_qaState;          // 面板加载状态，取值见 balloonchatwnd::QaPanelState
    bool                          m_qaRequested;      // 本窗口是否拉过问答：第一次切到「问答」Tab 时才拉，拉过之后 1339 推送才重拉
    std::vector<size_t>           m_qaShown;          // 各问答卡片对应 m_qaItems 的下标，下标即卡片序号；RenderQaPanel 时整体替换
    int                           m_qaRenderedWidth;  // 上次画卡片时视口的宽度（像素）；宽度变了要按新宽度重新量卡片高度
    net::GroupQaItem              m_pendingQaItem;    // 投出 kQaCardActionMsg 时记下的那条问答，处理时取出
    balloonchatwnd::CGroupQaEditDialog m_qaEditDlg;   // 「新建问答」「修改问答」对话框，宿主是本窗口

    HWND    m_hMainDlg;     // 主面板窗口句柄（为 NULL 时关闭不退出程序）
    int     m_replyIndex;   // mock 自动回复轮转下标（仅无群 id 的调试入口下用）
    // 当前连接态（ConnectionUiState：0=在线 1=正在连接 2=离线）。由 ApplyConnectionState
    // 写入，发送路径读它做门禁。默认按"在线"起步 —— 窗口能被打开说明此刻本就在线。
    int     m_connUiState;

    int64_t m_groupId;      // 本群 id；0 表示未绑定（调试入口，走 mock）
    int     m_myRole;       // 本人在本群角色（0成员 1管理员 2群主）；决定"群管理"齿轮显隐，默认成员
    int     m_atAllScope;   // 本群「谁可以 @ 全体」（0 仅群主和管理员 1 所有人）；与 m_myRole 一起决定 @ 浮层有没有「所有人」，默认 0
    int     m_muteAll;      // 本群的仅通知模式开关（原称全员禁言）：1 开 0 关，由主面板经 SetMuteAll 推来
    int     m_notifyOnlyShown;   // 输入区当前的状态，取值见 balloonchatwnd::GroupInputBlock（0 可发言、1 仅通知、2 待确认入群必读），-1 表示还没应用过（ApplyNotifyOnlyState 用来去重）
    CString m_groupName;    // 群名（窗口标题）
    HWND    m_hostPanel;    // 主面板 HWND，发消息后经它通知主面板冒泡群会话；0 表示调试入口
    long long m_pendingLocateMsgid;   // 待定定位的目标 msgid（历史在途时暂存，渲染后补定位）；0=无
    // 本群当前被 Pin 的消息条数，驱动头部下方那条「已 Pin N 条」提示带的显隐与文字。
    // 开窗时为 0（提示带隐藏），随后服务端返回的被 Pin 消息全集经 SetPinnedCount 校准；
    // 之后每收到一条 Pin / 取消 Pin 推送由 AdjustPinnedCount 就地 ±1。窗口级、不落盘。
    int     m_pinnedCount;

    // 屏幕共享横幅当前对应的那一场共享（RefreshScreenShareBanner 填写），点横幅时据此加入。
    // 横幅隐藏时房间号为空；本人发起的共享横幅只告知、点击不加入。
    std::string m_shareBannerRoomId;     // 房间号
    CString     m_shareBannerHostName;   // 发起人显示名，用作观看窗标题
    bool        m_shareBannerSelfHost;   // 进行中的共享是不是本人发起的
    bool        m_shareBannerForced;     // 进行中的共享是不是强制模式（第 13 期），点横幅加入时先确认
    // 群通话横幅当前对应的那一场（RefreshGroupCallBanner 填写），点横幅时据此加入；没有进行中的群通话时为空
    std::string m_callBannerRoomId;
    // 群通话选人框（发起与通话中邀请共用），首次使用时创建；关窗时随窗口销毁
    std::unique_ptr<CCreateGroupDlg> m_callPicker;
    // 群通话选人框的结果接收方（定义在 .cpp 里的小类，把结果转给 OnGroupCallMembersPicked），首次使用时创建
    std::unique_ptr<GroupCallPickSink> m_callPickSink;
    // 当前选人框的用途：发起时为媒体类型（1 群语音、2 群视频），通话中邀请时为 0
    int         m_callPickMedia;
    // 群通话窗请求过「邀请成员」、当时成员表还没加载：成员表到达后补开选人框（OnGroupMembersArrived 处理后清除）
    bool        m_callInvitePending;
    // 上面那次请求要排除的人（已加入或振铃中），m_callInvitePending 为 true 时有效
    std::vector<int64_t> m_callInviteExclude;
    // 选人结果的接收方要调私有的 OnGroupCallMembersPicked
    friend class GroupCallPickSink;

    // 本会话历史的向上翻页状态（最早已加载 msgid / 还有没有更早的 / 是否在途）。
    // 判定逻辑在 ChatHistoryPaging（纯逻辑、可单测），本窗口只持有状态并按它的
    // 结论发请求。m_loading 同时用来区分应答是首屏还是翻页 —— 服务端的应答里
    // 不回显 before，客户端只能自己记。
    balloonchatwnd::HistoryPagingState m_paging;

    // 首屏历史渲染之前发出的 Markdown 文档（从 Markdown 编辑器发送时窗口刚打开）。随窗口存续，
    // 首屏渲染完、这些文档都确认入库后据它重新拉一次最新一页
    balloonchatwnd::MarkdownEarlySendTracker m_mdEarlySends;

    // 打开计时：CreateAndShow 开始时重置，群成员到达、首屏历史到达与渲染完成时读取，
    // 得出从打开到看到历史消息各段花了多久（含两次等待服务端应答），写进日志供性能排查。
    PerfStopWatch m_openStopWatch;

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
    // 渲染收到的群消息气泡时据发送方 userid 取真实昵称/头像。与帖子群的群成员面板共用同一结构与同一套算法
    // （GroupMemberPanelKit，docs/帖子群方案.md 14.3），两处的显示名与头像口径一致。
    typedef balloonchatwnd::GroupMemberRowInfo MemberInfo;
    std::map<int64_t, MemberInfo> m_memberInfo;

    // 各成员的在线状态（键为 userid，值同 presence_type），由主面板经 SetMemberPresence 写入。
    // 成员侧栏每次重建时按它给各行设徽标；查不到的成员不画徽标。随窗口存在，关窗即丢弃
    std::map<int64_t, int> m_memberPresence;

    // 最近一次到达的本群成员（1304 应答的 members），按服务端返回的顺序。切换「平铺 / 按部门」视图、
    // 部门数组更新时据它重建成员列表；随窗口存在，关窗即丢弃
    std::vector<net::GroupMemberItem> m_lastMembers;
    // 企业部门数组（主面板经 SetOrgDepartments 推入），部门树视图用；没推过时为空，部门树里全部成员都在「未分配部门」下
    std::vector<net::DepartmentItem> m_orgDepartments;
    // 「成员」页签当前是否为部门树视图；建窗时取自账号配置 [GroupDlg] MemberView，点「按部门 / 平铺」切换按钮时切换并存盘
    bool m_memberTreeView;

    /**
     *  按 m_lastMembers 与当前视图重建成员侧栏（经 GroupMemberPanelKit 的 FillGroupMemberList，与帖子群的群成员面板
     *  共用）：平铺视图按群角色排序挂成员行；部门树视图按 BuildGroupMemberDeptTree 挂部门标题行与成员行。重建后按
     *  搜索框里的关键词重新过滤、重新排版。
     */
    void RebuildMemberRows();

    /**
     *  点了「按部门 / 平铺」切换按钮：切换成员列表的视图，记进账号配置并存盘，更新按钮的高亮状态与悬停提示，再重建成员列表。
     */
    void ToggleMemberView();

    // 成员表里查不到的发送者（已退群、被移出）在通讯录里查到的显示信息，或"通讯录里也没有"的
    // 结论。一页历史里同一个人往往发很多条，缓存起来只问主面板一次；成员表重新到达时清空
    struct NonMemberSender
    {
        bool                              m_found;       // 通讯录里有没有此人
        balloonchatwnd::GroupSenderRender m_render;      // 查到时的显示信息（昵称、字形、底色、真图路径）
        int                               m_facetype;    // 头像类型，真图下载完成后据它回填路径
        std::string                       m_customface;  // 头像标识（UTF-8），真图为 fileid
        NonMemberSender() : m_found(false), m_facetype(0) {}
    };
    std::map<int64_t, NonMemberSender> m_nonMemberSenders;

    // 进行中、有截止时间的接龙卡片：卡片 msgid → 截止时间（Unix 毫秒）。TrackRelayDeadline 维护，
    // 截止定时器据它找出该重画为已截止的卡片；到期处理过、或卡片已结束的条目即被移除。随窗口存在
    std::map<long long, long long> m_relayDeadlines;

    // 本群的「发起接龙」窗口：第一次点工具栏接龙按钮时创建，之后复用；关闭本窗口时一并销毁。
    // 回执经 OnSendChatAckBackfill / OnSendChatAckFailed 转给它
    std::unique_ptr<CRelayComposeDlg> m_relayComposeDlg;

    /**
     *  按 userid 解析发送者在气泡里的显示信息：先查成员表 m_memberInfo，成员表里没有（已退群、
     *  被移出）再经主面板查通讯录，两处都没有或字段为空时按 ResolveGroupSender 的规则回退
     *  （"用户<id>"、首字、按 userid 取的底色）。渲染对方的历史 / 实时 / 图片消息时共用这一处。
     *    userId：发送者 userid。
     *  @return 名字与字形保证非空的显示信息。
     */
    balloonchatwnd::GroupSenderRender ResolveSender(int64_t userId) const;

    /**
     *  同 ResolveSender，另外照顾机器人消息：成员表、本群机器人列表、通讯录都查不到发送者，而消息带机器人标记时，
     *  按标记里的名字显示并标为机器人（机器人已删除或不再用于本群，docs/机器人方案.md §3.2）。
     *  渲染对方的文字消息（历史与实时）时调用。
     *    userId：     发送者 userid。
     *    botId：      消息的 content.bot.id；0 表示不是机器人消息，此时与 ResolveSender 相同。
     *    botNameUtf8：content.bot.name（UTF-8）。
     *  @return 名字与字形保证非空的显示信息。
     */
    balloonchatwnd::GroupSenderRender ResolveSenderWithBotMark(int64_t userId, int64_t botId,
                                                               const std::string& botNameUtf8) const;

    /**
     *  切换字幕模式：头部「字幕模式」按钮被点时调用。字幕由主面板持有（不随本窗口关闭），这里经
     *  FMG_MSG_TOGGLE_SUBTITLE 同步请主面板切换，再按结果高亮按钮；群聊窗本身保持不动。
     *  调试入口（没有主面板）下什么都不做。副作用：写一行日志。
     */
    void ToggleSubtitleMode();

    /**
     *  ApplyGroupSendersToList 的查询回调：在成员表里查一名成员的显示信息。
     *    ctx：   本窗口（CBalloonGroupChatWindow*）。
     *    userId：要查的成员 userid。
     *    out：   [出参] 查到时填入该成员的显示信息。
     *  @return 在成员表里返回 true。
     */
    static bool LookupMemberSender(void* ctx, int64_t userId,
                                   balloonchatwnd::GroupSenderRender& out);

    /**
     *  查通讯录的回调：成员表里查不到的发送者，经 FMG_MSG_QUERY_CONTACT_BRIEF 同步问主面板，取账号
     *  昵称与头像。结果（含"查不到"）缓存在 m_nonMemberSenders 里。没有宿主主面板（调试入口）时
     *  一律返回 false。
     *    ctx：   本窗口（CBalloonGroupChatWindow*）。
     *    userId：要查的 userid。
     *    out：   [出参] 查到时填入显示信息。
     *  @return 通讯录里有此人返回 true。
     */
    static bool LookupContactSender(void* ctx, int64_t userId,
                                    balloonchatwnd::GroupSenderRender& out);

    /**
     *  ApplyGroupSendersToList 与 ResolveSender 共用的查询回调：先成员表、再通讯录
     *  （balloonchatwnd::LookupGroupSenderChain）。
     *    ctx：   本窗口（CBalloonGroupChatWindow*）。
     *    userId：要查的 userid。
     *    out：   [出参] 查到时填入显示信息。
     *  @return 任一处查到返回 true。
     */
    static bool LookupSenderChain(void* ctx, int64_t userId,
                                  balloonchatwnd::GroupSenderRender& out);

    /**
     *  查本群机器人列表的回调：发送者是机器人账号时填入机器人的名字、头像与「机器人」标记。
     *  机器人不在群成员表里（docs/机器人方案.md §3.3），由 LookupSenderChain 在成员表之前调用。
     *    ctx：   本窗口（CBalloonGroupChatWindow*）。
     *    userId：要查的 userid。
     *    out：   [出参] 查到时填入显示信息。
     *  @return 是本群的机器人返回 true。
     */
    static bool LookupBotSender(void* ctx, int64_t userId, balloonchatwnd::GroupSenderRender& out);

    /**
     *  企业打开了机器人时拉一次本群机器人（2801）。打开窗口、收到 2820、功能开关变化时调用。
     */
    void RequestGroupBots();

    /**
     *  按 m_groupBots 重建成员页签顶部的机器人区域：标题「机器人 N」、群主才有的「管理」、每个机器人一行。
     *  没有机器人或企业关闭了机器人时整块隐藏。显隐变化后对成员容器 ForceLayout（UI 约定第 5 条）。
     */
    void RefreshBotMemberSection();


    // 用户在输入框里插入过的 @ 记录（按插入顺序）。发送时<u>不</u>直接用它，而是以
    // 输入框最终文本为准调 RescanMentions 重扫一遍 —— 插入之后用户还可能把
    // "@张三" 删掉一半或在中间插字，插入时记的偏移就不准了。扫不到的记录自动作废。
    // 每次 DoSend 全部发完后清空。见 docs/群聊@提醒方案.md §6。
    std::vector<balloonchatwnd::MentionCandidate> m_pendingMentions;

    // 发送后待冒泡的队列：发送时按 clientMsgId 登记，回执成功时取出通知主面板，失败时丢弃。
    // 生命周期随窗口；关窗后迟到的回执由主面板直接忽略（窗口已不在映射里）。
    balloonchatwnd::ChatSendBumpQueue m_sendBumps;

    // 「@ 成员选择」浮层。惰性创建、跨多次 @ 复用。
    balloonchatwnd::CGroupAtDropdown m_atDropdown;

    // 当前正在编辑的那个 @ 片段里，'@' 在输入框文本中的字符下标；-1 表示当前
    // 不在 @ 状态。插入成员时要用它定位"从哪儿开始替换"。
    int m_mentionAtPos;

    // 插入输入框的内联图片（截图「发送到对话框」、粘贴、拖入）的源文件路径，按插入顺序记录。
    // 每张图的下标记在它的图片标记里，发送时按标记取对应项直接发（不抽 OLE，详见
    // StageImageInInput）。每次 DoSend 全部发完后清空。
    std::vector<std::wstring> m_pendingShotPaths;

    // 当前聊天字体（影响后续发送的群消息与输入框外观）。InitUi 时按"本群自定义 >
    // 全局默认"取得（见 ResolveChatFont），字体浮层改动后更新；m_valid 为 false 表示
    // 两者都没设过、消息不带字体。
    net::TextFont m_currentFont;
    // 输入框原本的文字色：InitUi 在应用任何聊天字体之前记下，恢复默认外观时用它。
    COLORREF m_inputDefaultTextColor;
    // 输入框（ChatInputRichEdit）当前生效的自定义字体句柄。富文本控件的
    // SetDefaultFontFromHFONT 不接管 HFONT 所有权，故由本窗口持有：每次应用新字体
    // 前先 DeleteObject 上一只再换新，析构时释放，避免 GDI 句柄泄漏。NULL=用默认字体。
    HFONT m_hInputFont;
    // 字体设置浮层（按需打开，本类持有）。
    CFontSettingPopup m_fontPopup;
    // 程序目录/Face 图片表情浮层（"表情"按钮弹出，本类持有）。
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
    // 等群文件库拉全后再定位的条目 id（ShowGroupFileEntry 在库未拉全时记下）；空串表示
    // 没有待定位的条目。定位一次后清空。
    std::string m_pendingLocateEntryId;
    // 当前以淡黄底标出的文件条目 id（最近一次定位到的那一个）；空串表示不标。RenderFilePanel
    // 建行时据它设高亮；保持到下一次定位或关窗，用户翻页、改分类后再回来仍能认出那一行。
    std::string m_highlightEntryId;

    // 正在下载的群文件：entryid → 进度百分比(0~100)。RenderFilePanel 据它给重建出来的
    // 文件行恢复"下载中"态（否则换页 / 推送触发重渲染会丢进度态）；下载结束时移除。
    std::map<std::string, int> m_downloadingFiles;
    // 正在上传的群文件跟踪器（可多笔并发）：每笔一行钉在文件列表顶部显示「上传中 NN%」+ ✕。
    // RenderFilePanel 据它在库文件行之前补出这些行；上传结束 / 取消时移除对应笔。
    balloonchatwnd::GroupFileUploadTracker m_uploadTracker;
    // 宿主「发送到会话」发来的文件（如录像）的上传登记：上传成功后还要在群里发一条文件消息（2026-10-05）
    balloonchatwnd::GroupFileChatPoster m_chatPoster;
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
    balloonchatwnd::CVoiceRecordSession m_voiceSession;   // 录音会话（语音消息第 6g 步）：持有录音条与流程状态机，InitUi 时把
                                                          // 录音条建在布局的占位里；它是语音消息中心的录音与试听通知接收方，
                                                          // 关窗与析构时取消或丢弃还没发送的录音
    std::unique_ptr<ballooncode::CCodeComposeWindow> m_codeComposeWnd;   //「发送代码」窗口；第一次打开时创建，本窗口关闭时销毁其窗口
    std::wstring m_codePasteText;   //「以代码片段发送」提示条显示时输入框里的文字；提示条收起时为空。
                                    // 之后输入框内容与它不同即视为用户继续输入，提示条随即收起
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
