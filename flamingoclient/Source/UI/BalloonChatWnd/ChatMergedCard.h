/**
 *  ChatMergedCard.h
 *
 *  合并转发的聊天记录（msgtype=15）在聊天窗里显示为一张卡片：标题、前几条子消息的
 *  「发送者：摘要」、底部「聊天记录（共 N 条）」。本文件把 content 原文解析成卡片要显示
 *  的内容，并套用到一条气泡数据上。纯逻辑，不碰绘制，可被 FlamingoUnit 直接测试。
 *  卡片的绘制在 ChatBubble 里，交互见 docs/聊天消息转发方案.md §3.5。
 *
 *  典型用法（聊天窗收到或翻到一条合并记录时）：
 *      balloonchatwnd::ChatMessage msg = MakeRealTextMessage(fromMe, _T("[聊天记录]"), ...);
 *      balloonchatwnd::ApplyMergedCard(msg, notify.m_mergedContent);
 *      list->AppendMessage(msg);
 *  先按普通文字气泡构造，是为了沿用各聊天窗已有的头像、发送人名与 msgid 的填法。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <string>
#include <vector>

#include "ChatMockData.h"   // ChatMessage / kMsgMerged

namespace balloonchatwnd {

//卡片上最多显示几条子消息的摘要。再多卡片就太高，用户点开查看窗看全部
const size_t kMergedCardPreviewLines = 4;

/**
 *  卡片要显示的内容。
 */
struct MergedCardPreview
{
    CString              m_title;   // 记录标题
    std::vector<CString> m_lines;   // 前 kMergedCardPreviewLines 条子消息的「发送者：摘要」
    int                  m_count;   // 子消息总条数

    MergedCardPreview()
        : m_count(0)
    {
    }
};

/**
 *  把合并记录的 content 原文解析成卡片内容。
 *  发送者取子消息带的 fromname（转发当时的名字），没有时写「用户<id>」；摘要取
 *  net::MessageProtocolClient::digestForMsgType（文字取正文、内联表情写成「[表情]」，
 *  其它类型取「[图片]」「[聊天记录]」这类占位）。
 *  @param contentJson content 原文（紧凑 JSON，UTF-8）
 *  @param out         [出参] 卡片内容
 *  @return 解析成功返回 true；content 不可用时返回 false，out 保持默认值
 */
bool BuildMergedCardPreview(const std::string& contentJson, MergedCardPreview& out);

/**
 *  把一条气泡数据改成合并卡片：kind 置为 kMsgMerged，填写 mergedXxx 字段，text 置为
 *  「[聊天记录]」（引用回复、复制等仍按 text 取摘要的地方因此拿到类型占位）。
 *  content 不可用时仍是一张卡片，标题取「聊天记录」、没有预览行、条数为 0。
 *  @param msg         [入参/出参] 已按普通文字气泡构造好的气泡数据（头像、msgid 等保持不变）
 *  @param contentJson content 原文，原样存入 msg.mergedContent，供打开查看窗时使用
 */
void ApplyMergedCard(ChatMessage& msg, const std::string& contentJson);

/**
 *  合并记录里一条在线直传文件子消息的说明文字（2026-10-05 起）：「[文件] 文件名（在线直传的文件，服务器未保存，
 *  无法转发）」。在线直传的文件服务器上没有副本，合并转发时文件本身不随记录发出，记录里只留这行文字；
 *  卡片的预览行与查看窗的那一行都用它。
 *  @param fileNameUtf8 文件名（UTF-8）；为空时省去文件名
 *  @return 已翻译的说明文字
 */
CString MergedDirectFilePlaceholder(const std::string& fileNameUtf8);

} // namespace balloonchatwnd
