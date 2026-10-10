/**
 *  机器人引擎，BotEngine.h（docs/机器人方案.md §5.2、§5.3）。
 *
 *  发消息的整条链路同步跑在发送者连接所在的 I/O 线程上，所以判定入口（onGroupMessage / onSingleMessage /
 *  onMembersJoined）只查一次内存快照判断「可能有机器人」，有才把消息副本投递给专用的机器人线程（bot-engine），
 *  I/O 线程上不做数据库查询。机器人线程按到达顺序判定并发出回复；定时规则也在这个线程上每 30 秒检查一次。
 *  呼叫外部服务是阻塞的 HTTP 请求，放在单独的几条工作线程上执行，不占用机器人线程。
 *
 *  引擎经 IBotEnv 发消息、查群与人；生产环境用 BotServerEnv，单元测试注入假实现，并直接调用 process* 系列
 *  同步函数，不起线程。
 *
 *  典型用法：
 *      BotEngine& engine = Singleton<BotEngine>::Instance();
 *      engine.start(&env, 2);                          //main.cpp 启动时
 *      engine.onGroupMessage(ev);                      //ChatSession 群消息推送完成之后
 *      engine.stop();                                  //退出时
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "BotDefs.h"
#include "BotRules.h"

namespace net {
class EventLoop;
class EventLoopThread;
}

/**
 *  机器人的运行环境：引擎经它发消息、查群与人、调用外部服务。全部方法都可能在机器人线程、外部服务工作线程或
 *  HTTP 线程上调用，实现须线程安全。
 */
class IBotEnv
{
public:
    virtual ~IBotEnv()
    {
    }

    //当前时间（Unix 毫秒）
    virtual int64_t nowMs() = 0;

    /**
     *  群的信息。
     *  @param groupid      群号
     *  @param enterpriseId [出参] 群所属企业（群主所属企业）
     *  @param name         [出参] 群名
     *  @param isThread     [出参] 是否为帖子群（帖子群没有聊天流，不触发机器人）
     *  @return 群存在返回 true
     */
    virtual bool groupInfo(int64_t groupid, int64_t& enterpriseId, std::string& name, bool& isThread) = 0;

    //用户所属企业；不存在返回 0
    virtual int64_t userEnterprise(int64_t userId) = 0;

    //用户的显示名（账号昵称）
    virtual std::string displayName(int64_t userId) = 0;

    //用户在群里的显示名（群名片优先，其次账号昵称）
    virtual std::string groupDisplayName(int64_t groupid, int64_t userId) = 0;

    //用户合并各设备后的在线状态（Protocol.h 的 presence_type）
    virtual int presence(int64_t userId) = 0;

    //用户所在的部门及其全部上级部门
    virtual void userDeptChain(int64_t userId, std::vector<int64_t>& out) = 0;

    //群可用的问答（本群问答在前、企业问答在后，已去重）
    virtual void groupQa(int64_t groupid, int64_t enterpriseId, std::vector<bot::QaItem>& out) = 0;

    //企业的全部群（不含帖子群）
    virtual void enterpriseGroups(int64_t enterpriseId, std::vector<int64_t>& out) = 0;

    //企业的某项功能是否开启（FeatureSwitchManager）
    virtual bool featureOn(int64_t enterpriseId, const std::string& feature) = 0;

    /**
     *  以指定身份发一条文字消息（落库并推送）。
     *  @param ctype        会话类型：1 单聊 2 群聊
     *  @param from         发送者 userid（机器人账号或个人机器人的主人）
     *  @param target       单聊为对方 userid，群聊为群号
     *  @param content      content 对象的紧凑 JSON
     *  @param clientMsgId  判重用的客户端消息 ID；同一发送者的同一 ID 只发一次
     *  @return 消息 ID；已存在时返回已有的 ID；失败返回 0
     */
    virtual int64_t postMessage(int ctype, int64_t from, int64_t target, const std::string& content,
                                const std::string& clientMsgId) = 0;

    /**
     *  外部内容是否命中企业敏感词（命中时由实现方写拦截记录，来源 sensitive_source_bot_external）。
     *  @param enterpriseId 企业
     *  @param botUserId    机器人账号 userid（记为提交者）
     *  @param botName      机器人名称
     *  @param groupid      群号
     *  @param text         内容
     *  @return 命中返回 true
     */
    virtual bool hitSensitive(int64_t enterpriseId, int64_t botUserId, const std::string& botName, int64_t groupid,
                              const std::string& text) = 0;

    /**
     *  发一个 HTTP(S) POST 请求（阻塞）。
     *  @param url       地址
     *  @param headers   额外的请求头
     *  @param body      请求体（JSON）
     *  @param timeoutMs 超时（毫秒）
     *  @param status    [出参] HTTP 状态码；网络错误时为 0
     *  @param respBody  [出参] 应答体（超过上限的部分丢弃）
     *  @return 收到了应答返回 true
     */
    virtual bool httpPost(const std::string& url, const std::vector<std::pair<std::string, std::string> >& headers,
                          const std::string& body, int timeoutMs, int& status, std::string& respBody) = 0;
};

/**
 *  按分钟计数的频率限制器：同一个键在同一分钟内最多放行 limit 次。线程安全。
 */
class BotRateLimiter
{
public:
    BotRateLimiter();

    /**
     *  放行一次，并计数。
     *  @param key   计数的键，如 "c:<机器人id>:<会话>"
     *  @param limit 每分钟上限
     *  @param nowMs 当前时间（Unix 毫秒）
     *  @return 未超过上限返回 true
     */
    bool allow(const std::string& key, int limit, int64_t nowMs);

    /**
     *  清掉全部计数（单元测试用）。
     */
    void reset();

private:
    std::mutex                                        m_mutex;       //保护下面两项
    std::map<std::string, std::pair<int64_t, int> >   m_counters;    //键 → (分钟序号, 本分钟已放行次数)
    int64_t                                           m_lastPrune;   //上次清理过期计数的分钟序号
};

//群消息事件（判定入口交给引擎的副本）
struct BotGroupEvent
{
    int64_t     m_groupid;     //群号
    int64_t     m_enterpriseId; //群所属企业（群主所属企业，判定入口从内存缓存取）；0 表示未知，由机器人线程再查
    int64_t     m_from;        //发送者 userid
    int64_t     m_msgid;       //消息 ID
    int         m_msgtype;     //消息内容类型
    std::string m_content;     //content 对象的紧凑 JSON
    int64_t     m_timestamp;   //消息时间（Unix 毫秒）

    BotGroupEvent()
        : m_groupid(0), m_enterpriseId(0), m_from(0), m_msgid(0), m_msgtype(1), m_timestamp(0)
    {
    }
};

//单聊消息事件
struct BotSingleEvent
{
    int64_t     m_from;       //发送者 userid
    int64_t     m_to;         //接收者 userid（可能是个人机器人的主人）
    int64_t     m_msgid;      //消息 ID
    int         m_msgtype;    //消息内容类型
    std::string m_content;    //content 对象的紧凑 JSON
    int64_t     m_timestamp;  //消息时间（Unix 毫秒）

    BotSingleEvent()
        : m_from(0), m_to(0), m_msgid(0), m_msgtype(1), m_timestamp(0)
    {
    }
};

//「试一试」的模拟输入
struct BotTryInput
{
    std::string m_text;       //模拟的正文
    bool        m_at;         //群里：是否 @ 了机器人
    int64_t     m_groupid;    //群里：在哪个群试（取这个群的问答）；个人机器人为 0
    int64_t     m_senderId;   //个人机器人：模拟的发送者；0 表示本人以外的任意同事
    int         m_presence;   //个人机器人：模拟的主人在线状态
    int64_t     m_timeMs;     //模拟的时间（Unix 毫秒）；0 表示现在

    BotTryInput()
        : m_at(true), m_groupid(0), m_senderId(0), m_presence(0), m_timeMs(0)
    {
    }
};

//Webhook 的结果码（写在应答 JSON 的 code 字段里，见 docs/协议设计-v2.md 机器人一节）
enum bot_webhook_code
{
    //成功
    bot_webhook_ok              = 0,
    //请求体不是 JSON、缺字段或正文为空 / 超长
    bot_webhook_bad_request     = 1,
    //令牌不对，或签名不对、时间戳过期
    bot_webhook_unauthorized    = 2,
    //机器人已停用、没有启用「外部系统推送」规则，或企业关闭了相关功能
    bot_webhook_forbidden       = 3,
    //超过每分钟 20 条
    bot_webhook_rate_limited    = 4,
    //内容含企业敏感词
    bot_webhook_sensitive       = 5,
    //需要指定群（企业机器人应用到多个群时），或指定的群不在机器人的应用范围内
    bot_webhook_bad_group       = 6,
    //服务端内部错误（发消息失败）
    bot_webhook_server_error    = 7,
};

//外部服务的一次调用任务
struct BotExternalJob
{
    std::shared_ptr<const bot::BotRecord> m_bot;   //机器人
    int64_t     m_groupid;     //群号
    std::string m_groupName;   //群名
    int64_t     m_senderId;    //发送者
    std::string m_senderName;  //发送者显示名
    int64_t     m_msgid;       //触发消息 ID
    std::string m_question;    //剪掉 @ 之后的正文
    std::string m_url;         //外部服务地址
    std::string m_fallback;    //失败时回复的兜底文字
    int64_t     m_enterpriseId; //群所属企业

    BotExternalJob()
        : m_groupid(0), m_senderId(0), m_msgid(0), m_enterpriseId(0)
    {
    }
};

/**
 *  机器人引擎；全局单例，经 Singleton<BotEngine> 访问。
 */
class BotEngine final
{
public:
    BotEngine();
    ~BotEngine();

    BotEngine(const BotEngine& rhs) = delete;
    BotEngine& operator=(const BotEngine& rhs) = delete;

    /**
     *  启动机器人线程与外部服务工作线程，并开始定时检查。
     *  @param env             运行环境；所有权不转移，须活到 stop() 之后
     *  @param externalThreads 外部服务工作线程数（1～8）
     *  @return 恒为 true
     */
    bool start(IBotEnv* env, int externalThreads);

    /**
     *  停止全部线程（等待正在执行的外部请求结束）。
     */
    void stop();

    /**
     *  只设置运行环境、不起线程（单元测试用；之后直接调 process* 系列）。
     */
    void setEnvForTest(IBotEnv* env);

    // ---------------- 判定入口（I/O 线程调用） ----------------

    /**
     *  群消息推送完成之后调用：本群可能有机器人时投递给机器人线程。
     */
    void onGroupMessage(const BotGroupEvent& ev);

    /**
     *  单聊消息推送完成之后调用：收发任一方有个人机器人时投递（记录主人发言时间也需要它）。
     */
    void onSingleMessage(const BotSingleEvent& ev);

    /**
     *  新成员入群完成之后调用。
     *  @param groupid      群号
     *  @param enterpriseId 群所属企业（群主所属企业）
     *  @param members      新成员
     */
    void onMembersJoined(int64_t groupid, int64_t enterpriseId, const std::vector<int64_t>& members);

    // ---------------- 同步处理（机器人线程上执行；单元测试直接调用） ----------------

    //判定一条群消息并发出回复
    void processGroupMessage(const BotGroupEvent& ev);

    //判定一条单聊消息并发出代答
    void processSingleMessage(const BotSingleEvent& ev);

    //入群
    void processJoin(int64_t groupid, const std::vector<int64_t>& members);

    /**
     *  定时检查：发出这一分钟到点的定时消息。同一分钟只处理一次。
     *  @param nowMs 当前时间（Unix 毫秒）
     */
    void processScheduleTick(int64_t nowMs);

    /**
     *  执行一次外部服务调用并发出回复（外部服务工作线程上执行；单元测试直接调用）。
     */
    void processExternalJob(const BotExternalJob& job);

    // ---------------- Webhook 与试一试（HTTP 线程、会话线程调用） ----------------

    /**
     *  处理一次 Webhook 推送：{"token","timestamp","sign","msgtype","text","groupid"}。
     *  @param body     请求体
     *  @param respJson [出参] 应答 JSON：{"code","msg","msgid"}
     *  @return 结果码，取值见 bot_webhook_code
     */
    int handleWebhook(const std::string& body, std::string& respJson);

    /**
     *  「试一试」：按给定机器人（可以是尚未保存的）判定一段模拟输入，不发消息。
     *  @param b  机器人（含规则）
     *  @param in 模拟输入
     *  @return 判定结果（外部服务只返回地址，不实际调用）
     */
    bot::BotDecision tryBot(const bot::BotRecord& b, const BotTryInput& in);

    /**
     *  单元测试用：清掉冷却、主人发言时间、定时去重与频率计数。
     */
    void resetStateForTest();

private:
    //机器人能否在这个企业里工作（企业开关）
    bool botAllowed(const bot::BotRecord& b, int64_t enterpriseId);

    //发一条群消息（含频率限制与最近发消息时间）
    int64_t sendGroup(const bot::BotRecord& b, int64_t enterpriseId, int64_t groupid, const std::string& content,
                      const std::string& clientMsgId);

    //把外部服务任务排进工作线程；没有起线程时（单元测试）直接执行
    void enqueueExternal(const BotExternalJob& job);

    //外部服务工作线程的主循环
    void externalWorker();

    //定时检查的入口（机器人线程上的定时器回调）
    void onScheduleTimer();

private:
    IBotEnv*                                     m_env;             //运行环境；不拥有
    std::unique_ptr<net::EventLoopThread>        m_thread;          //机器人线程
    std::atomic<net::EventLoop*>                 m_loop;            //机器人线程的事件循环；未启动或已停止时为 NULL
    BotRateLimiter                               m_limiter;         //频率限制
    std::mutex                                   m_stateMutex;      //保护下面三张表（机器人线程与「试一试」都会读）
    std::map<std::pair<int64_t, int64_t>, int64_t> m_lastReply;     //(机器人 id, 发送者) → 上次代答时间（Unix 毫秒）
    std::map<std::pair<int64_t, int64_t>, int64_t> m_lastSpoke;     //(发言人, 对方) → 发言人上次在这个单聊里发言的时间
    std::string                                  m_lastScheduleKey; //上次处理过的定时分钟（yyyyMMddHHmm）

    std::vector<std::thread>                     m_workers;         //外部服务工作线程
    std::mutex                                   m_jobMutex;        //保护 m_jobs 与 m_stopping
    std::condition_variable                      m_jobCond;         //有新任务或要退出时通知工作线程
    std::deque<BotExternalJob>                   m_jobs;            //待执行的外部服务任务
    bool                                         m_stopping;        //正在停止：工作线程取完任务后退出
};
