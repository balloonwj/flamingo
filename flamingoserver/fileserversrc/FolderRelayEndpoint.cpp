/**
 *  文件夹传输中转端点实现，FolderRelayEndpoint.cpp
 *  balloonwj@qq.com   2026-08-14
 */
#include "FolderRelayEndpoint.h"
#include <stdlib.h>
#include <time.h>
#include "../base/AsyncLog.h"
#include "../utils/RelayToken.h"
#include "../jsoncpp1.9.0/json.h"
#include "../chatserversrc/Protocol.h"   // enum error_code
#include "FileSession.h"

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或不是对象返回 false。
//与 FileSession.cpp / RelaySession.cpp 里的同名函数同构，各自是文件内静态函数
static bool parseJsonObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//通道序号的缺省值：握手包体里没有 lane 字段、或字段类型不对时取本值。它落在
//FolderRelayHub::isValidLane 的合法区间之外，因而会被后面的越界判定挡下，
//不必再为「字段缺失」单开一条分支
static const int kInvalidLane = -1;

//令牌里的 expire 字段以 Unix 毫秒计，而 time() 返回的是秒，故换算时乘本值
static const int64_t kMillisPerSecond = 1000;

//取握手包体里的通道序号：数字与字符串两种写法都收，缺失或类型不符返回 kInvalidLane
static int readLane(const Json::Value& root)
{
    const Json::Value& v = root["lane"];
    if (v.isNumeric())
        return v.asInt();

    if (v.isString())
        return atoi(v.asString().c_str());

    return kInvalidLane;
}

FolderRelayEndpoint::FolderRelayEndpoint(FolderRelayHub* hub, const std::string& tokenKey)
    : m_hub(hub),
      m_tokenKey(tokenKey),
      m_joined(false),
      m_lane(kInvalidLane),
      m_role(0)
{
}

FolderRelayEndpoint::~FolderRelayEndpoint()
{
}

int FolderRelayEndpoint::handleHello(const std::string& body, const SessionPtr& self,
                                     const std::string& peerAddr)
{
    //配对表由 FileServer 持有并在构造会话时传入，为空属编程错误：没有它无从配对，
    //放行反而会让后续每一帧都找不到对端
    if (m_hub == NULL)
    {
        LOGE("folder relay hello: relay hub is NULL, client: %s", peerAddr.c_str());
        return error_code_server_internal;
    }

    //重复报到：一条连接只能占一条通道。放行会在配对表里留下两条登记，而 leaveByConn
    //只摘掉首个匹配项，剩下那条就成了永远摘不掉的残留
    if (m_joined)
    {
        LOGE("folder relay hello: duplicate hello, session: %s, lane: %d, client: %s",
             m_sessionId.c_str(), m_lane, peerAddr.c_str());
        return error_code_bad_request;
    }

    //imgserver 复用 FileServer / FileSession 这套代码，但不承载文件夹传输，其中转密钥为空。
    //空密钥下 HMAC 形同虚设（任何人都能算出同样的签名），故直接拒绝，不进验签流程
    if (m_tokenKey.empty())
    {
        LOGE("folder relay hello: relay token key is not configured on this server, "
             "folder relay is disabled here, client: %s", peerAddr.c_str());
        return error_code_permission_denied;
    }

    Json::Value root;
    if (!parseJsonObject(body, root))
    {
        LOGE("folder relay hello: bad request body, client: %s", peerAddr.c_str());
        return error_code_bad_request;
    }

    std::string token = root["token"].isString() ? root["token"].asString() : std::string();
    int         lane  = readLane(root);

    //验签 + 验过期。令牌由 chatserver 在接收端同意之后签发，fileserver 只本地校验，
    //不回连 chatserver、不查库
    int64_t now = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    RelayTokenPayload payload;
    if (!RelayToken::verify(token, m_tokenKey, now, payload))
    {
        LOGE("folder relay hello: invalid or expired token, lane: %d, client: %s",
             lane, peerAddr.c_str());
        return error_code_session_expired;
    }

    //角色必须是文件夹传输的那两个：远程桌面与音视频的令牌（角色 1~4）虽然签名同样合法，
    //但拿到这里来用属于两套中转互相混用，一律拒绝
    if (payload.role != RelayToken::role_folder_sender
        && payload.role != RelayToken::role_folder_receiver)
    {
        LOGE("folder relay hello: role %d is not a folder transfer role, session: %s, client: %s",
             payload.role, payload.sessionid.c_str(), peerAddr.c_str());
        return error_code_permission_denied;
    }

    //通道序号由客户端自报、不写进令牌，必须卡住上限，否则一枚令牌就能开出任意多条连接，
    //把 Windows 上 select 的连接数配额耗光（见方案第 3.6、9 节）
    if (!FolderRelayHub::isValidLane(lane))
    {
        LOGE("folder relay hello: lane %d out of range [%d, %d], session: %s, client: %s",
             lane, fileserver::kFolderControlLane, fileserver::kFolderMaxLane,
             payload.sessionid.c_str(), peerAddr.c_str());
        return error_code_bad_request;
    }

    //登记进配对表：同一个 (sessionid, lane, role) 重复报到、该通道已有两端时都会被拒
    if (!m_hub->join(payload.sessionid, lane, payload.role, self))
    {
        LOGE("folder relay hello: join failed (duplicate lane endpoint or lane is full), "
             "session: %s, lane: %d, role: %d, client: %s",
             payload.sessionid.c_str(), lane, payload.role, peerAddr.c_str());
        return error_code_server_internal;
    }

    m_joined    = true;
    m_sessionId = payload.sessionid;
    m_lane      = lane;
    m_role      = payload.role;

    LOGI("folder relay hello ok, session: %s, lane: %d, role: %d, uid: %lld, peeruid: %lld, "
         "lanes joined on this session: %u, client: %s",
         m_sessionId.c_str(), m_lane, m_role,
         static_cast<long long>(payload.uid), static_cast<long long>(payload.peeruid),
         static_cast<unsigned>(m_hub->getLaneCount(m_sessionId)), peerAddr.c_str());
    return error_code_ok;
}

FolderRelayEndpoint::SessionPtr FolderRelayEndpoint::getPeer(const ConnectionPtr& self)
{
    //尚未握手的连接不在表里，直接按「没有对端」处理，免得白查一遍
    if (!m_joined || m_hub == NULL)
        return SessionPtr();

    return m_hub->getPeer(m_sessionId, m_lane, self);
}

bool FolderRelayEndpoint::detachByConnection(FolderRelayHub& hub, const ConnectionPtr& conn,
                                             std::string& sessionIdOut,
                                             std::vector<SessionPtr>& survivorsOut)
{
    sessionIdOut.clear();

    //leaveByConn 只在确实命中时才回填会话 id，故这里以「回填了没有」判断本连接是不是
    //一条中转连接。它的返回值是同一条通道上的对端，而下面的 getSessionEndpoints 会把它
    //连同其余通道一并取回来，所以此处不必单独接住
    hub.leaveByConn(conn, &sessionIdOut);
    if (sessionIdOut.empty())
        return false;

    //任一条通道断开即整个文件夹传输失败：把该次传输当前仍登记着的全部通道取出来交给
    //调用方关闭，再把整条会话清掉，使随后各条通道断开时的 leaveByConn 都成为空操作
    hub.getSessionEndpoints(sessionIdOut, survivorsOut);
    hub.removeSession(sessionIdOut);
    return true;
}
