/**
 *  ChatFolderAction.h
 *
 *  文件夹气泡（ChatBubble 的 kMsgFolder 行）底部按钮被点击时，气泡向宿主聊天窗
 *  PostMessage(WM_CHAT_FOLDER_ACTION, 0, (LPARAM)堆上 ChatFolderActionData*)，由宿主
 *  聊天窗集中处理（接收 / 另存为 / 拒绝 / 取消 / 重试续传 / 重新发送 / 打开文件夹），
 *  处理后 delete。
 *  与文件气泡的 ChatFileAction.h 是同一类东西、同一套写法：把"哪一次传输、做什么动作、
 *  所需的定位与元信息"收口到一个负载结构里，气泡只管发起，不持有会话与传输逻辑。
 *
 *  与文件那份分开定义而不复用：文件夹的动作枚举本就是另一套（见 ChatFolderActionKind），
 *  定位用的是会话 id 而不是 clientMsgId / msgid，混用会让宿主的分发代码分不清手上这条
 *  是文件还是文件夹。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（CString）。
 *
 *  balloonwj@qq.com   2026-08-14
 */
#pragma once

#include <string>
//动作枚举 ChatFolderActionKind 与状态枚举 ChatFolderState 定义在此（纯逻辑、无 ATL/WTL
//依赖，单元测试也要用）；本头文件在其之上追加依赖 CString 的动作负载结构。
#include "ChatFolderStateModel.h"

namespace balloonchatwnd {

//文件夹气泡按钮动作负载（堆对象，宿主聊天窗处理后 delete）。按动作不同，用到的字段
//不同：接收 / 另存为 / 拒绝 / 取消 / 重试只需 sessionId 定位会话；打开文件夹只需 localRoot；
//重新发送要从零发起一次完整传输，故还需要源目录路径与文件夹名。
struct ChatFolderActionData
{
    int         action;      // 动作类型，取值见 ChatFolderActionKind
    bool        fromMe;      // true=本端发起的这次传输，false=对方发来的邀请。宿主据它决定
                             // 同一个动作 id 发哪条信令（如「取消」在邀请态下，发送端是撤回
                             // 邀请、接收端是拒绝之后的中止），以及「打开文件夹」开哪个目录
    std::string sessionId;   // 本次传输的会话 id（UTF-8）。除「重新发送」外的全部动作都靠它
                             // 在服务端定位会话；会话尚未建立时为空
    CString     folderName;  // 文件夹名（重新发送时的提示文案、日志与会话冒泡摘要用）
    CString     localRoot;   // 本地根目录完整路径。发送端是源目录、接收端是落地目录；
                             // 「打开文件夹」与「重新发送」都要它。空=尚未确定
    int         fileCount;   // 文件夹内的文件总数，单位：个（重新发送前的二次确认提示用）
    long long   sizeBytes;   // 文件夹内全部文件的字节数之和（同上）

    ChatFolderActionData()
        : action(0)
        , fromMe(false)
        , fileCount(0)
        , sizeBytes(0)
    {
    }
};

} // namespace balloonchatwnd
