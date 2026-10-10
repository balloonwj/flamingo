/**
 *  屏幕共享信令的服务端处理流程，ScreenShareService.h
 *
 *  把 ScreenShareManager（房间状态）、ScreenShareRules（资格与通知对象）、ScreenShareProtocol（编解码）与
 *  群成员、消息落库、在线推送串起来，完成 cmd_screenshare_* 1720~1731 的完整处理：校验、改房间、回应答、
 *  推 1721 / 1725 / 1726 / 1729、写共享卡片与结束提示。全部为静态方法，不持有连接，因此连接断开、退出登录、
 *  登录成功与主循环的宽限期定时清理都能直接调用。
 *
 *  回给请求者本连接的帧（应答以及紧随其后的 1726 中转参数）不在本类里发送，而是按顺序放进 outReplies，
 *  由 ChatSession 用本连接与本次请求的 seq 发出 —— 这样应答一定先于中转参数到达，且只发给发请求的那个设备。
 *  发给其他人的推送则由本类经 ChatServer::pushToUsers 直接发出。
 *
 *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用本类的任何方法</b>：推送要遍历在线会话表，
 *  会再取一次该锁，而它是非递归锁。
 *
 *  典型用法（ChatSession 的分发里）：
 *      std::vector<ScreenShareReply> replies;
 *      ScreenShareService::handleRequest(cmd, m_userinfo.userid, m_userinfo.nickname, body, replies);
 *      for (size_t i = 0; i < replies.size(); ++i)
 *          send(replies[i].m_cmd, i == 0 ? m_seq : 0, replies[i].m_body);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

//要发回请求者本连接的一帧
struct ScreenShareReply
{
    uint16_t    m_cmd;    //命令号：第一帧为请求本身的命令号（应答），之后可能是 cmd_screenshare_relay_info
    std::string m_body;   //包体（UTF-8 JSON）

    ScreenShareReply(uint16_t cmd, const std::string& body)
        : m_cmd(cmd)
        , m_body(body)
    {
    }
};

//屏幕共享的服务端处理流程；全部为静态方法，不可实例化
class IScreenShareSensitiveChecker;

/**
 *  为单聊共享的观看者与发起人建 P2P 会话（P2P 方案第 6 期）。由 ChatSession 实现（它持有 P2P 信令服务与本连接），
 *  建成时产生的 2302 由实现方在发完应答与 1726 之后再发出。
 */
class IScreenShareP2PLinker
{
public:
    virtual ~IScreenShareP2PLinker()
    {
    }

    /**
     *  为一名观看者与发起人建 P2P 会话。
     *  @param featureSessionId 功能会话号：房间号 + '#' + 观看者座位号
     *  @param hostId           发起人 userid（发起方）
     *  @param viewerId         观看者 userid
     *  @param p2pSessionId     [出参] 建成时的 P2P 会话号
     *  @return 建成返回 true；对方不支持、企业关闭 P2P、会话数到上限等返回 false
     */
    virtual bool createViewerLink(const std::string& featureSessionId, int64_t hostId, int64_t viewerId,
                                  std::string& p2pSessionId) = 0;
};

class ScreenShareService
{
public:
    /**
     *  处理一条屏幕共享请求：发起（1720）、加入（1722）、离开（1723）、结束（1724）、查询（1727）、
     *  成员媒体（1728）、设置权限（1730）、录制状态（1731）、移出观看者（1732，第 14 期）、修改标题与描述（1733，第 15 期）、
     *  云端录制的开始 / 暂停 / 继续 / 停止（1754，第 26 期）等。
     *  @param cmd        请求的命令号；不是上述十个之一时只回一条 error_code_bad_request
     *  @param userId     请求者 userid（已登录）
     *  @param nickname   请求者昵称（UTF-8），发起共享时写进开始推送与共享卡片
     *  @param body       请求包体（UTF-8 JSON）
     *  @param outReplies [出参] 要按顺序发回请求者本连接的帧；第一帧总是应答
     *  @param checker    共享标题与描述的敏感词检查（第 15 期，发起与修改时使用）；为 NULL 时不检查（不拥有）
     *  @param p2pLinker  单聊共享有人加入时建 P2P 会话（P2P 方案第 6 期）；为 NULL 时不建（不拥有）
     */
    static void handleRequest(uint16_t cmd, int64_t userId, const std::string& nickname, const std::string& body,
                              std::vector<ScreenShareReply>& outReplies, IScreenShareSensitiveChecker* checker = NULL,
                              IScreenShareP2PLinker* p2pLinker = NULL);

    /**
     *  此人当前是否在某个屏幕共享房间里（发起人或观看者）。一对一通话的互斥判断用。
     */
    static bool isUserInShare(int64_t userId);

    /**
     *  用户已无任何在线连接（最后一条连接断开，或最后一个登录态的连接退出登录）时调用。
     *  观看者按「离开」处理并通知其余成员；发起人则房间保留、通知成员「发起人离线」，等宽限期。
     *  @param userId 下线的用户
     */
    static void onUserOffline(int64_t userId);

    /**
     *  用户登录成功后调用。若此人是宽限期内回来的发起人，通知房间成员「发起人已回来」，并把新的中转参数
     *  （1726）追加到 outReplies，由调用方发给这条新连接。
     *  @param userId     登录的用户
     *  @param outReplies [出参] 要发给这条新连接的帧；不需要时不追加
     */
    static void onUserLogin(int64_t userId, std::vector<ScreenShareReply>& outReplies);

    /**
     *  主循环定时调用：解散发起人离线超过宽限期的房间，通知会话成员共享已结束（原因 2）并写结束提示。
     */
    static void reapExpiredHosts();

    /**
     *  房间里的云端录制被动结束了（第 26 期，由 CloudRecordService 在 imgserver 开始失败或自己停止时调用）：清掉房间的录制状态，
     *  经 1725 事件 15 广播（actor 为 0，带 cloudrecordcode）。房间已不存在或已不在录这条录像时什么也不做。
     *  @param code 原因，如 error_code_cloudrecord_unavailable
     */
    static void onCloudRecordEnded(const std::string& roomId, int64_t recordId, int code);

    //房间是否还在录这条录像（第 26 期，CloudRecordService 在控制连接恢复时据此决定要不要补发 stop）
    static bool isCloudRecording(const std::string& roomId, int64_t recordId);

private:
    ScreenShareService();
};
