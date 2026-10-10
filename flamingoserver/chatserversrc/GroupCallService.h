/**
 *  群通话信令的服务端处理流程，GroupCallService.h
 *
 *  把 GroupCallManager（房间状态）、GroupCallProtocol（编解码）与群成员查询、在线判断、忙线判断、企业功能开关、
 *  通话卡片落库、在线推送串起来，完成 cmd_groupcall_* 1740~1751 的完整处理：校验、改房间、回应答、推 1741 / 1745 /
 *  1746 / 1750、写与改写通话卡片。
 *
 *  与屏幕共享的处理流程不同，本类不直接访问各个单例，而是经 IGroupCallEnv 取得外部能力：线上由 GroupCallServerEnv
 *  （GroupCallServerEnv.h）用 GroupManager、ChatServer、CallManager 等实现；单元测试传入假实现，连「谁收到哪条推送」
 *  都能脱离网络与数据库验证，见 tests/test_groupcallservice.cpp（Suite GCS）。
 *
 *  回给请求者本连接的帧（应答以及紧随其后的 1746 中转参数）不在本类里发送，而是按顺序放进 outReplies，由 ChatSession
 *  用本连接与本次请求的 seq 发出 —— 这样应答一定先于中转参数到达，且只发给发请求的那个设备。发给其他人的推送经
 *  IGroupCallEnv::push 发出。
 *
 *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用本类的任何方法</b>（线上环境的推送要遍历在线会话表，
 *  会再取一次该锁，而它是非递归锁）。
 *
 *  典型用法（ChatSession 的分发里）：
 *      GroupCallServerEnv env;
 *      GroupCallService service(Singleton<GroupCallManager>::Instance(), env);
 *      std::vector<GroupCallReply> replies;
 *      service.handleRequest(cmd, m_userinfo.userid, m_userinfo.nickname, body, nowMs, replies);
 *      for (size_t i = 0; i < replies.size(); ++i)
 *          send(replies[i].m_cmd, i == 0 ? m_seq : 0, replies[i].m_body);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "GroupCallManager.h"

//要发回请求者本连接的一帧
struct GroupCallReply
{
    uint16_t    m_cmd;    //命令号：第一帧为请求本身的命令号（应答），之后可能是 cmd_groupcall_relay_info
    std::string m_body;   //包体（UTF-8 JSON）

    GroupCallReply(uint16_t cmd, const std::string& body)
        : m_cmd(cmd)
        , m_body(body)
    {
    }
};

/**
 *  群通话处理流程依赖的外部能力。线上实现见 GroupCallServerEnv；单元测试传入假实现。
 *  全部方法都可能在任意 EventLoop 工作线程或主循环上调用，实现须线程安全。
 */
class IGroupCallEnv
{
public:
    virtual ~IGroupCallEnv()
    {
    }

    /**
     *  某人是否是某群的成员。
     *  @param groupId 群 id
     *  @param userId  userid
     */
    virtual bool isGroupMember(int64_t groupId, int64_t userId) = 0;

    /**
     *  群的全体成员 userid（含群主与管理员）；群不存在时返回空。
     */
    virtual std::vector<int64_t> groupMemberIds(int64_t groupId) = 0;

    /**
     *  群名（UTF-8），用于邀请推送；取不到时返回空串。
     */
    virtual std::string groupName(int64_t groupId) = 0;

    /**
     *  某人在群里的角色，取值见 enum group_role（0 成员、1 管理员、2 群主）；不是群成员返回 -1。第 6 期管理操作判断权限用。
     */
    virtual int groupRole(int64_t groupId, int64_t userId) = 0;

    /**
     *  某人当前是否有在线的登录连接。不在线的被邀请人不振铃，列入应答的 offline 名单。
     */
    virtual bool isOnline(int64_t userId) = 0;

    /**
     *  某人是否正在群通话以外的实时会话里：一对一通话（含响铃中）或屏幕共享房间。群通话房间本身的占用由
     *  GroupCallManager 判断，不在这里。
     */
    virtual bool isBusyOutsideGroupCall(int64_t userId) = 0;

    /**
     *  某人所在企业是否开着群通话功能（功能开关 groupcall）。不属于任何企业时返回 false。
     */
    virtual bool isFeatureEnabled(int64_t userId) = 0;

    /**
     *  往群里写一张通话卡片（message_type_rtc，记在 fromId 名下）并推给 receivers。
     *  @param groupId     群 id
     *  @param fromId      发起人 userid
     *  @param content     卡片 content（JSON 文本）
     *  @param clientMsgId 去重键
     *  @param now         当前时间，Unix 毫秒（作消息时间）
     *  @param receivers   推送对象（全体群成员，含发起人）
     *  @return 落库得到的消息 id；失败返回 0（卡片只是附带的通知，失败不影响通话本身）
     */
    virtual int64_t saveCard(int64_t groupId, int64_t fromId, const std::string& content, const std::string& clientMsgId,
                             int64_t now, const std::vector<int64_t>& receivers) = 0;

    /**
     *  改写一张已落库的卡片的 content（通话结束时）。
     *  @return 改写成功返回 true
     */
    virtual bool updateCard(int64_t msgId, const std::string& content) = 0;

    /**
     *  把一帧推给这些用户的全部在线设备。不在线的人直接跳过。
     *  @param users 接收者（可以有重复，实现方不必去重）
     *  @param cmd   命令号
     *  @param body  包体（UTF-8 JSON）
     */
    virtual void push(const std::vector<int64_t>& users, uint16_t cmd, const std::string& body) = 0;
};

//群通话的服务端处理流程；每次处理时就地构造，不持有状态（状态全在 GroupCallManager 里）
class GroupCallService
{
public:
    /**
     *  @param mgr 房间管理器（借用，不持有；线上为 Singleton<GroupCallManager>）
     *  @param env 外部能力（借用，不持有）
     */
    GroupCallService(GroupCallManager& mgr, IGroupCallEnv& env);

    /**
     *  处理一条群通话请求：发起（1740）、加入（1742）、拒绝（1743）、离开（1744）、追加邀请（1747）、成员媒体（1748）、
     *  查询（1749）、管理（1751，第 6 期）。
     *  @param cmd        请求的命令号；不是上述之一时只回一条 error_code_bad_request
     *  @param userId     请求者 userid（已登录）
     *  @param nickname   请求者昵称（UTF-8）：发起时写进邀请推送与通话卡片，追加邀请时写进邀请推送
     *  @param body       请求包体（UTF-8 JSON）
     *  @param now        当前时间，Unix 毫秒
     *  @param outReplies [出参] 要按顺序发回请求者本连接的帧；第一帧总是应答
     */
    void handleRequest(uint16_t cmd, int64_t userId, const std::string& nickname, const std::string& body, int64_t now,
                       std::vector<GroupCallReply>& outReplies);

    /**
     *  用户已无任何登录态连接时调用：振铃中的人从房间移除（事件 4），已加入的人进入掉线宽限期（事件 7）。
     */
    void onUserOffline(int64_t userId, int64_t now);

    /**
     *  用户登录成功后调用：宽限期内回来的已加入成员通知房间（事件 8），并把新的中转参数（1746）追加到 outReplies，
     *  由调用方发给这条新连接。
     */
    void onUserLogin(int64_t userId, int64_t now, std::vector<GroupCallReply>& outReplies);

    /**
     *  主循环每秒调用：移除振铃超时的人（事件 4）；掉线超过宽限期的已加入成员按离开处理（事件 2），房间因此结束时
     *  通知群成员并改写卡片。
     */
    void reapTick(int64_t now);

    /**
     *  此人当前是否在某个群通话房间里（已加入或振铃中），查的是全局单例。一对一通话、屏幕共享的互斥判断用。
     */
    static bool isUserInGroupCall(int64_t userId);

private:
    void handleStart(int64_t userId, const std::string& nickname, const std::string& body, int64_t now,
                     std::vector<GroupCallReply>& outReplies);
    void handleJoin(int64_t userId, const std::string& body, int64_t now, std::vector<GroupCallReply>& outReplies);
    void handleDecline(int64_t userId, const std::string& body, std::vector<GroupCallReply>& outReplies);
    void handleLeave(int64_t userId, const std::string& body, int64_t now, std::vector<GroupCallReply>& outReplies);
    void handleInviteMore(int64_t userId, const std::string& nickname, const std::string& body, int64_t now,
                          std::vector<GroupCallReply>& outReplies);
    void handleMemberMedia(int64_t userId, const std::string& body, std::vector<GroupCallReply>& outReplies);
    void handleQuery(int64_t userId, const std::string& body, std::vector<GroupCallReply>& outReplies);
    void handleManage(int64_t userId, const std::string& body, int64_t now, std::vector<GroupCallReply>& outReplies);

    /**
     *  按发起 / 追加邀请的规则筛选被邀请人：去重、去掉 selfId、去掉非群成员；不在线的进 outOffline，正在群通话以外的
     *  实时会话里的进 outBusy，其余进 outRing。群通话房间本身的占用由 GroupCallManager 再挑一遍。
     */
    void classifyInvitees(int64_t selfId, const std::vector<int64_t>& raw, const std::vector<int64_t>& groupMembers,
                          std::vector<int64_t>& outRing, std::vector<int64_t>& outBusy,
                          std::vector<int64_t>& outOffline);

    /**
     *  某人在这场群通话里是否有管理权：发起人、群主、管理员。有管理权的人可以静音、移出别人，开启 / 解除全员静音，
     *  且不受全员静音约束（2026-10-05）。群角色经 m_env 查询。
     *  @param room   房间快照（取发起人）
     *  @param userId 要判断的人
     *  @return 有管理权返回 true
     */
    bool hasManageRight(const GroupCallRoom& room, int64_t userId);

    //给已加入的人签发中转令牌，把 1746 追加到 outReplies；此人没有加入时不追加
    bool appendRelayInfo(const std::string& roomId, int64_t userId, int64_t now, std::vector<GroupCallReply>& outReplies);

    //向房间里的全部人（加上 extra 里的人）推一帧 1745
    void pushState(int event, int64_t actor, const GroupCallRoom& room, const std::vector<int64_t>& extra,
                   int64_t target = 0);

    //向群成员与房间里的人推一帧进行中的 1750（人数变了）
    void pushActiveStatus(const GroupCallRoom& room);

    //给 room 里振铃中、且在 newlyInvited 里的人逐个推 1741
    void pushInvites(const GroupCallRoom& room, const std::vector<int64_t>& newlyInvited, int64_t inviterId,
                     const std::string& inviterName, int64_t now);

    /**
     *  房间已结束后的收尾：改写通话卡片，向群成员、结束前仍在房间里的人与 extra 里的人推 active 为 0 的 1750。
     *  @param room  结束之前的房间快照
     *  @param extra 另外要通知的人（刚离开的人）
     */
    void finishRoom(const GroupCallRoom& room, int64_t now, const std::vector<int64_t>& extra);

    GroupCallManager& m_mgr;   //房间管理器（借用）
    IGroupCallEnv&    m_env;   //外部能力（借用）
};
