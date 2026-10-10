/**
 *  ChatDirectFileModel.h
 *
 *  单聊单文件 P2P 直传在聊天窗一侧的纯逻辑（2026-10-04，docs/单聊文件P2P直传与P2P徽章方案.md 第 3 节）：
 *    · 收到直传的邀请应答、状态推送后，按 DirectFileTransfer.h 判定出的处置，算出文件气泡该怎么变
 *      （转什么状态、还是不是直传、要不要立即改走上传服务器、对方的收取状态、接收方作废的原因）；
 *    · 直传特有的状态文字（等待对方接收、作废原因、历史里本机没有文件、接收中）；
 *    · 历史里的直传文件消息画成什么状态；
 *    · 文件夹气泡要不要画「P2P」徽章。
 *  不依赖 GDI / ATL / WTL，聊天窗照结果改气泡，单元测试直接断言（Tests/ChatDirectFileTests.cpp，标签 [directfilechat]）。
 *
 *  典型用法（发送方聊天窗收到 1815）：
 *      const DirectBubbleChange c = SenderBubbleForAction(foldertransfer::DecideDirectSendOnState(state, errCode));
 *      if (c.m_fileState != kDirectKeepState)
 *          msg.fileState = c.m_fileState;
 *      if (c.m_startUpload)
 *          ...（交给 FileTransfer 上传）
 *
 *  balloonwj@qq.com   2026-10-04
 */

#pragma once

#include <string>   //std::wstring：DirectFileHasLocalCopy 的本机路径

namespace balloonchatwnd {

//DirectBubbleChange 里「不改这一项」的取值
const int kDirectKeepState = -1;

//接收方作废原因（ChatMessage::fileDirectEnd）里「本端在传输中途取消了接收」的取值：会话状态里没有对应的一档
//（服务端记的是「已失败、原因为用户取消」），取一个不与 folder_transfer_state 重叠的值
const int kDirectEndSelfCanceled = 100;

//直传处置之后文件气泡的变化
struct DirectBubbleChange
{
    int  m_fileState;     //新的文件状态（ChatFileState）；kDirectKeepState 表示不改
    bool m_direct;        //改完之后这条还是不是在线直传（改为离线文件之后不再是）
    bool m_startUpload;   //是否要立即改走上传服务器（交给 FileTransfer 上传、发普通文件消息）
    int  m_peerState;     //新的对方收取状态（ChatFileStateModel.h 的 kFilePeer*）；kDirectKeepState 表示不改
    int  m_directEnd;     //这条直传作废（接收方）或被本端取消（发送方）的原因（folder_transfer_state 的取值）；0 表示不改

    DirectBubbleChange()
        : m_fileState(kDirectKeepState)
        , m_direct(true)
        , m_startUpload(false)
        , m_peerState(kDirectKeepState)
        , m_directEnd(0)
    {
    }
};

/**
 *  发送方的文件气泡按直传处置怎么变。
 *  @param senderAction 处置，取值见 DirectFileTransfer.h 的 DirectFileSenderAction
 *  @return 继续等：什么都不改；改为离线文件：上传中、不再是直传、立即上传、对方状态清零；对方拒绝：完成态（本地源文件
 *          仍可打开）、对方已拒绝；完成：完成态、对方已接收；传输中途失败：失败态、仍是直传；撤回：已取消并记下取消原因
 *          （状态行写「已取消发送」、不画进度条）；
 *          敏感词：失败态、不上传
 */
DirectBubbleChange SenderBubbleForAction(int senderAction);

/**
 *  接收方的文件气泡按直传处置怎么变。
 *  @param recvAction  处置，取值见 DirectFileTransfer.h 的 DirectFileRecvAction
 *  @param folderState 引起这次处置的会话状态（folder_transfer_state），用来记作废原因
 *  @return 继续等：什么都不改；完成：完成态；拒绝、对方撤回、改为离线、失败：「已作废」（kFileDirectToOffline），
 *          原因取 folderState，其中失败且被判为改为离线的（622）记作「已改为离线发送」；接收方从不自己上传
 */
DirectBubbleChange ReceiverBubbleForAction(int recvAction, int folderState);

/**
 *  直传特有的状态文字（未翻译的原文，调用方交给 TR；带 %d 的由调用方代入进度）。
 *  @param fileState 文件状态（ChatFileState）
 *  @param directEnd 接收方作废的原因（folder_transfer_state 的取值），只在「已作废」时有意义
 *  @param fromMe    这条是不是本人发出的
 *  @param direct    这条是不是在线直传
 *  @return 等待对方接收、各种作废原因（收发两侧措辞不同）、历史里本机没有文件、接收中这几种的原文；
 *          其余状态与普通文件返回 NULL，沿用普通文件的文字
 */
const wchar_t* DirectFileStatusKey(int fileState, int directEnd, bool fromMe, bool direct);

/**
 *  历史里的一条直传文件消息画成什么状态。
 *  @param result       消息里的结局（folder_state_completed 或 folder_state_rejected）
 *  @param hasLocalFile 本机是否找得到这份文件（本地文件登记库或下载缓存里有、且大小相符）
 *  @param outState     [出参] 文件状态：完成且本机有文件为完成态，本机没有为已失效（显示「在线直传，服务器未保存」）；
 *                      拒绝为「已作废」
 *  @param outDirectEnd [出参] 作废原因：拒绝时为 folder_state_rejected，其余为 0
 */
void DirectRecordBubble(int result, bool hasLocalFile, int& outState, int& outDirectEnd);

/**
 *  历史里的一条直传文件消息在发送方气泡上的「对方进展」。直传成功即表示对方已完整收到并校验通过，
 *  发送方的状态行应与实时传完时一样写「对方已接收」，而不是普通文件上传完成后的「传送完成」。
 *  @param result       消息里的结局（folder_state_completed 或 folder_state_rejected）
 *  @param fromMe       这条消息是不是本端发出的
 *  @param hasLocalFile 本机是否找得到这份文件。找不到时返回 0，让状态行保留「在线直传，服务器未保存」，
 *                      说明为什么没有可用的操作
 *  @return 取值见 ChatFilePeerState：发送方、成功、本机有文件时为 kFilePeerReceived，其余为 0。
 *          拒绝的结局另由作废原因说明（「对方已拒绝接收」），这里不重复
 */
int DirectRecordPeerState(int result, bool fromMe, bool hasLocalFile);

/**
 *  文件夹气泡要不要画「P2P」徽章（docs/单聊文件P2P直传与P2P徽章方案.md 第 4 节）。
 *  @param folderState  文件夹气泡的状态（ChatFolderState）
 *  @param linkIsDirect 链路是否直连，由调用方用 balloonmain::ShouldShowP2PBadge(链路路径) 算出
 *  @return 直连且在传输中或已完成时返回 true
 */
bool FolderBubbleShowsP2PBadge(int folderState, bool linkIsDirect);

/**
 *  在线直传的文件本机是否还有可用的一份（转发时据此决定以离线文件重新发送还是提示无法转发，2026-10-05 起）。
 *  每次都实测磁盘：用户随时会把文件移走、改名或删除。
 *  @param localPath  气泡记下的本机路径（发送方为所选的源文件，接收方为收完后的落地路径）；为空时返回 false
 *  @param expectSize 文件字节数；大于 0 时要求磁盘上的大小与之相同，否则只要求文件存在
 *  @return 文件存在、不是目录且大小相符时返回 true
 */
bool DirectFileHasLocalCopy(const std::wstring& localPath, long long expectSize);

/**
 *  聊天窗右键「转发」一条消息时是否直接提示「在线直传的文件无法转发」，不进入多选。
 *  @param isFile       这条是不是文件消息
 *  @param direct       是不是在线直传的文件
 *  @param hasLocalCopy 本机是否还有可用的一份（见 DirectFileHasLocalCopy）
 *  @return 是在线直传的文件、且本机已没有这份文件时返回 true；本函数无副作用
 */
bool DirectFileForwardBlocked(bool isFile, bool direct, bool hasLocalCopy);

} // namespace balloonchatwnd
