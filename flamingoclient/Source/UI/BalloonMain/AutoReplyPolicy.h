/**
 *  AutoReplyPolicy.h
 *
 *  「离开 / 忙碌时自动回复」的<u>纯逻辑判定</u>：给定本人当前的在线状态与连接态、自动回复设置、
 *  以及刚收到的这条消息，判断要不要自动回复、回复哪段文字。不碰 HWND、不发网络请求、不读配置
 *  文件，便于被 FlamingoUnit 测试工程直接断言（Tests/AutoReplyPolicyTests.cpp）。
 *  真正发出回复、回执后入库与显示在主面板（CBalloonMainPanel）里做。
 *
 *  规则（2026-10-03 与用户确认）：
 *    · 只在单聊里回复，群聊（包括群里 @我）一律不回复；
 *    · 离开、忙碌各有一个开关与一段文字，按收到消息时的状态取对应的那一套；
 *    · 对方发几条就回几条，但同一条消息（同一个 msgid）重复到达只回一次；
 *    · 不回复：自己发出的、系统消息、通话记录、文件夹传输记录、对方发来的自动回复
 *      （后者保证两台都开着自动回复的客户端不会互相无限回复）；
 *    · 本人正看着与对方的聊天窗时不回复（说明人就在电脑前）；
 *    · 连接没有就绪（正在连接、离线）时不回复。
 *
 *  典型用法：
 *      balloonmain::CAutoReplyPolicy policy;     //主面板成员，生命周期跟随登录会话
 *      std::wstring text;
 *      if (policy.Decide(presence, connUiState, selfId, settings, incoming, text)
 *          == balloonmain::kAutoReplySend)
 *      {
 *          ...把 text 作为自动回复发给 incoming.m_from...
 *      }
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <deque>
#include <string>
#include <stdint.h>

namespace balloonmain {

//自动回复文字的长度上限，按 UTF-16 字符计（与用户确认的「100 个字」）。设置页输入框与发送前的
//规范化共用这一个值；截断时不会把一个由两个 UTF-16 字符组成的表情从中间切开。
const size_t kAutoReplyMaxChars = 100;

//离开时的默认回复文字：用户没有设置、或设置成空白时使用。
extern const wchar_t* const kDefaultAwayReplyText;

//忙碌时的默认回复文字：用户没有设置、或设置成空白时使用。
extern const wchar_t* const kDefaultBusyReplyText;

/**
 *  自动回复设置：系统设置「聊天」页的两个开关与两段文字。由调用方从 CUserConfig 读出后填入。
 */
struct AutoReplySettings
{
    bool         m_awayEnabled;  // 「离开时自动回复」开关；默认关闭
    std::wstring m_awayText;     // 离开时回复的文字，用户设置的原文，不含「[自动回复] 」前缀；可为空（发送时改用默认文字）
    bool         m_busyEnabled;  // 「忙碌时自动回复」开关；默认关闭
    std::wstring m_busyText;     // 忙碌时回复的文字，含义同 m_awayText

    AutoReplySettings()
        : m_awayEnabled(false)
        , m_awayText(kDefaultAwayReplyText)
        , m_busyEnabled(false)
        , m_busyText(kDefaultBusyReplyText)
    {
    }
};

/**
 *  判定所需的、关于刚收到的这条消息的事实。由调用方从 net::MessageNotify 与界面状态填入。
 */
struct AutoReplyIncoming
{
    int     m_ctype;        // 会话类型：1 单聊，2 群聊；其它取值按「不是单聊」处理
    int64_t m_from;         // 发送方 userid
    int64_t m_msgid;        // 服务器分配的 msgid；<= 0 表示不是服务端落库的消息（不回复）
    int     m_msgtype;      // 消息类型，取值见 net/Protocol.h 的 message_type
    bool    m_isAutoReply;  // 这条消息本身是不是对方的自动回复（content 里带 autoreply 标记）
    bool    m_watching;     // 本人此刻是否正看着与发送方的聊天窗（可见、未最小化、在前台）

    AutoReplyIncoming()
        : m_ctype(0)
        , m_from(0)
        , m_msgid(0)
        , m_msgtype(0)
        , m_isAutoReply(false)
        , m_watching(false)
    {
    }
};

/**
 *  Decide 的结论。除 kAutoReplySend 外都是「不回复」，各值说明不回复的原因，供调用方打日志。
 */
enum AutoReplyDecision
{
    kAutoReplySend             = 0,   // 回复：出参 outText 为要发出的文字（不含前缀）
    kAutoReplySkipNotSingle    = 1,   // 不回复：不是单聊（群聊一律不回复）
    kAutoReplySkipFromSelf     = 2,   // 不回复：消息是本人发出的（多端同步、本人转发后回填的那一份）
    kAutoReplySkipPresence     = 3,   // 不回复：本人当前不是离开或忙碌（在线、离线）
    kAutoReplySkipDisabled     = 4,   // 不回复：当前状态对应的那个开关没有打开
    kAutoReplySkipNotConnected = 5,   // 不回复：连接没有就绪（正在连接、离线），回复发不出去
    kAutoReplySkipMsgType      = 6,   // 不回复：系统消息、通话记录、文件夹传输记录这类服务端生成的记录
    kAutoReplySkipPeerAuto     = 7,   // 不回复：对方发来的本身就是自动回复，避免两边互相无限回复
    kAutoReplySkipWatching     = 8,   // 不回复：本人正看着与对方的聊天窗
    kAutoReplySkipNoMsgid      = 9,   // 不回复：消息没有服务器 msgid，无法判断是否重复
    kAutoReplySkipDuplicate    = 10,  // 不回复：同一条消息（同一个 msgid）已经回复过
};

/**
 *  把用户设置的回复文字整理成要发出的文字：去掉首尾空白；去掉后为空则改用默认文字；
 *  超过 kAutoReplyMaxChars 的部分截掉（不切开代理对）。
 *    text：       用户设置的原文。
 *    defaultText：为空时改用的默认文字（kDefaultAwayReplyText / kDefaultBusyReplyText）；
 *                 传 NULL 时按空串处理。
 *  @return 整理后的文字。本函数无副作用。
 */
std::wstring NormalizeAutoReplyText(const std::wstring& text, const wchar_t* defaultText);

/**
 *  把文字截到 kAutoReplyMaxChars 个 UTF-16 字符以内，截断点落在代理对中间时往前退一个字符。
 *  设置页输入框限长与 NormalizeAutoReplyText 共用。
 *    text：原文。
 *  @return 截断后的文字；不超过上限时原样返回。
 */
std::wstring ClampAutoReplyText(const std::wstring& text);

/**
 *  自动回复判定器。只记一份状态：最近回复过的消息 msgid，用于同一条消息重复到达时只回一次。
 *  生命周期跟随主面板（一次登录会话）；登出或换账号时调用 Reset。
 */
class CAutoReplyPolicy
{
public:
    CAutoReplyPolicy();

    /**
     *  判断要不要对刚收到的这条消息自动回复。
     *    presence：   本人当前的在线状态，取值见 net/Protocol.h 的 presence_type。
     *    connUiState：界面连接态，取值见 OnlineCapability.h 的 ConnectionUiState；只有在线（连接就绪）才回复。
     *    selfId：     本人 userid；<= 0 表示取不到，此时一律按「不是本人发出的」处理。
     *    settings：   自动回复设置，只读。
     *    msg：        这条消息的事实，只读。
     *    outText：    [出参] 返回 kAutoReplySend 时为要发出的文字（已经过 NormalizeAutoReplyText，
     *                 不含「[自动回复] 」前缀）；其它返回值时清空。
     *  @return 结论，见 AutoReplyDecision。
     *  副作用：返回 kAutoReplySend 时记下这条消息的 msgid，之后同一个 msgid 再来返回 kAutoReplySkipDuplicate。
     */
    AutoReplyDecision Decide(int presence, int connUiState, int64_t selfId,
                             const AutoReplySettings& settings, const AutoReplyIncoming& msg,
                             std::wstring& outText);

    /**
     *  清空回复过的 msgid 记录。登出、换账号时调用。
     */
    void Reset();

private:
    // 最近回复过的消息 msgid，按回复先后排列；超过容量时丢弃最早的一条。只用来识别同一条
    // 消息重复到达（服务端重推、本端重复处理），不需要记住整个会话期间的全部消息
    std::deque<int64_t> m_repliedMsgids;
};

} // namespace balloonmain
