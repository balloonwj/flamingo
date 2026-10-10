/**
 *  数据库连接工厂：业务层创建数据库连接的唯一入口。
 *  它按 CDbConfig 里配置的数据库后端类型造出对应的 IDatabase 实现并完成连接，
 *  业务层因此不需要知道当前跑的是 MySQL 还是 SQLite。
 *
 *  典型用法：
 *      std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
 *      if (!pConn)
 *      {
 *          LOGE("XxxManager db connect failed");
 *          return false;
 *      }
 *
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

#include <string>

class IDatabase;

class DbFactory
{
public:
    //按全局配置创建一条**已经连接好**的数据库连接。
    //返回值：成功返回连接对象，所有权归调用方，用完须 delete（业务层一般交给 unique_ptr 托管）；
    //       失败返回 NULL，具体原因（配置非法、连不上库等）已由本函数或驱动写入日志。
    static IDatabase* createConnection();

    //打开或关闭数据库连接池，MySQL 与 SQLite 两个后端的池一起打开或关闭。
    //enabled 为 true：此后 createConnection() 造出的连接先从池里取，析构时状态正常就放回池中；
    //enabled 为 false：池中现有的空闲连接全部关闭，之后每次都新建连接。
    //池对象在第一次调用本函数时创建，而 Singleton 的创建不是线程安全的，所以本函数必须在启动阶段、
    //各工作线程开始访问数据库之前，由主线程调用一次（打开或关闭都可以）。
    static void setConnectionPoolEnabled(bool enabled);

    //连接池当前是否打开
    static bool isConnectionPoolEnabled();

    //定期维护连接池：给空闲连接发 ping 保活，关闭长时间没有被使用的连接。
    //由定时器周期性调用；连接池从未创建过时什么也不做。副作用：可能发出多次 ping（网络往来）
    static void maintainConnectionPools();

    //关闭连接池中全部空闲连接，池保持原来的打开或关闭状态。
    //删除或重建数据库之前、进程退出之前调用；连接池从未创建过时什么也不做
    static void clearConnectionPools();

    //连接池当前状态与累计统计的一行英文描述（空闲连接数、命中与未命中次数、ping 次数与失败次数、
    //关闭的连接数），供定期写日志与测试核对。连接池从未创建过时返回说明未创建的文字
    static std::string describeConnectionPools();

private:
    //纯静态工具类，禁止实例化
    DbFactory();
    ~DbFactory();
};
