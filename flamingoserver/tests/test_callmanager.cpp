/**
 *  音视频通话会话管理单元测试（Suite CM），test_callmanager.cpp
 *  覆盖 CallManager 的会话表 CRUD、按角色签发 relay 令牌（用 RelayToken 校验
 *  角色 / uid / peeruid / sessionid 正确）、sessionid 不含令牌分隔符，以及相比
 *  远程桌面新增的两项：isUserBusy（“对方忙”判断）、reapTimedOutSessions（响铃
 *  超时清理）。CallManager 为 Singleton，测试间共享单例，故各用例用独立 userid
 *  并在末尾清理自建会话，断言以 sessionid 精确定位而非依赖全表规模。
 *  balloonwj@qq.com   2026-06-11
 */
#include "doctest.h"
#include "../chatserversrc/CallManager.h"
#include "../chatserversrc/Protocol.h"      // enum error_code（switchMedia 失败码）
#include "../chatserversrc/CallProtocol.h"   // enum call_state / call_media
#include "../utils/RelayToken.h"
#include "../base/Singleton.h"

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
