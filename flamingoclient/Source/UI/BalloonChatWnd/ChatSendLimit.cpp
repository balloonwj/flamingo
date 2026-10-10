/**
 *  ChatSendLimit.cpp —— 聊天窗发送文字前的体积检查与发送失败提示文案。说明见 ChatSendLimit.h。
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "ChatSendLimit.h"
#include "GroupNotifyOnly.h"   // NotifyOnlyInputHintText：仅通知模式下发送被拒的提示
#include "GroupMustRead.h"     // MustReadSendFailureText：尚未确认入群必读时发送被拒的提示

#include "../../Common/Util/EncodeUtil.h"
#include "../../net/Protocol.h"   // MESSAGE_CONTENT_MAX_BYTES / error_code_msg_too_large
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonchatwnd {

const wchar_t* const kSendTextTooLargeTip = TR_NOOP(L"消息过长，请删减后分条发送。");

namespace {

// CString → UTF-8。与单聊窗、群聊窗发送文字时的转换完全相同（同一个 EncodeUtil 函数、
// 同样按 4 倍预留缓冲），保证这里量出的体积与真正发出去的一致。
std::string ToUtf8(const CString& text)
{
    const int wlen = text.GetLength();
    if (wlen <= 0)
    {
        return std::string();
    }
    // UTF-8 单个字符最多 4 字节，按 4 倍 + 1 预留缓冲。
    std::vector<char> buf((size_t)wlen * 4 + 1, 0);
    EncodeUtil::UnicodeToUtf8(text.GetString(), buf.data(), (int)buf.size());
    return std::string(buf.data());
}

} // namespace

int FindOversizedTextPart(const std::vector<RichOutgoingPart>& parts,
                          const net::TextFont& font,
                          const net::ReplyQuote& quote,
                          const std::vector<MentionCandidate>& mentions,
                          int textFormat)
{
    const net::ReplyQuote noQuote;
    bool firstText = true;   // 引用只随第一段文字发出：聊天窗发完第一段就清掉引用
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (parts[i].kind != kRichPartText)
        {
            continue;   // 图片段走图片消息，content 只有文件信息，不会超限
        }

        const std::string utf8 = ToUtf8(parts[i].text);
        //群聊按本段最终文本重扫 @，与 SendGroupTextPart 的做法相同；单聊不带候选，扫不出片段
        const std::vector<MentionSpan> ats = RescanMentions(utf8, mentions);
        const size_t bytes = net::MessageProtocolClient::textContentBytes(
            utf8, font, firstText ? quote : noQuote, ats, HasMentionAll(ats), textFormat);
        firstText = false;

        if (bytes > MESSAGE_CONTENT_MAX_BYTES)
        {
            return (int)i;
        }
    }
    return -1;
}

std::wstring SendFailureText(int code, bool groupNotifyOnly)
{
    //返回不带句号的短句：气泡下方的原因行直接用，弹框时由调用方补句号
    switch (code)
    {
    //消息命中发送者所属企业的敏感词库（docs/敏感词方案.md）
    case error_code_sensitive_word:
        return TR(L"消息包含敏感词，未能发出");

    //本人在该群被禁言，或群开启了仅通知模式（原称全员禁言）：已知是后一种时说清楚（docs/群管理接入方案.md §14）
    case error_code_member_muted:
        if (groupNotifyOnly)
        {
            return NotifyOnlyInputHintText();
        }
        return TR(L"你已被禁言，消息未能发出");

    //本人尚未确认入群必读（docs/群公告接入方案.md §14）
    case error_code_must_read_pending:
        return MustReadSendFailureText();

    //群设为仅群主和管理员可以 @所有人
    case error_code_at_all_denied:
        return TR(L"本群仅群主和管理员可以 @所有人");

    //单条消息内容超过服务端的体积上限。正常情况下发送前已经拦住，这里是服务端兜底拦截的结果
    case error_code_msg_too_large:
        return TR(L"消息内容过长，未能发出，请删减后分条发送");

    //其余错误：对端不存在、被拉黑、不能发给自己等，沿用通用说法并带上错误码便于排查
    default:
        return TRF(L"消息发送失败（错误码 {0}）", code);
    }
}

std::wstring FileSendFailureText(int code, bool groupNotifyOnly)
{
    switch (code)
    {
    //没有错误码：本地读文件失败、连不上文件服务器等，没有更具体的说法
    case 0:
        return TR(L"文件发送失败");

    //申请上传时文件名命中敏感词（621），或发文件消息时服务端按消息内容拒绝（507，content.name 命中）
    case error_code_file_sensitive_word:
    case error_code_sensitive_word:
        return TR(L"文件名包含敏感词，未能发出");

    //本人在该群被禁言，或群开启了仅通知模式：原先落到下面的通用说法、只显示错误码，用户看不懂（docs/群管理接入方案.md §14）
    case error_code_member_muted:
        if (groupNotifyOnly)
        {
            return NotifyOnlyInputHintText();
        }
        return TR(L"你已被禁言，文件未能发出");

    //本人尚未确认入群必读（docs/群公告接入方案.md §14）
    case error_code_must_read_pending:
        return MustReadSendFailureText();

    //其余错误：带上错误码便于排查
    default:
        return TRF(L"文件发送失败（错误码 {0}）", code);
    }
}

std::wstring GroupFileUploadFailureText(int code)
{
    //申请上传时文件名命中发送者所属企业的敏感词库（docs/敏感词方案.md 8.6）
    if (code == error_code_file_sensitive_word)
    {
        return TR(L"文件名包含敏感词，未能上传。");
    }
    //群文件已取消大小上限，其余失败一般是网络或服务端原因，沿用通用说法
    return TR(L"文件上传失败，请稍后重试。");
}

} // namespace balloonchatwnd
