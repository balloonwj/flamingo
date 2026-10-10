/**
 *  屏幕共享接入 ChatSession 所用业务规则的单元测试（Suite SCS），test_screensharerules.cpp
 *
 *  覆盖 docs/屏幕共享与录制方案.md 第 11.4 节 2a-2，2026-10-02 与用户确认的用例：
 *    - SCS1 共享卡片（msgtype=19）的 content：roomid、host、hostname、starttime 齐全，64 位 id 以字符串编码；
 *    - SCS2 结束系统提示的时长格式：不足 1 小时为 mm:ss，满 1 小时为 h:mm:ss；
 *    - SCS3 加入资格：单聊只允许会话双方，群聊只允许群成员，其余返回 709；
 *    - SCS4 开始推送的通知对象：单聊为对端，群聊为全体群成员去掉发起人；以及按接收者换算会话 id。
 *  第 2 期 2g 补充：
 *    - SCS5 会话列表摘要：共享卡片显示为「[屏幕共享]」，不再是兜底的「[消息]」；
 *    - SCS6 共享卡片只能由服务端生成：客户端不能自行发送、置顶、撤回、转发（与文件共享卡片同一规则）。
 *  第 12 期补 SSPW2（密码格式与加入校验）；第 13 期补 SSFM3b（强制模式下掉线的观看者回来时免校验密码）；
 *  第 14 期补 SSRM1c（被移出且不允许再次加入的人没有加入资格）；第 15 期补 SSTI1b（共享标题与描述的格式）、SSTI4（敏感词）。
 *  联调层面的完整流程见客户端的 ScreenShareLiveTests.cpp（Suite SSLIVE）。
 *
 *  典型用法：Tests.exe --test-case="SCS*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/ScreenShareRules.h"
#include "../chatserversrc/ScreenShareProtocol.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../chatserversrc/CallProtocol.h"   // enum call_state
#include "../chatserversrc/SensitiveWordDefs.h"   // sensitive_source_screenshare（第 15 期）
#include "../jsoncpp1.9.0/json.h"

namespace {

//测试用的两个用户与一个群
const int64_t kScsUserA = 5127;
const int64_t kScsUserB = 5308;
const int64_t kScsOutsider = 5001;
const int64_t kScsGroupId = 268435463;

//群里另外两名成员
const int64_t kScsMemberC = 5010;
const int64_t kScsMemberD = 5058;

//超过 2^53 的 userid，检查 64 位 id 以字符串原样往返、不经过 double
const int64_t kScsHugeId = 9007199254740993LL;

//发起时间，Unix 毫秒
const int64_t kScsStartTime = 1790000000123LL;

//「张三」的 UTF-8 编码
const char* kScsZhangSan = "\xE5\xBC\xA0\xE4\xB8\x89";

//「屏幕共享已结束，时长 」的 UTF-8 编码（末尾含一个半角空格）
const char* kScsEndTipPrefix = "\xE5\xB1\x8F\xE5\xB9\x95\xE5\x85\xB1\xE4\xBA\xAB\xE5\xB7\xB2\xE7\xBB\x93\xE6\x9D\x9F"
                               "\xEF\xBC\x8C\xE6\x97\xB6\xE9\x95\xBF ";

bool scsParseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

//构造一个单聊房间快照：会话键按「小 id 在 A、大 id 在 B」填写，与 ScreenShareManager::makeConvKey 一致
ScreenShareRoom scsSingleRoom(int64_t hostId, int64_t peerId)
{
    ScreenShareRoom room;
    room.m_roomId = "ss-test-single";
    room.m_ctype = conversation_single;
    room.m_convKeyA = hostId < peerId ? hostId : peerId;
    room.m_convKeyB = hostId < peerId ? peerId : hostId;
    room.m_hostId = hostId;
    room.m_startTime = kScsStartTime;
    return room;
}

//构造一个群聊房间快照
ScreenShareRoom scsGroupRoom(int64_t hostId, int64_t groupId)
{
    ScreenShareRoom room;
    room.m_roomId = "ss-test-group";
    room.m_ctype = conversation_group;
    room.m_convKeyA = groupId;
    room.m_convKeyB = 0;
    room.m_hostId = hostId;
    room.m_startTime = kScsStartTime;
    return room;
}

} // namespace

TEST_CASE("SCS1 共享卡片的 content 字段齐全，64 位 id 以字符串编码")
{
    Json::Value root;
    REQUIRE(scsParseJsonText(ScreenShareProtocol::buildCardContent("ss-5127-1-1", kScsUserA, kScsZhangSan,
                                                                   kScsStartTime),
                             root));
    CHECK(root["roomid"].asString() == "ss-5127-1-1");
    CHECK(root["host"].isString());
    CHECK(root["host"].asString() == "5127");
    CHECK(root["hostname"].asString() == kScsZhangSan);
    CHECK(root["starttime"].isString());
    CHECK(root["starttime"].asString() == "1790000000123");

    REQUIRE(scsParseJsonText(ScreenShareProtocol::buildCardContent("ss-x", kScsHugeId, "", kScsStartTime), root));
    CHECK(root["host"].asString() == "9007199254740993");
    CHECK(root["hostname"].asString() == "");
}

TEST_CASE("SCS2 结束提示的时长格式：不足 1 小时为 mm:ss，满 1 小时为 h:mm:ss")
{
    const std::string prefix(kScsEndTipPrefix);
    CHECK(ScreenShareRules::formatEndTip(0) == prefix + "00:00");
    CHECK(ScreenShareRules::formatEndTip(65) == prefix + "01:05");
    CHECK(ScreenShareRules::formatEndTip(3599) == prefix + "59:59");
    CHECK(ScreenShareRules::formatEndTip(3600) == prefix + "1:00:00");
    CHECK(ScreenShareRules::formatEndTip(3725) == prefix + "1:02:05");
    CHECK(ScreenShareRules::formatEndTip(37230) == prefix + "10:20:30");
    //时钟回拨等原因算出负数时按 0 显示
    CHECK(ScreenShareRules::formatEndTip(-5) == prefix + "00:00");
}

TEST_CASE("SCS3 加入资格：单聊只允许会话双方，群聊只允许群成员")
{
    SUBCASE("单聊")
    {
        ScreenShareRoom room = scsSingleRoom(kScsUserA, kScsUserB);
        CHECK(ScreenShareRules::checkJoinEligibility(room, kScsUserB, false) == error_code_ok);
        CHECK(ScreenShareRules::checkJoinEligibility(room, kScsUserA, false) == error_code_ok);
        CHECK(ScreenShareRules::checkJoinEligibility(room, kScsOutsider, false) == error_code_screenshare_not_member);
        //单聊忽略 isGroupMember：即便调用方误传 true，第三人也不能加入
        CHECK(ScreenShareRules::checkJoinEligibility(room, kScsOutsider, true) == error_code_screenshare_not_member);
    }
    SUBCASE("群聊")
    {
        ScreenShareRoom room = scsGroupRoom(kScsUserA, kScsGroupId);
        CHECK(ScreenShareRules::checkJoinEligibility(room, kScsUserB, true) == error_code_ok);
        CHECK(ScreenShareRules::checkJoinEligibility(room, kScsOutsider, false) == error_code_screenshare_not_member);
    }
}

TEST_CASE("SCS4 开始推送的通知对象与按接收者换算的会话 id")
{
    SUBCASE("单聊：通知对端，发起人是较小或较大的 id 都一样")
    {
        std::vector<int64_t> empty;
        std::vector<int64_t> t1 = ScreenShareRules::startNotifyTargets(scsSingleRoom(kScsUserA, kScsUserB), empty);
        REQUIRE(t1.size() == 1);
        CHECK(t1[0] == kScsUserB);
        std::vector<int64_t> t2 = ScreenShareRules::startNotifyTargets(scsSingleRoom(kScsUserB, kScsUserA), empty);
        REQUIRE(t2.size() == 1);
        CHECK(t2[0] == kScsUserA);
    }
    SUBCASE("群聊：全体群成员去掉发起人，重复的 id 只通知一次")
    {
        ScreenShareRoom room = scsGroupRoom(kScsUserA, kScsGroupId);
        std::vector<int64_t> members;
        members.push_back(kScsMemberC);
        members.push_back(kScsMemberD);
        members.push_back(kScsUserA);
        members.push_back(kScsUserB);
        members.push_back(kScsMemberC);
        std::vector<int64_t> t = ScreenShareRules::startNotifyTargets(room, members);
        REQUIRE(t.size() == 3);
        CHECK(t[0] == kScsMemberC);
        CHECK(t[1] == kScsMemberD);
        CHECK(t[2] == kScsUserB);
    }
    SUBCASE("按接收者换算会话 id：单聊为另一方，群聊为群 id")
    {
        ScreenShareRoom single = scsSingleRoom(kScsUserA, kScsUserB);
        CHECK(ScreenShareRules::conversationIdFor(single, kScsUserB) == kScsUserA);
        CHECK(ScreenShareRules::conversationIdFor(single, kScsUserA) == kScsUserB);
        ScreenShareRoom group = scsGroupRoom(kScsUserA, kScsGroupId);
        CHECK(ScreenShareRules::conversationIdFor(group, kScsUserB) == kScsGroupId);
        CHECK(ScreenShareRules::conversationIdFor(group, kScsUserA) == kScsGroupId);
    }
}

TEST_CASE("SCS5 会话列表摘要：共享卡片显示为「[屏幕共享]」")
{
    const std::string content = ScreenShareProtocol::buildCardContent("ss-1", kScsUserA, u8"张三", 1700000000000LL);
    CHECK(MessageProtocol::buildMessageDigest(message_type_screenshare, content) == u8"[屏幕共享]");
    //内容解析不了时同样只显示类型
    CHECK(MessageProtocol::buildMessageDigest(message_type_screenshare, "not json") == u8"[屏幕共享]");
}

TEST_CASE("SCS6 共享卡片不允许客户端发送、置顶、撤回、转发")
{
    CHECK_FALSE(MessageProtocol::isClientOperableMsgType(message_type_screenshare));
    MessageRecord src;
    src.msgtype = message_type_screenshare;
    CHECK_FALSE(MessageProtocol::isForwardableSource(src));
    //结束提示（系统消息）不受影响
    CHECK(MessageProtocol::isClientOperableMsgType(message_type_system));
}

TEST_CASE("SCS7 通话与共享的互斥：只有已接通的单聊通话双方之间可以共享")
{
    SUBCASE("不在通话中：不冲突，单聊群聊都可以")
    {
        CHECK(ScreenShareRules::checkCallConflict(false, 0, 0, conversation_single, kScsUserB) == error_code_ok);
        CHECK(ScreenShareRules::checkCallConflict(false, 0, 0, conversation_group, kScsGroupId) == error_code_ok);
    }
    SUBCASE("已接通、单聊、共享的另一方正是通话对端：允许（第 8 期，通话中共享屏幕）")
    {
        CHECK(ScreenShareRules::checkCallConflict(true, call_state_connected, kScsUserB, conversation_single,
                                                  kScsUserB) == error_code_ok);
    }
    SUBCASE("已接通、单聊，但共享对象不是通话对端：冲突")
    {
        CHECK(ScreenShareRules::checkCallConflict(true, call_state_connected, kScsUserB, conversation_single,
                                                  kScsOutsider) == error_code_screenshare_in_call);
    }
    SUBCASE("通话还在响铃：冲突，接通之后才能共享")
    {
        CHECK(ScreenShareRules::checkCallConflict(true, call_state_ringing, kScsUserB, conversation_single,
                                                  kScsUserB) == error_code_screenshare_in_call);
    }
    SUBCASE("通话中发起或加入群聊共享：冲突")
    {
        CHECK(ScreenShareRules::checkCallConflict(true, call_state_connected, kScsUserB, conversation_group,
                                                  kScsUserB) == error_code_screenshare_in_call);
        CHECK(ScreenShareRules::checkCallConflict(true, call_state_connected, kScsUserB, conversation_group,
                                                  kScsGroupId) == error_code_screenshare_in_call);
    }
}

TEST_CASE("SSPW2 共享密码：加入时校验，发起人、已在房间里的人、通话对方免校验")
{
    CHECK(ScreenShareRules::isValidPassword("482915"));
    CHECK(ScreenShareRules::isValidPassword("000000"));
    CHECK_FALSE(ScreenShareRules::isValidPassword(""));
    CHECK_FALSE(ScreenShareRules::isValidPassword("48291"));
    CHECK_FALSE(ScreenShareRules::isValidPassword("4829150"));
    CHECK_FALSE(ScreenShareRules::isValidPassword("48 915"));

    ScreenShareRoom room = scsGroupRoom(kScsUserA, kScsGroupId);
    //没有密码：谁都不校验
    CHECK(ScreenShareRules::checkJoinPassword(room, kScsUserB, "", false) == error_code_ok);
    CHECK(ScreenShareRules::checkJoinPassword(room, kScsUserB, "111111", false) == error_code_ok);

    room.m_password = "482915";
    CHECK(ScreenShareRules::checkJoinPassword(room, kScsUserB, "482915", false) == error_code_ok);
    CHECK(ScreenShareRules::checkJoinPassword(room, kScsUserB, "482916", false) == error_code_screenshare_bad_password);
    CHECK(ScreenShareRules::checkJoinPassword(room, kScsUserB, "", false) == error_code_screenshare_bad_password);
    //发起人自己（宽限期内重新登录）免校验
    CHECK(ScreenShareRules::checkJoinPassword(room, kScsUserA, "", false) == error_code_ok);
    //与发起人处于已接通通话的人免校验（第 8 期通话中共享，对方自动加入）
    CHECK(ScreenShareRules::checkJoinPassword(room, kScsUserB, "", true) == error_code_ok);
    //已在房间里的人重复加入免校验
    ScreenShareMember m;
    m.m_userId = kScsMemberC;
    m.m_seat = 1;
    room.m_members.push_back(m);
    CHECK(ScreenShareRules::checkJoinPassword(room, kScsMemberC, "", false) == error_code_ok);
}

TEST_CASE("SSFM3b 强制模式下掉线的观看者回到观看时免校验密码，其他人照常校验")
{
    ScreenShareRoom room;
    room.m_roomId = "ss-1";
    room.m_ctype = 2;
    room.m_hostId = 5127;
    room.m_password = "482915";
    room.m_forced = true;
    room.m_forcedAway.push_back(5308);
    CHECK(room.isForcedAway(5308));
    CHECK_FALSE(room.isForcedAway(5309));
    CHECK(ScreenShareRules::checkJoinPassword(room, 5308, "", false) == error_code_ok);
    CHECK(ScreenShareRules::checkJoinPassword(room, 5309, "", false) == error_code_screenshare_bad_password);
    CHECK(ScreenShareRules::checkJoinPassword(room, 5309, "482915", false) == error_code_ok);
}

TEST_CASE("SSRM1c 被移出且不允许再次加入的人没有加入资格（719），先于密码校验；其他人照常")
{
    ScreenShareRoom room;
    room.m_roomId = "ss-1";
    room.m_ctype = 2;
    room.m_convKeyA = kScsGroupId;
    room.m_hostId = kScsUserA;
    room.m_password = "482915";
    room.m_banned.push_back(kScsUserB);
    CHECK(room.isBanned(kScsUserB));
    CHECK_FALSE(room.isBanned(kScsMemberC));
    CHECK(ScreenShareRules::checkJoinEligibility(room, kScsUserB, true) == error_code_screenshare_removed);
    CHECK(ScreenShareRules::checkJoinEligibility(room, kScsMemberC, true) == error_code_ok);
    //不是群成员时仍回 709
    CHECK(ScreenShareRules::checkJoinEligibility(room, kScsUserB, false) == error_code_screenshare_not_member);
}

namespace {

//假的敏感词检查（第 15 期）：标题或描述里含 m_word 即算命中，并记下调用参数
class FakeShareSensitiveChecker : public IScreenShareSensitiveChecker
{
public:
    FakeShareSensitiveChecker()
        : m_called(false)
        , m_ctype(0)
        , m_cid(0)
    {
    }

    virtual bool hitShareInfo(int ctype, int64_t cid, const std::vector<std::string>& texts)
    {
        m_called = true;
        m_ctype = ctype;
        m_cid = cid;
        m_texts = texts;
        for (size_t i = 0; i < texts.size(); ++i)
        {
            if (!m_word.empty() && texts[i].find(m_word) != std::string::npos)
                return true;
        }
        return false;
    }

    std::string              m_word;    //假词库里唯一的敏感词；为空表示词库为空
    bool                     m_called;  //是否被调用过
    int                      m_ctype;   //最近一次调用的会话类型
    int64_t                  m_cid;     //最近一次调用的会话 id
    std::vector<std::string> m_texts;   //最近一次调用的待检查文字
};

//把一个 UTF-8 字符重复 n 次
std::string scsRepeat(const char* ch, size_t n)
{
    std::string s;
    for (size_t i = 0; i < n; ++i)
        s += ch;
    return s;
}

} // namespace

TEST_CASE("SSTI1b 共享标题必填、去掉首尾空白后 1～30 字，描述选填、最多 100 字")
{
    //首尾的半角空白与全角空格都去掉
    std::string title = u8"  周会演示\u3000";
    std::string desc = u8" 第三季度汇报 ";
    CHECK(ScreenShareRules::checkShareInfo(title, desc, 2, kScsGroupId, NULL) == error_code_ok);
    CHECK(title == u8"周会演示");
    CHECK(desc == u8"第三季度汇报");

    //没带、只有空白：不合法
    title.clear();
    desc.clear();
    CHECK(ScreenShareRules::checkShareInfo(title, desc, 2, kScsGroupId, NULL) == error_code_bad_request);
    title = u8"\u3000 \t";
    CHECK(ScreenShareRules::checkShareInfo(title, desc, 2, kScsGroupId, NULL) == error_code_bad_request);

    //字数按字符算：30 个汉字可以，31 个不行
    title = scsRepeat(u8"周", 30);
    desc.clear();
    CHECK(ScreenShareRules::checkShareInfo(title, desc, 2, kScsGroupId, NULL) == error_code_ok);
    title = scsRepeat(u8"周", 31);
    CHECK(ScreenShareRules::checkShareInfo(title, desc, 2, kScsGroupId, NULL) == error_code_bad_request);

    //描述可以为空；100 字可以，101 字不行
    title = "t";
    desc = scsRepeat(u8"述", 100);
    CHECK(ScreenShareRules::checkShareInfo(title, desc, 2, kScsGroupId, NULL) == error_code_ok);
    desc = scsRepeat(u8"述", 101);
    CHECK(ScreenShareRules::checkShareInfo(title, desc, 2, kScsGroupId, NULL) == error_code_bad_request);

    //不是合法 UTF-8：不合法
    title = "\xFF\xFE";
    desc.clear();
    CHECK(ScreenShareRules::checkShareInfo(title, desc, 2, kScsGroupId, NULL) == error_code_bad_request);

    CHECK(ScreenShareRules::kShareTitleMaxChars == 30);
    CHECK(ScreenShareRules::kShareDescMaxChars == 100);
}

TEST_CASE("SSTI4 共享标题或描述命中敏感词回 720；格式不合法时先回 3、不做敏感词检查")
{
    //标题命中：按共享所在会话检查标题与描述两段文字
    FakeShareSensitiveChecker byTitle;
    byTitle.m_word = u8"敏感";
    std::string title = u8"含敏感词的标题";
    std::string desc;
    CHECK(ScreenShareRules::checkShareInfo(title, desc, 2, kScsGroupId, &byTitle) == error_code_screenshare_sensitive);
    CHECK(byTitle.m_called);
    CHECK(byTitle.m_ctype == 2);
    CHECK(byTitle.m_cid == kScsGroupId);
    REQUIRE(byTitle.m_texts.size() == 2);
    CHECK(byTitle.m_texts[0] == u8"含敏感词的标题");
    CHECK(byTitle.m_texts[1].empty());

    //描述命中同样拒绝
    FakeShareSensitiveChecker byDesc;
    byDesc.m_word = u8"敏感";
    title = u8"周会";
    desc = u8"敏感内容";
    CHECK(ScreenShareRules::checkShareInfo(title, desc, 1, kScsUserB, &byDesc) == error_code_screenshare_sensitive);
    CHECK(byDesc.m_ctype == 1);
    CHECK(byDesc.m_cid == kScsUserB);

    //都干净：通过，且检查过一次
    FakeShareSensitiveChecker clean;
    clean.m_word = u8"敏感";
    title = u8"周会";
    desc = u8"汇报";
    CHECK(ScreenShareRules::checkShareInfo(title, desc, 2, kScsGroupId, &clean) == error_code_ok);
    CHECK(clean.m_called);

    //格式不合法：先回 3，不做敏感词检查（不为格式不合法的请求记拦截记录）
    FakeShareSensitiveChecker bad;
    bad.m_word = u8"敏感";
    title.clear();
    desc = u8"敏感";
    CHECK(ScreenShareRules::checkShareInfo(title, desc, 2, kScsGroupId, &bad) == error_code_bad_request);
    CHECK_FALSE(bad.m_called);

    CHECK(chatserver::sensitive_source_screenshare == 13);
}
