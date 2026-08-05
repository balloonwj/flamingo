/**
 *  DbFactory 的实现，详见 DbFactory.h。
 *  balloonwj@qq.com   2026-07-31
 */
#include "DbFactory.h"

#include "DbConfig.h"
#include "IDatabase.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "../sqliteapi/DatabaseSqlite.h"

//FLAMINGO_NO_MYSQL 由 CMake 在 -DWITH_MYSQL=OFF 时定义，用于编出一份
//不依赖 libmysqlclient 的纯本地版本，见 CMakeLists.txt 顶部的说明
#ifndef FLAMINGO_NO_MYSQL
#include "../mysqlapi/DatabaseMysql.h"
#endif

IDatabase* DbFactory::createConnection()
{
    CDbConfig& config = Singleton<CDbConfig>::Instance();

    switch (config.getDbType())
    {
    case DB_TYPE_MYSQL:
#ifdef FLAMINGO_NO_MYSQL
        //本次构建关掉了 MySQL 后端，只能用 dbtype=sqlite。
        //明确报错而不是静默改用 SQLite：配置写的是 mysql 却连到了别的库，问题更难查
        LOGE("create db connection failed, this build has no mysql backend "
             "(built with WITH_MYSQL=OFF), please set dbtype=sqlite, config: %s",
             config.describe().c_str());
        return NULL;
#else
        {
            CDatabaseMysql* pConn = new CDatabaseMysql();
            if (!pConn->initialize(config.getHost(), config.getUser(),
                                   config.getPassword(), config.getDbName()))
            {
                //连接失败的详细原因（错误码、错误文本）已由 CDatabaseMysql::initialize 打过日志，
                //这里不再重复打印，只负责回收对象
                delete pConn;
                return NULL;
            }

            return pConn;
        }
#endif
    case DB_TYPE_SQLITE:
        {
            CDatabaseSqlite* pConn = new CDatabaseSqlite();
            if (!pConn->initialize(config.getDbPath()))
            {
                //打开失败的详细原因已由 CDatabaseSqlite::initialize 打过日志
                delete pConn;
                return NULL;
            }

            return pConn;
        }
    default:
        LOGE("create db connection failed, unknown db type: %d, config: %s",
             (int)config.getDbType(), config.describe().c_str());
        return NULL;
    }
}
