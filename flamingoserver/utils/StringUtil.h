/**
 * 字符串操作工具类, StringUtil.h
 * zhangyl 2018.03.09
 */
#ifndef __STRING_UTIL_H__
#define __STRING_UTIL_H__
#include <string>
#include <vector>

class StringUtil
{
private:
    StringUtil() = delete;
    ~StringUtil() = delete;
    StringUtil(const StringUtil& rhs) = delete;
    StringUtil& operator=(const StringUtil& rhs) = delete;

public:
    static void split(const std::string& str, std::vector<std::string>& v, const char* delimiter = "|");
    //根据delimiter指定的字符串，将str切割成两半
    static void cut(const std::string& str, std::vector<std::string>& v, const char* delimiter = "|");
    static std::string& replace(std::string& str, const std::string& toReplaced, const std::string& newStr);

    static void trimLeft(std::string& str, char trimmed = ' ');
    static void trimRight(std::string& str, char trimmed = ' ');
    static void trim(std::string& str, char trimmed = ' ');
};


#endif //!__STRING_UTIL_H__