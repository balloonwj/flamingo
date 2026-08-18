/**
 *  v2 单聊文件夹传输信令编解码（客户端侧）单元测试（Suite FPC），FolderProtocolClientTests.cpp
 *
 *  覆盖 net/FolderProtocolClient 的两侧：
 *    - 请求构造：buildInviteRequest / buildResponse / buildCancel / buildClose / buildRetry
 *      的字段完整性、字段类型、紧凑性、64 位 ID 的字符串编码，以及入参不合契约时返回空串；
 *    - 应答与推送解析：parseInviteResponse（成功 / 规模超限 / 其余失败）、parseSessionResponse、
 *      parseInviteNotify、parseStateNotify、parseRelayInfo 的字段还原；
 *    - 大整数（userid 取 2^53+1、总字节数取 int64 上限）经协议编码往返不丢精度；
 *    - 畸形输入（非法 JSON、空串、缺字段、字段类型不对、取值越界）一律安全失败返回 false。
 *
 *  解析用例的输入不是自己拼的"理想报文"，而是照着服务端 chatserversrc/FolderTransferProtocol.cpp
 *  的构造实现<u>等价重写</u>的报文（见下面 makeServerXxx 一组函数，逐字段对应 buildInviteResponse /
 *  buildInviteLimitResponse / buildInviteNotify / buildSessionResponse / buildStateNotify /
 *  buildRelayInfo）。这样锁住的是两端真正的互通口径，而不只是本模块自己跟自己往返——字段名一旦
 *  跟服务端走偏（典型如 from 写成 fromid、relayip 写成 host），这组用例就会失败。
 *
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-14
 */
#include "catch_amalgamated.hpp"
#include "../net/FolderProtocolClient.h"
#include "../net/Protocol.h"   //folder_transfer_state 与 error_code_folder_* 的具体取值
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace net;

//本组用例反复用到的会话 id，取协议文档 12.2 里的样例值
static const char* const kSessionId = "7f3a2c1b-0004";

//把 JSON 文本解析成 Json::Value，供断言检查构造产物
static bool parseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//把 Json::Value 序列化为紧凑 JSON 文本，供构造测试用的应答 / 推送报文
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//==================== 服务端报文的等价重写 ====================
//以下六个函数逐字段对应服务端 FolderTransferProtocol.cpp 的同名构造函数，字段名、字段类型与
//"哪种情形带哪些字段"的取舍都照抄，用作解析用例的输入。

//对应服务端 buildInviteResponse：成功且会话 id 非空时才带 sessionid 与 state
static std::string makeServerInviteResponse(int code, const std::string& msg,
                                            const std::string& sessionid, int state)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    if (code == 0 && !sessionid.empty())
    {
        root["sessionid"] = sessionid;
        root["state"]     = state;
    }
    return writeCompact(root);
}

//对应服务端 buildInviteLimitResponse：规模超限（609 / 610）时回带上限值，不带 sessionid
static std::string makeServerInviteLimitResponse(int code, const std::string& msg,
                                                 int64_t maxfiles, int64_t maxsize)
{
    Json::Value root;
    root["code"]     = code;
    root["msg"]      = msg;
    root["maxfiles"] = static_cast<Json::Int64>(maxfiles);
    root["maxsize"]  = static_cast<Json::Int64>(maxsize);
    return writeCompact(root);
}

//对应服务端 buildInviteNotify：发送端 userid 的字段名是 from，且以十进制字符串编码
static std::string makeServerInviteNotify(const std::string& sessionid, const std::string& fromid,
                                          const std::string& fromname, const std::string& foldername,
                                          int64_t totaldirs, int64_t totalfiles, int64_t totalsize)
{
    Json::Value root;
    root["sessionid"]  = sessionid;
    root["from"]       = fromid;
    root["fromname"]   = fromname;
    root["foldername"] = foldername;
    root["totaldirs"]  = static_cast<Json::Int64>(totaldirs);
    root["totalfiles"] = static_cast<Json::Int64>(totalfiles);
    root["totalsize"]  = static_cast<Json::Int64>(totalsize);
    return writeCompact(root);
}

//对应服务端 buildSessionResponse：会话 id 非空才带该键（出错应答也带）
static std::string makeServerSessionResponse(int code, const std::string& msg,
                                             const std::string& sessionid)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    if (!sessionid.empty())
        root["sessionid"] = sessionid;
    return writeCompact(root);
}

//对应服务端 buildStateNotify：只有"已失败"态才带有意义的失败原因，其余状态一律置 0 与空串
static std::string makeServerStateNotify(const std::string& sessionid, int state,
                                         int errcode, const std::string& errmsg)
{
    Json::Value root;
    root["sessionid"] = sessionid;
    root["state"]     = state;
    if (state == folder_state_failed)
    {
        root["errcode"] = errcode;
        root["errmsg"]  = errmsg;
    }
    else
    {
        root["errcode"] = 0;
        root["errmsg"]  = "";
    }
    return writeCompact(root);
}

//对应服务端 buildRelayInfo：地址字段名为 relayip / relayport，另带 token / role / lanes
static std::string makeServerRelayInfo(const std::string& sessionid, int role,
                                       const std::string& relayip, int relayport,
                                       const std::string& token, int lanes)
{
    Json::Value root;
    root["sessionid"] = sessionid;
    root["role"]      = role;
    root["relayip"]   = relayip;
    root["relayport"] = relayport;
    root["token"]     = token;
    root["lanes"]     = lanes;
    return writeCompact(root);
}

//==================== 请求构造 ====================

TEST_CASE("FPC1 buildInviteRequest 字段齐全：peerid 走字符串编码、规模统计走 JSON 整数")
{
    std::string json = FolderProtocolClient::buildInviteRequest(
        100002, u8"2026Q3-设计稿", 37, 1284, 3865470976LL);

    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    //peerid 必须是字符串（防大整数精度丢失），值为接收方 userid
    CHECK(root["peerid"].isString());
    CHECK(root["peerid"].asString() == "100002");
    CHECK(root["foldername"].asString() == u8"2026Q3-设计稿");
    //三项规模统计必须是 JSON 整数，且 totalsize 按 64 位读（样例值已超过 32 位）
    CHECK(root["totaldirs"].isIntegral());
    CHECK(root["totaldirs"].asInt64() == 37);
    CHECK(root["totalfiles"].asInt64() == 1284);
    CHECK(root["totalsize"].isInt64());
    CHECK(root["totalsize"].asInt64() == 3865470976LL);
    //紧凑输出：不含换行
    CHECK(json.find('\n') == std::string::npos);
    //空目录（三项统计同时为 0）是合法的
    CHECK_FALSE(FolderProtocolClient::buildInviteRequest(100002, "empty", 0, 0, 0).empty());
}

TEST_CASE("FPC2 buildInviteRequest 入参不合契约时返回空串")
{
    //接收方 userid 非正
    CHECK(FolderProtocolClient::buildInviteRequest(0, "f", 1, 1, 1).empty());
    CHECK(FolderProtocolClient::buildInviteRequest(-1, "f", 1, 1, 1).empty());
    //根目录名为空：接收端无从建根目录
    CHECK(FolderProtocolClient::buildInviteRequest(100002, "", 1, 1, 1).empty());
    //规模统计为负
    CHECK(FolderProtocolClient::buildInviteRequest(100002, "f", -1, 1, 1).empty());
    CHECK(FolderProtocolClient::buildInviteRequest(100002, "f", 1, -1, 1).empty());
    CHECK(FolderProtocolClient::buildInviteRequest(100002, "f", 1, 1, -1).empty());
}

TEST_CASE("FPC3 buildResponse 接收 / 拒绝两态，accept 为整数 1 / 0")
{
    //接收
    {
        std::string json = FolderProtocolClient::buildResponse(kSessionId, true);
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["sessionid"].asString() == kSessionId);
        CHECK(root["accept"].isInt());
        CHECK(root["accept"].asInt() == 1);
    }
    //拒绝
    {
        std::string json = FolderProtocolClient::buildResponse(kSessionId, false);
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["accept"].asInt() == 0);
    }
    //缺会话 id 的应答服务端无从定位是哪一次传输，不应发出
    CHECK(FolderProtocolClient::buildResponse("", true).empty());
}

TEST_CASE("FPC4 buildCancel / buildRetry 只带 sessionid")
{
    {
        std::string json = FolderProtocolClient::buildCancel(kSessionId);
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["sessionid"].asString() == kSessionId);
        //撤回请求只有会话 id 一个字段
        CHECK(root.getMemberNames().size() == 1);
    }
    {
        std::string json = FolderProtocolClient::buildRetry(kSessionId);
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["sessionid"].asString() == kSessionId);
        CHECK(root.getMemberNames().size() == 1);
    }
    CHECK(FolderProtocolClient::buildCancel("").empty());
    CHECK(FolderProtocolClient::buildRetry("").empty());
}

TEST_CASE("FPC5 buildClose 成功结束：result 为 0，且不携带失败原因")
{
    //即使调用方误传了失败原因，成功结束也一律写 0 与空串——服务端对"成功却带 errcode"判非法请求
    std::string json = FolderProtocolClient::buildClose(kSessionId, true,
                                                        error_code_folder_write_fail, "ignored");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["sessionid"].asString() == kSessionId);
    CHECK(root["result"].isInt());
    CHECK(root["result"].asInt() == 0);
    CHECK(root["errcode"].asInt() == 0);
    CHECK(root["errmsg"].asString().empty());
}

TEST_CASE("FPC6 buildClose 失败结束：result 为 1，原样带失败原因；越界错误码返回空串")
{
    {
        std::string json = FolderProtocolClient::buildClose(kSessionId, false,
                                                            error_code_folder_write_fail,
                                                            "write file failed");
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["result"].asInt() == 1);
        CHECK(root["errcode"].asInt() == error_code_folder_write_fail);
        CHECK(root["errmsg"].asString() == "write file failed");
    }
    //errmsg 允许为空
    {
        std::string json = FolderProtocolClient::buildClose(kSessionId, false,
                                                            error_code_folder_canceled, "");
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["errcode"].asInt() == error_code_folder_canceled);
        CHECK(root["errmsg"].asString().empty());
    }
    //失败原因必须落在 606~620 之内，越界的包服务端必然判非法请求
    CHECK(FolderProtocolClient::buildClose(kSessionId, false, 0, "").empty());
    CHECK(FolderProtocolClient::buildClose(kSessionId, false, 605, "").empty());
    CHECK(FolderProtocolClient::buildClose(kSessionId, false, 621, "").empty());
    //缺会话 id
    CHECK(FolderProtocolClient::buildClose("", true, 0, "").empty());
}

//==================== 应答解析 ====================

TEST_CASE("FPC7 parseInviteResponse 成功应答取会话 id 与初始状态")
{
    std::string body = makeServerInviteResponse(0, "ok", kSessionId, folder_state_ringing);

    FolderInviteResponse resp;
    REQUIRE(FolderProtocolClient::parseInviteResponse(body, resp));
    CHECK(resp.m_code == 0);
    CHECK(resp.m_msg == "ok");
    CHECK(resp.m_sessionId == kSessionId);
    CHECK(resp.m_state == folder_state_ringing);
    //成功应答不带上限值
    CHECK(resp.m_maxFiles == 0);
    CHECK(resp.m_maxSize == 0);
}

TEST_CASE("FPC8 parseInviteResponse 规模超限应答取回服务端配置的上限值")
{
    //文件数超限：上限值为服务端配置 foldermaxfiles / foldermaxsize 的默认值
    {
        std::string body = makeServerInviteLimitResponse(
            error_code_folder_too_many_files, "too many files", 10000, 53687091200LL);

        FolderInviteResponse resp;
        REQUIRE(FolderProtocolClient::parseInviteResponse(body, resp));
        CHECK(resp.m_code == error_code_folder_too_many_files);
        CHECK(resp.m_maxFiles == 10000);
        //50 GiB 超出 32 位，须按 64 位取回
        CHECK(resp.m_maxSize == 53687091200LL);
        //超限应答不带会话 id
        CHECK(resp.m_sessionId.empty());
    }
    //总字节数超限
    {
        std::string body = makeServerInviteLimitResponse(
            error_code_folder_too_large, "folder too large", 10000, 53687091200LL);

        FolderInviteResponse resp;
        REQUIRE(FolderProtocolClient::parseInviteResponse(body, resp));
        CHECK(resp.m_code == error_code_folder_too_large);
        CHECK(resp.m_maxSize == 53687091200LL);
    }
}

TEST_CASE("FPC9 parseInviteResponse 其余失败只有 code / msg")
{
    std::string body = makeServerInviteResponse(
        error_code_folder_peer_offline, "peer offline", "", 0);

    FolderInviteResponse resp;
    REQUIRE(FolderProtocolClient::parseInviteResponse(body, resp));
    CHECK(resp.m_code == error_code_folder_peer_offline);
    CHECK(resp.m_msg == "peer offline");
    CHECK(resp.m_sessionId.empty());
    CHECK(resp.m_state == 0);
}

TEST_CASE("FPC10 parseSessionResponse 是 1812 / 1813 / 1814 / 1817 四条命令的通用应答")
{
    //成功应答原样带回会话 id
    {
        FolderSessionResponse resp;
        REQUIRE(FolderProtocolClient::parseSessionResponse(
            makeServerSessionResponse(0, "ok", kSessionId), resp));
        CHECK(resp.m_code == 0);
        CHECK(resp.m_msg == "ok");
        CHECK(resp.m_sessionId == kSessionId);
    }
    //出错应答同样带回会话 id（会话已超出保留期，不能再重试）
    {
        FolderSessionResponse resp;
        REQUIRE(FolderProtocolClient::parseSessionResponse(
            makeServerSessionResponse(error_code_folder_session_expired,
                                      "folder session expired", kSessionId), resp));
        CHECK(resp.m_code == error_code_folder_session_expired);
        CHECK(resp.m_sessionId == kSessionId);
    }
    //请求格式非法时服务端解析不出会话 id，应答里没有该字段
    {
        FolderSessionResponse resp;
        REQUIRE(FolderProtocolClient::parseSessionResponse(
            makeServerSessionResponse(error_code_bad_request, "bad request", ""), resp));
        CHECK(resp.m_code == error_code_bad_request);
        CHECK(resp.m_sessionId.empty());
    }
}

//==================== 推送解析 ====================

TEST_CASE("FPC11 parseInviteNotify 取发送方与规模统计，userid 字段名为 from")
{
    std::string body = makeServerInviteNotify(kSessionId, "100001", u8"谢知行",
                                              u8"2026Q3-设计稿", 37, 1284, 3865470976LL);

    FolderInviteNotify notify;
    REQUIRE(FolderProtocolClient::parseInviteNotify(body, notify));
    CHECK(notify.m_sessionId == kSessionId);
    CHECK(notify.m_fromId == 100001);
    CHECK(notify.m_fromName == u8"谢知行");
    CHECK(notify.m_folderName == u8"2026Q3-设计稿");
    CHECK(notify.m_totalDirs == 37);
    CHECK(notify.m_totalFiles == 1284);
    CHECK(notify.m_totalSize == 3865470976LL);

    //字段名走偏就取不到发送方：远程桌面那条推送用的是 fromid，文件夹这条是 from
    Json::Value wrong;
    REQUIRE(parseJsonText(body, wrong));
    wrong["fromid"] = wrong["from"];
    wrong.removeMember("from");
    FolderInviteNotify bad;
    CHECK_FALSE(FolderProtocolClient::parseInviteNotify(writeCompact(wrong), bad));
}

TEST_CASE("FPC12 parseStateNotify 已失败态取回失败原因，其余状态失败原因恒为 0")
{
    //已失败：errcode 由服务端从 1814 原样透传，两端看到同一条原因
    {
        std::string body = makeServerStateNotify(kSessionId, folder_state_failed,
                                                 error_code_folder_relay_broken,
                                                 "relay lane closed");
        FolderStateNotify st;
        REQUIRE(FolderProtocolClient::parseStateNotify(body, st));
        CHECK(st.m_sessionId == kSessionId);
        CHECK(st.m_state == folder_state_failed);
        CHECK(st.m_errCode == error_code_folder_relay_broken);
        CHECK(st.m_errMsg == "relay lane closed");
    }
    //已拒绝：非失败态，服务端一律把失败原因置 0 与空串
    {
        std::string body = makeServerStateNotify(kSessionId, folder_state_rejected,
                                                 error_code_folder_write_fail, "ignored");
        FolderStateNotify st;
        REQUIRE(FolderProtocolClient::parseStateNotify(body, st));
        CHECK(st.m_state == folder_state_rejected);
        CHECK(st.m_errCode == 0);
        CHECK(st.m_errMsg.empty());
    }
    //已完成
    {
        FolderStateNotify st;
        REQUIRE(FolderProtocolClient::parseStateNotify(
            makeServerStateNotify(kSessionId, folder_state_completed, 0, ""), st));
        CHECK(st.m_state == folder_state_completed);
    }
    //状态越界（协议只有 1~8）：客户端无从映射成气泡样式，按非法推送丢弃
    {
        FolderStateNotify st;
        CHECK_FALSE(FolderProtocolClient::parseStateNotify(
            makeServerStateNotify(kSessionId, 0, 0, ""), st));
        CHECK_FALSE(FolderProtocolClient::parseStateNotify(
            makeServerStateNotify(kSessionId, 9, 0, ""), st));
    }
}

TEST_CASE("FPC13 parseRelayInfo 地址字段名为 relayip / relayport，不是 host / port")
{
    const char* const kToken = "7f3a2c1b-0004|5|100001|100002|1716272400000.5d41402abc4b2a76";
    std::string body = makeServerRelayInfo(kSessionId, folder_role_sender,
                                           "192.168.1.8", 20001, kToken, 4);

    //报文里用的就是 relayip / relayport 这两个名字，没有 host / port
    Json::Value raw;
    REQUIRE(parseJsonText(body, raw));
    CHECK(raw.isMember("relayip"));
    CHECK(raw.isMember("relayport"));
    CHECK_FALSE(raw.isMember("host"));
    CHECK_FALSE(raw.isMember("port"));

    FolderRelayInfo info;
    REQUIRE(FolderProtocolClient::parseRelayInfo(body, info));
    CHECK(info.m_sessionId == kSessionId);
    CHECK(info.m_role == folder_role_sender);
    CHECK(info.m_relayIp == "192.168.1.8");
    CHECK(info.m_relayPort == 20001);
    CHECK(info.m_token == kToken);
    CHECK(info.m_lanes == 4);

    //接收端那一帧只有角色与令牌不同
    {
        FolderRelayInfo recvInfo;
        REQUIRE(FolderProtocolClient::parseRelayInfo(
            makeServerRelayInfo(kSessionId, folder_role_receiver, "192.168.1.8", 20001,
                                "another-token", 4), recvInfo));
        CHECK(recvInfo.m_role == folder_role_receiver);
    }

    //把地址字段名换成 host / port 就取不到中转地址，解析必须失败
    {
        Json::Value renamed;
        REQUIRE(parseJsonText(body, renamed));
        renamed["host"] = renamed["relayip"];
        renamed["port"] = renamed["relayport"];
        renamed.removeMember("relayip");
        renamed.removeMember("relayport");
        FolderRelayInfo bad;
        CHECK_FALSE(FolderProtocolClient::parseRelayInfo(writeCompact(renamed), bad));
    }

    //角色不是文件夹传输这两个（这里给的是音视频主叫 3）：说明下发串了会话
    {
        FolderRelayInfo bad;
        CHECK_FALSE(FolderProtocolClient::parseRelayInfo(
            makeServerRelayInfo(kSessionId, 3, "192.168.1.8", 20001, "t", 4), bad));
    }
    //数据通道条数为 0：一个字节也传不了
    {
        FolderRelayInfo bad;
        CHECK_FALSE(FolderProtocolClient::parseRelayInfo(
            makeServerRelayInfo(kSessionId, folder_role_sender, "192.168.1.8", 20001, "t", 0), bad));
    }
    //端口越界
    {
        FolderRelayInfo bad;
        CHECK_FALSE(FolderProtocolClient::parseRelayInfo(
            makeServerRelayInfo(kSessionId, folder_role_sender, "192.168.1.8", 0, "t", 4), bad));
        CHECK_FALSE(FolderProtocolClient::parseRelayInfo(
            makeServerRelayInfo(kSessionId, folder_role_sender, "192.168.1.8", 65536, "t", 4), bad));
    }
}

//==================== 大整数与畸形输入 ====================

TEST_CASE("FPC14 大整数往返不丢精度：userid 取 2^53+1、总字节数取 int64 上限")
{
    //该 userid 超过 2^53，若以 JSON number 承载会被双精度截断
    const int64_t kBigUserId = 9007199254740993LL;
    //int64 上限，用于验证字节数按 64 位读写
    const int64_t kMaxInt64 = 9223372036854775807LL;

    //构造侧：peerid 为字符串且与原值逐字符一致，totalsize 按 64 位整数写
    {
        std::string json = FolderProtocolClient::buildInviteRequest(
            kBigUserId, "big", 0, 1, kMaxInt64);
        Json::Value root;
        REQUIRE(parseJsonText(json, root));
        CHECK(root["peerid"].asString() == "9007199254740993");
        CHECK(root["totalsize"].isInt64());
        CHECK(root["totalsize"].asInt64() == kMaxInt64);
    }

    //解析侧：邀请推送的 from 与 totalsize 都能精确还原
    {
        std::string body = makeServerInviteNotify(kSessionId, "9007199254740993", "n", "f",
                                                  0, 1, kMaxInt64);
        FolderInviteNotify notify;
        REQUIRE(FolderProtocolClient::parseInviteNotify(body, notify));
        CHECK(notify.m_fromId == kBigUserId);
        CHECK(notify.m_totalSize == kMaxInt64);
    }

    //解析侧：规模超限应答里的上限值同样按 64 位取回
    {
        FolderInviteResponse resp;
        REQUIRE(FolderProtocolClient::parseInviteResponse(
            makeServerInviteLimitResponse(error_code_folder_too_large, "too large",
                                          kMaxInt64, kMaxInt64), resp));
        CHECK(resp.m_maxFiles == kMaxInt64);
        CHECK(resp.m_maxSize == kMaxInt64);
    }
}

TEST_CASE("FPC15 非法 JSON 与空串一律安全失败")
{
    FolderInviteResponse resp;
    CHECK_FALSE(FolderProtocolClient::parseInviteResponse("", resp));
    CHECK_FALSE(FolderProtocolClient::parseInviteResponse("not json", resp));
    //顶层不是对象
    CHECK_FALSE(FolderProtocolClient::parseInviteResponse("[1,2,3]", resp));

    FolderSessionResponse sresp;
    CHECK_FALSE(FolderProtocolClient::parseSessionResponse("", sresp));
    CHECK_FALSE(FolderProtocolClient::parseSessionResponse("{", sresp));

    FolderInviteNotify notify;
    CHECK_FALSE(FolderProtocolClient::parseInviteNotify("", notify));
    CHECK_FALSE(FolderProtocolClient::parseInviteNotify("[]", notify));

    FolderStateNotify st;
    CHECK_FALSE(FolderProtocolClient::parseStateNotify("", st));
    CHECK_FALSE(FolderProtocolClient::parseStateNotify("garbage", st));

    FolderRelayInfo info;
    CHECK_FALSE(FolderProtocolClient::parseRelayInfo("", info));
    CHECK_FALSE(FolderProtocolClient::parseRelayInfo("null", info));
}

TEST_CASE("FPC16 缺关键字段一律返回 false")
{
    //邀请应答缺 code
    {
        FolderInviteResponse resp;
        CHECK_FALSE(FolderProtocolClient::parseInviteResponse("{\"msg\":\"ok\"}", resp));
    }
    //成功的邀请应答缺 sessionid：后续信令无从发起
    {
        FolderInviteResponse resp;
        CHECK_FALSE(FolderProtocolClient::parseInviteResponse("{\"code\":0,\"msg\":\"ok\"}", resp));
    }
    //通用应答缺 code
    {
        FolderSessionResponse resp;
        CHECK_FALSE(FolderProtocolClient::parseSessionResponse(
            "{\"sessionid\":\"7f3a2c1b-0004\"}", resp));
    }
    //邀请推送分别缺 sessionid / from / foldername / 规模统计
    {
        FolderInviteNotify notify;
        CHECK_FALSE(FolderProtocolClient::parseInviteNotify(
            "{\"from\":\"1\",\"foldername\":\"f\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0}",
            notify));
        CHECK_FALSE(FolderProtocolClient::parseInviteNotify(
            "{\"sessionid\":\"s\",\"foldername\":\"f\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0}",
            notify));
        CHECK_FALSE(FolderProtocolClient::parseInviteNotify(
            "{\"sessionid\":\"s\",\"from\":\"1\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0}",
            notify));
        CHECK_FALSE(FolderProtocolClient::parseInviteNotify(
            "{\"sessionid\":\"s\",\"from\":\"1\",\"foldername\":\"f\",\"totalfiles\":0,\"totalsize\":0}",
            notify));
    }
    //状态推送缺 sessionid / 缺 state
    {
        FolderStateNotify st;
        CHECK_FALSE(FolderProtocolClient::parseStateNotify("{\"state\":7}", st));
        CHECK_FALSE(FolderProtocolClient::parseStateNotify(
            "{\"sessionid\":\"7f3a2c1b-0004\"}", st));
    }
    //中转参数缺 token / 缺 sessionid
    {
        FolderRelayInfo info;
        CHECK_FALSE(FolderProtocolClient::parseRelayInfo(
            "{\"sessionid\":\"s\",\"role\":5,\"relayip\":\"1.2.3.4\",\"relayport\":1,\"lanes\":4}",
            info));
        CHECK_FALSE(FolderProtocolClient::parseRelayInfo(
            "{\"role\":5,\"relayip\":\"1.2.3.4\",\"relayport\":1,\"token\":\"t\",\"lanes\":4}",
            info));
    }
}

TEST_CASE("FPC17 字段类型不对一律安全失败，且不抛异常")
{
    //code 是字符串而不是整数
    {
        FolderInviteResponse resp;
        CHECK_FALSE(FolderProtocolClient::parseInviteResponse(
            "{\"code\":\"0\",\"msg\":\"ok\",\"sessionid\":\"s\"}", resp));
    }
    //msg 是对象：不得因 asString() 抛异常而中断收包流程，取到空串即可
    {
        FolderSessionResponse resp;
        REQUIRE(FolderProtocolClient::parseSessionResponse(
            "{\"code\":0,\"msg\":{\"a\":1},\"sessionid\":\"s\"}", resp));
        CHECK(resp.m_code == 0);
        CHECK(resp.m_msg.empty());
        CHECK(resp.m_sessionId == "s");
    }
    //sessionid 是数组
    {
        FolderStateNotify st;
        CHECK_FALSE(FolderProtocolClient::parseStateNotify(
            "{\"sessionid\":[1],\"state\":7,\"errcode\":0,\"errmsg\":\"\"}", st));
    }
    //规模统计是浮点 / 字符串：与服务端同口径，必须是 JSON 整数
    {
        FolderInviteNotify notify;
        CHECK_FALSE(FolderProtocolClient::parseInviteNotify(
            "{\"sessionid\":\"s\",\"from\":\"1\",\"foldername\":\"f\","
            "\"totaldirs\":1.5,\"totalfiles\":0,\"totalsize\":0}", notify));
        CHECK_FALSE(FolderProtocolClient::parseInviteNotify(
            "{\"sessionid\":\"s\",\"from\":\"1\",\"foldername\":\"f\","
            "\"totaldirs\":\"1\",\"totalfiles\":0,\"totalsize\":0}", notify));
        //规模统计为负
        CHECK_FALSE(FolderProtocolClient::parseInviteNotify(
            "{\"sessionid\":\"s\",\"from\":\"1\",\"foldername\":\"f\","
            "\"totaldirs\":-1,\"totalfiles\":0,\"totalsize\":0}", notify));
    }
    //发送方 userid 是布尔值，取不到有效 userid
    {
        FolderInviteNotify notify;
        CHECK_FALSE(FolderProtocolClient::parseInviteNotify(
            "{\"sessionid\":\"s\",\"from\":true,\"foldername\":\"f\","
            "\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0}", notify));
    }
    //中转参数的 relayport 是字符串
    {
        FolderRelayInfo info;
        CHECK_FALSE(FolderProtocolClient::parseRelayInfo(
            "{\"sessionid\":\"s\",\"role\":5,\"relayip\":\"1.2.3.4\","
            "\"relayport\":\"20001\",\"token\":\"t\",\"lanes\":4}", info));
    }
}

TEST_CASE("FPC18 isValidState / isValidErrorCode 的上下界与协议表一致")
{
    //会话状态 1~8
    CHECK_FALSE(FolderProtocolClient::isValidState(0));
    CHECK(FolderProtocolClient::isValidState(folder_state_ringing));
    CHECK(FolderProtocolClient::isValidState(folder_state_transferring));
    CHECK(FolderProtocolClient::isValidState(folder_state_completed));
    CHECK_FALSE(FolderProtocolClient::isValidState(folder_state_completed + 1));

    //文件域错误码 606~620
    CHECK_FALSE(FolderProtocolClient::isValidErrorCode(error_code_folder_rejected - 1));
    CHECK(FolderProtocolClient::isValidErrorCode(error_code_folder_rejected));
    CHECK(FolderProtocolClient::isValidErrorCode(error_code_folder_canceled));
    CHECK(FolderProtocolClient::isValidErrorCode(error_code_folder_busy));
    CHECK_FALSE(FolderProtocolClient::isValidErrorCode(error_code_folder_busy + 1));
}

// ===================================================================
// 传输记录条（msgtype = message_type_folder = 18）的 content 编解码
//
// 记录条由<u>服务端</u>生成（客户端只收不发），故这一组解析用例的输入同样照着服务端
// chatserversrc/FolderTransferProtocol::buildRecordContent 的字段名逐字写死：
// sessionid / foldername / totaldirs / totalfiles / totalsize / result / errcode。
// 字段名一旦与服务端走偏，这几条用例就会失败。
// ===================================================================

namespace {

//照服务端 buildRecordContent 的字段口径拼一份记录条 content。
//errcode 原样写入（不在这里归一），好让用例能验证解析侧自己会归一。
static std::string makeServerRecordContent(const std::string& sessionId,
                                           const std::string& folderName,
                                           long long totalDirs, long long totalFiles,
                                           long long totalSize, int result, int errCode)
{
    Json::Value root;
    root["sessionid"]  = sessionId;
    root["foldername"] = folderName;
    root["totaldirs"]  = static_cast<Json::Int64>(totalDirs);
    root["totalfiles"] = static_cast<Json::Int64>(totalFiles);
    root["totalsize"]  = static_cast<Json::Int64>(totalSize);
    root["result"]     = result;
    root["errcode"]    = errCode;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, root);
}

} // namespace

TEST_CASE("FPC19 isTerminalState 只认 3~8，不认响铃中与传输中")
{
    //过程状态：会话还没有结局，不该据此落记录
    CHECK_FALSE(FolderProtocolClient::isTerminalState(0));
    CHECK_FALSE(FolderProtocolClient::isTerminalState(folder_state_ringing));
    CHECK_FALSE(FolderProtocolClient::isTerminalState(folder_state_transferring));

    //六种终态
    CHECK(FolderProtocolClient::isTerminalState(folder_state_rejected));
    CHECK(FolderProtocolClient::isTerminalState(folder_state_canceled));
    CHECK(FolderProtocolClient::isTerminalState(folder_state_peer_offline));
    CHECK(FolderProtocolClient::isTerminalState(folder_state_timeout));
    CHECK(FolderProtocolClient::isTerminalState(folder_state_failed));
    CHECK(FolderProtocolClient::isTerminalState(folder_state_completed));

    //越界
    CHECK_FALSE(FolderProtocolClient::isTerminalState(folder_state_completed + 1));
}

TEST_CASE("FPC20 记录条 content 的构造：七个字段齐全、不含文件清单")
{
    FolderRecordContent rec;
    rec.m_sessionId  = "sess-8811";
    rec.m_folderName = u8"项目资料";
    rec.m_totalDirs  = 12;
    rec.m_totalFiles = 128;
    rec.m_totalSize  = 2576980378LL;
    rec.m_result     = folder_state_completed;
    rec.m_errCode    = 0;

    const std::string body = FolderProtocolClient::buildRecordContent(rec);
    REQUIRE_FALSE(body.empty());

    Json::Value root;
    REQUIRE(parseJsonText(body, root));
    CHECK(root["sessionid"].asString() == "sess-8811");
    CHECK(root["foldername"].asString() == u8"项目资料");
    CHECK(root["totaldirs"].asInt64() == 12);
    CHECK(root["totalfiles"].asInt64() == 128);
    CHECK(root["totalsize"].asInt64() == 2576980378LL);
    CHECK(root["result"].asInt() == folder_state_completed);
    CHECK(root["errcode"].asInt() == 0);

    //只有这七个字段：一旦有人往里加文件清单 / 相对路径 / MD5，这条会立刻失败 ——
    //“服务端记动作、不记内容”这条不变式必须由用例守住
    CHECK(root.getMemberNames().size() == 7);
    CHECK_FALSE(root.isMember("files"));
    CHECK_FALSE(root.isMember("md5"));

    //紧凑格式：不带缩进与换行
    CHECK(body.find('\n') == std::string::npos);
}

TEST_CASE("FPC21 记录条 content 往返：六种终态各自还原一致")
{
    const int kStates[] =
    {
        folder_state_rejected,
        folder_state_canceled,
        folder_state_peer_offline,
        folder_state_timeout,
        folder_state_failed,
        folder_state_completed,
    };
    const int kStateCount = (int)(sizeof(kStates) / sizeof(kStates[0]));

    for (int i = 0; i < kStateCount; ++i)
    {
        FolderRecordContent rec;
        rec.m_sessionId  = "sess-round";
        rec.m_folderName = u8"设计稿 2026";
        rec.m_totalDirs  = 3;
        rec.m_totalFiles = 40;
        rec.m_totalSize  = 1048576LL;
        rec.m_result     = kStates[i];
        //失败态才带原因；其余结局故意也填一个，验证构造侧会把它归一成 0
        rec.m_errCode    = error_code_folder_write_fail;

        const std::string body = FolderProtocolClient::buildRecordContent(rec);
        REQUIRE_FALSE(body.empty());

        FolderRecordContent back;
        REQUIRE(FolderProtocolClient::parseRecordContent(body, back));
        CHECK(back.m_sessionId == rec.m_sessionId);
        CHECK(back.m_folderName == rec.m_folderName);
        CHECK(back.m_totalDirs == rec.m_totalDirs);
        CHECK(back.m_totalFiles == rec.m_totalFiles);
        CHECK(back.m_totalSize == rec.m_totalSize);
        CHECK(back.m_result == kStates[i]);

        if (kStates[i] == folder_state_failed)
        {
            CHECK(back.m_errCode == error_code_folder_write_fail);
        }
        else
        {
            //非失败结局：构造侧已把 errcode 归一成 0，往返回来自然也是 0
            CHECK(back.m_errCode == 0);
        }
    }
}

TEST_CASE("FPC22 记录条 content 解析：errcode 仅失败态有意义，其余结局一律归一成 0")
{
    //服务端那一侧即便漏了归一（报文里非失败结局却带着错误码），解析侧也要抹掉它 ——
    //否则界面会把一条本不该存在的失败原因显示出来
    {
        FolderRecordContent out;
        REQUIRE(FolderProtocolClient::parseRecordContent(
            makeServerRecordContent("s1", u8"资料", 1, 2, 3,
                                    folder_state_completed, error_code_folder_write_fail), out));
        CHECK(out.m_result == folder_state_completed);
        CHECK(out.m_errCode == 0);
    }
    //失败态：错误码原样保留
    {
        FolderRecordContent out;
        REQUIRE(FolderProtocolClient::parseRecordContent(
            makeServerRecordContent("s2", u8"资料", 1, 2, 3,
                                    folder_state_failed, error_code_folder_relay_broken), out));
        CHECK(out.m_result == folder_state_failed);
        CHECK(out.m_errCode == error_code_folder_relay_broken);
    }
    //失败态但没给具体原因：保留 0，界面回落到通用文案
    {
        FolderRecordContent out;
        REQUIRE(FolderProtocolClient::parseRecordContent(
            makeServerRecordContent("s3", u8"资料", 1, 2, 3, folder_state_failed, 0), out));
        CHECK(out.m_errCode == 0);
    }
}

TEST_CASE("FPC23 记录条 content 解析：字段缺失 / 类型不对 / 取值越界一律安全失败")
{
    FolderRecordContent out;

    //非法 JSON 与空串
    CHECK_FALSE(FolderProtocolClient::parseRecordContent("", out));
    CHECK_FALSE(FolderProtocolClient::parseRecordContent("{", out));
    //顶层不是对象
    CHECK_FALSE(FolderProtocolClient::parseRecordContent("[1,2,3]", out));

    //缺会话 id / 会话 id 为空串：无从与本地气泡对上
    CHECK_FALSE(FolderProtocolClient::parseRecordContent(
        "{\"foldername\":\"f\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":8,\"errcode\":0}", out));
    CHECK_FALSE(FolderProtocolClient::parseRecordContent(
        makeServerRecordContent("", "f", 0, 0, 0, folder_state_completed, 0), out));

    //结局不是终态（响铃中 / 传输中 / 越界 / 缺字段）
    CHECK_FALSE(FolderProtocolClient::parseRecordContent(
        makeServerRecordContent("s", "f", 0, 0, 0, folder_state_ringing, 0), out));
    CHECK_FALSE(FolderProtocolClient::parseRecordContent(
        makeServerRecordContent("s", "f", 0, 0, 0, folder_state_transferring, 0), out));
    CHECK_FALSE(FolderProtocolClient::parseRecordContent(
        makeServerRecordContent("s", "f", 0, 0, 0, folder_state_completed + 1, 0), out));
    CHECK_FALSE(FolderProtocolClient::parseRecordContent(
        "{\"sessionid\":\"s\",\"foldername\":\"f\",\"totaldirs\":0,"
        "\"totalfiles\":0,\"totalsize\":0,\"errcode\":0}", out));

    //规模统计缺字段 / 写成字符串 / 写成小数 / 为负
    CHECK_FALSE(FolderProtocolClient::parseRecordContent(
        "{\"sessionid\":\"s\",\"foldername\":\"f\","
        "\"totalfiles\":0,\"totalsize\":0,\"result\":8,\"errcode\":0}", out));
    CHECK_FALSE(FolderProtocolClient::parseRecordContent(
        "{\"sessionid\":\"s\",\"foldername\":\"f\",\"totaldirs\":\"1\","
        "\"totalfiles\":0,\"totalsize\":0,\"result\":8,\"errcode\":0}", out));
    CHECK_FALSE(FolderProtocolClient::parseRecordContent(
        "{\"sessionid\":\"s\",\"foldername\":\"f\",\"totaldirs\":1.5,"
        "\"totalfiles\":0,\"totalsize\":0,\"result\":8,\"errcode\":0}", out));
    CHECK_FALSE(FolderProtocolClient::parseRecordContent(
        makeServerRecordContent("s", "f", 0, -1, 0, folder_state_completed, 0), out));
    CHECK_FALSE(FolderProtocolClient::parseRecordContent(
        makeServerRecordContent("s", "f", 0, 0, -1, folder_state_completed, 0), out));

    //三项统计同时为 0 是合法的：发送了一个空目录
    FolderRecordContent empty;
    CHECK(FolderProtocolClient::parseRecordContent(
        makeServerRecordContent("s", "f", 0, 0, 0, folder_state_completed, 0), empty));
    CHECK(empty.m_totalFiles == 0);
}

TEST_CASE("FPC24 记录条 content 的构造对不合契约的入参返回空串")
{
    //缺会话 id
    {
        FolderRecordContent rec;
        rec.m_result = folder_state_completed;
        CHECK(FolderProtocolClient::buildRecordContent(rec).empty());
    }
    //结局不是终态（默认构造的 m_result 是 0）
    {
        FolderRecordContent rec;
        rec.m_sessionId = "s";
        CHECK(FolderProtocolClient::buildRecordContent(rec).empty());
        rec.m_result = folder_state_transferring;
        CHECK(FolderProtocolClient::buildRecordContent(rec).empty());
    }
    //规模统计为负
    {
        FolderRecordContent rec;
        rec.m_sessionId = "s";
        rec.m_result    = folder_state_completed;
        rec.m_totalSize = -1;
        CHECK(FolderProtocolClient::buildRecordContent(rec).empty());
    }
}

TEST_CASE("FPC25 记录条 content 的超大整数与中文目录名不丢失")
{
    //50 GiB（超过 4 GiB，32 位装不下）与 int64 上限各走一遍：JSON 数值若被按双精度
    //解析，int64 上限那个会被舍成 9223372036854775808，这条用例就会失败
    const long long kFiftyGiB = 53687091200LL;
    const long long kInt64Max = 9223372036854775807LL;

    FolderRecordContent rec;
    rec.m_sessionId  = "sess-big";
    rec.m_folderName = u8"归档·2026 年度（含空格与中文）";
    rec.m_totalDirs  = 65536;
    rec.m_totalFiles = 1000000;
    rec.m_totalSize  = kFiftyGiB;
    rec.m_result     = folder_state_completed;

    FolderRecordContent back;
    REQUIRE(FolderProtocolClient::parseRecordContent(
        FolderProtocolClient::buildRecordContent(rec), back));
    CHECK(back.m_totalSize == kFiftyGiB);
    CHECK(back.m_folderName == rec.m_folderName);

    rec.m_totalSize = kInt64Max;
    FolderRecordContent backMax;
    REQUIRE(FolderProtocolClient::parseRecordContent(
        FolderProtocolClient::buildRecordContent(rec), backMax));
    CHECK(backMax.m_totalSize == kInt64Max);
}

TEST_CASE("FPC26 记录条解析对服务端报文的字段名逐字对应")
{
    //字段名写岔（sessionid 写成 sessionId）时必须失败，而不是解析成一条会话 id 为空的
    //记录还返回 true
    FolderRecordContent out;
    CHECK_FALSE(FolderProtocolClient::parseRecordContent(
        "{\"sessionId\":\"s\",\"foldername\":\"f\",\"totaldirs\":0,"
        "\"totalfiles\":0,\"totalsize\":0,\"result\":8,\"errcode\":0}", out));

    //foldername 写成 name：会话 id 还在，故解析成功，但文件夹名取不到（退化成空串）。
    //这正是 getString 只认对名字、不做兜底转换的既定口径
    FolderRecordContent wrongName;
    REQUIRE(FolderProtocolClient::parseRecordContent(
        "{\"sessionid\":\"s\",\"name\":\"f\",\"totaldirs\":0,"
        "\"totalfiles\":0,\"totalsize\":0,\"result\":8,\"errcode\":0}", wrongName));
    CHECK(wrongName.m_folderName.empty());
}
