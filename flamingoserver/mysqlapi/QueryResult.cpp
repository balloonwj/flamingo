#include "QueryResult.h"
#include "../base/AsyncLog.h"

QueryResult::QueryResult(MYSQL_RES *result, uint64_t rowCount, uint32_t fieldCount)
 : m_FieldCount(fieldCount), m_RowCount(rowCount)
{
    m_Result = result;
	m_CurrentRow = new Field[m_FieldCount];
    //assert(mCurrentRow);

    MYSQL_FIELD *fields = mysql_fetch_fields(m_Result);

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

QueryResult::~QueryResult(void)
{
    endQuery();
}

bool QueryResult::nextRow()
{
    MYSQL_ROW row;

    if (!m_Result)
        return false;

    row = mysql_fetch_row(m_Result);
    if (!row)
    {
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

void QueryResult::endQuery()
{
    if (m_CurrentRow)
    {
        delete [] m_CurrentRow;
        m_CurrentRow = 0;
    }

    if (m_Result)
    {
		//LOGI << "QueryResult::EndQuery, mysql_free_result";
        mysql_free_result(m_Result);
        m_Result = 0;
    }
}

enum Field::DataTypes QueryResult::convertNativeType(enum_field_types mysqlType) const
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
