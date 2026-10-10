/**
 *  IDatabase 的 MySQL / MariaDB 实现：封装一条 MySQL 连接，负责建立连接、执行 SQL、
 *  断线后自动重连重试一次，以及把结果集包装成 MysqlQueryResult 交给调用方。
 *
 *  业务层不直接构造本类，而是通过 DbFactory::createConnection() 拿到 IDatabase*；
 *  只有 mysqlmgr/MysqlManager 这类 MySQL 专属的建库建表代码才直接使用本类，
 *  因为它需要用「不指定库名」的方式连接以执行 show databases / create database。
 *
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

#include <stdint.h>
#include <string>
//MariaDB Connector/C 的公共头文件。连接器以源码形式随仓库提交（mariadb-connector-c-3.4.9），这些头文件在其 include 目录下，不再带 mysql/ 前缀
#include <mysql.h>
#include <errmsg.h>

#include "../dbapi/IDatabase.h"
#include "../dbapi/DbHandlePool.h"

/**
 *  MySQL 连接句柄池：DbHandlePool 配上 MySQL 的关闭（mysql_close）与探活（mysql_ping）函数，
 *  池中保存的是 MYSQL* 句柄。全进程一个，通过 Singleton<CMysqlHandlePool>::Instance() 访问。
 *
 *  只有经 DbFactory::createConnection() 创建的连接才会进池（见 CDatabaseMysql::initialize
 *  的 usePool 参数）；CMysqlManager、CMysqlThrd 自己持有的长连接不受影响。
 *  池的打开、关闭与定时维护统一经 DbFactory 进行，见 DbFactory.h。
 */
class CMysqlHandlePool : public DbHandlePool
{
public:
    //创建一个空池，初始为关闭状态，容量与时间阈值取 DbHandlePool::Options 的默认值
    CMysqlHandlePool();
};

class CDatabaseMysql : public IDatabase
{
public:
	//一条连接所需的全部参数，保存下来供断线重连时复用
	struct DatabaseInfo
	{
		std::string strHost;
        std::string strUser;
        std::string strPwd;
        std::string strDBName;
	};

public:
	CDatabaseMysql(void);
	virtual ~CDatabaseMysql(void);

	//建立到 MySQL 的连接，重复调用会先关闭旧连接再重连。
	//host：数据库主机地址；user：登录账号；pwd：登录口令；
	//dbname：要选用的库名，传空串表示只连接服务器而不选库
	//        （CMysqlManager 建库阶段就是这么用的：库还不存在，没法选）。
	//usePool：是否使用连接池。为 true 时先从 CMysqlHandlePool 里取一条连到同一主机、账号与库的
	//        空闲连接，取不到再新建；本对象析构时，若连接状态正常（没有未结束的事务、最后一次
	//        操作没有出错）则把连接放回池中，而不是关闭。只有 DbFactory 传 true。
	//返回值：true 表示连接成功，参数会被保存下来供后续断线重连复用。
	bool initialize(const std::string& host, const std::string& user, const std::string& pwd, const std::string& dbname,
	                bool usePool = false);

	virtual QueryResult* query(const char *sql);

	//基类里 query(const std::string&) 这个便利重载会被本类的 query 声明遮蔽，
	//用 using 把它重新引入本类作用域；否则通过 CDatabaseMysql* 调用 query(ss.str()) 编译不过。
	using IDatabase::query;

	virtual QueryResult* pquery(const char *format,...);
	virtual bool execute(const char* sql);
	virtual bool execute(const char* sql, uint32_t& uAffectedCount, int& nErrno);
	virtual bool pexecute(const char *format,...);

	//事务接口，语义见 dbapi/IDatabase.h 里逐个方法的说明。
	//MySQL 一侧的两点实现细节：开启事务发的是 START TRANSACTION 而不是 BEGIN
	//（BEGIN 在存储程序里是复合语句的起始记号，含义会打架，官方文档因此推荐前者）；
	//事务进行中一旦发现连接断开，本类会立刻把事务标志清掉并让本次操作整体失败，
	//既不重连也不重试 —— 重连得到的是一条全新的、处于自动提交状态的连接，
	//在它上面接着跑剩下的语句会让每条语句各自提交，原子性被静默破坏。
	virtual bool beginTransaction();
	virtual bool commit();
	virtual bool rollback();
	virtual bool inTransaction() const;

	virtual uint32_t getInsertID();

	//丢弃连接上尚未取走的多结果集，避免下一条语句报 "commands out of sync"。
	//MySQL 专有行为，不进 IDatabase 接口。
	void clearStoredResults();

	virtual int32_t escapeString(char* szDst, const char* szSrc, uint32_t uSize);

private:
	//打印一条数据库错误日志：包含 mysql 错误码、错误文本与出错的 SQL 原文。
	//operation：出错的操作名（如 "query"/"execute"），用于定位；
	//sql：出错的 SQL 语句原文，可为 NULL（连接类错误无对应 SQL）。
	void logDbError(const char* operation, const char* sql);

	//事务进行中撞上断线错误时的统一处理：清掉事务标志、记一条日志，告诉调用方本次操作
	//必须整体失败。抽出来是因为 query 与两个 execute 重载各需要一份，逻辑必须完全一致。
	//uMysqlErrno：mysql_errno() 的返回值；
	//operation：出错的操作名（如 "query"/"execute"），用于定位；
	//sql：出错的 SQL 语句原文，可为 NULL。
	//返回 true 表示「确实是事务中途断线，已按整体失败处理」，调用方应立即返回失败；
	//返回 false 表示不属于这种情形，调用方继续走原有的重试 / 报错判断。
	bool abortTransactionOnDisconnect(unsigned int uMysqlErrno, const char* operation, const char* sql);

	//按 m_DBInfo 新建一条连接，替换当前连接（当前连接直接关闭，不放回池中）。
	//用于断线后重连：此时连接池里的其他空闲连接很可能也已断开（例如数据库服务刚重启过），
	//所以始终新建，不从池里取。返回 true 表示连接成功
	bool reconnect();

	//按 m_DBInfo 建立一条新连接，结果写入 m_Mysql / m_bInit。调用前当前连接必须已经关闭。
	//返回 true 表示连接成功；失败原因已写入日志
	bool connectFresh();

	//关闭当前连接（不放回池中），并把 m_Mysql、m_bInit 复位
	void closeConnection();

	//当前连接能否放回连接池：没有未结束的事务，且最后一次操作没有出错。
	//出过错的连接一律不放回 —— 错误可能是断线，也可能是别的异常状态，关掉重开最稳妥
	bool canReturnToPool() const;

	//本连接在连接池中的 key：主机、账号与库名，三者都相同的连接才能互相替代
	std::string poolKey() const;

	DatabaseInfo    m_DBInfo;    //数据库连接参数（主机/用户/密码/库名），断线重连时复用

	//本连接是否使用连接池，见 initialize 的 usePool 参数。为 true 时析构可能把连接放回池中
	bool            m_bPooled;
	MYSQL*          m_Mysql;     //MySQL 连接句柄；NULL 表示尚未连接或连接已断开
	bool            m_bInit;     //是否已成功建立过连接，析构时据此决定是否 mysql_close

	//本连接上当前是否有一个由 beginTransaction() 开启、尚未了结的事务。
	//除 commit / rollback 外，事务进行中检测到断线时也会被清掉 —— 那种情况下
	//服务端一侧的事务已经被回滚，本连接上再谈「事务」已无意义。
	bool            m_bInTransaction;

	//连接的代数：每成功建立一次连接（含断线重连）自增 1，从 0 起算。
	//用来判断 getInsertID() 读到的自增 ID 是不是还属于写入它的那条连接 ——
	//mysql_insert_id() 取的是客户端句柄上缓存的值，换了句柄就只剩 0，
	//若不加区分，调用方无从分辨「这条语句本来就没有自增列」和「ID 随着旧连接一起没了」。
	uint32_t        m_uConnectionGeneration;

	//最近一次成功执行写语句时的连接代数。getInsertID() 拿它与 m_uConnectionGeneration
	//比对，不相等说明中间发生过重连，此时缓存的自增 ID 已经失效。
	uint32_t        m_uInsertIdGeneration;
};
