/**
 *  数据库查询结果集的抽象基类：只保存与具体数据库无关的部分 —— 当前行的字段数组与列名表，
 *  由 MysqlQueryResult / SqliteQueryResult 等子类负责从各自的原生结果集里取行、填进行缓冲区。
 *
 *  典型用法（业务层遍历结果集的固定写法）：
 *      QueryResult* pResult = pConn->query("SELECT f_user_id, f_username FROM t_user");
 *      if (pResult == NULL)
 *          return false;
 *      while (true)
 *      {
 *          Field* pRow = pResult->fetch();
 *          if (pRow == NULL)               //取完最后一行后 fetch() 返回 NULL，循环据此结束
 *              break;
 *          int32_t userid = pRow[0].getInt32();
 *          std::string username = pRow[1].getString();
 *          if (!pResult->nextRow())
 *              break;
 *      }
 *      delete pResult;                     //结果集由调用方负责释放
 *
 *  注意：连接的 query() 返回结果集之前已经调用过一次 nextRow()，也就是说拿到手的结果集
 *  已经停在第一行上，所以上面的循环是「先 fetch 后 nextRow」而不是反过来。
 *
 *  本文件由 mysqlapi/QueryResult.h 拆分而来：与 MySQL 相关的部分（MYSQL_RES 句柄、
 *  原生列类型映射）已下沉到 mysqlapi/MysqlQueryResult.h。
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

#include <stdint.h>
#include <vector>
#include <map>
#include <string>

#include "Field.h"

class QueryResult
{
public:
    //列序号（从 0 开始）到列名的映射表
    typedef std::map<uint32_t, std::string> FieldNames;

public:
    //rowCount：结果集的总行数；fieldCount：结果集的列数。
    //构造时按列数分配「当前行」缓冲区，列名与列类型由子类构造函数负责填入。
    QueryResult(uint64_t rowCount, uint32_t fieldCount);
    virtual ~QueryResult();

    //推进到下一行，成功返回 true。
    //返回 false 表示已无更多行，子类实现须在此时调用 endQuery() 释放资源，
    //使后续 fetch() 返回 NULL —— 业务层的遍历循环正是靠这一点结束的。
    virtual bool nextRow() = 0;

    //提前释放结果集占用的资源，允许重复调用。
    //子类须覆写以额外释放各自的原生结果集句柄，并在覆写实现里调用本类实现回收行缓冲区。
    virtual void endQuery();

    //按列名查列序号。name 为列名（大小写敏感，与结果集返回的列名一致）；
    //列名不存在时返回 uint32_t(-1)，调用方须自行判断。
    uint32_t getField_idx(const std::string& name) const
    {
        for(FieldNames::const_iterator iter = getFieldNames().begin(); iter != getFieldNames().end(); ++iter)
        {
            if(iter->second == name)
                return iter->first;
        }

        return uint32_t(-1);
    }

    //取当前行的字段数组，长度为 getFieldCount()；已无更多行时返回 NULL
    Field* fetch() const { return m_CurrentRow; }

    //按列序号取当前行的某个字段。index 须落在 [0, getFieldCount()) 内，本类不做越界检查
    const Field & operator [] (int index) const
    {
        return m_CurrentRow[index];
    }

    //按列名取当前行的某个字段。列名不存在时下标为 uint32_t(-1)，属于调用方错误
    const Field& operator [] (const std::string &name) const
    {
        return m_CurrentRow[getField_idx(name)];
    }

    //结果集的列数
    uint32_t getFieldCount() const { return m_FieldCount; }
    //结果集的总行数
    uint64_t getRowCount() const { return m_RowCount; }
    //列序号到列名的映射表
    FieldNames const& getFieldNames() const {return m_FieldNames; }
    //按列序号排列的列名数组
    std::vector<std::string> const& getNames() const {return m_vtFieldNames;}

protected:
    //回收「当前行」缓冲区并把指针置空，允许重复调用。
    //之所以单独提供这个非虚函数，是因为基类析构函数里不能调用虚函数 endQuery()——
    //那时虚表已退化为基类版本，子类持有的原生句柄不会被释放。
    void freeRowBuffer();

protected:
    Field*                      m_CurrentRow;       //当前行的字段数组，长度 m_FieldCount；取完最后一行后置为 NULL
    uint32_t                    m_FieldCount;       //结果集列数
    uint64_t                    m_RowCount;         //结果集总行数
    FieldNames                  m_FieldNames;       //列序号 -> 列名
    std::vector<std::string>    m_vtFieldNames;     //按列序号排列的列名，供 getNames() 返回
};
