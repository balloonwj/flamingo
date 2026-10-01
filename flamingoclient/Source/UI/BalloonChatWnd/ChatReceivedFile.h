/**
 *  ChatReceivedFile.h
 *
 *  单聊窗与群聊窗共用的文件消息（msgtype=3）处理：把收到 / 翻到的文件消息构造成文件
 *  气泡，以及处理文件气泡上「打开 / 打开所在文件夹 / 另存为 / 下载」这几种与会话无关
 *  的按钮动作。
 *
 *  两个窗口的差别只在会话相关的部分：单聊有「接收 / 拒绝」流程、要给发送方回执，群聊
 *  没有，点「下载」直接取文件。这些差别留在各自窗口里，本文件只放两边完全相同的逻辑，
 *  免得同一套「本地落点查找 + 缓存命中判定」在两个窗口各写一份、日后改一边忘一边。
 *
 *  典型用法：
 *      bool cached = false;
 *      balloonchatwnd::ChatMessage msg =
 *          balloonchatwnd::BuildReceivedFileMessage(fromMe, notify, false, &cached);
 *      // 窗口自己填头像 / 发送人，再 AppendMessage
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（CString）。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <string>
#include "ChatMockData.h"     // ChatMessage
#include "ChatFileAction.h"   // ChatFileActionData

namespace net {
struct MessageNotify;
}

namespace balloonchatwnd {

class ChatMessageList;

/**
 *  判断本地某个路径上的文件能否当作这条文件消息的完整副本使用。
 *  只取文件属性、不读文件内容，一屏几十条气泡逐条查也没有可感知的开销。
 *  @param path       待查的本地完整路径；空串直接判为不可用
 *  @param expectSize 期望的文件字节数；<=0 表示消息里没有大小信息，此时只判断文件存在
 *  @return 文件存在、不是目录、且大小与期望相符（或期望缺失）时返回 true
 */
bool IsLocalFileUsable(const std::wstring& path, long long expectSize);

/**
 *  把一条文件消息构造成文件气泡的数据（不含头像与发送人信息，由调用方按会话类型填写）。
 *
 *  本地已有完整文件（登记表里的路径或下载缓存命中）时构造完成态气泡，可直接打开；
 *  否则构造待下载态气泡，不自动下载，等用户点按钮再发起。
 *
 *  @param fromMe         该条是否本端发出，决定气泡画在左侧还是右侧
 *  @param notify         文件消息（取 m_msgid / m_fileName / m_fileSize / m_fileFileId /
 *                        m_fileMd5 / m_fileAckState）
 *  @param directDownload true 表示该会话没有「接收 / 拒绝」流程（群聊），待下载态只给
 *                        「下载」一个按钮；false 为单聊的接收流程
 *  @param cached         [出参，可为 NULL] 写入本地是否已有完整文件，调用方据此打日志
 *  @return 构造好的气泡数据。服务器消息ID 只填在 fileMsgid 上（列表去重、转发、下载回填
 *          都认它），msgid 保持为 0，与单聊窗一直以来的做法一致
 */
ChatMessage BuildReceivedFileMessage(bool fromMe, const net::MessageNotify& notify,
                                     bool directDownload, bool* cached);

/**
 *  处理文件气泡上与会话无关的本地文件动作：打开文件、打开所在文件夹、另存为。
 *  @param owner 弹出另存为对话框、调用 ShellExecute 时用的父窗口
 *  @param d     气泡投上来的动作负载；所有权仍归调用方
 *  @return d.action 属于上述三种之一时返回 true（无论本地路径是否为空都算已处理）；
 *          其余动作返回 false，由调用方按会话类型自行处理
 */
bool HandleLocalFileAction(HWND owner, const ChatFileActionData& d);

/**
 *  从服务器下载文件气泡对应的文件，并把气泡切到「下载中」。下载进度与结果由
 *  net::FileTransfer 回投主面板，再按 msgid 广播回各聊天窗。
 *  @param d    气泡投上来的动作负载，须带 msgid 与 fileId；所有权仍归调用方
 *  @param list 气泡所在的消息列表，用来把该行切到「下载中」；可为 NULL（只发起下载）
 *  @return 成功发起下载返回 true；缺少 msgid 或 fileId 时返回 false 且不做任何事
 */
bool StartChatFileDownload(const ChatFileActionData& d, ChatMessageList* list);

} // namespace balloonchatwnd
