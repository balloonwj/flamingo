/**
 *  群通话处理流程的线上环境，GroupCallServerEnv.h
 *
 *  IGroupCallEnv（见 GroupCallService.h）的线上实现：群成员与群名查 GroupManager，在线判断与推送走 ChatServer，
 *  忙线判断查 CallManager 与 ScreenShareManager，企业开关查 FeatureSwitchManager（按 DepartmentManager 取用户所属企业），
 *  通话卡片经 MessageManager 落库与改写。本类不持有状态，每次处理时就地构造即可。
 *
 *  本文件依赖 chatserver 的众多单例，不进单元测试；处理流程的单元测试用假环境，见 tests/test_groupcallservice.cpp。
 *
 *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用本类的方法</b>（isOnline 与 push 都要再取该锁）。
 *
 *  典型用法：
 *      GroupCallServerEnv env;
 *      GroupCallService service(Singleton<GroupCallManager>::Instance(), env);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include "GroupCallService.h"

//群通话处理流程的线上环境
class GroupCallServerEnv : public IGroupCallEnv
{
public:
    GroupCallServerEnv();
    virtual ~GroupCallServerEnv();

    //以下为 IGroupCallEnv 的实现，含义见 GroupCallService.h

    //查 GroupManager::isGroupMember
    virtual bool isGroupMember(int64_t groupId, int64_t userId) override;

    //查 GroupManager::getGroupMembers，只取 userid
    virtual std::vector<int64_t> groupMemberIds(int64_t groupId) override;

    //查 GroupManager::getGroup 的群名；查不到返回空串
    virtual std::string groupName(int64_t groupId) override;

    //查 GroupManager::getMemberRole；不是成员返回 -1
    virtual int groupRole(int64_t groupId, int64_t userId) override;

    //ChatServer 里此人名下有在线会话即为在线
    virtual bool isOnline(int64_t userId) override;

    //在一对一通话里（CallManager::isUserBusy，含响铃中）或屏幕共享房间里（ScreenShareManager::isUserInShare）
    virtual bool isBusyOutsideGroupCall(int64_t userId) override;

    //所属企业的功能开关 groupcall；不属于任何企业时为 false
    virtual bool isFeatureEnabled(int64_t userId) override;

    //落库一条群聊的 message_type_rtc 消息并以 cmd_message_notify 推给 receivers
    virtual int64_t saveCard(int64_t groupId, int64_t fromId, const std::string& content,
                             const std::string& clientMsgId, int64_t now,
                             const std::vector<int64_t>& receivers) override;

    //MessageManager::updateMessageContent，content 按落库格式包一层外壳
    virtual bool updateCard(int64_t msgId, const std::string& content) override;

    //ChatServer::pushToUsers
    virtual void push(const std::vector<int64_t>& users, uint16_t cmd, const std::string& body) override;
};
