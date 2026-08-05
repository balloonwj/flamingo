/**
 *  全局数据库配置：保存进程当前使用的数据库后端类型及其连接参数。
 *  进程启动时由 main() 从 etc/chatserver.conf 读出配置项灌进本单例，
 *  之后 DbFactory 与各数据库驱动都从这里取参数，业务层不再逐层传递连接串。
 *
 *  典型用法：
 *      Singleton<CDbConfig>::Instance().init(dbtype, dbserver, dbuser, dbpassword, dbname, dbpath);
 *      ...
 *      if (Singleton<CDbConfig>::Instance().getDbType() == DB_TYPE_SQLITE) { ... }
 *
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

#include <string>

//数据库后端类型
enum DbType
{
    //远程 MySQL / MariaDB。默认值：配置文件不写 dbtype 项时取该值，
    //保证历史部署升级后行为完全不变。
    DB_TYPE_MYSQL  = 0,

    //本地 SQLite 单文件数据库。整个库就是磁盘上的一个文件，
    //部署时不需要另外准备数据库服务，适合单机部署、演示环境与开发调试。
    DB_TYPE_SQLITE = 1
};

//dbtype 配置项写成该文本时使用 MySQL 后端（比较时大小写不敏感）
#define DB_TYPE_NAME_MYSQL      "mysql"
//dbtype 配置项写成该文本时使用 SQLite 后端（比较时大小写不敏感）
#define DB_TYPE_NAME_SQLITE     "sqlite"
//dbpath 配置项缺失或为空时使用的默认 SQLite 数据库文件路径（相对进程工作目录）
#define DEFAULT_SQLITE_DB_PATH  "data/flamingo.db"

class CDbConfig
{
public:
    CDbConfig();
    ~CDbConfig();

    //用配置文件里读到的原始文本初始化本单例，进程启动时调用一次。
    //dbtype：数据库类型文本，取 "mysql" 或 "sqlite"，比较时大小写不敏感；
    //        为 NULL、空串或取值非法时回落为 mysql，并打一条告警日志。
    //host / user / pwd / dbname：MySQL 模式下的主机、账号、口令与库名；
    //        SQLite 模式下这四项不参与连接，仅原样保存备查。四者均可为 NULL，按空串处理。
    //dbpath：SQLite 模式下的数据库文件路径，相对路径以进程工作目录为基准；
    //        为 NULL 或空串时取 DEFAULT_SQLITE_DB_PATH。MySQL 模式下不参与连接。
    void init(const char* dbtype, const char* host, const char* user, const char* pwd,
              const char* dbname, const char* dbpath);

    //当前使用的数据库后端类型，决定 DbFactory 造出哪种连接
    DbType getDbType() const { return m_dbType; }

    //MySQL 主机地址（SQLite 模式下无意义）
    const std::string& getHost() const { return m_strHost; }
    //MySQL 登录账号（SQLite 模式下无意义）
    const std::string& getUser() const { return m_strUser; }
    //MySQL 登录口令（SQLite 模式下无意义）
    const std::string& getPassword() const { return m_strPassword; }
    //MySQL 库名（SQLite 模式下无意义）
    const std::string& getDbName() const { return m_strDbName; }
    //SQLite 数据库文件路径（MySQL 模式下无意义）
    const std::string& getDbPath() const { return m_strDbPath; }

    //返回一句人类可读的连接描述，专供日志使用，形如
    //  "mysql 127.0.0.1/flamingo"  或  "sqlite data/flamingo.db"。
    //出于安全考虑不包含登录口令。
    std::string describe() const;

private:
    DbType      m_dbType;       //数据库后端类型，见 DbType
    std::string m_strHost;      //MySQL 主机地址，形如 127.0.0.1
    std::string m_strUser;      //MySQL 登录账号
    std::string m_strPassword;  //MySQL 登录口令，不得写入日志
    std::string m_strDbName;    //MySQL 库名，默认 flamingo
    std::string m_strDbPath;    //SQLite 数据库文件路径，相对进程工作目录
};
