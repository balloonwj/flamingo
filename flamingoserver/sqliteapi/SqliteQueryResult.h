/**
 *  QueryResult 的 SQLite 实现，SqliteQueryResult.h
 *  balloonwj@qq.com   2026-07-31
 *
 *  与 MySQL 实现的一个关键差异：MySQL 用 mysql_store_result 把整个结果集先取到客户端，
 *  本类对齐这个语义 —— 由 CDatabaseSqlite::query 一次性把 sqlite3_stmt 的所有行读进内存后
 *  再构造本对象，构造完语句句柄就已经 finalize 掉了。这样做的好处是结果集的生命周期
 *  与连接彻底解耦，业务层「拿着 QueryResult 慢慢遍历」的既有写法不会踩到句柄失效的坑。
 *
 *  用法见 dbapi/QueryResult.h 的说明，业务层拿到的是基类指针 QueryResult*。
 */
#ifndef __SQLITE_QUERY_RESULT_H__
#define __SQLITE_QUERY_RESULT_H__

#include <stdint.h>
#include <string>
#include <vector>

#include "../dbapi/QueryResult.h"

namespace sqliteapi {

//结果集中的一个单元格。取值统一以字节串保存（与 Field 的存储方式一致），
//BLOB 列保存原始字节，其余类型保存 UTF-8 文本。
struct SqliteCell
{
    SqliteCell() : m_bNull(false) {}

    std::string m_strValue;     //单元格取值；SQL NULL 时为空串
    bool        m_bNull;        //该单元格是否为 SQL NULL
};

class SqliteQueryResult : public QueryResult
{
public:
    /**
     *  构造一个已经把全部结果行读进内存的结果集。
     *  @param fieldNames 按列序号排列的列名，其大小即结果集列数。
     *  @param fieldTypes 按列序号排列的列类型，大小须与 fieldNames 一致。
     *  @param rows       全部结果行，每行的单元格数须等于列数。
     *                    **注意**：为避免大结果集的深拷贝，构造函数会把 rows 的内容
     *                    swap 走，调用方传入的 rows 在构造之后变为空，不要再使用。
     *
     *  构造完成后结果集停在「第一行之前」，须由调用方再调一次 nextRow() 把它推到第一行上。
     *  这与 MySQL 实现的约定完全一致（见 CDatabaseMysql::query）。
     */
    SqliteQueryResult(const std::vector<std::string>& fieldNames,
                      const std::vector<Field::DataTypes>& fieldTypes,
                      std::vector<std::vector<SqliteCell> >& rows);
    virtual ~SqliteQueryResult();

    //推进到下一行；无更多行时释放资源并返回 false，此后 fetch() 返回 NULL
    virtual bool nextRow();

    //提前释放行缓冲区与已读入内存的全部结果行，允许重复调用
    virtual void endQuery();

private:
    std::vector<std::vector<SqliteCell> >   m_rows;             //全部结果行，构造时从调用方 swap 而来
    size_t                                  m_nextRowIndex;     //下一次 nextRow() 要取的行下标
};

} // namespace sqliteapi

#endif // __SQLITE_QUERY_RESULT_H__
