/**
 *  AutoReplyPolicy.cpp
 *
 *  CAutoReplyPolicy 与自动回复文字整理函数的实现，规则与用法见 AutoReplyPolicy.h。
 *  全部为内存中的判断与字符串处理，无任何 Win32 / 网络副作用。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// 本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
// （工程里对应的 PrecompiledHeader 设为 NotUsing），与 OnlineCapability.cpp 同例。
#include "AutoReplyPolicy.h"

#include <algorithm>
#include <windows.h>               // OnlineCapability.h 要求调用方先引入 windows.h
#include "OnlineCapability.h"      // kConnUiOnline
#include "../../net/Protocol.h"    // presence_type / message_type
#include "../../Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找

namespace balloonmain {

const wchar_t* const kDefaultAwayReplyText = TR_NOOP(L"您好，我现在不在，稍后回复您。");
const wchar_t* const kDefaultBusyReplyText = TR_NOOP(L"您好，我正在忙，稍后回复您。");

namespace {

// 判断重复时记住的最近回复过的 msgid 条数。重复到达只会发生在刚处理过的几条消息上，
// 记 64 条足够覆盖，又不会随会话时长无限增长
const size_t kRepliedMsgidCapacity = 64;

// 会话类型取值：单聊（与 net/Protocol.h 里 ctype 的约定一致）
const int kCtypeSingle = 1;

// 代理对高位的取值范围：落在这个范围内的 UTF-16 字符必须与后一个字符一起保留或一起截掉
const wchar_t kHighSurrogateFirst = 0xD800;
const wchar_t kHighSurrogateLast  = 0xDBFF;

// 全角空格：中文输入法下常见的空白，去首尾空白时与半角空白一样处理
const wchar_t kFullWidthSpace = 0x3000;

//是不是需要去掉的空白字符
bool IsReplyWhitespace(wchar_t ch)
{
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == kFullWidthSpace;
}

//这类消息是服务端生成的记录条，不是对方发来的话，不自动回复
bool IsServerGeneratedType(int msgtype)
{
    return msgtype == message_type_system || msgtype == message_type_rtc
        || msgtype == message_type_folder;
}

} // anonymous namespace

std::wstring ClampAutoReplyText(const std::wstring& text)
{
    if (text.size() <= kAutoReplyMaxChars)
    {
        return text;
    }
    size_t cut = kAutoReplyMaxChars;
    //截断点前一个字符是代理对的高位时，把它也去掉，免得留下半个表情
    const wchar_t last = text[cut - 1];
    if (last >= kHighSurrogateFirst && last <= kHighSurrogateLast)
    {
        --cut;
    }
    return text.substr(0, cut);
}

std::wstring NormalizeAutoReplyText(const std::wstring& text, const wchar_t* defaultText)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && IsReplyWhitespace(text[begin]))
    {
        ++begin;
    }
    while (end > begin && IsReplyWhitespace(text[end - 1]))
    {
        --end;
    }
    if (begin == end)
    {
        return ClampAutoReplyText((defaultText != NULL) ? std::wstring(defaultText) : std::wstring());
    }
    return ClampAutoReplyText(text.substr(begin, end - begin));
}

CAutoReplyPolicy::CAutoReplyPolicy()
{
}

AutoReplyDecision CAutoReplyPolicy::Decide(int presence, int connUiState, int64_t selfId,
                                           const AutoReplySettings& settings,
                                           const AutoReplyIncoming& msg, std::wstring& outText)
{
    outText.clear();

    if (msg.m_ctype != kCtypeSingle)
    {
        return kAutoReplySkipNotSingle;
    }
    if (selfId > 0 && msg.m_from == selfId)
    {
        return kAutoReplySkipFromSelf;
    }

    //按当前状态取对应的那一套开关与文字；在线、离线等其它状态不回复
    bool enabled = false;
    const std::wstring* text = NULL;
    const wchar_t* defaultText = NULL;
    if (presence == presence_away)
    {
        enabled = settings.m_awayEnabled;
        text = &settings.m_awayText;
        defaultText = TR(kDefaultAwayReplyText);
    }
    else if (presence == presence_busy)
    {
        enabled = settings.m_busyEnabled;
        text = &settings.m_busyText;
        defaultText = TR(kDefaultBusyReplyText);
    }
    else
    {
        return kAutoReplySkipPresence;
    }
    if (!enabled)
    {
        return kAutoReplySkipDisabled;
    }
    if (connUiState != kConnUiOnline)
    {
        return kAutoReplySkipNotConnected;
    }

    if (IsServerGeneratedType(msg.m_msgtype))
    {
        return kAutoReplySkipMsgType;
    }
    if (msg.m_isAutoReply)
    {
        return kAutoReplySkipPeerAuto;
    }
    if (msg.m_watching)
    {
        return kAutoReplySkipWatching;
    }
    if (msg.m_msgid <= 0)
    {
        return kAutoReplySkipNoMsgid;
    }
    if (std::find(m_repliedMsgids.begin(), m_repliedMsgids.end(), msg.m_msgid) != m_repliedMsgids.end())
    {
        return kAutoReplySkipDuplicate;
    }

    m_repliedMsgids.push_back(msg.m_msgid);
    if (m_repliedMsgids.size() > kRepliedMsgidCapacity)
    {
        m_repliedMsgids.pop_front();
    }
    outText = NormalizeAutoReplyText(*text, defaultText);
    return kAutoReplySend;
}

void CAutoReplyPolicy::Reset()
{
    m_repliedMsgids.clear();
}

} // namespace balloonmain
