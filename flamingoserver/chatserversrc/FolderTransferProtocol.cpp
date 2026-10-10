/**
 *  v2 单聊文件夹传输信令编解码（服务端侧）实现，FolderTransferProtocol.cpp
 *
 *  取字段的四个内部辅助函数都刻意做了严格的类型判定：JSON 是客户端可以任意伪造的
 *  输入，jsoncpp 的 asString() / asInt64() 在类型不符时会抛异常，而本模块在会话层
 *  的分发路径上被调用，一旦抛出就会打断整条连接的收包流程。因此一律先判类型再取值，
 *  任何不合契约的输入都以返回 false 的方式安全失败。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#include "FolderTransferProtocol.h"
#include <stdlib.h>                  // atoll
#include "../jsoncpp1.9.0/json.h"

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或顶层不是对象返回 false
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 JSON 里以字符串编码的 64 位 ID 取出；字段缺失或不是字符串一律返回 0，
//由调用方按"ID 必须为正"判定非法。做法与 RemoteDesktopProtocol / MessageProtocol 一致
static int64_t getStringId(const Json::Value& root, const char* key)
{
    if (!root.isMember(key) || !root[key].isString())
        return 0;
    return static_cast<int64_t>(atoll(root[key].asString().c_str()));
}

//取出必填字符串字段；字段缺失、不是字符串、或取到空串一律返回 false
static bool getRequiredString(const Json::Value& root, const char* key, std::string& out)
{
    if (!root.isMember(key) || !root[key].isString())
        return false;
    out = root[key].asString();
    return !out.empty();
}

//取出可选字符串字段；字段缺失按空串处理并返回 true，存在但不是字符串返回 false
static bool getOptionalString(const Json::Value& root, const char* key, std::string& out)
{
    out.clear();
    if (!root.isMember(key))
        return true;
    if (!root[key].isString())
        return false;
    out = root[key].asString();
    return true;
}

//取出 32 位整数字段；字段缺失、不是整数、或超出 int 表示范围一律返回 false。
//isInt() 为真才调 asInt()，故不会因取值越界而抛异常；true / false 与数字字符串
//都不算整数，同样被拒
static bool getRequiredInt(const Json::Value& root, const char* key, int& out)
{
    if (!root.isMember(key) || !root[key].isInt())
        return false;
    out = root[key].asInt();
    return true;
}

//取出可选 32 位整数字段；字段缺失按 0 处理并返回 true，存在但不是整数返回 false
static bool getOptionalInt(const Json::Value& root, const char* key, int& out)
{
    out = 0;
    if (!root.isMember(key))
        return true;
    if (!root[key].isInt())
        return false;
    out = root[key].asInt();
    return true;
}

//取出非负的 64 位整数字段（规模统计用）。isInt64() 为真意味着该值能无损转成 int64，
//故随后的 asInt64() 不会抛异常；浮点、字符串、布尔与超出 int64 范围的数一律被拒
static bool getNonNegativeInt64(const Json::Value& root, const char* key, int64_t& out)
{
    if (!root.isMember(key) || !root[key].isInt64())
        return false;
    out = root[key].asInt64();
    return out >= 0;
}

//取出可选的链路路径字段：缺省按 0（没有走 P2P）处理并返回 true；存在时必须是 0~3 的整数
static bool getOptionalLinkPath(const Json::Value& root, int& out)
{
    if (!getOptionalInt(root, "linkpath", out))
        return false;
    return out >= kFolderLinkPathNone && out <= kFolderLinkPathMax;
}

//取出可选的 MD5 字段：缺省为空并返回 true；存在时必须是 32 位十六进制字符串，统一转成小写
static bool getOptionalMd5(const Json::Value& root, std::string& out)
{
    out.clear();
    if (!root.isMember("md5"))
        return true;
    if (!root["md5"].isString())
        return false;
    std::string v = root["md5"].asString();
    if (v.size() != kFolderMd5HexLength)
        return false;
    for (size_t i = 0; i < v.size(); ++i)
    {
        char c = v[i];
        if (c >= 'A' && c <= 'F')
            c = static_cast<char>(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
        v[i] = c;
    }
    out = v;
    return true;
}

bool FolderTransferProtocol::parseInviteRequest(const std::string& json, FolderInviteRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //接收方 userid 以字符串编码传输，非字符串或非正数一律视为非法
    req.peerid = getStringId(root, "peerid");
    if (req.peerid <= 0)
        return false;

    //根目录名必填：接收端要用它在选定位置建根目录，空名字无从落地
    if (!getRequiredString(root, "foldername", req.foldername))
        return false;

    //三项规模统计必填且不得为负；三项同时为 0 是合法的（发送一个空目录）
    if (!getNonNegativeInt64(root, "totaldirs", req.totaldirs))
        return false;
    if (!getNonNegativeInt64(root, "totalfiles", req.totalfiles))
        return false;
    if (!getNonNegativeInt64(root, "totalsize", req.totalsize))
        return false;

    //传输种类可缺省（2026-10-04 之前的客户端不带，按文件夹传输）；存在时只认 0 / 1
    if (!getOptionalInt(root, "kind", req.kind))
        return false;
    if (req.kind != folder_kind_folder && req.kind != folder_kind_file)
        return false;

    //单文件直传：恰好一个文件、没有目录；否则接收端按单个文件落地时对不上
    if (req.kind == folder_kind_file && (req.totaldirs != 0 || req.totalfiles != 1))
        return false;

    return true;
}

bool FolderTransferProtocol::parseResponseRequest(const std::string& json,
                                                  std::string& sessionid, int& accept)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //缺会话 id 无从定位是哪一次传输，视为非法
    if (!getRequiredString(root, "sessionid", sessionid))
        return false;

    if (!getRequiredInt(root, "accept", accept))
        return false;

    //accept 只允许"接收"与"拒绝"两个取值，其余一律视为非法请求
    return accept == folder_response_reject || accept == folder_response_accept;
}

bool FolderTransferProtocol::parseSessionRequest(const std::string& json, std::string& sessionid)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //缺会话 id 视为非法
    return getRequiredString(root, "sessionid", sessionid);
}

bool FolderTransferProtocol::parseCloseRequest(const std::string& json, FolderCloseRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //缺会话 id 视为非法
    if (!getRequiredString(root, "sessionid", req.sessionid))
        return false;

    if (!getRequiredInt(root, "result", req.result))
        return false;

    //result 只允许"成功结束"与"失败结束"两个取值
    if (req.result != folder_close_success && req.result != folder_close_failed)
        return false;

    //errcode 可缺省（按 0 处理），存在时必须是整数
    if (!getOptionalInt(root, "errcode", req.errcode))
        return false;

    if (req.result == folder_close_failed)
    {
        //失败结束必须给出本组之内的失败原因：服务端要把它原样透传给对端的状态推送，
        //两端才能看到同一条原因；越界的错误码会让对端显示不出文案
        if (!isValidErrorCode(req.errcode))
            return false;
    }
    else
    {
        //成功结束不得携带失败原因，否则与 result 自相矛盾
        if (req.errcode != 0)
            return false;
    }

    //errmsg 是给排查用的英文诊断串，可缺省
    if (!getOptionalString(root, "errmsg", req.errmsg))
        return false;

    //链路路径与 MD5（2026-10-04 起）都可缺省，存在时须合法
    if (!getOptionalLinkPath(root, req.linkpath))
        return false;
    if (!getOptionalMd5(root, req.md5))
        return false;

    return true;
}

std::string FolderTransferProtocol::buildInviteResponse(int code, const std::string& msg,
                                                        const std::string& sessionid, int state)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    //仅成功且会话 id 非空时下发会话 id 与初始状态；失败应答不带这两个字段
    if (code == 0 && !sessionid.empty())
    {
        root["sessionid"] = sessionid;
        root["state"]     = state;
    }
    return writeCompact(root);
}

std::string FolderTransferProtocol::buildInviteLimitResponse(int code, const std::string& msg,
                                                             int64_t maxfiles, int64_t maxsize)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    //上限值以 JSON 整数编码：maxsize 默认 50 GiB，超出 32 位，须按 64 位写入
    root["maxfiles"] = static_cast<Json::Int64>(maxfiles);
    root["maxsize"]  = static_cast<Json::Int64>(maxsize);
    return writeCompact(root);
}

std::string FolderTransferProtocol::buildInviteNotify(const std::string& sessionid, int64_t fromid,
                                                      const std::string& fromname,
                                                      const FolderInviteRequest& req)
{
    Json::Value root;
    root["sessionid"] = sessionid;
    //fromid 以字符串编码，避免客户端按双精度解析时丢精度
    root["from"]       = std::to_string(fromid);
    root["fromname"]   = fromname;
    //目录名与三项规模统计由 1810 请求原样透传，接收端气泡直接据此显示
    root["foldername"] = req.foldername;
    root["totaldirs"]  = static_cast<Json::Int64>(req.totaldirs);
    root["totalfiles"] = static_cast<Json::Int64>(req.totalfiles);
    root["totalsize"]  = static_cast<Json::Int64>(req.totalsize);
    //只有单文件直传才写 kind：文件夹传输的推送与 2026-10-04 之前逐字相同
    if (req.kind == folder_kind_file)
        root["kind"] = folder_kind_file;
    return writeCompact(root);
}

std::string FolderTransferProtocol::buildSessionResponse(int code, const std::string& msg,
                                                         const std::string& sessionid)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    //会话 id 非空即原样带回（出错应答也带），供客户端定位是哪一次传输
    if (!sessionid.empty())
        root["sessionid"] = sessionid;
    return writeCompact(root);
}

std::string FolderTransferProtocol::buildStateNotify(const std::string& sessionid, int state,
                                                     int errcode, const std::string& errmsg)
{
    Json::Value root;
    root["sessionid"] = sessionid;
    root["state"]     = state;

    if (state == folder_state_failed)
    {
        //只有"已失败"态才带有意义的失败原因，客户端据 errcode 本地化文案
        root["errcode"] = errcode;
        root["errmsg"]  = errmsg;
    }
    else
    {
        //其余状态两个字段无意义，统一置空，保证协议里"errcode 恒为 0"这条不变式
        root["errcode"] = 0;
        root["errmsg"]  = "";
    }

    return writeCompact(root);
}

std::string FolderTransferProtocol::buildRelayInfo(const std::string& sessionid, int role,
                                                   const std::string& relayip, int relayport,
                                                   const std::string& token, int lanes,
                                                   const std::string& p2pSessionId)
{
    Json::Value root;
    root["sessionid"] = sessionid;
    root["role"]      = role;
    //地址字段名与远程桌面 cmd_rd_relay_info、音视频通话的中转参数推送保持一致，
    //客户端的解析代码因而可以复用
    root["relayip"]   = relayip;
    root["relayport"] = relayport;
    root["token"]     = token;
    //数据通道条数，不含 lane 0 那条控制通道
    root["lanes"]     = lanes;
    //P2P 会话号：只在为本次传输建了 P2P 会话时带上，老客户端不认识这个字段，照旧走中转
    if (!p2pSessionId.empty())
        root["p2psessionid"] = p2pSessionId;
    return writeCompact(root);
}

std::string FolderTransferProtocol::buildRecordContent(const FolderTransferRecord& rec)
{
    Json::Value root;
    root["sessionid"]  = rec.sessionid;
    root["foldername"] = rec.foldername;
    //三项规模统计以 JSON 整数编码：totalsize 可超过 4 GiB，须按 64 位写入
    root["totaldirs"]  = static_cast<Json::Int64>(rec.totaldirs);
    root["totalfiles"] = static_cast<Json::Int64>(rec.totalfiles);
    root["totalsize"]  = static_cast<Json::Int64>(rec.totalsize);
    root["result"]     = rec.result;

    //只有"已失败"这一种结局才带有意义的失败原因，其余结局一律写 0。这条不变式与
    //buildStateNotify 里的同一条一样集中在此处保证，各调用点不必各写一遍
    if (rec.result == folder_state_failed)
        root["errcode"] = rec.errcode;
    else
        root["errcode"] = 0;

    //链路路径只在走过 P2P 时写（2026-10-04 起），客户端据此在历史里显示「P2P」徽章
    if (rec.linkpath > kFolderLinkPathNone)
        root["linkpath"] = rec.linkpath;

    return writeCompact(root);
}

bool FolderTransferProtocol::parseRecordContent(const std::string& json, FolderTransferRecord& rec)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //会话 id 与根目录名都必填：前者用于把这条记录与那次传输对上，后者是记录条正文里
    //唯一给人看的信息，缺了这条记录就没有意义
    if (!getRequiredString(root, "sessionid", rec.sessionid))
        return false;
    if (!getRequiredString(root, "foldername", rec.foldername))
        return false;

    //三项规模统计必填且不得为负；三项同时为 0 是合法的（传的是一个空目录）
    if (!getNonNegativeInt64(root, "totaldirs", rec.totaldirs))
        return false;
    if (!getNonNegativeInt64(root, "totalfiles", rec.totalfiles))
        return false;
    if (!getNonNegativeInt64(root, "totalsize", rec.totalsize))
        return false;

    if (!getRequiredInt(root, "result", rec.result))
        return false;

    //记录条只在会话走到终态时产生，故 result 必须是文件夹传输的终态值（3~8）；响铃中 / 传输中这两个
    //过程态出现在这里即说明记录是伪造的或写坏了。「已改为离线发送」(9) 只属于单文件直传，也不会出现在文件夹记录里
    if (rec.result < folder_state_rejected || rec.result > folder_state_completed)
        return false;

    //errcode 可缺省（按 0 处理），存在时必须是整数
    if (!getOptionalInt(root, "errcode", rec.errcode))
        return false;

    if (rec.result == folder_state_failed)
    {
        //失败结局必须给出本组之内的失败原因：客户端要据它本地化那行失败文案，
        //越界的错误码显示不出任何文案
        if (!isValidErrorCode(rec.errcode))
            return false;
    }
    else
    {
        //其余结局不得携带失败原因，否则与 result 自相矛盾
        if (rec.errcode != 0)
            return false;
    }

    //链路路径可缺省（2026-10-04 之前的记录没有），存在时须合法
    return getOptionalLinkPath(root, rec.linkpath);
}

std::string FolderTransferProtocol::buildRecordClientMsgId(const std::string& sessionid,
                                                           int attemptSeq)
{
    //前缀取一个客户端不会用到的固定串，使它必然不与客户端生成的 UUID 型 clientmsgid 相撞；
    //末尾的尝试序号使不同次尝试各得一个键，从而各留一条记录
    return "folder-" + sessionid + "-" + std::to_string(attemptSeq);
}

bool FolderTransferProtocol::isTerminalState(int state)
{
    //enum folder_transfer_state 取值连续，3~9 这一段全是终态：
    //已拒绝 / 已取消 / 对方离线 / 已超时 / 已失败 / 已完成 / 已改为离线发送
    return state >= folder_state_rejected && state <= folder_state_to_offline;
}

bool FolderTransferProtocol::isValidState(int state)
{
    //enum folder_transfer_state 取值连续（1~9），按上下界判定即可
    return state >= folder_state_ringing && state <= folder_state_to_offline;
}

bool FolderTransferProtocol::isValidErrorCode(int errcode)
{
    //error_code_folder_* 这组码 606~620 连续，按上下界判定；621 已被敏感词占用，单文件直传的 622 单独判
    return (errcode >= error_code_folder_rejected && errcode <= error_code_folder_busy)
        || errcode == error_code_folder_direct_unavailable;
}

//==== 2026-10-04 单文件直传与链路路径 ====

bool FolderTransferProtocol::parseCancelRequest(const std::string& json, std::string& sessionid, int& tooffline)
{
    tooffline = 0;
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //缺会话 id 视为非法
    if (!getRequiredString(root, "sessionid", sessionid))
        return false;

    //tooffline 可缺省（普通撤回），存在时只认 0 / 1
    if (!getOptionalInt(root, "tooffline", tooffline))
        return false;
    return tooffline == 0 || tooffline == 1;
}

std::string FolderTransferProtocol::buildDirectFileContent(const DirectFileRecord& rec)
{
    Json::Value root;
    //与普通文件消息同名的三项：客户端照常按文件气泡渲染
    root["filename"] = rec.filename;
    root["filesize"] = static_cast<Json::Int64>(rec.filesize);
    if (!rec.md5.empty())
        root["md5"] = rec.md5;
    //直传标记与会话 id：客户端据前者知道服务器上没有这个文件，据后者接管聊天窗里那条实时气泡
    root["direct"]    = 1;
    root["sessionid"] = rec.sessionid;
    root["result"]    = rec.result;
    if (rec.linkpath > kFolderLinkPathNone)
        root["linkpath"] = rec.linkpath;
    return writeCompact(root);
}

bool FolderTransferProtocol::parseDirectFileContent(const std::string& json, DirectFileRecord& rec)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //没有 direct=1 的是普通离线文件消息，不归本函数解析
    int direct = 0;
    if (!getOptionalInt(root, "direct", direct) || direct != 1)
        return false;

    if (!getRequiredString(root, "sessionid", rec.sessionid))
        return false;
    if (!getRequiredString(root, "filename", rec.filename))
        return false;
    if (!getNonNegativeInt64(root, "filesize", rec.filesize))
        return false;
    if (!getRequiredInt(root, "result", rec.result))
        return false;

    //直传只在完成与拒绝两种结局留下文件消息（见 shouldWriteRecord）
    if (rec.result != folder_state_completed && rec.result != folder_state_rejected)
        return false;

    if (!getOptionalMd5(root, rec.md5))
        return false;
    return getOptionalLinkPath(root, rec.linkpath);
}

bool FolderTransferProtocol::shouldWriteRecord(int kind, int state)
{
    //单文件直传：完成与拒绝各留一条文件消息；其余终态之后发送端要么续传、要么改走上传服务器，
    //随后的离线文件消息或最终结局代替它，不留中间状态
    if (kind == folder_kind_file)
        return state == folder_state_completed || state == folder_state_rejected;

    //文件夹传输：每个终态都留一条记录（状态 9 只属于单文件直传，不会出现在文件夹传输里）
    return state >= folder_state_rejected && state <= folder_state_completed;
}
