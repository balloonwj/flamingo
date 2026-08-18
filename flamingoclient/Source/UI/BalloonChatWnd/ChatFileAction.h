/**
 *  ChatFileAction.h
 *
 *  文件气泡（ChatBubble 的 kMsgFile 行）底部按钮被点击时，气泡向宿主聊天窗
 *  PostMessage(WM_CHAT_FILE_ACTION, 0, (LPARAM)堆上 ChatFileActionData*)，由宿主
 *  聊天窗集中处理（打开文件 / 另存为 / 取消传输 / 重新发送下载），处理后 delete。
 *  把"哪条文件、做什么动作、所需的定位与元信息"收口到一个负载结构里，气泡只管
 *  发起、不持有传输与会话逻辑。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（CString）。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#pragma once

#include <string>
//动作枚举 ChatFileActionKind 与状态枚举定义在此（纯逻辑、无 ATL/WTL 依赖，单元测试
//也要用）；本头文件在其之上追加依赖 CString 的动作负载结构。
#include "ChatFileStateModel.h"

namespace balloonchatwnd {

//文件气泡按钮动作负载（堆对象，宿主聊天窗处理后 delete）。按动作不同，用到的字段
//不同：打开 / 另存为只需 localPath / fileName；取消 / 重试发送方按 clientMsgId、接收方
//按 msgid 定位，重新下载还需 fileId / md5 / sizeBytes。
struct ChatFileActionData
{
    int         action;       // 动作类型，取值见 ChatFileActionKind
    bool        fromMe;       // true=本端发出的文件（重试=重新发送），false=收到的文件（重试=重新下载）
    std::string clientMsgId;  // 关联本端"发送中"气泡（发送方重试 / 取消、按它定位本行）
    long long   msgid;        // 服务器消息ID（接收方重试 / 取消、按它定位本行；0=本端发出/无）
    CString     localPath;    // 本地文件完整路径（打开 / 另存为用；未就绪为空）
    CString     fileName;     // 原始文件名（另存为默认文件名、会话冒泡 digest 用）
    std::string fileId;       // 文件ID（接收方重新下载用，来自消息 content）
    std::string md5;          // 文件内容MD5（接收方重新下载校验用）
    long long   sizeBytes;    // 文件字节数

    ChatFileActionData()
        : action(0)
        , fromMe(false)
        , msgid(0)
        , sizeBytes(0)
    {
    }
};

} // namespace balloonchatwnd
