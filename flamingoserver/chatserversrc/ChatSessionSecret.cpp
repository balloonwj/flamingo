/**
 *  ChatSession 的单聊密聊部分（cmd_secret_* 2601~2609，docs/密聊方案.md 第 4、5 节）：把收到的帧交给
 *  SecretChatManager，把它产出的应答与推送按连接标识发出；连接下线、企业关闭开关时的收尾也在这里。
 *
 *  密聊内容不留任何记录：本文件的日志只记命令号、会话所涉 userid、结果码与帧数，不记包体。
 *  连接标识与 P2P 共用 ChatSession::p2pConnKey()（会话对象地址），查找对端会话共用 findSessionByP2PConnKey。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "ChatSession.h"

#include <chrono>
#include <vector>
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"
#include "SecretChatManager.h"
#include "FeatureSwitchManager.h"
#include "DepartmentManager.h"
#include "UserManager.h"

namespace {

//当前时间（毫秒，Unix 纪元起）
int64_t secretNowMs()
{
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

/**
 *  密聊用的企业开关、所属企业与黑名单查询：分别转给 FeatureSwitchManager、DepartmentManager 与 UserManager。
 */
class SecretChatGateImpl : public ISecretChatGate
{
public:
    bool isSecretChatEnabledForUser(int64_t userId) override
    {
        const int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(userId);
        return Singleton<FeatureSwitchManager>::Instance().isEnabled(enterpriseId, kFeatureSecretChat);
    }

    int64_t enterpriseOfUser(int64_t userId) override
    {
        return Singleton<DepartmentManager>::Instance().getUserEnterpriseId(userId);
    }

    bool isBlockedBy(int64_t ownerId, int64_t targetId) override
    {
        return Singleton<UserManager>::Instance().isInBlacklist(static_cast<int32_t>(ownerId),
                                                                static_cast<int32_t>(targetId));
    }
};

} // namespace

void ChatSession::onSecretCommand(uint16_t cmd, const std::string& body)
{
    SecretRequestContext ctx;
    ctx.m_conn = p2pConnKey();
    ctx.m_userId = m_userinfo.userid;
    ctx.m_seq = static_cast<uint32_t>(m_seq);
    ctx.m_nowMs = secretNowMs();

    SecretChatGateImpl gate;
    std::vector<SecretOutFrame> frames;
    const int code = Singleton<SecretChatManager>::Instance().handle(cmd, ctx, body, gate, frames);
    //只记结果，不记包体（里面是临时公钥与密文）
    LOGI("secret cmd handled, userid: %d, cmd: %u, seq: %d, code: %d, frames: %u", m_userinfo.userid, (unsigned)cmd,
         m_seq, code, static_cast<unsigned>(frames.size()));
    sendSecretFrames(frames);
}

void ChatSession::sendSecretFrames(const std::vector<SecretOutFrame>& frames)
{
    for (size_t i = 0; i < frames.size(); ++i)
    {
        const SecretOutFrame& f = frames[i];
        //TcpSession::send 会投递到目标连接自己的线程执行，发给请求者自己的应答也一样
        std::shared_ptr<ChatSession> target = findSessionByP2PConnKey(f.m_conn);
        if (!target)
        {
            LOGW("secret frame dropped, connection gone, cmd: %u", (unsigned)f.m_cmd);
            continue;
        }
        target->send(f.m_cmd, static_cast<int32_t>(f.m_seq), f.m_body);
        //转发的密文帧量大（图片按块发），只在调试级别记
        if (f.m_cmd == cmd_secret_message_notify || f.m_cmd == cmd_secret_message)
            LOGD("secret frame sent, cmd: %u, to userid: %d, bytes: %u", (unsigned)f.m_cmd, target->getUserId(),
                 static_cast<unsigned>(f.m_body.size()));
        else
            LOGI("secret frame sent, cmd: %u, to userid: %d", (unsigned)f.m_cmd, target->getUserId());
    }
}

void ChatSession::cleanupSecretOnOffline()
{
    std::vector<SecretOutFrame> frames;
    Singleton<SecretChatManager>::Instance().onDisconnect(p2pConnKey(), frames);
    if (frames.empty())
        return;
    LOGI("secret cleanup on offline, userid: %d, frames: %u", m_userinfo.userid, static_cast<unsigned>(frames.size()));
    sendSecretFrames(frames);
}

void ChatSession::endSecretChatsOfEnterprise(int64_t enterpriseId)
{
    SecretChatGateImpl gate;
    std::vector<SecretOutFrame> frames;
    const size_t ended = Singleton<SecretChatManager>::Instance().onFeatureDisabled(enterpriseId, gate, frames);
    LOGI("secret chat switched off, enterpriseid: %lld, sessions ended: %u, frames: %u", (long long)enterpriseId,
         static_cast<unsigned>(ended), static_cast<unsigned>(frames.size()));
    sendSecretFrames(frames);
}
