/**
 *  v2 单聊文件夹传输信令编解码（客户端侧）实现，FolderProtocolClient.cpp
 *
 *  取字段的几个内部辅助函数都先判类型再取值：jsoncpp 的 asString() 在字段是数组 / 对象时
 *  会抛异常，asInt() / asInt64() 在取值越界时同样会抛，而本模块在收包分发路径上被调用，
 *  一旦抛出就会打断整条连接的收包流程。因此任何不合契约的输入都以返回 false 的方式安全
 *  失败，绝不抛出。
 *
 *  balloonwj@qq.com   2026-08-14
 */
#include "FolderProtocolClient.h"
#include <stdlib.h>                     // strtoll
#include "Protocol.h"                   // folder_transfer_state 与 error_code_folder_* 的取值范围
//vendored 的 jsoncpp/json.h 含非 UTF-8 字节，当本文件作为 UTF-8 编译单元时 MSVC 会对其
//报 C4828；该告警仅涉及 json.h 内注释、不影响代码，故就地屏蔽，不污染编译输出。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../third-party/jsoncpp-1.9.0/json.h"
#pragma warning(pop)

namespace net {

//==================== 协议取值常量 ====================

//cmd_folder_response(1812) 的 accept 字段取值，与服务端 enum folder_response_type 一致；
//服务端对这两个值以外的取值一律判非法请求
static const int kFolderResponseReject = 0;   //拒绝接收本文件夹
static const int kFolderResponseAccept = 1;   //同意接收本文件夹

//cmd_folder_close(1814) 的 result 字段取值，与服务端 enum folder_close_result 一致
static const int kFolderCloseSuccess = 0;   //成功结束：全部文件传完并通过校验，errcode 须为 0
static const int kFolderCloseFailed  = 1;   //失败结束：errcode 须为 error_code_folder_* 之一

//TCP 端口号的合法取值范围，用于校验 cmd_folder_relay_info 下发的 relayport
static const int kMinPortNumber = 1;
static const int kMaxPortNumber = 65535;

//数据通道条数的下限：至少要有一条数据通道，否则一个文件字节也传不了。上限（8 条）由中转层
//foldertransfer::kFolderMaxDataLanes 把关，本模块不再重复一份
static const int kMinRelayLanes = 1;

//==================== JSON 读写辅助 ====================

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 JSON 文本解析成对象 Json::Value；非法 JSON 或顶层非对象返回 false
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//取出字符串字段；字段缺失或不是字符串一律返回空串。不调 asString() 兜底转换，是为了让
//"服务端把字符串字段写成了数字"这类口径不一致暴露成空串，而不是被静默转换掩盖过去
static std::string getString(const Json::Value& root, const char* key)
{
    if (!root.isMember(key) || !root[key].isString())
        return std::string();
    return root[key].asString();
}

//取出以十进制字符串编码的 64 位 ID。v2 约定 userid 这类 ID 走字符串编码，避免 JSON 数值
//按双精度解析时丢精度；整数形式一并接受，是为了容纳"服务端某处漏写引号"的情形，读侧宽容
//一点不会带来歧义。字段缺失或类型不符返回 0，由调用方按"ID 必须为正"判非法
static int64_t getId(const Json::Value& root, const char* key)
{
    if (!root.isMember(key))
        return 0;
    if (root[key].isString())
        return static_cast<int64_t>(strtoll(root[key].asString().c_str(), NULL, 10));
    if (root[key].isInt64())
        return root[key].asInt64();
    return 0;
}

//取出 32 位整数字段（状态、错误码、角色、端口、通道条数等）；字段缺失、不是整数、或超出
//int 表示范围一律返回 0。isInt() 为真才调 asInt()，故不会因取值越界而抛异常
static int getInt(const Json::Value& root, const char* key)
{
    if (!root.isMember(key) || !root[key].isInt())
        return 0;
    return root[key].asInt();
}

//取出 64 位整数字段（字节数与计数等）；字段缺失或不是 64 位整数一律返回 0。
//isInt64() 为真意味着该值能无损转成 int64，故随后的 asInt64() 不会抛异常
static int64_t getInt64(const Json::Value& root, const char* key)
{
    if (!root.isMember(key) || !root[key].isInt64())
        return 0;
    return root[key].asInt64();
}

//取出非负的 64 位整数字段（规模统计用）；字段缺失、不是 64 位整数、或取值为负返回 false。
//与服务端 FolderTransferProtocol.cpp 的同名判定同口径，两侧对"什么算合法规模"看法一致
static bool getRequiredNonNegativeInt64(const Json::Value& root, const char* key, int64_t& out)
{
    if (!root.isMember(key) || !root[key].isInt64())
        return false;
    out = root[key].asInt64();
    return out >= 0;
}

//==================== 请求构造 ====================

std::string FolderProtocolClient::buildInviteRequest(int64_t peerUserId, const std::string& folderName,
                                                     int64_t totalDirs, int64_t totalFiles, int64_t totalSize)
{
    //服务端对这四项都会再校验一遍；此处先拦一道，不把必然被拒的包发出去
    if (peerUserId <= 0 || folderName.empty())
        return std::string();
    if (totalDirs < 0 || totalFiles < 0 || totalSize < 0)
        return std::string();

    Json::Value root;
    //peerid 以十进制字符串编码，避免大整数精度丢失
    root["peerid"]     = std::to_string(peerUserId);
    root["foldername"] = folderName;
    //三项规模统计以 JSON 64 位整数编码：totalsize 可以超过 4 GiB，32 位装不下
    root["totaldirs"]  = static_cast<Json::Int64>(totalDirs);
    root["totalfiles"] = static_cast<Json::Int64>(totalFiles);
    root["totalsize"]  = static_cast<Json::Int64>(totalSize);
    return writeCompact(root);
}

std::string FolderProtocolClient::buildResponse(const std::string& sessionId, bool accept)
{
    if (sessionId.empty())
        return std::string();

    Json::Value root;
    root["sessionid"] = sessionId;
    root["accept"]    = accept ? kFolderResponseAccept : kFolderResponseReject;
    return writeCompact(root);
}

std::string FolderProtocolClient::buildCancel(const std::string& sessionId)
{
    if (sessionId.empty())
        return std::string();

    Json::Value root;
    root["sessionid"] = sessionId;
    return writeCompact(root);
}

std::string FolderProtocolClient::buildClose(const std::string& sessionId, bool success,
                                             int errCode, const std::string& errMsg)
{
    if (sessionId.empty())
        return std::string();

    //失败结束必须给出本组之内的失败原因：服务端要把它原样透传给对端的状态推送，越界的错误码
    //会被服务端判为非法请求，对端也显示不出文案
    if (!success && !isValidErrorCode(errCode))
        return std::string();

    Json::Value root;
    root["sessionid"] = sessionId;
    root["result"]    = success ? kFolderCloseSuccess : kFolderCloseFailed;

    if (success)
    {
        //成功结束不得携带失败原因，否则与 result 自相矛盾，服务端判非法请求。这条不变式集中
        //在此处保证，各调用点不必各写一遍
        root["errcode"] = 0;
        root["errmsg"]  = "";
    }
    else
    {
        root["errcode"] = errCode;
        root["errmsg"]  = errMsg;
    }

    return writeCompact(root);
}

std::string FolderProtocolClient::buildRetry(const std::string& sessionId)
{
    if (sessionId.empty())
        return std::string();

    //包体与 cmd_folder_cancel 相同，区别只在服务端的处理：那条撤回响铃中的邀请，这条在保留期
    //内重试续传
    Json::Value root;
    root["sessionid"] = sessionId;
    return writeCompact(root);
}

//==================== 应答与推送解析 ====================

bool FolderProtocolClient::parseInviteResponse(const std::string& jsonbody, FolderInviteResponse& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //没有整型 code 视为非法应答
    if (!root["code"].isIntegral())
        return false;

    out.m_code = getInt(root, "code");
    out.m_msg  = getString(root, "msg");

    if (out.m_code == 0)
    {
        //成功应答必带会话 id 与初始状态；缺会话 id 则后续信令无从发起，视为非法应答
        out.m_sessionId = getString(root, "sessionid");
        if (out.m_sessionId.empty())
            return false;
        out.m_state = getInt(root, "state");
    }
    else
    {
        //规模超限（609 / 610）的应答回带服务端配置的上限值，其余失败没有这两个字段，取到 0
        out.m_maxFiles = getInt64(root, "maxfiles");
        out.m_maxSize  = getInt64(root, "maxsize");
    }

    return true;
}

bool FolderProtocolClient::parseSessionResponse(const std::string& jsonbody, FolderSessionResponse& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //没有整型 code 视为非法应答
    if (!root["code"].isIntegral())
        return false;

    out.m_code = getInt(root, "code");
    out.m_msg  = getString(root, "msg");
    //会话 id 可缺省：请求格式非法时服务端解析不出它，应答里就没有这个字段
    out.m_sessionId = getString(root, "sessionid");
    return true;
}

bool FolderProtocolClient::parseInviteNotify(const std::string& jsonbody, FolderInviteNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    out.m_sessionId = getString(root, "sessionid");
    //发送端 userid 的字段名是 from（不是远程桌面那条的 fromid），与服务端
    //FolderTransferProtocol::buildInviteNotify 一致
    out.m_fromId     = getId(root, "from");
    out.m_fromName   = getString(root, "fromname");
    out.m_folderName = getString(root, "foldername");

    //缺会话 id、发送端 userid 非正、或缺根目录名，接收端的气泡都无从显示与落地
    if (out.m_sessionId.empty() || out.m_fromId <= 0 || out.m_folderName.empty())
        return false;

    //三项规模统计必填且不得为负；三项同时为 0 是合法的（对方发来的是一个空目录）
    if (!getRequiredNonNegativeInt64(root, "totaldirs", out.m_totalDirs))
        return false;
    if (!getRequiredNonNegativeInt64(root, "totalfiles", out.m_totalFiles))
        return false;
    if (!getRequiredNonNegativeInt64(root, "totalsize", out.m_totalSize))
        return false;

    return true;
}

bool FolderProtocolClient::parseStateNotify(const std::string& jsonbody, FolderStateNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    out.m_sessionId = getString(root, "sessionid");
    out.m_state     = getInt(root, "state");
    out.m_errCode   = getInt(root, "errcode");
    out.m_errMsg    = getString(root, "errmsg");

    //缺会话 id 无从定位是哪一次传输；状态越界则客户端无从映射成气泡样式，两者都按非法推送丢弃
    if (out.m_sessionId.empty() || !isValidState(out.m_state))
        return false;
    return true;
}

bool FolderProtocolClient::parseRelayInfo(const std::string& jsonbody, FolderRelayInfo& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    out.m_sessionId = getString(root, "sessionid");
    out.m_role      = getInt(root, "role");
    //地址字段名为 relayip / relayport，与远程桌面 cmd_rd_relay_info、音视频通话的中转参数
    //推送同名；服务端 FolderTransferProtocol::buildRelayInfo 也是这两个名字
    out.m_relayIp   = getString(root, "relayip");
    out.m_relayPort = getInt(root, "relayport");
    out.m_token     = getString(root, "token");
    out.m_lanes     = getInt(root, "lanes");

    //缺会话 id 或令牌，中转连接报到不上去
    if (out.m_sessionId.empty() || out.m_token.empty())
        return false;

    //地址与端口缺一不可，否则连不上 fileserver 的中转端口
    if (out.m_relayIp.empty() || out.m_relayPort < kMinPortNumber || out.m_relayPort > kMaxPortNumber)
        return false;

    //角色决定本端是推字节还是收字节，取值只能是文件夹传输这两个；拿到远程桌面或音视频的角色
    //编号说明服务端下发串了会话
    if (out.m_role != folder_role_sender && out.m_role != folder_role_receiver)
        return false;

    //数据通道条数至少一条，否则建不出可传字节的通道
    if (out.m_lanes < kMinRelayLanes)
        return false;

    return true;
}

//==================== 取值校验 ====================

bool FolderProtocolClient::isValidState(int state)
{
    //enum folder_transfer_state 取值连续（1~8），按上下界判定即可
    return state >= folder_state_ringing && state <= folder_state_completed;
}

bool FolderProtocolClient::isValidErrorCode(int errCode)
{
    //error_code_folder_* 这组码取值连续（606~620），按上下界判定即可
    return errCode >= error_code_folder_rejected && errCode <= error_code_folder_busy;
}

bool FolderProtocolClient::isTerminalState(int state)
{
    //响铃中(1) 与传输中(2) 是过程状态，会话还没有结局；3~8 六个都是定局
    return state >= folder_state_rejected && state <= folder_state_completed;
}

//==================== 传输记录条的 content 编解码 ====================

std::string FolderProtocolClient::buildRecordContent(const FolderRecordContent& rec)
{
    //缺会话 id 的记录无从与聊天窗里那条本地气泡对上；非终态的结局说不清这次传输到底
    //怎么收的场。两者都不该被写进聊天记录，就地返回空串暴露问题
    if (rec.m_sessionId.empty() || !isTerminalState(rec.m_result))
        return std::string();

    //三项规模统计不得为负；同时为 0 是合法的（发送了一个空目录）
    if (rec.m_totalDirs < 0 || rec.m_totalFiles < 0 || rec.m_totalSize < 0)
        return std::string();

    Json::Value root;
    root["sessionid"]  = rec.m_sessionId;
    root["foldername"] = rec.m_folderName;
    //三项规模统计以 JSON 64 位整数编码：totalsize 可以超过 4 GiB，32 位装不下
    root["totaldirs"]  = static_cast<Json::Int64>(rec.m_totalDirs);
    root["totalfiles"] = static_cast<Json::Int64>(rec.m_totalFiles);
    root["totalsize"]  = static_cast<Json::Int64>(rec.m_totalSize);
    root["result"]     = rec.m_result;
    //「errcode 仅失败态有意义」这条不变式在此归一：其余五种结局一律写 0，
    //各调用点不必各判一遍，也不会出现"已完成却带着失败原因"这种自相矛盾的记录
    root["errcode"]    = (rec.m_result == folder_state_failed) ? rec.m_errCode : 0;
    return writeCompact(root);
}

bool FolderProtocolClient::parseRecordContent(const std::string& jsonbody, FolderRecordContent& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    out.m_sessionId  = getString(root, "sessionid");
    out.m_folderName = getString(root, "foldername");
    //缺会话 id 就没法把这条记录与本地气泡对上，也没法判定是不是同一次传输
    if (out.m_sessionId.empty())
        return false;

    //结局必须是终态：过程状态与越界值都无从映射成气泡样式，按脏数据丢弃
    out.m_result = getInt(root, "result");
    if (!isTerminalState(out.m_result))
        return false;

    //三项规模统计必填且不得为负。写成字符串、写成小数、缺字段都在这里被挡下 ——
    //气泡与历史行要拿它们直接显示"共 n 个文件、x GB"，取到脏值就会显示成负数
    if (!getRequiredNonNegativeInt64(root, "totaldirs", out.m_totalDirs))
        return false;
    if (!getRequiredNonNegativeInt64(root, "totalfiles", out.m_totalFiles))
        return false;
    if (!getRequiredNonNegativeInt64(root, "totalsize", out.m_totalSize))
        return false;

    //与构造侧对称地归一：非失败结局一律把失败原因抹成 0，免得界面显示出一条
    //本不该存在的失败原因
    out.m_errCode = (out.m_result == folder_state_failed) ? getInt(root, "errcode") : 0;
    return true;
}

} // namespace net
