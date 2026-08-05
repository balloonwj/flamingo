/**
 *  远程桌面中转令牌单元测试（Suite RT），test_relaytoken.cpp
 *  覆盖：令牌签发与校验往返、错误密钥拒绝、过期拒绝（含边界）、payload 篡改
 *       拒绝、签名篡改拒绝、畸形令牌拒绝、字段回填、两端令牌配对语义
 *       （同 sessionid / 相反 role / uid 与 peeruid 对调）。
 *  balloonwj@qq.com   2026-06-08
 */
#include "doctest.h"
#include "../utils/RelayToken.h"

//构造一枚控制端令牌的载荷，便于各用例复用
static RelayTokenPayload makeControllerPayload()
{
    RelayTokenPayload p;
    p.sessionid = "a1b2c3d4-0001";
    p.role      = RelayToken::role_controller;
    p.uid       = 100007;
    p.peeruid   = 200001;
    p.expire    = 5000000000000LL;   //远未到期的时间点
    return p;
}

TEST_CASE("RT1 令牌签发后校验往返一致")
{
    const std::string key = "flamingo-relay-key";
    RelayTokenPayload in = makeControllerPayload();
    std::string token = RelayToken::sign(in, key);
    REQUIRE_FALSE(token.empty());

    RelayTokenPayload out;
    REQUIRE(RelayToken::verify(token, key, 1716268800000LL, out));
    CHECK(out.sessionid == in.sessionid);
    CHECK(out.role == RelayToken::role_controller);
    CHECK(out.uid == in.uid);
    CHECK(out.peeruid == in.peeruid);
    CHECK(out.expire == in.expire);
}

TEST_CASE("RT2 错误密钥校验失败")
{
    std::string token = RelayToken::sign(makeControllerPayload(), "right-key");

    RelayTokenPayload out;
    CHECK_FALSE(RelayToken::verify(token, "wrong-key", 1716268800000LL, out));
    //正确密钥仍能通过
    CHECK(RelayToken::verify(token, "right-key", 1716268800000LL, out));
}

TEST_CASE("RT3 过期令牌被拒（含边界）")
{
    const std::string key = "k";
    RelayTokenPayload in = makeControllerPayload();
    in.expire = 1716268800000LL;   //过期时间
    std::string token = RelayToken::sign(in, key);

    RelayTokenPayload out;
    //当前时间刚好到达过期时间——判定过期
    CHECK_FALSE(RelayToken::verify(token, key, 1716268800000LL, out));
    //当前时间越过过期时间——判定过期
    CHECK_FALSE(RelayToken::verify(token, key, 1716268800001LL, out));
    //当前时间早于过期时间——仍有效
    CHECK(RelayToken::verify(token, key, 1716268799999LL, out));
}

TEST_CASE("RT4 篡改 payload 或签名段后校验失败")
{
    const std::string key = "k";
    std::string token = RelayToken::sign(makeControllerPayload(), key);

    //篡改 payload：把 uid 字段 100007 改成 900007（签名段不变）
    {
        std::string tampered = token;
        std::string::size_type pos = tampered.find("100007");
        REQUIRE(pos != std::string::npos);
        tampered[pos] = '9';

        RelayTokenPayload out;
        CHECK_FALSE(RelayToken::verify(tampered, key, 1716268800000LL, out));
    }
    //篡改签名：翻转签名段最后一个字符
    {
        std::string tampered = token;
        char& last = tampered[tampered.size() - 1];
        last = (last == '0') ? '1' : '0';

        RelayTokenPayload out;
        CHECK_FALSE(RelayToken::verify(tampered, key, 1716268800000LL, out));
    }
}

TEST_CASE("RT5 畸形令牌被拒且不崩溃")
{
    const std::string key = "k";
    RelayTokenPayload out;
    //空串
    CHECK_FALSE(RelayToken::verify("", key, 1716268800000LL, out));
    //无 "." 分隔符
    CHECK_FALSE(RelayToken::verify("nodothere", key, 1716268800000LL, out));
    //仅有分隔符
    CHECK_FALSE(RelayToken::verify(".", key, 1716268800000LL, out));
    //随意字符串
    CHECK_FALSE(RelayToken::verify("garbage.garbage", key, 1716268800000LL, out));
    //payload 字段数不足却带貌似合法的结构
    CHECK_FALSE(RelayToken::verify("a|1|2.abcdef", key, 1716268800000LL, out));
    //sessionid 为空（首字段空）——即便签名自洽也应拒（构造一枚真签名的空会话令牌）
    {
        RelayTokenPayload empty;
        empty.sessionid = "";
        empty.role      = RelayToken::role_controlled;
        empty.uid       = 1;
        empty.peeruid   = 2;
        empty.expire    = 5000000000000LL;
        std::string tok = RelayToken::sign(empty, key);
        CHECK_FALSE(RelayToken::verify(tok, key, 1, out));
    }
}

TEST_CASE("RT6 两端令牌配对语义：同 sessionid、相反 role、uid 与 peeruid 对调")
{
    const std::string key = "pair-key";

    //控制端令牌
    RelayTokenPayload ctrl = makeControllerPayload();   //uid=100007 peeruid=200001
    //被控端令牌：同一会话，角色相反，uid/peeruid 对调
    RelayTokenPayload ctrled;
    ctrled.sessionid = ctrl.sessionid;
    ctrled.role      = RelayToken::role_controlled;
    ctrled.uid       = ctrl.peeruid;
    ctrled.peeruid   = ctrl.uid;
    ctrled.expire    = ctrl.expire;

    RelayTokenPayload a;
    RelayTokenPayload b;
    REQUIRE(RelayToken::verify(RelayToken::sign(ctrl, key),   key, 1, a));
    REQUIRE(RelayToken::verify(RelayToken::sign(ctrled, key), key, 1, b));

    //同一会话
    CHECK(a.sessionid == b.sessionid);
    //角色相反
    CHECK(a.role == RelayToken::role_controller);
    CHECK(b.role == RelayToken::role_controlled);
    //uid 与 peeruid 互为对端
    CHECK(a.uid == b.peeruid);
    CHECK(a.peeruid == b.uid);
}

TEST_CASE("RT7 音视频角色 role_caller / role_callee 签发校验与配对语义")
{
    const std::string key = "av-relay-key";

    //主叫令牌：uid=主叫、peeruid=被叫
    RelayTokenPayload caller;
    caller.sessionid = "710001-720001-1000-1";
    caller.role      = RelayToken::role_caller;
    caller.uid       = 710001;
    caller.peeruid   = 720001;
    caller.expire    = 5000000000000LL;

    //被叫令牌：同会话、角色相反、uid/peeruid 对调
    RelayTokenPayload callee;
    callee.sessionid = caller.sessionid;
    callee.role      = RelayToken::role_callee;
    callee.uid       = caller.peeruid;
    callee.peeruid   = caller.uid;
    callee.expire    = caller.expire;

    RelayTokenPayload a;
    RelayTokenPayload b;
    REQUIRE(RelayToken::verify(RelayToken::sign(caller, key), key, 1, a));
    REQUIRE(RelayToken::verify(RelayToken::sign(callee, key), key, 1, b));

    //角色正确回填
    CHECK(a.role == RelayToken::role_caller);
    CHECK(b.role == RelayToken::role_callee);
    //音视频角色(3/4)与远程桌面角色(1/2)全局不重复：中转层据 role 即可区分会话两端，
    //音视频会话又用独立 sessionid，两类中转不会互相串话
    CHECK(a.role != RelayToken::role_controller);
    CHECK(a.role != RelayToken::role_controlled);
    CHECK(b.role != RelayToken::role_controller);
    CHECK(b.role != RelayToken::role_controlled);
    //同会话、互为对端
    CHECK(a.sessionid == b.sessionid);
    CHECK(a.uid == b.peeruid);
    CHECK(a.peeruid == b.uid);
}
