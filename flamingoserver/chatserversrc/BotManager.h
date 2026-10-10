/**
 *  机器人存储管理器，BotManager.h（docs/机器人方案.md §5.5）。
 *
 *  负责五张表：t_bot（机器人）、t_bot_rule（规则）、t_bot_group（企业机器人所选的群）、t_bot_group_off（在某群关闭了的
 *  企业机器人）、t_bot_domain（外部服务的域名白名单）。企业机器人与群机器人另在 t_user 里建一行 f_usertype = user_type_bot
 *  的账号（经 UserManager::addUser），作为它在群里发消息的身份；个人机器人以主人的身份代答，不建账号。
 *
 *  全部未删除的机器人连同规则常驻内存，组织成一份只读的快照 BotSnapshot。每次写库成功后换一份新快照：读的一方拿到的
 *  std::shared_ptr<const BotSnapshot> 在使用期间不会被修改，I/O 线程（判定入口）、机器人线程、HTTP 线程都可以直接读，
 *  不需要另外加锁。机器人的数量有上限（每企业 50、每群 5、每人 3），快照里的记录以 shared_ptr 共享，换快照时只复制索引。
 *
 *  典型用法：
 *      BotManager& mgr = Singleton<BotManager>::Instance();
 *      mgr.loadFromDb();                                  //启动时
 *      std::shared_ptr<const bot::BotSnapshot> snap = mgr.snapshot();
 *      std::vector<std::shared_ptr<const bot::BotRecord> > bots;
 *      snap->botsForGroup(groupid, enterpriseId, true, bots);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "BotDefs.h"

namespace bot {

//机器人的只读快照：全部未删除的机器人与几张索引
class BotSnapshot
{
public:
    typedef std::shared_ptr<const BotRecord> BotPtr;

    BotSnapshot();

    /**
     *  由一组机器人与关闭记录、白名单建快照（纯逻辑，单元测试直接调用）。
     *  @param bots     全部未删除的机器人（已带规则与所选的群）
     *  @param groupOff 群号 → 在该群关闭了的企业机器人 id
     *  @param domains  企业 id → 外部服务域名白名单
     */
    void build(const std::vector<BotPtr>& bots, const std::map<int64_t, std::set<int64_t> >& groupOff,
               const std::map<int64_t, std::vector<std::string> >& domains);

    /**
     *  按 id 找机器人。
     *  @return 找不到（或已删除）返回空指针
     */
    BotPtr find(int64_t botId) const;

    /**
     *  按机器人账号的 userid 找机器人（企业机器人与群机器人才有账号）。
     *  @return 找不到返回空指针
     */
    BotPtr findByUserId(int64_t userId) const;

    /**
     *  按 Webhook 令牌找机器人。
     *  @return 找不到返回空指针
     */
    BotPtr findByWebhookToken(const std::string& token) const;

    /**
     *  某个群里的机器人：应用到本群的企业机器人 + 本群的群机器人。
     *  @param groupid      群号
     *  @param enterpriseId 群所属企业（群主所属企业）
     *  @param activeOnly   为 true 时只取在本群生效的（启用、且没有在本群关闭）；为 false 时连同停用的、在本群关闭了的一起取
     *  @param out          [出参] 企业机器人在前（按 id），群机器人在后（按 id）
     */
    void botsForGroup(int64_t groupid, int64_t enterpriseId, bool activeOnly, std::vector<BotPtr>& out) const;

    /**
     *  某个企业机器人是否在某群关闭了。
     */
    bool isOffInGroup(int64_t groupid, int64_t botId) const;

    /**
     *  判定入口的快速判断：这个群可能有生效的机器人（只查索引，不展开规则）。
     */
    bool groupMayHaveBots(int64_t groupid, int64_t enterpriseId) const;

    /**
     *  某人的个人机器人（含「自动回复」的预置机器人）。
     *  @param activeOnly 为 true 时只取启用的
     */
    void personalBots(int64_t ownerId, bool activeOnly, std::vector<BotPtr>& out) const;

    /**
     *  某人是否有启用的个人机器人（判定入口的快速判断）。
     */
    bool userHasPersonalBot(int64_t ownerId) const;

    /**
     *  某企业的全部企业机器人（含停用的），按 id 排序。
     */
    void enterpriseBots(int64_t enterpriseId, std::vector<BotPtr>& out) const;

    /**
     *  全部启用、且有定时规则的企业机器人与群机器人（定时调度用）。
     */
    void scheduledBots(std::vector<BotPtr>& out) const;

    /**
     *  某企业的外部服务域名白名单。
     */
    std::vector<std::string> domains(int64_t enterpriseId) const;

    /**
     *  全部机器人的数量（日志用）。
     */
    size_t botCount() const;

private:
    std::map<int64_t, BotPtr>                         m_bots;            //机器人 id → 机器人（未删除）
    std::map<int64_t, int64_t>                        m_userToBot;       //机器人账号 userid → 机器人 id
    std::map<std::string, int64_t>                    m_tokenToBot;      //Webhook 令牌 → 机器人 id
    std::map<int64_t, std::vector<int64_t> >          m_enterpriseBots;  //企业 id → 企业机器人 id
    std::map<int64_t, std::vector<int64_t> >          m_groupBots;       //群号 → 群机器人 id
    std::map<int64_t, std::vector<int64_t> >          m_personalBots;    //主人 userid → 个人机器人 id
    std::map<int64_t, std::set<int64_t> >             m_groupOff;        //群号 → 在该群关闭了的企业机器人 id
    std::map<int64_t, std::vector<std::string> >      m_domains;         //企业 id → 域名白名单
};

//写操作的结果
enum bot_store_result
{
    //成功
    bot_store_ok        = 0,
    //机器人不存在或已删除
    bot_store_not_exist = 1,
    //写库失败
    bot_store_db_fail   = 2,
};

} // namespace bot

/**
 *  机器人存储管理器；全局单例，经 Singleton<BotManager> 访问。启动时调 loadFromDb()。
 */
class BotManager final
{
public:
    BotManager();
    ~BotManager();

    BotManager(const BotManager& rhs) = delete;
    BotManager& operator=(const BotManager& rhs) = delete;

    /**
     *  从库里载入全部未删除的机器人、规则、所选的群、关闭记录与白名单，建第一份快照。
     *  @return 数据库不可用时返回 false（快照为空，机器人不响应）
     */
    bool loadFromDb();

    /**
     *  当前快照。返回的快照不会再被修改，可以在任意线程长时间持有。
     */
    std::shared_ptr<const bot::BotSnapshot> snapshot() const;

    /**
     *  新建一个机器人：企业机器人与群机器人先建账号（t_user），再在事务里写 t_bot、规则与所选的群；成功后换快照。
     *  @param rec   [入出参] 机器人；调用方已校验并填好归属、企业、创建人、令牌与密钥。成功后填上 m_botId、m_userId、
     *               规则的 m_id 与各时间
     *  @param nowMs 当前时间（Unix 毫秒）
     *  @return 取值见 bot::bot_store_result
     */
    int createBot(bot::BotRecord& rec, int64_t nowMs);

    /**
     *  修改一个机器人的可编辑字段（名称、简介、头像、状态、应用范围、所选的群、是否允许群主关闭）并整份替换规则；
     *  成功后换快照。账号昵称随名称一起改。
     *  @param rec   机器人；m_botId 必须是已存在的机器人
     *  @param nowMs 当前时间
     *  @return 取值见 bot::bot_store_result
     */
    int updateBot(const bot::BotRecord& rec, int64_t nowMs);

    /**
     *  停用或启用。
     *  @param status bot_status_active 或 bot_status_disabled
     */
    int setStatus(int64_t botId, int status, int64_t nowMs);

    /**
     *  软删除：t_bot.f_status 置为已删除，删掉规则与所选的群；账号停用但保留。
     */
    int deleteBot(int64_t botId, int64_t nowMs);

    /**
     *  在某群关闭或打开一个企业机器人。
     *  @param off 为 true 时关闭
     */
    int setGroupOff(int64_t groupid, int64_t botId, bool off, int64_t operatorId, int64_t nowMs);

    /**
     *  重新生成 Webhook 令牌与签名密钥。
     *  @param outToken  [出参] 新令牌
     *  @param outSecret [出参] 新密钥
     */
    int resetSecrets(int64_t botId, std::string& outToken, std::string& outSecret);

    /**
     *  整份替换某企业的外部服务域名白名单（调用方已校验每一项）。
     */
    bool setDomains(int64_t enterpriseId, const std::vector<std::string>& domains);

    /**
     *  记下机器人最近一次发出消息的时间：内存立即更新（不换快照），库最多每分钟写一次。
     */
    void touchFire(int64_t botId, int64_t nowMs);

    /**
     *  机器人最近一次发出消息的时间（内存里的值优先，没有时取快照里从库载入的值）。
     *  @return Unix 毫秒；0 表示还没有发过
     */
    int64_t lastFireTime(int64_t botId) const;

    /**
     *  群解散：软删除该群的群机器人，删掉该群的所选记录与关闭记录。
     */
    void onGroupDismissed(int64_t groupid, int64_t nowMs);

    /**
     *  员工被删除：软删除他的个人机器人。
     */
    void onUserDeleted(int64_t userId, int64_t nowMs);

    /**
     *  企业被删除：删掉该企业的全部机器人数据（账号由企业删除流程一并删除）。
     */
    void onEnterpriseDeleted(int64_t enterpriseId);

    /**
     *  设置 Webhook 的对外根地址（chatserver.conf 的 botwebhookbaseurl）。只在启动时、机器人线程与网络线程开始工作之前
     *  调用一次，之后只读，因此不加锁。
     *    baseUrl：如 "https://im.example.com:8443"；空串表示没有配置。
     */
    void setWebhookBaseUrl(const std::string& baseUrl);

    /**
     *  Webhook 的对外完整地址（bot::BuildWebhookUrl 的结果），随 2802 详情与管理接口下发。
     *  @return 没有配置根地址时返回空串
     */
    const std::string& webhookUrl() const;

    /**
     *  直接替换快照（单元测试用，不碰数据库）。
     */
    void setSnapshotForTest(const std::shared_ptr<const bot::BotSnapshot>& snap);

    /**
     *  按 id 从库里读一个机器人（含已删除的，含规则），不经快照。
     *  @return 找到返回 true
     */
    bool loadBot(int64_t botId, bot::BotRecord& out);

private:
    //从库里重建快照并替换；失败时保留旧快照。调用方持有 m_writeMutex
    bool reloadLocked();

private:
    mutable std::mutex                       m_snapMutex;    //保护 m_snapshot 指针本身的读写
    std::shared_ptr<const bot::BotSnapshot>  m_snapshot;     //当前快照；从不为空
    std::mutex                               m_writeMutex;   //串行化全部写操作（写库与换快照）
    mutable std::mutex                       m_fireMutex;    //保护 m_lastFire 与 m_fireWritten
    std::map<int64_t, int64_t>               m_lastFire;     //机器人 id → 最近一次发出消息的时间（Unix 毫秒），进程内的最新值
    std::map<int64_t, int64_t>               m_fireWritten;  //机器人 id → 上次把最近发消息时间写进库的时间（Unix 毫秒）
    std::string                              m_webhookUrl;   //Webhook 的对外完整地址；启动时设置一次，之后只读；空串表示没有配置
};
