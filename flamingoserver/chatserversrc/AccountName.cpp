/**
 *  企业员工登录账号生成规则的实现，AccountName.cpp
 *  balloonwj@qq.com   2026-08-04
 */
#include "AccountName.h"
#include <stdio.h>

//拼装账号的缓冲区大小。前缀 3 字节 + int64 十进制最多 20 位 + 结尾 0，
//32 字节远够用；上限还受 t_user.f_username 的 varchar(64) 约束。
#define ACCOUNT_NAME_BUF_LEN   32

std::string MakeEmployeeAccount(int64_t userId)
{
    if (userId <= 0)
    {
        return std::string();
    }

    char buf[ACCOUNT_NAME_BUF_LEN] = { 0 };
    snprintf(buf, sizeof(buf), "%s%lld", EMPLOYEE_ACCOUNT_PREFIX, (long long)userId);
    return std::string(buf);
}
