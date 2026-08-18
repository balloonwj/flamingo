/**
 *  文件夹传输中转通道客户端的纯逻辑单元测试（Suite FRC），FolderRelayClientTests.cpp
 *
 *  FolderRelayClient 里能脱离网络验证的只有两小块，本文件覆盖的就是它们：
 *    - 报到帧包体的构造：字段名 token / lane 与 docs/协议设计-v2.md 12.3.1 一致，产出
 *      是合法 JSON，令牌里出现双引号 / 反斜杠 / 控制字符 / UTF-8 多字节时仍能原样还原；
 *    - lane 序号的合法区间：控制通道 0 与数据通道 1..8 合法，负数与越过硬上限的一律拒绝；
 *    - 单帧发送上限：明文上限恰好给加密膨胀让出了位置，压线的应用帧不会在封帧处失败。
 *
 *  socket 连接、加密握手、收发线程不在单元测试覆盖范围（与 RdRelayClient 一致，靠真机
 *  联调验证），本文件不起任何网络连接。
 *
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-08-13
 */

#include "catch_amalgamated.hpp"

#include <string>

//vendored 的 jsoncpp/json.h 含非 UTF-8 字节，当本文件作为 UTF-8 编译单元时 MSVC 会
//对其报 C4828；该告警仅涉及 json.h 内注释、不影响代码，故就地屏蔽，说明同
//FolderTransfer/FolderRelayFraming.cpp。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../third-party/jsoncpp-1.9.0/json.h"
#pragma warning(pop)

//被测头文件<u>放在最后</u>：它引入 <winsock2.h>，而后者会带进 <windows.h> 里的 min /
//max 宏。这两个宏一旦先定义，Catch2 与 jsoncpp 里形如 std::numeric_limits<T>::min()
//的写法就会被当成宏调用，编译期报出一串 C4003"函数式宏的参数不足"。放到最后即可，
//本文件自身不用 min / max。
#include "../Feature/FolderTransfer/FolderRelayClient.h"

using namespace foldertransfer;

namespace {

//用 jsoncpp 把报到帧包体解回来。用真正的 JSON 解析器而不是比对字符串，是为了让用例
//验的是"产出的确是合法 JSON 且字段值正确"，而不是把实现里的拼接顺序抄一遍。
bool parseHelloBody(const std::string& body, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(body.c_str(), body.c_str() + body.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//一枚形态与真实令牌一致的样例令牌：sessionid|role|uid|peeruid|expire + "." + 签名，
//取自 docs/协议设计-v2.md 12.3.1 的示例。
const char* const kSampleToken = "7f3a2c1b-0004|5|100001|100002|1716272400000.5d41402abc4b2a76";

} // namespace

TEST_CASE("FRC1 报到帧包体：token 与 lane 按约定字段名写入，控制通道与数据通道均正确")
{
    //控制通道
    Json::Value root;
    REQUIRE(parseHelloBody(folderrelay::BuildRelayHelloBody(kSampleToken, kFolderControlLane), root));
    REQUIRE(root["token"].isString());
    REQUIRE(root["token"].asString() == kSampleToken);
    REQUIRE(root["lane"].isIntegral());
    REQUIRE(root["lane"].asInt() == kFolderControlLane);

    //数据通道：逐个走一遍 1..硬上限，确保序号原样写入
    for (int lane = 1; lane <= kFolderMaxLaneIndex; ++lane)
    {
        Json::Value one;
        REQUIRE(parseHelloBody(folderrelay::BuildRelayHelloBody(kSampleToken, lane), one));
        REQUIRE(one["token"].asString() == kSampleToken);
        REQUIRE(one["lane"].asInt() == lane);
    }
}

TEST_CASE("FRC2 报到帧包体：令牌里的双引号、反斜杠与控制字符被转义，解析后逐字节还原")
{
    //令牌格式目前不会出现这些字符，这里验的是"万一出现了也不会把包体拼坏"
    std::string token = "ab\"cd\\ef";
    token.push_back('\n');
    token.push_back(static_cast<char>(0x01));
    token += "gh";

    Json::Value root;
    REQUIRE(parseHelloBody(folderrelay::BuildRelayHelloBody(token, 1), root));
    REQUIRE(root["token"].asString() == token);
    REQUIRE(root["lane"].asInt() == 1);
}

TEST_CASE("FRC3 报到帧包体：UTF-8 多字节序列原样透传，不被逐字节转义打散")
{
    //中文按 UTF-8 是三字节序列，逐字节转义会把它拆坏；JSON 允许它原样出现
    const std::string token = "令牌-token-\xE4\xB8\xAD";

    Json::Value root;
    REQUIRE(parseHelloBody(folderrelay::BuildRelayHelloBody(token, 2), root));
    REQUIRE(root["token"].asString() == token);
}

TEST_CASE("FRC4 报到帧包体：空令牌仍产出合法 JSON，token 字段为空串")
{
    Json::Value root;
    REQUIRE(parseHelloBody(folderrelay::BuildRelayHelloBody(std::string(), kFolderControlLane), root));
    REQUIRE(root["token"].isString());
    REQUIRE(root["token"].asString().empty());
    REQUIRE(root["lane"].asInt() == kFolderControlLane);
}

TEST_CASE("FRC5 lane 序号校验：控制通道与 1..8 合法，负数与越过硬上限的被拒")
{
    REQUIRE(folderrelay::IsValidLaneIndex(kFolderControlLane));
    for (int lane = 1; lane <= kFolderMaxDataLanes; ++lane)
        REQUIRE(folderrelay::IsValidLaneIndex(lane));

    REQUIRE_FALSE(folderrelay::IsValidLaneIndex(-1));
    REQUIRE_FALSE(folderrelay::IsValidLaneIndex(kFolderMaxLaneIndex + 1));
    REQUIRE_FALSE(folderrelay::IsValidLaneIndex(kFolderMaxLaneIndex + 100));
}

TEST_CASE("FRC6 单帧发送上限：明文上限给加密膨胀让出了位置，压线帧不会在封帧处失败")
{
    //明文上限 + 加密膨胀恰好等于中转单帧包体上限：多一个字节都会让 net::encodeFrame 拒收
    REQUIRE(kFolderRelayMaxAppFrameBytes + net::STREAM_OVERHEAD_BYTES == kFolderRelayMaxBodyBytes);
    //文件分片是热路径上最大的一种应用帧，必须离上限有充足余量
    REQUIRE(kFolderRelayChunkBytes < kFolderRelayMaxAppFrameBytes);
}
