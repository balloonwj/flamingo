/**
 *  ChatFolderStateModel.h
 *
 *  单聊「文件夹」气泡的「状态 → 可用操作」映射（纯逻辑核心，不依赖 GDI / ATL / WTL）。
 *
 *  文件夹传输与单个文件传输的最大差别，是它必须先征得对方同意才开始传（一个目录可能
 *  有几万个文件、几十 GB），因此气泡上要走完「邀请 → 应答 → 建目录 → 传字节 → 收尾」
 *  一整条链路，中途还有失败、重试续传、保留期过期、任一端取消这些岔路。每一步两端看到
 *  的按钮都不一样：同一个「传输失败」，发送端能点「重试」，接收端只能看着。
 *
 *  本模块把这套映射单独拿出来，与文件气泡的 ChatFileStateModel 是同一类东西、同一套
 *  写法：发送端与接收端<u>共用一套状态枚举</u>，用 fromMe 区分两端的可用操作。气泡绘制
 *  代码只管照结果画，按钮数、按钮动作、是否画进度条三处不会再各写一份而互相走偏
 *  （那种错位的典型症状是「按钮画出来了但点不到」或「气泡高度不够按钮被裁掉」）。
 *
 *  与协议的关系：本枚举 1~8 号取值与 net/Protocol.h 的 folder_transfer_state
 *  <u>逐值相同</u>，收到 cmd_folder_state_notify（1815）后可直接把 state 字段填进来；
 *  另有三个协议表达不了的本端状态从 101 起编号（见 ChatFolderState 的注释）。为免把
 *  v2 协议头带进气泡绘制的编译单元（那边可能仍在编 v1 的 Msg.h，两者枚举名冲突），
 *  本头文件<u>不</u>包含 Protocol.h，取值一致性由单元测试锁定。
 *
 *  可脱离界面单元测试（见 flamingoclient/Source/Tests/ChatFolderStateModelTests.cpp，
 *  Suite CFOLD）。几何常量与气泡高度仍留在绘制侧——那些与绘制强耦合，脱离绘制没有意义。
 *
 *  典型用法（气泡绘制侧）：
 *      int acts[kMaxChatFolderActions] = { 0 };
 *      const int n = FolderButtonActions(state, fromMe, acts);
 *      for (int i = 0; i < n; ++i)
 *      {
 *          // acts[i] 即该按钮的动作 id，取值见 ChatFolderActionKind
 *      }
 *      if (FolderStateHasProgress(state))
 *      {
 *          // 画进度条
 *      }
 *
 *  需求来源：docs/单聊文件夹传输方案.md 第 2 节（产品选择）、7.2 节（界面）、第 8 节
 *  （正常传输 / 失败 / 重试续传 / 取消四条流程）。
 *
 *  balloonwj@qq.com   2026-08-13
 */

#pragma once

namespace balloonchatwnd {

//文件夹气泡底部按钮的动作类型。与 ChatFileActionKind 分开定义而不复用：文件夹的
//「重试」是断点续传、「打开文件夹」的落点两端不同，语义并不是文件那套的子集，混用
//会让宿主的分发代码分不清手上这条是文件还是文件夹。
enum ChatFolderActionKind
{
    kChatFolderAccept     = 1,   // 接收：接收端同意接收本文件夹，<u>不</u>弹选目录框，直接落到本机默认的文件夹接收目录（Users\<登录账号>\ChatFolder\<文件夹名>，同名时由接收端状态机自动改名），随后发 cmd_folder_response(accept=1)
    kChatFolderReject     = 2,   // 拒绝：接收端拒绝接收，发 cmd_folder_response(accept=0)，会话立即结束
    kChatFolderCancel     = 3,   // 取消：中止本次传输。应答前由发送端撤回邀请（cmd_folder_cancel），传输中则任一端都可点
    kChatFolderRetry      = 4,   // 重试：失败会话仍在保留期内时断点续传（cmd_folder_retry），不再征求接收方同意
    kChatFolderOpenFolder = 5,   // 打开文件夹：接收端打开落地目录，发送端打开源目录。两端同一个动作 id，开哪个目录由宿主决定
    kChatFolderResend     = 6,   // 重新发送：会话已不可续传时，从零发起一次完整传输（重新征求同意）
    kChatFolderSaveAs     = 7,   // 另存为：接收端同意接收，但先弹出「选择保存位置」由用户挑落地位置，选定后同样发 cmd_folder_response(accept=1)。与 kChatFolderAccept 只差「目录从哪来」这一步，后续流程完全一致
};

//文件夹传输在气泡上的状态。发送端与接收端共用这一套，两端的差别全部由 fromMe 表达。
//
//编号分两段：
//  · 1~8 与协议的 folder_transfer_state 逐值相同，服务端推什么就能直接用什么；
//  · 101 起是本端才有的状态——协议里没有对应取值，或协议用「失败 + 错误码」表达而
//    气泡要单独成一档。之所以跳到 101 而不是接着 9 排，是给协议将来新增状态留出余量，
//    免得两边各自扩号撞在一起。
enum ChatFolderState
{
    kFolderInviting       = 1,   // 邀请已发出、等待对方应答（== folder_state_ringing）：发送端显示「等待对方接收」，接收端显示「接收 / 另存为 / 拒绝」
    kFolderTransferring   = 2,   // 传输中（== folder_state_transferring）：目录结构已建好，正在数据通道上收发文件字节，有进度可显示
    kFolderRejected       = 3,   // 对方已拒绝（== folder_state_rejected）：接收端点了「拒绝」，会话立即结束，不产生任何中转连接
    kFolderInviteCanceled = 4,   // 邀请被发送方在应答前撤回（== folder_state_canceled）：发送端发了 cmd_folder_cancel。<u>不</u>包括传输中途的取消，那种见 kFolderUserCanceled
    kFolderPeerOffline    = 5,   // 对方不在线（== folder_state_peer_offline）：发起邀请时对方无在线连接，或响铃期间对方退出登录，会话作废
    kFolderTimeout        = 6,   // 邀请超时（== folder_state_timeout）：限定时间内无人应答，服务端自动结束会话，会话作废
    kFolderFailed         = 7,   // 传输失败（== folder_state_failed）：任一端出错或中转通道断开，整个文件夹判失败；会话保留 10 分钟，期内可重试续传
    kFolderCompleted      = 8,   // 全部完成（== folder_state_completed）：全部文件传完并通过校验

    kFolderPreparing      = 101,  // 接收端正在建目录结构：清单已送达，接收端正逐级建目录（含空目录），还<u>没有</u>开始传字节。协议侧仍算 folder_state_transferring，气泡要单独成一档才能显示「正在创建目录」而不是 0% 的进度条
    kFolderExpired        = 102,  // 传输失败且已超出 10 分钟保留期：服务端已销毁会话，续传位点作废，「重试」不再可用，只能重新发送
    kFolderUserCanceled   = 103,  // 用户取消：传输过程中任一端点了「取消」。协议侧按「失败 + error_code_folder_canceled」表达，气泡上单独成一档，措辞是「已取消」而不是「传输失败」
};

//一个文件夹气泡底部最多容纳的操作数。上限来自邀请态的接收端（「接收 / 另存为 / 拒绝」
//三个），其余状态都只有一个操作。FolderButtonActions 的出参数组据此定长。
//
//改这个值必须同步检查气泡高度的计算（ChatBubble.cpp 的 FolderBubbleHeight）：按钮排在
//<u>同一行</u>内，行高与按钮数无关，故高度只取决于「有没有按钮」而不是「有几个」；哪天
//改成折成两行排，高度就必须跟着按钮数算，否则会出现按钮被裁在气泡外面点不到的错位。
const int kMaxChatFolderActions = 3;

/**
 *  该状态下气泡是否要画进度条。
 *  传输中显然要画；失败 / 已过保留期 / 用户取消这三种是「传到一半停住」，进度条停在
 *  中断处，用户能看出传到哪儿断的。邀请态与建目录态都还没有字节传出去，画一条 0% 的
 *  进度条只会让人误以为在传；完成态与几种会话作废的状态都已成定局，也不必画。
 *  @param state 文件夹传输状态，取值见 ChatFolderState
 *  @return 需要画进度条返回 true；未知状态返回 false
 */
bool FolderStateHasProgress(int state);

/**
 *  该状态下气泡底部的操作数量。
 *  @param state  文件夹传输状态，取值见 ChatFolderState
 *  @param fromMe 该条气泡是否属于发送端（true = 本端是发起传输的一方）。除传输中与
 *                建目录中「两端都能取消」外，其余状态两端的操作都不一样：接收端只在
 *                邀请态与完成态有按钮，重试与重新发送都只属于发送端
 *  @return 操作数量，范围 0 ~ kMaxChatFolderActions；未知状态按 0（不给操作）处理
 */
int FolderStateButtonCount(int state, bool fromMe);

/**
 *  取该状态下底部各操作的动作 id，按从左到右的显示顺序写入 out。
 *  @param state  文件夹传输状态，取值见 ChatFolderState
 *  @param fromMe 该条气泡是否属于发送端，含义同 FolderStateButtonCount
 *  @param out    [出参] 动作 id 数组（取值见 ChatFolderActionKind），调用方须提供
 *                至少 kMaxChatFolderActions 个元素的空间；仅前若干个被写入，返回 0
 *                时一个都不写
 *  @return 实际写入的动作个数，与 FolderStateButtonCount(state, fromMe) 一致
 */
int FolderButtonActions(int state, bool fromMe, int out[kMaxChatFolderActions]);

/**
 *  「重试」当前是否可用（决定按钮是正常显示还是置灰）。
 *
 *  失败会话在服务端只保留 folderretrywindow（默认 10 分钟），期内点重试是断点续传、
 *  不再打扰接收方；期满服务端销毁会话，续传位点一并作废，只能重新发起一次完整传输。
 *  宿主有两种表达方式，结果一致：本端已确知会话被销毁（收到 error_code_folder_session_expired，
 *  或本地计时已过）时把状态切到 kFolderExpired，按钮直接变成「重新发送」；仅凭本地
 *  计时做即时判断时保持失败态，用本函数把「重试」置灰并提示已超时。
 *
 *  @param state             文件夹传输状态，取值见 ChatFolderState
 *  @param fromMe            该条气泡是否属于发送端。重试由发送端发起，接收端全程只是
 *                           看到气泡从失败变回传输中，故接收端一律返回 false
 *  @param withinRetryWindow 该会话是否仍在保留期内（由宿主按失败时刻与保留期时长算出，
 *                           或依据服务端应答判定）
 *  @return 「重试」可用返回 true。仅「失败态 + 发送端 + 仍在保留期内」三者同时成立时
 *          才为 true；kFolderExpired 恒为 false（它的语义就是保留期已过）
 */
bool FolderRetryEnabled(int state, bool fromMe, bool withinRetryWindow);

/**
 *  失败原因错误码 → 气泡上展示的中文文案。
 *
 *  失败原因由服务端与对端沿两条路送来（中转通道内的 ft_abort 帧、以及
 *  cmd_folder_close / cmd_folder_state_notify 的 errcode），双方拿到的是同一个码，
 *  经本函数映射后<u>双方看到的也是同一条文案</u>。文案是给用户看的界面文字，故用中文；
 *  日志另按英文规范输出，不要拿本函数的返回值去写日志。
 *
 *  @param errcode 错误码，取值见 net/Protocol.h 的 error_code_folder_*（606~620）
 *  @return 指向静态只读字符串的指针，调用方<u>不得</u>释放，也不必拷贝。未收录的取值
 *          （含 0 与今后新增而本表尚未跟上的码）一律返回兜底文案，保证界面上不会出现
 *          空白的一行
 */
const wchar_t* FolderErrorText(int errcode);

/**
 *  把服务端传输记录条（msgtype=message_type_folder=18）里的「结局 + 失败原因」翻成
 *  气泡状态。
 *
 *  记录条的 result 取的是协议的 folder_transfer_state 终态（3~8），与本文件 1~8 号
 *  取值逐值相同，故绝大多数结局是恒等映射。需要另判一步的只有失败态：协议把「用户
 *  取消」与「会话已过保留期」都表达成「失败 + 对应错误码」，而气泡上这两种各自成一档
 *  （措辞是「已取消」「请重新发送」而不是「传输失败」）。这一步与实时路径的
 *  CBalloonChatWindow::ApplyFolderFailure 判的是同一件事，故抽到这里由两处共用，
 *  免得同一次传输"实时看到的"与"翻历史看到的"说法不一样。
 *
 *  @param result  记录条里的结局，取值为 folder_transfer_state 的终态（3~8）
 *  @param errCode 记录条里的失败原因（error_code_folder_*）；仅 result 为失败态时有意义
 *  @return 对应的 ChatFolderState 取值；result 不是终态（含 0 与越界值）时返回 0，
 *          调用方据此按"内容不可用"降级处理 —— 0 不是合法状态，气泡会退化成无进度条、
 *          无按钮的样子，不会画出莫名其妙的界面
 */
int FolderStateFromRecord(int result, int errCode);

} // namespace balloonchatwnd
