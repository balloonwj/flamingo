#pragma once

#include <algorithm>
#include <string>

using namespace std;

inline void ToLowerString(string& str)
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

        enum DataTypes
        {
            DB_TYPE_UNKNOWN = 0x00,
            DB_TYPE_STRING  = 0x01,
            DB_TYPE_INTEGER = 0x02,
            DB_TYPE_FLOAT   = 0x03,
            DB_TYPE_BOOL    = 0x04
        };

        Field();
        Field(Field &f);
        Field(const char *value, enum DataTypes type);

        ~Field();

        enum DataTypes GetType() const { return mType; }

        const string GetString() const { return  m_strValue; }
        std::string GetCppString() const
        {
            return  m_strValue;                    // std::string s = 0 have undefine result in C++
        }
        float GetFloat() const { return static_cast<float>(atof( m_strValue.c_str())); }
        bool GetBool() const { return  atoi(m_strValue.c_str()) > 0; }
        int32_t GetInt32() const { return  static_cast<int32_t>(atol(m_strValue.c_str())); }
        uint8_t GetUInt8() const { return  static_cast<uint8_t>(atol(m_strValue.c_str())); }
        uint16_t GetUInt16() const { return  static_cast<uint16_t>(atol(m_strValue.c_str())); }
        int16_t GetInt16() const { return  static_cast<int16_t>(atol(m_strValue.c_str())); }
        uint32_t GetUInt32() const { return  static_cast<uint32_t>(atol(m_strValue.c_str())); }
        uint64_t GetUInt64() const
        {
            uint64_t value = 0;
            value = atoll(m_strValue.c_str());
            return value;
        }

        void SetType(enum DataTypes type) { mType = type; }

        void SetValue(const char *value, size_t uLen);
        void SetName(const string& strName)
        {
            m_strFieldName = strName;
            ToLowerString(m_strFieldName);
        }
        const string& GetName()
        {
            return m_strFieldName;
        }

        bool IsNULL()
        {
            return m_bNULL;
        }

        template<typename T>
        void ConvertValue(T& value);

    private:
        string m_strValue;
        string m_strFieldName;
        enum DataTypes mType;

    public:
        bool m_bNULL;
};
