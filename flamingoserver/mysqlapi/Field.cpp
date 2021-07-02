#include "Field.h"

Field::Field() : m_iType(DB_TYPE_UNKNOWN)
{
    m_bNULL = false;
}

Field::Field(Field &f)
{
    m_strValue = f.m_strValue;
    m_strFieldName = f.m_strFieldName;

    m_iType = f.getType();
}

Field::Field(const char *value, enum Field::DataTypes type) : m_iType(type)
{
    m_strValue = value;
}

Field::~Field()
{
}

void Field::setValue(const char *value, size_t uLen)
{
    //m_strValue = value;
    m_strValue.assign(value, uLen);
}
