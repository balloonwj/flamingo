/**
 *  屏幕共享多人同时共享的单元测试（第 22 期，docs/屏幕共享与录制方案.md 第 2 节第 27～30 条）：
 *    - SSMS1 主持人发起即在共享、用自己的座位推流；共享人数上限的配置与修正；
 *    - SSMS2 申请、撤回、同意、拒绝：转给谁、只有主持人能批、没有申请时 732、人数满时 731、同意时分配与成员座位不重复的画面座位；
 *    - SSMS3 停止共享：共享者停自己的、主持人停别人的、非主持人不能停别人的、没在共享 729；主持人停掉自己的画面后再申请直接恢复；
 *            停止后控制结束、画面座位释放并可被复用；
 *    - SSMS4 「只推流」令牌：角色、座位、只给主持人以外正在共享的人签；
 *    - SSMS5 移交主持人：只有主持人能移交、对方须在共享，移交后原主持人离开房间；任何正在共享的人都是可以被申请控制的画面主人；
 *    - SSMS6 协议：1737 / 1739 的解析、1738 的组装、1725 快照里的共享字段与人数上限；
 *    - SSMS8 经移交成为主持人的人停掉自己的画面后再申请：另分配画面座位并签「只推流」令牌（他自己的连接只是观看连接）；
 *    - SSMS9 经移交成为主持人的人掉线与重新登录（2026-10-07，方案第 14.3 节第 3 条）：掉线时房间保留、按主持人离线处理，
 *            成员座位不变；宽限期内重新登录时按主持人回来处理，并能按原来的成员座位重新签中转令牌；
 *    - SSMS10 经移交成为主持人、正在共享的人可以把自己的屏幕交给观看者控制，停掉自己的画面后不能；
 *    - SSMS11 移交主持人时主持人昵称换成新主持人的，最初的发起人与昵称不变；移交被拒时都不变；
 *    - SSMS12 移交后由新主持人修改标题时，改写的共享卡片上的发起人仍是最初的发起人；
 *    - SSWB1～4 白板（第 23 期）：主持人直接新建、占共享名额、座位不重复；其他人申请经主持人同意；关闭与离开；协议字段。
 *  典型用法：Tests.exe --test-case="SSMS*"
 *  balloonwj@qq.com   2026-10-05
 */
#include "doctest.h"

#include <algorithm>
#include <string>
#include <vector>

#include "../chatserversrc/Protocol.h"
#include "../chatserversrc/ScreenShareManager.h"
#include "../chatserversrc/ScreenShareProtocol.h"
#include "../chatserversrc/ScreenShareRules.h"
#include "../jsoncpp1.9.0/json.h"
#include "../utils/RelayToken.h"

namespace {

//测试用的 relay 令牌密钥与中转地址
const char* kMsKey       = "screenshare-multi-key";
const char* kMsRelayIp   = "127.0.0.1";
const int   kMsRelayPort = 20003;
//测试用的时间点，Unix 毫秒
const int64_t kMsT0 = 2000000;
//群号与几个人：1 主持人，2~5 观看者
const int64_t kMsGroup = 700001;
const int64_t kMsHost = 2001;
const int64_t kMsB = 2002;
const int64_t kMsC = 2003;
const int64_t kMsD = 2004;
const int64_t kMsE = 2005;

//按共享人数上限配置一个管理器（房间人数 50、摄像头 9）
void msConfigure(ScreenShareManager& mgr, int maxSharers)
{
    ScreenShareConfig cfg;
    cfg.m_relayTokenKey = kMsKey;
    cfg.m_relayHost = kMsRelayIp;
    cfg.m_relayPort = kMsRelayPort;
    cfg.m_maxMembers = 50;
    cfg.m_maxCameras = 9;
    cfg.m_maxSharers = maxSharers;
    mgr.configure(cfg);
}

//建房间：主持人发起，其余人依次加入；返回房间号
std::string msRoom(ScreenShareManager& mgr, const std::vector<int64_t>& viewers)
{
    std::string roomId;
    REQUIRE(mgr.startShare(kMsHost, conversation_group, kMsGroup, false, false, false, kMsT0, roomId) == 0);
    for (size_t i = 0; i < viewers.size(); ++i)
    {
        int seat = -1;
        REQUIRE(mgr.joinShare(roomId, viewers[i], kMsT0 + 1, seat) == 0);
    }
    return roomId;
}

//某人的成员记录（拷贝）
ScreenShareMember msMember(ScreenShareManager& mgr, const std::string& roomId, int64_t userId)
{
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(roomId, room));
    const ScreenShareMember* m = room.findMember(userId);
    REQUIRE(m != NULL);
    return *m;
}

//把 JSON 文本解析成 Json::Value，供断言检查组装结果
bool msParse(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

//申请并经主持人同意
void msApprove(ScreenShareManager& mgr, const std::string& roomId, int64_t userId)
{
    ScreenShareSharingResult r;
    REQUIRE(mgr.applySharing(roomId, userId, screenshare_sharing_request, 0, r) == 0);
    REQUIRE(mgr.applySharing(roomId, kMsHost, screenshare_sharing_approve, userId, r) == 0);
}

//申请并经指定的主持人同意（主持人已经移交给别人之后用）
void msApproveBy(ScreenShareManager& mgr, const std::string& roomId, int64_t hostId, int64_t userId)
{
    ScreenShareSharingResult r;
    REQUIRE(mgr.applySharing(roomId, userId, screenshare_sharing_request, 0, r) == 0);
    REQUIRE(mgr.applySharing(roomId, hostId, screenshare_sharing_approve, userId, r) == 0);
}

} // namespace

TEST_CASE("SSMS1 主持人发起即在共享、用自己的座位推流；共享人数上限的配置与修正")
{
    ScreenShareManager mgr;
    msConfigure(mgr, 4);
    CHECK(mgr.maxSharers() == 4);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB});
    const ScreenShareMember host = msMember(mgr, rid, kMsHost);
    CHECK(host.m_sharing);
    CHECK(host.m_screenSeat == 0);
    const ScreenShareMember b = msMember(mgr, rid, kMsB);
    CHECK_FALSE(b.m_sharing);
    CHECK(b.m_screenSeat == -1);
    CHECK_FALSE(b.m_shareRequested);

    //越界按默认值修正：0 与超过房间人数都回到默认 4
    ScreenShareManager bad;
    msConfigure(bad, 0);
    CHECK(bad.maxSharers() == ScreenShareManager::kDefaultMaxSharers);
    msConfigure(bad, 51);
    CHECK(bad.maxSharers() == ScreenShareManager::kDefaultMaxSharers);
    msConfigure(bad, 1);
    CHECK(bad.maxSharers() == 1);
}

TEST_CASE("SSMS2 申请、撤回、同意、拒绝：转给谁、只有主持人能批、没有申请 732、人数满 731、画面座位不与成员座位重复")
{
    ScreenShareManager mgr;
    msConfigure(mgr, 3);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB, kMsC, kMsD});   //座位 1、2、3

    //申请：转给主持人
    ScreenShareSharingResult r;
    REQUIRE(mgr.applySharing(rid, kMsB, screenshare_sharing_request, 0, r) == 0);
    CHECK(r.m_notifyUser == kMsHost);
    CHECK(r.m_subject == kMsB);
    CHECK_FALSE(r.m_changed);
    CHECK(msMember(mgr, rid, kMsB).m_shareRequested);
    //撤回：转给主持人，申请清除
    REQUIRE(mgr.applySharing(rid, kMsB, screenshare_sharing_cancel, 0, r) == 0);
    CHECK(r.m_notifyUser == kMsHost);
    CHECK_FALSE(msMember(mgr, rid, kMsB).m_shareRequested);
    //没有申请时同意：732
    CHECK(mgr.applySharing(rid, kMsHost, screenshare_sharing_approve, kMsB, r) == error_code_screenshare_no_request);
    //重新申请；非主持人不能批
    REQUIRE(mgr.applySharing(rid, kMsB, screenshare_sharing_request, 0, r) == 0);
    CHECK(mgr.applySharing(rid, kMsC, screenshare_sharing_approve, kMsB, r) == error_code_screenshare_not_host);
    //主持人同意：分配画面座位（与成员座位 0~3 不重复，取 4），转给当事人，要另连中转推流，广播
    REQUIRE(mgr.applySharing(rid, kMsHost, screenshare_sharing_approve, kMsB, r) == 0);
    CHECK(r.m_notifyUser == kMsB);
    CHECK(r.m_publishUser == kMsB);
    CHECK(r.m_changed);
    CHECK(r.m_subject == kMsB);
    ScreenShareMember b = msMember(mgr, rid, kMsB);
    CHECK(b.m_sharing);
    CHECK(b.m_screenSeat == 4);
    CHECK_FALSE(b.m_shareRequested);
    //已在共享的人再申请：空操作
    REQUIRE(mgr.applySharing(rid, kMsB, screenshare_sharing_request, 0, r) == 0);
    CHECK(r.m_notifyUser == 0);
    CHECK_FALSE(r.m_changed);
    //拒绝：转给申请人
    REQUIRE(mgr.applySharing(rid, kMsC, screenshare_sharing_request, 0, r) == 0);
    REQUIRE(mgr.applySharing(rid, kMsHost, screenshare_sharing_reject, kMsC, r) == 0);
    CHECK(r.m_notifyUser == kMsC);
    CHECK_FALSE(r.m_changed);
    CHECK_FALSE(msMember(mgr, rid, kMsC).m_shareRequested);
    //上限 3：再同意 C 后满了，D 申请直接 731；D 先申请、满了之后同意也是 731 且申请保留
    REQUIRE(mgr.applySharing(rid, kMsD, screenshare_sharing_request, 0, r) == 0);
    msApprove(mgr, rid, kMsC);
    CHECK(msMember(mgr, rid, kMsC).m_screenSeat == 5);
    CHECK(mgr.applySharing(rid, kMsHost, screenshare_sharing_approve, kMsD, r) == error_code_screenshare_sharers_full);
    CHECK(msMember(mgr, rid, kMsD).m_shareRequested);
    REQUIRE(mgr.applySharing(rid, kMsD, screenshare_sharing_cancel, 0, r) == 0);
    CHECK(mgr.applySharing(rid, kMsD, screenshare_sharing_request, 0, r) == error_code_screenshare_sharers_full);
    //新成员加入：座位跳过画面座位
    int seat = -1;
    REQUIRE(mgr.joinShare(rid, kMsE, kMsT0 + 5, seat) == 0);
    CHECK(seat == 6);
    //参数与身份
    CHECK(mgr.applySharing(rid, kMsB, 99, 0, r) == error_code_bad_request);
    CHECK(mgr.applySharing(rid, 9999, screenshare_sharing_request, 0, r) == error_code_screenshare_not_member);
    CHECK(mgr.applySharing("no-room", kMsB, screenshare_sharing_request, 0, r) == error_code_screenshare_not_exist);
}

TEST_CASE("SSMS3 停止共享：自己停、主持人停别人、非主持人不能停别人、没在共享 729；主持人恢复自己的画面；控制结束、座位复用")
{
    ScreenShareManager mgr;
    msConfigure(mgr, 4);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB, kMsC});   //座位 1、2
    msApprove(mgr, rid, kMsB);   //画面座位 3
    msApprove(mgr, rid, kMsC);   //画面座位 4
    //C 控制 B 的画面
    ScreenShareControlResult cr;
    REQUIRE(mgr.applyControl(rid, kMsB, screenshare_control_grant, kMsB, kMsC, cr) == 0);
    CHECK(msMember(mgr, rid, kMsB).m_controlledBy == kMsC);

    //非主持人不能停别人的
    ScreenShareSharingResult r;
    CHECK(mgr.applySharing(rid, kMsC, screenshare_sharing_stop, kMsB, r) == error_code_screenshare_not_host);
    //主持人停 B 的：B 收到通知，控制结束，画面座位释放
    REQUIRE(mgr.applySharing(rid, kMsHost, screenshare_sharing_stop, kMsB, r) == 0);
    CHECK(r.m_changed);
    CHECK(r.m_notifyUser == kMsB);
    CHECK(r.m_subject == kMsB);
    ScreenShareMember b = msMember(mgr, rid, kMsB);
    CHECK_FALSE(b.m_sharing);
    CHECK(b.m_screenSeat == -1);
    CHECK(b.m_controlledBy == 0);
    //没在共享：729
    CHECK(mgr.applySharing(rid, kMsB, screenshare_sharing_stop, 0, r) == error_code_screenshare_not_sharing);
    //C 停自己的：不另行通知
    REQUIRE(mgr.applySharing(rid, kMsC, screenshare_sharing_stop, 0, r) == 0);
    CHECK(r.m_changed);
    CHECK(r.m_notifyUser == 0);
    //释放的画面座位被复用：再同意 B，取最小的空闲座位 3
    msApprove(mgr, rid, kMsB);
    CHECK(msMember(mgr, rid, kMsB).m_screenSeat == 3);

    //主持人停掉自己的画面：仍是主持人、房间还在；画面不可被控制
    REQUIRE(mgr.applySharing(rid, kMsHost, screenshare_sharing_stop, 0, r) == 0);
    CHECK(r.m_changed);
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.m_hostId == kMsHost);
    CHECK_FALSE(msMember(mgr, rid, kMsHost).m_sharing);
    CHECK(mgr.applyControl(rid, kMsC, screenshare_control_request, kMsHost, 0, cr) == error_code_screenshare_not_sharing);
    //主持人再申请：直接恢复，用自己的座位，不用另签令牌
    REQUIRE(mgr.applySharing(rid, kMsHost, screenshare_sharing_request, 0, r) == 0);
    CHECK(r.m_changed);
    CHECK(r.m_publishUser == 0);
    CHECK(r.m_notifyUser == 0);
    CHECK(msMember(mgr, rid, kMsHost).m_sharing);
    CHECK(msMember(mgr, rid, kMsHost).m_screenSeat == 0);

    //共享者离开房间：画面随之结束，画面座位释放
    bool dissolved = false;
    REQUIRE(mgr.leaveShare(rid, kMsB, dissolved, room) == 0);
    CHECK_FALSE(dissolved);
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.findMember(kMsB) == NULL);
}

TEST_CASE("SSMS4 「只推流」令牌：角色、画面座位，只给主持人以外正在共享的人签")
{
    ScreenShareManager mgr;
    msConfigure(mgr, 4);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB, kMsC});
    msApprove(mgr, rid, kMsB);
    const int64_t now = kMsT0 + 100;
    std::string token;
    int screenSeat = -1;
    REQUIRE(mgr.signPublishToken(rid, kMsB, now, token, screenSeat));
    CHECK(screenSeat == msMember(mgr, rid, kMsB).m_screenSeat);
    RelayTokenPayload p;
    REQUIRE(RelayToken::verify(token, kMsKey, now, p));
    CHECK(p.sessionid == rid);
    CHECK(p.role == RelayToken::role_screenshare_publisher);
    CHECK(p.uid == kMsB);
    CHECK(p.peeruid == screenSeat);
    CHECK(p.expire == now + ScreenShareManager::kRelayTokenTtlMs);
    //主持人用自己的连接推流、没在共享的人、非成员、房间不存在：都不签
    CHECK_FALSE(mgr.signPublishToken(rid, kMsHost, now, token, screenSeat));
    CHECK_FALSE(mgr.signPublishToken(rid, kMsC, now, token, screenSeat));
    CHECK_FALSE(mgr.signPublishToken(rid, 9999, now, token, screenSeat));
    CHECK_FALSE(mgr.signPublishToken("no-room", kMsB, now, token, screenSeat));
}

TEST_CASE("SSMS5 移交主持人：只有主持人、对方须在共享，移交后原主持人离开；任何正在共享的人都是画面主人")
{
    ScreenShareManager mgr;
    msConfigure(mgr, 4);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB, kMsC});
    msApprove(mgr, rid, kMsB);
    //C 可以申请控制 B 的画面（B 不是主持人）
    ScreenShareControlResult cr;
    REQUIRE(mgr.applyControl(rid, kMsC, screenshare_control_request, kMsB, 0, cr) == 0);
    CHECK(cr.m_notifyUser == kMsB);

    ScreenShareRoom room;
    CHECK(mgr.transferHost(rid, kMsB, kMsC, "", room) == error_code_screenshare_not_host);
    CHECK(mgr.transferHost(rid, kMsHost, kMsC, "", room) == error_code_screenshare_not_sharing);
    CHECK(mgr.transferHost(rid, kMsHost, 9999, "", room) == error_code_screenshare_not_member);
    CHECK(mgr.transferHost(rid, kMsHost, kMsHost, "", room) == error_code_bad_request);
    REQUIRE(mgr.transferHost(rid, kMsHost, kMsB, "", room) == 0);
    CHECK(room.m_hostId == kMsB);
    CHECK(room.findMember(kMsHost) == NULL);
    //原主持人已不在房间里，可以去别处
    CHECK_FALSE(mgr.isUserInShare(kMsHost));
    //新主持人能批别人的申请
    ScreenShareSharingResult r;
    REQUIRE(mgr.applySharing(rid, kMsC, screenshare_sharing_request, 0, r) == 0);
    CHECK(r.m_notifyUser == kMsB);
    REQUIRE(mgr.applySharing(rid, kMsB, screenshare_sharing_approve, kMsC, r) == 0);
    //新主持人离开等同结束
    bool dissolved = false;
    REQUIRE(mgr.leaveShare(rid, kMsB, dissolved, room) == 0);
    CHECK(dissolved);
}

TEST_CASE("SSMS6 协议：1737 / 1739 的解析、1738 的组装、1725 快照里的共享字段与人数上限")
{
    std::string roomId;
    int action = 0;
    int64_t target = 0;
    REQUIRE(ScreenShareProtocol::parseSharingRequest("{\"roomid\":\"ss-1\",\"action\":1}", roomId, action, target));
    CHECK(roomId == "ss-1");
    CHECK(action == screenshare_sharing_request);
    CHECK(target == 0);
    //同意、拒绝必须带对象；停止可选
    CHECK_FALSE(ScreenShareProtocol::parseSharingRequest("{\"roomid\":\"ss-1\",\"action\":3}", roomId, action, target));
    REQUIRE(ScreenShareProtocol::parseSharingRequest("{\"roomid\":\"ss-1\",\"action\":3,\"target\":\"2002\"}", roomId,
                                                     action, target));
    CHECK(target == 2002);
    REQUIRE(ScreenShareProtocol::parseSharingRequest("{\"roomid\":\"ss-1\",\"action\":5}", roomId, action, target));
    CHECK(target == 0);
    REQUIRE(ScreenShareProtocol::parseSharingRequest("{\"roomid\":\"ss-1\",\"action\":5,\"target\":\"2003\"}", roomId,
                                                     action, target));
    CHECK(target == 2003);
    //申请时带了对象也忽略；动作越界拒绝
    REQUIRE(ScreenShareProtocol::parseSharingRequest("{\"roomid\":\"ss-1\",\"action\":2,\"target\":\"2003\"}", roomId,
                                                     action, target));
    CHECK(target == 0);
    CHECK_FALSE(ScreenShareProtocol::parseSharingRequest("{\"roomid\":\"ss-1\",\"action\":6}", roomId, action, target));
    CHECK_FALSE(ScreenShareProtocol::parseSharingRequest("{\"action\":1}", roomId, action, target));
    //移交主持人
    REQUIRE(ScreenShareProtocol::parseTransferHostRequest("{\"roomid\":\"ss-1\",\"target\":\"2002\"}", roomId, target));
    CHECK(target == 2002);
    CHECK_FALSE(ScreenShareProtocol::parseTransferHostRequest("{\"roomid\":\"ss-1\"}", roomId, target));

    //1738：同意时带中转字段，其余不带
    Json::Value v;
    REQUIRE(msParse(ScreenShareProtocol::buildSharingNotify("ss-1", screenshare_sharing_approve, 2001, "host", "tok",
                                                                 "1.2.3.4", 20003, 5), v));
    CHECK(v["roomid"].asString() == "ss-1");
    CHECK(v["action"].asInt() == screenshare_sharing_approve);
    CHECK(v["actor"].asString() == "2001");
    CHECK(v["actorname"].asString() == "host");
    CHECK(v["token"].asString() == "tok");
    CHECK(v["relayip"].asString() == "1.2.3.4");
    CHECK(v["relayport"].asInt() == 20003);
    CHECK(v["screenseat"].asInt() == 5);
    REQUIRE(msParse(ScreenShareProtocol::buildSharingNotify("ss-1", screenshare_sharing_request, 2002, "b"), v));
    CHECK_FALSE(v.isMember("token"));
    CHECK_FALSE(v.isMember("screenseat"));

    //1725：成员项带 sharing、在共享时带 screenseat、申请中带 requesting；房间带 maxsharers
    ScreenShareManager mgr;
    msConfigure(mgr, 3);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB, kMsC});
    msApprove(mgr, rid, kMsB);
    ScreenShareSharingResult r;
    REQUIRE(mgr.applySharing(rid, kMsC, screenshare_sharing_request, 0, r) == 0);
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(rid, room));
    REQUIRE(msParse(ScreenShareProtocol::buildStateNotify(screenshare_event_sharing_changed, kMsHost, room, kMsB), v));
    CHECK(v["event"].asInt() == screenshare_event_sharing_changed);
    CHECK(v["maxsharers"].asInt() == 3);
    const Json::Value& members = v["members"];
    REQUIRE(members.size() == 3);
    for (Json::ArrayIndex i = 0; i < members.size(); ++i)
    {
        const std::string uid = members[i]["uid"].asString();
        if (uid == "2001")
        {
            CHECK(members[i]["sharing"].asInt() == 1);
            CHECK(members[i]["screenseat"].asInt() == 0);
        }
        else if (uid == "2002")
        {
            CHECK(members[i]["sharing"].asInt() == 1);
            CHECK(members[i]["screenseat"].asInt() == msMember(mgr, rid, kMsB).m_screenSeat);
            CHECK_FALSE(members[i].isMember("requesting"));
        }
        else
        {
            CHECK(members[i]["sharing"].asInt() == 0);
            CHECK_FALSE(members[i].isMember("screenseat"));
            CHECK(members[i]["requesting"].asInt() == 1);
        }
    }
}

TEST_CASE("SSMS8 经移交成为主持人的人重新共享：另分配画面座位、签「只推流」令牌，不用自己的成员座位")
{
    ScreenShareManager mgr;
    msConfigure(mgr, 4);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB, kMsC});   //座位 1、2
    msApprove(mgr, rid, kMsB);   //画面座位 3
    ScreenShareRoom room;
    REQUIRE(mgr.transferHost(rid, kMsHost, kMsB, "", room) == 0);
    //新主持人停掉自己的画面：画面座位释放
    ScreenShareSharingResult r;
    REQUIRE(mgr.applySharing(rid, kMsB, screenshare_sharing_stop, 0, r) == 0);
    CHECK_FALSE(msMember(mgr, rid, kMsB).m_sharing);
    //再申请：直接生效，但画面座位另分配（不是成员座位 1），并由服务端给他本人签令牌
    REQUIRE(mgr.applySharing(rid, kMsB, screenshare_sharing_request, 0, r) == 0);
    CHECK(r.m_changed);
    CHECK(r.m_publishUser == kMsB);
    const ScreenShareMember b = msMember(mgr, rid, kMsB);
    CHECK(b.m_sharing);
    CHECK_FALSE(b.m_shareRequested);
    CHECK(b.m_screenSeat >= 0);
    CHECK(b.m_screenSeat != b.m_seat);
    std::string token;
    int screenSeat = -1;
    REQUIRE(mgr.signPublishToken(rid, kMsB, kMsT0 + 100, token, screenSeat));
    CHECK(screenSeat == b.m_screenSeat);
    //人数满时同样回 731：B 停下后让 C 共享，再把上限改为 1
    REQUIRE(mgr.applySharing(rid, kMsB, screenshare_sharing_stop, 0, r) == 0);
    REQUIRE(mgr.applySharing(rid, kMsC, screenshare_sharing_request, 0, r) == 0);
    REQUIRE(mgr.applySharing(rid, kMsB, screenshare_sharing_approve, kMsC, r) == 0);
    msConfigure(mgr, 1);
    CHECK(mgr.applySharing(rid, kMsB, screenshare_sharing_request, 0, r) == error_code_screenshare_sharers_full);
}

TEST_CASE("SSMS9 经移交成为主持人的人掉线：房间保留、按主持人离线；重新登录时按主持人回来、按原座位签令牌")
{
    ScreenShareManager mgr;
    msConfigure(mgr, 4);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB, kMsC});   //座位 1、2
    msApprove(mgr, rid, kMsB);
    ScreenShareRoom room;
    REQUIRE(mgr.transferHost(rid, kMsHost, kMsB, "", room) == 0);
    const int seatB = msMember(mgr, rid, kMsB).m_seat;
    //新主持人掉线：不是按观看者离开移出，而是房间保留、记下主持人离线时刻
    CHECK(mgr.onUserOffline(kMsB, kMsT0 + 500, room) == screenshare_offline_host_away);
    CHECK(room.m_hostId == kMsB);
    CHECK(room.m_hostOfflineSince == kMsT0 + 500);
    CHECK(msMember(mgr, rid, kMsB).m_seat == seatB);
    CHECK(mgr.isUserInShare(kMsB));
    //宽限期内重新登录：按主持人回来处理（服务端随后广播事件 8，并把新的 1726 发给他的新连接）
    REQUIRE(mgr.onUserOnline(kMsB, room));
    CHECK(room.m_roomId == rid);
    CHECK(room.m_hostOfflineSince == 0);
    std::string token;
    int seat = -1;
    REQUIRE(mgr.signTokenFor(rid, kMsB, kMsT0 + 600, token, seat));
    CHECK(seat == seatB);
    //普通观看者重新登录不算主持人回来
    CHECK_FALSE(mgr.onUserOnline(kMsC, room));
}

TEST_CASE("SSMS10 经移交成为主持人、正在共享的人把自己的屏幕交给观看者控制：服务端接受；停掉自己的画面后不能再授予")
{
    //2026-10-08：客户端打开观看者名单的「允许控制」之前，先确认服务端支持这条路径
    ScreenShareManager mgr;
    msConfigure(mgr, 4);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB, kMsC});
    msApprove(mgr, rid, kMsB);
    ScreenShareRoom room;
    REQUIRE(mgr.transferHost(rid, kMsHost, kMsB, "", room) == 0);
    //B 是主持人且在共享：把自己的屏幕交给 C 控制，控制关系记在 B 身上
    ScreenShareControlResult g;
    REQUIRE(mgr.applyControl(rid, kMsB, screenshare_control_grant, kMsB, kMsC, g) == 0);
    CHECK(g.m_changed);
    CHECK(msMember(mgr, rid, kMsB).m_controlledBy == kMsC);
    //B 收回
    ScreenShareControlResult rv;
    REQUIRE(mgr.applyControl(rid, kMsB, screenshare_control_revoke, kMsB, 0, rv) == 0);
    CHECK(rv.m_changed);
    CHECK(msMember(mgr, rid, kMsB).m_controlledBy == 0);
    //B 停掉自己的画面：不再是画面主人，不能授予
    ScreenShareSharingResult r;
    REQUIRE(mgr.applySharing(rid, kMsB, screenshare_sharing_stop, 0, r) == 0);
    ScreenShareControlResult e;
    CHECK(mgr.applyControl(rid, kMsB, screenshare_control_grant, kMsB, kMsC, e) == error_code_screenshare_not_sharing);
}

TEST_CASE("SSMS11 移交主持人：主持人昵称换成新主持人的，最初的发起人与昵称不变；移交被拒时都不变")
{
    //2026-10-08 之前，移交主持人时服务端只换了主持人 id，主持人昵称仍是发起人的，链接概要与 rejoin 推送因此名字对不上
    ScreenShareManager mgr;
    msConfigure(mgr, 4);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB, kMsC});
    //发起成功后服务层调用一次 setHostName：主持人与发起人是同一个人
    REQUIRE(mgr.setHostName(rid, u8"张勇"));
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.m_hostId == kMsHost);
    CHECK(room.m_hostName == u8"张勇");
    CHECK(room.m_starterId == kMsHost);
    CHECK(room.m_starterName == u8"张勇");

    //移交被拒：操作者不是主持人、对方没在共享，主持人与发起人都不变
    CHECK(mgr.transferHost(rid, kMsB, kMsC, u8"韩雪飞", room) == error_code_screenshare_not_host);
    CHECK(mgr.transferHost(rid, kMsHost, kMsC, u8"韩雪飞", room) == error_code_screenshare_not_sharing);
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.m_hostId == kMsHost);
    CHECK(room.m_hostName == u8"张勇");

    //移交给正在共享的 B：返回的快照与房间里记的都换成 B 的 id 与昵称，发起人仍是原来的
    msApprove(mgr, rid, kMsB);
    ScreenShareRoom out;
    REQUIRE(mgr.transferHost(rid, kMsHost, kMsB, u8"韩晓燕888", out) == 0);
    CHECK(out.m_hostId == kMsB);
    CHECK(out.m_hostName == u8"韩晓燕888");
    CHECK(out.m_starterId == kMsHost);
    CHECK(out.m_starterName == u8"张勇");
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.m_hostName == u8"韩晓燕888");
    CHECK(room.m_starterName == u8"张勇");

    //发起人昵称只在第一次写入：之后再调用 setHostName 只改主持人昵称
    REQUIRE(mgr.setHostName(rid, u8"韩晓燕"));
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.m_hostName == u8"韩晓燕");
    CHECK(room.m_starterName == u8"张勇");

    //再移交一次给 C：发起人仍是最初的那个人
    msApproveBy(mgr, rid, kMsB, kMsC);
    REQUIRE(mgr.transferHost(rid, kMsB, kMsC, u8"韩雪飞", out) == 0);
    CHECK(out.m_hostId == kMsC);
    CHECK(out.m_hostName == u8"韩雪飞");
    CHECK(out.m_starterId == kMsHost);
    CHECK(out.m_starterName == u8"张勇");
}

TEST_CASE("SSMS12 移交后由新主持人修改标题：改写的共享卡片上的发起人仍是最初的发起人")
{
    //2026-10-08（用户决定卡片始终写最初的发起人）：移交前改写卡片用的是新主持人的 id 配发起人的昵称
    ScreenShareManager mgr;
    msConfigure(mgr, 4);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB});
    REQUIRE(mgr.setHostName(rid, u8"张勇"));
    msApprove(mgr, rid, kMsB);
    ScreenShareRoom room;
    REQUIRE(mgr.transferHost(rid, kMsHost, kMsB, u8"韩晓燕888", room) == 0);

    Json::Value card;
    REQUIRE(msParse(ScreenShareRules::buildCardContentFor(room, u8"新标题", u8"说明"), card));
    CHECK(card["roomid"].asString() == rid);
    CHECK(card["host"].asString() == std::to_string(kMsHost));
    CHECK(card["hostname"].asString() == u8"张勇");
    CHECK(card["starttime"].asString() == std::to_string(kMsT0));
    CHECK(card["title"].asString() == u8"新标题");
    CHECK(card["desc"].asString() == u8"说明");
}

TEST_CASE("SSWB1 主持人直接新建白板：分配不重复的座位、通知本人、占共享名额、每人至多一块")
{
    ScreenShareManager mgr;
    msConfigure(mgr, 2);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB, kMsC});   //座位 1、2
    ScreenShareSharingResult r;
    REQUIRE(mgr.applyWhiteboard(rid, kMsHost, screenshare_sharing_request, 0, r) == 0);
    CHECK(r.m_changed);
    CHECK(r.m_boardSeat == 3);
    CHECK(r.m_notifyUser == kMsHost);
    CHECK(r.m_publishUser == 0);
    REQUIRE(r.m_room.m_whiteboards.size() == 1);
    CHECK(r.m_room.m_whiteboards[0].m_seat == 3);
    CHECK(r.m_room.m_whiteboards[0].m_ownerId == kMsHost);
    //再申请：已有白板，空操作
    REQUIRE(mgr.applyWhiteboard(rid, kMsHost, screenshare_sharing_request, 0, r) == 0);
    CHECK_FALSE(r.m_changed);
    CHECK(r.m_boardSeat == -1);
    //主持人的屏幕加白板占满两个名额：别人再申请共享屏幕回 731
    CHECK(mgr.applySharing(rid, kMsB, screenshare_sharing_request, 0, r) == error_code_screenshare_sharers_full);
    //新加入的人不会分到白板的座位
    int seat = -1;
    REQUIRE(mgr.joinShare(rid, kMsD, kMsT0 + 2, seat) == 0);
    CHECK(seat == 4);
}

TEST_CASE("SSWB2 其他人申请新建白板：转给主持人、撤回、同意（通知本人带座位）、拒绝、没有申请 732、非主持人 716、名额满 731")
{
    ScreenShareManager mgr;
    msConfigure(mgr, 2);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB, kMsC});
    ScreenShareSharingResult r;
    //B 申请：转给主持人，快照里记着申请
    REQUIRE(mgr.applyWhiteboard(rid, kMsB, screenshare_sharing_request, 0, r) == 0);
    CHECK(r.m_notifyUser == kMsHost);
    CHECK_FALSE(r.m_changed);
    CHECK(msMember(mgr, rid, kMsB).m_boardRequested);
    //B 撤回：转给主持人
    REQUIRE(mgr.applyWhiteboard(rid, kMsB, screenshare_sharing_cancel, 0, r) == 0);
    CHECK(r.m_notifyUser == kMsHost);
    CHECK_FALSE(msMember(mgr, rid, kMsB).m_boardRequested);
    //没有申请时同意：732；非主持人同意：716
    CHECK(mgr.applyWhiteboard(rid, kMsHost, screenshare_sharing_approve, kMsB, r) == error_code_screenshare_no_request);
    REQUIRE(mgr.applyWhiteboard(rid, kMsB, screenshare_sharing_request, 0, r) == 0);
    CHECK(mgr.applyWhiteboard(rid, kMsC, screenshare_sharing_approve, kMsB, r) == error_code_screenshare_not_host);
    //同意：为 B 新建白板，通知 B 并带座位
    REQUIRE(mgr.applyWhiteboard(rid, kMsHost, screenshare_sharing_approve, kMsB, r) == 0);
    CHECK(r.m_changed);
    CHECK(r.m_notifyUser == kMsB);
    CHECK(r.m_subject == kMsB);
    CHECK(r.m_boardSeat == 3);
    CHECK_FALSE(msMember(mgr, rid, kMsB).m_boardRequested);
    //名额已满（主持人的屏幕加 B 的白板）：C 申请白板直接 731
    CHECK(mgr.applyWhiteboard(rid, kMsC, screenshare_sharing_request, 0, r) == error_code_screenshare_sharers_full);
    //上限放宽后 C 申请，主持人拒绝：通知 C
    msConfigure(mgr, 4);
    REQUIRE(mgr.applyWhiteboard(rid, kMsC, screenshare_sharing_request, 0, r) == 0);
    REQUIRE(mgr.applyWhiteboard(rid, kMsHost, screenshare_sharing_reject, kMsC, r) == 0);
    CHECK(r.m_notifyUser == kMsC);
    CHECK_FALSE(r.m_changed);
    CHECK_FALSE(msMember(mgr, rid, kMsC).m_boardRequested);
    //同意时名额已满：申请保留
    REQUIRE(mgr.applyWhiteboard(rid, kMsC, screenshare_sharing_request, 0, r) == 0);
    msConfigure(mgr, 2);
    CHECK(mgr.applyWhiteboard(rid, kMsHost, screenshare_sharing_approve, kMsC, r) == error_code_screenshare_sharers_full);
    CHECK(msMember(mgr, rid, kMsC).m_boardRequested);
}

TEST_CASE("SSWB3 关闭白板：建立者关自己的、主持人关别人的（通知建立者）、别人不能关、没有白板 729；建立者离开时随之关闭、座位复用")
{
    ScreenShareManager mgr;
    msConfigure(mgr, 4);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB, kMsC});
    ScreenShareSharingResult r;
    REQUIRE(mgr.applyWhiteboard(rid, kMsB, screenshare_sharing_request, 0, r) == 0);
    REQUIRE(mgr.applyWhiteboard(rid, kMsHost, screenshare_sharing_approve, kMsB, r) == 0);
    //C 不能关 B 的
    CHECK(mgr.applyWhiteboard(rid, kMsC, screenshare_sharing_stop, kMsB, r) == error_code_screenshare_not_host);
    //C 没有白板：729
    CHECK(mgr.applyWhiteboard(rid, kMsC, screenshare_sharing_stop, 0, r) == error_code_screenshare_not_sharing);
    //主持人关 B 的：通知 B
    REQUIRE(mgr.applyWhiteboard(rid, kMsHost, screenshare_sharing_stop, kMsB, r) == 0);
    CHECK(r.m_changed);
    CHECK(r.m_notifyUser == kMsB);
    CHECK(r.m_subject == kMsB);
    CHECK(r.m_room.m_whiteboards.empty());
    //B 再建、自己关：不另行通知
    REQUIRE(mgr.applyWhiteboard(rid, kMsB, screenshare_sharing_request, 0, r) == 0);
    REQUIRE(mgr.applyWhiteboard(rid, kMsHost, screenshare_sharing_approve, kMsB, r) == 0);
    CHECK(r.m_boardSeat == 3);
    REQUIRE(mgr.applyWhiteboard(rid, kMsB, screenshare_sharing_stop, 0, r) == 0);
    CHECK(r.m_notifyUser == 0);
    //建立者离开房间：白板随之关闭，座位可以复用
    REQUIRE(mgr.applyWhiteboard(rid, kMsC, screenshare_sharing_request, 0, r) == 0);
    REQUIRE(mgr.applyWhiteboard(rid, kMsHost, screenshare_sharing_approve, kMsC, r) == 0);
    CHECK(r.m_boardSeat == 3);
    bool dissolved = false;
    ScreenShareRoom room;
    REQUIRE(mgr.leaveShare(rid, kMsC, dissolved, room) == 0);
    REQUIRE(mgr.getRoom(rid, room));
    CHECK(room.m_whiteboards.empty());
    REQUIRE(mgr.applyWhiteboard(rid, kMsHost, screenshare_sharing_request, 0, r) == 0);
    CHECK(r.m_boardSeat == 2);
}

TEST_CASE("SSWB4 协议：1737 的 kind、白板的 1738、快照里的白板与 boardrequesting")
{
    CHECK(ScreenShareProtocol::parseSharingKind("{\"roomid\":\"ss-1\",\"action\":1}") == screenshare_kind_screen);
    CHECK(ScreenShareProtocol::parseSharingKind("{\"roomid\":\"ss-1\",\"action\":1,\"kind\":2}") ==
          screenshare_kind_whiteboard);
    CHECK(ScreenShareProtocol::parseSharingKind("{\"roomid\":\"ss-1\",\"action\":1,\"kind\":9}") ==
          screenshare_kind_screen);
    CHECK(ScreenShareProtocol::parseSharingKind("not json") == screenshare_kind_screen);
    //同意：带座位
    Json::Value root;
    REQUIRE(msParse(ScreenShareProtocol::buildBoardNotify("ss-1", screenshare_sharing_approve, kMsHost, "zhang", 5), root));
    CHECK(root["kind"].asInt() == screenshare_kind_whiteboard);
    CHECK(root["action"].asInt() == screenshare_sharing_approve);
    CHECK(root["actor"].asString() == "2001");
    CHECK(root["screenseat"].asInt() == 5);
    CHECK_FALSE(root.isMember("token"));
    //拒绝：不带座位
    REQUIRE(msParse(ScreenShareProtocol::buildBoardNotify("ss-1", screenshare_sharing_reject, kMsHost, "zhang", -1), root));
    CHECK_FALSE(root.isMember("screenseat"));
    //快照
    ScreenShareManager mgr;
    msConfigure(mgr, 4);
    const std::string rid = msRoom(mgr, std::vector<int64_t>{kMsB, kMsC});
    ScreenShareSharingResult r;
    REQUIRE(mgr.applyWhiteboard(rid, kMsHost, screenshare_sharing_request, 0, r) == 0);
    REQUIRE(mgr.applyWhiteboard(rid, kMsC, screenshare_sharing_request, 0, r) == 0);
    ScreenShareRoom room;
    REQUIRE(mgr.getRoom(rid, room));
    REQUIRE(msParse(ScreenShareProtocol::buildStateNotify(screenshare_event_sharing_changed, kMsC, room), root));
    REQUIRE(root["whiteboards"].isArray());
    REQUIRE(root["whiteboards"].size() == 1);
    CHECK(root["whiteboards"][0]["seat"].asInt() == 3);
    CHECK(root["whiteboards"][0]["owner"].asString() == "2001");
    bool sawRequest = false;
    for (Json::ArrayIndex i = 0; i < root["members"].size(); ++i)
    {
        if (root["members"][i]["uid"].asString() == "2003")
            sawRequest = root["members"][i].isMember("boardrequesting");
    }
    CHECK(sawRequest);
}
