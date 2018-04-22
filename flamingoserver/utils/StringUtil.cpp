/**
 * 字符串操作工具类, StringUtil.cpp
 * zhangyl 2018.03.09
 */
#include "StringUtil.h"

void StringUtil::Split(const std::string& str, std::vector<std::string>& v, const char* delimiter/* = "|"*/)
{
    if (delimiter == NULL || delimiter[0] == '\0' || str.empty())
        return;

    std::string buf = str;
    size_t pos = std::string::npos;
    std::string substr;
    while (true)
    {
        pos = buf.find(delimiter);
        if (pos != std::string::npos)
        {
            substr = buf.substr(0, pos);
            if (!substr.empty())
                v.push_back(substr);

            buf = buf.substr(pos + 1);
        }
        else
        {
            v.push_back(buf);
            break;
        }           
    }
}