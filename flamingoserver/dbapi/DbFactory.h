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

class IDatabase;

class DbFactory
{
public:
    //按全局配置创建一条**已经连接好**的数据库连接。
    //返回值：成功返回连接对象，所有权归调用方，用完须 delete（业务层一般交给 unique_ptr 托管）；
    //       失败返回 NULL，具体原因（配置非法、连不上库等）已由本函数或驱动写入日志。
    static IDatabase* createConnection();

private:
    //纯静态工具类，禁止实例化
    DbFactory();
    ~DbFactory();
};
