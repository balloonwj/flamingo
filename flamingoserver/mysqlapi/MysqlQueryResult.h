/**
 *  QueryResult 的 MySQL 实现：接管 mysql_store_result 返回的 MYSQL_RES 句柄，
 *  逐行把 MySQL 的原生行数据拷进基类的行缓冲区，并把 MySQL 的原生列类型
 *  映射成与数据库无关的 Field::DataTypes。
 *
 *  本类只由 CDatabaseMysql::query 内部构造，业务层拿到的是基类指针 QueryResult*，
 *  用法见 dbapi/QueryResult.h 的说明。
 *
 *  本文件由 mysqlapi/QueryResult.h 拆分而来：与数据库无关的部分已上提到 dbapi/QueryResult.h。
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

//MariaDB Connector/C 的公共头文件。连接器以源码形式随仓库提交（mariadb-connector-c-3.4.9），这些头文件在其 include 目录下，不再带 mysql/ 前缀
#include <mysql.h>
#include <stdint.h>

#include "../dbapi/QueryResult.h"

class MysqlQueryResult : public QueryResult
{
public:
    //result：mysql_store_result 返回的结果集句柄，不得为 NULL；
    //        本对象**接管**其生命周期，析构或 endQuery() 时调用 mysql_free_result 释放。
    //rowCount：结果集的总行数（取自 mysql_affected_rows）。
    //fieldCount：结果集的列数（取自 mysql_field_count）。
    MysqlQueryResult(MYSQL_RES* result, uint64_t rowCount, uint32_t fieldCount);
    virtual ~MysqlQueryResult();

    //取下一行填入行缓冲区。无更多行时释放资源并返回 false，此后 fetch() 返回 NULL
    virtual bool nextRow();

    //释放行缓冲区与 MYSQL_RES 句柄，允许重复调用
    virtual void endQuery();

private:
    //把 MySQL 的原生列类型映射为与数据库无关的 Field::DataTypes。
    //mysqlType：mysql_fetch_fields 返回的列类型；无法识别的类型映射为 DB_TYPE_UNKNOWN。
    enum Field::DataTypes convertNativeType(enum_field_types mysqlType) const;

    //释放 MYSQL_RES 句柄并置空，允许重复调用
    void freeResult();

private:
    MYSQL_RES*  m_Result;   //MySQL 原生结果集句柄，由本对象负责释放；已释放时为 NULL
};
