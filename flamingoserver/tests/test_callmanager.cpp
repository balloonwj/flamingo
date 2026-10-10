/**
 *  音视频通话会话管理单元测试（Suite CM），test_callmanager.cpp
 *  覆盖 CallManager 的会话表 CRUD、按角色签发 relay 令牌（用 RelayToken 校验
 *  角色 / uid / peeruid / sessionid 正确）、sessionid 不含令牌分隔符，以及相比
 *  远程桌面新增的两项：isUserBusy（“对方忙”判断）、reapTimedOutSessions（响铃
 *  超时清理）；第 15 期补 SSRING1（响铃剩余毫秒数，随来电邀请推送下发）。CallManager 为 Singleton，测试间共享单例，故各用例用独立 userid
 *  并在末尾清理自建会话，断言以 sessionid 精确定位而非依赖全表规模。
 *  balloonwj@qq.com   2026-06-11
 */
#include "doctest.h"
#include "../chatserversrc/CallManager.h"
#include "../chatserversrc/Protocol.h"      // enum error_code（switchMedia 失败码）
#include "../chatserversrc/CallProtocol.h"   // enum call_state / call_media
#include "../utils/RelayToken.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"   // SSRING1 解析来电邀请推送（第 15 期）
#include <memory>

TEST_CASE("CM1 createSession 登记会话、状态 ringing、sessionid 不含令牌分隔符")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    std::string sid = mgr.createSession(710001, 720001, call_media_audio, 1000);

    REQUIRE_FALSE(sid.empty());
    //sessionid 进 RelayToken 的 payload，须不含分隔符 '.' 与 '|'
    CHECK(sid.find('.') == std::string::npos);
    CHECK(sid.find('|') == std::string::npos);

    CallSessionInfo s;
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.callerId == 710001);
    CHECK(s.calleeId == 720001);
    CHECK(s.media == call_media_audio);
    CHECK(s.state == call_state_ringing);
    CHECK(s.createTime == 1000);

    mgr.removeSession(sid);
}

TEST_CASE("CM2 removeSession 后查不到")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    std::string sid = mgr.createSession(710002, 720002, call_media_audio, 1000);

    REQUIRE(mgr.removeSession(sid));
    CallSessionInfo s;
    CHECK_FALSE(mgr.getSession(sid, s));
    //再次删除返回 false（本无该会话）
    CHECK_FALSE(mgr.removeSession(sid));
}

TEST_CASE("CM3 不存在的 sessionid：查询 / 改状态 / 签 token 均失败")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    CallSessionInfo s;
    std::string token;
    CHECK_FALSE(mgr.getSession("no-such-call", s));
    CHECK_FALSE(mgr.setState("no-such-call", call_state_ended));
    CHECK_FALSE(mgr.signTokenFor("no-such-call", RelayToken::role_caller, 1000, token));
}

TEST_CASE("CM4 signTokenFor 双方令牌：同会话、相反角色、uid 与 peeruid 对调")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    const std::string key = "call-relay-test-key";
    mgr.configure(key, "192.168.1.100", 20003);
    CHECK(mgr.getRelayHost() == "192.168.1.100");
    CHECK(mgr.getRelayPort() == 20003);

    std::string sid = mgr.createSession(710004, 720004, call_media_audio, 1000);

    std::string tokCaller;
    std::string tokCallee;
    REQUIRE(mgr.signTokenFor(sid, RelayToken::role_caller, 1000, tokCaller));
    REQUIRE(mgr.signTokenFor(sid, RelayToken::role_callee, 1000, tokCallee));

    RelayTokenPayload a;
    RelayTokenPayload b;
    //令牌有效期 5 分钟，now=2000 仍在有效期内
    REQUIRE(RelayToken::verify(tokCaller, key, 2000, a));
    REQUIRE(RelayToken::verify(tokCallee, key, 2000, b));

    //同一会话
    CHECK(a.sessionid == sid);
    CHECK(b.sessionid == sid);
    //角色相反
    CHECK(a.role == RelayToken::role_caller);
    CHECK(b.role == RelayToken::role_callee);
    //主叫令牌 uid=主叫、peeruid=被叫；被叫令牌对调
    CHECK(a.uid == 710004);
    CHECK(a.peeruid == 720004);
    CHECK(b.uid == 720004);
    CHECK(b.peeruid == 710004);

    mgr.removeSession(sid);
}

TEST_CASE("CM5 setState 更新会话状态")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    std::string sid = mgr.createSession(710005, 720005, call_media_audio, 1000);

    REQUIRE(mgr.setState(sid, call_state_connected));
    CallSessionInfo s;
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.state == call_state_connected);

    mgr.removeSession(sid);
}

TEST_CASE("CM6 isUserBusy：会话双方均算忙，他人不忙，移除后不再忙")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    const int64_t caller = 710006;
    const int64_t callee = 720006;
    const int64_t other  = 730006;

    std::string sid = mgr.createSession(caller, callee, call_media_audio, 1000);
    CHECK(mgr.isUserBusy(caller));
    CHECK(mgr.isUserBusy(callee));
    CHECK_FALSE(mgr.isUserBusy(other));

    REQUIRE(mgr.removeSession(sid));
    CHECK_FALSE(mgr.isUserBusy(caller));
    CHECK_FALSE(mgr.isUserBusy(callee));
}

TEST_CASE("CM7 reapTimedOutSessions：响铃超时被清理，未超时/已接通不清理")
{
    CallManager& mgr = Singleton<CallManager>::Instance();

    //（1）响铃超时：到达阈值即被收集并移除
    {
        std::vector<CallSessionInfo> reaped;
        std::string sid = mgr.createSession(710071, 720071, call_media_audio, 5000);
        mgr.reapTimedOutSessions(5000 + CallManager::kRingingTimeoutMs, reaped);

        CallSessionInfo s;
        CHECK_FALSE(mgr.getSession(sid, s));   //已被移除

        //返回列表里能找到该会话且双方 id 正确（用 find 而非 size，避免单例遗留干扰）
        bool found = false;
        for (size_t i = 0; i < reaped.size(); ++i)
        {
            if (reaped[i].sessionid == sid)
            {
                found = true;
                CHECK(reaped[i].callerId == 710071);
                CHECK(reaped[i].calleeId == 720071);
            }
        }
        CHECK(found);
    }

    //（2）未到阈值：不清理
    {
        std::vector<CallSessionInfo> reaped;
        std::string sid = mgr.createSession(710072, 720072, call_media_audio, 5000);
        mgr.reapTimedOutSessions(5000 + CallManager::kRingingTimeoutMs - 1, reaped);

        CallSessionInfo s;
        CHECK(mgr.getSession(sid, s));         //仍在
        mgr.removeSession(sid);
    }

    //（3）已接通：即使早已超过响铃阈值也不清理（通话可长时间进行）
    {
        std::vector<CallSessionInfo> reaped;
        std::string sid = mgr.createSession(710073, 720073, call_media_audio, 5000);
        REQUIRE(mgr.setState(sid, call_state_connected));
        mgr.reapTimedOutSessions(5000 + CallManager::kRingingTimeoutMs * 100, reaped);

        CallSessionInfo s;
        CHECK(mgr.getSession(sid, s));         //仍在
        mgr.removeSession(sid);
    }
}

// =====================================================================
// CM-SM. switchMedia —— 通话中切换媒体类型的校验与状态更新
// =====================================================================

TEST_CASE("CM-SM1 仅已接通态允许切换，其余状态一律拒绝")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    std::string sid = mgr.createSession(1001, 1002, call_media_audio, 1000);

    bool changed = true;
    //新建会话处于响铃中：此时双方还没建立媒体通道，切换没有意义
    CHECK(mgr.switchMedia(sid, 1001, call_media_video, changed) == error_code_call_state_invalid);
    CHECK(changed == false);

    //置为已接通后才放行
    CHECK(mgr.setState(sid, call_state_connected));
    CHECK(mgr.switchMedia(sid, 1001, call_media_video, changed) == 0);
    CHECK(changed == true);

    mgr.removeSession(sid);
}

TEST_CASE("CM-SM2 会话不存在时返回 call_not_exist")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    bool changed = true;
    CHECK(mgr.switchMedia("no-such-session", 1001, call_media_video, changed)
          == error_code_call_not_exist);
    CHECK(changed == false);
}

TEST_CASE("CM-SM3 非通话双方无权切换")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    std::string sid = mgr.createSession(1001, 1002, call_media_audio, 1000);
    CHECK(mgr.setState(sid, call_state_connected));

    bool changed = true;
    //第三方即便拿到 sessionid 也不能切
    CHECK(mgr.switchMedia(sid, 9999, call_media_video, changed) == error_code_call_not_member);
    CHECK(changed == false);

    //双方都可以切
    CHECK(mgr.switchMedia(sid, 1001, call_media_video, changed) == 0);
    CHECK(mgr.switchMedia(sid, 1002, call_media_audio, changed) == 0);

    mgr.removeSession(sid);
}

TEST_CASE("CM-SM4 目标类型与当前相同时幂等：成功但不标记变更")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    std::string sid = mgr.createSession(1001, 1002, call_media_audio, 1000);
    CHECK(mgr.setState(sid, call_state_connected));

    bool changed = true;
    //当前就是语音，再切语音：返回成功但 changed 为 false，调用方据此跳过推送
    CHECK(mgr.switchMedia(sid, 1001, call_media_audio, changed) == 0);
    CHECK(changed == false);

    mgr.removeSession(sid);
}

TEST_CASE("CM-SM5 切换成功后会话记录的媒体类型被更新")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    std::string sid = mgr.createSession(1001, 1002, call_media_audio, 1000);
    CHECK(mgr.setState(sid, call_state_connected));

    bool changed = false;
    CHECK(mgr.switchMedia(sid, 1002, call_media_video, changed) == 0);
    CHECK(changed == true);

    CallSessionInfo info;
    REQUIRE(mgr.getSession(sid, info));
    CHECK(info.media == call_media_video);

    //再降回语音同样落到会话记录上
    CHECK(mgr.switchMedia(sid, 1002, call_media_audio, changed) == 0);
    REQUIRE(mgr.getSession(sid, info));
    CHECK(info.media == call_media_audio);

    mgr.removeSession(sid);
}

TEST_CASE("CM-SM6 双方同时请求：后到者以当前状态为准、不报错")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    std::string sid = mgr.createSession(1001, 1002, call_media_audio, 1000);
    CHECK(mgr.setState(sid, call_state_connected));

    bool changedA = false;
    bool changedB = true;
    //A 先切到视频，真正发生变更
    CHECK(mgr.switchMedia(sid, 1001, call_media_video, changedA) == 0);
    CHECK(changedA == true);
    //B 紧接着也请求切到视频：仍返回成功，但已无变更，不会再推一轮
    CHECK(mgr.switchMedia(sid, 1002, call_media_video, changedB) == 0);
    CHECK(changedB == false);

    mgr.removeSession(sid);
}

TEST_CASE("CM8 getUserCall：会话双方都能查到同一通会话，他人查不到，移除后查不到")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    const int64_t caller = 710008;
    const int64_t callee = 720008;
    const int64_t other  = 730008;

    std::string sid = mgr.createSession(caller, callee, call_media_audio, 1000);
    REQUIRE(mgr.setState(sid, call_state_connected));

    CallSessionInfo info;
    REQUIRE(mgr.getUserCall(caller, info));
    CHECK(info.sessionid == sid);
    CHECK(info.callerId == caller);
    CHECK(info.calleeId == callee);
    CHECK(info.state == call_state_connected);

    CallSessionInfo info2;
    REQUIRE(mgr.getUserCall(callee, info2));
    CHECK(info2.sessionid == sid);

    CallSessionInfo none;
    CHECK_FALSE(mgr.getUserCall(other, none));

    REQUIRE(mgr.removeSession(sid));
    CHECK_FALSE(mgr.getUserCall(caller, none));
}

TEST_CASE("CM9 掉线宽限：最后一个连接断开后超过宽限期才清理，期间重新登录则保留")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    const int64_t caller = 710009;
    const int64_t callee = 720009;
    //宽限期 30 秒（毫秒），与默认配置一致
    const int64_t grace = 30 * 1000;
    const int64_t t0 = 5000000;
    mgr.setOfflineGraceMs(grace);
    CHECK(mgr.offlineGraceMs() == grace);

    std::string sid = mgr.createSession(caller, callee, call_media_audio, t0);
    REQUIRE(mgr.setState(sid, call_state_connected));

    //被叫掉线：宽限期内不清理
    mgr.markUserOffline(callee, t0 + 1000);
    std::vector<CallSessionInfo> reaped;
    mgr.reapOfflineSessions(t0 + 1000 + grace - 1, reaped);
    CHECK(reaped.empty());
    CHECK(mgr.isUserBusy(caller));

    //宽限期内重新登录：掉线时刻清掉，过了原定期限也不清理
    mgr.markUserOnline(callee);
    mgr.reapOfflineSessions(t0 + 1000 + grace * 2, reaped);
    CHECK(reaped.empty());

    //再次掉线：重复调用不刷新掉线时刻（以第一次为准）
    mgr.markUserOffline(callee, t0 + 100000);
    mgr.markUserOffline(callee, t0 + 120000);
    mgr.reapOfflineSessions(t0 + 100000 + grace, reaped);
    REQUIRE(reaped.size() == 1);
    CHECK(reaped[0].sessionid == sid);
    CHECK(reaped[0].callerId == caller);
    CHECK(reaped[0].calleeId == callee);
    CHECK(reaped[0].calleeOfflineAt == t0 + 100000);
    CHECK(reaped[0].callerOfflineAt == 0);
    //清理后会话已不存在，双方都不再忙
    CallSessionInfo info;
    CHECK_FALSE(mgr.getSession(sid, info));
    CHECK_FALSE(mgr.isUserBusy(caller));
    CHECK_FALSE(mgr.isUserBusy(callee));
}

TEST_CASE("CM10 掉线只影响该用户参与的通话；响铃中的通话同样按宽限期清理")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    const int64_t a = 710010;
    const int64_t b = 720010;
    const int64_t c = 730010;
    const int64_t d = 740010;
    //宽限期 30 秒（毫秒）
    const int64_t grace = 30 * 1000;
    const int64_t t0 = 6000000;
    mgr.setOfflineGraceMs(grace);

    std::string ab = mgr.createSession(a, b, call_media_video, t0);
    REQUIRE(mgr.setState(ab, call_state_connected));
    //c 呼叫 d，仍在响铃
    std::string cd = mgr.createSession(c, d, call_media_audio, t0);

    mgr.markUserOffline(a, t0);
    mgr.markUserOffline(c, t0);
    std::vector<CallSessionInfo> reaped;
    mgr.reapOfflineSessions(t0 + grace, reaped);
    REQUIRE(reaped.size() == 2);
    CHECK(((reaped[0].sessionid == ab && reaped[1].sessionid == cd) ||
           (reaped[0].sessionid == cd && reaped[1].sessionid == ab)));

    //没有掉线的人参与的通话不受影响
    std::string bd = mgr.createSession(b, d, call_media_audio, t0 + grace);
    mgr.markUserOffline(a, t0 + grace);
    reaped.clear();
    mgr.reapOfflineSessions(t0 + grace * 3, reaped);
    CHECK(reaped.empty());
    CallSessionInfo info;
    CHECK(mgr.getSession(bd, info));
    mgr.removeSession(bd);
}

TEST_CASE("CM11 宽限期配置：非正数按默认 30 秒处理")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    //默认宽限期（毫秒）
    const int64_t defaultGrace = 30 * 1000;
    mgr.setOfflineGraceMs(0);
    CHECK(mgr.offlineGraceMs() == defaultGrace);
    mgr.setOfflineGraceMs(-5);
    CHECK(mgr.offlineGraceMs() == defaultGrace);
    //其它值原样保存
    mgr.setOfflineGraceMs(3000);
    CHECK(mgr.offlineGraceMs() == 3000);
    mgr.setOfflineGraceMs(defaultGrace);
}

TEST_CASE("SSRING1 来电邀请推送带剩余振铃毫秒数：随时间递减，到时为 0，接通后与不存在时为 -1")
{
    CallManager& mgr = Singleton<CallManager>::Instance();
    const int64_t t0 = 5000;
    std::string sid = mgr.createSession(710091, 720091, call_media_audio, t0);
    CHECK(mgr.ringRemainingMs(sid, t0) == CallManager::kRingingTimeoutMs);
    CHECK(mgr.ringRemainingMs(sid, t0 + 10000) == CallManager::kRingingTimeoutMs - 10000);
    CHECK(mgr.ringRemainingMs(sid, t0 + CallManager::kRingingTimeoutMs) == 0);
    CHECK(mgr.ringRemainingMs(sid, t0 + CallManager::kRingingTimeoutMs + 5000) == 0);
    //本机时钟回拨（now 早于创建时间）：按刚创建算
    CHECK(mgr.ringRemainingMs(sid, t0 - 1000) == CallManager::kRingingTimeoutMs);
    //接通后不再振铃
    REQUIRE(mgr.setState(sid, call_state_connected));
    CHECK(mgr.ringRemainingMs(sid, t0 + 1000) == -1);
    CHECK(mgr.ringRemainingMs("call-none", t0) == -1);
    mgr.removeSession(sid);

    //推送带 ringms；不传时不带该字段
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    const std::string withRing = CallProtocol::buildInviteNotify("call-1", 710091, "a", call_media_audio, 45000);
    REQUIRE(reader->parse(withRing.c_str(), withRing.c_str() + withRing.size(), &root, &errs));
    CHECK(root["ringms"].asInt64() == 45000);
    const std::string noRing = CallProtocol::buildInviteNotify("call-1", 710091, "a", call_media_audio);
    REQUIRE(reader->parse(noRing.c_str(), noRing.c_str() + noRing.size(), &root, &errs));
    CHECK_FALSE(root.isMember("ringms"));
}


TEST_CASE("CM12 markConnected 把状态置为已接通并记下接通时刻；不存在的会话返回 false")
{
    CallManager mgr;
    const std::string sid = mgr.createSession(5127, 5308, call_media_audio, 1000);
    CallSessionInfo s;
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.connectTime == 0);
    CHECK(mgr.markConnected(sid, 2500));
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.state == call_state_connected);
    CHECK(s.connectTime == 2500);
    CHECK_FALSE(mgr.markConnected("no-such-session", 1));
}

TEST_CASE("CM13 setCard 记下通话卡片的 msgid 与 content，会话快照与响铃超时清理收走的快照里都带着")
{
    CallManager mgr;
    const std::string sid = mgr.createSession(5127, 5308, call_media_video, 1000);
    CallSessionInfo s;
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.cardMsgId == 0);
    CHECK(s.cardContent.empty());
    CHECK(mgr.setCard(sid, 777, "{\"kind\":\"call\"}"));
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.cardMsgId == 777);
    CHECK(s.cardContent == "{\"kind\":\"call\"}");
    CHECK_FALSE(mgr.setCard("no-such-session", 1, ""));

    //响铃超时清理收走的会话快照也带着卡片信息（main.cpp 据此改写卡片）
    std::vector<CallSessionInfo> reaped;
    mgr.reapTimedOutSessions(1000 + CallManager::kRingingTimeoutMs, reaped);
    REQUIRE(reaped.size() == 1);
    CHECK(reaped[0].cardMsgId == 777);
}
