/**
 *  v2 单聊文件夹传输信令编解码单元测试（Suite FTP），test_foldertransferprotocol.cpp
 *
 *  覆盖 FolderTransferProtocol 的五条请求解析（invite / response / cancel 与 retry 共用的
 *  session / close）与五种应答、推送构造（invite 应答、规模超限应答、invite_notify、通用
 *  session 应答、state_notify、relay_info），逐条验证：字段名与协议文档一致、字段齐全、
 *  64 位 ID 走字符串编码且大整数不丢精度、构造产物再解析回来与入参一致，以及畸形输入
 *  （非法 JSON、字段缺失、类型不对、accept / result / state / errcode 取值越界、空 sessionid）
 *  一律安全失败而不抛异常。
 *
 *  另覆盖一次传输结束后留在双方单聊会话里的那条记录条（msgtype=message_type_folder=18）：
 *  content 的构造与解析、六种终态各自的取值、"errcode 仅失败态非零"这条不变式、
 *  幂等键（clientmsgid，由会话 id 与尝试序号派生）的拼法，以及终态判定 isTerminalState
 *  的边界。
 *
 *  协议契约见 docs/协议设计-v2.md「文件夹传输协议（单聊）」一章。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#include "doctest.h"
#include "../chatserversrc/FolderTransferProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../utils/RelayToken.h"
#include "../jsoncpp1.9.0/json.h"

//int64 上限，用来验证大整数在编解码往返中不被截断
static const int64_t FTP_INT64_MAX = 9223372036854775807LL;
//2^53+1，是 IEEE754 双精度已经无法精确表示的第一个整数；
//userid 用它来验证"以字符串编码传输"确实避开了精度问题
static const int64_t FTP_BEYOND_DOUBLE_EXACT = 9007199254740993LL;
//50 GiB，服务端配置项 foldermaxsize 的建议默认值，超出 32 位
static const int64_t FTP_MAX_SIZE_50GIB = 53687091200LL;
//协议文档 12.2.1 示例里的那组规模统计，多处用例复用
static const int64_t FTP_SAMPLE_TOTALDIRS  = 37;
static const int64_t FTP_SAMPLE_TOTALFILES = 1284;
static const int64_t FTP_SAMPLE_TOTALSIZE  = 3865470976LL;

//中转令牌里的两个文件夹角色取值，取自 utils/RelayToken.h。
//这里先复制成本编译单元的常量再参与断言，是为了绕开一个链接期问题：
//RelayToken::role_folder_sender / role_folder_receiver 是类内初始化的 static const int，
//而 utils/RelayToken.cpp 目前只给 role_controller ~ role_callee 这四个补了类外定义，
//新增的这两个漏了；doctest 的 CHECK 宏按引用捕获操作数，正好构成 ODR-use，直接写
//RelayToken::role_folder_sender 会在链接期报未定义符号（该文件的注释本身就说明了这条规则）。
//本行的初始化是常量表达式，不构成 ODR-use。RelayToken.cpp 补上定义后可以直接内联回去。
static const int FTP_ROLE_FOLDER_SENDER   = RelayToken::role_folder_sender;
static const int FTP_ROLE_FOLDER_RECEIVER = RelayToken::role_folder_receiver;

//协议文档 12.2.1 的示例请求，正常解析与"逐个字段改坏"两类用例都以它为基准
static const char* FTP_VALID_INVITE =
    "{\"peerid\":\"100002\",\"foldername\":\"2026Q3-design\","
    "\"totaldirs\":37,\"totalfiles\":1284,\"totalsize\":3865470976}";

//把 JSON 文本解析成 Json::Value，供断言检查构造产物
static bool ftpParseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

// ======================= cmd_folder_invite(1810) 请求解析 =======================

TEST_CASE("FTP1 parseInviteRequest 取出 peerid（字符串编码）、目录名与三项规模统计")
{
    FolderInviteRequest req;
    REQUIRE(FolderTransferProtocol::parseInviteRequest(FTP_VALID_INVITE, req));
    CHECK(req.peerid == 100002);
    CHECK(req.foldername == "2026Q3-design");
    CHECK(req.totaldirs == FTP_SAMPLE_TOTALDIRS);
    CHECK(req.totalfiles == FTP_SAMPLE_TOTALFILES);
    CHECK(req.totalsize == FTP_SAMPLE_TOTALSIZE);

    //中文目录名照常透传（包体是 UTF-8）
    FolderInviteRequest cn;
    REQUIRE(FolderTransferProtocol::parseInviteRequest(
        u8"{\"peerid\":\"7\",\"foldername\":\"设计稿\","
        u8"\"totaldirs\":0,\"totalfiles\":1,\"totalsize\":10}", cn));
    CHECK(cn.foldername == u8"设计稿");

    //空目录：三项统计同时为 0 是合法的
    FolderInviteRequest empty;
    REQUIRE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"empty\","
        "\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0}", empty));
    CHECK(empty.totalfiles == 0);
    CHECK(empty.totalsize == 0);
}

TEST_CASE("FTP2 parseInviteRequest 对畸形输入一律安全失败")
{
    FolderInviteRequest req;

    //非法 JSON / 顶层不是对象
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest("not json", req));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest("[1,2,3]", req));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest("", req));

    //缺 peerid / peerid 不是字符串 / peerid 非正
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"foldername\":\"d\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":100002,\"foldername\":\"d\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"0\",\"foldername\":\"d\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"-5\",\"foldername\":\"d\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0}", req));

    //缺 foldername / 为空串 / 不是字符串
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":123,\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0}", req));

    //三项规模统计缺任意一项
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"d\",\"totalfiles\":0,\"totalsize\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"d\",\"totaldirs\":0,\"totalsize\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"d\",\"totaldirs\":0,\"totalfiles\":0}", req));

    //统计字段为负数
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"d\",\"totaldirs\":-1,\"totalfiles\":0,\"totalsize\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"d\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":-1}", req));

    //统计字段类型不对：字符串 / 浮点 / 布尔 / 对象一律拒绝，且不得抛异常
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"d\",\"totaldirs\":\"0\",\"totalfiles\":0,\"totalsize\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"d\",\"totaldirs\":0,\"totalfiles\":1.5,\"totalsize\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"d\",\"totaldirs\":0,\"totalfiles\":true,\"totalsize\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"d\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":{}}", req));
    //超出 int64 表示范围的数（jsoncpp 会读成浮点），同样安全失败
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"d\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":1e30}", req));
}

TEST_CASE("FTP3 parseInviteRequest 大整数不丢精度：userid 走字符串、totalsize 按 64 位读")
{
    FolderInviteRequest req;
    REQUIRE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"9007199254740993\",\"foldername\":\"d\","
        "\"totaldirs\":0,\"totalfiles\":1,\"totalsize\":9223372036854775807}", req));
    CHECK(req.peerid == FTP_BEYOND_DOUBLE_EXACT);
    CHECK(req.totalsize == FTP_INT64_MAX);

    //50 GiB（默认上限值）超出 32 位，同样要完整读回
    REQUIRE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"d\","
        "\"totaldirs\":0,\"totalfiles\":1,\"totalsize\":53687091200}", req));
    CHECK(req.totalsize == FTP_MAX_SIZE_50GIB);
}

// ======================= cmd_folder_invite(1810) 应答构造 =======================

TEST_CASE("FTP4 buildInviteResponse 成功带 sessionid 与初始状态、失败两者都不带")
{
    //成功：含 sessionid 与 state=1（响铃中）
    {
        std::string json = FolderTransferProtocol::buildInviteResponse(
            0, "ok", "7f3a2c1b-0004", folder_state_ringing);
        Json::Value root;
        REQUIRE(ftpParseJsonText(json, root));
        CHECK(root["code"].asInt() == 0);
        CHECK(root["msg"].asString() == "ok");
        CHECK(root["sessionid"].asString() == "7f3a2c1b-0004");
        CHECK(root["state"].asInt() == folder_state_ringing);
    }
    //失败：契约要求不下发 sessionid 与 state
    {
        std::string json = FolderTransferProtocol::buildInviteResponse(
            error_code_folder_peer_offline, "peer offline", "", folder_state_ringing);
        Json::Value root;
        REQUIRE(ftpParseJsonText(json, root));
        CHECK(root["code"].asInt() == error_code_folder_peer_offline);
        CHECK(root["msg"].asString() == "peer offline");
        CHECK_FALSE(root.isMember("sessionid"));
        CHECK_FALSE(root.isMember("state"));
    }
}

TEST_CASE("FTP5 buildInviteLimitResponse 回带服务端配置的 maxfiles / maxsize")
{
    //文件数超限：609
    {
        std::string json = FolderTransferProtocol::buildInviteLimitResponse(
            error_code_folder_too_many_files, "too many files", 10000, FTP_MAX_SIZE_50GIB);
        Json::Value root;
        REQUIRE(ftpParseJsonText(json, root));
        CHECK(root["code"].asInt() == error_code_folder_too_many_files);
        CHECK(root["msg"].asString() == "too many files");
        CHECK(root["maxfiles"].asInt64() == 10000);
        //50 GiB 超出 32 位，必须按 64 位原样带回
        CHECK(root["maxsize"].asInt64() == FTP_MAX_SIZE_50GIB);
        //超限应答不下发 sessionid
        CHECK_FALSE(root.isMember("sessionid"));
    }
    //总大小超限：610
    {
        std::string json = FolderTransferProtocol::buildInviteLimitResponse(
            error_code_folder_too_large, "too large", 10000, FTP_INT64_MAX);
        Json::Value root;
        REQUIRE(ftpParseJsonText(json, root));
        CHECK(root["code"].asInt() == error_code_folder_too_large);
        CHECK(root["maxsize"].asInt64() == FTP_INT64_MAX);
    }
}

// ======================= cmd_folder_invite_notify(1811) 推送构造 =======================

TEST_CASE("FTP6 buildInviteNotify 字段齐全、from 走字符串编码、规模统计原样透传")
{
    FolderInviteRequest req;
    REQUIRE(FolderTransferProtocol::parseInviteRequest(FTP_VALID_INVITE, req));

    std::string json = FolderTransferProtocol::buildInviteNotify(
        "7f3a2c1b-0004", 100001, u8"谢知行", req);
    Json::Value root;
    REQUIRE(ftpParseJsonText(json, root));
    CHECK(root["sessionid"].asString() == "7f3a2c1b-0004");
    //userid 必须是字符串而不是数字
    CHECK(root["from"].isString());
    CHECK(root["from"].asString() == "100001");
    CHECK(root["fromname"].asString() == u8"谢知行");
    CHECK(root["foldername"].asString() == "2026Q3-design");
    CHECK(root["totaldirs"].asInt64() == FTP_SAMPLE_TOTALDIRS);
    CHECK(root["totalfiles"].asInt64() == FTP_SAMPLE_TOTALFILES);
    CHECK(root["totalsize"].asInt64() == FTP_SAMPLE_TOTALSIZE);
    //接收端就是自己，推送里不回带 peerid
    CHECK_FALSE(root.isMember("peerid"));
}

TEST_CASE("FTP7 buildInviteNotify 大整数往返不丢精度")
{
    FolderInviteRequest req;
    req.foldername = "big";
    req.totaldirs  = 1;
    req.totalfiles = 1;
    req.totalsize  = FTP_INT64_MAX;

    std::string json = FolderTransferProtocol::buildInviteNotify(
        "s-1", FTP_BEYOND_DOUBLE_EXACT, "sender", req);
    Json::Value root;
    REQUIRE(ftpParseJsonText(json, root));
    CHECK(root["from"].asString() == "9007199254740993");
    CHECK(root["totalsize"].asInt64() == FTP_INT64_MAX);
}

// ======================= cmd_folder_response(1812) =======================

TEST_CASE("FTP8 parseResponseRequest 取 sessionid 与 accept")
{
    std::string sessionid;
    int accept = -1;

    REQUIRE(FolderTransferProtocol::parseResponseRequest(
        "{\"sessionid\":\"7f3a2c1b-0004\",\"accept\":1}", sessionid, accept));
    CHECK(sessionid == "7f3a2c1b-0004");
    CHECK(accept == folder_response_accept);

    REQUIRE(FolderTransferProtocol::parseResponseRequest(
        "{\"sessionid\":\"s\",\"accept\":0}", sessionid, accept));
    CHECK(accept == folder_response_reject);
}

TEST_CASE("FTP9 parseResponseRequest 拒绝 accept 越界、空 sessionid 与畸形输入")
{
    std::string sessionid;
    int accept = -1;

    //accept 只认 0 / 1，其余取值一律拒绝
    CHECK_FALSE(FolderTransferProtocol::parseResponseRequest(
        "{\"sessionid\":\"s\",\"accept\":2}", sessionid, accept));
    CHECK_FALSE(FolderTransferProtocol::parseResponseRequest(
        "{\"sessionid\":\"s\",\"accept\":-1}", sessionid, accept));
    //accept 类型不对：字符串 / 布尔 / 浮点
    CHECK_FALSE(FolderTransferProtocol::parseResponseRequest(
        "{\"sessionid\":\"s\",\"accept\":\"1\"}", sessionid, accept));
    CHECK_FALSE(FolderTransferProtocol::parseResponseRequest(
        "{\"sessionid\":\"s\",\"accept\":true}", sessionid, accept));
    CHECK_FALSE(FolderTransferProtocol::parseResponseRequest(
        "{\"sessionid\":\"s\",\"accept\":1.5}", sessionid, accept));
    //缺 accept
    CHECK_FALSE(FolderTransferProtocol::parseResponseRequest(
        "{\"sessionid\":\"s\"}", sessionid, accept));
    //缺 sessionid / sessionid 为空 / sessionid 不是字符串
    CHECK_FALSE(FolderTransferProtocol::parseResponseRequest("{\"accept\":1}", sessionid, accept));
    CHECK_FALSE(FolderTransferProtocol::parseResponseRequest(
        "{\"sessionid\":\"\",\"accept\":1}", sessionid, accept));
    CHECK_FALSE(FolderTransferProtocol::parseResponseRequest(
        "{\"sessionid\":123,\"accept\":1}", sessionid, accept));
    //非法 JSON
    CHECK_FALSE(FolderTransferProtocol::parseResponseRequest("garbage", sessionid, accept));
}

// ======================= cmd_folder_cancel(1813) / cmd_folder_retry(1817) =======================

TEST_CASE("FTP10 parseSessionRequest（cancel 与 retry 共用）取 sessionid")
{
    std::string sessionid;
    REQUIRE(FolderTransferProtocol::parseSessionRequest(
        "{\"sessionid\":\"7f3a2c1b-0004\"}", sessionid));
    CHECK(sessionid == "7f3a2c1b-0004");

    //多带字段不影响解析（老客户端可能带冗余字段）
    REQUIRE(FolderTransferProtocol::parseSessionRequest(
        "{\"sessionid\":\"s-9\",\"extra\":1}", sessionid));
    CHECK(sessionid == "s-9");

    //缺 sessionid / 空 sessionid / 类型不对 / 非法 JSON 一律失败
    CHECK_FALSE(FolderTransferProtocol::parseSessionRequest("{}", sessionid));
    CHECK_FALSE(FolderTransferProtocol::parseSessionRequest("{\"sessionid\":\"\"}", sessionid));
    CHECK_FALSE(FolderTransferProtocol::parseSessionRequest("{\"sessionid\":7}", sessionid));
    CHECK_FALSE(FolderTransferProtocol::parseSessionRequest("{\"sessionid\":[]}", sessionid));
    CHECK_FALSE(FolderTransferProtocol::parseSessionRequest("garbage", sessionid));
}

// ======================= cmd_folder_close(1814) =======================

TEST_CASE("FTP11 parseCloseRequest 解析成功结束：errcode 必须为 0，可缺省")
{
    FolderCloseRequest req;
    REQUIRE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"7f3a2c1b-0004\",\"result\":0,\"errcode\":0,\"errmsg\":\"\"}", req));
    CHECK(req.sessionid == "7f3a2c1b-0004");
    CHECK(req.result == folder_close_success);
    CHECK(req.errcode == 0);
    CHECK(req.errmsg.empty());

    //errcode / errmsg 缺省时按 0 与空串处理
    FolderCloseRequest lean;
    REQUIRE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s\",\"result\":0}", lean));
    CHECK(lean.errcode == 0);
    CHECK(lean.errmsg.empty());
}

TEST_CASE("FTP12 parseCloseRequest 解析失败结束：errcode 取 606~620 两端边界")
{
    //协议文档 12.2.5 的示例：写盘失败
    FolderCloseRequest req;
    REQUIRE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"7f3a2c1b-0004\",\"result\":1,\"errcode\":613,"
        "\"errmsg\":\"write file failed\"}", req));
    CHECK(req.result == folder_close_failed);
    CHECK(req.errcode == error_code_folder_write_fail);
    CHECK(req.errmsg == "write file failed");

    //下界 606
    FolderCloseRequest low;
    REQUIRE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s\",\"result\":1,\"errcode\":606}", low));
    CHECK(low.errcode == error_code_folder_rejected);

    //上界 620
    FolderCloseRequest high;
    REQUIRE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s\",\"result\":1,\"errcode\":620}", high));
    CHECK(high.errcode == error_code_folder_busy);
}

TEST_CASE("FTP13 parseCloseRequest 拒绝 result 越界、errcode 越界与自相矛盾的组合")
{
    FolderCloseRequest req;

    //result 只认 0 / 1
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s\",\"result\":2,\"errcode\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s\",\"result\":-1,\"errcode\":0}", req));
    //缺 result / result 类型不对
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest("{\"sessionid\":\"s\"}", req));
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s\",\"result\":\"1\",\"errcode\":613}", req));

    //失败结束但 errcode 落在 606~620 之外（605 与 621 是紧邻的两个越界值）
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s\",\"result\":1,\"errcode\":605}", req));
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s\",\"result\":1,\"errcode\":621}", req));
    //失败结束却没给 errcode
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s\",\"result\":1}", req));
    //成功结束却带着失败原因，自相矛盾
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s\",\"result\":0,\"errcode\":613}", req));

    //errcode / errmsg 类型不对
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s\",\"result\":1,\"errcode\":\"613\"}", req));
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s\",\"result\":1,\"errcode\":613,\"errmsg\":99}", req));

    //空 sessionid / 缺 sessionid / 非法 JSON
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"\",\"result\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest("{\"result\":0}", req));
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest("garbage", req));
}

// ======================= 1812 / 1813 / 1814 / 1817 通用应答 =======================

TEST_CASE("FTP14 buildSessionResponse 原样带回 sessionid，空 sessionid 时不带该键")
{
    {
        std::string json = FolderTransferProtocol::buildSessionResponse(0, "ok", "7f3a2c1b-0004");
        Json::Value root;
        REQUIRE(ftpParseJsonText(json, root));
        CHECK(root["code"].asInt() == 0);
        CHECK(root["msg"].asString() == "ok");
        CHECK(root["sessionid"].asString() == "7f3a2c1b-0004");
    }
    //出错应答同样带回 sessionid，供客户端定位是哪一次传输
    {
        std::string json = FolderTransferProtocol::buildSessionResponse(
            error_code_folder_session_expired, "session expired", "7f3a2c1b-0004");
        Json::Value root;
        REQUIRE(ftpParseJsonText(json, root));
        CHECK(root["code"].asInt() == error_code_folder_session_expired);
        CHECK(root["sessionid"].asString() == "7f3a2c1b-0004");
    }
    //请求本身就没解析出 sessionid 时不下发该键
    {
        std::string json = FolderTransferProtocol::buildSessionResponse(
            error_code_bad_request, "bad request", "");
        Json::Value root;
        REQUIRE(ftpParseJsonText(json, root));
        CHECK_FALSE(root.isMember("sessionid"));
    }
}

// ======================= cmd_folder_state_notify(1815) =======================

TEST_CASE("FTP15 buildStateNotify 已失败态带 errcode 与 errmsg")
{
    std::string json = FolderTransferProtocol::buildStateNotify(
        "7f3a2c1b-0004", folder_state_failed, error_code_folder_relay_broken,
        "relay lane closed");
    Json::Value root;
    REQUIRE(ftpParseJsonText(json, root));
    CHECK(root["sessionid"].asString() == "7f3a2c1b-0004");
    CHECK(root["state"].asInt() == folder_state_failed);
    CHECK(root["errcode"].asInt() == error_code_folder_relay_broken);
    CHECK(root["errmsg"].asString() == "relay lane closed");
}

TEST_CASE("FTP16 buildStateNotify 非失败态一律把 errcode 置 0、errmsg 置空")
{
    //协议规定只有 state=7 才带失败原因，其余状态即便调用方传了也不写入
    const int states[] =
    {
        folder_state_ringing,
        folder_state_transferring,
        folder_state_rejected,
        folder_state_canceled,
        folder_state_peer_offline,
        folder_state_timeout,
        folder_state_completed,
    };
    const size_t stateCount = sizeof(states) / sizeof(states[0]);

    for (size_t i = 0; i < stateCount; ++i)
    {
        std::string json = FolderTransferProtocol::buildStateNotify(
            "s-1", states[i], error_code_folder_write_fail, "should be dropped");
        Json::Value root;
        REQUIRE(ftpParseJsonText(json, root));
        CHECK(root["state"].asInt() == states[i]);
        CHECK(root["errcode"].asInt() == 0);
        CHECK(root["errmsg"].asString() == "");
    }
}

// ======================= cmd_folder_relay_info(1816) =======================

TEST_CASE("FTP17 buildRelayInfo 字段齐全：地址字段名为 relayip / relayport，另带 token/role/lanes")
{
    const std::string token = "7f3a2c1b-0004|5|100001|100002|1716272400000.5d41402abc4b2a76";

    //发送端那一份
    {
        std::string json = FolderTransferProtocol::buildRelayInfo(
            "7f3a2c1b-0004", FTP_ROLE_FOLDER_SENDER, "192.168.1.8", 20001, token, 4);
        Json::Value root;
        REQUIRE(ftpParseJsonText(json, root));
        CHECK(root["sessionid"].asString() == "7f3a2c1b-0004");
        //字段名沿用远程桌面与音视频的中转参数推送，客户端解析代码可复用
        CHECK(root["relayip"].asString() == "192.168.1.8");
        CHECK(root["relayport"].asInt() == 20001);
        CHECK(root["token"].asString() == token);
        CHECK(root["role"].asInt() == FTP_ROLE_FOLDER_SENDER);
        CHECK(root["lanes"].asInt() == 4);
        //不得出现 host / port 这套旧名字
        CHECK_FALSE(root.isMember("host"));
        CHECK_FALSE(root.isMember("port"));
    }
    //接收端那一份：同会话、同 lanes，角色相反
    {
        std::string json = FolderTransferProtocol::buildRelayInfo(
            "7f3a2c1b-0004", FTP_ROLE_FOLDER_RECEIVER, "192.168.1.8", 20001, token, 4);
        Json::Value root;
        REQUIRE(ftpParseJsonText(json, root));
        CHECK(root["role"].asInt() == FTP_ROLE_FOLDER_RECEIVER);
        CHECK(root["lanes"].asInt() == 4);
    }
}

// ======================= 取值校验 =======================

TEST_CASE("FTP18 isValidState 认 1~9、拒绝两端越界值")
{
    CHECK(FolderTransferProtocol::isValidState(folder_state_ringing));
    CHECK(FolderTransferProtocol::isValidState(folder_state_transferring));
    CHECK(FolderTransferProtocol::isValidState(folder_state_failed));
    CHECK(FolderTransferProtocol::isValidState(folder_state_completed));

    CHECK_FALSE(FolderTransferProtocol::isValidState(0));
    CHECK_FALSE(FolderTransferProtocol::isValidState(-1));
    //9 是紧邻上界的第一个越界值
    //2026-10-04 起上界是单文件直传的「已改为离线发送」(9)
    CHECK_FALSE(FolderTransferProtocol::isValidState(folder_state_to_offline + 1));
}

TEST_CASE("FTP19 isValidErrorCode 认 606~620、拒绝紧邻的 605 与 621")
{
    CHECK(FolderTransferProtocol::isValidErrorCode(error_code_folder_rejected));
    CHECK(FolderTransferProtocol::isValidErrorCode(error_code_folder_canceled));
    CHECK(FolderTransferProtocol::isValidErrorCode(error_code_folder_busy));

    //605 是上传 MD5 不符，语义不同，不属于本组
    CHECK_FALSE(FolderTransferProtocol::isValidErrorCode(error_code_folder_rejected - 1));
    CHECK_FALSE(FolderTransferProtocol::isValidErrorCode(error_code_folder_busy + 1));
    CHECK_FALSE(FolderTransferProtocol::isValidErrorCode(0));
}

// ======================= 端到端往返 =======================

TEST_CASE("FTP20 一次邀请的完整往返：请求解析 → 邀请推送 → 结束请求解析 → 状态推送")
{
    //发送端发起邀请
    FolderInviteRequest req;
    REQUIRE(FolderTransferProtocol::parseInviteRequest(FTP_VALID_INVITE, req));

    //服务端建会话后应答发送端
    std::string respJson = FolderTransferProtocol::buildInviteResponse(
        0, "ok", "7f3a2c1b-0004", folder_state_ringing);
    Json::Value resp;
    REQUIRE(ftpParseJsonText(respJson, resp));
    const std::string sessionid = resp["sessionid"].asString();
    REQUIRE(sessionid == "7f3a2c1b-0004");

    //服务端把邀请推给接收端，规模统计与请求里的一致
    std::string notifyJson = FolderTransferProtocol::buildInviteNotify(
        sessionid, 100001, "sender", req);
    Json::Value notify;
    REQUIRE(ftpParseJsonText(notifyJson, notify));
    CHECK(notify["sessionid"].asString() == sessionid);
    CHECK(notify["foldername"].asString() == req.foldername);
    CHECK(notify["totaldirs"].asInt64() == req.totaldirs);
    CHECK(notify["totalfiles"].asInt64() == req.totalfiles);
    CHECK(notify["totalsize"].asInt64() == req.totalsize);

    //接收端同意，服务端下发中转参数
    std::string acceptedSession;
    int accept = -1;
    REQUIRE(FolderTransferProtocol::parseResponseRequest(
        "{\"sessionid\":\"7f3a2c1b-0004\",\"accept\":1}", acceptedSession, accept));
    CHECK(acceptedSession == sessionid);
    CHECK(accept == folder_response_accept);

    //传输中接收端写盘失败，发 1814 报失败
    FolderCloseRequest close;
    REQUIRE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"7f3a2c1b-0004\",\"result\":1,\"errcode\":613,"
        "\"errmsg\":\"write file failed\"}", close));
    CHECK(close.sessionid == sessionid);

    //服务端把同一个 errcode 原样透传给对端，保证两端看到同一条失败原因
    std::string stateJson = FolderTransferProtocol::buildStateNotify(
        close.sessionid, folder_state_failed, close.errcode, close.errmsg);
    Json::Value state;
    REQUIRE(ftpParseJsonText(stateJson, state));
    CHECK(state["sessionid"].asString() == sessionid);
    CHECK(state["state"].asInt() == folder_state_failed);
    CHECK(state["errcode"].asInt() == error_code_folder_write_fail);
    CHECK(state["errmsg"].asString() == "write file failed");
}

// ======= 传输结束后留在聊天记录里的那条记录条（message_type_folder=18） =======

//记录条用例复用的一组基准取值：会话 id 与根目录名
static const char* FTP_RECORD_SESSIONID  = "7f3a2c1b-0004";
static const char* FTP_RECORD_FOLDERNAME = "2026Q3-design";

//按基准取值造一条记录，再按各用例的需要改 result / errcode
static FolderTransferRecord ftpMakeRecord(int result, int errcode)
{
    FolderTransferRecord rec;
    rec.sessionid  = FTP_RECORD_SESSIONID;
    rec.foldername = FTP_RECORD_FOLDERNAME;
    rec.totaldirs  = FTP_SAMPLE_TOTALDIRS;
    rec.totalfiles = FTP_SAMPLE_TOTALFILES;
    rec.totalsize  = FTP_SAMPLE_TOTALSIZE;
    rec.result     = result;
    rec.errcode    = errcode;
    return rec;
}

TEST_CASE("FTP21 buildRecordContent 字段名与协议文档一致、七个字段齐全且不含文件清单")
{
    const std::string json =
        FolderTransferProtocol::buildRecordContent(ftpMakeRecord(folder_state_completed, 0));

    Json::Value root;
    REQUIRE(ftpParseJsonText(json, root));

    //协议文档 12.8 规定的七个字段，一个都不能少
    CHECK(root.isMember("sessionid"));
    CHECK(root.isMember("foldername"));
    CHECK(root.isMember("totaldirs"));
    CHECK(root.isMember("totalfiles"));
    CHECK(root.isMember("totalsize"));
    CHECK(root.isMember("result"));
    CHECK(root.isMember("errcode"));

    CHECK(root["sessionid"].asString() == FTP_RECORD_SESSIONID);
    CHECK(root["foldername"].asString() == FTP_RECORD_FOLDERNAME);
    CHECK(root["totaldirs"].asInt64() == FTP_SAMPLE_TOTALDIRS);
    CHECK(root["totalfiles"].asInt64() == FTP_SAMPLE_TOTALFILES);
    CHECK(root["totalsize"].asInt64() == FTP_SAMPLE_TOTALSIZE);
    CHECK(root["result"].asInt() == folder_state_completed);
    CHECK(root["errcode"].asInt() == 0);

    //记录条只记动作与结局：文件清单、相对路径、md5 一概不得出现
    CHECK_FALSE(root.isMember("files"));
    CHECK_FALSE(root.isMember("filelist"));
    CHECK_FALSE(root.isMember("paths"));
    CHECK_FALSE(root.isMember("md5"));
}

TEST_CASE("FTP22 buildRecordContent 六种终态各自的取值都能原样往返")
{
    //3 已拒绝 / 4 应答前撤回 / 5 对方离线 / 6 邀请超时 / 8 已完成：这五种都不带失败原因
    const int nonFailedStates[] = {
        folder_state_rejected,
        folder_state_canceled,
        folder_state_peer_offline,
        folder_state_timeout,
        folder_state_completed,
    };

    for (size_t i = 0; i < sizeof(nonFailedStates) / sizeof(nonFailedStates[0]); ++i)
    {
        const std::string json =
            FolderTransferProtocol::buildRecordContent(ftpMakeRecord(nonFailedStates[i], 0));

        FolderTransferRecord back;
        REQUIRE(FolderTransferProtocol::parseRecordContent(json, back));
        CHECK(back.result == nonFailedStates[i]);
        CHECK(back.errcode == 0);
        CHECK(back.sessionid == FTP_RECORD_SESSIONID);
        CHECK(back.foldername == FTP_RECORD_FOLDERNAME);
        CHECK(back.totaldirs == FTP_SAMPLE_TOTALDIRS);
        CHECK(back.totalfiles == FTP_SAMPLE_TOTALFILES);
        CHECK(back.totalsize == FTP_SAMPLE_TOTALSIZE);
    }

    //7 已失败：带本组之内的失败原因，两端边界都能往返
    FolderTransferRecord low;
    REQUIRE(FolderTransferProtocol::parseRecordContent(
        FolderTransferProtocol::buildRecordContent(
            ftpMakeRecord(folder_state_failed, error_code_folder_rejected)), low));
    CHECK(low.result == folder_state_failed);
    CHECK(low.errcode == error_code_folder_rejected);

    FolderTransferRecord high;
    REQUIRE(FolderTransferProtocol::parseRecordContent(
        FolderTransferProtocol::buildRecordContent(
            ftpMakeRecord(folder_state_failed, error_code_folder_busy)), high));
    CHECK(high.result == folder_state_failed);
    CHECK(high.errcode == error_code_folder_busy);
}

TEST_CASE("FTP23 buildRecordContent 只有失败态写 errcode，其余终态一律置 0、忽略传入值")
{
    //调用方为了让代码读起来清楚，往往照常传入一个错误码（如拒绝时传 606）；
    //"非失败态 errcode 恒为 0"这条不变式由本函数集中保证，不指望各调用点自己清零
    const int statesWithBogusErrcode[] = {
        folder_state_rejected,
        folder_state_canceled,
        folder_state_peer_offline,
        folder_state_timeout,
        folder_state_completed,
    };

    for (size_t i = 0; i < sizeof(statesWithBogusErrcode) / sizeof(statesWithBogusErrcode[0]); ++i)
    {
        const std::string json = FolderTransferProtocol::buildRecordContent(
            ftpMakeRecord(statesWithBogusErrcode[i], error_code_folder_peer_offline));

        Json::Value root;
        REQUIRE(ftpParseJsonText(json, root));
        CHECK(root["errcode"].asInt() == 0);

        //归一之后的内容仍要能被解析回来，否则 result 与 errcode 会自相矛盾
        FolderTransferRecord back;
        CHECK(FolderTransferProtocol::parseRecordContent(json, back));
        CHECK(back.errcode == 0);
    }

    //失败态则原样写入
    const std::string failedJson = FolderTransferProtocol::buildRecordContent(
        ftpMakeRecord(folder_state_failed, error_code_folder_write_fail));
    Json::Value failedRoot;
    REQUIRE(ftpParseJsonText(failedJson, failedRoot));
    CHECK(failedRoot["errcode"].asInt() == error_code_folder_write_fail);
}

TEST_CASE("FTP24 parseRecordContent 对畸形输入一律安全失败")
{
    FolderTransferRecord rec;

    //非法 JSON、顶层不是对象
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent("", rec));
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent("{", rec));
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent("[1,2]", rec));

    //缺 sessionid、sessionid 为空、sessionid 不是字符串
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0,\"result\":8}", rec));
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":8}", rec));
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":123,\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":8}", rec));

    //缺 foldername、foldername 为空
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0,\"result\":8}", rec));
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":8}", rec));

    //规模统计为负、为浮点、写成字符串
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":-1,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":8}", rec));
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":1.5,"
        "\"totalsize\":0,\"result\":8}", rec));
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":\"100\",\"result\":8}", rec));

    //缺 result
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0}", rec));

    //result 是过程态：记录条只在会话走到终态时产生，1 与 2 出现在这里即为非法
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":1}", rec));
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":2}", rec));

    //result 越界：0 与 9 分别是终态段两侧紧邻的第一个非法值
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":0}", rec));
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":9}", rec));
}

TEST_CASE("FTP25 parseRecordContent 校验 errcode 与 result 相符：仅失败态非零")
{
    FolderTransferRecord rec;

    //失败态缺省 errcode 即按 0 处理，而 0 不在 606~620 之内，故被拒
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":7}", rec));

    //失败态带越界错误码：605 属上传域、621 已出本组上界
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":7,\"errcode\":605}", rec));
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":7,\"errcode\":621}", rec));

    //失败态带本组之内的错误码：通过
    REQUIRE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":7,\"errcode\":616}", rec));
    CHECK(rec.errcode == error_code_folder_relay_broken);

    //非失败态带非零 errcode：与 result 自相矛盾，拒绝
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":8,\"errcode\":613}", rec));
    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":3,\"errcode\":606}", rec));

    //非失败态缺省 errcode：按 0 处理，通过
    REQUIRE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s1\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":0,"
        "\"totalsize\":0,\"result\":6}", rec));
    CHECK(rec.result == folder_state_timeout);
    CHECK(rec.errcode == 0);
}

TEST_CASE("FTP26 buildRecordClientMsgId 由会话 id 与尝试序号派生固定串")
{
    //幂等键的拼法：固定前缀 "folder-" 加会话 id 加连字符加尝试序号
    CHECK(FolderTransferProtocol::buildRecordClientMsgId(FTP_RECORD_SESSIONID, 1)
          == std::string("folder-") + FTP_RECORD_SESSIONID + "-1");

    //不同会话必然得到不同的键
    CHECK(FolderTransferProtocol::buildRecordClientMsgId("7f3a2c1b-0005", 1)
          != FolderTransferProtocol::buildRecordClientMsgId(FTP_RECORD_SESSIONID, 1));

    //固定前缀使它不会与客户端生成的 UUID 型 clientmsgid 相撞
    const std::string key = FolderTransferProtocol::buildRecordClientMsgId(FTP_RECORD_SESSIONID, 1);
    CHECK(key.compare(0, 7, "folder-") == 0);

    //会话 id 为空时仍返回前缀加序号而不是空串（调用方不会这么用，此处只钉住行为）
    CHECK(FolderTransferProtocol::buildRecordClientMsgId("", 1) == "folder--1");

    //序号是多位数时照常拼接，不做补零
    CHECK(FolderTransferProtocol::buildRecordClientMsgId(FTP_RECORD_SESSIONID, 12)
          == std::string("folder-") + FTP_RECORD_SESSIONID + "-12");
}

TEST_CASE("FTP26B 幂等键：同一会话同一序号恒相同，不同序号必不同")
{
    //同一会话、同一尝试序号多次调用必然得到同一个键 —— 双方各发一帧 cmd_folder_close 时，
    //正是靠它让第二次终态判定命中去重、这一次尝试不再多留一条记录
    const std::string first =
        FolderTransferProtocol::buildRecordClientMsgId(FTP_RECORD_SESSIONID, 1);
    const std::string second =
        FolderTransferProtocol::buildRecordClientMsgId(FTP_RECORD_SESSIONID, 1);
    CHECK(first == second);

    //同一会话、不同尝试序号必然得到不同的键 —— 一笔传输先失败（序号 1）、在保留期内重试
    //又成功（序号 2）时，两条记录靠它各自成立，不会被前一条去重掉
    const std::string secondAttempt =
        FolderTransferProtocol::buildRecordClientMsgId(FTP_RECORD_SESSIONID, 2);
    CHECK(secondAttempt != first);

    //连续多次重试同样两两不同
    const std::string thirdAttempt =
        FolderTransferProtocol::buildRecordClientMsgId(FTP_RECORD_SESSIONID, 3);
    CHECK(thirdAttempt != first);
    CHECK(thirdAttempt != secondAttempt);

    //相邻序号不得因拼接方式而产生歧义：会话 id 以数字结尾时，"…-1" 与 "…-12" 之类
    //必须仍是两个不同的键（本用例同时钉住"序号紧跟在一个连字符之后"这个格式）
    CHECK(FolderTransferProtocol::buildRecordClientMsgId("sess-1", 2)
          != FolderTransferProtocol::buildRecordClientMsgId("sess", 12));
}

TEST_CASE("FTP27 isTerminalState 认 3~9、拒绝响铃中与传输中这两个过程态")
{
    CHECK(FolderTransferProtocol::isTerminalState(folder_state_rejected));
    CHECK(FolderTransferProtocol::isTerminalState(folder_state_canceled));
    CHECK(FolderTransferProtocol::isTerminalState(folder_state_peer_offline));
    CHECK(FolderTransferProtocol::isTerminalState(folder_state_timeout));
    CHECK(FolderTransferProtocol::isTerminalState(folder_state_failed));
    CHECK(FolderTransferProtocol::isTerminalState(folder_state_completed));

    //过程态：会话还没结束，不该留记录
    CHECK_FALSE(FolderTransferProtocol::isTerminalState(folder_state_ringing));
    CHECK_FALSE(FolderTransferProtocol::isTerminalState(folder_state_transferring));

    //两端越界
    CHECK_FALSE(FolderTransferProtocol::isTerminalState(0));
    CHECK_FALSE(FolderTransferProtocol::isTerminalState(-1));
    //2026-10-04 起上界是单文件直传的「已改为离线发送」(9)
    CHECK_FALSE(FolderTransferProtocol::isTerminalState(folder_state_to_offline + 1));
}

TEST_CASE("FTP28 记录条的大整数、中文目录名与空目录都能原样往返")
{
    //50 GiB 超出 32 位，须按 64 位编码
    FolderTransferRecord big = ftpMakeRecord(folder_state_completed, 0);
    big.totalsize = FTP_MAX_SIZE_50GIB;

    FolderTransferRecord backBig;
    REQUIRE(FolderTransferProtocol::parseRecordContent(
        FolderTransferProtocol::buildRecordContent(big), backBig));
    CHECK(backBig.totalsize == FTP_MAX_SIZE_50GIB);

    //int64 上限也不被截断
    FolderTransferRecord max = ftpMakeRecord(folder_state_completed, 0);
    max.totalsize  = FTP_INT64_MAX;
    max.totalfiles = FTP_INT64_MAX;

    FolderTransferRecord backMax;
    REQUIRE(FolderTransferProtocol::parseRecordContent(
        FolderTransferProtocol::buildRecordContent(max), backMax));
    CHECK(backMax.totalsize == FTP_INT64_MAX);
    CHECK(backMax.totalfiles == FTP_INT64_MAX);

    //中文目录名照常透传（包体是 UTF-8）
    FolderTransferRecord cn = ftpMakeRecord(folder_state_failed, error_code_folder_canceled);
    cn.foldername = u8"设计稿 2026";

    FolderTransferRecord backCn;
    REQUIRE(FolderTransferProtocol::parseRecordContent(
        FolderTransferProtocol::buildRecordContent(cn), backCn));
    CHECK(backCn.foldername == u8"设计稿 2026");
    CHECK(backCn.errcode == error_code_folder_canceled);

    //空目录：三项统计同时为 0 是合法的
    FolderTransferRecord empty = ftpMakeRecord(folder_state_completed, 0);
    empty.totaldirs  = 0;
    empty.totalfiles = 0;
    empty.totalsize  = 0;

    FolderTransferRecord backEmpty;
    REQUIRE(FolderTransferProtocol::parseRecordContent(
        FolderTransferProtocol::buildRecordContent(empty), backEmpty));
    CHECK(backEmpty.totaldirs == 0);
    CHECK(backEmpty.totalfiles == 0);
    CHECK(backEmpty.totalsize == 0);
}

TEST_CASE("FTP29 buildRelayInfo 的 p2psessionid：建了 P2P 会话时带上，没建时不出现（P2P 方案第 5 期）")
{
    const std::string token = "7f3a2c1b-0004|5|100001|100002|1716272400000.5d41402abc4b2a76";
    Json::Value root;
    REQUIRE(ftpParseJsonText(FolderTransferProtocol::buildRelayInfo("7f3a2c1b-0004", FTP_ROLE_FOLDER_SENDER, "192.168.1.8",
                                                                     20001, token, 4, "p2p-100001-100002-1-1"), root));
    CHECK(root["p2psessionid"].asString() == "p2p-100001-100002-1-1");
    CHECK(root["lanes"].asInt() == 4);
    REQUIRE(ftpParseJsonText(FolderTransferProtocol::buildRelayInfo("7f3a2c1b-0004", FTP_ROLE_FOLDER_SENDER, "192.168.1.8",
                                                                     20001, token, 4), root));
    CHECK_FALSE(root.isMember("p2psessionid"));
}

// ======================= 2026-10-04 单聊单文件 P2P 直传与链路路径 =======================
// 方案见 docs/单聊文件P2P直传与P2P徽章方案.md：同一组 1810~1817 信令带 kind 区分文件夹传输与单文件直传，
// 1814 带上客户端看到的链路路径与 MD5，结束时直传留下的是一条普通文件消息（msgtype 3，带 direct=1）。

//一份合法的 32 位十六进制 MD5
static const char* FTP_SAMPLE_MD5 = "0123456789abcdef0123456789abcdef";

TEST_CASE("FTP30 parseInviteRequest 的 kind：缺省为文件夹；为 1 时须恰好一个文件、没有目录；其余取值非法")
{
    FolderInviteRequest req;
    REQUIRE(FolderTransferProtocol::parseInviteRequest(FTP_VALID_INVITE, req));
    CHECK(req.kind == folder_kind_folder);

    FolderInviteRequest file;
    REQUIRE(FolderTransferProtocol::parseInviteRequest(
        u8"{\"peerid\":\"100002\",\"foldername\":\"报价单.xlsx\",\"totaldirs\":0,\"totalfiles\":1,"
        u8"\"totalsize\":53687091200,\"kind\":1}", file));
    CHECK(file.kind == folder_kind_file);
    CHECK(file.foldername == u8"报价单.xlsx");
    CHECK(file.totalsize == FTP_MAX_SIZE_50GIB);

    //显式写 0 与缺省相同
    FolderInviteRequest zero;
    REQUIRE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"a\",\"totaldirs\":3,\"totalfiles\":9,\"totalsize\":1,\"kind\":0}", zero));
    CHECK(zero.kind == folder_kind_folder);

    FolderInviteRequest bad;
    //单文件直传带了目录、或文件数不是 1：非法
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"a.txt\",\"totaldirs\":1,\"totalfiles\":1,\"totalsize\":1,\"kind\":1}", bad));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"a.txt\",\"totaldirs\":0,\"totalfiles\":2,\"totalsize\":1,\"kind\":1}", bad));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"a.txt\",\"totaldirs\":0,\"totalfiles\":0,\"totalsize\":0,\"kind\":1}", bad));
    //未知取值与类型不对：非法
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":1,\"totalsize\":1,\"kind\":2}", bad));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":1,\"totalsize\":1,\"kind\":\"1\"}", bad));
    CHECK_FALSE(FolderTransferProtocol::parseInviteRequest(
        "{\"peerid\":\"7\",\"foldername\":\"a\",\"totaldirs\":0,\"totalfiles\":1,\"totalsize\":1,\"kind\":-1}", bad));
}

TEST_CASE("FTP31 buildInviteNotify：单文件直传写 kind=1，文件夹传输不写（旧客户端看到的包体不变）")
{
    FolderInviteRequest file;
    file.peerid = 100002;
    file.foldername = "a.txt";
    file.totalfiles = 1;
    file.totalsize = 10;
    file.kind = folder_kind_file;
    Json::Value root;
    REQUIRE(ftpParseJsonText(FolderTransferProtocol::buildInviteNotify("s-1", 100001, "alice", file), root));
    CHECK(root["kind"].asInt() == folder_kind_file);
    CHECK(root["foldername"].asString() == "a.txt");

    file.kind = folder_kind_folder;
    REQUIRE(ftpParseJsonText(FolderTransferProtocol::buildInviteNotify("s-1", 100001, "alice", file), root));
    CHECK_FALSE(root.isMember("kind"));
}

TEST_CASE("FTP32 parseCancelRequest：tooffline 缺省为 0，可为 1，其余非法；sessionid 必填")
{
    std::string sid;
    int tooffline = -1;
    REQUIRE(FolderTransferProtocol::parseCancelRequest("{\"sessionid\":\"s-1\"}", sid, tooffline));
    CHECK(sid == "s-1");
    CHECK(tooffline == 0);
    REQUIRE(FolderTransferProtocol::parseCancelRequest("{\"sessionid\":\"s-2\",\"tooffline\":1}", sid, tooffline));
    CHECK(sid == "s-2");
    CHECK(tooffline == 1);
    REQUIRE(FolderTransferProtocol::parseCancelRequest("{\"sessionid\":\"s-3\",\"tooffline\":0}", sid, tooffline));
    CHECK(tooffline == 0);
    CHECK_FALSE(FolderTransferProtocol::parseCancelRequest("{\"sessionid\":\"s-4\",\"tooffline\":2}", sid, tooffline));
    CHECK_FALSE(FolderTransferProtocol::parseCancelRequest("{\"sessionid\":\"s-4\",\"tooffline\":\"1\"}", sid, tooffline));
    CHECK_FALSE(FolderTransferProtocol::parseCancelRequest("{\"sessionid\":\"\",\"tooffline\":1}", sid, tooffline));
    CHECK_FALSE(FolderTransferProtocol::parseCancelRequest("{\"tooffline\":1}", sid, tooffline));
}

TEST_CASE("FTP33 parseCloseRequest 的 linkpath 与 md5：缺省为 0 与空，越界或格式不对非法，md5 统一为小写")
{
    FolderCloseRequest req;
    REQUIRE(FolderTransferProtocol::parseCloseRequest("{\"sessionid\":\"s-1\",\"result\":0}", req));
    CHECK(req.linkpath == kFolderLinkPathNone);
    CHECK(req.md5.empty());

    REQUIRE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s-1\",\"result\":0,\"linkpath\":1,\"md5\":\"0123456789ABCDEF0123456789abcdef\"}", req));
    CHECK(req.linkpath == 1);
    CHECK(req.md5 == FTP_SAMPLE_MD5);
    REQUIRE(FolderTransferProtocol::parseCloseRequest("{\"sessionid\":\"s-1\",\"result\":0,\"linkpath\":3}", req));
    CHECK(req.linkpath == kFolderLinkPathMax);
    //失败结束也可以带路径
    REQUIRE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s-1\",\"result\":1,\"errcode\":613,\"linkpath\":2}", req));
    CHECK(req.linkpath == 2);

    FolderCloseRequest bad;
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest("{\"sessionid\":\"s-1\",\"result\":0,\"linkpath\":4}", bad));
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest("{\"sessionid\":\"s-1\",\"result\":0,\"linkpath\":-1}", bad));
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest("{\"sessionid\":\"s-1\",\"result\":0,\"linkpath\":\"1\"}", bad));
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest("{\"sessionid\":\"s-1\",\"result\":0,\"md5\":\"0123\"}", bad));
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s-1\",\"result\":0,\"md5\":\"0123456789abcdef0123456789abcdeg\"}", bad));
    CHECK_FALSE(FolderTransferProtocol::parseCloseRequest("{\"sessionid\":\"s-1\",\"result\":0,\"md5\":12}", bad));
    //622 是合法的失败原因（直传建不起 P2P 链路）
    REQUIRE(FolderTransferProtocol::parseCloseRequest(
        "{\"sessionid\":\"s-1\",\"result\":1,\"errcode\":622}", req));
    CHECK(req.errcode == error_code_folder_direct_unavailable);
}

TEST_CASE("FTP34 文件夹记录条的 linkpath：大于 0 时写入并能取回，为 0 时不写；越界非法")
{
    FolderTransferRecord rec;
    rec.sessionid = "s-1";
    rec.foldername = "docs";
    rec.totalfiles = 3;
    rec.result = folder_state_completed;
    rec.linkpath = 2;
    Json::Value root;
    REQUIRE(ftpParseJsonText(FolderTransferProtocol::buildRecordContent(rec), root));
    CHECK(root["linkpath"].asInt() == 2);
    FolderTransferRecord back;
    REQUIRE(FolderTransferProtocol::parseRecordContent(FolderTransferProtocol::buildRecordContent(rec), back));
    CHECK(back.linkpath == 2);

    rec.linkpath = kFolderLinkPathNone;
    REQUIRE(ftpParseJsonText(FolderTransferProtocol::buildRecordContent(rec), root));
    CHECK_FALSE(root.isMember("linkpath"));
    REQUIRE(FolderTransferProtocol::parseRecordContent(FolderTransferProtocol::buildRecordContent(rec), back));
    CHECK(back.linkpath == kFolderLinkPathNone);

    CHECK_FALSE(FolderTransferProtocol::parseRecordContent(
        "{\"sessionid\":\"s-1\",\"foldername\":\"d\",\"totaldirs\":0,\"totalfiles\":1,\"totalsize\":1,"
        "\"result\":8,\"linkpath\":7}", back));
}

TEST_CASE("FTP35 buildDirectFileContent：普通文件消息的字段加 direct=1，不写 fileid；md5、linkpath 按需写")
{
    DirectFileRecord rec;
    rec.sessionid = "100001-100002-1-1";
    rec.filename = u8"报价单.xlsx";
    rec.filesize = FTP_MAX_SIZE_50GIB;
    rec.md5 = FTP_SAMPLE_MD5;
    rec.result = folder_state_completed;
    rec.linkpath = 1;
    Json::Value root;
    REQUIRE(ftpParseJsonText(FolderTransferProtocol::buildDirectFileContent(rec), root));
    CHECK(root["direct"].asInt() == 1);
    CHECK(root["sessionid"].asString() == "100001-100002-1-1");
    CHECK(root["filename"].asString() == u8"报价单.xlsx");
    CHECK(root["filesize"].asInt64() == FTP_MAX_SIZE_50GIB);
    CHECK(root["md5"].asString() == FTP_SAMPLE_MD5);
    CHECK(root["result"].asInt() == folder_state_completed);
    CHECK(root["linkpath"].asInt() == 1);
    CHECK_FALSE(root.isMember("fileid"));

    //拒绝：没有 md5、没有链路
    DirectFileRecord rejected;
    rejected.sessionid = "s-2";
    rejected.filename = "a.txt";
    rejected.filesize = 10;
    rejected.result = folder_state_rejected;
    REQUIRE(ftpParseJsonText(FolderTransferProtocol::buildDirectFileContent(rejected), root));
    CHECK(root["result"].asInt() == folder_state_rejected);
    CHECK_FALSE(root.isMember("md5"));
    CHECK_FALSE(root.isMember("linkpath"));
}

TEST_CASE("FTP36 parseDirectFileContent：往返一致，direct 不为 1、结局不是 3 / 8、字段不合法时失败")
{
    DirectFileRecord rec;
    rec.sessionid = "s-1";
    rec.filename = "a.txt";
    rec.filesize = 10;
    rec.md5 = FTP_SAMPLE_MD5;
    rec.result = folder_state_completed;
    rec.linkpath = 2;
    DirectFileRecord back;
    REQUIRE(FolderTransferProtocol::parseDirectFileContent(FolderTransferProtocol::buildDirectFileContent(rec), back));
    CHECK(back.sessionid == "s-1");
    CHECK(back.filename == "a.txt");
    CHECK(back.filesize == 10);
    CHECK(back.md5 == FTP_SAMPLE_MD5);
    CHECK(back.result == folder_state_completed);
    CHECK(back.linkpath == 2);

    //普通的离线文件消息（有 fileid、没有 direct）：不是直传记录
    CHECK_FALSE(FolderTransferProtocol::parseDirectFileContent(
        "{\"fileid\":\"abc\",\"filename\":\"a.txt\",\"filesize\":10,\"md5\":\"0123456789abcdef0123456789abcdef\"}", back));
    CHECK_FALSE(FolderTransferProtocol::parseDirectFileContent(
        "{\"direct\":0,\"sessionid\":\"s\",\"filename\":\"a\",\"filesize\":1,\"result\":8}", back));
    CHECK_FALSE(FolderTransferProtocol::parseDirectFileContent(
        "{\"direct\":1,\"sessionid\":\"s\",\"filename\":\"a\",\"filesize\":1,\"result\":7}", back));
    CHECK_FALSE(FolderTransferProtocol::parseDirectFileContent(
        "{\"direct\":1,\"sessionid\":\"\",\"filename\":\"a\",\"filesize\":1,\"result\":8}", back));
    CHECK_FALSE(FolderTransferProtocol::parseDirectFileContent(
        "{\"direct\":1,\"sessionid\":\"s\",\"filename\":\"\",\"filesize\":1,\"result\":8}", back));
    CHECK_FALSE(FolderTransferProtocol::parseDirectFileContent(
        "{\"direct\":1,\"sessionid\":\"s\",\"filename\":\"a\",\"filesize\":-1,\"result\":8}", back));
    CHECK_FALSE(FolderTransferProtocol::parseDirectFileContent(
        "{\"direct\":1,\"sessionid\":\"s\",\"filename\":\"a\",\"filesize\":1,\"result\":8,\"linkpath\":9}", back));
    CHECK_FALSE(FolderTransferProtocol::parseDirectFileContent(
        "{\"direct\":1,\"sessionid\":\"s\",\"filename\":\"a\",\"filesize\":1,\"result\":8,\"md5\":\"xyz\"}", back));
}

TEST_CASE("FTP37 shouldWriteRecord：文件夹传输每个终态都留记录；单文件直传只在完成与拒绝时留")
{
    for (int state = folder_state_rejected; state <= folder_state_completed; ++state)
        CHECK(FolderTransferProtocol::shouldWriteRecord(folder_kind_folder, state));
    CHECK_FALSE(FolderTransferProtocol::shouldWriteRecord(folder_kind_folder, folder_state_ringing));
    CHECK_FALSE(FolderTransferProtocol::shouldWriteRecord(folder_kind_folder, folder_state_transferring));

    CHECK(FolderTransferProtocol::shouldWriteRecord(folder_kind_file, folder_state_completed));
    CHECK(FolderTransferProtocol::shouldWriteRecord(folder_kind_file, folder_state_rejected));
    CHECK_FALSE(FolderTransferProtocol::shouldWriteRecord(folder_kind_file, folder_state_canceled));
    CHECK_FALSE(FolderTransferProtocol::shouldWriteRecord(folder_kind_file, folder_state_peer_offline));
    CHECK_FALSE(FolderTransferProtocol::shouldWriteRecord(folder_kind_file, folder_state_timeout));
    CHECK_FALSE(FolderTransferProtocol::shouldWriteRecord(folder_kind_file, folder_state_failed));
    CHECK_FALSE(FolderTransferProtocol::shouldWriteRecord(folder_kind_file, folder_state_to_offline));
    CHECK_FALSE(FolderTransferProtocol::shouldWriteRecord(folder_kind_file, folder_state_ringing));
}

TEST_CASE("FTP38 取值范围：状态 9 是终态，622 是合法的失败原因，621 不是")
{
    CHECK(FolderTransferProtocol::isTerminalState(folder_state_to_offline));
    CHECK(FolderTransferProtocol::isValidState(folder_state_to_offline));
    CHECK_FALSE(FolderTransferProtocol::isValidState(10));
    CHECK(FolderTransferProtocol::isValidErrorCode(error_code_folder_direct_unavailable));
    CHECK_FALSE(FolderTransferProtocol::isValidErrorCode(error_code_file_sensitive_word));
    CHECK_FALSE(FolderTransferProtocol::isValidErrorCode(623));
}
