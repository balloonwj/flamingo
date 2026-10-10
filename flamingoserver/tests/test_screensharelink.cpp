/**
 *  屏幕共享分享链接码表单元测试（Suite SSLK），test_screensharelink.cpp
 *
 *  覆盖 ScreenShareLinkTable（docs/屏幕共享与录制方案.md 第 12.4 节，第 25 期）：
 *    - SSLK1 链接码的格式：长度 10、只用 56 个字符；生成的码格式合法；不合法的码解析为无效；
 *    - SSLK2 取得链接：同一房间重复取得返回同一个码；不同房间的码不同；只取不生成的 get 在没有链接时返回 false；
 *    - SSLK3 重置与停用：重置后旧码无效、新码有效、免输密码的设置沿用；停用后码无效、get 取不到，再次取得生成新码；
 *    - SSLK4 免输密码：没有链接时设置失败；有链接时设置后解析结果带上；
 *    - SSLK5 共享结束：结束后解析为「已结束」并回填房间号，保留期满后由 reap 清除、解析为无效；结束后再取得生成新码；
 *    - SSLK6 解析频率限制：窗口内第 31 次被拒绝，窗口过后恢复；不同用户分开计数；reap 清除过期计数；
 *    - SSLK7 生成时遇到重复码会重新生成（用可预测的随机数制造重复）；
 *    - SSLK8 加入资格（ScreenShareRules::checkLinkJoin）：会话成员不变、有效免密链接可免密码；非会话成员凭有效链接且同企业放行
 *            并标为链接访客；链接无效、已结束、属于别的房间回 733；不同企业回 734；被移出且不允许再加入回 719；没带链接码原样返回；
 *    - SSLK9 协议：1722 解析 linkcode；1752 请求解析与应答；1753 请求解析，应答对会话成员带 ctype / cid、对链接访客不带，
 *            已结束时只带状态，失败时只有 code；
 *    - SSLK10 房间成员的链接访客标记：joinShare 带 viaLink 时记下，随 1725 快照下发 vialink；会话成员不带；
 *    - SSLK11 单聊共享里给链接访客换算的会话 id 为 0，会话双方照旧。
 *  每条用例都构造自己的 ScreenShareLinkTable / ScreenShareManager 实例，互不共享状态。
 *
 *  典型用法：Tests.exe --test-case="SSLK*"
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include <set>
#include <string>

#include "doctest.h"
#include "../chatserversrc/Protocol.h"
#include "../chatserversrc/ScreenShareLinkTable.h"
#include "../chatserversrc/ScreenShareManager.h"
#include "../chatserversrc/ScreenShareProtocol.h"
#include "../chatserversrc/ScreenShareRules.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//测试用的时间点，Unix 毫秒
const int64_t kT0 = 1000000;

//可预测的随机数：按调用次数依次返回 0、1、2……再对上界取模（SSLK7 用来制造重复码）
uint32_t g_seqCounter = 0;
//每生成多少个字符后回到起点：设为链接码长度时，每次生成的码都相同
uint32_t g_seqPeriod = 0;

uint32_t sequentialRandom(uint32_t upperBound)
{
    const uint32_t value = (g_seqPeriod > 0) ? (g_seqCounter % g_seqPeriod) : g_seqCounter;
    ++g_seqCounter;
    return value % upperBound;
}

//可预测的随机数：前 kRepeatCodes 个码都相同（全取 0），之后按调用次数递增
const uint32_t kRepeatChars = 2 * ScreenShareLinkTable::kCodeLength;
uint32_t g_repeatCounter = 0;

uint32_t repeatThenSequentialRandom(uint32_t upperBound)
{
    const uint32_t value = (g_repeatCounter < kRepeatChars) ? 0 : g_repeatCounter;
    ++g_repeatCounter;
    return value % upperBound;
}

} // namespace

TEST_CASE("SSLK1 link code format")
{
    const std::string alphabet = ScreenShareLinkTable::alphabet();
    CHECK(alphabet.size() == 56);
    //易混字符不在字符表里
    const char confusable[] = { '0', 'O', 'o', '1', 'l', 'I' };
    for (size_t i = 0; i < sizeof(confusable); ++i)
        CHECK(alphabet.find(confusable[i]) == std::string::npos);

    ScreenShareLinkTable links;
    std::string code;
    bool noPassword = true;
    CHECK(links.getOrCreate("ss-1-1000-1", code, noPassword));
    CHECK(code.size() == ScreenShareLinkTable::kCodeLength);
    CHECK(ScreenShareLinkTable::isValidCode(code));
    CHECK_FALSE(noPassword);

    //长度不对、含字符表以外的字符、含 '\0' 的都不合法
    CHECK_FALSE(ScreenShareLinkTable::isValidCode(""));
    CHECK_FALSE(ScreenShareLinkTable::isValidCode("Ab3dE7kQ9"));
    CHECK_FALSE(ScreenShareLinkTable::isValidCode("Ab3dE7kQ9zz"));
    CHECK_FALSE(ScreenShareLinkTable::isValidCode("Ab3dE7kQ90"));
    CHECK_FALSE(ScreenShareLinkTable::isValidCode("Ab3dE7kQ9/"));
    CHECK_FALSE(ScreenShareLinkTable::isValidCode(std::string("Ab3dE7kQ9\0", 10)));
    CHECK(ScreenShareLinkTable::isValidCode("Ab3dE7kQ9z"));

    std::string roomId;
    CHECK(links.resolve("Ab3dE7kQ90", roomId, noPassword) == screenshare_link_invalid);
    CHECK(links.resolve("Ab3dE7kQ9z", roomId, noPassword) == screenshare_link_invalid);
}

TEST_CASE("SSLK2 get or create returns the same code per room")
{
    ScreenShareLinkTable links;
    std::string first;
    std::string again;
    std::string other;
    bool noPassword = false;
    CHECK(links.getOrCreate("room-a", first, noPassword));
    CHECK_FALSE(links.getOrCreate("room-a", again, noPassword));
    CHECK(first == again);
    CHECK(links.getOrCreate("room-b", other, noPassword));
    CHECK(other != first);

    std::string got;
    CHECK(links.get("room-a", got, noPassword));
    CHECK(got == first);
    CHECK_FALSE(links.get("room-c", got, noPassword));

    std::string roomId;
    CHECK(links.resolve(first, roomId, noPassword) == screenshare_link_active);
    CHECK(roomId == "room-a");
    CHECK(links.resolve(other, roomId, noPassword) == screenshare_link_active);
    CHECK(roomId == "room-b");
}

TEST_CASE("SSLK3 reset and disable")
{
    ScreenShareLinkTable links;
    std::string oldCode;
    bool noPassword = false;
    links.getOrCreate("room-a", oldCode, noPassword);
    CHECK(links.setNoPassword("room-a", true));

    std::string newCode;
    links.reset("room-a", newCode, noPassword);
    CHECK(newCode != oldCode);
    CHECK(noPassword);
    std::string roomId;
    CHECK(links.resolve(oldCode, roomId, noPassword) == screenshare_link_invalid);
    bool resolvedNoPassword = false;
    CHECK(links.resolve(newCode, roomId, resolvedNoPassword) == screenshare_link_active);
    CHECK(resolvedNoPassword);

    //停用：码无效、取不到；再次取得时生成新码，免输密码回到默认的关闭
    CHECK(links.disable("room-a"));
    CHECK_FALSE(links.disable("room-a"));
    CHECK(links.resolve(newCode, roomId, noPassword) == screenshare_link_invalid);
    std::string got;
    CHECK_FALSE(links.get("room-a", got, noPassword));
    std::string third;
    CHECK(links.getOrCreate("room-a", third, noPassword));
    CHECK(third != newCode);
    CHECK_FALSE(noPassword);

    //原来没有链接的房间：重置等同生成
    std::string fresh;
    links.reset("room-z", fresh, noPassword);
    CHECK(ScreenShareLinkTable::isValidCode(fresh));
    CHECK_FALSE(noPassword);
}

TEST_CASE("SSLK4 no-password flag")
{
    ScreenShareLinkTable links;
    CHECK_FALSE(links.setNoPassword("room-a", true));
    std::string code;
    bool noPassword = true;
    links.getOrCreate("room-a", code, noPassword);
    CHECK_FALSE(noPassword);
    CHECK(links.setNoPassword("room-a", true));
    std::string roomId;
    bool resolved = false;
    CHECK(links.resolve(code, roomId, resolved) == screenshare_link_active);
    CHECK(resolved);
    std::string got;
    bool gotNoPassword = false;
    CHECK(links.get("room-a", got, gotNoPassword));
    CHECK(gotNoPassword);
    CHECK(links.setNoPassword("room-a", false));
    CHECK(links.resolve(code, roomId, resolved) == screenshare_link_active);
    CHECK_FALSE(resolved);
}

TEST_CASE("SSLK5 ended links are kept for a while")
{
    ScreenShareLinkTable links;
    std::string code;
    bool noPassword = false;
    links.getOrCreate("room-a", code, noPassword);
    links.onRoomEnded("room-a", kT0);
    //没有链接的房间结束：什么也不做
    links.onRoomEnded("room-none", kT0);

    std::string roomId;
    CHECK(links.resolve(code, roomId, noPassword) == screenshare_link_ended);
    CHECK(roomId == "room-a");
    std::string got;
    CHECK_FALSE(links.get("room-a", got, noPassword));

    //保留期内 reap 不清除
    links.reap(kT0 + ScreenShareLinkTable::kEndedKeepMs - 1);
    CHECK(links.resolve(code, roomId, noPassword) == screenshare_link_ended);
    CHECK(links.size() == 1);
    //保留期满后清除
    links.reap(kT0 + ScreenShareLinkTable::kEndedKeepMs);
    CHECK(links.resolve(code, roomId, noPassword) == screenshare_link_invalid);
    CHECK(links.size() == 0);

    //房间号如果又被用上（实际不会，房间号带时间与序号），结束后的取得生成新码，不复用旧码
    std::string other;
    links.getOrCreate("room-b", other, noPassword);
    links.onRoomEnded("room-b", kT0);
    std::string again;
    CHECK(links.getOrCreate("room-b", again, noPassword));
    CHECK(again != other);
}

TEST_CASE("SSLK6 resolve rate limit")
{
    ScreenShareLinkTable links;
    const int64_t userA = 5127;
    const int64_t userB = 5308;
    for (int i = 0; i < ScreenShareLinkTable::kResolveLimitPerWindow; ++i)
        CHECK(links.allowResolve(userA, kT0 + i));
    CHECK_FALSE(links.allowResolve(userA, kT0 + 100));
    //另一个用户不受影响
    CHECK(links.allowResolve(userB, kT0 + 100));
    //窗口过后重新计数
    CHECK(links.allowResolve(userA, kT0 + ScreenShareLinkTable::kResolveWindowMs));
    //reap 清除已过期的计数后，用户照常可以解析
    links.reap(kT0 + 10 * ScreenShareLinkTable::kResolveWindowMs);
    CHECK(links.allowResolve(userB, kT0 + 10 * ScreenShareLinkTable::kResolveWindowMs));
}

TEST_CASE("SSLK7 duplicate codes are regenerated")
{
    ScreenShareLinkTable links;
    g_repeatCounter = 0;
    links.setRandomFn(&repeatThenSequentialRandom);
    std::string first;
    std::string second;
    bool noPassword = false;
    links.getOrCreate("room-a", first, noPassword);
    //第二个房间先拿到与第一个相同的码，须重新生成
    links.getOrCreate("room-b", second, noPassword);
    CHECK(first == std::string(ScreenShareLinkTable::kCodeLength, ScreenShareLinkTable::alphabet()[0]));
    CHECK(second != first);
    CHECK(ScreenShareLinkTable::isValidCode(second));

    //可预测的随机数也只会产出合法的码
    g_seqCounter = 0;
    g_seqPeriod = 0;
    links.setRandomFn(&sequentialRandom);
    std::set<std::string> seen;
    for (int i = 0; i < 20; ++i)
    {
        std::string code;
        links.reset("room-c", code, noPassword);
        CHECK(ScreenShareLinkTable::isValidCode(code));
        seen.insert(code);
    }
    CHECK(seen.size() == 20);
    links.setRandomFn(NULL);
}

namespace {

//SSLK8～SSLK11 用的群聊房间：房间号、群号、发起人
const char* kLinkRoomId = "ss-5127-1000000-1";
const int64_t kLinkGroupId = 9001;
const int64_t kLinkHostId = 5127;
const int64_t kLinkGuestId = 7001;

ScreenShareRoom makeLinkRoom(int ctype)
{
    ScreenShareRoom room;
    room.m_roomId = kLinkRoomId;
    room.m_ctype = ctype;
    room.m_hostId = kLinkHostId;
    if (ctype == conversation_single)
    {
        room.m_convKeyA = kLinkHostId;
        room.m_convKeyB = 5308;
    }
    else
    {
        room.m_convKeyA = kLinkGroupId;
        room.m_convKeyB = 0;
    }
    return room;
}

Json::Value parseJson(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    Json::CharReader* reader = b.newCharReader();
    reader->parse(text.data(), text.data() + text.size(), &root, &errs);
    delete reader;
    return root;
}

} // namespace

TEST_CASE("SSLK8 join eligibility with a share link")
{
    const ScreenShareRoom room = makeLinkRoom(conversation_group);
    bool viaLink = true;
    bool skipPassword = true;
    //会话成员：资格不变；带了有效的免密链接时免密码，不算链接访客
    CHECK(ScreenShareRules::checkLinkJoin(room, kLinkGuestId, error_code_ok, screenshare_link_active, kLinkRoomId, true,
                                          false, viaLink, skipPassword) == error_code_ok);
    CHECK_FALSE(viaLink);
    CHECK(skipPassword);
    CHECK(ScreenShareRules::checkLinkJoin(room, kLinkGuestId, error_code_ok, screenshare_link_active, kLinkRoomId, false,
                                          true, viaLink, skipPassword) == error_code_ok);
    CHECK_FALSE(skipPassword);
    //非会话成员凭有效链接、同企业：放行，标为链接访客；免密码随链接设置
    CHECK(ScreenShareRules::checkLinkJoin(room, kLinkGuestId, error_code_screenshare_not_member, screenshare_link_active,
                                          kLinkRoomId, false, true, viaLink, skipPassword) == error_code_ok);
    CHECK(viaLink);
    CHECK_FALSE(skipPassword);
    CHECK(ScreenShareRules::checkLinkJoin(room, kLinkGuestId, error_code_screenshare_not_member, screenshare_link_active,
                                          kLinkRoomId, true, true, viaLink, skipPassword) == error_code_ok);
    CHECK(skipPassword);
    //链接无效、已结束、属于别的房间
    CHECK(ScreenShareRules::checkLinkJoin(room, kLinkGuestId, error_code_screenshare_not_member, screenshare_link_ended,
                                          kLinkRoomId, false, true, viaLink, skipPassword) ==
          error_code_screenshare_link_invalid);
    CHECK_FALSE(viaLink);
    CHECK(ScreenShareRules::checkLinkJoin(room, kLinkGuestId, error_code_screenshare_not_member, screenshare_link_active,
                                          "ss-other", false, true, viaLink, skipPassword) ==
          error_code_screenshare_link_invalid);
    //不同企业
    CHECK(ScreenShareRules::checkLinkJoin(room, kLinkGuestId, error_code_screenshare_not_member, screenshare_link_active,
                                          kLinkRoomId, false, false, viaLink, skipPassword) ==
          error_code_screenshare_link_enterprise);
    //没带链接码：原样返回
    CHECK(ScreenShareRules::checkLinkJoin(room, kLinkGuestId, error_code_screenshare_not_member, screenshare_link_invalid,
                                          "", false, true, viaLink, skipPassword) == error_code_screenshare_not_member);
    //被移出且不允许再加入：会话成员的 719 原样返回，链接访客同样被拒
    CHECK(ScreenShareRules::checkLinkJoin(room, kLinkGuestId, error_code_screenshare_removed, screenshare_link_active,
                                          kLinkRoomId, true, true, viaLink, skipPassword) == error_code_screenshare_removed);
    ScreenShareRoom banned = room;
    banned.m_banned.push_back(kLinkGuestId);
    CHECK(ScreenShareRules::checkLinkJoin(banned, kLinkGuestId, error_code_screenshare_not_member, screenshare_link_active,
                                          kLinkRoomId, true, true, viaLink, skipPassword) ==
          error_code_screenshare_removed);
    CHECK_FALSE(viaLink);
}

TEST_CASE("SSLK9 share link protocol")
{
    //1722 带链接码；不带时为空；不是字符串时不合法
    std::string roomId;
    std::string password;
    std::string linkCode;
    REQUIRE(ScreenShareProtocol::parseJoinRequest("{\"roomid\":\"r1\",\"password\":\"123456\",\"linkcode\":\"Ab3dE7kQ9z\"}",
                                                  roomId, password, linkCode));
    CHECK(roomId == "r1");
    CHECK(password == "123456");
    CHECK(linkCode == "Ab3dE7kQ9z");
    REQUIRE(ScreenShareProtocol::parseJoinRequest("{\"roomid\":\"r1\"}", roomId, password, linkCode));
    CHECK(linkCode.empty());
    CHECK_FALSE(ScreenShareProtocol::parseJoinRequest("{\"roomid\":\"r1\",\"linkcode\":5}", roomId, password, linkCode));

    //1752 请求
    int action = 0;
    bool noPassword = false;
    REQUIRE(ScreenShareProtocol::parseLinkRequest("{\"roomid\":\"r1\",\"action\":4,\"nopassword\":1}", roomId, action,
                                                  noPassword));
    CHECK(action == screenshare_link_set_nopassword);
    CHECK(noPassword);
    REQUIRE(ScreenShareProtocol::parseLinkRequest("{\"roomid\":\"r1\",\"action\":1}", roomId, action, noPassword));
    CHECK(action == screenshare_link_get);
    CHECK_FALSE(noPassword);
    CHECK_FALSE(ScreenShareProtocol::parseLinkRequest("{\"roomid\":\"r1\",\"action\":9}", roomId, action, noPassword));
    CHECK_FALSE(ScreenShareProtocol::parseLinkRequest("{\"action\":1}", roomId, action, noPassword));

    //1752 应答
    Json::Value v = parseJson(ScreenShareProtocol::buildLinkResponse(0, "", "r1", "Ab3dE7kQ9z", true));
    CHECK(v["code"].asInt() == 0);
    CHECK(v["roomid"].asString() == "r1");
    CHECK(v["linkcode"].asString() == "Ab3dE7kQ9z");
    CHECK(v["nopassword"].asInt() == 1);
    v = parseJson(ScreenShareProtocol::buildLinkResponse(error_code_screenshare_not_host, "", "r1", "", false));
    CHECK(v["code"].asInt() == error_code_screenshare_not_host);
    CHECK_FALSE(v.isMember("linkcode"));

    //1753 请求
    REQUIRE(ScreenShareProtocol::parseLinkResolveRequest("{\"linkcode\":\"Ab3dE7kQ9z\"}", linkCode));
    CHECK(linkCode == "Ab3dE7kQ9z");
    CHECK_FALSE(ScreenShareProtocol::parseLinkResolveRequest("{\"linkcode\":\"\"}", linkCode));
    CHECK_FALSE(ScreenShareProtocol::parseLinkResolveRequest("{}", linkCode));

    //1753 应答：会话成员带会话，链接访客不带
    ScreenShareLinkSummary s;
    s.m_status = screenshare_link_active;
    s.m_roomId = "r1";
    s.m_title = "t";
    s.m_desc = "d";
    s.m_hostId = kLinkHostId;
    s.m_hostName = "host";
    s.m_locked = true;
    s.m_forced = false;
    s.m_member = true;
    s.m_ctype = conversation_group;
    s.m_cid = kLinkGroupId;
    v = parseJson(ScreenShareProtocol::buildLinkResolveResponse(0, "", s));
    CHECK(v["status"].asInt() == screenshare_link_active);
    CHECK(v["roomid"].asString() == "r1");
    CHECK(v["host"].asString() == "5127");
    CHECK(v["hostname"].asString() == "host");
    CHECK(v["locked"].asInt() == 1);
    CHECK(v["member"].asInt() == 1);
    CHECK(v["ctype"].asInt() == conversation_group);
    CHECK(v["cid"].asString() == "9001");
    s.m_member = false;
    v = parseJson(ScreenShareProtocol::buildLinkResolveResponse(0, "", s));
    CHECK(v["member"].asInt() == 0);
    CHECK_FALSE(v.isMember("ctype"));
    CHECK_FALSE(v.isMember("cid"));
    //已结束：只带状态
    ScreenShareLinkSummary ended;
    ended.m_status = screenshare_link_ended;
    v = parseJson(ScreenShareProtocol::buildLinkResolveResponse(0, "", ended));
    CHECK(v["status"].asInt() == screenshare_link_ended);
    CHECK_FALSE(v.isMember("roomid"));
    //失败：只有 code
    v = parseJson(ScreenShareProtocol::buildLinkResolveResponse(error_code_screenshare_link_invalid, "", s));
    CHECK(v["code"].asInt() == error_code_screenshare_link_invalid);
    CHECK_FALSE(v.isMember("status"));
}

TEST_CASE("SSLK10 link guests are marked in the room snapshot")
{
    ScreenShareManager mgr;
    ScreenShareConfig cfg;
    cfg.m_relayTokenKey = "screenshare-link-test-key";
    cfg.m_relayHost = "127.0.0.1";
    cfg.m_relayPort = 20003;
    mgr.configure(cfg);
    std::string roomId;
    REQUIRE(mgr.startShare(kLinkHostId, conversation_group, kLinkGroupId, false, false, false, kT0, roomId) == 0);
    int seat = 0;
    REQUIRE(mgr.joinShare(roomId, 5308, kT0, seat) == 0);
    REQUIRE(mgr.joinShare(roomId, kLinkGuestId, kT0, seat, true) == 0);
    //重复加入不改原来的标记
    REQUIRE(mgr.joinShare(roomId, kLinkGuestId, kT0, seat, false) == 0);
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    REQUIRE(room.findMember(kLinkGuestId) != NULL);
    CHECK(room.findMember(kLinkGuestId)->m_viaLink);
    CHECK_FALSE(room.findMember(5308)->m_viaLink);
    const Json::Value v = parseJson(ScreenShareProtocol::buildStateNotify(1, kLinkGuestId, room));
    const Json::Value& members = v["members"];
    REQUIRE(members.isArray());
    int marked = 0;
    for (Json::ArrayIndex i = 0; i < members.size(); ++i)
    {
        if (members[i].isMember("vialink"))
        {
            CHECK(members[i]["uid"].asString() == "7001");
            CHECK(members[i]["vialink"].asInt() == 1);
            ++marked;
        }
    }
    CHECK(marked == 1);
}

TEST_CASE("SSLK11 link guests have no single-chat conversation id")
{
    const ScreenShareRoom single = makeLinkRoom(conversation_single);
    CHECK(ScreenShareRules::conversationIdFor(single, kLinkHostId) == 5308);
    CHECK(ScreenShareRules::conversationIdFor(single, 5308) == kLinkHostId);
    CHECK(ScreenShareRules::conversationIdFor(single, kLinkGuestId) == 0);
    const ScreenShareRoom group = makeLinkRoom(conversation_group);
    CHECK(ScreenShareRules::conversationIdFor(group, kLinkGuestId) == kLinkGroupId);
}
