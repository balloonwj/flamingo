/**
 *  云端录制处理流程的线上环境，CloudRecordServerEnv.h
 *
 *  另有 CloudRecordLinkBridge：把 CloudRecordLink（chatserver 到 imgserver 的控制连接）接到 CloudRecordService，既是控制消息的
 *  出口，也把连接线程上的事件与 imgserver 的消息投回主循环再交给服务处理。
 *
 *  ICloudRecordEnv（见 CloudRecordService.h）的线上实现：所属企业查 DepartmentManager，开关与数值设置查 FeatureSwitchManager，
 *  群成员查 GroupManager，推送走 ChatServer，房间的录制状态交给 ScreenShareService，云端录像卡片经 MessageManager 落库后推送，
 *  imgserver 地址取自 FileStoreManager。本类不持有状态。
 *
 *  本文件依赖 chatserver 的众多单例，不进单元测试；处理流程的单元测试用假环境，见 tests/test_cloudrecordservice.cpp。
 *
 *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用本类的方法</b>（推送要再取该锁）。
 *
 *  典型用法（main.cpp）：
 *      static CloudRecordServerEnv env;
 *      static CloudRecordService service(store, env, Singleton<CloudRecordLink>::Instance(), filetokenkey);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include "../net/EventLoop.h"
#include "CloudRecordLink.h"
#include "CloudRecordService.h"

//云端录制处理流程的线上环境
class CloudRecordServerEnv : public ICloudRecordEnv
{
public:
    CloudRecordServerEnv();
    virtual ~CloudRecordServerEnv();

    //以下为 ICloudRecordEnv 的实现，含义见 CloudRecordService.h

    //DepartmentManager::getUserEnterpriseId
    virtual int64_t enterpriseOf(int64_t userId) override;
    //FeatureSwitchManager::isEnabled(企业, cloudrecord)
    virtual bool isEnabled(int64_t enterpriseId) override;
    //FeatureSwitchManager::getNumber(企业, cloudrecord.quotagb)
    virtual int64_t quotaGb(int64_t enterpriseId) override;
    //FeatureSwitchManager::getNumber(企业, cloudrecord.retaindays)
    virtual int64_t retainDays(int64_t enterpriseId) override;
    //GroupManager::isGroupMember
    virtual bool isGroupMember(int64_t groupId, int64_t userId) override;
    //ChatServer::pushToUsers
    virtual void push(int64_t userId, uint16_t cmd, const std::string& body) override;
    //ScreenShareService::onCloudRecordEnded
    virtual void onRoomRecordEnded(const std::string& roomId, int64_t recordId, int code) override;
    //ScreenShareService::isCloudRecording
    virtual bool isRoomRecording(const std::string& roomId, int64_t recordId) override;
    //落库一条 message_type_cloudrecord 消息，推给会话里的人（单聊为双方，群聊为全体群成员）
    virtual int64_t sendCard(int64_t fromId, int ctype, int64_t cid, const std::string& content, int64_t nowMs) override;
    //FileStoreManager::getServerForScene(file_scene_cloud_record)
    virtual void imgServerAddr(std::string& host, int& port) override;
};

//把控制连接接到云端录制服务：发送直接交给连接（它自己保证线程安全），回报投回主循环处理
class CloudRecordLinkBridge : public ICloudRecordCtlSender, public ICloudRecordLinkListener
{
public:
    /**
     *  @param link 控制连接（不拥有，生命周期须长于本对象）
     *  @param loop 主循环（不拥有）；回报都投到这里处理
     */
    CloudRecordLinkBridge(CloudRecordLink& link, net::EventLoop* loop);
    virtual ~CloudRecordLinkBridge();

    //登记要接收回报的服务（不拥有）；没有登记时回报只记日志
    void setService(CloudRecordService* service);

    //以下为 ICloudRecordCtlSender 的实现：直接交给 CloudRecordLink
    virtual bool isUp() const override;
    virtual bool send(const std::string& body) override;

    //以下为 ICloudRecordLinkListener 的实现（在连接线程上调用）：投回主循环
    virtual void onCloudRecordLinkUp() override;
    virtual void onCloudRecordLinkDown() override;
    virtual void onCloudRecordLinkMessage(const std::string& body) override;

private:
    //在主循环上执行
    void handleUp();
    void handleMessage(const std::string& body);

    CloudRecordLink&    m_link;      //控制连接（不拥有）
    net::EventLoop*     m_loop;      //主循环（不拥有）
    CloudRecordService* m_service;   //云端录制服务（不拥有）；只在启动时设置
};
