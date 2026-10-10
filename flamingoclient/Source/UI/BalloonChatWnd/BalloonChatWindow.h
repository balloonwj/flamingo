/**
 *  BalloonChatWindow.h
 *
 *  新版单聊窗口 —— 基于 balloonui 的气泡式聊天窗，对照
 *  design/pc-snapshots/07-chat-single.png。
 *
 *  与旧 BalloonChat/BalloonBuddyChatDlg 的关系（已与用户确认）：旧窗口是
 *  richedit 式聊天（只读 richedit 收消息 + 可编辑 richedit 发消息），与
 *  07 的气泡式是两种范式。故新窗口独立另建：本阶段用 mock 消息数据，
 *  旧 BalloonBuddyChatDlg 保持不动、仍可用；真实消息收发的接入留作后续
 *  阶段。
 *
 *  P0 范围：骨架 —— 一个能创建 / 显示 / 关闭的空 DuiFrameWindow；真正的
 *  头部 / 消息区 / 输入区由后续 P1 阶段的 ChatWindowLayout 构建。
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
#include "EmojiPickerPopup.h"                       // 旧 Unicode emoji 浮层（保留不再挂入；成员需完整类型）
#include "FaceImagePickerPopup.h"                   // 程序目录/Face 图片表情浮层（成员需完整类型）
#include "FontSettingPopup.h"                       // 字体设置浮层（成员需完整类型；含 net::TextFont）
#include "ChatDropController.h"                     // 拖文件进窗口发送（成员需完整类型）
#include "ChatHistoryPaging.h"                      // HistoryPagingState（向上翻页，成员需完整类型）
#include "ChatMarkdown.h"                           // MarkdownEarlySendTracker（成员需完整类型）
#include "ChatEarlyTips.h"                          // EarlySystemTipTracker（成员需完整类型）
#include "ShakeRateLimiter.h"                      // 窗口抖动收发节流器（成员需完整类型）
#include "TypingIndicator.h"                        // 「正在输入」收发状态机（成员需完整类型）
#include "../../Common/Util/PerfStopWatch.h"        // 打开窗口到首屏历史渲染完成的计时（成员需完整类型）
#include "ChatMessageList.h"                           // WM_CHAT_DELETE_REQUEST（消息映射里要用到消息号）
#include "ChatSendBumpQueue.h"                         // 发送后待冒泡的队列（成员需完整类型）
#include "VoiceRecordSession.h"                        // 录音会话与录音条（语音消息第 3b、6g 步，成员需完整类型）
#include "../BalloonCode/CodeComposeWindow.h"           // 「发送代码」窗口与它交回结果的消息
#include "ChatCodeMessage.h"                           // kMsgChatCodePaste（消息映射里要用到消息号）
#include <map>                                      // 密聊邀请卡片的登记 m_secretInviteCards
#include <memory>
#include <string>                                   // 发图进度 / 结果回投用 clientMsgId
#include <vector>                                   // 待发送截图源路径表 m_pendingShotPaths

class CFlamingoClient;

//轻量提示条（拖放里"这些没发出去"的汇总提示）。只按指针持有，前向声明即可。
namespace balloonwjui { class DuiToast; }
//密聊图片的大图浮层（docs/密聊方案.md 4.10）。只按指针持有，前向声明即可。
namespace balloonchatwnd { class SecretImageOverlay; }

//v2 单聊消息推送 / 历史消息解析结果（由 BalloonMainPanel 路由进来）。
namespace net { struct MessageNotify; struct SyncMessagesResult; }
//v2 单聊文件夹传输的两条推送（同样由 BalloonMainPanel 路由进来）。
namespace net { struct FolderInviteNotify; struct FolderStateNotify; }
//文件夹传输的本端事件投递载体（定义在 FolderTransfer/FolderTransferManager.h）。只按
//const 引用传参，前向声明即可 —— 那个头文件把整套会话管理都带进来，不该传染给使用方。
namespace foldertransfer { struct FolderScanNotice; struct FolderBindNotice;
                           struct FolderProgressNotice; struct FolderResultNotice; }
//一条会话消息（结构定义在 ChatMockData.h）。ChatFileActionData 定义在 ChatFileAction.h，
//此处只按 const 引用传参，前向声明即可——该头文件要求先引入 ATL/WTL，直接 include 会
//把这个约束传染给所有包含本头文件的编译单元。
namespace balloonchatwnd { struct ChatMessage; class ChatMessageList; struct ChatFileActionData;
                           struct ChatFolderActionData; struct VoicePlayedQuery; }

/**
 *  CBalloonChatWindow
 *
 *  新版气泡式单聊窗口。继承 balloonwjui::DuiFrameWindow —— 由它提供无
 *  边框自绘窗体、标题栏与 min/max/close 按钮。非模态窗口，消息由进程
 *  主消息循环统一泵送。
 */
class CBalloonChatWindow : public balloonwjui::DuiFrameWindow,
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
    explicit CBalloonChatWindow(CFlamingoClient* pFMGClient);
    ~CBalloonChatWindow();

    /**
     *  创建并显示单聊窗口。
     *    hMainDlg：主面板的窗口句柄；调试入口下关闭本窗口即退出程序，
     *              据此请求退出程序。
     */
    void CreateAndShow(HWND hMainDlg);

    /**
     *  设置本会话对端用户 userid。须在 CreateAndShow 之前调用 —— 决定窗口
     *  标题（对方昵称）与发送/接收消息的归属对端。
     *    peerUserId：对方 userid（账号ID）。
     */
    void SetPeerUserId(UINT peerUserId);

    /**
     *  设置对端显示名（昵称）。须在 CreateAndShow 之前调用 —— 用于窗口标题与
     *  头像图标首字。v2 下好友列表为空、CUserMgr 查不到昵称，故由打开方
     *  （主面板，持有通讯录缓存）直接把昵称传进来。
     *    name：对端昵称；空则窗口回退到查 CUserMgr / "用户<id>"。
     */
    void SetPeerName(LPCTSTR name);

    /**
     *  设置主面板 HWND。点击头部信息块（个性签名所在）时，单聊窗经它请求
     *  主面板打开对方的联系人资料窗。由主面板在创建窗口时传入。
     */
    void SetHostPanelHwnd(HWND hostPanel);

    /**
     *  设置对端在线状态（v2 presence_type，见 net/Protocol.h），刷新头部状态胶囊。
     *  可在 CreateAndShow 前后任意时刻调用：窗口已建好则立即更新头部并重绘；未建好则
     *  暂存，InitUi 末尾再应用。主面板在打开会话时按通讯录缓存注入初值，收到 1109 推送
     *  时再调用本方法实时刷新（与联系人树/会话列表的状态点同源）。
     *    presence：对端 v2 在线状态。
     */
    void SetPeerPresence(int presence);

    /**
     *  设置对端个性签名，刷新头部签名行。同样可在 CreateAndShow 前后调用。空串表示无签名
     *  （头部不画签名行）。与 SetPeerPresence 配套，让头部信息块完全取真实数据、不再用 mock。
     *    signature：对端个性签名（可空）。
     */
    void SetPeerSignature(const CString& signature);

    /**
     *  设置对端头像（facetype + customface），决定标题栏 / 任务栏图标画字形头像还是回退。
     *  可在 CreateAndShow 前后调用：窗口已建则立即重建图标。打开单聊时由主面板从通讯录
     *  缓存注入；其余字段不变。
     *    facetype       对端头像类型（见 BalloonMain/AvatarSpec.h AvatarKind）。
     *    customfaceUtf8 对端头像标识（UTF-8）。
     */
    void SetPeerAvatar(int facetype, const std::string& customfaceUtf8);

    /**
     *  收到对端资料变更推送（1108）后，运行期更新已打开单聊窗的对端昵称 / 签名 / 头像。
     *  与 SetPeerName/SetPeerSignature（须在 CreateAndShow 之前调）不同，本方法用于
     *  窗口<u>已打开</u>后即时刷新：名字或头像变了则刷窗口标题 + 标题栏 / 任务栏图标，
     *  签名变了则刷头部签名行。窗口未打开时为 no-op。
     *    name      新昵称；空则不动标题 / 图标。
     *    signature 新个性签名（可空，空串表示清空头部签名行）。
     *    facetype       新头像类型。
     *    customfaceUtf8 新头像标识（UTF-8）。
     */
    void UpdatePeerInfo(LPCTSTR name, LPCTSTR signature, int facetype, const std::string& customfaceUtf8);

    /**
     *  某真图头像下载完成后由主面板广播给所有打开的单聊窗。本窗口若有发送者（对端或
     *  本人）的真图头像 customface 等于该 fileid，则重建标题栏 / 任务栏图标并刷新气泡
     *  里该发送者的头像真图。fileid 不匹配则为 no-op。
     *    fileid 刚下载完成的头像文件ID（UTF-8）。
     */
    void OnAvatarDownloaded(const std::string& fileid);

    /**
     *  取本会话对端 userid，供主面板按对端路由窗口。
     */
    UINT GetPeerUserId() const { return m_peerUserId; }

    /**
     *  某个共享的状态变了（主面板收到文件共享宿主的通知后逐窗转来）：把本会话里这个共享的卡片就地重填。
     *    shareId：共享 id；state / reason：新状态与原因（share_state 及其原因）。
     *  @return 重填了的卡片张数
     */
    int OnShareCardStateChanged(long long shareId, int state, int reason);

    /**
     *  设置头部文件共享按钮上的角标（主面板收到本会话可用共享的个数后转来，2026-10-03 起）。
     *    count：本会话里可用的共享个数；0 时角标消失。
     */
    void SetShareBadgeCount(int count);

    // 头部文件共享按钮上角标的数字（单元测试用）；没有角标或找不到按钮时为 0
    int GetShareBadgeCount();

    /**
     *  本会话的屏幕共享状态变了（主面板收到 FlamingoMedia.dll 的回报后转来）：本会话里的屏幕共享卡片按屏幕共享中心
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
     *  入群邀请卡片的状态变化（cmd_group_invite_changed_notify，由主面板改写本地库之后调用）：列表里那张卡片换成
     *  新状态并重画，同时清除「正在加入」。卡片尚未加载进当前视图时直接忽略，之后从本地库或服务端取回的已是新状态。
     *  @param msgid 卡片消息的服务器 msgid
     *  @param state 新的显示状态，取值见 group_invite_state
     *  @return 列表里找到并刷新了这张卡片返回 true
     */
    bool OnGroupInviteChanged(int64_t msgid, int state);

    /**
     *  一对一通话卡片已改写成结束状态（cmd_call_card_notify，由主面板改写本地库之后调用）：列表里那张卡片按新 content 重填。
     *  @param msgid       卡片消息的 msgid
     *  @param contentJson 改写后的卡片 content 原文
     *  @return 列表里有这张卡片并已刷新返回 true；卡片不在当前视图里返回 false
     */
    bool OnCallCardChanged(int64_t msgid, const std::string& contentJson);

    /**
     *  本窗口的消息列表里有没有 msgids 中的任何一条。按改写序号同步之后，主面板据此决定要不要
     *  重新加载最新一页：被改写的消息不在当前视图里时不打乱用户的滚动位置。
     *  @param msgids 消息 msgid
     *  @return 至少有一条正在列表里返回 true；列表未就绪返回 false
     */
    bool IsShowingAnyMessage(const std::vector<int64_t>& msgids);

    /**
     *  本人与对方的通话状态变了（主面板收到 IMediaHost::OnCallPresence 后调用，新建窗口时也调用一次）：记下媒体类型，
     *  并重填列表里还没结束的通话卡片的状态（「正在呼叫」「通话中」）。
     *  @param media 本人此刻与对方通话的媒体类型（1 语音、2 视频）；不在通话中为 0
     */
    void SetInCallMedia(int media);

    /**
     *  点「加入群聊」失败（cmd_accept_group_invite 应答 code 非 0，由主面板转来）：在列表里按邀请 id 找到那张卡片，
     *  清除「正在加入」；state 不是 balloonchatwnd::kGroupInviteStateUnchanged 时一并换成该状态。
     *  @param inviteId 应答回带的邀请 id
     *  @param state    卡片应换成的状态（balloonchatwnd::GroupInviteStateForAcceptError 的结果）
     *  @return 找到的卡片 msgid，主面板据此改写本地库；列表里没有这张卡片时返回 0
     */
    int64_t OnGroupInviteAcceptFailed(int64_t inviteId, int state);

    /**
     *  把一个本地文件按普通文件消息发给对方（录屏完成后的「发送给 某某」、转发在线直传文件时重新发送本机文件，
     *  主面板转来）。离线时提示并放弃。
     *  @param path         文件完整路径
     *  @param forceOffline true 时直接上传服务器、不试在线直传（转发出去的文件要留在服务器上）
     */
    void SendLocalFile(const std::wstring& path, bool forceOffline = false);

    /**
     *  把一张本地图片按图片消息发给对方（2026-10-07：屏幕共享标注「保存为图片」后的「发送到本会话」，主面板转来）。
     *  与选图、截图发送走同一条 SendImageFile 链路。
     *  @param path 图片完整路径
     */
    void SendLocalImage(const std::wstring& path);

    /**
     *  以普通文字消息发出一段文字（屏幕共享第 25 期：主持人「发送邀请到…」把分享链接发到选中的会话）。
     *  与输入框里点「发送」一样有乐观气泡、回执与会话冒泡，但不带输入框上方正在引用的消息，也不清掉输入框与引用条。
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
     *  把 Markdown 编辑器里的一篇文档作为 content.format = "markdown" 的文字消息发给对方（主面板转来，
     *  docs/Markdown消息方案.md 第 11.3 节）：生成卡片形式的乐观气泡、登记回执后的会话冒泡。体积与在线状态由编辑器先检查。
     *  @param sourceUtf8 源文本（UTF-8，换行为 \n），非空
     *  @return 已发出返回 true；窗口未绑定对端或源文本为空时返回 false
     */
    bool SendMarkdownDocument(const std::string& sourceUtf8);

    /**
     *  在窗口顶部居中弹出一条轻量提示，几秒后自动消失（与拖放文件的跳过提示共用同一个
     *  提示条）。主面板用它提示「已转发」这类不需要用户确认的结果。
     *    text：提示文字，调用期间有效。
     */
    void ShowToast(LPCTSTR text);

    /**
     *  某条消息被撤回：把消息区里对应的气泡就地变成居中提示行。
     *  由主面板收到 cmd_message_recalled_notify 推送后路由进来。
     *  @param msgid   被撤消息的服务器 msgid；本窗口没有该条时安静忽略
     *  @param tipText 提示文案（见 balloonchat::BuildRecallTipText）
     */
    /**
     *  在聊天区追加一条居中系统提示（kMsgTime 类型，如"对方向你发送了窗口抖动"、
     *  "对方拒绝了通话"）。本窗口内部（窗口抖动）与主面板（通话结果）都会调。
     *    text：提示文字。
     */
    void AppendSystemTip(const CString& text);

    /**
     *  请本窗口发起一次截图（主面板收到截图全局快捷键、且前台正是本窗口时调用）。
     *  只投递 kDoScreenshotMsg，等当前消息处理完再由本窗口发起，效果与点工具栏剪刀相同。
     *  @param fullScreen true=选区直接为鼠标所在的整块显示器（截取全屏）
     */
    void RequestScreenshot(bool fullScreen);

    /**
     *  某条消息被撤回：把消息区里对应的气泡就地变成居中提示行。由主面板收到 cmd_message_recalled_notify
     *  推送后路由进来。撤回者是本人、被撤的是本人发出的文字类消息时，提示行在 5 分钟内带「编辑」
     *  （docs/撤回后重新编辑方案.md），并启动到期定时器。
     *    msgid：       被撤消息的服务器 msgid；本窗口没有该条时安静忽略。
     *    tipText：     提示文案（见 balloonchat::BuildRecallTipText）。
     *    recalledByMe：撤回者是否本人。
     *    nowMs：       撤回推送到达的时间（Unix 毫秒）。
     *  @return 生成了「编辑」时返回快照 JSON，由主面板存进本机库；否则返回空串。
     */
    std::string OnMessageRecalled(long long msgid, const CString& tipText, bool recalledByMe, long long nowMs);

    /**
     *  系统设置里改了「发送消息快捷键」（BUG-119）：按新的发送键刷新左下角提示与输入框占位文字。
     */
    void OnSendModeChanged();

    /**
     *  移除本会话里已被本人删除的消息行。收到 cmd_messages_deleted_notify（单条形式）后由
     *  主面板调用；列表里没有的 msgid（尚未加载进当前视图）安静忽略。
     *    msgids：被删除的服务器 msgid
     */
    void OnMessagesDeleted(const std::vector<long long>& msgids);

    /**
     *  丢弃当前列表，重新拉取最新一页历史。按时间范围清理、或本人清空了本会话的聊天记录之后
     *  由主面板调用：被删的消息可能是列表里任意多行，逐行移除不如整页重载 —— 效果等同于重新
     *  打开本窗口。
     *  应答照常经 OnHistoryMessages 按"首屏"处理（先清空再渲染）。调试入口（没有对端）
     *  下什么都不做。
     */
    void ReloadLatestMessages();

    /**
     *  某条消息被 Pin / 取消 Pin（收到服务端推送后由主面板调用）：把列表里对应那行的
     *  图钉标记置上 / 清掉。列表里没有该 msgid（该消息尚未加载进当前视图）时安静忽略 ——
     *  本地库那份标记已经落好，下次翻历史渲染出来时自然带着。
     *    msgid： 目标消息的服务器 msgid。
     *    pinned：true 钉住、false 取消。
     */
    void OnMessagePinned(long long msgid, bool pinned);

    /**
     *  设置本会话当前被 Pin 的消息条数，并据此刷新头部信息行下方的「已 Pin N 条」
     *  提示带（0 条时整条隐藏、不占位）。
     *
     *  由主面板在收到服务端返回的被 Pin 消息全集（cmd_get_pinned_messages 应答）后
     *  调用 —— 那份应答里的 total 是<u>唯一权威口径</u>：它是服务端算出的总数，不受
     *  单次拉取上限（PIN_FETCH_LIMIT）截断影响，也不受本地库同步进度影响。
     *    count：被 Pin 的消息条数，负数按 0 处理。
     *
     *  副作用：提示带显隐变化时会补一次客户区布局并投递 kRelayoutMsg 重排。
     */
    void SetPinnedCount(int count);

    /**
     *  在当前记录的 Pin 条数上增减并刷新提示带。收到单条 Pin / 取消 Pin 推送时用它
     *  就地加减，省得为了刷新一个数字再向服务端拉一趟全集。
     *
     *  这份计数是"乐观"的：极端情况下（推送在离线期间丢失等）可能与服务端不一致，
     *  但每次重开聊天窗都会重新拉一次全集、用 SetPinnedCount 校准回来。
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
     *  发送回执失败（cmd_send_message 应答 code 非 0）：按 clientMsgId 找到那条「刚发出」的气泡，
     *  标为发送失败并在下方写原因（SendFailureText），该条不再冒泡。
     *  @param clientMsgId 回执里的客户端消息ID
     *  @param code        回执结果码，取值见 enum error_code
     *  @return 本窗口找到了这条气泡返回 true，主面板据此不再弹框；否则返回 false
     */
    bool OnSendChatAckFailed(const std::string& clientMsgId, int code);

    /**
     *  应用对方的已读位点：把本窗口里自己发出、msgid 不大于位点的消息标为已读（气泡下方
     *  「未读」变成「✓ 已读」）。位点只增不减，之后才加载进来或才回填 msgid 的消息也会
     *  按它判定（见 ChatMessageList::ApplyPeerReadPosition）。由主面板在收到
     *  cmd_message_read_notify 推送、建窗、或拿到单聊同步消息应答时调用。
     *  @param readMsgid 对方已读到的最大消息 msgid；<=0 忽略
     *  @return 本次新标为已读的气泡行数
     */
    int ApplyPeerReadPosition(long long readMsgid);

    /**
     *  收到对方发来的一条单聊消息：渲染为对方气泡。由主面板在收到
     *  cmd_message_notify 推送、按发送方 userid 找到本窗口后调用。
     *    notify：消息推送解析结果（本轮仅处理文本，m_text 为正文）。
     */
    void OnRecvMessage(const net::MessageNotify& notify);

    /**
     *  按本会话桌面字幕的状态高亮或恢复头部「字幕模式」按钮（docs/聊天字幕模式方案.md 第 1 节第 11 条）：
     *  高亮时只把图标画成蓝色，悬停提示同时换成「字幕已开启」，关着时为「字幕已关闭」。
     *  字幕由主面板持有、不随本窗口关闭；主面板在字幕开关时、以及建出本窗口后调用。窗口还没建好时什么都不做。
     *  @param active true 字幕开着（按钮高亮），false 关着
     */
    void SetSubtitleHighlight(bool active);

    /**
     *  对端的「正在输入」状态变化（收到 cmd_typing_notify 推送后由主面板路由进来）：
     *  刷新头部状态胶囊 —— 输入中显示主题色的"正在输入…"，停止则恢复按在线状态显示。
     *
     *  这条推送不可靠（服务端不落库、不重发，对端掉线时 state=0 永远不来），故本窗口
     *  内部另有过期兜底：收到"输入中"后若 kTypingExpireMs 内再无新推送，定时器会自动
     *  把提示收起，不会一直挂着。
     *    state：输入状态，取值见 net 的 enum typing_state（1 输入中、0 停止）。
     */
    void OnPeerTyping(int state);

    /**
     *  渲染本会话的历史消息：打开窗口时拉到的最近若干条。服务端按 msgid 升序
     *  （时间正序、最旧在前）返回，本函数正序遍历逐条追加（自己的在右、对方的
     *  在左），末尾滚到底。
     *  由主面板的 OnSyncMessagesArrived 路由调用。
     *    result：历史消息解析结果。
     */
    void OnHistoryMessages(const net::SyncMessagesResult& result);

    /**
     *  定位到本会话里某条消息（历史消息记录窗"定位"按钮经主面板转发到此）：滚动消息区
     *  到该 msgid 行附近。若该消息行此刻还未加载（如刚打开窗、历史仍在途），记为待定，
     *  等 OnHistoryMessages 渲染完后再补定位。
     *    msgid：目标消息的服务器 msgid。
     */
    void LocateMessage(long long msgid);

    /**
     *  更新本端"发送中"图片气泡的上传进度。由主面板在收到
     *  FMG_MSG_V2_IMAGE_SEND_PROGRESS 后按会话路由调用。
     *    clientMsgId：与发图时乐观气泡对回的客户端消息ID。
     *    sent/total： 已上传 / 总字节数（据此算百分比）。
     */
    void OnImageSendProgress(const std::string& clientMsgId, long long sent, long long total);

    /**
     *  收尾本端"发送中"图片气泡：成功置正常态、失败标"发送失败"。由主面板在
     *  收到 FMG_MSG_V2_IMAGE_SEND_RESULT 后按会话路由调用。
     *    ok：         是否上传并发出成功。
     *    clientMsgId：与发图时乐观气泡对回的客户端消息ID。
     */
    void OnImageSendResult(bool ok, const std::string& clientMsgId,
                           const std::string& fileid = std::string(),
                           const std::string& md5 = std::string());

    /**
     *  收到的某条图片下载完成：按 msgid 找到那条"下载中"气泡，填本地缓存路径并
     *  渲染。由主面板在收到 FMG_MSG_V2_IMAGE_DOWNLOAD_RESULT 后路由调用。
     *    msgid：    图片消息的服务器 msgid。
     *    localPath：原图落地的本地缓存文件完整路径。
     */
    void OnImageDownloaded(long long msgid, const std::wstring& localPath);

    /**
     *  收到的某条图片下载失败：按 msgid 把那条气泡标失败。由主面板路由调用。
     *    msgid：图片消息的服务器 msgid。
     */
    void OnImageDownloadFailed(long long msgid);

    // ---- 文件消息（msgtype=3，与图片镜像）。由主面板按会话 / 全局路由调用 ----

    //更新本端"发送中"文件气泡的上传进度（按 clientMsgId 定位）。
    void OnFileSendProgress(const std::string& clientMsgId, long long sent, long long total);
    //收尾本端"发送中"文件气泡（按 clientMsgId 定位）：成功置完成态；失败标失败；
    //cancelled 为 true（用户点了「取消」）则置「已取消」态而非失败态——二者气泡样式
    //与可用按钮都不同，取消后只留「重新发送」。失败时 errcode 为失败错误码，气泡下方据此写原因
    //（如文件名包含敏感词，见 FileSendFailureText）。
    void OnFileSendResult(bool ok, bool cancelled, const std::string& clientMsgId, int errcode = 0);
    //更新收到的某条文件的下载进度（按服务器 msgid 定位）。
    void OnFileDownloadProgress(long long msgid, long long received, long long total);
    //收到的某条文件下载完成：按 msgid 填本地落地路径并渲染。
    void OnFileDownloaded(long long msgid, const std::wstring& localPath);
    //收到的某条文件下载失败：按 msgid 标失败；cancelled 为 true 则标「已取消」（理由同上）。
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
    /**
     *  对方把本端发出的某个文件收到什么程度了（cmd_file_transfer_ack 推送到达时）：
     *  按 msgid 找到本端那条文件气泡，在其状态行显示「对方正在接收 / 已接收 / 已拒绝」。
     *  本窗口没有该条时安静忽略（消息尚未加载进当前视图）。由主面板路由调用。
     *  @param msgid     本端发出的那条文件消息的服务器 msgid
     *  @param peerState 对端收取状态，取值见 balloonchatwnd::ChatFilePeerState
     */
    void OnFilePeerAck(long long msgid, int peerState);

    // ---- 单聊文件夹传输（cmd_folder_* 1810~1817）。全部由主面板广播进来，本窗口
    //      按会话 id 在消息列表里找那条文件夹气泡；找不到即说明这笔传输不属于本会话，
    //      安静忽略。
    //
    //      传输<u>进行中</u>的那条气泡是纯本地的（不发 cmd_send_message、没有 msgid）；
    //      传输走到终态后，服务端会在双方会话里各落一条 msgtype=18 的传输记录条，那条
    //      才是留在聊天记录里、翻历史还能看到的。两者由 OnFolderRecord 就地接合成一条，
    //      详见该函数的说明。----

    /**
     *  收到对方发来的文件夹传输邀请：在会话里追加一条带「接收 / 另存为 / 拒绝」的邀请气泡。
     *    notify：邀请推送解析结果（发送方 userid / 昵称、文件夹名与规模统计）。
     */
    void OnFolderInvite(const net::FolderInviteNotify& notify);

    /**
     *  本端目录扫描完成：把文件数 / 目录数 / 总大小补进那条按本地占位 id 建出来的气泡。
     *  扫描失败时把它转成失败态并写上失败原因。
     *    notice：扫描结果。
     */
    void OnFolderScanned(const foldertransfer::FolderScanNotice& notice);

    /**
     *  发起邀请的应答已到：把气泡上的本地占位会话 id 换成服务端分配的那个，此后所有
     *  信令都按后者定位本行。被服务端拒绝时把气泡转成失败态；规模超限时另弹一次提示，
     *  用的是<u>服务端回带的上限值</u>，客户端不硬编码。
     *    bind：管理器回填的定位信息。
     */
    void OnFolderBound(const foldertransfer::FolderBindNotice& bind);

    /**
     *  会话状态推送到达：按会话 id 把气泡切到对应状态（响铃 / 传输中 / 拒绝 / 撤回 /
     *  对方离线 / 超时 / 失败 / 完成）。
     *    notify：状态推送解析结果。
     */
    void OnFolderState(const net::FolderStateNotify& notify);

    /**
     *  中转参数已下发、通道正在建立：把气泡切到「传输中」，让用户立刻看到进展，
     *  不必等第一片字节的进度回调。
     *    sessionId：本次传输的会话 id（UTF-8）。
     */
    void OnFolderTransferStarted(const std::string& sessionId);

    /**
     *  传输进度（收发共用）：更新气泡的进度条、已传字节数、当前文件名与序号。
     *    progress：进度快照。
     */
    void OnFolderProgress(const foldertransfer::FolderProgressNotice& progress);

    /**
     *  传输最终结果（收发共用）：把气泡切成完成 / 失败 / 已取消态，并记下落地目录
     *  （供「打开文件夹」）与失败原因。
     *    result：结果快照。
     */
    void OnFolderResult(const foldertransfer::FolderResultNotice& result);

    /**
     *  渲染一条服务端发来的<u>文件夹传输记录条</u>（msgtype=message_type_folder=18）。
     *  实时推送与翻历史两条路都走它，故同一笔传输在两处看到的样子必然一致。
     *
     *  行为分两种情形：
     *    · 会话里已有一条<u>还没被任何记录接管过</u>的本地文件夹气泡（会话 id 相同、
     *      msgid 仍为 0）：<u>就地接管</u>它 —— 填上服务器 msgid 与消息时间、按记录里的
     *      结局切到对应终态，界面上因而只有一条、位置也不跳。
     *    · 找不到那样的气泡（换了台机器、窗口是传完之后才打开的、或这一笔早先的尝试
     *      已经各留下过一条记录）：当作一条新消息正常追加。
     *
     *  <u>同一次传输可能留下多条记录，这是预期行为，不是重复消息。</u>服务端按「会话 id +
     *  尝试序号」做幂等，每次尝试各落一条：先失败落一条 result=7，10 分钟保留期内重试
     *  成功再落一条 result=8。于是实时看到的是一条随状态变化的气泡（重试时经
     *  ChatMessageList::ReviveFolderMessage 清掉 msgid 放回本地态，好被下一条记录再接管），
     *  而重开窗口从历史加载时，会看到这一笔的每次尝试各一条记录。请勿把它当成重复消息去"修"。
     *
     *    notify：消息推送 / 历史条目的解析结果，其 m_folderXxx 系列字段由
     *            net::FolderProtocolClient::parseRecordContent 填好；m_folderResult 为 0
     *            表示 content 解析不出来，此时本方法只记一条日志、不渲染。
     *    fromMe：本端是不是这次传输的<u>发送方</u>（由调用方按 notify.m_from 与当前登录
     *            用户比对得出）。它决定气泡画在左边还是右边，也决定终态下给哪些按钮。
     */
    void OnFolderRecord(const net::MessageNotify& notify, bool fromMe);

    BEGIN_MSG_MAP(CBalloonChatWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        // 窗口被切到前台时通知主面板清该会话未读（handler 置 bHandled=FALSE，
        // 消息继续沿消息映射交给 DuiFrameWindow 链）。
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivateMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(kRelayoutMsg, OnRelayout)
        MESSAGE_HANDLER(kSendOnEnterMsg, OnSendOnEnter)
        MESSAGE_HANDLER(kOpenSendMenuMsg, OnOpenSendMenu)
        MESSAGE_HANDLER(kDoScreenshotMsg, OnDoScreenshot)
        MESSAGE_HANDLER(kOpenRecordMenuMsg, OnOpenRecordMenu)
        MESSAGE_HANDLER(kInputChangedMsg, OnInputChanged)
        MESSAGE_HANDLER(WM_VIEW_CHAT_IMAGE, OnViewImageMsg)
        MESSAGE_HANDLER(WM_CHAT_FILE_ACTION, OnChatFileAction)
        MESSAGE_HANDLER(WM_CHAT_FOLDER_ACTION, OnChatFolderAction)
        MESSAGE_HANDLER(WM_CHAT_IMAGE_ACTION, OnChatImageAction)
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
        MESSAGE_HANDLER(WM_CHAT_OPEN_CLOUD_RECORD_CARD, OnChatOpenCloudRecordCard)
        MESSAGE_HANDLER(WM_CHAT_JOIN_GROUP_INVITE, OnChatJoinGroupInvite)
        //密聊邀请卡片上的「同意」「忽略」「取消」
        MESSAGE_HANDLER(WM_CHAT_SECRET_INVITE_ACTION, OnChatSecretInviteAction)
        //头部「密聊」按钮等延后到鼠标事件之外执行的密聊操作
        MESSAGE_HANDLER(kSecretActionMsg, OnSecretActionMsg)
        MESSAGE_HANDLER(WM_CHAT_OPEN_CODE_VIEWER, OnChatOpenCodeViewer)
        MESSAGE_HANDLER(WM_CHAT_OPEN_MARKDOWN_PREVIEW, OnChatOpenMarkdownPreview)
        MESSAGE_HANDLER(ballooncode::kMsgCodeComposeSend, OnCodeComposeSend)
        //输入框的粘贴识别通知：显示或收起「以代码片段发送」提示条
        MESSAGE_HANDLER(balloonchatwnd::kMsgChatCodePaste, OnCodePasteNotify)
        MESSAGE_HANDLER(WM_CHAT_LOAD_OLDER, OnChatLoadOlder)
        MESSAGE_HANDLER(WM_CHAT_REPLY_REQUEST, OnChatReplyRequest)
        MESSAGE_HANDLER(WM_CHAT_LOCATE_QUOTED, OnChatLocateQuoted)
        MESSAGE_HANDLER(WM_CHAT_IMAGE_READY, OnImageReady)
        MESSAGE_HANDLER(WM_CHAT_OPEN_LINK, OnChatOpenLink)
        MESSAGE_HANDLER(WM_CHAT_COPY_SELECTION, OnChatCopySelection)
        MESSAGE_HANDLER(WM_CHAT_CANCEL_LINK_OPEN, OnChatCancelLinkOpen)
        MESSAGE_HANDLER(WM_CHAT_FOCUS_AREA, OnChatFocusArea)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDownMsg)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDownMsg)
        //窗口销毁：注销本会话的可用共享个数（不拦截，交给基类继续处理）
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

public:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端，引用不持有

    /**
     *  应用当前连接态：顶部提示条的显隐与文字、发送按钮是否可用（离线时输入框仍可编辑）。
     *
     *  由会话控制器的 BroadcastConnectionState 经主面板逐窗转发；用户主动离线、从离线
     *  恢复在线、登录成功 / 失败、掉线时都会调到。
     *
     *    state：界面连接态，取值见 BalloonMain/OnlineCapability.h 的 ConnectionUiState。
     *
     *  副作用：改动提示条 / 输入框 / 发送按钮的状态并重绘；记住 state 供 DoSend 等
     *          发送路径二次判断（拦住"提示条还没刷到"的边角情形）。
     */
    void ApplyConnectionState(int state);

    /**
     *  「自动识别链接」开关变化后，让本窗口的消息行重新测量并重绘。由主面板在收到
     *  FMG_MSG_LINK_OPTION_CHANGED 后逐窗调用（进程级开关须先设好再调本方法）。
     *  窗口尚未初始化完成时为空操作。
     */
    void RefreshLinkRendering();

    /**
     *  全局默认聊天字体改了之后，若本会话没有单独设过字体，就改用新的默认字体（输入框
     *  随之更新，之后发出的消息也用它）；单独设过字体的会话不受影响。由主面板在收到
     *  FMG_MSG_DEFAULT_CHAT_FONT_CHANGED 后逐窗调用，新字体从账号配置里读。
     */
    void ApplyDefaultChatFontIfNotOverridden();

private:
    /**
     *  取「发送框」控件 —— 即输入框所在的那个 vbox（工具栏 + 输入框 + 底部提示 /
     *  发送行都在其中）。拖放的落点判定与提示描边都用它。
     *  @return 发送框控件；界面尚未构建时返回 NULL。
     */
    balloonwjui::DuiVBox* InputBlockCtrl();

public:

    // ---- balloonchatwnd::IChatDropSink 实现（把文件拖进窗口发送）----

    /**
     *  IChatDropSink 覆写：落点是否落在发送框（输入区块）内。拖文件只在发送框接收，
     *  落在上面的消息区一律拒收。
     *    ptClient：落点（本窗口客户区坐标）。
     */
    bool IsChatDropZone(const POINT& ptClient) override;

    /**
     *  IChatDropSink 覆写：拖到发送框上 / 离开时开关提示（发送框描边 + 左下角提示
     *  文字换成"松开发送给〈对方〉"）。
     *    visible：true 显示、false 收起并还原原提示文案。
     */
    void OnChatDropHintChanged(bool visible) override;

    /**
     *  IChatDropSink 覆写：文件被放下。按 ChatDropModel 归类后，图片走 SendImageFile、
     *  普通文件走 SendFileData、文件夹走 SendDroppedFolders（即文件夹传输链路）；
     *  发不出去的项用 toast 汇总提示。
     *    paths：      拖进来的路径列表，可以混着文件与文件夹。
     *    forceAsFile：落手时按住了 Shift —— 图片也按原文件发（不影响文件夹）。
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

    // 按文件共享模块是否可用（已加载且本企业的功能开关打开）显示或隐藏工具栏的「文件共享」按钮；显隐变化后
    // 对按钮所在的行重排（UI 约定第 5 条）。窗口建好时调一次；运行中收到 1013 功能开关推送后由主面板再调
    void ApplyShareButtonVisibility();

    // ======== 单聊密聊（docs/密聊方案.md 4.7、4.8；实现在 BalloonChatWindowSecret.cpp）========

    /**
     *  按密聊入口是否可用显示或隐藏头部的「密聊」按钮：密聊 DLL 就绪、企业开关打开、服务端确认开启、对方不是本人、
     *  且窗口处于普通状态时显示。显隐变化后对按钮所在的行重排（UI 约定第 5 条）。窗口建好时调一次；报到应答到达、
     *  1013 改了开关后由主面板再调。
     */
    void ApplySecretEntryVisibility();

    /**
     *  本人从本窗口发起的密聊邀请有了服务端应答（主面板收到密聊宿主的 OnSecretInviteSent 后按对方转来）。
     *  成功时在列表末尾插入「已邀请对方进入密聊」卡片并开始倒计时；失败时在窗口顶部提示原因。
     *  @param sessionId 会话号；失败时为 0
     *  @param remainMs  邀请剩余毫秒数（成功时有意义）
     *  @param code      0 成功；其余为 error_code_secret_* 错误码
     */
    void OnSecretInviteSent(unsigned long long sessionId, unsigned int remainMs, int code);

    /**
     *  显示一张对方发来的密聊邀请卡片（带「同意」「忽略」与剩余秒数）。实时收到与窗口打开时补出共用；
     *  已有同一会话号的卡片时只更新到期时刻，不重复插入。
     *  @param sessionId   会话号
     *  @param remainMs    邀请剩余毫秒数
     *  @param peerProtect 对方的防截图级别（secretchat::CaptureProtectLevel），为 0 时卡片上写明对方无法防止截图
     */
    void ShowSecretInviteCard(unsigned long long sessionId, unsigned int remainMs, int peerProtect);

    /**
     *  本窗口当前的密聊状态，取值见 balloonchatwnd::ChatSecretState。
     */
    int GetSecretState() const { return m_secretState; }

    /**
     *  与对方的密聊会话状态变化（主面板收到密聊宿主的 OnSecretSessionState 后按对方转来）。
     *  邀请阶段（卡片还在）：移除卡片，没能建立时插一条本地提示；已建立时进入密聊状态；
     *  进行中的会话结束（对方退出、掉线、管理员关闭、本机出错）时转为「只能查看」；对方防截图级别变化时刷新提示。
     *  @param sessionId   会话号
     *  @param state       取值见 secretchat::SessionStateEvent
     *  @param reason      state 为已结束时的结束原因（secret_end_reason），其余为 0
     *  @param peerProtect 对方当前的防截图级别
     */
    void OnSecretSessionState(unsigned long long sessionId, int state, int reason, int peerProtect);

    /**
     *  收到对方的一条密聊文字（已解密，主面板转来）：会话正是本窗口进行中的那个时，追加为对方的密聊气泡
     *  （isSecret 为 true，只存在于列表内存里，不落库）。其余情况丢弃并记日志（不记正文）。
     *  @param sessionId    会话号
     *  @param msgId        对方本次会话内的消息编号
     *  @param text         正文（表情以 /f["id"] 编码），只在本次调用期间有效
     *  @param serverTimeMs 服务端转发时刻（Unix 毫秒），用于时间分隔
     */
    void OnSecretText(unsigned long long sessionId, unsigned int msgId, const wchar_t* text, long long serverTimeMs);

    /**
     *  本人发出的一条密聊文字的结果（主面板转来）：失败时把那条气泡标为发送失败并写原因。
     *  @param sessionId 会话号
     *  @param msgId     本人本次会话内的消息编号
     *  @param code      0 成功；其余为错误码
     */
    void OnSecretSendResult(unsigned long long sessionId, unsigned int msgId, int code);

    /**
     *  对方开始发来一张密聊图片（收到首块时，主面板转来）：先插入一条「接收中」的对方图片占位气泡。
     *  @param sessionId    会话号
     *  @param imageId      对方本次会话内的图片编号
     *  @param totalBytes   原图字节数（用于算接收进度）
     *  @param width/height 原图宽高（像素），用于占位气泡的显示尺寸
     *  @param serverTimeMs 服务端转发时刻（Unix 毫秒），用于时间分隔
     */
    void OnSecretImageBegin(unsigned long long sessionId, unsigned int imageId, unsigned int totalBytes,
                            unsigned int width, unsigned int height, long long serverTimeMs);

    /**
     *  密聊图片的接收进度：刷新占位气泡上的「接收中 NN%」。
     *  @param receivedBytes 已收到的字节数
     */
    void OnSecretImageProgress(unsigned long long sessionId, unsigned int imageId, unsigned int receivedBytes);

    /**
     *  一张密聊图片收齐并校验通过（ok 为 true），或接收失败：成功时把字节登记为内存图片（secretmem:// 伪路径，
     *  不落盘），占位气泡改为就绪；失败时改为破图。
     *  @param bytes / len 原图字节，只在本次调用期间有效（内存图源会自行复制）
     */
    void OnSecretImage(unsigned long long sessionId, unsigned int imageId, const void* bytes, unsigned int len, bool ok);

    /**
     *  本人发出的一张密聊图片的发送进度与结果：刷新「上传中 NN%」，发完改为就绪，失败标为发送失败。
     *  @param sentBytes  已被服务端受理的字节数
     *  @param totalBytes 原图字节数
     *  @param code       0 表示进行中或已完成；其余为失败的错误码
     */
    void OnSecretImageSendProgress(unsigned long long sessionId, unsigned int imageId, unsigned int sentBytes,
                                   unsigned int totalBytes, int code);

private:
    /**
     *  发送前的连接态门禁：不可发送时弹出提示并返回 false。
     *    action：操作类别，取值见 BalloonMain/OnlineCapability.h 的 OnlineActionKind。
     *  返回：true 表示可以继续该操作。
     */
    bool CheckOnlineFor(int action);

    /**
     *  发送文件夹之前的<u>对端</u>在线门禁：对方不在线时弹出提示并返回 false。
     *
     *  与 CheckOnlineFor 的分工：那一个看的是本端连不连得上服务器，这一个看的是对端
     *  有没有在线连接。文件夹传输要双方各自连上 fileserver 建中转通道，对方不在线时
     *  这条通道建不起来，服务端在收到邀请的那一刻就会直接拒绝、连会话都不建。与其
     *  等那条拒绝回来再把气泡改成"发送失败"（聊天记录里会平白多出一条失败消息，而
     *  实际上一个字节都没发出去），不如在这里就把话说清楚。
     *
     *  本端掌握的对端状态可能是旧的（对方刚掉线、推送还没到），所以这道门禁只在
     *  <u>确知对方离线</u>时才拦：状态尚未注入（m_peerPresence 为 -1）时一律放行，
     *  由服务端的应答兜底（见 OnFolderBound 对 kFolderErrPeerOffline 的处理）。
     *
     *  返回：true 表示可以继续发送；false 表示已弹出提示、调用方应当直接返回。
     */
    bool CheckPeerOnlineForFolder();

    /**
     *  请主面板打开与对端的历史消息记录窗。头部「消息记录」按钮与「已 Pin N 条」
     *  提示带共用本函数，区别只在预选哪个类型胶囊。
     *    initialTypeFilter：打开后预选的类型胶囊，取 balloonhistory::HistoryTypeFilter
     *                       的值（0=全部，6=Pin）。
     *  副作用：向主面板投递 FMG_MSG_OPEN_HISTORY（cid 走堆 int64_t*，由主面板释放）。
     *          没有对端 / 主面板已销毁时什么都不做。
     */
    void RequestOpenHistory(int initialTypeFilter);

    /**
     *  按 m_pinnedCount 刷新「已 Pin N 条」提示带的显隐与文字。SetPinnedCount 与
     *  AdjustPinnedCount 改完计数后统一调它，保证只有一处在动这条提示带。
     *  副作用：显隐真的变了才补布局 —— SetVisible 只重绘不重排，不补的话新显示的
     *          提示带高度为 0（看不见），刚隐藏的那条空白也收不掉。
     */
    void RefreshPinBanner();

    /**
     *  WM_CLOSE 处理：销毁本窗口；调试入口下同时请求退出程序。
     */
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_ACTIVATE 处理：本窗口被切到前台时，向主面板投 FMG_MSG_CONV_MARK_READ，
     *  请它把本会话的未读清零。
     *
     *  存在理由：收消息那一路只在"消息到达的当下窗口就已在前台"时才算已读；用户
     *  在别处忙、事后才切回这个窗口来看的情形由本处补齐，否则消息看过了红点还亮着。
     *  不消费该消息（bHandled=FALSE），仍交 DuiFrameWindow 链继续处理。
     */
    LRESULT OnActivateMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_DUI_NOTIFY 处理：识别「发送」按钮点击，触发发消息。
     */
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_FILE_ACTION 处理：文件气泡底部按钮被点（打开 / 另存为 / 取消 / 重试）。
     *  lParam=堆上 balloonchatwnd::ChatFileActionData*，处理后 delete。
     */
    LRESULT OnChatFileAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_FOLDER_ACTION 处理：文件夹气泡底部按钮被点（接收 / 另存为 / 拒绝 /
     *  取消 / 重试续传 / 重新发送 / 打开文件夹）。lParam=堆上
     *  balloonchatwnd::ChatFolderActionData*，<u>本处理器负责 delete</u>。
     *  气泡不认识传输会话，只把"哪一次传输、点了什么"如实上报，具体动作由本窗口
     *  转交 foldertransfer::FolderTransferManager 执行。
     */
    LRESULT OnChatFolderAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  同意接收某一笔文件夹传输，并指定落地位置的父目录。
     *
     *  「接收」与「另存为」两个按钮只差落地目录从哪来（前者取本机默认的接收目录，
     *  后者由用户在选目录框里挑），此后的应答、提示与失败收尾完全一致，故收口到本方法，
     *  两处不再各写一遍。
     *
     *    data：         气泡上报的动作负载，本方法只读其 sessionId。
     *    saveParentDir：落地位置的<u>父目录</u>（根目录将建在它下面），须已存在且非空；
     *                   重名时由接收端状态机自动改名，本方法不处理重名。
     *  副作用：受理失败时弹一个提示框，并把该条气泡置为「已取消」态。
     */
    void AcceptFolderInvite(const balloonchatwnd::ChatFolderActionData& data,
                            const std::wstring& saveParentDir);

    /**
     *  对一个已经确定的本地目录发起一次「发送文件夹」：交 FolderTransferManager 起传输，
     *  受理成功则就地建出一条发送方文件夹气泡（挂本地占位会话 id，等扫描结果补齐规模）。
     *
     *  工具栏按钮与拖放两条入口都收口到本方法，两者的受理与气泡形态因而必然一致；
     *  提示文案由各自的调用方给（拖放要一次说清哪几个没发起，与单个目录的措辞不同）。
     *
     *    folderPath：要发送的本地目录完整路径，须非空且不是磁盘根目录。
     *  @return 见 foldertransfer::FolderStartVerdict：kFolderStartOk 表示已受理并已建气泡；
     *          kFolderStartBusy 表示本机并发笔数已达上限；其余为参数非法等。
     *  副作用：受理成功时占住一个本机并发名额，并向消息列表追加一条气泡。
     */
    int StartFolderTransfer(const std::wstring& folderPath);

    /**
     *  弹系统选目录框选一个本地目录，并发起一次「发送文件夹」。工具栏「发送文件夹」
     *  按钮（kIdTbFolder）点击时调用。仅真实会话（m_peerUserId != 0）且当前在线时生效；
     *  本机已有一笔文件夹传输在跑时会被管理器拦下，此处给出提示。
     */
    void PickAndSendFolder();

    /**
     *  把拖进发送框的一批文件夹逐个发起传输（DropPlan::m_folders 就是这一批）。
     *
     *  本机同时进行的文件夹传输有笔数上限（配置项 foldermaxconcurrent，默认 5 笔，发送与
     *  接收各算一笔）。按拖入顺序逐个发起，某一个因名额已满被拒时，它<u>连同后面所有</u>
     *  都不再发起 —— 名额要等已在跑的某一笔结束才放开，继续试只是重复同一个失败。
     *
     *    folders：文件夹完整路径列表，顺序即用户拖入顺序；空列表时本方法什么都不做。
     *  副作用：为每个受理成功的文件夹追加一条气泡；有未发起的文件夹时弹一个提示框，
     *          列出其中前几个的名字与未发起的原因。
     */
    void SendDroppedFolders(const std::vector<std::wstring>& folders);

    /**
     *  弹系统选目录框（与「系统设置 → 存储」里选数据目录用的是同一套 SHBrowseForFolder，
     *  不另造一份）。
     *    title：对话框标题栏上方的提示文字。
     *    out：  [出参] 用户选中的目录完整路径；返回 false 时被清空。
     *  @return 用户选定了一个文件系统目录返回 true；取消或选中的不是真实目录返回 false。
     */
    bool PickFolderPath(LPCTSTR title, std::wstring& out);

    /**
     *  按会话 id 取回本窗口里那条文件夹气泡的消息。
     *    sessionId：会话 id（UTF-8）。
     *    out：      [出参] 命中时填入。
     *  @return 找到返回 true；本会话里没有这条传输返回 false（属正常情形，不必记错误日志）。
     */
    bool GetFolderMessage(const std::string& sessionId, balloonchatwnd::ChatMessage& out);

    /**
     *  把改好的消息写回文件夹气泡并重排重绘。
     *    sessionId：定位用的会话 id（写回<u>之前</u>那条气泡上的那个）。
     *    msg：      改好的整条消息。
     *  @return 命中并写回返回 true。
     */
    bool SetFolderMessage(const std::string& sessionId, const balloonchatwnd::ChatMessage& msg);

    /**
     *  在会话末尾追加一条<u>对方发来的</u>文件夹邀请气泡（带「接收 / 另存为 / 拒绝」三个按钮）。
     *  实时收到推送与稍后点开会话补渲染共用它，两处的气泡形态因此必然一致。
     *    sessionIdUtf8：  本次传输的会话 id（UTF-8），气泡按它接收后续信令。
     *    folderNameUtf8：根目录名（UTF-8，不含路径）。
     *    totalDirs：     子目录总数（含空目录、不含根目录自身），单位：个。
     *    totalFiles：    文件总数，单位：个。
     *    totalBytes：    全部文件字节数合计，单位：字节。
     */
    void AppendFolderInviteBubble(const std::string& sessionIdUtf8,
                                  const std::string& folderNameUtf8,
                                  long long totalDirs, long long totalFiles,
                                  long long totalBytes);

    // ======== 单聊单文件 P2P 直传（2026-10-04 起，实现在 BalloonChatWindowDirectFile.cpp）========

    /**
     *  发一个文件时先试在线直传：对方没被确知离线、本进程登记了直传的 P2P 业务方、调用方没要求按离线文件上传时
     *  （判定见 foldertransfer::ShouldTryDirectFileSend），插一条「等待对方接收」的文件气泡并发直传邀请；
     *  邀请发不出去时就地改为上传服务器。
     *    path / fileName / sizeBytes：要发的文件（完整路径、显示名、字节数）。
     *    clientMsgId：这条文件气泡的本地消息号（调用方生成）。
     *    peerKnownOffline：本端掌握的对方状态是否为离线。
     *    forceOffline：调用方是否要求直接按离线文件上传（转发在线直传文件时重新发送本机文件）。
     *  @return 走了直传（气泡已插入）返回 true；不具备直传条件返回 false，调用方照旧上传服务器
     */
    bool TrySendFileDirect(const std::wstring& path, const CString& fileName, long long sizeBytes,
                           const std::string& clientMsgId, bool peerKnownOffline, bool forceOffline);

    /**
     *  把一条在线直传的文件气泡改为上传服务器发离线文件：气泡转「上传中」，交给 FileTransfer 上传，并登记待冒泡。
     *    key：直传会话 id 或 clientMsgId。只有本端发出的那条才会上传。
     */
    void StartDirectFileOfflineUpload(const std::string& key);

    //以下五个把文件夹传输的事件落到在线直传的文件气泡上；事件不属于本会话的直传气泡时返回 false，
    //调用方照旧按文件夹传输处理
    bool OnDirectFileBound(const foldertransfer::FolderBindNotice& bind);
    bool OnDirectFileState(const net::FolderStateNotify& notify);
    bool OnDirectFileTransferStarted(const std::string& sessionId);
    bool OnDirectFileProgress(const foldertransfer::FolderProgressNotice& progress);
    bool OnDirectFileResult(const foldertransfer::FolderResultNotice& result);

    /**
     *  在会话末尾追加一条对方发来的在线直传邀请（待接收的文件气泡，「接收 / 拒绝」）。已经画过时不重复画。
     *    sessionIdUtf8：直传会话 id；fileNameUtf8：文件名（UTF-8）；sizeBytes：文件字节数。
     */
    void AppendDirectFileInviteBubble(const std::string& sessionIdUtf8, const std::string& fileNameUtf8,
                                      long long sizeBytes);

    /**
     *  处理在线直传文件气泡上的按钮（发送离线文件、取消、接收、拒绝、重新发送）。
     *    d：气泡投上来的动作负载（不接管所有权）。
     *  @return 处理了返回 true；不是直传的气泡返回 false，调用方照旧按普通文件处理
     */
    bool HandleDirectFileAction(const balloonchatwnd::ChatFileActionData& d);

    /**
     *  服务端为一次直传留下的文件消息到达时，接管会话里那条实时气泡（补上服务器消息号、MD5 与链路路径）。
     *    notify：文件消息（m_fileDirect 为 true）。
     *  @return 接管了返回 true；会话里没有那条实时气泡时返回 false，调用方新建一条
     */
    bool AdoptDirectFileRecord(const net::MessageNotify& notify);

    /**
     *  向 foldertransfer::FolderTransferManager 查一次"与本会话对端之间有哪些待应答的
     *  文件夹邀请"，把查到的逐条补成邀请气泡。
     *
     *  存在理由：收到邀请<u>不再</u>弹出聊天窗（那是一次明显的打扰），于是用户往往是在
     *  邀请到达之后才从会话列表点进来的；没有这一步，他看到的会是一个空会话，那些邀请
     *  永远没机会被应答。
     *
     *  <u>可能有多条</u>：本机同时可进行多笔文件夹传输，同一个人先后发来两个文件夹时，
     *  两条邀请会同时挂着，都要画出来。
     *
     *  调用时机有两处：窗口建好并显示之后调一次（历史那一页可能迟迟不回），首屏历史渲染
     *  完之后再调一次（渲染首屏会先清空列表，把先补的那些一并清掉）。本方法逐条按会话 id
     *  查重，重复调用不会画出两条。已被拒绝 / 撤回 / 超时的邀请管理器查不出来，故不会
     *  补出已经失效的气泡。
     */
    void RestorePendingFolderInvite();

    /**
     *  首屏历史渲染之前发出的 Markdown 文档都已确认入库、且首屏已渲染时，重新拉一次最新一页，
     *  让被首屏渲染清掉的卡片按服务端快照显示出来（判断见 balloonchatwnd::MarkdownEarlySendTracker）。
     *  条件不满足时什么都不做。
     */
    void ReloadIfMarkdownEarlySendsDone();

    /**
     *  把一次传输的失败 / 结束原因落到气泡上：切状态、写失败原因、按保留期决定「重试」
     *  可用与否。失败、取消、过期三种收场共用它，免得三处各写一遍。
     *    sessionId：会话 id。
     *    errCode：  失败原因错误码（error_code_folder_*）；0 表示没有具体原因。
     */
    void ApplyFolderFailure(const std::string& sessionId, int errCode);

    /**
     *  WM_CHAT_IMAGE_ACTION 处理：图片气泡右键选了「图片另存为」/「再次发送」。
     *  lParam=堆上 balloonchatwnd::ChatImageActionData*（图片本地路径 + 另存为默认
     *  文件名 + 选中的菜单项），处理后 delete。另存为按原文件字节整体拷贝（无损、
     *  保持原格式）；再次发送把同一张图作为<u>一条新消息</u>重新发到本会话，走的
     *  是与选图发送同一条 SendImageFile 链路。
     */
    LRESULT OnChatImageAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_RECALL_REQUEST 处理：消息列表里某条气泡右键选了「撤回」。
     *  lParam=堆上 long long*（被撤消息的服务器 msgid），处理后 delete。
     *  本处理器只负责把请求连同本窗口的会话身份（单聊 ctype=1、cid=对端 userid）
     *  发给服务端；界面改动等服务端推回 cmd_message_recalled_notify 后再做。
     */
    LRESULT OnChatRecallRequest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_PIN_REQUEST 处理：消息列表里某条气泡右键选了「Pin / 取消 Pin」。
     *  wParam=目标状态（1 钉住、0 取消），lParam=堆上 long long*（目标消息的服务器
     *  msgid），处理后 delete。只负责把请求连同本窗口的会话身份（单聊 ctype=1、
     *  cid=对端 userid）发给服务端；标记改动等服务端推回 cmd_message_pinned_notify 后再做。
     */
    LRESULT OnChatPinRequest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_DELETE_REQUEST 处理：消息列表里有服务器 msgid 的行被右键「删除」或多选「删除 N」。
     *  lParam=堆上 std::vector<long long>*（要删除的服务器 msgid），处理后 delete。
     *  弹确认框，确认后逐条发 cmd_delete_message（该命令不带会话身份，只认 msgid）；
     *  气泡等服务端推回 cmd_messages_deleted_notify 后经 OnMessagesDeleted 移除。
     *  调试入口没有服务端可问，确认后直接在本窗口移除，便于核对界面效果。
     */
    LRESULT OnChatDeleteRequest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_FORWARD_REQUEST 处理：多选操作条上点了「逐条转发」或「合并转发」。
     *  lParam=堆上 balloonchatwnd::ChatForwardRequest*，处理后 delete。补上源会话（本单聊）、
     *  本窗口句柄与合并标题后，以 FMG_MSG_OPEN_FORWARD_PICKER 投给主面板打开目标选择窗。
     *  调试入口没有主面板，只记日志。
     */
    LRESULT OnChatForwardRequest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_REEDIT_REQUEST 处理：发送失败的文字气泡右键选了「重新编辑」。
     *  lParam=堆上 balloonchatwnd::ChatReeditData*，处理后 delete。把原文插到输入框光标处并把焦点交给输入框。
     */
    LRESULT OnChatReeditRequest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_ADD_TO_MEMO 处理：文字消息右键选了「添加到备忘」（docs/备忘与提醒方案.md §8）。按「来自 对方名字，时间」
     *  补上来源，生成预填的标题与备注，打开新建本人备忘的窗口。lParam 为堆上的 ChatAddToMemoRequest，本函数 delete。
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

    // WM_DESTROY：向文件共享宿主注销本会话的可用共享个数；bHandled 置 FALSE，基类照常处理
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 屏幕共享卡片被单击（WM_CHAT_JOIN_SCREENSHARE）：lParam 为堆上的 ScreenShareJoinRequest，经屏幕共享中心交给
    // FlamingoMedia.dll 加入共享后释放
    LRESULT OnChatJoinScreenShare(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 云端录像卡片的「播放」被点（WM_CHAT_OPEN_CLOUD_RECORD_CARD）：lParam 为堆上的 CloudRecordOpenRequest，经屏幕共享中心
    // 交给 FlamingoMedia.dll 下载并打开后释放（docs/屏幕共享与录制方案.md 12.6.5）。不在这里判断是否在线：本机缓存里已有
    // 这条录像时不需要连接，离线时由 DLL 提示
    LRESULT OnChatOpenCloudRecordCard(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 代码片段卡片请求打开代码查看窗（WM_CHAT_OPEN_CODE_VIEWER）：lParam 为堆上的 CodeViewerOpenRequest，
    // 按消息标识打开或调出查看窗后释放（docs/代码片段方案.md 第 3.3 节）
    LRESULT OnChatOpenCodeViewer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // Markdown 卡片请求打开预览窗口（WM_CHAT_OPEN_MARKDOWN_PREVIEW）：lParam 为堆上的 MarkdownPreviewOpenRequest，
    // 补上发送者名字后交给 Markdown 编辑器管理器打开或调出预览窗口，然后释放（docs/Markdown消息方案.md 第 11.3 节）
    LRESULT OnChatOpenMarkdownPreview(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 「发送代码」窗口把要发送的代码交回本窗口（ballooncode::kMsgCodeComposeSend）：lParam 为堆上的 CodeComposeResult，
    // 手选的语言记进用户配置，代码按代码片段发出后释放（docs/代码片段方案.md 第 3.1 节）
    LRESULT OnCodeComposeSend(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 输入框的粘贴识别通知（balloonchatwnd::kMsgChatCodePaste，docs/代码片段方案.md 第 3.1 节）：wParam 为 1 表示
    // 刚往空输入框里粘贴了文字，像代码时显示「以代码片段发送」提示条；为 0 表示内容有变化，提示条显示之后
    // 内容与显示时不同（继续输入、删改、发送）就收起
    LRESULT OnCodePasteNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 入群邀请卡片上点了「加入群聊」（WM_CHAT_JOIN_GROUP_INVITE）：lParam 为堆上的 GroupInviteJoinRequest，
    // 把卡片置为「正在加入」并发 cmd_accept_group_invite 后释放；离线时提示并放弃
    LRESULT OnChatJoinGroupInvite(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_LOAD_OLDER 处理：消息区滚到接近顶部，判断要不要拉更早的一页历史。
     *  判定在 ShouldLoadOlderHistory（纯逻辑、可单测），本函数只喂当前滚动状态、
     *  并在它点头时发请求、置"在途"标志。与群聊窗同一套。
     */
    LRESULT OnChatLoadOlder(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_REPLY_REQUEST 处理：气泡右键选了「引用回复」。
     *  lParam=堆上 long long*（被引用消息的服务器 msgid），处理后 delete。
     *  组装引用快照存进 m_pendingQuote，下一次发送文字时随消息发出。与群聊窗同一套。
     */
    LRESULT OnChatReplyRequest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_LOCATE_QUOTED 处理：用户点了气泡里的引用块，滚动定位到被引用的原消息。
     *  lParam=堆上 long long*（原消息的服务器 msgid），处理后 delete。
     *  原消息不在已加载范围内（翻不到那么早）时安静忽略 —— 自动回拉历史再定位属于
     *  后续阶段，不在本期范围。
     */
    LRESULT OnChatLocateQuoted(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 待发的引用快照：点了「引用回复」后暂存，下一次发送文字时随消息发出并清空。
    net::ReplyQuote m_pendingQuote;

    // 发送后待冒泡的队列：发送时按 clientMsgId 登记，回执成功时取出通知主面板，失败时丢弃。
    // 生命周期随窗口；关窗后迟到的回执由主面板直接忽略（窗口已不在映射里）。
    balloonchatwnd::ChatSendBumpQueue m_sendBumps;

    /**
     *  按 m_pendingQuote 刷新输入框上方的引用条：有待发引用就显示成
     *  「回复 张三：你好」（人名主题色、摘要灰色），没有就收起。
     *  设置 / 清空 m_pendingQuote 之后都要调一次。与群聊窗同一套。
     */
    void RefreshReplyBar();

    /**
     *  取消当前引用（点引用条上的 ✕、或发送完成后）：清空 m_pendingQuote 并收起引用条。
     */
    void CancelPendingReply();

    // 本会话历史的向上翻页状态。m_loading 同时用来区分应答是首屏还是翻页 ——
    // 服务端应答里不回显 before，客户端只能自己记。
    balloonchatwnd::HistoryPagingState m_paging;

    // 首屏历史渲染之前发出的 Markdown 文档（从 Markdown 编辑器发送时窗口刚打开）。随窗口存续，
    // 首屏渲染完、这些文档都确认入库后据它重新拉一次最新一页
    balloonchatwnd::MarkdownEarlySendTracker m_mdEarlySends;

    // 首屏历史渲染之前插入的居中提示（如从主面板右键菜单发起通话、对方不在线时，窗口刚打开就插入的
    // 「对方当前无法接听」）。首屏渲染会清空列表，渲染完后据它把这些提示补回末尾。随窗口存续；
    // 调试入口（没有对端、不拉历史）不记录
    balloonchatwnd::EarlySystemTipTracker m_earlyTips;

    // 打开计时：CreateAndShow 开始时重置，首屏历史渲染完成时读取，得出「从打开到看到
    // 历史消息」一共花了多久（含等待服务端应答），写进日志供性能排查。只在首屏用，翻页不读。
    PerfStopWatch m_openStopWatch;

    /**
     *  WM_TIMER 处理：mock 自动回复定时器到点后，追加一条对方回复消息。
     */
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  构建并装载客户区控件树。须在窗口 Create 之后调用。
     */
    void InitUi();

    /**
     *  发消息：取输入框文字，非空则追加为自己的气泡、清空输入框，并起一个
     *  定时器触发 mock 自动回复。
     */
    void DoSend();

    // 发送一段文字（真实会话）：乐观回显气泡 + 经网络发文本消息 + 冒泡会话。供
    // DoSend 逐段发送时调用。仅 m_peerUserId != 0 时生效。
    //   text：该段文字原文。
    //   withQuote：是否带上输入框上方正在引用的那条消息（并在发出后收起引用条）；SendPlainText 传 false，
    //              引用条保持原样（屏幕共享第 25 期：分享链接的邀请不该带上用户正在编辑的引用）
    void SendTextPartReal(const CString& text, bool withQuote = true);

    // 打开 Markdown 编辑器，发送对象预选本会话（工具栏的 Markdown 编辑器按钮、Markdown 消息的「重新编辑」共用；
    // docs/Markdown消息方案.md 第 11.3 节）。调试入口没有对端时不预选发送对象。
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

    // 代码片段批注作者的显示名（userid 到名字），单聊里是本人与对方。代码查看窗与批注模式的批注栏据此写作者
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


    // 发送输入框里的一张内嵌图（真实会话）：取 m_pendingShotPaths[imageIndex] 这个源文件
    // 直接走 SendImageFile 发出（不从 OLE 抽位图）。供 DoSend 逐段发送时调用。
    //   imageIndex：源文件在 m_pendingShotPaths 里的下标（BuildOutgoingParts 从图片标记读回）。
    void SendImagePartReal(int imageIndex);

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
     *  选图并作为图片消息发送：弹文件选择对话框选一张图片，乐观插入一条
     *  "上传中"的图片气泡（显示本地图），再交给 net::ImageTransfer 工作线程
     *  完成"申请上传→传字节→发图片消息"。仅真实会话（m_peerUserId != 0）下生效。
     */
    void PickAndSendImage();

    /**
     *  把给定本地图片文件作为图片消息发送（选图与截图发送共用）：乐观插入一条
     *  "上传中"气泡，再交 net::ImageTransfer 工作线程上传并发消息，并冒泡会话。
     *  仅真实会话（m_peerUserId != 0）下生效。
     *    path：本地图片文件全路径（UTF-16）。
     */
    void SendImageFile(const std::wstring& path);

    /**
     *  调起全屏截图浮层（截图工具栏按钮、▾ 菜单或截图快捷键）。模态运行：复制 / 保存由浮层
     *  内部完成，用户选「发送」时经 OnScreenshotSend 回调把截图作为图片消息发到当前会话。
     *  「截图时隐藏当前窗口」开启时（CUserConfig::IsScreenshotHideWindow）本窗口不出现在截图里。
     *  仅真实会话（m_peerUserId != 0）下生效。
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

    // 截图浮层「发送」回调（C 风格函数指针，ctx 为本窗口 this）：把临时 PNG 走
    // SendImageFile 立即发出。
    //   ctx：    本窗口 this；
    //   pngPath：浮层存好的临时 PNG 文件全路径。
    static void OnScreenshotSend(void* ctx, const wchar_t* pngPath);

    // 截图浮层「发送到对话框」回调（C 风格函数指针，ctx 为本窗口 this）：把临时 PNG
    // 作为内联图片插入输入框（不立即发），等用户按「发送」时随文字一起逐段发出。
    //   ctx：    本窗口 this；
    //   pngPath：浮层存好的临时 PNG 文件全路径。
    static void OnScreenshotStage(void* ctx, const wchar_t* pngPath);

    // 输入框「插入图片」回调（ChatInputRichEdit::StageImageFn，ctx=本窗口 this）：粘贴或
    // 拖入的聊天内容里有图片时，转给 StageImageInInput 登记源文件并插入。
    //   返回：插入成功返回 true。
    static bool StageImageThunk(void* ctx, const std::wstring& path);

    /**
     *  把一条收到的图片消息渲染成"下载中"气泡并触发原图下载。供 OnRecvMessage
     *  与 OnHistoryMessages 共用。
     *    list：   消息区列表（调用方已取好，避免重复查找）。
     *    fromMe： 是否本端发出（决定气泡头像左右与归属）。
     *    notify： 该图片消息（取 msgid / fileid / md5 / 宽高 / 大小）。
     */
    void RenderIncomingImage(balloonchatwnd::ChatMessageList* list,
                             bool fromMe, const net::MessageNotify& notify);

    /**
     *  选文件并作为文件消息发送：弹文件选择对话框（所有类型），乐观插入一条
     *  "上传中"文件气泡，再交 net::FileTransfer 工作线程完成"申请上传→传字节→发
     *  文件消息"。仅真实会话（m_peerUserId != 0）下生效。
     */
    void PickAndSendFile();

    /**
     *  把给定本地文件作为文件消息发送：乐观插入"上传中"气泡，交 net::FileTransfer
     *  工作线程上传并发消息，并冒泡会话。仅真实会话（m_peerUserId != 0）下生效。
     *  对方在线时先试在线直传（见 TrySendFileDirect），forceOffline 为 true 时不试。
     *    path：本地文件全路径（UTF-16）。
     *    forceOffline：true 时直接上传服务器、不试在线直传。
     */
    void SendFileData(const std::wstring& path, bool forceOffline = false);

    /**
     *  把一条收到的文件消息渲染成气泡：本地已缓存则直接完成态、否则"下载中"并触发
     *  下载。供 OnRecvMessage 与 OnHistoryMessages 共用。
     *    list：   消息区列表（调用方已取好）。
     *    fromMe： 是否本端发出（决定气泡头像左右）。
     *    notify： 该文件消息（取 msgid / fileid / 文件名 / md5 / 大小）。
     */
    /**
     *  取消息区列表控件（本窗口内多处要用，收口成一个helper）。
     *  @return 消息列表指针；窗口内容未就绪时返回 NULL，调用方须判空。
     */
    balloonchatwnd::ChatMessageList* GetMessageList();

    /**
     *  按文件气泡按钮带上来的信息发起一次下载（用户点「接收」或「重新下载」时）。
     *  内部会把该行气泡切到「下载中」；进度与结果随后按 msgid 异步回投。
     *    d：气泡按钮动作负载，用到 msgid / fileId / md5 / fileName / sizeBytes。
     *       msgid <= 0 或 fileId 为空时只记错误日志、不发起下载。
     */
    void StartFileDownloadFromBubble(const balloonchatwnd::ChatFileActionData& d);

    /**
     *  重新发送本端那笔被取消 / 失败的文件（用户点「重新发送」时）。沿用原 clientMsgId，
     *  故气泡仍是原来那条、不另插一行。源文件已被移走或删除时标失败并提示用户。
     *    d：气泡按钮动作负载，用到 clientMsgId / localPath。
     */
    void RestartFileSendFromBubble(const balloonchatwnd::ChatFileActionData& d);

    /**
     *  把某条收到的文件置为「已拒绝接收」（用户点「拒绝」时）。仅改本端气泡状态，
     *  不下载、不通知发送方；该态仍可点「重新下载」反悔。
     *    msgid：该文件消息的服务器 msgid。
     */
    void MarkFileRejected(long long msgid);

    /**
     *  向发送方上报本端把某个收到的文件处理成什么样了（接收 / 收完 / 拒绝）。
     *  仅真实会话下有效：调试入口无对端绑定、本端发出的消息没有服务器 msgid，两者
     *  都直接跳过不发包。
     *  @param msgid 该文件消息的服务器 msgid
     *  @param state 收取状态，取值见 net 的 enum file_ack_state
     */
    void ReportFileAck(long long msgid, int state);

    void RenderIncomingFile(balloonchatwnd::ChatMessageList* list,
                            bool fromMe, const net::MessageNotify& notify);

    /**
     *  把收到 / 历史里的一条语音消息（msgtype=4）渲染成语音气泡（语音消息第 3d-3 步）：本机缓存里有这段语音时
     *  直接就绪，否则显示下载中并在后台下载（方案 V5）；对方发来且未听的画红点（方案 V7）。
     *    list：消息列表控件，可为 NULL（直接返回）；fromMe：是否本人所发；notify：消息解析结果；
     *    played：本条所在批次的已听查询结果（QueryVoicePlayed 填好的），据它判断画不画红点。
     */
    void RenderIncomingVoice(balloonchatwnd::ChatMessageList* list, bool fromMe,
                             const net::MessageNotify& notify, const balloonchatwnd::VoicePlayedQuery& played);

    /**
     *  发送一条"窗口抖动"消息：经网络层把抖动发给对端（msgtype=13），聊天区追加
     *  一条居中系统提示，并晃动自己窗口；同时通知主面板冒泡该会话。仅真实会话
     *  （m_peerUserId != 0）下生效。
     */
    void DoSendShake();

    /**
     *  晃动本窗口若干次（窗口抖动动画）。UI 线程定时器驱动：记录当前窗口左上角
     *  为基准，按预设偏移序列逐步移窗，结束后归位。重复调用会从头重放。
     */
    void ShakeWindow();

    /**
     *  把本窗口调到用户眼前：最小化则还原、隐藏则显示，并尝试设为前台；用户正在
     *  操作别的程序、设前台被系统拒绝时，改为只把窗口提到其他普通窗口之上并闪烁
     *  任务栏按钮，不抢键盘焦点（见 WindowAttention.h）。收到对端窗口抖动时调用，
     *  确保接收方能看到抖动。
     *  副作用：可能改变窗口显示状态、层级与前台状态；写一行诊断日志。
     */
    void BringChatWindowToFront();

    /**
     *  切换字幕模式：头部「字幕模式」按钮被点时调用。字幕由主面板持有（不随本窗口关闭），这里经
     *  FMG_MSG_TOGGLE_SUBTITLE 同步请主面板切换，再按结果高亮按钮；聊天窗本身保持不动。
     *  调试入口（没有主面板）下什么都不做。副作用：写一行日志。
     */
    void ToggleSubtitleMode();

    /**
     *  在聊天区追加一条居中系统提示（kMsgTime 类型，如"对方向你发送了窗口抖动"）。
     *    text：提示文字。
     */


    /**
     *  打开表情选择面板 —— 点击工具栏"表情"按钮时调用。锚定在表情按钮上方，
     *  弹出 程序目录/Face 图片表情浮层（已取代旧的 Unicode emoji 浮层），选中某表情
     *  经回调以"带 faceId 标记的内联图"插入输入框。
     */
    void OpenEmojiPicker();

    // 工具栏「文件共享」：在按钮旁弹出本会话的共享列表浮层（FlamingoFileShare.dll 提供）
    void OpenConversationShares();

    // 收到或翻到一条共享卡片：按普通文字气泡构造后套用卡片内容（含 DLL 手里的最新状态），追加到列表
    void AppendShareCard(balloonchatwnd::ChatMessageList* list, const net::MessageNotify& notify, bool fromMe,
                         const std::string& clientMsgId);

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
     *  发送一段录好的语音：在列表末尾插一条发送中的语音气泡，再交给文件传输线程上传并发出（第 3c 步）。
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

    // 收到或翻到一张屏幕共享卡片：按普通文字气泡构造后套用卡片内容与屏幕共享中心记录的状态，追加到列表
    void AppendScreenShareCard(balloonchatwnd::ChatMessageList* list, const net::MessageNotify& notify, bool fromMe,
                               const std::string& clientMsgId);

    // 收到或翻到一张云端录像卡片：按普通文字气泡构造后套用卡片内容（卡片的发送者是开启者，fromMe 决定显示哪一侧），追加到列表
    void AppendCloudRecordCard(balloonchatwnd::ChatMessageList* list, const net::MessageNotify& notify, bool fromMe,
                               const std::string& clientMsgId);

    // 收到或翻到一张入群邀请卡片：按普通文字气泡构造后套用卡片内容（卡片的发送者是邀请人，fromMe 决定显示哪一侧），
    // 追加到列表
    void AppendGroupInviteCard(balloonchatwnd::ChatMessageList* list, const net::MessageNotify& notify, bool fromMe,
                               const std::string& clientMsgId);

    // 收到或翻到一张一对一通话卡片：按普通文字气泡构造后套用卡片内容（卡片的发送者是主叫，fromMe 决定显示哪一侧），追加到列表
    void AppendCallCard(balloonchatwnd::ChatMessageList* list, const net::MessageNotify& notify, bool fromMe,
                        const std::string& clientMsgId);

    /**
     *  把一个 emoji 串追加到输入框末尾。旧 Unicode emoji 浮层用；现已不再挂入，
     *  保留以便代码可编。
     *    emoji：UTF-16 表情串。
     */
    void InsertEmojiToInput(const wchar_t* emoji);

    // 旧 Unicode emoji 面板"选中"回调（保留不再挂入）。
    static void OnEmojiPicked(void* userdata, const wchar_t* emoji);

    /**
     *  把一个系统表情以"带 faceId 标记的内联图"插入输入框当前光标处（所见即所得）。
     *  供图片表情浮层选中回调使用。
     *    faceId：  系统表情 id（Face/FaceConfig.xml 的 id）。
     *    facePath：该表情 PNG 的绝对路径。
     */
    void InsertFaceToInput(int faceId, const wchar_t* facePath);

    // 图片表情面板"选中"回调（C 风格函数指针，userdata 为本窗口 this）。
    static void OnFacePicked(void* userdata, int faceId, const wchar_t* facePath);

    /**
     *  构造一条带"真实头像"的文本消息：fromMe=true 用本端头像（自己昵称首字
     *  + 按自身 userid 取色），否则用对端头像（对端昵称首字 + 按对端 userid
     *  取色）。仅用于真实会话（m_peerUserId != 0）。
     *    fromMe：是否本端发出。
     *    text：  文本内容。
     */
    //   msgid / timestampMs：服务器消息ID与消息时间（Unix 毫秒）。收到推送 / 渲染历史时
    //     按报文原样传入；本端刚发出、回执未到时传 0，稍后由 BackfillSentMessage 回填。
    //     右键菜单的"撤回"项是否出现即取决于这两个值（见 canRecallMessage）。
    //   clientMsgId：本端发出时生成的客户端消息ID，供回执按它定位本行回填；收到 /
    //     历史消息传空串。
    balloonchatwnd::ChatMessage MakeRealTextMessage(bool fromMe, const CString& text,
                                                    const net::TextFont& font,
                                                    long long msgid = 0,
                                                    long long timestampMs = 0,
                                                    const std::string& clientMsgId = std::string(),
                                                    const net::ReplyQuote& quote = net::ReplyQuote());

    /**
     *  填写一条消息的发送者头像（字形 / 底色 / 真图路径 / 发送者 userid）。fromMe 时按
     *  本端 (facetype, customface) 解析，否则按对端。真图未缓存会在内部入队下载，下载
     *  完成后经 OnAvatarDownloaded 回刷本气泡。单聊气泡不显示发送者名，故不设 senderName。
     *    msg：    要填写的消息（就地修改其 sender* 字段）。
     *    fromMe： 是否本端发出。
     */
    void FillSenderAvatar(balloonchatwnd::ChatMessage& msg, bool fromMe);

    /**
     *  打开字体设置浮层 —— 点击工具栏「AF」按钮时调用。锚定在按钮上方，面板内
     *  任意设置变化即经 OnFontChanged 回调更新当前字体（影响后续发送 + 输入框
     *  所见即所得）。
     */
    void OpenFontPanel();

    // 字体设置变化回调（C 风格函数指针，userdata 为本窗口 this）：更新当前字体、
    // 应用到输入框，并存为本会话的自定义字体（账号配置，重启后仍然有效）。
    static void OnFontChanged(void* userdata, const net::TextFont& font);

    // 字体浮层「使用默认字体」回调（userdata 为本窗口 this）：删掉本会话的自定义
    // 字体、改用全局默认，应用到输入框并存盘，浮层随之回显默认字体。
    static void OnUseDefaultFont(void* userdata);

    /**
     *  取本会话实际生效的聊天字体：本会话自定义 > 全局默认 > 未设置（见
     *  Session/ChatFontPrefs.h）。调试入口（没有登录账号或对端）下恒为未设置。
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
     *  WM_SIZE 处理：窗口尺寸变化后投递 kRelayoutMsg；不消费 WM_SIZE，
     *  仍交 DuiFrameWindow 链处理。
     */
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kRelayoutMsg 处理：强制客户区控件树重新布局 —— 修复窗口缩放后
     *  消息区未跟随重排、气泡内容溢出到输入区的问题。
     */
    LRESULT OnRelayout(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kSendOnEnterMsg 处理：聊天输入框按 Enter 触发发送（由输入框控件
     *  ChatInputRichEdit 投递），转调 DoSend()。
     */
    LRESULT OnSendOnEnter(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kOpenSendMenuMsg 处理：发送按钮「▼」被点（OnDuiNotify 收到下拉通知后投递
     *  本消息，避开在鼠标事件里直接弹模态菜单），弹出「Enter 发送 / Ctrl+Enter
     *  发送」切换菜单；选中后切换全局发送模式并刷新提示与 placeholder。
     */
    LRESULT OnOpenSendMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kOpenRecordMenuMsg 处理：工具栏「屏幕录制」被点后，在按钮下方弹出录制菜单（开始录制 / 停止录制、录制管理），
     *  按选择交给屏幕共享中心（docs/语音字幕方案.md 第 2 节第 10 条）。
     */
    LRESULT OnOpenRecordMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kDoScreenshotMsg 处理：在 DUI 鼠标事件之外发起截图（DoScreenshot）或弹出截图选项菜单
     *  （OpenScreenshotMenu），避免模态截图浮层或菜单在鼠标事件派发栈内运行导致的捕获 / 重入问题。
     *  wParam 为 balloonchatwnd::ChatScreenshotRequest。
     */
    LRESULT OnDoScreenshot(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_VIEW_CHAT_IMAGE：气泡双击图片冒泡上来，lParam 为图片本地路径（LPCTSTR）。
     *  调进程内共享看图窗（CBalloonImageViewerWindow::OpenShared）放大查看该图。
     */
    LRESULT OnViewImageMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_OPEN_LINK 处理：气泡里的超链接被点击（或右键选了「在浏览器中打开」）。
     *  lParam 为堆上的 std::wstring*（规范化后的 URL），本函数负责 delete。
     *  打开前先过 balloonchatwnd::IsSafeLinkUrl 的协议白名单 —— 只放行 http / https。
     */
    LRESULT OnChatOpenLink(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_COPY_SELECTION 处理：把消息区里当前选中的那段文字复制到剪贴板。
     *  内容口径与气泡右键「复制」完全一致（内联表情替换成 "[表情]"）。
     *  @return 有选区且已复制返回 1；消息区没有选区返回 0（调用方据此决定要不要
     *          把这次按键交回原处理者）。
     */
    LRESULT OnChatCopySelection(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_CANCEL_LINK_OPEN 处理：撤销上一条 WM_CHAT_OPEN_LINK 排队的打开动作
     *  （气泡双击时投来 —— 双击的语义是"选中整条"，不该顺带弹出浏览器）。
     */
    LRESULT OnChatCancelLinkOpen(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_FOCUS_AREA 处理：记下用户当前在哪一块操作（wParam 见 CustomMsgDef.h
     *  的 CHAT_FOCUS_*）。Ctrl+C 据它路由。
     */
    LRESULT OnChatFocusArea(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_LBUTTONDOWN 旁听：落点在消息区里就把逻辑焦点记成"接收框"。
     *
     *  只旁听、<u>不消费</u>（恒置 bHandled=FALSE），否则整个界面的鼠标交互都会失灵。
     *  输入框是无窗口控件，点它也会产生本消息、落点在发送框那一块矩形内；同时输入框
     *  自己也会投 WM_CHAT_FOCUS_AREA 上报（见 ChatInputRichEdit），两条路径结果一致。
     */
    LRESULT OnLButtonDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  真正打开排队中的那个链接（延时定时器到点时调）。打开前过协议白名单，
     *  只放行 http / https；没有排队的链接时为空操作。
     */
    void OpenPendingLink();

    /**
     *  WM_KEYDOWN 处理：焦点<u>不在输入框</u>时（点了消息区空白处等）按 Ctrl+C，
     *  同样复制消息区选区。焦点在输入框里的那条路走的是输入框键盘钩子 →
     *  WM_CHAT_COPY_SELECTION，两条路最终落到同一个函数上。
     *  非 Ctrl+C 的按键一律不消费。
     */
    LRESULT OnKeyDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_IMAGE_READY：ChatImageCache 后台把某张图片缩放完成后投递。重绘消息区，
     *  让图片气泡再画一次（这次查缓存命中、换成真图）。wParam / lParam 未用。
     */
    LRESULT OnImageReady(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  按当前全局发送模式刷新输入区左下角提示文字与输入框 placeholder。在 InitUi
     *  末尾、以及切换发送模式后调用。
     */
    void RefreshSendModeUi();

    /**
     *  撤回提示行「编辑」的到期处理（docs/撤回后重新编辑方案.md §3.3）：去掉已到期的「编辑」，再按最早的到期时刻
     *  重新定 kRecallEditTimerId；一个都不剩时停掉定时器。收到本人撤回、拉完历史、定时器触发时调用。
     */
    void RefreshRecallEditTimer();

    /**
     *  把已注入的对端在线状态(m_peerPresence)/签名(m_peerSignature)应用到头部信息块
     *  (ChatHeaderInfo)并触发其重绘。窗口尚未建好（取不到客户区或控件）时安全返回——
     *  InitUi 末尾会再调一次兜底。SetPeerPresence/SetPeerSignature 与 InitUi 都调它。
     */
    void ApplyPeerStatusToHeader();

    /**
     *  把头像灰彩告知消息列表（离线用户头像置灰，2026-10-03）：对方的气泡按对方的在线状态
     *  m_peerPresence，本人的气泡按本端连接态 m_connUiState。消息列表据此改已有的行、并记下来
     *  给之后追加的行用。窗口尚未建好（取不到消息列表）时安全返回 —— InitUi 末尾会再调一次。
     *  SetPeerPresence、ApplyConnectionState 与 InitUi 都调它。
     */
    void ApplyAvatarGrayToMessageList();

    /**
     *  按当前 m_peerName / m_peerUserId 重建标题栏图标与任务栏 / Alt-Tab 图标
     *  （monogram：名字首字 + 按 userid 取色）。先释放旧位图 / 图标再重建。调试入口
     *  （m_peerUserId==0）下不设。CreateAndShow 首次设图标与 UpdatePeerInfo 改名后
     *  重设都复用本 helper 背后的 MakeRoundTitleIcon / MakeMonogramAvatar。
     */
    void RebuildPeerIcons();

private:
    // 以下五条都是本窗口投给自己的私有消息，取 WM_APP 段而不是公共的 WM_USER 段。
    //
    // 理由：WM_USER 段被 UserSessionData.h / CustomMsgDef.h 那两批跨窗口消息占着，
    // 号是手工分配的，聊天窗在里面另起一段只会增加撞号面。这几条既不经
    // CFlamingoClient::ProxyWndProc 中转、也不受 FMG_MSG_FIRST / FMG_MSG_LAST 闸门
    // 约束，与那两批消息没有任何交集，放进 WM_APP 段更贴合它们的实际作用域。
    //
    // 取值段说明：本窗口用 WM_APP + 0x40 起，群聊窗（BalloonGroupChatWindow）用
    // WM_APP + 0x50 起，两边刻意错开。窗口私有消息即便同号也互不干扰（各发各的窗口），
    // 错开只是为了排查时一眼能看出某个号属于哪个窗口。仓库里其它窗口已占用的
    // WM_APP 偏移是 1~3、0x21、0x22、0x31、0x32、0x137~0x13A，均已避开。
    // 号段规约见开发约定文档「UI 约定」第 6 条，重号由
    // Source\Tests\MessageIdRegistryTests.cpp 的守卫用例把关。

    // 自定义消息：窗口缩放后投递，触发客户区重新布局（避开 WM_SIZE
    // 处理顺序问题 —— 等 DuiFrameWindow 把客户区缩放完再重排）。
    static const UINT kRelayoutMsg = WM_APP + 0x40;

    // 自定义消息：聊天输入框按 Enter 时投递，触发发送（Ctrl+Enter 则在框内换行）。
    static const UINT kSendOnEnterMsg = WM_APP + 0x41;

    // 自定义消息：发送按钮「▼」被点时投递，弹出发送模式切换菜单。
    static const UINT kOpenSendMenuMsg = WM_APP + 0x42;

    // 自定义消息：截图按钮被点时投递。截图浮层 overlay.Run 是个模态消息循环，若直接在
    // OnDuiNotify（DUI 鼠标事件同步派发栈）里跑，会引发捕获 / 重入问题，导致截图结束后
    // 发送按钮等点击失灵（与发送▼菜单同理）。故投递本消息、延后到鼠标事件外再发起截图。
    // 2026-10-05 起 wParam 为 balloonchatwnd::ChatScreenshotRequest：普通截图、截取全屏，
    // 或弹出 ▾ 截图选项菜单（菜单同样不能在鼠标事件里弹）。
    static const UINT kDoScreenshotMsg = WM_APP + 0x43;

    // 自定义消息：输入框内容可能变化时由键盘钩子投递（打字 / 退格 / 粘贴 / 输入法上屏），
    // 触发「正在输入」发包判断。wParam 为 1 表示输入框失焦（须立即收尾）、0 表示普通变化。
    static const UINT kInputChangedMsg = WM_APP + 0x44;

    // 自定义消息：工具栏「屏幕录制」被点时投递，延后到鼠标事件之外弹出录制菜单（菜单不能在鼠标事件里弹）
    static const UINT kOpenRecordMenuMsg = WM_APP + 0x45;

    /**
     *  kInputChangedMsg 处理：输入框内容可能变化了，交发送侧节流器判断要不要发包。
     *  wParam 非 0 表示失焦，此时无论输入框里还有没有字都立即收尾。
     */
    LRESULT OnInputChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  按节流器给出的动作发「正在输入」包。仅真实会话（m_peerUserId != 0）且当前
     *  在线时才真的发 —— 离线时这条提示既送不出去、也不该弹任何提示打扰用户。
     *    action：节流器返回的动作，取值见 balloonchatwnd::TypingAction。
     */
    void SendTypingAction(int action);

    /**
     *  收尾本端的「正在输入」通告（消息已发出、窗口关闭、切到离线时调用）：
     *  若此前正在通告则补发一包 state=0，让对端立刻收起提示。
     */
    void StopTypingNotify();

    /**
     *  按 m_peerTyping 的当前值刷新头部状态胶囊并重绘。窗口内容未就绪时安全返回。
     */
    void RefreshTypingHeader();

    // 本端打字的发包节流器（决定何时发 state=1 / state=0，见 TypingIndicator.h）。
    balloonchatwnd::TypingSendThrottle m_typingThrottle;
    // 对端"正在输入"的显示状态，含过期兜底（对方掉线时 state=0 收不到，靠它自动收起）。
    balloonchatwnd::TypingPeerState m_peerTyping;

    HWND    m_hMainDlg;      // 主面板窗口句柄（调试入口下关闭即退出程序）
    int     m_replyIndex;    // mock 自动回复轮转下标（仅 --chatwindow 调试入口下用）
    // 当前连接态（ConnectionUiState：0=在线 1=正在连接 2=离线）。由 ApplyConnectionState
    // 写入，发送路径读它做门禁。默认按"在线"起步 —— 窗口能被打开说明此刻本就在线，
    // 随后主面板会把真实状态推过来。
    int     m_connUiState;

    UINT    m_peerUserId;    // 本会话对端 userid（账号ID）；0 表示未绑定（调试入口）
    CString m_peerName;      // 对端昵称（由打开方传入）；空则回退查 CUserMgr
    HWND    m_hostPanel;     // 主面板 HWND，点头部信息块经它打开对方资料窗；0 表示调试入口
    int     m_inCallMedia;   // 本人此刻与对方通话的媒体类型（1 语音、2 视频；0 不在通话中），通话卡片据此写「通话中」；由 SetInCallMedia 设置
    long long m_pendingLocateMsgid;   // 待定定位的目标 msgid（历史在途时暂存，渲染后补定位）；0=无
    // 本会话当前被 Pin 的消息条数，驱动头部下方那条「已 Pin N 条」提示带的显隐与文字。
    // 开窗时为 0（提示带隐藏），随后服务端返回的被 Pin 消息全集经 SetPinnedCount 校准；
    // 之后每收到一条 Pin / 取消 Pin 推送由 AdjustPinnedCount 就地 ±1。窗口级、不落盘。
    int     m_pinnedCount;
    int     m_peerPresence;  // 对端在线状态(v2 presence_type)，-1=未注入(头部回退 mock)；SetPeerPresence 写入
    CString m_peerSignature; // 对端个性签名（由打开方注入）；配合 m_peerSignatureSet 决定是否覆盖头部
    bool    m_peerSignatureSet; // 是否已注入过对端签名（注入空串也算，用于隐藏 mock 签名）
    int     m_peerFacetype;  // 对端头像类型（见 BalloonMain/AvatarSpec.h AvatarKind）；决定标题/任务栏画字形头像还是回退
    std::string m_peerCustomface; // 对端头像标识（UTF-8）：字形为 "glyph|colorIdx"，自定义图为 fileid
    HBITMAP m_titleIconBmp;  // 标题栏图标用的对方 monogram 头像位图，本类持有，析构释放
    HICON   m_hTaskIcon;     // 任务栏/Alt-Tab 图标（由上述位图转换），本类持有，析构销毁
    bool    m_peerIconGray;  // 上面两个图标当前是否按灰度生成（对方离线时为 true）；对方状态变化后与它比较，变了才重建图标

    // 窗口抖动动画状态：m_shakeStep 为当前偏移序列下标，-1 表示未在抖动；
    // m_shakeOrigin 为抖动起点的窗口左上角（屏幕坐标），动画结束后归位到此点。
    int     m_shakeStep;
    POINT   m_shakeOrigin;
    // 发送侧节流器：限制本窗口向对端连发窗口抖动的频率（同一目标最小间隔
    // kShakeSendMinIntervalMs，5 秒）。距上次发送不足该间隔的点击被拦截、本窗口给出
    // 提示，不发送也不晃自己。
    balloonchatwnd::ShakeRateLimiter m_sendShakeLimiter;
    // 接收侧节流器：限制处理对端连发抖动的频率（同一来源最小间隔
    // kShakeRecvMinIntervalMs，4 秒），避免被刷屏式晃动；超频的抖动被静默忽略。
    balloonchatwnd::ShakeRateLimiter m_recvShakeLimiter;

    // 表情选择浮层（按需打开，本类持有）。
    CEmojiPickerPopup m_emojiPopup;       // 旧 Unicode emoji 浮层（保留不再挂入）
    CFaceImagePickerPopup m_facePopup;    // 程序目录/Face 图片表情浮层（"表情"按钮现弹它）

    // 当前聊天字体（影响后续发送的消息与输入框外观）。InitUi 时按"本会话自定义 >
    // 全局默认"取得（见 ResolveChatFont），字体浮层改动后更新；m_valid 为 false 表示
    // 两者都没设过、消息不带字体。
    net::TextFont m_currentFont;
    // 输入框原本的文字色：InitUi 在应用任何聊天字体之前记下，恢复默认外观时用它。
    COLORREF m_inputDefaultTextColor;
    // 输入框（ChatInputRichEdit）当前生效的自定义字体句柄。富文本控件的
    // SetDefaultFontFromHFONT 不接管 HFONT 所有权，故由本窗口持有：每次应用新字体
    // 前先 DeleteObject 上一只再换新，析构时释放，避免 GDI 句柄泄漏。NULL=用默认字体。
    HFONT m_hInputFont;
    // 插入输入框的内联图片（截图「发送到对话框」、粘贴、拖入）的源文件路径，按插入顺序记录。
    // 每张图的下标记在它的图片标记里，发送时按标记取对应项直接发（不从 OLE 抽位图，避免
    // 黑块 / 重编码）。每次发送后清空。
    std::vector<std::wstring> m_pendingShotPaths;
    // 字体设置浮层（按需打开，本类持有）。
    CFontSettingPopup m_fontPopup;
    // 用户当前在哪一块操作（取 CustomMsgDef.h 的 CHAT_FOCUS_*）。Ctrl+C 据它路由：
    // 在发送框就让输入框自己复制，在接收框才复制气泡选区。窗口刚打开时焦点在
    // 输入框，故初值为发送框。
    int m_focusArea;
    // 排队待打开的链接 URL：点了链接先记在这里、等一个双击间隔再真打开（见
    // kLinkOpenTimerId 的注释）；期间收到双击就清空。空串表示当前没有排队的链接。
    std::wstring m_pendingLinkUrl;
    // 拖文件进窗口发送的接收器。InitUi 末尾绑定到本窗口与输入框，OnCloseMsg 解绑。
    balloonchatwnd::ChatDropController m_dropCtrl;
    // 轻量提示条，用来汇总"哪些拖进来的东西没发出去"。所有权在控件树（客户区根
    // 容器的最后一个子），本指针只作访问用；窗口未初始化完成时为 NULL。
    balloonwjui::DuiToast* m_dropToast;
    balloonchatwnd::CVoiceRecordSession m_voiceSession;   // 录音会话（语音消息第 6g 步）：持有录音条与流程状态机，InitUi 时把
                                                          // 录音条建在布局的占位里；它是语音消息中心的录音与试听通知接收方，
                                                          // 关窗与析构时取消或丢弃还没发送的录音
    std::unique_ptr<ballooncode::CCodeComposeWindow> m_codeComposeWnd;   //「发送代码」窗口；第一次打开时创建，本窗口关闭时销毁其窗口
    std::wstring m_codePasteText;   //「以代码片段发送」提示条显示时输入框里的文字；提示条收起时为空。
                                    // 之后输入框内容与它不同即视为用户继续输入，提示条随即收起

    // ======== 单聊密聊（docs/密聊方案.md 4.7、4.8；实现在 BalloonChatWindowSecret.cpp）========

    // 本窗口投给自己的密聊操作：头部「密聊」按钮被点后，等鼠标事件处理完再执行（本机无法防截图时要弹确认框，
    // 模态对话框不能在 DUI 鼠标事件的派发过程中弹出）。接在本窗口已用的 WM_APP + 0x40~0x45 之后。wParam 为 SecretWindowAction
    static const UINT kSecretActionMsg = WM_APP + 0x46;

    // 密聊邀请卡片的倒计时定时器 id：有卡片时每秒刷新一次各张卡片上的剩余秒数，没有卡片时停掉
    static const UINT_PTR kSecretInviteTimerId = 0xC006;

    // kSecretActionMsg 的 wParam：要执行的密聊操作
    enum SecretWindowAction
    {
        kSecretActInvite     = 1,   // 点了头部「密聊」按钮：发起邀请
        kSecretActExit       = 2,   // 点了密聊头部条上的「退出密聊」：不再二次确认，直接退出并清除内容
        kSecretActReinvite   = 3,   // 点了「只能查看」状态头部条上的「重新邀请」：与「密聊」按钮走同一条发起流程
        kSecretActSafetyCode = 4,   // 点了密聊头部条上的「安全码」：弹框显示，供双方核对
    };

    // 列表里一张密聊邀请卡片的状态。卡片本身在消息列表里，首屏历史渲染会清空列表，之后据此重建
    struct SecretInviteCardInfo
    {
        unsigned long long m_deadlineTick;   // 邀请到期时刻（GetTickCount64 口径，毫秒）
        bool               m_iAmInviter;     // 本人是不是发起方：发起方的卡片画在右侧、只有「取消」
        int                m_peerProtect;    // 对方的防截图级别；发起方在邀请阶段不知道，按能防截图处理

        SecretInviteCardInfo()
            : m_deadlineTick(0)
            , m_iAmInviter(false)
            , m_peerProtect(0)
        {
        }
    };

    // WM_CHAT_SECRET_INVITE_ACTION 处理：邀请卡片上的按钮被点。wParam 为 ChatSecretInviteAction，lParam 为堆上的
    // unsigned long long*（会话号），处理后 delete
    LRESULT OnChatSecretInviteAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // kSecretActionMsg 处理：按 wParam 执行一项密聊操作
    LRESULT OnSecretActionMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 发起密聊：在线检查、已有邀请或会话时提示、本机无法防截图时先确认，然后经密聊宿主发 2602
    void StartSecretInvite();

    // 登记一张邀请卡片并插入列表、开始倒计时。同一会话号已登记时只更新到期时刻与防截图级别
    //   sessionId：会话号；iAmInviter：本人是不是发起方；remainMs：剩余毫秒数；peerProtect：对方的防截图级别
    void AddSecretInviteCard(unsigned long long sessionId, bool iAmInviter, unsigned int remainMs, int peerProtect);

    // 按登记的状态把一张卡片插到列表末尾；列表里已有同一会话号的卡片时什么都不做
    void InsertSecretInviteCardRow(unsigned long long sessionId, const SecretInviteCardInfo& info);

    // 移除一张卡片（登记与列表里的那一行）；没有卡片了就停掉倒计时定时器
    void RemoveSecretInviteCard(unsigned long long sessionId);

    // 补出应有的邀请卡片：先向密聊宿主查对方发来、还在倒计时内的邀请，再把登记了却不在列表里的卡片重新插入。
    // 建窗之后与首屏历史渲染完之后各调一次（首屏渲染会清空列表）
    void RestoreSecretInviteCards();

    // 倒计时定时器触发：刷新各张卡片的剩余秒数；超过到期时刻仍没等到服务端的结果（如断网），本机撤回或忽略后移除
    void OnSecretInviteTick();

    // 对方是不是本人（「我的电脑」一类的自聊会话不显示密聊入口）
    bool IsPeerSelf() const;

    /**
     *  进入「密聊中」：从普通状态进入时先清空列表里的普通历史；从「只能查看」经重新邀请回来时保留已有内容，
     *  新消息接在后面。打开防截图、隐藏受管按钮、显示头部条、标题加「（密聊）」（docs/密聊方案.md 4.7）。
     *    sessionId：会话号；peerProtect：对方的防截图级别
     */
    void EnterSecretActive(unsigned long long sessionId, int peerProtect);

    /**
     *  转为「只能查看」：会话在服务端结束而本人没有主动退出时调用。内容保留、防截图保持，输入框、发送、表情、图片
     *  不可用，头部条按原因换文字，「重新邀请」按入口是否可用显示。
     *    endReason：secret_end_reason（1~5），或 kSecretEndLocalError / kSecretEndLocalOffline
     */
    void EnterSecretReadOnly(int endReason);

    /**
     *  本人退出密聊：结束会话（closeSession 为 true 时经宿主通知服务端并清零密钥）、撤销防截图、清除列表里的全部
     *  密聊内容。reloadHistory 为 true 时（点了「退出密聊」）再恢复普通状态的按钮与标题、重新拉取最新一页历史，
     *  首屏到达后插入「你已退出密聊」提示；关窗、窗口销毁时传 false，只做清除。普通状态下调用什么都不做。
     */
    void ExitSecretMode(bool closeSession, bool reloadHistory);

    // 按 m_secretState 刷新界面：受管按钮的显隐、输入框与发送按钮是否可用、头部条、窗口标题
    void ApplySecretUi();

    // 按 m_secretState、结束原因与双方的防截图级别刷新头部条：显隐、状态文字、防截图提示行（有提示时头部条加高）、
    // 三个链接的显隐
    void ApplySecretBanner();

    // 弹框显示安全码（密聊中才有）
    void ShowSecretSafetyCode();

    // 插入一条密聊相关的居中提示（邀请没能建立、本人已退出等）。AppendSystemTip 在密聊状态下不插入普通提示，
    // 密聊自己的提示走这里，任何状态下都插入。text 为空时什么都不做
    void AppendSecretTip(const CString& text);

    /**
     *  密聊中发送输入框里的一段文字（DoSend 在密聊状态下逐段调用）：交给密聊模块加密发送，成功后追加本人的密聊气泡。
     *  不经过 SendChatTextMessage、不登记会话冒泡、不写本地库（docs/密聊方案.md 4.5）。
     *    text：该段文字（表情以 /f["id"] 编码）
     *  @return 已发出返回 true；超长或模块拒绝时提示并返回 false
     */
    bool SendSecretTextPart(const CString& text);

    // 密聊中发送输入框里的一张内嵌图（DoSend 在密聊状态下调用）：取 m_pendingShotPaths[imageIndex] 交给 SendSecretImageFile
    bool SendSecretImagePart(int imageIndex);

    /**
     *  密聊中发送一张本地图片：读入整个文件（不超过 10 MB，超过时提示并返回 false），取宽高后交给密聊模块分块加密发送，
     *  字节登记为内存图片（secretmem:// 伪路径）供本人的气泡显示；不上传图片服务器、不写任何文件（docs/密聊方案.md 4.10）。
     *    path：图片文件完整路径
     *  @return 已开始发送返回 true
     */
    bool SendSecretImageFile(const std::wstring& path);

    // 把图片真实宽高按普通图片气泡的规则缩成气泡里的显示尺寸（转调本文件的 ComputeImageDisplaySize，密聊图片共用）
    static void SecretImageDisplaySize(int realW, int realH, int& dispW, int& dispH);

    std::map<unsigned long long, SecretInviteCardInfo> m_secretInviteCards;   // 列表里应有的邀请卡片：会话号 → 状态
    bool m_secretInviting;   // 已经发出 2602、还没收到应答：期间再点「密聊」只提示，不再发第二个邀请
    int  m_secretState;      // 窗口的密聊状态，取值见 balloonchatwnd::ChatSecretState
    unsigned long long m_secretSessionId;   // 密聊中的会话号；只能查看时为已结束的那个会话；普通状态为 0
    int  m_secretEndReason;  // 只能查看时的结束原因（见 EnterSecretReadOnly）；其余状态为 0
    int  m_secretMyProtect;  // 本机对本窗口实际达到的防截图级别（secretchat::CaptureProtectLevel），进入密聊时取得
    int  m_secretPeerProtect;   // 对方的防截图级别：建立时由 2605 带来，状态 6 时更新
    bool m_secretExitTipPending;   // 本人刚退出密聊、正在重新拉历史：首屏渲染完插入「你已退出密聊」提示后清掉
    std::map<unsigned int, unsigned int> m_secretRecvImageBytes;   // 正在接收的对方图片：图片编号 → 原图字节数（算进度用），收齐或失败后删除
    balloonchatwnd::SecretImageOverlay* m_secretImageOverlay;   // 密聊图片的大图浮层（所有权在控件树，挂在客户区根容器末尾）；InitUi 之前为 NULL
    CString m_windowTitle;   // 窗口标题（不含「（密聊）」后缀）：建窗与对方改名时更新，密聊状态下在它后面加后缀
};
