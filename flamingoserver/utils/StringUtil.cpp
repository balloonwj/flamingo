/**
 * �ַ�������������, StringUtil.cpp
 * zhangyl 2018.03.09
 */
#include "StringUtil.h"
#include <string.h>

void StringUtil::split(const std::string& str, std::vector<std::string>& v, const char* delimiter/* = "|"*/)
{
    if (delimiter == NULL || str.empty())
        return;

    std::string buf(str);
    size_t pos = std::string::npos;
    std::string substr;
    int delimiterlength = strlen(delimiter);
    while (true)
    {
        pos = buf.find(delimiter);
        if (pos != std::string::npos)
        {
            substr = buf.substr(0, pos);
            if (!substr.empty())
                v.push_back(substr);

            buf = buf.substr(pos + delimiterlength);
        }
        else
        {
            if (!buf.empty())
                v.push_back(buf);
            break;
        }
    }
}

void StringUtil::cut(const std::string& str, std::vector<std::string>& v, const char* delimiter/* = "|"*/)
{
    if (delimiter == NULL || str.empty())
        return;

    std::string buf(str);
    int delimiterlength = strlen(delimiter);
    size_t pos = buf.find(delimiter);
    if (pos == std::string::npos)
        return;

    std::string substr1 = buf.substr(0, pos);
    std::string substr2 = buf.substr(pos + delimiterlength);
    if (!substr1.empty())
        v.push_back(substr1);

    if (!substr2.empty())
        v.push_back(substr2);
}

std::string& StringUtil::replace(std::string& str, const std::string& toReplaced, const std::string& newStr)
{
    if (toReplaced.empty() || newStr.empty())
        return str;

    for (std::string::size_type pos = 0; pos != std::string::npos; pos += newStr.length())
    {
        pos = str.find(toReplaced, pos);
        if (pos != std::string::npos)
            str.replace(pos, toReplaced.length(), newStr);
        else
            break;
    }

    return str;
}

void StringUtil::trimLeft(std::string& str, char trimmed/* = ' '*/)
{
    std::string tmp = str;
    size_t length = tmp.length();
    for (size_t i = 0; i < length; ++i)
    {
        if (tmp[i] != trimmed)
        {
            str = tmp.substr(i);
            break;
        }
    }
}

void StringUtil::trimRight(std::string& str, char trimmed/* = ' '*/)
{
    std::string tmp = str;
    size_t length = tmp.length();
    for (size_t i = length - 1; i >= 0; --i)
    {
        if (tmp[i] != trimmed)
        {
            str = tmp.substr(0, i + 1);
            break;
        }
    }
}

void StringUtil::trim(std::string& str, char trimmed/* = ' '*/)
{
    trimLeft(str, trimmed);
    trimRight(str, trimmed);
}