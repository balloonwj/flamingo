/**
 *  数据库结果集中「一个字段」的载体：把各种数据库驱动取回的原始字节统一存成字符串，
 *  再由调用方按需转成整型 / 浮点 / 布尔。因为只依赖 std::string，本类与具体数据库无关，
 *  MySQL 与 SQLite 两种后端共用同一份实现。
 *
 *  典型用法：
 *      Field* pRow = pResult->fetch();
 *      int32_t userid = pRow[0].getInt32();
 *      std::string name = pRow[1].getString();
 *
 *  本文件由 mysqlapi/Field.h 迁移而来，内容与迁移前一致（仅补充了文件头与 <cstdlib>）。
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

//本文件下方的取值方法直接使用 uint8_t / uint16_t / uint32_t / uint64_t 等定宽整数类型。
//旧版本 GCC 会经由 <string> 等标准头文件间接引入这些类型，GCC 13 起不再传递，
//必须显式包含 <cstdint>，否则报 "uint8_t does not name a type"。
#include <cstdint>
//下方取值方法用到 atof / atoi / atol / atoll，理由同 <cstdint>：不能指望别的头文件间接带进来。
#include <cstdlib>
#include <algorithm>
#include <string>

inline void toLowerString(std::string& str)
{
    for(size_t i = 0; i < str.size(); i++)
    {
        if(str[i] >= 'A' && str[i] <= 'Z')
        {
            str[i] = str[i] + ('a' - 'A');
        }
    }
}

class Field
{
    public:

        //字段的数据类型。由各数据库驱动把自己的原生列类型映射到这几个值上，
        //调用方一般不需要关心，因为取值方法本身会做字符串到目标类型的转换。
        enum DataTypes
        {
            DB_TYPE_UNKNOWN = 0x00,     //未知类型：驱动无法映射的原生类型落到这里
            DB_TYPE_STRING  = 0x01,     //字符串类：varchar / text / blob / 日期时间等
            DB_TYPE_INTEGER = 0x02,     //整型类：tinyint / int / bigint 等
            DB_TYPE_FLOAT   = 0x03,     //浮点类：float / double / decimal
            DB_TYPE_BOOL    = 0x04      //布尔类：目前两种后端都不会产生，保留
        };

        Field();
        Field(Field &f);
        Field(const char *value, enum DataTypes type);

        ~Field();

        enum DataTypes getType() const { return m_iType; }

        const std::string getString() const { return  m_strValue; }
        std::string getCppString() const
        {
            return  m_strValue;                    // std::string s = 0 have undefine result in C++
        }
        float getFloat() const { return static_cast<float>(atof( m_strValue.c_str())); }
        bool getBool() const { return  atoi(m_strValue.c_str()) > 0; }
        int32_t getInt32() const { return  static_cast<int32_t>(atol(m_strValue.c_str())); }
        uint8_t getUInt8() const { return  static_cast<uint8_t>(atol(m_strValue.c_str())); }
        uint16_t getUInt16() const { return  static_cast<uint16_t>(atol(m_strValue.c_str())); }
        int16_t getInt16() const { return  static_cast<int16_t>(atol(m_strValue.c_str())); }
        uint32_t getUInt32() const { return  static_cast<uint32_t>(atol(m_strValue.c_str())); }
        uint64_t getUInt64() const
        {
            uint64_t value = 0;
            value = atoll(m_strValue.c_str());
            return value;
        }

        void setType(enum DataTypes type) { m_iType = type; }

        void setValue(const char* value, size_t uLen);
        void setName(const std::string& strName)
        {
            m_strFieldName = strName;
            toLowerString(m_strFieldName);
        }
        const std::string& getName()
        {
            return m_strFieldName;
        }

        bool isNULL()
        {
            return m_bNULL;
        }

        template<typename T>
        void convertValue(T& value);

    public:
        bool                 m_bNULL;

    private:
        std::string          m_strValue;
        std::string          m_strFieldName;
        enum DataTypes       m_iType;


};
