/**
 *  ChatQuoteModel.cpp
 *
 *  引用回复的换算与摘要生成实现。说明见 ChatQuoteModel.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-03
 */

// ---- balloonui / ATL 头文件要求的前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>

#include "ChatQuoteModel.h"
#include "../BalloonChat/BalloonChatMsgFormat.h"   // BodyToDigest（内联表情→[表情]）
#include "ChatMarkdown.h"                          // MarkdownDigestFromBody（Markdown 消息去掉标记）
#include "ChatCodeMessage.h"                       // CodeDigestFromCode（代码片段写成「[代码] 第一行」）
#include "../../Common/Util/EncodeUtil.h"                          // UTF-8 ↔ 宽字符
#include "../../net/Protocol.h"
#include "ChatVoiceBubble.h"   // FormatVoiceDuration：引用语音时摘要带时长

#include <vector>
#include <ctime>   // localtime_s（引用头部的时间格式化）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

// 非文本消息在引用块里的占位文案。与会话列表预览同口径 —— 用户在两处看到的
// 说法一致，不必二次理解。
const TCHAR* const kDigestImage = TR_NOOP(L"[图片]");
const TCHAR* const kDigestFile  = TR_NOOP(L"[文件]");
const TCHAR* const kDigestOther = TR_NOOP(L"[消息]");

// 截断超长摘要并补省略号。按 UTF-16 字符截 —— 引用块只有一行，再长也显示不下，
// 而快照要随每条消息落库，没必要把整段正文都塞进去。
CString TruncateDigest(const CString& text)
{
    if (text.GetLength() <= kQuoteDigestMaxChars)
    {
        return text;
    }
    CString cut = text.Left(kQuoteDigestMaxChars);
    cut += _T("…");
    return cut;
}

// UTF-8 → CString。
CString Utf8ToCStr(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    std::vector<wchar_t> buf(utf8.size() + 1, 0);
    EncodeUtil::Utf8ToUnicode(utf8.c_str(), &buf[0], (int)buf.size());
    return CString(&buf[0]);
}

// CString → UTF-8。
std::string CStrToUtf8(const CString& s)
{
    if (s.IsEmpty())
    {
        return std::string();
    }
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)s, -1, NULL, 0, NULL, NULL);
    if (need <= 1)
    {
        return std::string();
    }
    std::vector<char> buf((size_t)need, 0);
    ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)s, -1, &buf[0], need, NULL, NULL);
    return std::string(&buf[0]);
}

// 把 Unix 毫秒时间戳格式化成"2026-08-04 10:23"。引用头部只到分钟 —— 秒对"这条是
// 什么时候说的"没有意义，反而让那一行更长。
CString FormatQuoteTime(long long timestampMs)
{
    if (timestampMs <= 0)
    {
        return CString();
    }
    const time_t secs = (time_t)(timestampMs / 1000);
    struct tm tmv;
    if (::localtime_s(&tmv, &secs) != 0)
    {
        return CString();
    }
    CString out;
    out.Format(_T("%04d-%02d-%02d %02d:%02d"),
               tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
               tmv.tm_hour, tmv.tm_min);
    return out;
}

} // anonymous namespace

int MsgKindToProtocolType(int kind)
{
    // 两套枚举的数值<u>不</u>重合，逐个显式映射，绝不靠"碰巧相等"。
    switch (kind)
    {
    case kMsgText:
        return message_type_text;
    case kMsgEmoji:
        // 大 emoji 在协议里就是一条普通文本消息，没有独立类型。
        return message_type_text;
    case kMsgImage:
        return message_type_image;
    case kMsgFile:
        return message_type_file;
    case kMsgRelay:
        // 群接龙卡片：引用块只显示摘要「[接龙] 标题」，类型如实写出
        return message_type_relay;
    case kMsgVoice:
        // 语音：引用块只显示「[语音] 8″」，类型如实写出（收方据此还原为语音，不画缩略图）
        return message_type_voice;
    default:
        // 时间分隔 / 撤回提示这类行根本不该被引用，真走到这里按文本处理，
        // 至少不会把类型写成别的东西。
        return message_type_text;
    }
}

int ProtocolTypeToMsgKind(int msgtype)
{
    switch (msgtype)
    {
    case message_type_text:
        return kMsgText;
    case message_type_image:
        return kMsgImage;
    case message_type_file:
        return kMsgFile;
    case message_type_voice:
        return kMsgVoice;
    default:
        return kMsgText;
    }
}

CString BuildQuoteDigest(const ChatMessage& src)
{
    switch (src.kind)
    {
    case kMsgText:
        // 走 BodyToDigest：内联表情替换成 [表情]、"//" 还原成 "/"，不把
        // /f["5"] 这类协议转义原样暴露给用户。与会话列表预览同一个函数。
        // Markdown 消息另外去掉 **、# 等标记（DLL 不可用时与 BodyToDigest 相同）
        // 代码片段写成「[代码] 第一行」（docs/代码片段方案.md 第 3.4 节）
        if (src.isCode)
        {
            return TruncateDigest(CString(CodeDigestFromCode(std::wstring((LPCWSTR)src.text), src.codeNotes).c_str()));
        }
        if (src.isMarkdown)
        {
            return TruncateDigest(MarkdownDigestFromBody(src.text));
        }
        return TruncateDigest(balloonchat::BodyToDigest(src.text));
    case kMsgEmoji:
        return TruncateDigest(src.text);
    case kMsgImage:
        return TR(kDigestImage);
    case kMsgFile:
        // 文件给文件名比给 [文件] 有信息量得多 —— 一个会话里往往有好几个文件。
        return src.fileName.IsEmpty() ? CString(TR(kDigestFile)) : TruncateDigest(src.fileName);
    case kMsgRelay:
        // 接龙卡片的 text 已由 ApplyRelayCard 写成「[接龙] 标题」（docs/群接龙方案.md §3.7）
        return TruncateDigest(src.text);
    case kMsgVoice:
        // 语音：类型加时长，如「[语音] 8″」，与消息记录窗的预览同一写法
        return CString(TR(L"[语音]")) + L" " + FormatVoiceDuration(src.voiceDuration).c_str();
    default:
        return TR(kDigestOther);
    }
}

CString BuildQuotedReplyText(const CString& senderName, long long timestampMs,
                             const CString& digest)
{
    // 头尾两条线用不同字符：头部用 ASCII 短横（与纯文本邮件的引用线一致，任何字体都画得出），
    // 结尾用制表线，视觉上比再来一遍短横更像"引用到此为止"。
    const TCHAR* const kHeadRule = TR(L"------------------ 原消息 ------------------");
    const TCHAR* const kTailRule = _T("───────────────────────");

    // 换行统一用 CRLF —— RichEdit 的 EM_REPLACESEL 认这一种；只给 LF 会被它当成
    // 一个不可见字符吞掉，整段引用会挤成一行。
    const TCHAR* const kCrLf = _T("\r\n");

    CString text;
    text += kHeadRule;
    text += kCrLf;

    text += TR(L"发件人：");
    text += senderName;
    text += kCrLf;

    // 时间未知（本端刚发出、回执未到）时整行不输出 —— 与其写一个假时间，不如不写。
    if (timestampMs > 0)
    {
        text += TR(L"时间：");
        text += FormatQuoteTime(timestampMs);
        text += kCrLf;
    }

    text += kCrLf;
    text += digest;
    text += kCrLf;
    text += kCrLf;
    text += kTailRule;
    // 末尾再留一个空行：插入后光标停在最末，用户接着打字就是回复正文，
    // 不必自己先敲一下回车。
    text += kCrLf;
    text += kCrLf;
    return text;
}

void FillQuoteForRender(const net::ReplyQuote& quote, ChatMessage& out,
                        const CString& cachedImagePath)
{
    if (!quote.m_valid || quote.m_msgid == 0)
    {
        // 没有引用（或引用数据坏到没有可定位的原消息）：清干净，按无引用渲染。
        out.hasQuote       = false;
        out.quoteName.Empty();
        out.quoteText.Empty();
        out.quoteMsgid     = 0;
        out.quoteFrom      = 0;
        out.quoteMsgType   = 0;
        out.quoteImagePath.Empty();
        out.quoteFileId.Empty();
        out.quoteMd5.Empty();
        out.quoteImageW    = 0;
        out.quoteImageH    = 0;
        return;
    }

    out.hasQuote     = true;
    out.quoteMsgid   = quote.m_msgid;
    out.quoteFrom    = quote.m_from;
    out.quoteName    = Utf8ToCStr(quote.m_name);
    out.quoteText    = Utf8ToCStr(quote.m_digest);
    // 存成气泡口径，渲染侧直接按 ChatMsgKind 分派，不必每处再换算一遍。
    out.quoteMsgType = ProtocolTypeToMsgKind(quote.m_msgtype);

    // 图片引用的附加项。缩略图本地路径由渲染侧按 fileid/md5 查缓存后回填，
    // 这里只把定位所需的信息带过去。
    out.quoteFileId  = Utf8ToCStr(quote.m_fileid);
    out.quoteMd5     = Utf8ToCStr(quote.m_md5);
    out.quoteImageW  = quote.m_width;
    out.quoteImageH  = quote.m_height;
    out.quoteImagePath = cachedImagePath;
}

net::ReplyQuote BuildQuoteFromMessage(const ChatMessage& src,
                                      const CString& senderName,
                                      const CString& imageFileId,
                                      const CString& imageMd5)
{
    net::ReplyQuote q;

    // 没有服务器 msgid 说明原消息还没落库（本端刚发出、回执未到）——
    // 此刻引用它，对端拿到的 msgid 是 0，永远定位不到，不如不让引用。
    if (src.msgid == 0)
    {
        return q;
    }

    q.m_valid   = true;
    q.m_msgid   = src.msgid;
    q.m_from    = src.senderUserId;
    q.m_name    = CStrToUtf8(senderName);
    q.m_msgtype = MsgKindToProtocolType(src.kind);
    q.m_digest  = CStrToUtf8(BuildQuoteDigest(src));

    if (src.kind == kMsgImage)
    {
        // 收方据 fileid + md5 命中本地缓存或发起下载，宽高用来等比缩放。
        // ChatMessage 本身不持有 fileid / md5（它只关心怎么画），故由调用方从
        // 自己的发送 / 接收记录里取好传进来。
        q.m_fileid = CStrToUtf8(imageFileId);
        q.m_md5    = CStrToUtf8(imageMd5);
        q.m_width  = src.imageW;
        q.m_height = src.imageH;
    }
    return q;
}

} // namespace balloonchatwnd
