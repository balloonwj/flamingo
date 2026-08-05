/**
 *  远程桌面中转配对表，RelayHub.h
 *
 *  imgserver 的 relay 通道上，控制端与被控端各建一条连接、用同一 sessionid
 *  报到。RelayHub 按 sessionid 把这两条连接两两配对，供 RelaySession 在转发
 *  屏幕帧 / 控制命令时定位对端。一个 sessionid 至多两端（角色相反）。
 *
 *  只持有 TcpConnection 的弱引用——连接可能因网络出错先行销毁；转发前再 lock，
 *  失败即视作对端已走。内部以互斥量保护，可被多个 I/O 工作线程并发访问。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <memory>
#include "../net/TcpConnection.h"

using namespace net;

class RelayHub
{
public:
    RelayHub() = default;
    ~RelayHub() = default;

    RelayHub(const RelayHub& rhs) = delete;
    RelayHub& operator=(const RelayHub& rhs) = delete;

    /**
     *  登记一端到指定会话。
     *  @param sessionid 会话 id（来自 relay 令牌）
     *  @param role      本端角色（见 RelayToken::role_*）
     *  @param conn      本端连接
     *  @return 成功返回 true；该会话已满两端、或同角色重复登记返回 false
     */
    bool join(const std::string& sessionid, int role, const std::shared_ptr<TcpConnection>& conn);

    /**
     *  取某会话里 self 的对端连接。
     *  @param sessionid 会话 id
     *  @param self      本端连接裸指针（用于在两端中排除自己）
     *  @return 对端连接；对端未登记 / 已销毁返回空指针
     */
    std::shared_ptr<TcpConnection> getPeer(const std::string& sessionid, const TcpConnection* self);

    /**
     *  按连接移除一端（连接断开时调用）。移除后若该会话还剩对端，则返回对端
     *  连接供调用方连带关闭；本端是会话最后一端时返回空并清除该会话条目。
     *  @param self 已断开的连接裸指针
     *  @return 对端连接（供连带关闭）；无对端返回空指针
     */
    std::shared_ptr<TcpConnection> leaveByConn(const TcpConnection* self);

private:
    //会话中的一端
    struct Endpoint
    {
        int                          role;   //角色，见 RelayToken::role_*
        std::weak_ptr<TcpConnection> conn;   //该端连接（弱引用）
    };

    std::mutex                                   m_mutex;   //保护配对表
    std::map<std::string, std::vector<Endpoint>> m_table;   //sessionid → 端点（至多 2）
};
