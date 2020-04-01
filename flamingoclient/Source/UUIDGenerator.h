/** 
 * 全局唯一的UUID生成工具类，Windows上实际使用的是GUID, UUIDGenerator.h
 * zhangyl 20190710
 */

#ifndef __UUID_GENERATOR_H__
#define __UUID_GENERATOR_H__

#include <string>

class UUIDGenerator final
{
private:
    UUIDGenerator() = delete;
    ~UUIDGenerator() = delete;

    UUIDGenerator(const UUIDGenerator& rhs) = delete;
    UUIDGenerator& operator =(const UUIDGenerator& rhs) = delete;

public:
    static std::string generate();

};

#endif //!__UUID_GENERATOR_H__

