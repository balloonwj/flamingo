/**
 *  Markdown 消息的两个只用 std::string 的函数（docs/Markdown消息方案.md 第 12 节）：判断本地库 content 列是不是
 *  Markdown 消息、生成一行摘要。本头文件不依赖 ATL，供不使用预编译头、刻意不引入 ATL 的纯文件（如消息记录窗的
 *  HistoryView.cpp、HistoryConversationList.cpp）调用。
 *
 *  实现分别在 ChatMarkdownCard.cpp（IsMarkdownContentJson）与 ChatMarkdown.cpp（MarkdownDigestFromUtf8Body）；
 *  ChatMarkdownCard.h、ChatMarkdown.h 都包含本文件，原有调用方不受影响。
 *
 *  典型用法：
 *      if (balloonchatwnd::IsMarkdownContentJson(msgtype, contentJson))
 *          digest = balloonchatwnd::MarkdownDigestFromUtf8Body(textUtf8);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <string>

namespace balloonchatwnd {

/**
 *  本地库 content 列（V2MsgQuery 的 BuildContentJson 写入的规范化 JSON）记的是不是 Markdown 消息：文字消息，且
 *  format 为 "markdown"。
 *  @param msgtype     消息类型（::message_type_*）
 *  @param contentJson content 列的 JSON 文本（UTF-8）
 *  @return 是 Markdown 消息时返回 true；不是文字消息、没有 format、format 为其它值、JSON 无法解析时返回 false
 */
bool IsMarkdownContentJson(int msgtype, const std::string& contentJson);

/**
 *  同 MarkdownDigestFromBody（ChatMarkdown.h），输入输出都是 UTF-8，供网络层、主面板与消息记录窗直接使用：
 *  FlamingoMarkdown.dll 可用时为「[Markdown] 标题」，不可用时为去掉表情标记的正文。
 *  @param utf8Body 发送正文（UTF-8）
 *  @return 摘要（UTF-8）
 */
std::string MarkdownDigestFromUtf8Body(const std::string& utf8Body);

} // namespace balloonchatwnd
