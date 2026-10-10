/**
 *  P2P 连接信令编解码单元测试（Suite P2PP），test_p2pprotocol.cpp
 *  覆盖（对应 docs/P2P通信方案.md 第 2 期第 2a 步，2026-10-03 与用户确认的用例）：
 *    - P2PP1 cmd_p2p_register(2301)：version 合法时解析成功，缺失或类型不对时失败；应答带 enabled 与地址探测服务器列表；
 *    - P2PP2 cmd_p2p_session_notify(2302)：各字段齐全，取值与 docs/协议设计-v2.md 第 18 节一致，peeruid 以十进制字符串写出；
 *    - P2PP3 cmd_p2p_signal(2303)：会话号必填，data 必须是 JSON 对象，超过上限报 905；
 *            转给对端的包体原样带上 data，并加上以字符串写出的 fromuid；
 *    - P2PP4 cmd_p2p_result(2304)：ok 必须是布尔值，ok 为真时 path 只接受 1~3，ok 为假时不带路径；
 *    - P2PP5 cmd_p2p_close(2305)：reason 只接受 1~5，errcode 可省略，解析与构造能互相还原；
 *    - P2PP6 cmd_p2p_debug_open(2306)：peeruid 必填且大于 0，字符串与整数两种写法都接受；
 *    - P2PP7 2301 的 purposes（第 5 期 5d 补充）：缺省与空数组都解析为空；数组逐个读出并保留顺序；不是数组、元素不是
 *            字符串、空串、用途名超过 31 字节、超过 16 个时解析失败。
 *  balloonwj@qq.com   2026-10-03
 */
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/P2PProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//把 JSON 文本解析成对象，失败时让用例失败
Json::Value parseJson(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    REQUIRE(ok);
    return root;
}

//示例里的会话号、业务会话号、对端与本人 userid（取自 docs/协议设计-v2.md 第 18 节）
const char* const kSessionId = "p2p-10001-10002-1790990000000-7";
const char* const kFeatureSessionId = "10001-10002-1790990000000-3";
const int64_t kPeerUid = 10002;
const int64_t kSelfUid = 10001;

//示例里的地址探测端口、中转端口与三个超时（毫秒）
const int kProbePort1 = 20004;
const int kProbePort2 = 20005;
const int kRelayPort = 20003;
const int kConnectTimeoutMs = 3000;
const int kKeepaliveMs = 5000;
const int kLinkTimeoutMs = 15000;

//2304 示例里的建链耗时（毫秒）
const int kElapsedMs = 412;

} // namespace

TEST_CASE("P2PP1 cmd_p2p_register：请求解析与应答构造")
{
    int version = 0;
    std::vector<std::string> purposes;
    CHECK(P2PProtocol::parseRegisterRequest("{\"version\":1}", version, purposes));
    CHECK(version == kP2PProtocolVersion);

    //缺失、类型不对、顶层不是对象、不是 JSON：一律失败
    CHECK_FALSE(P2PProtocol::parseRegisterRequest("{}", version, purposes));
    CHECK_FALSE(P2PProtocol::parseRegisterRequest("{\"version\":\"1\"}", version, purposes));
    CHECK_FALSE(P2PProtocol::parseRegisterRequest("{\"version\":1.5}", version, purposes));
    CHECK_FALSE(P2PProtocol::parseRegisterRequest("[1]", version, purposes));
    CHECK_FALSE(P2PProtocol::parseRegisterRequest("not json", version, purposes));

    //应答：enabled 与两个地址探测端口
    std::vector<P2PProbeAddress> probes;
    P2PProbeAddress a;
    a.m_ip = "101.37.25.166";
    a.m_port = kProbePort1;
    probes.push_back(a);
    a.m_port = kProbePort2;
    probes.push_back(a);
    Json::Value resp = parseJson(P2PProtocol::buildRegisterResponse(error_code_ok, "", true, probes));
    CHECK(resp["code"].asInt() == error_code_ok);
    CHECK(resp["enabled"].isBool());
    CHECK(resp["enabled"].asBool());
    REQUIRE(resp["probe"].isArray());
    REQUIRE(resp["probe"].size() == 2);
    CHECK(resp["probe"][0]["ip"].asString() == "101.37.25.166");
    CHECK(resp["probe"][0]["port"].asInt() == kProbePort1);
    CHECK(resp["probe"][1]["port"].asInt() == kProbePort2);

    //企业关闭了 P2P：enabled 为 false，probe 为空数组
    Json::Value off = parseJson(P2PProtocol::buildRegisterResponse(error_code_ok, "", false,
                                                                  std::vector<P2PProbeAddress>()));
    CHECK_FALSE(off["enabled"].asBool());
    REQUIRE(off["probe"].isArray());
    CHECK(off["probe"].size() == 0);

    //版本不受支持：code 为 906
    Json::Value bad = parseJson(P2PProtocol::buildRegisterResponse(error_code_p2p_version_unsupported, "version", false,
                                                                  std::vector<P2PProbeAddress>()));
    CHECK(bad["code"].asInt() == error_code_p2p_version_unsupported);
}

TEST_CASE("P2PP7 cmd_p2p_register 的 purposes：缺省为空，数组逐个读出，类型、个数、长度不合法时失败")
{
    int version = 0;
    std::vector<std::string> purposes(1, "stale");

    //缺省：第 5 期之前的报到包体，用途为空（出参先被清空）
    REQUIRE(P2PProtocol::parseRegisterRequest("{\"version\":1}", version, purposes));
    CHECK(purposes.empty());

    //两个用途，顺序保留；空数组合法、用途为空
    REQUIRE(P2PProtocol::parseRegisterRequest("{\"version\":1,\"purposes\":[\"folder\",\"call\"]}", version, purposes));
    REQUIRE(purposes.size() == 2);
    CHECK(purposes[0] == "folder");
    CHECK(purposes[1] == "call");
    REQUIRE(P2PProtocol::parseRegisterRequest("{\"version\":1,\"purposes\":[]}", version, purposes));
    CHECK(purposes.empty());

    //不是数组、元素不是字符串、空串：失败
    CHECK_FALSE(P2PProtocol::parseRegisterRequest("{\"version\":1,\"purposes\":\"folder\"}", version, purposes));
    CHECK_FALSE(P2PProtocol::parseRegisterRequest("{\"version\":1,\"purposes\":[1]}", version, purposes));
    CHECK_FALSE(P2PProtocol::parseRegisterRequest("{\"version\":1,\"purposes\":[\"\"]}", version, purposes));

    //用途名恰好 kP2PPurposeMaxBytes 字节：成功；多一个字节：失败
    const std::string longest(kP2PPurposeMaxBytes, 'p');
    REQUIRE(P2PProtocol::parseRegisterRequest("{\"version\":1,\"purposes\":[\"" + longest + "\"]}", version, purposes));
    REQUIRE(purposes.size() == 1);
    CHECK(purposes[0] == longest);
    CHECK_FALSE(P2PProtocol::parseRegisterRequest("{\"version\":1,\"purposes\":[\"" + longest + "p\"]}", version,
                                                  purposes));

    //恰好 kP2PMaxPurposes 个：成功；多一个：失败
    std::string list;
    for (size_t i = 0; i < kP2PMaxPurposes; ++i)
        list += (i == 0 ? "\"u" : ",\"u") + std::to_string(i) + "\"";
    REQUIRE(P2PProtocol::parseRegisterRequest("{\"version\":1,\"purposes\":[" + list + "]}", version, purposes));
    CHECK(purposes.size() == kP2PMaxPurposes);
    CHECK_FALSE(P2PProtocol::parseRegisterRequest("{\"version\":1,\"purposes\":[" + list + ",\"extra\"]}", version,
                                                  purposes));
}

TEST_CASE("P2PP2 cmd_p2p_session_notify：各字段齐全，peeruid 以字符串写出")
{
    P2PSessionNotify n;
    n.m_sessionId = kSessionId;
    n.m_role = p2p_role_controlling;
    n.m_purpose = "folder";
    n.m_featureSessionId = kFeatureSessionId;
    n.m_peerUid = kPeerUid;
    n.m_peerDeviceId = "PC-ABC";
    n.m_relayIp = "101.37.25.166";
    n.m_relayPort = kRelayPort;
    n.m_token = "token-text";
    n.m_connectTimeoutMs = kConnectTimeoutMs;
    n.m_keepaliveMs = kKeepaliveMs;
    n.m_linkTimeoutMs = kLinkTimeoutMs;

    Json::Value v = parseJson(P2PProtocol::buildSessionNotify(n));
    CHECK(v["p2psessionid"].asString() == kSessionId);
    CHECK(v["role"].asInt() == p2p_role_controlling);
    CHECK(v["purpose"].asString() == "folder");
    CHECK(v["featuresessionid"].asString() == kFeatureSessionId);
    REQUIRE(v["peeruid"].isString());
    CHECK(v["peeruid"].asString() == "10002");
    CHECK(v["peerdeviceid"].asString() == "PC-ABC");
    CHECK(v["relayip"].asString() == "101.37.25.166");
    CHECK(v["relayport"].asInt() == kRelayPort);
    CHECK(v["token"].asString() == "token-text");
    CHECK(v["connecttimeoutms"].asInt() == kConnectTimeoutMs);
    CHECK(v["keepalivems"].asInt() == kKeepaliveMs);
    CHECK(v["linktimeoutms"].asInt() == kLinkTimeoutMs);
    //不多带字段：十二个字段，与协议文档的示例一一对应
    CHECK(v.getMemberNames().size() == 12);
}

TEST_CASE("P2PP3 cmd_p2p_signal：请求解析、长度上限与转发包体")
{
    P2PSignalRequest req;
    const std::string valid = std::string("{\"p2psessionid\":\"") + kSessionId
                              + "\",\"data\":{\"pubkey\":\"QUJD\",\"candidates\":[{\"type\":1,\"ip\":\"192.168.10.2\",\"port\":54429}]}}";
    CHECK(P2PProtocol::parseSignalRequest(valid, req) == error_code_ok);
    CHECK(req.m_sessionId == kSessionId);
    //data 以紧凑 JSON 文本保存，内容原样
    Json::Value data = parseJson(req.m_data);
    CHECK(data["pubkey"].asString() == "QUJD");
    CHECK(data["candidates"][0]["port"].asInt() == 54429);

    //会话号缺失、为空、不是字符串、超长：报文格式错误
    CHECK(P2PProtocol::parseSignalRequest("{\"data\":{}}", req) == error_code_bad_request);
    CHECK(P2PProtocol::parseSignalRequest("{\"p2psessionid\":\"\",\"data\":{}}", req) == error_code_bad_request);
    CHECK(P2PProtocol::parseSignalRequest("{\"p2psessionid\":7,\"data\":{}}", req) == error_code_bad_request);
    const std::string longId(kP2PSessionIdMaxBytes + 1, 'x');
    CHECK(P2PProtocol::parseSignalRequest("{\"p2psessionid\":\"" + longId + "\",\"data\":{}}", req) == error_code_bad_request);

    //data 缺失或不是对象：报文格式错误
    CHECK(P2PProtocol::parseSignalRequest(std::string("{\"p2psessionid\":\"") + kSessionId + "\"}", req) == error_code_bad_request);
    CHECK(P2PProtocol::parseSignalRequest(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"data\":\"x\"}", req) == error_code_bad_request);
    CHECK(P2PProtocol::parseSignalRequest(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"data\":[1]}", req) == error_code_bad_request);
    CHECK(P2PProtocol::parseSignalRequest("not json", req) == error_code_bad_request);

    //data 序列化后超过上限：905；恰好在上限以内：成功
    //{"k":"..."} 的外壳占 8 个字节，填充串长度据此算出
    const size_t shell = 8;
    const std::string fitPad(kP2PSignalDataMaxBytes - shell, 'a');
    const std::string overPad(kP2PSignalDataMaxBytes - shell + 1, 'a');
    CHECK(P2PProtocol::parseSignalRequest(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"data\":{\"k\":\"" + fitPad + "\"}}", req)
          == error_code_ok);
    CHECK(P2PProtocol::parseSignalRequest(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"data\":{\"k\":\"" + overPad + "\"}}", req)
          == error_code_p2p_signal_too_large);

    //转发给对端：会话号、原样的 data、以字符串写出的 fromuid
    REQUIRE(P2PProtocol::parseSignalRequest(valid, req) == error_code_ok);
    Json::Value fwd = parseJson(P2PProtocol::buildSignalForward(req.m_sessionId, kSelfUid, req.m_data));
    CHECK(fwd["p2psessionid"].asString() == kSessionId);
    REQUIRE(fwd["fromuid"].isString());
    CHECK(fwd["fromuid"].asString() == "10001");
    REQUIRE(fwd["data"].isObject());
    CHECK(fwd["data"] == parseJson(req.m_data));
}

TEST_CASE("P2PP4 cmd_p2p_result：ok 必须是布尔值，路径取值 1~3")
{
    P2PResultReport rep;
    const std::string ok = std::string("{\"p2psessionid\":\"") + kSessionId
                           + "\",\"ok\":true,\"path\":2,\"elapsedms\":412,"
                             "\"pair\":\"192.168.10.2:54429 -> 183.192.103.49:16725\","
                             "\"nat\":{\"samemapping\":true,\"portpreserved\":false}}";
    REQUIRE(P2PProtocol::parseResultReport(ok, rep));
    CHECK(rep.m_sessionId == kSessionId);
    CHECK(rep.m_ok);
    CHECK(rep.m_path == p2p_path_punched);
    CHECK(rep.m_elapsedMs == kElapsedMs);
    CHECK(rep.m_pair == "192.168.10.2:54429 -> 183.192.103.49:16725");
    CHECK(parseJson(rep.m_nat)["samemapping"].asBool());

    //三种路径都接受
    CHECK(P2PProtocol::parseResultReport(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"ok\":true,\"path\":1}", rep));
    CHECK(rep.m_path == p2p_path_lan);
    CHECK(P2PProtocol::parseResultReport(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"ok\":true,\"path\":3}", rep));
    CHECK(rep.m_path == p2p_path_relay);

    //ok 为真时路径必须在 1~3：缺失、0、4 都失败
    CHECK_FALSE(P2PProtocol::parseResultReport(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"ok\":true}", rep));
    CHECK_FALSE(P2PProtocol::parseResultReport(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"ok\":true,\"path\":0}", rep));
    CHECK_FALSE(P2PProtocol::parseResultReport(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"ok\":true,\"path\":4}", rep));

    //ok 为假：不带路径，解析成功且路径为 0；带了路径则失败
    REQUIRE(P2PProtocol::parseResultReport(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"ok\":false,\"elapsedms\":3000}", rep));
    CHECK_FALSE(rep.m_ok);
    CHECK(rep.m_path == 0);
    CHECK_FALSE(P2PProtocol::parseResultReport(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"ok\":false,\"path\":2}", rep));

    //ok 不是布尔值、缺失，会话号缺失，耗时为负：失败
    CHECK_FALSE(P2PProtocol::parseResultReport(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"ok\":1,\"path\":2}", rep));
    CHECK_FALSE(P2PProtocol::parseResultReport(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"path\":2}", rep));
    CHECK_FALSE(P2PProtocol::parseResultReport("{\"ok\":true,\"path\":2}", rep));
    CHECK_FALSE(P2PProtocol::parseResultReport(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"ok\":true,\"path\":2,\"elapsedms\":-1}", rep));
}

TEST_CASE("P2PP5 cmd_p2p_close：原因取值 1~5，解析与构造互相还原")
{
    P2PCloseMessage msg;
    REQUIRE(P2PProtocol::parseClose(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"reason\":1,\"errcode\":0}", msg));
    CHECK(msg.m_sessionId == kSessionId);
    CHECK(msg.m_reason == p2p_close_business_end);
    CHECK(msg.m_errCode == 0);

    //errcode 可省略，按 0 处理
    REQUIRE(P2PProtocol::parseClose(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"reason\":2}", msg));
    CHECK(msg.m_errCode == 0);

    //五种原因都接受，0 与 6 拒绝
    for (int reason = p2p_close_business_end; reason <= p2p_close_self_logout; ++reason)
    {
        P2PCloseMessage m;
        m.m_sessionId = kSessionId;
        m.m_reason = reason;
        m.m_errCode = error_code_p2p_peer_offline;
        P2PCloseMessage back;
        REQUIRE(P2PProtocol::parseClose(P2PProtocol::buildClose(m), back));
        CHECK(back.m_sessionId == m.m_sessionId);
        CHECK(back.m_reason == m.m_reason);
        CHECK(back.m_errCode == m.m_errCode);
    }
    CHECK_FALSE(P2PProtocol::parseClose(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"reason\":0}", msg));
    CHECK_FALSE(P2PProtocol::parseClose(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"reason\":6}", msg));

    //reason 缺失或不是整数、会话号缺失、errcode 不是整数：失败
    CHECK_FALSE(P2PProtocol::parseClose(std::string("{\"p2psessionid\":\"") + kSessionId + "\"}", msg));
    CHECK_FALSE(P2PProtocol::parseClose(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"reason\":\"1\"}", msg));
    CHECK_FALSE(P2PProtocol::parseClose("{\"reason\":1}", msg));
    CHECK_FALSE(P2PProtocol::parseClose(std::string("{\"p2psessionid\":\"") + kSessionId + "\",\"reason\":1,\"errcode\":\"x\"}", msg));
}

TEST_CASE("P2PP6 cmd_p2p_debug_open：peeruid 必填且大于 0")
{
    int64_t peer = 0;
    CHECK(P2PProtocol::parseDebugOpenRequest("{\"peeruid\":\"10002\"}", peer));
    CHECK(peer == kPeerUid);
    CHECK(P2PProtocol::parseDebugOpenRequest("{\"peeruid\":10002}", peer));
    CHECK(peer == kPeerUid);

    CHECK_FALSE(P2PProtocol::parseDebugOpenRequest("{}", peer));
    CHECK_FALSE(P2PProtocol::parseDebugOpenRequest("{\"peeruid\":0}", peer));
    CHECK_FALSE(P2PProtocol::parseDebugOpenRequest("{\"peeruid\":-5}", peer));
    CHECK_FALSE(P2PProtocol::parseDebugOpenRequest("{\"peeruid\":\"abc\"}", peer));
    CHECK_FALSE(P2PProtocol::parseDebugOpenRequest("{\"peeruid\":\"\"}", peer));
    CHECK_FALSE(P2PProtocol::parseDebugOpenRequest("not json", peer));

    //应答：会话号与结果码
    Json::Value ok = parseJson(P2PProtocol::buildDebugOpenResponse(error_code_ok, "", kSessionId));
    CHECK(ok["code"].asInt() == error_code_ok);
    CHECK(ok["p2psessionid"].asString() == kSessionId);
    Json::Value off = parseJson(P2PProtocol::buildDebugOpenResponse(error_code_p2p_debug_disabled, "debug open disabled", ""));
    CHECK(off["code"].asInt() == error_code_p2p_debug_disabled);
    CHECK_FALSE(off.isMember("p2psessionid"));
}
