/**
 * 全局唯一的UUID生成工具类，Windows上实际使用的是GUID, UUIDGenerator.cpp
 * zhangyl 20190710
 */

#include "UUIDGenerator.h"


#ifdef WIN32

#include <combaseapi.h>

std::string UUIDGenerator::generate()
{
    GUID guid;
    CoCreateGuid(&guid);
    char cBuffer[64] = { 0 };
    sprintf_s(cBuffer, sizeof(cBuffer),
        "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        guid.Data1, guid.Data2,
        guid.Data3, guid.Data4[0],
        guid.Data4[1], guid.Data4[2],
        guid.Data4[3], guid.Data4[4],
        guid.Data4[5], guid.Data4[6],
        guid.Data4[7]);
    return std::string(cBuffer, 36);
}

#else

#include <uuid.h>

std::string UUIDGenerator::generate()
{
    uuid_t uuid;
    char str[40] = { 0 };

    uuid_generate(uuid);
    uuid_unparse(uuid, str);
    return std::string(str, 36);
}

#endif