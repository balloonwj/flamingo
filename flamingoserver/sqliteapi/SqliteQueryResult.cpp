/**
 *  SqliteQueryResult 的实现，详见 SqliteQueryResult.h。
 *  balloonwj@qq.com   2026-07-31
 */
#include "SqliteQueryResult.h"

namespace sqliteapi {

SqliteQueryResult::SqliteQueryResult(const std::vector<std::string>& fieldNames,
                                     const std::vector<Field::DataTypes>& fieldTypes,
                                     std::vector<std::vector<SqliteCell> >& rows)
    : QueryResult(static_cast<uint64_t>(rows.size()), static_cast<uint32_t>(fieldNames.size())),
      m_nextRowIndex(0)
{
    //基类构造函数已按列数分配好行缓冲区，这里填列名与列类型。
    //注意 rows.size() 必须在 swap 之前取值，故它写在上面的初始化列表里。
    for (uint32_t i = 0; i < m_FieldCount; i++)
    {
        m_FieldNames[i] = fieldNames[i];
        m_vtFieldNames.push_back(fieldNames[i]);

        //列类型数组理论上与列名等长，这里仍做一次防御，缺失时按未知类型处理
        if (i < fieldTypes.size())
        {
            m_CurrentRow[i].setType(fieldTypes[i]);
        }
        else
        {
            m_CurrentRow[i].setType(Field::DB_TYPE_UNKNOWN);
        }
    }

    //把结果行搬进来而不是拷贝一份：全量加载用户表这类查询可能有上万行，深拷贝的代价不小
    m_rows.swap(rows);
}

SqliteQueryResult::~SqliteQueryResult()
{
    //本类没有需要手工释放的原生句柄（语句句柄在 CDatabaseSqlite::query 里就 finalize 了），
    //行缓冲区由基类析构函数回收，m_rows 由 vector 自行析构。
}

bool SqliteQueryResult::nextRow()
{
    //已经取完或被提前 endQuery：行缓冲区已释放，不能再填
    if (m_CurrentRow == NULL)
        return false;

    if (m_nextRowIndex >= m_rows.size())
    {
        //已取完最后一行：释放资源，使后续 fetch() 返回 NULL，业务层的遍历循环据此结束
        endQuery();
        return false;
    }

    const std::vector<SqliteCell>& row = m_rows[m_nextRowIndex];
    m_nextRowIndex++;

    for (uint32_t i = 0; i < m_FieldCount; i++)
    {
        if (i < row.size())
        {
            const SqliteCell& cell = row[i];
            m_CurrentRow[i].m_bNULL = cell.m_bNull;
            //按长度赋值而不是当成 C 字符串：BLOB 列的内容里可能含 '\0'
            m_CurrentRow[i].setValue(cell.m_strValue.c_str(), cell.m_strValue.size());
        }
        else
        {
            //行的单元格数少于列数属于构造方的错误，这里按 NULL 兜底，避免越界
            m_CurrentRow[i].m_bNULL = true;
            m_CurrentRow[i].setValue("", 0);
        }

        m_CurrentRow[i].setName(m_FieldNames[i]);
    }

    return true;
}

void SqliteQueryResult::endQuery()
{
    //先回收行缓冲区，与 MySQL 实现的释放顺序保持一致
    QueryResult::endQuery();

    //vector::clear 只把 size 置零、不归还容量，用 swap 一个空 vector 的写法才能真正释放内存。
    //结果集可能占用几十兆（全量加载用户表），提前归还是有意义的。
    std::vector<std::vector<SqliteCell> >().swap(m_rows);
    m_nextRowIndex = 0;
}

} // namespace sqliteapi
