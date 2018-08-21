#include "Field.h"

Field::Field() :
    mType(DB_TYPE_UNKNOWN)
{
    m_bNULL = false;
}

Field::Field(Field &f)
{
    m_strValue = f.m_strValue;
    m_strFieldName = f.m_strFieldName;

    mType = f.GetType();
}

Field::Field(const char *value, enum Field::DataTypes type) :
    mType(type)
{
    m_strValue = value;
}

Field::~Field()
{
}

void Field::SetValue(const char *value, size_t uLen)
{
    //m_strValue = value;
    m_strValue.assign(value, uLen);
}
