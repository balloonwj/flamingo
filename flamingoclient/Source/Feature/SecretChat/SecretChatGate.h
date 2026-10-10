/**
 *  单聊密聊入口的显隐判定与主程序代发 2600 段命令的放行规则（纯函数，docs/密聊方案.md 4.9、6.2），
 *  以及本机密聊会话的登记簿 SecretSessionBook（会话号属于哪个对方、别人发来的待应答邀请）。
 *
 *  聊天窗头部的「密聊」按钮只在四个条件同时满足时显示：密聊 DLL 已加载并初始化、本人所属企业的开关打开（登录应答
 *  features.secretchat 或 1013）、服务端在 2601 应答里确认开启、对方不是本人。代发命令只放行 2600~2699 段、包体非空、
 *  主连接已连上。写成纯函数便于单元测试（Tests/SecretChatHostTests.cpp，标签 [secretchat]）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <map>

namespace balloonsecret {

/**
 *  聊天窗是否显示「密聊」按钮。
 *  @param moduleReady    DLL 已加载并初始化
 *  @param featureEnabled 本人所属企业的密聊开关打开
 *  @param serverEnabled  服务端在 2601 应答里确认开启
 *  @param peerIsSelf     会话对方是不是本人（「我的电脑」一类的自聊会话不显示）
 *  @return 显示返回 true
 */
inline bool ShowSecretEntry(bool moduleReady, bool featureEnabled, bool serverEnabled, bool peerIsSelf)
{
    return moduleReady && featureEnabled && serverEnabled && !peerIsSelf;
}

/**
 *  主程序是否替 DLL 发出一条命令。
 *  @param cmd       命令号
 *  @param bodyLen   包体字节数
 *  @param connected 主连接是否已连上
 *  @return 放行返回 true
 */
inline bool CanSendSecretCommand(uint32_t cmd, uint32_t bodyLen, bool connected)
{
    //号段与 FlamingoSecretChat/SecretChatApi.h 的 kSecretCmdFirst / kSecretCmdLast 一致
    const uint32_t kFirst = 2600;
    const uint32_t kLast = 2699;
    return connected && bodyLen > 0 && cmd >= kFirst && cmd <= kLast;
}

/**
 *  别人发给本人、还在倒计时内的一个密聊邀请。主面板按对方 userid 记下，单聊窗在倒计时内打开时据此补出邀请卡片
 *  （docs/密聊方案.md 4.8，做法同文件夹邀请）。
 */
struct PendingSecretInvite
{
    uint64_t m_sessionId;     //会话号
    uint64_t m_deadlineTick;  //邀请到期的时刻（GetTickCount64 口径，毫秒）
    int      m_peerProtect;   //发起方报到的防截图级别（secretchat::CaptureProtectLevel）

    PendingSecretInvite()
        : m_sessionId(0)
        , m_deadlineTick(0)
        , m_peerProtect(0)
    {
    }
};

/**
 *  邀请还剩多少毫秒。
 *  @param deadlineTick 到期时刻（GetTickCount64 口径）
 *  @param nowTick      当前时刻（同一口径）
 *  @return 剩余毫秒数；已到期返回 0
 */
inline uint64_t SecretInviteRemainMs(uint64_t deadlineTick, uint64_t nowTick)
{
    return deadlineTick > nowTick ? deadlineTick - nowTick : 0;
}

/**
 *  把剩余毫秒数换算成卡片上显示的剩余秒数：不足一秒的部分向上取整，倒计时不会提前显示 0。
 *  @param remainMs 剩余毫秒数
 *  @return 剩余秒数
 */
inline uint32_t SecretInviteRemainSeconds(uint64_t remainMs)
{
    const uint64_t kMsPerSecond = 1000;
    return static_cast<uint32_t>((remainMs + kMsPerSecond - 1) / kMsPerSecond);
}

/**
 *  SecretSessionBook —— 本机参与的密聊会话的登记簿（纯逻辑，CSecretChatHost 持有一份，单测直接构造）。
 *
 *  记两件事：
 *    - 会话号属于哪个对方：发起成功（2602 应答）、收到邀请（2603）时登记；会话走到终态（未接受、已撤回、已结束、
 *      已在其他设备处理、本机出错）或本人忽略、撤回、退出时删除。DLL 有几个回调只带会话号（图片进度、发送结果），
 *      宿主据此补上对方 userid 再交给主面板；
 *    - 别人发给本人、还在倒计时内的邀请：收到邀请时登记，本人同意或忽略、邀请走到终态时删除。单聊窗在倒计时内
 *      打开时据此补出邀请卡片（docs/密聊方案.md 4.8，做法同文件夹邀请）。
 *  只在界面线程上使用，不加锁。
 */
class SecretSessionBook
{
public:
    /**
     *  本人发起的邀请被服务端受理。
     *  @param sessionId 会话号（大于 0）
     *  @param peerId    对方 userid
     */
    void OnInviteSent(uint64_t sessionId, int64_t peerId)
    {
        if (sessionId != 0 && peerId > 0)
            m_sessionPeers[sessionId] = peerId;
    }

    /**
     *  收到别人的邀请。同一个人再发来新邀请时覆盖旧的那条待应答记录。
     *  @param sessionId   会话号（大于 0）
     *  @param fromId      发起方 userid
     *  @param deadlineTick 邀请到期时刻（GetTickCount64 口径，毫秒）
     *  @param peerProtect 发起方的防截图级别
     */
    void OnInviteArrived(uint64_t sessionId, int64_t fromId, uint64_t deadlineTick, int peerProtect)
    {
        if (sessionId == 0 || fromId <= 0)
            return;
        m_sessionPeers[sessionId] = fromId;
        PendingSecretInvite invite;
        invite.m_sessionId = sessionId;
        invite.m_deadlineTick = deadlineTick;
        invite.m_peerProtect = peerProtect;
        m_incoming[fromId] = invite;
    }

    /**
     *  会话不再进行（走到终态，或本人忽略、撤回、退出）：删除它的对方登记与待应答记录。
     *  @param sessionId 会话号
     */
    void Forget(uint64_t sessionId)
    {
        std::map<uint64_t, int64_t>::iterator it = m_sessionPeers.find(sessionId);
        if (it != m_sessionPeers.end())
        {
            ForgetIncoming(it->second, sessionId);
            m_sessionPeers.erase(it);
        }
    }

    /**
     *  本人已应答（同意或忽略）一条别人发来的邀请：只删待应答记录，会话号与对方的登记保留（同意后会话继续进行）。
     *  @param sessionId 会话号
     */
    void OnResponded(uint64_t sessionId)
    {
        std::map<uint64_t, int64_t>::iterator it = m_sessionPeers.find(sessionId);
        if (it != m_sessionPeers.end())
            ForgetIncoming(it->second, sessionId);
    }

    /**
     *  会话号属于哪个对方。
     *  @return 对方 userid；不认识的会话号返回 0
     */
    int64_t PeerOf(uint64_t sessionId) const
    {
        std::map<uint64_t, int64_t>::const_iterator it = m_sessionPeers.find(sessionId);
        return (it != m_sessionPeers.end()) ? it->second : 0;
    }

    /**
     *  取别人发给本人、还未到期的待应答邀请；已到期的顺带删除（服务端的超时推送随后会到，这里只是不再补出卡片）。
     *  @param fromId  发起方 userid
     *  @param nowTick 当前时刻（GetTickCount64 口径）
     *  @param out     [出参] 邀请
     *  @return 有且未到期返回 true
     */
    bool GetIncoming(int64_t fromId, uint64_t nowTick, PendingSecretInvite& out)
    {
        std::map<int64_t, PendingSecretInvite>::iterator it = m_incoming.find(fromId);
        if (it == m_incoming.end())
            return false;
        if (SecretInviteRemainMs(it->second.m_deadlineTick, nowTick) == 0)
        {
            m_incoming.erase(it);
            return false;
        }
        out = it->second;
        return true;
    }

    /**
     *  登记的会话个数（单元测试与日志用）。
     */
    size_t SessionCount() const
    {
        return m_sessionPeers.size();
    }

    /**
     *  退出登录：全部清空。
     */
    void Clear()
    {
        m_sessionPeers.clear();
        m_incoming.clear();
    }

private:
    //删掉 peerId 名下会话号为 sessionId 的待应答记录；同一个人后来又发了新邀请时，旧会话号对不上，不删新的那条
    void ForgetIncoming(int64_t peerId, uint64_t sessionId)
    {
        std::map<int64_t, PendingSecretInvite>::iterator it = m_incoming.find(peerId);
        if (it != m_incoming.end() && it->second.m_sessionId == sessionId)
            m_incoming.erase(it);
    }

private:
    std::map<uint64_t, int64_t>             m_sessionPeers;   //会话号 → 对方 userid（本机参与的待应答与进行中的会话）
    std::map<int64_t, PendingSecretInvite>  m_incoming;       //发起方 userid → 别人发给本人、还在倒计时内的邀请
};

} // namespace balloonsecret
