/**
 *  数据库连接句柄池：把用完的数据库连接句柄暂存起来，下次需要连接时直接拿来用，
 *  省掉建立连接的开销（MySQL 是 TCP 握手与登录认证，SQLite 是打开文件、设置 PRAGMA
 *  与解析库结构）。
 *
 *  本类只负责「句柄存放在哪里、什么时候检查、什么时候关闭」，不依赖具体的数据库：
 *  句柄以 void* 保存，关闭与探活（ping）由构造时传入的两个函数完成。
 *  每个数据库后端各持有一个实例（MySQL 的见 mysqlapi/DatabaseMysql.h 的 CMysqlHandlePool）。
 *
 *  三条时间规则（时长单位均为毫秒，取值见 Options）：
 *    · 取用前校验：句柄距上次与服务器往来超过 m_validateAfterMs 时，交给调用方之前先 ping
 *      一次，ping 不通就关闭它、改取下一个；
 *    · 保活：maintain() 给距上次往来超过 m_keepAliveAfterMs 的空闲句柄各 ping 一次，
 *      避免数据库服务器（MySQL 的 wait_timeout）或中间的网络设备因为空闲而断开连接；
 *    · 收缩：maintain() 关闭距上次被业务使用超过 m_maxIdleMs 的空闲句柄。ping 不算业务使用，
 *      所以负载降下来以后池会逐渐变小，不会被保活一直留住。
 *
 *  典型用法（以 MySQL 后端为例）：
 *      void* h = pool.acquire(key);     //池里没有可用句柄时返回 NULL，调用方自行新建连接
 *      ...在 h 上执行若干语句...
 *      pool.release(key, h);            //只有连接状态正常时才放回；有未结束的事务等情况应直接关闭
 *      //另由定时器周期性地调用 pool.maintain()
 *
 *  线程安全：所有公开方法都可以在任意线程调用。ping 与关闭句柄都在锁外执行，
 *  一次网络往来不会阻塞其他线程取用或归还句柄。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <mutex>
#include <string>
#include <vector>

class DbHandlePool
{
public:
    //关闭一个句柄并释放其资源。handle 不会是 NULL；调用之后本池不再使用该句柄
    typedef void (*CloseHandleFn)(void* handle);

    //探测一个句柄对应的连接是否仍然可用（MySQL 即 mysql_ping）。
    //返回 true 表示可用。可以为 NULL，表示该后端不需要探活（如 SQLite），此时取用前校验与保活都不做
    typedef bool (*PingHandleFn)(void* handle);

    //返回当前时刻，单位毫秒，只要求单调递增、不要求与日历时间对应。
    //传 NULL 时使用 std::chrono::steady_clock；单元测试传入可控的假时钟
    typedef int64_t (*NowMsFn)();

    //池的容量与三条时间规则的阈值
    struct Options
    {
        //池中空闲句柄总数的上限（所有 key 合计）。归还时超出上限，关闭最早归还的那个句柄
        size_t  m_maxIdle;

        //取用前校验的阈值，单位毫秒：句柄距上次与服务器往来达到该时长，交出去之前先 ping。
        //0 表示每次取用都 ping；负数表示从不在取用时 ping
        int64_t m_validateAfterMs;

        //保活的阈值，单位毫秒：maintain() 时句柄距上次与服务器往来达到该时长就 ping 一次。
        //负数表示 maintain() 不做保活
        int64_t m_keepAliveAfterMs;

        //收缩的阈值，单位毫秒：maintain() 时句柄距上次被业务归还达到该时长就关闭。
        //负数表示不按空闲时长关闭
        int64_t m_maxIdleMs;

        //按 DbHandlePool.cpp 里的默认常量初始化
        Options();
    };

    //累计统计，供测试与运维日志使用。所有计数从池创建起累加，clear() 不清零
    struct Stats
    {
        uint64_t m_acquireHits;      //acquire() 成功交出池中句柄的次数
        uint64_t m_acquireMisses;    //acquire() 因池中没有可用句柄而返回 NULL 的次数
        uint64_t m_validatePings;    //取用前校验发出的 ping 次数
        uint64_t m_keepAlivePings;   //maintain() 保活发出的 ping 次数
        uint64_t m_pingFailures;     //上面两类 ping 中失败（句柄随即被关闭）的次数
        uint64_t m_closedOverflow;   //因超出 m_maxIdle 被关闭的句柄数
        uint64_t m_closedIdle;       //因空闲超过 m_maxIdleMs 被 maintain() 关闭的句柄数

        Stats();
    };

public:
    /**
     *  创建一个空池，初始为<u>关闭</u>状态（见 setEnabled）。
     *  closeFn：关闭句柄的函数，不能为 NULL；
     *  pingFn：探活函数，可以为 NULL（该后端不需要探活）；
     *  options：容量与时间阈值；
     *  nowFn：时钟函数，NULL 表示使用 steady_clock。
     */
    DbHandlePool(CloseHandleFn closeFn, PingHandleFn pingFn, const Options& options, NowMsFn nowFn = NULL);

    //析构时关闭池中全部空闲句柄
    ~DbHandlePool();

    /**
     *  打开或关闭本池。关闭状态下 acquire() 一律返回 NULL、release() 直接关闭句柄；
     *  从打开切换到关闭时，池中现有的空闲句柄全部关闭。
     */
    void setEnabled(bool enabled);

    //本池当前是否处于打开状态
    bool isEnabled() const;

    //替换容量与时间阈值，对之后的 acquire / release / maintain 生效；已在池中的句柄不立即调整
    void setOptions(const Options& options);

    //当前生效的容量与时间阈值
    Options getOptions() const;

    /**
     *  取出一个属于 key 的空闲句柄，所有权交给调用方。
     *  优先取最近归还的那个（其连接最可能仍然可用）。达到校验阈值的句柄先 ping，
     *  ping 不通的直接关闭并继续找下一个。
     *  key：区分连接目标的字符串（MySQL 为主机、账号与库名，SQLite 为文件路径），
     *       不同 key 的句柄绝不混用。
     *  返回值：可用的句柄；池中没有可用句柄或本池处于关闭状态时返回 NULL。
     *  副作用：可能发出 ping（一次网络往来），可能关闭失效句柄。
     */
    void* acquire(const std::string& key);

    /**
     *  把一个用完的句柄归还给本池，所有权随之转交本池。
     *  调用方须保证句柄处于可以直接交给下一个使用者的状态（没有未结束的事务、
     *  没有未取完的结果集等），不满足时应自行关闭而不是归还。
     *  key：与 acquire 时相同含义的连接目标字符串；
     *  handle：要归还的句柄，NULL 时什么也不做。
     *  副作用：本池处于关闭状态时直接关闭该句柄；超出 m_maxIdle 时关闭最早归还的那个句柄。
     */
    void release(const std::string& key, void* handle);

    /**
     *  定期维护：关闭空闲过久的句柄，并给达到保活阈值的句柄各 ping 一次。
     *  由定时器周期性调用，周期应明显短于 m_keepAliveAfterMs 与数据库的空闲超时。
     *  副作用：可能发出多次 ping（锁外逐个执行，期间被 ping 的句柄暂不参与取用），可能关闭句柄。
     */
    void maintain();

    //关闭池中全部空闲句柄。删除数据库文件或数据库之前、以及进程退出前调用
    void clear();

    //池中当前的空闲句柄数（所有 key 合计）
    size_t idleCount() const;

    //累计统计的快照
    Stats getStats() const;

private:
    //池中的一个空闲句柄
    struct Entry
    {
        std::string m_key;            //连接目标，见 acquire 的 key 参数
        void*       m_handle;         //句柄本身，非 NULL
        int64_t     m_lastUsedMs;     //最近一次被业务归还的时刻，单位毫秒，用于收缩判定
        int64_t     m_lastActiveMs;   //最近一次与服务器往来（业务归还或 ping 成功）的时刻，单位毫秒，用于校验与保活判定
    };

    //读当前时刻，单位毫秒
    int64_t nowMs() const;

    //池中空闲句柄超出 m_maxIdle 时，把最早归还的那些移出到 toClose。调用方须已持有 m_mutex
    void trimToCapacityLocked(std::vector<void*>& toClose);

    //逐个关闭 handles 中的句柄。须在锁外调用
    void closeAll(const std::vector<void*>& handles);

    CloseHandleFn       m_closeFn;    //关闭句柄的函数，构造后不变
    PingHandleFn        m_pingFn;     //探活函数，NULL 表示不探活，构造后不变
    NowMsFn             m_nowFn;      //时钟函数，NULL 表示 steady_clock，构造后不变

    mutable std::mutex  m_mutex;      //保护以下全部成员
    Options             m_options;    //当前生效的容量与时间阈值
    bool                m_bEnabled;   //本池是否打开，见 setEnabled

    //空闲句柄，按归还时刻从早到晚排列：尾部是最近归还的，取用从尾部找，超容量时从头部关闭
    std::vector<Entry>  m_idle;

    Stats               m_stats;      //累计统计
};
