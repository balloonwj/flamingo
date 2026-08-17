/**
 *  ChatMessageList.h
 *
 *  单聊窗的消息区 —— 一个可滚动的消息行容器。每个子控件是一条
 *  ChatBubble（变高行）；本控件按各行的 GetRowHeight() 自上而下堆叠，
 *  超出视口即可用鼠标滚轮滚动，右侧画一条细滚动条。
 *
 *  本控件还承载消息的"多选模式"：进入后底部让出一条操作条（取消 /
 *  转发 N / 删除 N），各 ChatBubble 行首显示勾选圈。复制 / 删除 /
 *  撤回 / 转发 / 引用回复等单条操作由 ChatBubble 的右键菜单回调到
 *  本类对应的 RequestXxx 接口。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "ChatMockData.h"

namespace balloonchatwnd {

/**
 *  把一段 Unicode 文字放进剪贴板。
 *
 *  放在本文件是因为"选中一段文字然后复制"有<u>两条</u>入口 —— 气泡的右键「复制」
 *  与消息区的 Ctrl+C —— 两处必须写出同样的剪贴板内容，实现只该有一份。之所以不
 *  放进 ChatTextSelection：那是明确的<u>纯逻辑</u>模块，不引 windows.h，而剪贴板
 *  少不了 HWND。
 *    owner：拥有本次剪贴板会话的窗口。
 *    text： 要复制的文字；NULL 或空串时什么都不做。
 */
void PutTextToClipboard(HWND owner, LPCWSTR text);

class ChatBubble;

/**
 *  ChatMessageList —— 可滚动的消息行容器。
 */
class ChatMessageList : public balloonwjui::DuiControl
{
public:
    ChatMessageList();
    ~ChatMessageList();

    // 把滚动位置移到底部（显示最新消息）。须在填充消息后调用一次。
    void ScrollToBottom();

    // 滚动到带指定服务器 msgid 的消息行，使其进入视口顶部附近（供历史消息记录窗"定位"
    // 用）。找到并滚动成功返回 true；列表里没有该 msgid（如较早消息未加载）返回 false。
    //   msgid：目标消息的服务器 msgid；0 一律返回 false。
    bool ScrollToMsgid(long long msgid);

    // 追加一条消息 —— 新建一个 ChatBubble 挂到末尾，重新布局并滚到底部。
    //   msg：要追加的消息。
    void AppendMessage(const ChatMessage& msg);

    /**
     *  向上翻页渲染的<u>开始</u>标记。
     *
     *  用法：BeginPrependBatch() → 照常用 AppendMessage / 各类渲染入口把这一页
     *  更早的消息按时间正序灌进来 → EndPrependBatch()。中间这一段追加到末尾的行，
     *  会在 End 时被整体挪到列表头部。
     *
     *  之所以做成"标记 + 照常追加"而不是一个吃 vector 的 PrependMessages：两个
     *  聊天窗的历史渲染并不是简单地把 ChatMessage 灌进来 —— 图片消息要插"下载中"
     *  气泡并触发下载、发送人头像要按成员表解析。把那套逻辑照抄一份到翻页分支，
     *  两份迟早跑偏（@ 那次就是历史与实时各写一份漏了填 ats）。包一层标记，两条
     *  路就能共用同一段渲染代码。
     *
     *  批次期间 AppendMessage <u>不会</u>滚到底 —— 翻页时用户正盯着某处看，滚到底
     *  等于把他甩走。
     */
    void BeginPrependBatch();

    /**
     *  向上翻页渲染的<u>结束</u>标记：把本批新追加的行整体挪到列表头部，并把滚动
     *  偏移按新增高度补偿，使视口里的内容纹丝不动。
     *
     *  补偿是这件事的关键：往头部插内容会把下面所有行整体推下去，不补偿的话用户
     *  正在看的内容会突然跳走。做法是取批次前后的内容总高之差 —— 比自己逐行累加
     *  行高可靠，后者还要先把每行 Measure 一遍，容易与列表内部口径不一致。
     *
     *  本批一行都没追加时什么都不做。
     */
    void EndPrependBatch();

    // ---- 向上翻页的滚动状态查询（供宿主聊天窗判定要不要拉下一页）----
    int ScrollY() const       { return m_scrollY; }
    int ViewHeight() const    { return m_viewHeight; }
    int ContentHeight() const { return m_contentHeight; }


    // 清空所有消息行（销毁全部 ChatBubble、滚动位置归零、重排重绘）。历史消息
    // 应答是"最近 N 条"的完整快照，渲染前先清空，避免与收到推送时乐观追加的
    // 行叠加导致重复 / 错位。
    void ClearAllMessages();

    /**
     *  按服务器 msgid 取回该行承载的消息（供宿主组装引用快照）。
     *    msgid：目标消息的服务器 msgid；0 一律返回 false。
     *    out：  [出参] 命中时填入该行的消息。
     *  @return 找到返回 true；列表里没有该 msgid（如更早未加载）返回 false。
     */
    bool GetMessageByMsgid(long long msgid, ChatMessage& out) const;

    // 列表里是否已存在带该服务器 msgid 的图片气泡（收到 live 图片时去重用，避免与
    // 历史已渲染的同一条重复 / 错位）。msgid 为 0 一律返回 false。
    bool HasImageMsgid(long long msgid) const;

    // ---- 发图气泡的发送态更新（按 clientMsgId 定位本端"发送中"的图片气泡）----

    // 更新某条图片消息的上传进度（0-100）并置"上传中"态。clientMsgId 找不到则忽略。
    void UpdateImageUploadProgress(const std::string& clientMsgId, int percent);

    // 把某条图片消息标为"发送失败"态。clientMsgId 找不到则忽略。
    void MarkImageUploadFailed(const std::string& clientMsgId);

    // 把某条图片消息置为"正常"态（上传完成）。clientMsgId 找不到则忽略。
    void MarkImageUploadDone(const std::string& clientMsgId);

    /**
     *  上传成功后，把服务端返回的 fileid / md5 回填到本端那条图片气泡上。
     *
     *  本端发出的图，气泡是在<u>上传之前</u>就插进列表的，那时这两项还不存在。它们
     *  只影响一件事：这条消息将来被引用时，要写进 content.quote 供收方画缩略图 ——
     *  不回填的话，引用自己发的图永远带不出定位信息，对方的引用块只有一块空白。
     *    clientMsgId：本端发送时生成的客户端消息ID；空串一律忽略。
     *    fileid / md5：服务端返回的图片文件ID与内容MD5。
     */
    void SetImageIdsByClientMsgId(const std::string& clientMsgId,
                                  const CString& fileid, const CString& md5);

    // ---- 收到图片的下载态更新（按服务器 msgid 定位"下载中"的图片气泡）----

    // 某条收到的图片下载完成：按 msgid 找到本行，填本地缓存路径并渲染。找不到则忽略。
    void SetImageDownloadedByMsgid(long long msgid, const CString& localPath);

    // 某条收到的图片下载失败：按 msgid 找到本行标失败。找不到则忽略。
    void MarkImageDownloadFailedByMsgid(long long msgid);

    // ---- 文件气泡的收发态更新 -----------------------------------------
    // 注：文件气泡高度随状态变化，故改状态的接口在更新后会 ForceLayout 重排（进度类
    // 更新不改高度，只 Invalidate）。clientMsgId / msgid 找不到则忽略。

    // 列表里是否已存在带该服务器 msgid 的文件气泡（收到 live 文件去重）。0 返回 false。
    bool HasFileMsgid(long long msgid) const;

    // 发送方：按 clientMsgId 定位本端"发送中"的文件气泡。
    void UpdateFileUploadProgress(const std::string& clientMsgId, int percent);
    void MarkFileUploadFailed(const std::string& clientMsgId);
    void MarkFileUploadDone(const std::string& clientMsgId);
    void MarkFileCancelledByClientMsgId(const std::string& clientMsgId);
    // 用户点「重新发送」并已重新入队上传：把该条置「上传中」态、进度归零。
    void MarkFileUploadStartedByClientMsgId(const std::string& clientMsgId);

    // 接收方：按服务器 msgid 定位文件气泡。
    void UpdateFileDownloadProgress(long long msgid, int percent);
    void SetFileDownloadedByMsgid(long long msgid, const CString& localPath);
    void MarkFileDownloadFailedByMsgid(long long msgid);
    void MarkFileCancelledByMsgid(long long msgid);
    // 用户点「拒绝」：把该条置「已拒绝接收」态（仍可再点「重新下载」反悔）。
    void MarkFileRejectedByMsgid(long long msgid);
    // 用户点「接收」/「重新下载」并已发起下载：把该条置「下载中」态、进度归零。
    void MarkFileDownloadStartedByMsgid(long long msgid);
    void MarkFileDeletedByMsgid(long long msgid);
    // 发送方：更新"对端把这条文件收到什么程度了"（按 msgid 定位本端发出的那条气泡）。
    //   peerState：取值见 ChatFilePeerState。
    // 返回是否命中了某条气泡 —— 调用方据此把"本窗口没有这条消息"与"更新了但没生效"
    // 区分开，这两类问题的排查方向完全不同。
    bool SetFilePeerStateByMsgid(long long msgid, int peerState);

    // ---- 文件夹气泡（kMsgFolder）的状态更新 ---------------------------
    //
    // 传输<u>进行中</u>的文件夹气泡是纯本地的：不走 cmd_send_message、没有服务器 msgid，
    // 定位一律靠<u>会话 id</u>。发起之初用的是本地占位 id，服务端分配了真会话 id 之后换成
    // 后者（见 CBalloonChatWindow::OnFolderBound）。
    // 更新分成"取出来改—写回去"两步而不是给每种状态各开一个接口：文件夹传输的状态字段有
    // 十来个（状态 / 进度 / 当前文件 / 序号 / 失败原因 / 落地目录…），一次事件往往要改好几个，
    // 逐个开接口只会让宿主分不清哪些该一起改。
    //
    // 传输走到终态后，服务端会在双方会话里落一条 msgtype=18 的传输记录条推下来，聊天窗
    // 用它<u>就地接管</u>那条本地气泡（填上 msgid 与服务端时间戳、切到对应终态），界面上
    // 因而始终只有一条、位置也不跳。接管之后那条气泡就带上了 msgid，本组的两个接口<u>不再
    // 认它</u>——见下面各自的说明。

    /**
     *  按会话 id 取回<u>尚未被服务端记录接管</u>的那条文件夹气泡（即 msgid 仍为 0 的
     *  本地气泡）。
     *
     *  为什么要把带 msgid 的排除在外：同一次传输可能留下<u>多条</u>服务端记录（每次尝试
     *  各一条：先失败落一条，保留期内重试成功再落一条），它们的会话 id 完全相同。若不加
     *  这道限制，第二条记录就会把第一条那个已经定格的气泡再改一遍；翻历史时同一会话的
     *  多条记录一起渲染进来，实时进度也会更新到最早那条上去。
     *
     *    sessionId：本次传输的会话 id（UTF-8）；空串一律返回 false。
     *    out：      [出参] 命中时填入该行的消息。
     *  @return 找到返回 true；列表里没有这条会话的<u>本地</u>气泡（从未有过、或已被记录
     *          接管）返回 false。
     */
    bool GetFolderMessage(const std::string& sessionId, ChatMessage& out) const;

    /**
     *  按会话 id 把改好的消息写回那条<u>尚未被记录接管</u>的文件夹气泡，并重排 + 重绘。
     *  命中条件与 GetFolderMessage 完全一致（会话 id 相同且 msgid 仍为 0），两者成对使用。
     *
     *  <u>必须重排而不能只重绘</u>：文件夹气泡的高度随状态变化（有没有进度条、有没有
     *  失败原因那一行、底部有几个按钮），只 Invalidate 的话新出现的按钮会被裁在气泡外面、
     *  点不到，看上去就是"点了没反应"。
     *
     *    sessionId：定位用的会话 id（UTF-8，即写回<u>之前</u>那条气泡上的那个）。
     *    msg：      改好的整条消息。msgid 可以在本次写回时一并填上——那正是"接管"这一步，
     *               写回之后本方法与 GetFolderMessage 就都不再认这条气泡了。
     *  @return 命中并写回返回 true；找不到返回 false。
     */
    bool SetFolderMessage(const std::string& sessionId, const ChatMessage& msg);

    /**
     *  把某条<u>尚未被记录接管</u>的文件夹气泡整行移除（销毁该 ChatBubble 并重排）。
     *  命中条件与 GetFolderMessage 完全一致（会话 id 相同且 msgid 仍为 0）。
     *
     *  用在"这一笔根本没能开始"的场合：发送端点了「发送文件夹」之后气泡是先建出来
     *  的（用户要立刻看到反馈），随后才收到服务端的邀请应答。若服务端因对方不在线
     *  直接拒绝，这一笔连会话都没建立、服务端不会留下任何消息记录，此时把气泡改成
     *  失败态并不合适 —— 聊天记录里会平白多出一条"发送失败"，而实际上什么都没发生。
     *  这种情形应当移除气泡、另给用户一句提示。
     *
     *    sessionId：本次传输的会话 id（UTF-8，此时通常还是本地占位 id）；空串一律
     *               返回 false。
     *  @return 找到并移除返回 true；没有这条会话的本地气泡返回 false。
     */
    bool RemoveFolderMessage(const std::string& sessionId);

    /**
     *  把某条已被服务端记录接管的文件夹气泡放回"活的本地气泡"状态：清掉它身上的 msgid
     *  与消息时间，使 GetFolderMessage / SetFolderMessage 重新认得它。
     *
     *  用在<u>重试续传</u>这一步：一笔传输失败后服务端已经落过一条 result=7 的记录、把
     *  气泡接管了；用户点「重试」、传输重新开始时，这条气泡要重新变成活的，本次尝试
     *  结束时到达的那条新记录才能再接管它。不这么做的话，重试期间的进度更新一条也落不
     *  到气泡上（表现为点了重试界面纹丝不动）。
     *
     *    sessionId：本次传输的会话 id（UTF-8）；空串一律返回 false。
     *  @return 找到并清掉返回 true；列表里没有这条会话的气泡返回 false。
     *          同一会话有多条气泡时（翻历史把这一笔早先几次尝试的记录也拉了进来）取
     *          <u>最后</u>那条——它才是刚被接管、马上要复用的那一条，更早的几条属于
     *          已经过去的尝试，不该被改动。
     */
    bool ReviveFolderMessage(const std::string& sessionId);

    // ---- 真图头像下载完成后刷新气泡 ----------------------------------

    // 把所有"发送者为 userId"的气泡的头像真图换成 imagePath（真图下载完成后由聊天窗
    // 调用，让历史已渲染的该发送者气泡也显示真图）。userId 为 0 一律忽略。
    void RefreshSenderAvatar(int64_t userId, const CString& imagePath);

    // ---- 多选模式 -----------------------------------------------------

    // 当前是否处于多选模式。
    bool IsMultiSelectMode() const { return m_multiSelect; }

    // 进入 / 退出多选模式。进入后底部让出操作条、各行显示勾选圈；退出
    // 时清空所有行的选中态。会触发一次重排 + 重绘。
    void SetMultiSelectMode(bool on);

    /**
     *  查询某 userid 是否仍在本会话（群）的成员表里。
     *    userdata：SetMemberInGroupQuery 时透传的用户数据（一般是群聊窗 this）。
     *    uid：     待查的 userid。
     *  @return 仍在群里返回 true。
     */
    typedef bool (*MemberInGroupQuery)(void* userdata, long long uid);

    /**
     *  装上"成员是否仍在群里"的查询钩子。气泡里 @某人 的右键菜单据此决定
     *  「再 @ 一次 TA」要不要置灰 —— 人都退群了，再 @ 也送不到。
     *  只有群聊窗需要装；单聊窗不装即可（不装时一律视为在群，见 IsMemberInGroup）。
     *    fn：      查询回调；传 NULL 表示卸载。
     *    userdata：回调透传的用户数据（借用，不持有）。
     */
    void SetMemberInGroupQuery(MemberInGroupQuery fn, void* userdata);

    /**
     *  该成员是否仍在本会话的成员表里。未装查询钩子时恒返回 true ——
     *  单聊窗、以及调试入口都属于"无从查证"，此时不该平白把菜单项置灰。
     */
    bool IsMemberInGroup(long long uid) const;

    /**
     *  查询某 userid 在本群的角色。
     *    userdata：SetMemberRoleQuery 时透传的用户数据（一般是群聊窗 this）。
     *    uid：     待查的 userid。
     *  @return 网络侧角色取值（0 成员 / 1 管理员 / 2 群主）；不在群或查不到返回负值。
     */
    typedef int (*MemberRoleQuery)(void* userdata, long long uid);

    /**
     *  装上"查成员在本群的角色"的查询钩子。气泡里 @某人 的右键菜单据此决定
     *  末尾要不要给出「设为管理员 / 撤销管理员」。只有群聊窗需要装。
     *    fn：      查询回调；传 NULL 表示卸载。
     *    userdata：回调透传的用户数据（借用，不持有）。
     */
    void SetMemberRoleQuery(MemberRoleQuery fn, void* userdata);

    /**
     *  该成员在本群的角色，取值见 grouprole::NetGroupRole。
     *  未装查询钩子时返回 -1（无从查证），此时管理项一律不出现。
     */
    int MemberNetRole(long long uid) const;

    /**
     *  查询某 userid 当前是否被本人「特别关注」。
     *    userdata：SetStarredQuery 时透传的用户数据（一般是群聊窗 this）。
     *    uid：     待查的 userid。
     *  @return 已关注返回 true。
     */
    typedef bool (*StarredQuery)(void* userdata, long long uid);

    /**
     *  装上"该联系人是否被特别关注"的查询钩子。气泡里 @某人 的右键菜单、以及发送者
     *  头像的右键菜单据此决定那一项的文案是「特别关注」还是「取消特别关注」。
     *
     *  为什么要走钩子：关注名单只有主面板那一份（balloonmain::StarredModel），气泡与
     *  消息列表都不认识它；与上面两个群成员查询同一套路子，宿主窗代问、控件只管显示。
     *  只有群聊窗需要装；单聊窗不装即可（不装时恒返回 false，见 IsStarred）。
     *    fn：      查询回调；传 NULL 表示卸载。
     *    userdata：回调透传的用户数据（借用，不持有）。
     */
    void SetStarredQuery(StarredQuery fn, void* userdata);

    /**
     *  该联系人当前是否被本人特别关注。未装查询钩子时恒返回 false（无从查证时
     *  按"未关注"显示，点下去就是去关注，是这两种误判里代价较小的那个）。
     */
    bool IsStarred(long long uid) const;

    /**
     *  设置 / 读取本人在本群的角色，取值见 grouprole::NetGroupRole，负值表示未知。
     *  由群聊窗在自己的角色更新时同步下来 —— 气泡不认识"当前登录用户"这类业务对象，
     *  与其让它去问，不如由宿主推给它。
     */
    void SetMyNetRole(int role);
    int  MyNetRole() const;

    // ---- ChatBubble 右键菜单 / 多选点击回调进来的请求 -----------------

    // 删除某一条消息行（从列表移除并销毁该 ChatBubble，随后重排）。
    void RequestDeleteBubble(ChatBubble* bubble);

    // 请求撤回某一条消息行。本方法<u>不</u>就地改界面 —— 它只把该行的服务器 msgid
    // 经 WM_CHAT_RECALL_REQUEST 投给宿主聊天窗，由宿主连同自己的 ctype/cid 发
    // cmd_recall_message；界面改动一律等服务端推来 cmd_message_recalled_notify 后
    // 走 RecallMessageByMsgid，这样本端与对端看到的结果必然一致。
    // msgid 为 0（服务器尚未回执）的行直接忽略——右键菜单本就不会给这类行显示"撤回"。
    void RequestRecallBubble(ChatBubble* bubble);

    /**
     *  请求 Pin / 取消 Pin 某一条消息行。
     *
     *  与 RequestRecallBubble 同一套路：本方法<u>不</u>就地改界面，只把该行的服务器
     *  msgid 与目标状态经 WM_CHAT_PIN_REQUEST 投给宿主聊天窗，由宿主连同自己的
     *  ctype/cid 发 cmd_pin_message；标记改动一律等服务端推来
     *  cmd_message_pinned_notify 后走 SetPinnedByMsgid。Pin 是会话内共享的状态，
     *  这样本端与对端看到的结果必然一致。
     *  msgid 为 0（服务器尚未回执）的行直接忽略 —— 右键菜单本就不会给这类行显示该项。
     *
     *    bubble：目标消息行。
     *    pin：   目标状态，true 钉住、false 取消。
     */
    void RequestPinBubble(ChatBubble* bubble, bool pin);

    /**
     *  把带指定服务器 msgid 的消息行的 Pin 标记置上 / 清掉（收到 Pin 推送后调用）。
     *  标记画在气泡外侧、不占内容宽度也不改行高，故只重绘、不重排。
     *    msgid： 目标消息的服务器 msgid；0 一律忽略。
     *    pinned：true 钉住、false 取消。
     *  @return 找到并改了返回 true；列表里没有该 msgid（如该消息尚未加载进当前视图）
     *          时返回 false —— 这不是错误，下次翻历史时本地库里已带着标记。
     */
    bool SetPinnedByMsgid(long long msgid, bool pinned);

    /**
     *  数一数当前列表里有多少行带着 Pin 标记。
     *
     *  <u>只在没有服务端可问的时候用</u> —— 即两个调试入口（`--chatwindow` /
     *  `--groupchatwindow`）下给顶部「已 Pin N 条」提示带凑一个数出来。真实会话的
     *  权威条数是服务端 `cmd_get_pinned_messages` 应答里的 `total`：列表里只有已经
     *  加载进当前视图的那些行，被钉住的老消息往往还没翻到，照这里数会少报。
     *
     *  @return 带 Pin 标记的消息行数；列表为空时返回 0。
     */
    int CountPinned() const;

    // 把带指定服务器 msgid 的消息行就地变成居中的撤回提示行（收到撤回推送后调用）。
    //   msgid：  被撤消息的服务器 msgid；0 一律忽略。
    //   tipText：提示文案（见 balloonchat::BuildRecallTipText）。
    // 列表里没有该 msgid（如该消息尚未加载进当前视图）时安静忽略，不报错。
    // 找到并改成提示行返回 true，否则 false。
    bool RecallMessageByMsgid(long long msgid, const CString& tipText);

    // 发送回执到达后，按本端生成的 clientMsgId 找到那条"刚发出"的消息行，把服务器
    // 分配的 msgid 与消息时间回填进去。没有这一步，本端发出的气泡会一直是
    // msgid=0 / 时间=0，右键菜单里就永远不会出现"撤回"（时限判断拿不到依据）。
    //   clientMsgId： 本端发送时生成的客户端消息ID；空串一律返回 false。
    //   msgid：       服务器分配的消息ID。
    //   timestampMs： 服务器消息时间（Unix 毫秒）。
    // clientMsgId 全局唯一，故调用方可向所有已打开的聊天窗广播、由本方法自行筛选。
    // 找到并回填返回 true，本列表里没有该 clientMsgId 时返回 false（不是错误）。
    bool BackfillSentMessage(const std::string& clientMsgId, long long msgid,
                             long long timestampMs);

    // 以某一条消息为起点进入多选模式（该行预选中）。
    void EnterMultiSelectWith(ChatBubble* bubble);

    // 转发某一条消息（mock 阶段为占位提示，真实转发待接入）。
    void RequestForwardBubble(ChatBubble* bubble);

    // 引用回复某一条消息（mock 阶段为占位提示，真实引用待接入）。
    void RequestReplyBubble(ChatBubble* bubble);

    // 多选模式下某行选中态变化后由 ChatBubble 回调 —— 刷新底部操作条
    // 上的计数显示。
    void NotifySelectionChanged();

    /**
     *  清掉除 keep 之外所有气泡的<u>文字选区</u>（与多选勾选无关）。
     *  同一时刻只允许一个气泡有文字选区 —— 否则会出现两条消息同时高亮、而"复制"
     *  只取其中一条的割裂感。在某个气泡上开始新选择、或点到空白处时调用。
     *    keep：要保留选区的气泡；传 NULL 表示全部清掉。
     */
    void ClearTextSelectionExcept(ChatBubble* keep);

    /**
     *  取当前有文字选区的那个气泡里选中的文本（表情已替换成 "[表情]"）。
     *  没有任何气泡有选区时返回空串。供宿主聊天窗响应 Ctrl+C。
     */
    CString GetSelectedText() const;

    /**
     *  把当前选中的那段文字复制到剪贴板（内容口径与气泡右键「复制」完全一致，
     *  内联表情替换成 "[表情]"）。
     *
     *  供宿主聊天窗响应 Ctrl+C。<u>没有任何气泡有选区时什么都不做</u> —— 不去
     *  "退而复制整条"，那种情形下"整条"指的是哪一条说不清楚。
     *  @return 确实有选区并已写入剪贴板返回 true；没有选区返回 false（调用方据此
     *          决定要不要把这次按键交回原处理者，比如输入框自己的复制）。
     */
    bool CopySelectedTextToClipboard();

    /**
     *  让所有消息行重新测量并重绘。
     *
     *  用于"消息内容没变、但渲染口径变了"的场合 —— 目前只有系统设置里改动
     *  「自动识别链接」开关时用得上。行高可能随之变化（链接不改变文字，理论上
     *  不变；但重新测量是安全的做法），故走一次完整重排而不是只 Invalidate。
     */
    void RefreshAllRows();

    // DuiControl 覆写：按各 ChatBubble 的行高堆叠子控件（含滚动偏移）。
    void Layout(const RECT& rcAvail) override;

    // DuiControl 覆写：绘制消息行 + 右侧细滚动条 +（多选时）底部操作条。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：滚轮滚动消息列表。
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

    // DuiControl 覆写：左键点击 —— 命中滚动条滑块则开始拖动；否则多选模式下
    // 命中底部操作条执行 取消 / 转发所选 / 删除所选。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：鼠标移动 —— 正在拖动滚动条滑块时据 pt.y 更新滚动位置。
    bool OnMouseMove(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：左键抬起 —— 结束滚动条滑块拖动。
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

    /**
     *  DuiControl 覆写：把滚动条命中带的点击优先判给本控件。
     *
     *  基类的命中测试是"子控件优先"，而消息行（ChatBubble）的矩形横向占满整个
     *  列表宽度、把右侧滚动条整个盖在下面，于是点击滚动条会先命中气泡、被气泡
     *  当成"开始拖拽选中文字"消费掉，滚动条就再也点不动了。消息把可视区填满时
     *  （长文本）尤其明显 —— 列表下方连一块没有行覆盖的空白都不剩。
     *  故这里在下探子控件之前先截下滚动条命中带。
     *    ptHostClient：落点（host 客户区坐标）。
     *  @return 落在滚动条命中带内时返回 this；否则退回基类的逐层命中。
     */
    balloonwjui::DuiControl* HitTest(POINT ptHostClient) override;

private:
    // 滚动条滑块矩形（host 客户区坐标）；内容不溢出（无滚动条）时返回空矩形。
    RECT ThumbRect() const;

    /**
     *  落点是否位于滚动条的命中带内。命中带 = 滑块所在竖直窄带（左右各放宽
     *  kBarMargin 便于点中）与消息行区的交集；没有滚动条时恒为 false。
     *  供 HitTest 与 OnLButtonDown 共用，避免两处判断随时间漂移。
     *    pt：落点（host 客户区坐标）。
     *  @return 位于命中带内返回 true。
     */
    bool HitScrollBarBand(POINT pt) const;

    // 据滑块顶部目标 y（host 客户区坐标）反推并设置滚动偏移 m_scrollY，随后重排重绘。
    //   thumbTopY：期望的滑块顶部 y。
    void ScrollToThumbTop(int thumbTopY);

    // 消息行区域（去掉多选操作条后的可视矩形）。
    RECT RowsViewRect() const;

    // 多选操作条矩形（非多选模式返回空矩形）。
    RECT MultiSelectBarRect() const;

    // 统计当前选中的消息条数。
    int  SelectedCount() const;

    // 取本列表所属窗口的 HWND（用于弹消息框 / 操作剪贴板）。
    HWND HostHwnd() const;

    // 按需(重)建离屏合成缓冲，使其至少 w×h 大；ref 提供位图色彩格式。
    void EnsureOffscreen(HDC ref, int w, int h);

    // 释放离屏合成缓冲。
    void ReleaseOffscreen();


private:
    // 滚动到接近顶部时向宿主聊天窗投 WM_CHAT_LOAD_OLDER，请它判断要不要拉更早的
    // 一页。本控件不持有分页状态（有没有在途、还有没有更早的、before 锚点是多少），
    // 那些属于会话、归宿主窗管，故这里只报告"用户滚到顶部附近了"这一个事实。
    void NotifyMaybeLoadOlder();

    // 向上翻页批次的现场：批次开始时的行数与内容总高。EndPrependBatch 据此算出
    // 本批追加了多少行、上方长高了多少。m_prependActive 同时用来抑制批次期间
    // AppendMessage 的"滚到底"。
    bool   m_prependActive;
    size_t m_prependMarkCount;
    int    m_prependMarkHeight;

    // 上一条<u>真实</u>消息的时间（Unix 毫秒），供时间分隔行判定用；0=还没有过。
    // 只记真实消息：分隔行自身不参与判定，时间未知（0）的消息也不更新它，否则
    // 后面一条正常消息会与 0 相比、被判成"没有上一条"而多插一行。
    long long m_lastMsgTimestampMs;

    // 向上翻页期间临时寄存的原基准（BeginPrependBatch 存、EndPrependBatch 还原）。
    // 翻页批次会被整体挪到列表最前面，判定基准必须临时清零，收尾后再还给实时消息用。
    long long m_prependSavedLastMs;

    int  m_scrollY;        // 当前纵向滚动偏移
    int  m_contentHeight;  // 全部消息行的总高
    int  m_viewHeight;     // 视口高（消息行区域的高，已扣除多选操作条）
    bool m_multiSelect;    // 是否处于多选模式

    // "该成员是否仍在群里"的查询钩子与其透传数据（借用，不持有）。群聊窗装上，
    // 单聊窗不装 —— 为空时 IsMemberInGroup 恒返回 true。
    MemberInGroupQuery m_memberQuery;
    void*              m_memberQueryUd;

    // "查成员在本群的角色"的查询钩子与其透传数据（借用，不持有）。群聊窗装上，
    // 单聊窗不装 —— 为空时 MemberNetRole 恒返回 -1。
    MemberRoleQuery m_roleQuery;
    void*           m_roleQueryUd;

    // "该联系人是否被特别关注"的查询钩子与其透传数据（借用，不持有）。群聊窗装上，
    // 单聊窗不装 —— 为空时 IsStarred 恒返回 false。
    StarredQuery m_starredQuery;
    void*        m_starredQueryUd;

    // 本人在本群的角色，取值见 grouprole::NetGroupRole；-1 表示未知（单聊窗 / 尚未拉到群资料）
    int m_myNetRole;

    // 滚动条滑块拖动状态。
    bool m_thumbDragging;    // 是否正在用鼠标拖动滑块
    int  m_thumbDragOffsetY; // 拖动开始时鼠标相对滑块顶部的 y 偏移（保持抓点不跳）

    // ---- 离屏合成缓冲 ----
    // 气泡用 GDI+ 自绘，GDI 裁剪区约束不住 GDI+，滚动时气泡会溢出消息
    // 区。改为把气泡画进这张离屏位图（位图物理边界兜住一切绘制），再
    // 只把消息行可视区那一条 BitBlt 回真 DC。
    HDC     m_memDC;       // 离屏合成 DC
    HBITMAP m_memBmp;      // 离屏合成位图
    HBITMAP m_memOldBmp;   // m_memDC 的原始位图（释放前须换回）
    int     m_memW;        // 离屏位图当前宽
    int     m_memH;        // 离屏位图当前高
};

} // namespace balloonchatwnd
