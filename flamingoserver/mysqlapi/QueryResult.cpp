#include "QueryResult.h"
#include "../base/AsyncLog.h"

QueryResult::QueryResult(MYSQL_RES *result, uint64_t rowCount, uint32_t fieldCount)
 : mFieldCount(fieldCount), mRowCount(rowCount)
{
    mResult = result;
	mCurrentRow = new Field[mFieldCount];
    //assert(mCurrentRow);

    MYSQL_FIELD *fields = mysql_fetch_fields(mResult);

    for (uint32_t i = 0; i < mFieldCount; i++)
    {
        //TODO: 这个地方要不要判断为NULL？
		if (fields[i].name != NULL)
        {
            mFieldNames[i] = fields[i].name;
            m_vtFieldNames.push_back(fields[i].name);
        }
        else
        {
            mFieldNames[i] = "";
            m_vtFieldNames.push_back("");
        }
        
        mCurrentRow[i].SetType(ConvertNativeType(fields[i].type));
    }
}

QueryResult::~QueryResult(void)
{
    EndQuery();
}

bool QueryResult::NextRow()
{
    MYSQL_ROW row;

    if (!mResult)
        return false;

    row = mysql_fetch_row(mResult);
    if (!row)
    {
        EndQuery();
        return false;
    }

    unsigned long int *ulFieldLength;
    ulFieldLength = mysql_fetch_lengths(mResult);
    for (uint32_t i = 0; i < mFieldCount; i++)
    {
        if(row[i] == NULL)
        {
            mCurrentRow[i].m_bNULL = true;
            mCurrentRow[i].SetValue("", 0);
        }
        else
        {
            mCurrentRow[i].m_bNULL = false;
           mCurrentRow[i].SetValue(row[i], ulFieldLength[i]);
        }

        mCurrentRow[i].SetName(mFieldNames[i]);
    }

    return true;
}

void QueryResult::EndQuery()
{
    if (mCurrentRow)
    {
        delete [] mCurrentRow;
        mCurrentRow = 0;
    }

    if (mResult)
    {
		//LOGI << "QueryResult::EndQuery, mysql_free_result";
        mysql_free_result(mResult);
        mResult = 0;
    }
}

enum Field::DataTypes QueryResult::ConvertNativeType(enum_field_types mysqlType) const
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
