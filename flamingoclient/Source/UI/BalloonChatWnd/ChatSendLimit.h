/**
 *  ChatSendLimit.h
 *
 *  聊天窗发送文字前的体积检查，以及发送失败回执的提示文案。
 *
 *  背景：服务端对单条消息 content 的体积有上限
 *  MESSAGE_CONTENT_MAX_BYTES（256 KiB，按紧凑 JSON 计，一个汉字计 6 字节），超限回
 *  error_code_msg_too_large。超大消息若被放行，推送或同步给对方时整帧会超过客户端的单帧
 *  上限，对方断线。输入框本身不限字数，由本模块在按下发送时检查：任何一段文字超限就整条
 *  不发，弹窗提示，输入框原样保留，方便用户删减后再发。
 *
 *  计量方式与实际发送逐段一致：文字段的 content 由 MessageProtocolClient::textContentBytes
 *  按发送请求同一段代码组装并序列化；引用只随第一段文字发出（聊天窗发完第一段就清掉引用）；
 *  群聊的 @ 按每段文字各自用 RescanMentions 重扫，与 SendGroupTextPart 相同。
 *
 *  纯逻辑，不碰窗口与网络，FlamingoUnit 直接断言（Tests/ChatSendLimitTests.cpp）。
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（用到 CString）。
 *
 *  典型用法（聊天窗 DoSend，生成任何气泡之前）：
 *      if (balloonchatwnd::FindOversizedTextPart(parts, m_currentFont, m_pendingQuote,
 *                                                m_pendingMentions) >= 0)
 *      {
 *          ::MessageBox(m_hWnd, TR(balloonchatwnd::kSendTextTooLargeTip), ...);
 *          return;
 *      }
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <string>
#include <vector>

#include "ChatRichContentSplit.h"              // RichOutgoingPart
#include "GroupMentionModel.h"                 // MentionCandidate
#include "../../net/MessageProtocolClient.h"   // net::TextFont / net::ReplyQuote

namespace balloonchatwnd {

// 按下发送时某段文字超限的提示文案。只做了标记（TR_NOOP）的原文，显示时须经 TR 取译文。
extern const wchar_t* const kSendTextTooLargeTip;

/**
 *  在待发送的各段里找出第一段超过单条消息体积上限的文字段。
 *  @param parts      BuildOutgoingParts 的结果（文字段与图片段按文档顺序排列；图片段不计）
 *  @param font       当前字体设置，每段文字都带
 *  @param quote      待发的引用回复快照，只计入第一段文字
 *  @param mentions   群聊里待发的 @ 候选，每段文字按 RescanMentions 各自重扫；单聊传空
 *  @param textFormat 文字段的格式，取值见 net::text_format；Markdown 时计入 content.format
 *  @return 超限那一段在 parts 里的下标；都没超限返回 -1
 */
int FindOversizedTextPart(const std::vector<RichOutgoingPart>& parts,
                          const net::TextFont& font,
                          const net::ReplyQuote& quote,
                          const std::vector<MentionCandidate>& mentions,
                          int textFormat = net::text_format_plain);

/**
 *  发送消息失败回执（cmd_send_message 应答 code 非 0）的提示文案。
 *  @param code            应答里的错误码，取值见 enum error_code
 *  @param groupNotifyOnly 是不是在开启了仅通知模式（原称全员禁言）的群里、本人又是普通成员（docs/群管理接入方案.md §14）。
 *                         服务端对单人禁言与仅通知模式都回 306，为 true 时 306 说成「本群为仅通知模式……」；单聊传 false
 *  @return 给用户看的一句话；内容过长（error_code_msg_too_large）有专门的说法，
 *          其余错误码给出通用说法并带上错误码，便于排查
 */
std::wstring SendFailureText(int code, bool groupNotifyOnly = false);

/**
 *  发文件失败的原因（文件气泡下方显示）：申请上传被拒或上传过程出错。
 *  @param code            失败错误码，取值见 enum error_code；0 表示没有错误码（如本地读文件失败）
 *  @param groupNotifyOnly 含义同 SendFailureText
 *  @return 不带句号的短句；文件名包含敏感词（621，或服务端在发消息时以 507 拒绝）、被禁言或仅通知模式（306）有专门的说法
 */
std::wstring FileSendFailureText(int code, bool groupNotifyOnly = false);

/**
 *  群文件上传（群聊窗「发文件」、群文件面板上传）失败的提示，弹框用。
 *  @param code 失败错误码；621（error_code_file_sensitive_word）为文件名包含敏感词
 *  @return 带句号的整句；其余错误码沿用原来的通用说法
 */
std::wstring GroupFileUploadFailureText(int code);

} // namespace balloonchatwnd
