/**
 *  MysqlQueryResult 的实现，详见 MysqlQueryResult.h。
 *  本文件由 mysqlapi/QueryResult.cpp 拆分而来，取行与类型映射的逻辑与拆分前一致。
 *  balloonwj@qq.com   2026-07-31
 */
#include "MysqlQueryResult.h"

MysqlQueryResult::MysqlQueryResult(MYSQL_RES* result, uint64_t rowCount, uint32_t fieldCount)
    : QueryResult(rowCount, fieldCount), m_Result(result)
{
    MYSQL_FIELD* fields = mysql_fetch_fields(m_Result);

    for (uint32_t i = 0; i < m_FieldCount; i++)
    {
        //TODO: 这个地方要不要判断为NULL？
        if (fields[i].name != NULL)
        {
            m_FieldNames[i] = fields[i].name;
            m_vtFieldNames.push_back(fields[i].name);
        }
        else
        {
            m_FieldNames[i] = "";
            m_vtFieldNames.push_back("");
        }

        m_CurrentRow[i].setType(convertNativeType(fields[i].type));
    }
}

MysqlQueryResult::~MysqlQueryResult()
{
    //这里只释放 MySQL 自己的句柄；行缓冲区由基类析构函数回收。
    //不能改成调用虚函数 endQuery()——基类析构阶段虚表已退化，语义会变。
    freeResult();
}

bool MysqlQueryResult::nextRow()
{
    MYSQL_ROW row;

    if (!m_Result)
        return false;

    row = mysql_fetch_row(m_Result);
    if (!row)
    {
        //已取完最后一行：释放资源，使后续 fetch() 返回 NULL，业务层的遍历循环据此结束
        endQuery();
        return false;
    }

    unsigned long int *ulFieldLength;
    ulFieldLength = mysql_fetch_lengths(m_Result);
    for (uint32_t i = 0; i < m_FieldCount; i++)
    {
        if(row[i] == NULL)
        {
            m_CurrentRow[i].m_bNULL = true;
            m_CurrentRow[i].setValue("", 0);
        }
        else
        {
            m_CurrentRow[i].m_bNULL = false;
            m_CurrentRow[i].setValue(row[i], ulFieldLength[i]);
        }

        m_CurrentRow[i].setName(m_FieldNames[i]);
    }

    return true;
}

void MysqlQueryResult::endQuery()
{
    //释放顺序与拆分前的实现保持一致：先回收行缓冲区，再释放 MYSQL_RES
    QueryResult::endQuery();
    freeResult();
}

void MysqlQueryResult::freeResult()
{
    if (m_Result != NULL)
    {
        mysql_free_result(m_Result);
        m_Result = NULL;
    }
}

enum Field::DataTypes MysqlQueryResult::convertNativeType(enum_field_types mysqlType) const
{
    switch (mysqlType)
    {
        case FIELD_TYPE_TIMESTAMP:
        case FIELD_TYPE_DATE:
        case FIELD_TYPE_TIME:
        case FIELD_TYPE_DATETIME:
        case FIELD_TYPE_YEAR:
        case FIELD_TYPE_STRING:
        case FIELD_TYPE_VAR_STRING:
        case FIELD_TYPE_BLOB:
        case FIELD_TYPE_SET:
        case FIELD_TYPE_NULL:
            return Field::DB_TYPE_STRING;
        case FIELD_TYPE_TINY:

        case FIELD_TYPE_SHORT:
        case FIELD_TYPE_LONG:
        case FIELD_TYPE_INT24:
        case FIELD_TYPE_LONGLONG:
        case FIELD_TYPE_ENUM:
            return Field::DB_TYPE_INTEGER;
        case FIELD_TYPE_DECIMAL:
        case FIELD_TYPE_FLOAT:
        case FIELD_TYPE_DOUBLE:
            return Field::DB_TYPE_FLOAT;
        default:
            return Field::DB_TYPE_UNKNOWN;
    }
}
