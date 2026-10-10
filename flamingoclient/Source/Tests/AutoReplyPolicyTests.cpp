/**
 *  AutoReplyPolicyTests.cpp
 *
 *  「离开 / 忙碌时自动回复」判定 CAutoReplyPolicy 与文字整理函数的测试。覆盖与用户确认过的
 *  用例 ARP1～ARP8（2026-10-03）：
 *    ARP1 离开且「离开时自动回复」打开：单聊收到同事消息时回复，用离开那段文字；
 *    ARP2 忙碌且「忙碌时自动回复」打开：用忙碌那段文字；
 *    ARP3 两个开关互不影响；在线、离线、正在连接一律不回复；
 *    ARP4 自己发出的、系统消息、通话记录、文件夹记录、对方的自动回复不触发；
 *    ARP5 群聊一律不触发（包括群里 @我）；
 *    ARP6 同一个人连发三条回三次；同一条消息（同一个 msgid）重复到达只回一次；
 *    ARP7 本人正看着与对方的聊天窗时不回复；
 *    ARP8 设置的文字为空或只有空白时改用默认文字（另含长度截断）。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口、不连网络。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../UI/BalloonMain/AutoReplyPolicy.h"
#include "../UI/BalloonMain/OnlineCapability.h"   // kConnUiOnline / kConnUiConnecting / kConnUiOffline
#include "../net/Protocol.h"                      // presence_type / message_type

using namespace balloonmain;

namespace {

// 本人与两位同事的 userid，取值任意，只要互不相同且为正
const int64_t kSelfUid   = 10001;
const int64_t kPeerUid   = 10002;
const int64_t kOtherUid  = 10003;

// 第一条消息的 msgid，后续消息在它的基础上递增；取值任意
const int64_t kFirstMsgid = 900001;

// 会话类型：单聊 / 群聊
const int kCtypeSingle = 1;
const int kCtypeGroup  = 2;

// ARP6：同一个人连发的条数
const int kBurstCount = 3;

// 用户自定义的两段回复文字
const wchar_t* const kAwayCustom = L"我在开会，一小时后回复。";
const wchar_t* const kBusyCustom = L"正在处理紧急事务，稍后联系。";

// 两个开关都打开、两段文字都自定义的设置
AutoReplySettings MakeBothOn()
{
    AutoReplySettings s;
    s.m_awayEnabled = true;
    s.m_awayText = kAwayCustom;
    s.m_busyEnabled = true;
    s.m_busyText = kBusyCustom;
    return s;
}

// 同事在单聊里发来的一条普通文字消息
AutoReplyIncoming MakeSingleText(int64_t from, int64_t msgid)
{
    AutoReplyIncoming m;
    m.m_ctype = kCtypeSingle;
    m.m_from = from;
    m.m_msgid = msgid;
    m.m_msgtype = message_type_text;
    return m;
}

} // anonymous namespace

TEST_CASE("ARP1 离开且离开开关打开时回复单聊消息，用离开那段文字", "[autoreply]")
{
    CAutoReplyPolicy policy;
    std::wstring text;
    CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(),
                        MakeSingleText(kPeerUid, kFirstMsgid), text) == kAutoReplySend);
    CHECK(text == kAwayCustom);

    //用户从来没有改过文字时，用的是离开的默认文字
    AutoReplySettings defaults;
    defaults.m_awayEnabled = true;
    CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, defaults,
                        MakeSingleText(kPeerUid, kFirstMsgid + 1), text) == kAutoReplySend);
    CHECK(text == kDefaultAwayReplyText);
}

TEST_CASE("ARP2 忙碌且忙碌开关打开时用忙碌那段文字", "[autoreply]")
{
    CAutoReplyPolicy policy;
    std::wstring text;
    CHECK(policy.Decide(presence_busy, kConnUiOnline, kSelfUid, MakeBothOn(),
                        MakeSingleText(kPeerUid, kFirstMsgid), text) == kAutoReplySend);
    CHECK(text == kBusyCustom);

    AutoReplySettings defaults;
    defaults.m_busyEnabled = true;
    CHECK(policy.Decide(presence_busy, kConnUiOnline, kSelfUid, defaults,
                        MakeSingleText(kPeerUid, kFirstMsgid + 1), text) == kAutoReplySend);
    CHECK(text == kDefaultBusyReplyText);
}

TEST_CASE("ARP3 两个开关互不影响；在线、离线、正在连接都不回复", "[autoreply]")
{
    std::wstring text;

    SECTION("只开离开：忙碌时不回复")
    {
        CAutoReplyPolicy policy;
        AutoReplySettings s = MakeBothOn();
        s.m_busyEnabled = false;
        CHECK(policy.Decide(presence_busy, kConnUiOnline, kSelfUid, s,
                            MakeSingleText(kPeerUid, kFirstMsgid), text) == kAutoReplySkipDisabled);
        CHECK(text.empty());
        CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, s,
                            MakeSingleText(kPeerUid, kFirstMsgid + 1), text) == kAutoReplySend);
    }

    SECTION("只开忙碌：离开时不回复")
    {
        CAutoReplyPolicy policy;
        AutoReplySettings s = MakeBothOn();
        s.m_awayEnabled = false;
        CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, s,
                            MakeSingleText(kPeerUid, kFirstMsgid), text) == kAutoReplySkipDisabled);
        CHECK(policy.Decide(presence_busy, kConnUiOnline, kSelfUid, s,
                            MakeSingleText(kPeerUid, kFirstMsgid + 1), text) == kAutoReplySend);
    }

    SECTION("在线、离线：不回复，两个开关都开着也一样")
    {
        CAutoReplyPolicy policy;
        CHECK(policy.Decide(presence_online, kConnUiOnline, kSelfUid, MakeBothOn(),
                            MakeSingleText(kPeerUid, kFirstMsgid), text) == kAutoReplySkipPresence);
        CHECK(policy.Decide(presence_offline, kConnUiOffline, kSelfUid, MakeBothOn(),
                            MakeSingleText(kPeerUid, kFirstMsgid), text) == kAutoReplySkipPresence);
    }

    SECTION("状态是离开但连接没有就绪：不回复")
    {
        CAutoReplyPolicy policy;
        CHECK(policy.Decide(presence_away, kConnUiConnecting, kSelfUid, MakeBothOn(),
                            MakeSingleText(kPeerUid, kFirstMsgid), text) == kAutoReplySkipNotConnected);
        CHECK(policy.Decide(presence_busy, kConnUiOffline, kSelfUid, MakeBothOn(),
                            MakeSingleText(kPeerUid, kFirstMsgid), text) == kAutoReplySkipNotConnected);
        //连上之后同一条消息照常回复：不回复的那几次没有把它记成「已回复」
        CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(),
                            MakeSingleText(kPeerUid, kFirstMsgid), text) == kAutoReplySend);
    }
}

TEST_CASE("ARP4 自己发出的、服务端生成的记录、对方的自动回复都不触发", "[autoreply]")
{
    CAutoReplyPolicy policy;
    std::wstring text;
    int64_t msgid = kFirstMsgid;

    //自己在别处发出、同步回来的消息，以及本人转发后回填的那一份
    CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(),
                        MakeSingleText(kSelfUid, msgid++), text) == kAutoReplySkipFromSelf);

    //系统消息、通话记录、文件夹传输记录
    const int serverTypes[] = { message_type_system, message_type_rtc, message_type_folder };
    for (size_t i = 0; i < sizeof(serverTypes) / sizeof(serverTypes[0]); ++i)
    {
        INFO("msgtype = " << serverTypes[i]);
        AutoReplyIncoming m = MakeSingleText(kPeerUid, msgid++);
        m.m_msgtype = serverTypes[i];
        CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(), m, text)
              == kAutoReplySkipMsgType);
    }

    //对方的自动回复：两台都开着自动回复的客户端不会互相无限回复
    AutoReplyIncoming peerAuto = MakeSingleText(kPeerUid, msgid++);
    peerAuto.m_isAutoReply = true;
    CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(), peerAuto, text)
          == kAutoReplySkipPeerAuto);
    CHECK(text.empty());

    //图片、文件、窗口抖动这类对方主动发来的内容照常回复
    const int userTypes[] = { message_type_image, message_type_file, message_type_shake };
    for (size_t i = 0; i < sizeof(userTypes) / sizeof(userTypes[0]); ++i)
    {
        INFO("msgtype = " << userTypes[i]);
        AutoReplyIncoming m = MakeSingleText(kPeerUid, msgid++);
        m.m_msgtype = userTypes[i];
        CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(), m, text)
              == kAutoReplySend);
    }
}

TEST_CASE("ARP5 群聊一律不触发，包括群里 @我", "[autoreply]")
{
    CAutoReplyPolicy policy;
    std::wstring text;
    //群消息的 @ 信息不在判定输入里：不论有没有 @我，群聊都按会话类型直接排除
    AutoReplyIncoming m = MakeSingleText(kPeerUid, kFirstMsgid);
    m.m_ctype = kCtypeGroup;
    CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(), m, text)
          == kAutoReplySkipNotSingle);
    CHECK(policy.Decide(presence_busy, kConnUiOnline, kSelfUid, MakeBothOn(), m, text)
          == kAutoReplySkipNotSingle);
    CHECK(text.empty());
}

TEST_CASE("ARP6 同一个人发几条回几条；同一条消息重复到达只回一次", "[autoreply]")
{
    CAutoReplyPolicy policy;
    std::wstring text;

    for (int i = 0; i < kBurstCount; ++i)
    {
        INFO("第 " << (i + 1) << " 条");
        CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(),
                            MakeSingleText(kPeerUid, kFirstMsgid + i), text) == kAutoReplySend);
    }

    //同一个 msgid 再到达一次（服务端重推、本端重复处理）：不再回复
    CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(),
                        MakeSingleText(kPeerUid, kFirstMsgid), text) == kAutoReplySkipDuplicate);
    CHECK(text.empty());

    //另一位同事不受影响
    CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(),
                        MakeSingleText(kOtherUid, kFirstMsgid + kBurstCount), text) == kAutoReplySend);

    //没有服务器 msgid 的消息无法判断是否重复，不回复
    CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(),
                        MakeSingleText(kPeerUid, 0), text) == kAutoReplySkipNoMsgid);

    //登出后清空记录：同一个 msgid 又可以回复（换账号时不应受上一个账号影响）
    policy.Reset();
    CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(),
                        MakeSingleText(kPeerUid, kFirstMsgid), text) == kAutoReplySend);
}

TEST_CASE("ARP7 正看着与对方的聊天窗时不回复", "[autoreply]")
{
    CAutoReplyPolicy policy;
    std::wstring text;
    AutoReplyIncoming m = MakeSingleText(kPeerUid, kFirstMsgid);
    m.m_watching = true;
    CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(), m, text)
          == kAutoReplySkipWatching);

    //离开窗口后同一条消息并没有被记成「已回复」；下一条照常回复
    m.m_watching = false;
    m.m_msgid = kFirstMsgid + 1;
    CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, MakeBothOn(), m, text)
          == kAutoReplySend);
}

TEST_CASE("ARP8 文字为空或只有空白时改用默认文字，超长时截断", "[autoreply]")
{
    //空串、半角空格、换行与制表、全角空格都按空白处理
    const std::wstring blanks[] = { L"", L"   ", L"\r\n\t", L"\x3000\x3000" };
    for (size_t i = 0; i < sizeof(blanks) / sizeof(blanks[0]); ++i)
    {
        INFO("空白样本 index = " << i);
        CHECK(NormalizeAutoReplyText(blanks[i], kDefaultAwayReplyText) == kDefaultAwayReplyText);
        CHECK(NormalizeAutoReplyText(blanks[i], kDefaultBusyReplyText) == kDefaultBusyReplyText);
    }

    //判定时同样生效：离开文字设成空白，发出去的是离开的默认文字
    CAutoReplyPolicy policy;
    std::wstring text;
    AutoReplySettings s = MakeBothOn();
    s.m_awayText = L"  ";
    CHECK(policy.Decide(presence_away, kConnUiOnline, kSelfUid, s,
                        MakeSingleText(kPeerUid, kFirstMsgid), text) == kAutoReplySend);
    CHECK(text == kDefaultAwayReplyText);

    //首尾空白去掉，中间的保留
    CHECK(NormalizeAutoReplyText(L"  稍后 回复  ", kDefaultAwayReplyText) == L"稍后 回复");

    //超过上限截到 kAutoReplyMaxChars 个字符
    const std::wstring longText(kAutoReplyMaxChars + 20, L'字');
    CHECK(ClampAutoReplyText(longText).size() == kAutoReplyMaxChars);
    CHECK(NormalizeAutoReplyText(longText, kDefaultAwayReplyText).size() == kAutoReplyMaxChars);

    //截断点落在表情（代理对）中间时往前退一个字符，不留下半个表情
    std::wstring withEmoji(kAutoReplyMaxChars - 1, L'a');
    withEmoji += L"\xD83D\xDE00";   //一个由两个 UTF-16 字符组成的笑脸表情，跨过上限
    const std::wstring clamped = ClampAutoReplyText(withEmoji);
    CHECK(clamped.size() == kAutoReplyMaxChars - 1);
    CHECK(clamped == std::wstring(kAutoReplyMaxChars - 1, L'a'));

    //正好到上限、末尾是完整表情的不截
    std::wstring exact(kAutoReplyMaxChars - 2, L'a');
    exact += L"\xD83D\xDE00";
    CHECK(ClampAutoReplyText(exact) == exact);
}
