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

#include "../../balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY / DuiNotify
#include "../CustomMsgDef.h"                        // WM_VIEW_CHAT_IMAGE（双击图片打开看图窗）
#include "EmojiPickerPopup.h"                       // 旧 Unicode emoji 浮层（保留不再挂入；成员需完整类型）
#include "FaceImagePickerPopup.h"                   // Bin/Face 图片表情浮层（成员需完整类型）
#include "FontSettingPopup.h"                       // 字体设置浮层（成员需完整类型；含 net::TextFont）
#include "ChatDropController.h"                     // 拖文件进窗口发送（成员需完整类型）
#include "ChatHistoryPaging.h"                      // HistoryPagingState（向上翻页，成员需完整类型）
#include "ShakeRateLimiter.h"                       // 窗口抖动收发节流器（成员需完整类型）
#include "TypingIndicator.h"                        // 「正在输入」收发状态机（成员需完整类型）
#include <string>                                   // 发图进度 / 结果回投用 clientMsgId
#include <vector>                                   // 待发送截图源路径表 m_pendingShotPaths

class CFlamingoClient;

//轻量提示条（拖放里"这些没发出去"的汇总提示）。只按指针持有，前向声明即可。
namespace balloonwjui { class DuiToast; }

//v2 单聊消息推送 / 历史消息解析结果（由 BalloonMainPanel 路由进来）。
namespace net { struct MessageNotify; struct SyncMessagesResult; }
//一条会话消息（结构定义在 ChatMockData.h）。ChatFileActionData 定义在 ChatFileAction.h，
//此处只按 const 引用传参，前向声明即可——该头文件要求先引入 ATL/WTL，直接 include 会
//把这个约束传染给所有包含本头文件的编译单元。
namespace balloonchatwnd { struct ChatMessage; class ChatMessageList; struct ChatFileActionData; }

/**
 *  CBalloonChatWindow
 *
 *  新版气泡式单聊窗口。继承 balloonwjui::DuiFrameWindow —— 由它提供无
 *  边框自绘窗体、标题栏与 min/max/close 按钮。非模态窗口，消息由进程
 *  主消息循环统一泵送。
 */
class CBalloonChatWindow : public balloonwjui::DuiFrameWindow,
                           public balloonchatwnd::IChatDropSink
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
     *    hMainDlg：CMainDlg 的窗口句柄；调试入口下关闭本窗口即退出程序，
     *              据此通知 CMainDlg。
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
     *  收到对方发来的一条单聊消息：渲染为对方气泡。由主面板在收到
     *  cmd_message_notify 推送、按发送方 userid 找到本窗口后调用。
     *    notify：消息推送解析结果（本轮仅处理文本，m_text 为正文）。
     */
    void OnRecvMessage(const net::MessageNotify& notify);

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
    //与可用按钮都不同，取消后只留「重新发送」。
    void OnFileSendResult(bool ok, bool cancelled, const std::string& clientMsgId);
    //更新收到的某条文件的下载进度（按服务器 msgid 定位）。
    void OnFileDownloadProgress(long long msgid, long long received, long long total);
    //收到的某条文件下载完成：按 msgid 填本地落地路径并渲染。
    void OnFileDownloaded(long long msgid, const std::wstring& localPath);
    //收到的某条文件下载失败：按 msgid 标失败；cancelled 为 true 则标「已取消」（理由同上）。
    void OnFileDownloadFailed(long long msgid, bool cancelled);
    /**
     *  对方把本端发出的某个文件收到什么程度了（cmd_file_transfer_ack 推送到达时）：
     *  按 msgid 找到本端那条文件气泡，在其状态行显示「对方正在接收 / 已接收 / 已拒绝」。
     *  本窗口没有该条时安静忽略（消息尚未加载进当前视图）。由主面板路由调用。
     *  @param msgid     本端发出的那条文件消息的服务器 msgid
     *  @param peerState 对端收取状态，取值见 balloonchatwnd::ChatFilePeerState
     */
    void OnFilePeerAck(long long msgid, int peerState);

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
        MESSAGE_HANDLER(kInputChangedMsg, OnInputChanged)
        MESSAGE_HANDLER(WM_VIEW_CHAT_IMAGE, OnViewImageMsg)
        MESSAGE_HANDLER(WM_CHAT_FILE_ACTION, OnChatFileAction)
        MESSAGE_HANDLER(WM_CHAT_IMAGE_ACTION, OnChatImageAction)
        MESSAGE_HANDLER(WM_CHAT_RECALL_REQUEST, OnChatRecallRequest)
        MESSAGE_HANDLER(WM_CHAT_PIN_REQUEST, OnChatPinRequest)
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
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

public:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端，引用不持有

    /**
     *  应用当前连接态：顶部提示条的显隐与文字、输入框是否只读、发送按钮是否可用。
     *
     *  由 CMainDlg::BroadcastConnectionState 经主面板逐窗转发；用户主动离线、从离线
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
     *  IChatDropSink 覆写：文件被放下。按 ChatDropModel 归类后，图片走
     *  SendImageFile、其余走 SendFileData；发不出去的项用 toast 汇总提示。
     *    paths：      拖进来的路径列表。
     *    forceAsFile：落手时按住了 Shift —— 图片也按原文件发。
     */
    void OnChatFilesDropped(const std::vector<std::wstring>& paths,
                            bool forceAsFile) override;

private:
    /**
     *  发送前的连接态门禁：不可发送时弹出提示并返回 false。
     *    action：操作类别，取值见 BalloonMain/OnlineCapability.h 的 OnlineActionKind。
     *  返回：true 表示可以继续该操作。
     */
    bool CheckOnlineFor(int action);

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
     *  WM_CLOSE 处理：销毁本窗口；调试入口下同时通知 CMainDlg 退出程序。
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
    void SendTextPartReal(const CString& text);

    // 发送输入框里第 imageIndex 个内嵌图（真实会话）：取 m_pendingShotPaths[imageIndex]
    // 这个源 PNG 直接走 SendImageFile 发出（不从 OLE 抽位图）。供 DoSend 逐段发送时调用。
    //   imageIndex：内嵌图在文档里的序号（0-based，与 0xFFFC 出现顺序一致）。
    void SendImagePartReal(int imageIndex);

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
     *  调起全屏截图浮层（截图工具栏按钮）。模态运行：复制 / 保存由浮层内部完成，
     *  用户选「发送」时经 OnScreenshotSend 回调把截图作为图片消息发到当前会话。
     *  仅真实会话（m_peerUserId != 0）下生效。
     */
    void DoScreenshot();

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
     *    path：本地文件全路径（UTF-16）。
     */
    void SendFileData(const std::wstring& path);

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

    /**
     *  判断某个本地路径当前是否是一份可用的完整文件（仅取属性、不读盘）。
     *  用于渲染文件气泡前确认"登记的落点 / 缓存副本还在不在"，决定给「打开」还是「下载」。
     *  @param path       待查的本地完整路径；空串一律返回 false
     *  @param expectSize 期望字节数；>0 时要求实际大小与之相符（挡掉半截的下载残留与
     *                    被改写的同名文件），<=0 时退回只判存在
     *  @return 文件存在、非目录、且大小相符返回 true
     */
    static bool LocalFileUsable(const std::wstring& path, long long expectSize);

    void RenderIncomingFile(balloonchatwnd::ChatMessageList* list,
                            bool fromMe, const net::MessageNotify& notify);

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
     *  把本窗口弹到前台：最小化则还原、隐藏则显示，并尽力置顶抢焦点。收到对端
     *  窗口抖动时调用，确保接收方能立即看到抖动。
     */
    void BringChatWindowToFront();

    /**
     *  在聊天区追加一条居中系统提示（kMsgTime 类型，如"对方向你发送了窗口抖动"）。
     *    text：提示文字。
     */


    /**
     *  打开表情选择面板 —— 点击工具栏"表情"按钮时调用。锚定在表情按钮上方，
     *  弹出 Bin/Face 图片表情浮层（已取代旧的 Unicode emoji 浮层），选中某表情
     *  经回调以"带 faceId 标记的内联图"插入输入框。
     */
    void OpenEmojiPicker();

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
    // 应用到输入框、写回 flamingo.ini。
    static void OnFontChanged(void* userdata, const net::TextFont& font);

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
     *  kSendOnEnterMsg 处理：聊天输入框按 Enter 触发发送（由 ChatInputKeyFilter
     *  的子类过程投递），转调 DoSend()。
     */
    LRESULT OnSendOnEnter(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kOpenSendMenuMsg 处理：发送按钮「▼」被点（OnDuiNotify 收到下拉通知后投递
     *  本消息，避开在鼠标事件里直接弹模态菜单），弹出「Enter 发送 / Ctrl+Enter
     *  发送」切换菜单；选中后切换全局发送模式并刷新提示与 placeholder。
     */
    LRESULT OnOpenSendMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  kDoScreenshotMsg 处理：在 DUI 鼠标事件之外发起截图（DoScreenshot），避免模态
     *  截图浮层在鼠标事件派发栈内运行导致的捕获 / 重入问题。
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
     *  点输入框不会产生本消息（那是子窗口的事），发送框那一侧由输入框的键盘钩子报。
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
     *  把已注入的对端在线状态(m_peerPresence)/签名(m_peerSignature)应用到头部信息块
     *  (ChatHeaderInfo)并触发其重绘。窗口尚未建好（取不到客户区或控件）时安全返回——
     *  InitUi 末尾会再调一次兜底。SetPeerPresence/SetPeerSignature 与 InitUi 都调它。
     */
    void ApplyPeerStatusToHeader();

    /**
     *  按当前 m_peerName / m_peerUserId 重建标题栏图标与任务栏 / Alt-Tab 图标
     *  （monogram：名字首字 + 按 userid 取色）。先释放旧位图 / 图标再重建。调试入口
     *  （m_peerUserId==0）下不设。CreateAndShow 首次设图标与 UpdatePeerInfo 改名后
     *  重设都复用本 helper 背后的 MakeRoundTitleIcon / MakeMonogramAvatar。
     */
    void RebuildPeerIcons();

private:
    // 自定义消息：窗口缩放后投递，触发客户区重新布局（避开 WM_SIZE
    // 处理顺序问题 —— 等 DuiFrameWindow 把客户区缩放完再重排）。
    static const UINT kRelayoutMsg = WM_USER + 0x601;

    // 自定义消息：聊天输入框按 Enter 时投递，触发发送（Ctrl+Enter 则在框内换行）。
    static const UINT kSendOnEnterMsg = WM_USER + 0x602;

    // 自定义消息：发送按钮「▼」被点时投递，弹出发送模式切换菜单。
    static const UINT kOpenSendMenuMsg = WM_USER + 0x603;

    // 自定义消息：截图按钮被点时投递。截图浮层 overlay.Run 是个模态消息循环，若直接在
    // OnDuiNotify（DUI 鼠标事件同步派发栈）里跑，会引发捕获 / 重入问题，导致截图结束后
    // 发送按钮等点击失灵（与发送▼菜单同理）。故投递本消息、延后到鼠标事件外再发起截图。
    static const UINT kDoScreenshotMsg = WM_USER + 0x604;

    // 自定义消息：输入框内容可能变化时由键盘钩子投递（打字 / 退格 / 粘贴 / 输入法上屏），
    // 触发「正在输入」发包判断。wParam 为 1 表示输入框失焦（须立即收尾）、0 表示普通变化。
    static const UINT kInputChangedMsg = WM_USER + 0x605;

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

    HWND    m_hMainDlg;      // CMainDlg 窗口句柄（调试入口下关闭即退出程序）
    int     m_replyIndex;    // mock 自动回复轮转下标（仅 --chatwindow 调试入口下用）
    // 当前连接态（ConnectionUiState：0=在线 1=正在连接 2=离线）。由 ApplyConnectionState
    // 写入，发送路径读它做门禁。默认按"在线"起步 —— 窗口能被打开说明此刻本就在线，
    // 随后主面板会把真实状态推过来。
    int     m_connUiState;

    UINT    m_peerUserId;    // 本会话对端 userid（账号ID）；0 表示未绑定（调试入口）
    CString m_peerName;      // 对端昵称（由打开方传入）；空则回退查 CUserMgr
    HWND    m_hostPanel;     // 主面板 HWND，点头部信息块经它打开对方资料窗；0 表示调试入口
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

    // 窗口抖动动画状态：m_shakeStep 为当前偏移序列下标，-1 表示未在抖动；
    // m_shakeOrigin 为抖动起点的窗口左上角（屏幕坐标），动画结束后归位到此点。
    int     m_shakeStep;
    POINT   m_shakeOrigin;
    // 发送侧节流器：限制本窗口向对端连发窗口抖动的频率（同一目标最小间隔
    // kShakeMinIntervalMs）。距上次发送不足该间隔的点击被拦截、本窗口给出
    // 提示，不发送也不晃自己。
    balloonchatwnd::ShakeRateLimiter m_sendShakeLimiter;
    // 接收侧节流器：限制处理对端连发抖动的频率（同一来源最小间隔
    // kShakeMinIntervalMs），避免被刷屏式晃动；超频的抖动被静默忽略。
    balloonchatwnd::ShakeRateLimiter m_recvShakeLimiter;

    // 表情选择浮层（按需打开，本类持有）。
    CEmojiPickerPopup m_emojiPopup;       // 旧 Unicode emoji 浮层（保留不再挂入）
    CFaceImagePickerPopup m_facePopup;    // Bin/Face 图片表情浮层（"表情"按钮现弹它）

    // 当前聊天字体（影响后续发送的消息与输入框外观）。InitUi 时从 flamingo.ini
    // 载入，面板改动后写回；m_valid 为 false 表示尚未设过、消息不带字体。
    net::TextFont m_currentFont;
    // 输入框（DuiRichEditHost）当前生效的自定义字体句柄。RichEdit 的
    // SetDefaultFontFromHFONT 不接管 HFONT 所有权，故由本窗口持有：每次应用新字体
    // 前先 DeleteObject 上一只再换新，析构时释放，避免 GDI 句柄泄漏。NULL=用默认字体。
    HFONT m_hInputFont;
    // 「发送到对话框」插入输入框的截图源 PNG 路径，按插入顺序记录。发送时第 k 个内嵌图
    // 取本表第 k 项直接发（不从 OLE 抽位图，避免黑块 / 重编码）。每次发送后清空。
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
};
