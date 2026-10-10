/**
 *  系统提示结构化字段（多语言第 5 期）的端到端联调用例（Suite STL，标签 [.live][systemtip-live]），SystemTipLiveTests.cpp
 *
 *  用 LiveChatClient 直接收发 v2 帧，走服务端发布群公告、转让群主、退群、邀请入群（入群欢迎语）、参与接龙的真实路径，
 *  核对收到的系统提示带 tipkind / tipargs，客户端按本地模板生成的正文与服务端拼的中文 text 逐字相同，会话列表的摘要
 *  也由客户端在本地生成；配了语言包时再按英文生成一次，确认换成了英文。
 *
 *  默认不参与常规测试：用例带 [.live] 标签，Catch2 会跳过。会改动群的状态（群主转出再转回、成员退群再入群），
 *  所以只对隔离环境跑：先运行 tmp/i18n_phase5/setup_livesrv.py 复制一份联调库（并开启测试群的入群欢迎语、解除禁言），
 *  再用 tmp/i18n_phase5/start_livesrv.ps1 起服务端（端口 29600），然后：
 *
 *      FlamingoUnit.exe "[systemtip-live]"
 *
 *  四条用例按声明顺序执行，STL3 依赖 STL2 把群主转回原群主。英文检查需要一份带第 5 期词条的 Win32 语言包，
 *  由环境变量 FLAMINGO_I18N_LIVE_LANG_DLL 给出完整路径；不设置时只做简体中文的核对。
 *
 *  账号：MY0127（5127，群 268435463 的群主）与 MY0308（5308，普通成员），密码都是 perf1234。
 *
 *  STL5 ~ STL7（2026-10-04，docs/邀请入群方案.md 第 1 期）核对入群提示 member_join 的三条入群路径：群主直接邀请、
 *  成员申请经审批、普通成员邀请转审批。它们另带标签 [gi-live]，STL7 还要用到第三个账号 MY0010（5010，普通成员）。
 *  需要第 1 期之后的 chatserver，环境由 tmp/gi_live/setup_gi_live.py 准备（同样监听 29600，与上面的环境不能同时运行），
 *  然后：FlamingoUnit.exe "[gi-live]"
 *
 *  STL10 ~ STL13（2026-10-04，docs/邀请入群方案.md 第 3 期 3e）核对入群邀请卡片：开关打开后群主邀请、普通成员邀请转审批
 *  再拒绝 / 通过、过期与失效、开关关闭后旧卡片仍可入群。它们同样带 [gi-live]，需要第 3 期之后的 chatserver；经后台接口
 *  （HTTP 29645，平台管理员 li@flamingo.com，密码由 setup_gi_live.py 在库副本里设成 gitest1234）切换开关，STL12 还直接
 *  改隔离库 tmp/gi_live/livesrv/bin/data/flamingo.db 里卡片的到期时间。
 *
 *  STL14 ~ STL16（2026-10-04，docs/邀请入群方案.md 第 4 期）核对审批只生效一次与「申请已处理」推送（1329）、主动退群不再插
 *  系统提示；同样带 [gi-live]，STL14、STL16 在用例里临时把 MY0010 设为管理员、结束时撤销。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <time.h>
#include "LiveChatClient.h"
#include "../net/MessageProtocolClient.h"
#include "../net/ConversationsProtocolClient.h"
#include "../net/SystemTipText.h"
#include "../net/GroupProtocolClient.h"   // STL10 ~ STL13：1327 / 1328 的构造与解析
#include "sqlite3.h"                    // STL12：改隔离库里卡片的到期时间
#include "../net/Protocol.h"
#include "../Common/I18n/I18n.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace livetest;

namespace {

//tmp/i18n_phase5/livesrv 隔离服务端的 chatserver 端口
const unsigned short kLiveSrvPort = 29600;

//两个测试账号与密码
const char* kOwnerAccount  = "MY0127";
const char* kMemberAccount = "MY0308";
const char* kPassword      = "perf1234";
const int64_t kOwnerId  = 5127;
const int64_t kMemberId = 5308;

//A 任群主、B 是普通成员的测试群
const int64_t kGroupId = 268435463;

//等一条应答或推送的最长时间，单位毫秒
const int kWaitMs = 5000;

//等某一类系统提示时最多看几条消息推送：中间可能夹着别的系统提示或卡片
const int kMaxNotifies = 20;

//英文检查时模拟的系统界面语言（简体中文），只影响「跟随系统」时的选择，这里显式指定了 en-US
const unsigned short kSystemLangZhCn = 0x0804;

//指定英文检查所用语言包的环境变量名，以及读取它的缓冲区长度（字符数）
const wchar_t* const kLangDllEnvName = L"FLAMINGO_I18N_LIVE_LANG_DLL";
const DWORD kEnvBufChars = 1024;

bool parseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

std::string toStr(int64_t v)
{
    char buf[32] = { 0 };
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%lld", (long long)v);
    return std::string(buf);
}

//登录一个测试账号；失败时让用例停下并带上原因
void login(LiveChatClient& c, const char* account)
{
    std::string reason;
    bool ok = c.ConnectAndLogin(kLiveSrvPort, kTestServerPubKey, account, kPassword, reason);
    INFO("login " << account << " failed: " << reason);
    REQUIRE(ok);
}

//发一条请求并等它的应答，返回解析后的 JSON
Json::Value request(LiveChatClient& c, uint16_t cmd, const std::string& body)
{
    REQUIRE(c.Send(cmd, body));
    std::string resp;
    REQUIRE(c.WaitFor(cmd, resp, kWaitMs));
    Json::Value root;
    REQUIRE(parseJson(resp, root));
    return root;
}

/**
 *  等一条 tipkind 为 kind 的系统提示推送。
 *  @param c          收推送的连接
 *  @param kind       要等的提示类型
 *  @param notify     [出参] 客户端解析出的消息（正文已按本地模板生成）
 *  @param serverText [出参] 服务端 content 里的中文 text
 *  @return 等到返回 true
 */
bool waitSystemTip(LiveChatClient& c, const char* kind, net::MessageNotify& notify, std::string& serverText)
{
    for (int i = 0; i < kMaxNotifies; ++i)
    {
        std::string body;
        if (!c.WaitFor(cmd_message_notify, body, kWaitMs))
            return false;
        net::MessageNotify n;
        if (!net::MessageProtocolClient::parseMessageNotify(body, n))
            continue;
        if (n.m_msgtype != message_type_system || n.m_tipKind != kind)
            continue;
        Json::Value root;
        if (!parseJson(body, root))
            continue;
        serverText = root["content"]["text"].asString();
        notify = n;
        return true;
    }
    return false;
}

//英文检查所用的语言包路径；环境变量没设置时返回空串
/**
 *  等一条「某成员主动退出测试群」的成员变动推送（1317，changetype 为退出）。2026-10-04 起退群不再插系统提示
 *  （docs/邀请入群方案.md 第 4 期），用例改等这条推送确认退群已生效；期间收到的其它成员变动推送丢弃。
 *  @param c      收推送的连接
 *  @param userId 退群者的 userid
 *  @return 等到返回 true
 */
bool waitMemberLeft(LiveChatClient& c, int64_t userId)
{
    for (int i = 0; i < kMaxNotifies; ++i)
    {
        std::string body;
        if (!c.WaitFor(cmd_group_member_changed_notify, body, kWaitMs))
            return false;
        net::GroupMemberChangedNotify n;
        if (!net::GroupProtocolClient::parseGroupMemberChangedNotify(body, n))
            continue;
        if (n.m_groupid != kGroupId || n.m_changetype != group_member_change_leave)
            continue;
        for (size_t k = 0; k < n.m_userids.size(); ++k)
        {
            if (n.m_userids[k] == userId)
                return true;
        }
    }
    return false;
}

std::wstring englishPackPath()
{
    wchar_t buf[kEnvBufChars] = { 0 };
    const DWORD n = ::GetEnvironmentVariableW(kLangDllEnvName, buf, kEnvBufChars);
    if (n == 0 || n >= kEnvBufChars)
        return std::wstring();
    return std::wstring(buf);
}

/**
 *  核对一条结构化系统提示：简体中文下本地生成的正文与服务端 text 逐字相同；配了语言包时按英文重新生成，
 *  结果与中文不同且含有预期的英文片段。
 *  @param n               客户端解析出的消息
 *  @param serverText      服务端的中文 text
 *  @param englishFragment 英文译文里一定会出现的片段；传 NULL 表示该提示的英文与中文相同（如入群欢迎语只有名字与欢迎词）
 */
void checkLocalized(const net::MessageNotify& n, const std::string& serverText, const char* englishFragment)
{
    CHECK_FALSE(serverText.empty());
    CHECK(n.m_text == serverText);

    const std::wstring dll = englishPackPath();
    if (dll.empty() || englishFragment == NULL)
        return;
    i18n::ResetForTest();
    const i18n::InitResult r = i18n::Init(L"en-US", dll, kSystemLangZhCn);
    REQUIRE(r.m_status == i18n::init_ok);
    std::string english;
    REQUIRE(net::FormatSystemTipJson(n.m_tipKind, n.m_tipArgs, english));
    i18n::ResetForTest();
    INFO("english: " << english);
    CHECK(english != serverText);
    CHECK(english.find(englishFragment) != std::string::npos);
}

//拉会话列表，返回测试群那一项（客户端解析后的结果）与原始 lastmsg
bool syncGroupConversation(LiveChatClient& c, net::ConversationItem& item, Json::Value& lastRaw)
{
    REQUIRE(c.Send(cmd_sync_conversations, "{}"));
    std::string resp;
    REQUIRE(c.WaitFor(cmd_sync_conversations, resp, kWaitMs));
    net::ConversationsResult out;
    REQUIRE(net::ConversationsProtocolClient::parseSyncConversationsResponse(resp, out));
    Json::Value root;
    REQUIRE(parseJson(resp, root));
    const Json::Value& arr = root["conversations"];
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (arr[i]["ctype"].asInt() == conversation_group && arr[i]["cid"].asString() == toStr(kGroupId))
            lastRaw = arr[i]["lastmsg"];
    }
    for (size_t i = 0; i < out.m_items.size(); ++i)
    {
        if (out.m_items[i].m_ctype == conversation_group && out.m_items[i].m_cid == kGroupId)
        {
            item = out.m_items[i];
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("STL1 发布群公告：成员收到带 tipkind 的提示，本地生成的正文与会话摘要都与服务端一致", "[.live][systemtip-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);

    const std::string title = "STL1 " + toStr((int64_t)time(NULL));
    Json::Value published = request(owner, cmd_publish_group_announcement,
        "{\"groupid\":\"" + toStr(kGroupId) + "\",\"title\":\"" + title + "\",\"content\":\"live check\"}");
    REQUIRE(published["code"].asInt() == 0);

    net::MessageNotify tip;
    std::string serverText;
    REQUIRE(waitSystemTip(member, kSystemTipAnnouncement, tip, serverText));
    checkLocalized(tip, serverText, "posted a group announcement");
    Json::Value args;
    REQUIRE(parseJson(tip.m_tipArgs, args));
    CHECK(args["title"].asString() == title);

    //会话列表：最后一条就是这条提示，lastmsg 带 tipkind，客户端在本地生成的摘要与服务端的 digest 相同
    net::ConversationItem item;
    Json::Value lastRaw;
    REQUIRE(syncGroupConversation(member, item, lastRaw));
    CHECK(lastRaw["tipkind"].asString() == kSystemTipAnnouncement);
    CHECK(item.m_digest == lastRaw["digest"].asString());
    CHECK(item.m_digest == serverText);
}

TEST_CASE("STL2 转让群主再转回：双方各收到一条转让提示", "[.live][systemtip-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);

    Json::Value out = request(owner, cmd_transfer_owner,
        "{\"groupid\":\"" + toStr(kGroupId) + "\",\"userid\":\"" + toStr(kMemberId) + "\"}");
    REQUIRE(out["code"].asInt() == 0);
    net::MessageNotify tip;
    std::string serverText;
    REQUIRE(waitSystemTip(member, kSystemTipOwnerTransfer, tip, serverText));
    checkLocalized(tip, serverText, "transferred group ownership");

    //转回原群主，环境恢复原样（STL3 依赖这一步）
    Json::Value back = request(member, cmd_transfer_owner,
        "{\"groupid\":\"" + toStr(kGroupId) + "\",\"userid\":\"" + toStr(kOwnerId) + "\"}");
    REQUIRE(back["code"].asInt() == 0);
    net::MessageNotify tipBack;
    std::string serverTextBack;
    REQUIRE(waitSystemTip(owner, kSystemTipOwnerTransfer, tipBack, serverTextBack));
    checkLocalized(tipBack, serverTextBack, "transferred group ownership");
}

TEST_CASE("STL3 成员退群后再被邀请入群：群主收到成员变动推送（不再有退群提示）与入群欢迎语", "[.live][systemtip-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);

    //2026-10-04 起退群不再插「「某人」退出了群聊」（docs/邀请入群方案.md 第 4 期），群主只收到成员变动推送
    Json::Value left = request(member, cmd_leave_group, "{\"groupid\":\"" + toStr(kGroupId) + "\"}");
    REQUIRE(left["code"].asInt() == 0);
    REQUIRE(waitMemberLeft(owner, kMemberId));

    //邀请回来：测试群开着入群欢迎语（setup_livesrv 之后在库里打开），服务端发入群欢迎语
    Json::Value invited = request(owner, cmd_invite_to_group,
        "{\"groupid\":\"" + toStr(kGroupId) + "\",\"userids\":[\"" + toStr(kMemberId) + "\"]}");
    REQUIRE(invited["code"].asInt() == 0);
    net::MessageNotify welcomeTip;
    std::string welcomeText;
    REQUIRE(waitSystemTip(owner, kSystemTipGroupWelcome, welcomeTip, welcomeText));
    //只有一位新成员：英文与中文相同（名字加群主写的欢迎词），不做英文检查
    checkLocalized(welcomeTip, welcomeText, NULL);
    Json::Value args;
    REQUIRE(parseJson(welcomeTip.m_tipArgs, args));
    REQUIRE(args["names"].isArray());
    CHECK(args["names"].size() == 1);
}

TEST_CASE("STL4 接龙：卡片的会话摘要带标题，参与后群主收到带 cardmsgid 的参与提示", "[.live][systemtip-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);

    const std::string title = "STL4 " + toStr((int64_t)time(NULL));
    const std::string clientMsgId = "stl4-" + toStr((int64_t)time(NULL));
    //服务端要求接龙必须带截止时间，且不早于当前时间 + 5 分钟（error_code_relay_deadline_invalid = 510）：取一天后
    const int64_t kMillisPerSecond = 1000;
    const int64_t kSecondsPerDay = 86400;   // 24 小时 × 3600 秒
    const int64_t kOneDayMs = kSecondsPerDay * kMillisPerSecond;
    const int64_t deadline = (int64_t)time(NULL) * kMillisPerSecond + kOneDayMs;
    Json::Value sent = request(owner, cmd_send_message,
        "{\"clientmsgid\":\"" + clientMsgId + "\",\"ctype\":2,\"cid\":\"" + toStr(kGroupId) + "\","
        "\"msgtype\":11,\"content\":{\"title\":\"" + title + "\",\"deadline\":" + toStr(deadline) + "},"
        "\"mentions\":[],\"mentionall\":false}");
    REQUIRE(sent["code"].asInt() == 0);
    const std::string cardMsgId = sent["msgid"].asString();
    REQUIRE_FALSE(cardMsgId.empty());

    //会话列表：最后一条是接龙卡片，lastmsg 带 digestarg，客户端生成「[接龙] 标题」
    net::ConversationItem item;
    Json::Value lastRaw;
    REQUIRE(syncGroupConversation(owner, item, lastRaw));
    CHECK(lastRaw["digestarg"].asString() == title);
    CHECK(item.m_digest == lastRaw["digest"].asString());

    Json::Value joined = request(member, cmd_join_relay,
        "{\"ctype\":2,\"cid\":\"" + toStr(kGroupId) + "\",\"msgid\":\"" + cardMsgId + "\",\"text\":\"STL4 join\"}");
    REQUIRE(joined["code"].asInt() == 0);
    net::MessageNotify tip;
    std::string serverText;
    REQUIRE(waitSystemTip(owner, kSystemTipRelayJoin, tip, serverText));
    CHECK(toStr(tip.m_cardMsgId) == cardMsgId);
    checkLocalized(tip, serverText, "joined the sign-up list");
}

//==================== 入群提示 member_join（docs/邀请入群方案.md 第 1 期，2026-10-04）====================
//STL5 ~ STL7 另带标签 [gi-live]，可单独运行：FlamingoUnit.exe "[gi-live]"。三条用例依赖测试群原有的两项设置
//（加群需审批、普通成员邀请需审批，即接龙联调库里的原值），按声明顺序执行，每条结束时 MY0308 都回到群里。

namespace {

//第三个账号：MY0010（5010），测试群的普通成员，STL7 由他发起邀请
const char* kThirdAccount = "MY0010";
const int64_t kThirdId = 5010;

//审批加群申请的动作：同意，与服务端 GROUP_JOIN_ACTION_ACCEPT 一致
const int kJoinActionAccept = 1;

/**
 *  以某个账号的视角等一条入群提示：先把本人 userid 设成该账号再解析推送，结束时恢复为 0。
 *  @param c          收推送的连接
 *  @param selfId     这条连接登录的 userid
 *  @param notify     [出参] 客户端解析出的消息（正文已按本地模板生成）
 *  @param serverText [出参] 服务端 content 里的中文 text
 *  @param args       [出参] tipargs
 *  @return 等到并解析成功返回 true
 */
bool waitJoinTip(LiveChatClient& c, int64_t selfId, net::MessageNotify& notify, std::string& serverText,
                 Json::Value& args)
{
    net::SetSystemTipSelfUserId(selfId);
    const bool ok = waitSystemTip(c, kSystemTipMemberJoin, notify, serverText);
    net::SetSystemTipSelfUserId(0);
    if (!ok)
        return false;
    return parseJson(notify.m_tipArgs, args);
}

//MY0308 退群，并等群主收到退群的成员变动推送，保证随后的入群发生在退群之后（2026-10-04 起退群不再插提示）
void memberLeaves(LiveChatClient& owner, LiveChatClient& member)
{
    Json::Value left = request(member, cmd_leave_group, "{\"groupid\":\"" + toStr(kGroupId) + "\"}");
    REQUIRE(left["code"].asInt() == 0);
    REQUIRE(waitMemberLeft(owner, kMemberId));
}

//群主等一条 MY0308 的加群申请推送并同意它；expectRecommender 为申请里应带的推荐人（本人申请时为 0）
void ownerApprovesMember(LiveChatClient& owner, int64_t expectRecommender)
{
    std::string body;
    REQUIRE(owner.WaitFor(cmd_join_group_request_notify, body, kWaitMs));
    Json::Value req;
    REQUIRE(parseJson(body, req));
    CHECK(req["fromuserid"].asString() == toStr(kMemberId));
    CHECK(req["recommender"].asString() == toStr(expectRecommender));
    Json::Value handled = request(owner, cmd_handle_join_group,
        "{\"requestid\":\"" + req["requestid"].asString() + "\",\"action\":" + toStr(kJoinActionAccept) + "}");
    REQUIRE(handled["code"].asInt() == 0);
}

} // namespace

TEST_CASE("STL5 群主邀请成员：双方收到入群提示并各自看到「你」，新成员的群未读为 0", "[.live][systemtip-live][gi-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);
    memberLeaves(owner, member);

    Json::Value invited = request(owner, cmd_invite_to_group,
        "{\"groupid\":\"" + toStr(kGroupId) + "\",\"userids\":[\"" + toStr(kMemberId) + "\"]}");
    REQUIRE(invited["code"].asInt() == 0);

    //群主一侧：tipargs 带邀请人与新成员，本地正文写「你邀请」
    net::MessageNotify ownerTip;
    std::string serverText;
    Json::Value args;
    REQUIRE(waitJoinTip(owner, kOwnerId, ownerTip, serverText, args));
    CHECK(args["inviterid"].asString() == toStr(kOwnerId));
    REQUIRE(args["members"].size() == 1);
    CHECK(args["members"][0]["userid"].asString() == toStr(kMemberId));
    const std::string inviterNick = args["inviter"].asString();
    const std::string memberNick = args["members"][0]["nickname"].asString();
    REQUIRE_FALSE(inviterNick.empty());
    REQUIRE_FALSE(memberNick.empty());
    CHECK(serverText == u8"「" + inviterNick + u8"」邀请「" + memberNick + u8"」加入了群聊");
    CHECK(ownerTip.m_text == u8"你邀请「" + memberNick + u8"」加入了群聊");

    //不设本人 userid 时，本地生成的正文与服务端 text 逐字相同
    std::string plain;
    REQUIRE(net::FormatSystemTipJson(ownerTip.m_tipKind, ownerTip.m_tipArgs, plain));
    CHECK(plain == serverText);

    //新成员一侧：收到同一条提示，写「邀请你」
    net::MessageNotify memberTip;
    std::string memberServerText;
    Json::Value memberArgs;
    REQUIRE(waitJoinTip(member, kMemberId, memberTip, memberServerText, memberArgs));
    CHECK(memberTip.m_msgid == ownerTip.m_msgid);
    CHECK(memberTip.m_text == u8"「" + inviterNick + u8"」邀请你加入了群聊");

    //新成员的会话列表：入群前的消息不算未读（docs/邀请入群方案.md §2.5）；最后一条是入群提示时摘要写「邀请你」
    net::SetSystemTipSelfUserId(kMemberId);
    net::ConversationItem item;
    Json::Value lastRaw;
    const bool found = syncGroupConversation(member, item, lastRaw);
    net::SetSystemTipSelfUserId(0);
    REQUIRE(found);
    CHECK(item.m_unread == 0);
    if (lastRaw["tipkind"].asString() == kSystemTipMemberJoin)
        CHECK(item.m_digest == u8"「" + inviterNick + u8"」邀请你加入了群聊");
}

TEST_CASE("STL6 成员申请入群、群主审批通过：入群提示没有邀请人", "[.live][systemtip-live][gi-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);
    memberLeaves(owner, member);

    Json::Value applied = request(member, cmd_join_group_request,
        "{\"groupid\":\"" + toStr(kGroupId) + "\",\"verifymsg\":\"STL6\"}");
    REQUIRE(applied["code"].asInt() == 0);
    ownerApprovesMember(owner, 0);

    //群主一侧：没有 inviter / inviterid，正文写「「某某」加入了群聊」，与服务端 text 相同
    net::MessageNotify ownerTip;
    std::string serverText;
    Json::Value args;
    REQUIRE(waitJoinTip(owner, kOwnerId, ownerTip, serverText, args));
    CHECK_FALSE(args.isMember("inviterid"));
    CHECK_FALSE(args.isMember("inviter"));
    REQUIRE(args["members"].size() == 1);
    const std::string memberNick = args["members"][0]["nickname"].asString();
    CHECK(serverText == u8"「" + memberNick + u8"」加入了群聊");
    CHECK(ownerTip.m_text == serverText);

    //新成员一侧：「你加入了群聊」
    net::MessageNotify memberTip;
    std::string memberServerText;
    Json::Value memberArgs;
    REQUIRE(waitJoinTip(member, kMemberId, memberTip, memberServerText, memberArgs));
    CHECK(memberTip.m_text == u8"你加入了群聊");
}

TEST_CASE("STL7 普通成员邀请转为审批、群主通过：入群提示里的邀请人是那位普通成员", "[.live][systemtip-live][gi-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    LiveChatClient third;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);
    login(third, kThirdAccount);
    memberLeaves(owner, member);

    //普通成员邀请：群设为普通成员邀请需审批，服务端把它转成推荐人为 MY0010 的加群申请
    Json::Value invited = request(third, cmd_invite_to_group,
        "{\"groupid\":\"" + toStr(kGroupId) + "\",\"userids\":[\"" + toStr(kMemberId) + "\"]}");
    REQUIRE(invited["code"].asInt() == 0);
    ownerApprovesMember(owner, kThirdId);

    //群主一侧：邀请人是 MY0010，正文与服务端 text 相同
    net::MessageNotify ownerTip;
    std::string serverText;
    Json::Value args;
    REQUIRE(waitJoinTip(owner, kOwnerId, ownerTip, serverText, args));
    CHECK(args["inviterid"].asString() == toStr(kThirdId));
    const std::string inviterNick = args["inviter"].asString();
    const std::string memberNick = args["members"][0]["nickname"].asString();
    CHECK(serverText == u8"「" + inviterNick + u8"」邀请「" + memberNick + u8"」加入了群聊");
    CHECK(ownerTip.m_text == serverText);

    //邀请人一侧：「你邀请…」
    net::MessageNotify thirdTip;
    std::string thirdServerText;
    Json::Value thirdArgs;
    REQUIRE(waitJoinTip(third, kThirdId, thirdTip, thirdServerText, thirdArgs));
    CHECK(thirdTip.m_text == u8"你邀请「" + memberNick + u8"」加入了群聊");

    //新成员一侧：「…邀请你…」
    net::MessageNotify memberTip;
    std::string memberServerText;
    Json::Value memberArgs;
    REQUIRE(waitJoinTip(member, kMemberId, memberTip, memberServerText, memberArgs));
    CHECK(memberTip.m_text == u8"「" + inviterNick + u8"」邀请你加入了群聊");
}

//==================== 入群邀请卡片（docs/邀请入群方案.md 第 3 期 3e，2026-10-04）====================
//STL10 ~ STL13 同样带标签 [gi-live]，按声明顺序执行。它们经后台接口打开 / 关闭 group.inviteconfirm，每条用例开头
//把开关设成自己要的值，结束时关掉开关、让 MY0308 回到群里，STL5 ~ STL7 重跑时不受影响。后台登录用平台管理员
//li@flamingo.com，密码由 tmp/gi_live/setup_gi_live.py 在库副本里设好；STL12 直接改隔离库里那张卡片的到期时间。

namespace {

//隔离服务端的 HTTP 短连接端口（后台接口），与 setup_gi_live.py 的端口映射一致
const unsigned short kLiveHttpPort = 29645;

//平台管理员账号（只在库副本里改了密码）
const char* kAdminEmail = "li@flamingo.com";
const char* kAdminPassword = "gitest1234";

//三个测试账号所属的企业
const int64_t kEnterpriseId = 1;

//「邀请入群需被邀请人同意」的设置项名
const char* kInviteConfirmKey = "group.inviteconfirm";

//审批加群申请的动作：拒绝（服务端把 1 以外的取值都当作拒绝）
const int kJoinActionReject = 2;

//后台接口成功时的 code
const int kAdminCodeOk = 0;

//隔离服务端的数据库：STL12 直接改卡片的到期时间
const wchar_t* const kLiveDbPath = L"E:\\flamingo\\tmp\\gi_live\\livesrv\\bin\\data\\flamingo.db";

//STL12 把到期时间改成的值（Unix 毫秒）：远早于当前时间
const long long kExpiredTimeMs = 1;

//SQLite 遇到服务端正在写库时的等待上限，单位毫秒
const int kDbBusyTimeoutMs = 3000;

//确认「这段时间里没有收到某类推送」时等待的时长，单位毫秒
const int kQuietWaitMs = 1500;

//等一条某状态的卡片变化推送时最多看几条：前面可能夹着别的状态
const int kMaxInviteChanges = 5;

//HTTP 报文头与包体的分隔，以及收应答用的缓冲区大小（字节）
const char* const kHttpHeaderEnd = "\r\n\r\n";
const size_t kHttpHeaderEndLen = 4;
const int kHttpRecvBufSize = 4096;

//JSON 值 → 紧凑文本
std::string compactJson(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//向隔离服务端的 HTTP 端口发一条 JSON POST，读回应答包体（后台接口是短连接，读到对端关闭为止）
bool adminPost(const std::string& path, const Json::Value& body, Json::Value& out)
{
    EnsureWinsockReady();
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET)
        return false;
    DWORD timeout = (DWORD)kWaitMs;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(kLiveHttpPort);
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    if (::connect(s, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        ::closesocket(s);
        return false;
    }
    const std::string json = compactJson(body);
    const std::string req = "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
        "Content-Length: " + std::to_string(json.size()) + "\r\nConnection: close\r\n\r\n" + json;
    size_t sent = 0;
    while (sent < req.size())
    {
        const int n = ::send(s, req.data() + sent, (int)(req.size() - sent), 0);
        if (n <= 0)
        {
            ::closesocket(s);
            return false;
        }
        sent += (size_t)n;
    }
    std::string resp;
    char buf[kHttpRecvBufSize];
    for (;;)
    {
        const int n = ::recv(s, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        resp.append(buf, (size_t)n);
    }
    ::closesocket(s);
    const size_t headerEnd = resp.find(kHttpHeaderEnd);
    if (headerEnd == std::string::npos)
        return false;
    return parseJson(resp.substr(headerEnd + kHttpHeaderEndLen), out);
}

//经后台接口打开或关闭本企业的「邀请入群需被邀请人同意」；失败时让用例停下
void setInviteConfirm(bool on)
{
    Json::Value login;
    login["email"] = kAdminEmail;
    login["password"] = kAdminPassword;
    Json::Value loginResp;
    REQUIRE(adminPost("/admin/login.do", login, loginResp));
    INFO("admin login response: " << compactJson(loginResp));
    REQUIRE(loginResp["code"].asInt() == kAdminCodeOk);

    Json::Value req;
    req["token"] = loginResp["token"].asString();
    req["enterpriseid"] = (Json::Int64)kEnterpriseId;
    req["key"] = kInviteConfirmKey;
    req["value"] = on ? "1" : "0";
    Json::Value resp;
    REQUIRE(adminPost("/admin/setting/set.do", req, resp));
    INFO("setting/set.do response: " << compactJson(resp));
    REQUIRE(resp["code"].asInt() == kAdminCodeOk);
}

/**
 *  等一张发给指定被邀请人的入群邀请卡片的消息推送（期间收到的其它消息推送、以及被邀请人不是 inviteeId 的卡片都丢弃）。
 *  邀请人自己也会收到所发卡片的一份，同一条连接上可能先排着这种卡片，所以要按被邀请人筛选。
 *  @param c         收推送的连接
 *  @param inviteeId 卡片的被邀请人 userid
 *  @param card      [出参] 客户端解析出的消息，content 原文在 m_groupInviteContent
 *  @param root      [出参] 卡片 content 解析后的 JSON
 *  @return 等到返回 true
 */
bool waitInviteCard(LiveChatClient& c, int64_t inviteeId, net::MessageNotify& card, Json::Value& root)
{
    for (int i = 0; i < kMaxNotifies; ++i)
    {
        std::string body;
        if (!c.WaitFor(cmd_message_notify, body, kWaitMs))
            return false;
        net::MessageNotify n;
        if (!net::MessageProtocolClient::parseMessageNotify(body, n) || n.m_msgtype != message_type_group_invite)
            continue;
        Json::Value parsed;
        if (!parseJson(n.m_groupInviteContent, parsed))
            return false;
        if (parsed["inviteeid"].asString() != toStr(inviteeId))
            continue;
        root = parsed;
        card = n;
        return true;
    }
    return false;
}

/**
 *  等一条指定状态的卡片状态变化推送（1328）。
 *  @param c      收推送的连接
 *  @param state  要等的状态
 *  @param notify [出参] 解析结果
 *  @return 等到返回 true
 */
bool waitInviteChanged(LiveChatClient& c, int state, net::GroupInviteChangedNotify& notify)
{
    for (int i = 0; i < kMaxInviteChanges; ++i)
    {
        std::string body;
        if (!c.WaitFor(cmd_group_invite_changed_notify, body, kWaitMs))
            return false;
        net::GroupInviteChangedNotify n;
        if (!net::GroupProtocolClient::parseGroupInviteChangedNotify(body, n) || n.m_state != state)
            continue;
        notify = n;
        return true;
    }
    return false;
}

//在 timeoutMs 内等一条 tipkind 为 kind 的系统提示推送，用法与 waitSystemTip 相同；用于确认「这段时间里没有收到」
bool waitSystemTipWithin(LiveChatClient& c, const char* kind, net::MessageNotify& notify, std::string& serverText,
                         int timeoutMs)
{
    const ULONGLONG deadline = ::GetTickCount64() + (ULONGLONG)timeoutMs;
    while (::GetTickCount64() < deadline)
    {
        const int left = (int)(deadline - ::GetTickCount64());
        std::string body;
        if (!c.WaitFor(cmd_message_notify, body, left > 0 ? left : 1))
            return false;
        net::MessageNotify n;
        if (!net::MessageProtocolClient::parseMessageNotify(body, n))
            continue;
        if (n.m_msgtype != message_type_system || n.m_tipKind != kind)
            continue;
        Json::Value root;
        if (!parseJson(body, root))
            continue;
        serverText = root["content"]["text"].asString();
        notify = n;
        return true;
    }
    return false;
}

//被邀请人点「加入群聊」：发 1327 并返回解析后的应答
net::AcceptGroupInviteResult acceptInvite(LiveChatClient& c, int64_t inviteId)
{
    REQUIRE(c.Send(cmd_accept_group_invite, net::GroupProtocolClient::buildAcceptGroupInviteRequest(inviteId)));
    std::string resp;
    REQUIRE(c.WaitFor(cmd_accept_group_invite, resp, kWaitMs));
    net::AcceptGroupInviteResult result;
    REQUIRE(net::GroupProtocolClient::parseAcceptGroupInviteResponse(resp, result));
    return result;
}

//某账号邀请 MY0308（开关开着时发卡片）；返回 1305 应答
Json::Value inviteMember(LiveChatClient& inviter)
{
    Json::Value invited = request(inviter, cmd_invite_to_group,
        "{\"groupid\":\"" + toStr(kGroupId) + "\",\"userids\":[\"" + toStr(kMemberId) + "\"]}");
    REQUIRE(invited["code"].asInt() == 0);
    return invited;
}

//群主等一条 MY0308 的加群申请推送并拒绝它；expectRecommender 为申请里应带的推荐人
void ownerRejectsMember(LiveChatClient& owner, int64_t expectRecommender)
{
    std::string body;
    REQUIRE(owner.WaitFor(cmd_join_group_request_notify, body, kWaitMs));
    Json::Value req;
    REQUIRE(parseJson(body, req));
    CHECK(req["recommender"].asString() == toStr(expectRecommender));
    Json::Value handled = request(owner, cmd_handle_join_group,
        "{\"requestid\":\"" + req["requestid"].asString() + "\",\"action\":" + toStr(kJoinActionReject) + "}");
    REQUIRE(handled["code"].asInt() == 0);
}

//把隔离库里一张卡片的到期时间改到过去（服务端每次都从库里读邀请记录，改完立即生效）
void expireInviteInDb(int64_t inviteId)
{
    sqlite3* db = NULL;
    REQUIRE(sqlite3_open16(kLiveDbPath, &db) == SQLITE_OK);
    sqlite3_busy_timeout(db, kDbBusyTimeoutMs);
    const std::string sql = "UPDATE t_group_invite SET f_expire_time = " + toStr(kExpiredTimeMs)
                          + " WHERE f_id = " + toStr(inviteId);
    char* err = NULL;
    const int rc = sqlite3_exec(db, sql.c_str(), NULL, NULL, &err);
    INFO("sqlite error: " << (err != NULL ? err : ""));
    sqlite3_free(err);
    const int changed = sqlite3_changes(db);
    sqlite3_close(db);
    REQUIRE(rc == SQLITE_OK);
    REQUIRE(changed == 1);
}

//卡片 content 里的邀请 id
int64_t inviteIdOf(const Json::Value& card)
{
    return (int64_t)_strtoi64(card["inviteid"].asString().c_str(), NULL, 10);
}

//MY0308 收卡片、点加入、直接入群（群主或管理员发的邀请），并等双方收到「已加入」；用于各用例末尾把 MY0308 放回群里
void memberAcceptsOwnerInvite(LiveChatClient& owner, LiveChatClient& member)
{
    inviteMember(owner);
    net::MessageNotify card;
    Json::Value root;
    REQUIRE(waitInviteCard(member, kMemberId, card, root));
    const net::AcceptGroupInviteResult r = acceptInvite(member, inviteIdOf(root));
    REQUIRE(r.m_code == 0);
    REQUIRE(r.m_joined);
    net::GroupInviteChangedNotify changed;
    REQUIRE(waitInviteChanged(member, group_invite_joined, changed));
}

} // namespace

TEST_CASE("STL10 开关打开后群主邀请：被邀请人在单聊里收到卡片，点加入后入群提示出现、双方卡片变为已加入", "[.live][gi-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);
    setInviteConfirm(true);
    memberLeaves(owner, member);

    //邀请：应答里一人发了卡片、没有人直接入群
    const Json::Value invited = inviteMember(owner);
    CHECK(invited["invited"].asInt() == 1);
    CHECK(invited["added"].asInt() == 0);

    //被邀请人：单聊卡片，发送者为群主，待加入，按钮在被邀请人一侧
    net::MessageNotify card;
    Json::Value root;
    REQUIRE(waitInviteCard(member, kMemberId, card, root));
    CHECK(card.m_from == kOwnerId);
    CHECK(card.m_ctype == conversation_single);
    CHECK(root["groupid"].asString() == toStr(kGroupId));
    CHECK(root["state"].asInt() == group_invite_pending);
    CHECK(root["inviteeid"].asString() == toStr(kMemberId));
    CHECK(net::MessageProtocolClient::digestForNotify(card) == u8"[入群邀请] " + root["groupname"].asString());
    const int64_t inviteId = inviteIdOf(root);
    REQUIRE(inviteId > 0);

    //邀请人一侧同样收到这张卡片（同一个 msgid）
    net::MessageNotify ownerCard;
    Json::Value ownerRoot;
    REQUIRE(waitInviteCard(owner, kMemberId, ownerCard, ownerRoot));
    CHECK(ownerCard.m_msgid == card.m_msgid);

    //还没点加入：群里没有入群提示
    net::MessageNotify early;
    std::string earlyText;
    CHECK_FALSE(waitSystemTipWithin(owner, kSystemTipMemberJoin, early, earlyText, kQuietWaitMs));

    //点加入：直接入群
    const net::AcceptGroupInviteResult r = acceptInvite(member, inviteId);
    CHECK(r.m_code == 0);
    CHECK(r.m_inviteid == inviteId);
    CHECK(r.m_groupid == kGroupId);
    CHECK(r.m_joined);

    //双方都收到 1328：已加入，按两个 userid 定位到彼此的单聊
    net::GroupInviteChangedNotify toMember;
    REQUIRE(waitInviteChanged(member, group_invite_joined, toMember));
    CHECK(toMember.m_msgid == card.m_msgid);
    CHECK(toMember.m_inviterid == kOwnerId);
    CHECK(toMember.m_inviteeid == kMemberId);
    net::GroupInviteChangedNotify toOwner;
    REQUIRE(waitInviteChanged(owner, group_invite_joined, toOwner));
    CHECK(toOwner.m_inviteid == inviteId);

    //入群提示：邀请人是群主
    net::MessageNotify ownerTip;
    std::string serverText;
    Json::Value args;
    REQUIRE(waitJoinTip(owner, kOwnerId, ownerTip, serverText, args));
    CHECK(args["inviterid"].asString() == toStr(kOwnerId));
    REQUIRE(args["members"].size() == 1);
    CHECK(args["members"][0]["userid"].asString() == toStr(kMemberId));

    //再点一次：已处理过的邀请回 314
    const net::AcceptGroupInviteResult again = acceptInvite(member, inviteId);
    CHECK(again.m_code == error_code_group_invite_invalid);
    CHECK(again.m_inviteid == inviteId);

    setInviteConfirm(false);
}

TEST_CASE("STL11 普通成员邀请、群需审批：点加入后转为审批，群主拒绝则未通过，通过则已加入", "[.live][gi-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    LiveChatClient third;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);
    login(third, kThirdAccount);
    setInviteConfirm(true);
    memberLeaves(owner, member);

    //第一轮：MY0010 邀请，MY0308 点加入 → 转为审批 → 群主拒绝
    inviteMember(third);
    net::MessageNotify card;
    Json::Value root;
    REQUIRE(waitInviteCard(member, kMemberId, card, root));
    CHECK(card.m_from == kThirdId);
    const int64_t firstId = inviteIdOf(root);
    const net::AcceptGroupInviteResult first = acceptInvite(member, firstId);
    CHECK(first.m_code == 0);
    CHECK_FALSE(first.m_joined);
    net::GroupInviteChangedNotify approving;
    REQUIRE(waitInviteChanged(member, group_invite_approving, approving));
    CHECK(approving.m_msgid == card.m_msgid);
    net::GroupInviteChangedNotify thirdApproving;
    REQUIRE(waitInviteChanged(third, group_invite_approving, thirdApproving));
    ownerRejectsMember(owner, kThirdId);
    net::GroupInviteChangedNotify rejected;
    REQUIRE(waitInviteChanged(member, group_invite_rejected, rejected));
    CHECK(rejected.m_inviteid == firstId);
    net::GroupInviteChangedNotify thirdRejected;
    REQUIRE(waitInviteChanged(third, group_invite_rejected, thirdRejected));

    //第二轮：再邀请一次，转为审批后群主通过 → 已加入，入群提示里的邀请人是 MY0010
    inviteMember(third);
    REQUIRE(waitInviteCard(member, kMemberId, card, root));
    const int64_t secondId = inviteIdOf(root);
    CHECK(secondId != firstId);
    const net::AcceptGroupInviteResult second = acceptInvite(member, secondId);
    CHECK(second.m_code == 0);
    CHECK_FALSE(second.m_joined);
    REQUIRE(waitInviteChanged(member, group_invite_approving, approving));
    ownerApprovesMember(owner, kThirdId);
    net::GroupInviteChangedNotify joined;
    REQUIRE(waitInviteChanged(member, group_invite_joined, joined));
    CHECK(joined.m_inviteid == secondId);
    net::GroupInviteChangedNotify thirdJoined;
    REQUIRE(waitInviteChanged(third, group_invite_joined, thirdJoined));
    net::MessageNotify ownerTip;
    std::string serverText;
    Json::Value args;
    REQUIRE(waitJoinTip(owner, kOwnerId, ownerTip, serverText, args));
    CHECK(args["inviterid"].asString() == toStr(kThirdId));

    setInviteConfirm(false);
}

TEST_CASE("STL12 过期与失效：到期时间已过点加入回 313，邀请人退群后点加入回 314", "[.live][gi-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    LiveChatClient third;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);
    login(third, kThirdAccount);
    setInviteConfirm(true);
    memberLeaves(owner, member);

    //过期：群主邀请，把到期时间改到过去，点加入回 313，不推 1328
    inviteMember(owner);
    net::MessageNotify card;
    Json::Value root;
    REQUIRE(waitInviteCard(member, kMemberId, card, root));
    const int64_t expiredId = inviteIdOf(root);
    expireInviteInDb(expiredId);
    const net::AcceptGroupInviteResult expired = acceptInvite(member, expiredId);
    CHECK(expired.m_code == error_code_group_invite_expired);
    CHECK(expired.m_inviteid == expiredId);
    std::string body;
    CHECK_FALSE(member.WaitFor(cmd_group_invite_changed_notify, body, kQuietWaitMs));

    //失效：MY0010 邀请后退群，点加入回 314
    inviteMember(third);
    REQUIRE(waitInviteCard(member, kMemberId, card, root));
    const int64_t invalidId = inviteIdOf(root);
    Json::Value left = request(third, cmd_leave_group, "{\"groupid\":\"" + toStr(kGroupId) + "\"}");
    REQUIRE(left["code"].asInt() == 0);
    const net::AcceptGroupInviteResult invalid = acceptInvite(member, invalidId);
    CHECK(invalid.m_code == error_code_group_invite_invalid);
    CHECK(invalid.m_inviteid == invalidId);

    //恢复：群主邀请 MY0010 与 MY0308 回群（群主的邀请点了直接入群）
    Json::Value reinvite = request(owner, cmd_invite_to_group,
        "{\"groupid\":\"" + toStr(kGroupId) + "\",\"userids\":[\"" + toStr(kThirdId) + "\"]}");
    REQUIRE(reinvite["code"].asInt() == 0);
    net::MessageNotify thirdCard;
    Json::Value thirdRoot;
    REQUIRE(waitInviteCard(third, kThirdId, thirdCard, thirdRoot));
    const net::AcceptGroupInviteResult thirdBack = acceptInvite(third, inviteIdOf(thirdRoot));
    REQUIRE(thirdBack.m_code == 0);
    REQUIRE(thirdBack.m_joined);
    memberAcceptsOwnerInvite(owner, member);

    setInviteConfirm(false);
}

TEST_CASE("STL13 开关关闭后，之前发出、仍在有效期内的卡片照样可以入群", "[.live][gi-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);
    setInviteConfirm(true);
    memberLeaves(owner, member);

    inviteMember(owner);
    net::MessageNotify card;
    Json::Value root;
    REQUIRE(waitInviteCard(member, kMemberId, card, root));
    const int64_t inviteId = inviteIdOf(root);

    //关闭开关后再点加入：照样直接入群
    setInviteConfirm(false);
    const net::AcceptGroupInviteResult r = acceptInvite(member, inviteId);
    CHECK(r.m_code == 0);
    CHECK(r.m_joined);
    net::GroupInviteChangedNotify joined;
    REQUIRE(waitInviteChanged(member, group_invite_joined, joined));
    CHECK(joined.m_inviteid == inviteId);

    //开关关闭时再邀请：不发卡片，直接入群（先让 MY0308 退群）
    memberLeaves(owner, member);
    const Json::Value direct = inviteMember(owner);
    CHECK(direct["added"].asInt() == 1);
    CHECK(direct["invited"].asInt() == 0);
}

//==================== 审批只生效一次、「申请已处理」推送、退群不插提示（docs/邀请入群方案.md 第 4 期，2026-10-04）====================
//STL14 ~ STL16 同样带标签 [gi-live]。STL14、STL16 在开头由群主经协议把 MY0010 设为管理员、结束时撤销（STL7 / STL11 / STL12
//把 MY0010 当普通成员用，不能在库里直接改成管理员）；三条结束时 MY0308 都在群里，开关保持关闭。

namespace {

//群成员角色：普通成员、管理员（与服务端 group_member_role 一致）
const int kRoleMember = 0;
const int kRoleAdmin  = 1;

//加群申请的处理结果：1 已通过 2 已拒绝（与 t_group_join_request.f_status、1329 的 status 一致）
const int kRequestAccepted = 1;
const int kRequestRejected = 2;

//审批已被处理过（含同时处理时没抢到的一方）时的错误码
const int kErrJoinRequestHandled = 308;

//群主把某个成员设为 / 撤销管理员。应答成功时角色已在服务端生效，之后的 1307 / 1329 按新角色下发
void setAdmin(LiveChatClient& owner, int64_t userId, bool admin)
{
    Json::Value set = request(owner, cmd_set_member_role,
        net::GroupProtocolClient::buildSetMemberRoleRequest(kGroupId, userId, admin ? kRoleAdmin : kRoleMember));
    REQUIRE(set["code"].asInt() == 0);
}

//等一条 MY0308 的加群申请推送（1307），返回申请 id
int64_t waitJoinRequest(LiveChatClient& c)
{
    std::string body;
    REQUIRE(c.WaitFor(cmd_join_group_request_notify, body, kWaitMs));
    Json::Value req;
    REQUIRE(parseJson(body, req));
    CHECK(req["fromuserid"].asString() == toStr(kMemberId));
    return (int64_t)_strtoi64(req["requestid"].asString().c_str(), NULL, 10);
}

//等一条指定申请的「已处理」推送（1329）
bool waitRequestHandled(LiveChatClient& c, int64_t requestId, net::JoinRequestHandledNotify& out)
{
    for (int i = 0; i < kMaxNotifies; ++i)
    {
        std::string body;
        if (!c.WaitFor(cmd_join_request_handled_notify, body, kWaitMs))
            return false;
        net::JoinRequestHandledNotify n;
        if (!net::GroupProtocolClient::parseJoinRequestHandledNotify(body, n) || n.m_requestid != requestId)
            continue;
        out = n;
        return true;
    }
    return false;
}

//在隔离库里执行一条只返回一个整数的查询，失败时让用例停下
int64_t queryLiveDbInt(const std::string& sql)
{
    sqlite3* db = NULL;
    REQUIRE(sqlite3_open16(kLiveDbPath, &db) == SQLITE_OK);
    sqlite3_busy_timeout(db, kDbBusyTimeoutMs);
    sqlite3_stmt* stmt = NULL;
    const int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
    int64_t value = -1;
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
        value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    INFO("sql: " << sql);
    REQUIRE(rc == SQLITE_OK);
    return value;
}

//测试群里的系统提示条数（msgtype 17）
int64_t countGroupSystemMessages()
{
    return queryLiveDbInt("SELECT COUNT(*) FROM t_chatmsg WHERE f_ctype = 2 AND f_to = " + toStr(kGroupId)
                          + " AND f_msgtype = " + toStr(message_type_system));
}

//MY0308 退群后自己申请入群（测试群为加群需审批），返回群主收到的申请 id；管理员 admin 不为空时也要收到同一条
int64_t memberAppliesAgain(LiveChatClient& owner, LiveChatClient& member, LiveChatClient* admin)
{
    memberLeaves(owner, member);
    Json::Value applied = request(member, cmd_join_group_request,
        "{\"groupid\":\"" + toStr(kGroupId) + "\",\"verifymsg\":\"STL\"}");
    REQUIRE(applied["code"].asInt() == 0);
    const int64_t requestId = waitJoinRequest(owner);
    REQUIRE(requestId > 0);
    if (admin != NULL)
        CHECK(waitJoinRequest(*admin) == requestId);
    return requestId;
}

//群主直接邀请 MY0308 回群（开关关闭时直接入群），用于用例末尾恢复
void ownerBringsMemberBack(LiveChatClient& owner)
{
    Json::Value invited = request(owner, cmd_invite_to_group,
        "{\"groupid\":\"" + toStr(kGroupId) + "\",\"userids\":[\"" + toStr(kMemberId) + "\"]}");
    REQUIRE(invited["code"].asInt() == 0);
    REQUIRE(invited["added"].asInt() == 1);
}

} // namespace

TEST_CASE("STL14 先后审批：其他管理员收到「申请已处理」推送，再处理同一条回 308", "[.live][gi-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    LiveChatClient third;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);
    login(third, kThirdAccount);
    setInviteConfirm(false);
    setAdmin(owner, kThirdId, true);

    //MY0308 申请入群：群主与管理员 MY0010 都收到 1307
    const int64_t requestId = memberAppliesAgain(owner, member, &third);

    //群主同意：MY0010 收到 1329（同意、处理人为群主），群主本人的连接也收到
    Json::Value accepted = request(owner, cmd_handle_join_group,
        net::GroupProtocolClient::buildHandleJoinGroupRequest(requestId, true));
    REQUIRE(accepted["code"].asInt() == 0);
    net::JoinRequestHandledNotify toAdmin;
    REQUIRE(waitRequestHandled(third, requestId, toAdmin));
    CHECK(toAdmin.m_groupid == kGroupId);
    CHECK(toAdmin.m_status == kRequestAccepted);
    CHECK(toAdmin.m_handler == kOwnerId);
    CHECK_FALSE(toAdmin.m_handlerName.empty());
    net::JoinRequestHandledNotify toOwner;
    CHECK(waitRequestHandled(owner, requestId, toOwner));

    //MY0010 再处理同一条：308，不改变结果
    Json::Value late = request(third, cmd_handle_join_group,
        net::GroupProtocolClient::buildHandleJoinGroupRequest(requestId, false));
    CHECK(late["code"].asInt() == kErrJoinRequestHandled);
    CHECK(queryLiveDbInt("SELECT f_status FROM t_group_join_request WHERE f_id = " + toStr(requestId)) == kRequestAccepted);

    setAdmin(owner, kThirdId, false);
}

TEST_CASE("STL15 主动退群不再插「退出了群聊」的系统提示", "[.live][gi-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);
    setInviteConfirm(false);

    const int64_t before = countGroupSystemMessages();
    memberLeaves(owner, member);

    //一段时间内群主收不到退群提示，库里也没有新增系统消息
    net::MessageNotify leaveTip;
    std::string leaveText;
    CHECK_FALSE(waitSystemTipWithin(owner, kSystemTipMemberLeave, leaveTip, leaveText, kQuietWaitMs));
    CHECK(countGroupSystemMessages() == before);

    ownerBringsMemberBack(owner);
}

TEST_CASE("STL16 两位管理员几乎同时审批同一条申请：只有一个生效，结果与是否入群一致", "[.live][gi-live]")
{
    i18n::ResetForTest();
    LiveChatClient owner;
    LiveChatClient member;
    LiveChatClient third;
    login(owner, kOwnerAccount);
    login(member, kMemberAccount);
    login(third, kThirdAccount);
    setInviteConfirm(false);
    setAdmin(owner, kThirdId, true);

    const int64_t requestId = memberAppliesAgain(owner, member, &third);

    //不等应答，两位管理员连着发：群主同意、MY0010 拒绝
    REQUIRE(owner.Send(cmd_handle_join_group, net::GroupProtocolClient::buildHandleJoinGroupRequest(requestId, true)));
    REQUIRE(third.Send(cmd_handle_join_group, net::GroupProtocolClient::buildHandleJoinGroupRequest(requestId, false)));
    std::string ownerBody;
    std::string thirdBody;
    REQUIRE(owner.WaitFor(cmd_handle_join_group, ownerBody, kWaitMs));
    REQUIRE(third.WaitFor(cmd_handle_join_group, thirdBody, kWaitMs));
    Json::Value ownerResp;
    Json::Value thirdResp;
    REQUIRE(parseJson(ownerBody, ownerResp));
    REQUIRE(parseJson(thirdBody, thirdResp));
    const int ownerCode = ownerResp["code"].asInt();
    const int thirdCode = thirdResp["code"].asInt();
    INFO("owner code " << ownerCode << ", admin code " << thirdCode);

    //恰好一个成功、另一个回 308
    CHECK(((ownerCode == 0 && thirdCode == kErrJoinRequestHandled)
           || (ownerCode == kErrJoinRequestHandled && thirdCode == 0)));

    //申请的最终状态与是否在群里一致
    const int64_t status = queryLiveDbInt("SELECT f_status FROM t_group_join_request WHERE f_id = " + toStr(requestId));
    const int64_t inGroup = queryLiveDbInt("SELECT COUNT(*) FROM t_group_member WHERE f_groupid = " + toStr(kGroupId)
                                           + " AND f_userid = " + toStr(kMemberId));
    if (ownerCode == 0)
    {
        CHECK(status == kRequestAccepted);
        CHECK(inGroup == 1);
        //入群提示只有一条
        net::MessageNotify tip;
        std::string serverText;
        Json::Value args;
        REQUIRE(waitJoinTip(owner, kOwnerId, tip, serverText, args));
        net::MessageNotify extra;
        std::string extraText;
        CHECK_FALSE(waitSystemTipWithin(owner, kSystemTipMemberJoin, extra, extraText, kQuietWaitMs));
    }
    else
    {
        CHECK(status == kRequestRejected);
        CHECK(inGroup == 0);
        ownerBringsMemberBack(owner);
    }

    setAdmin(owner, kThirdId, false);
}
