/**
 *  Field 类的实现，详见 Field.h。
 *  本文件由 mysqlapi/Field.cpp 迁移而来，代码逻辑与迁移前完全一致。
 *  balloonwj@qq.com   2026-07-31
 */
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
    //字段值可能含有 '\0'（blob 列），必须按长度拷贝而不能当成 C 字符串
    m_strValue.assign(value, uLen);
}
