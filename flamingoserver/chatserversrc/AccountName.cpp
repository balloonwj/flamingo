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

std::string MakeRandomAccount(const std::string& prefix, int32_t number)
{
    if (number < RANDOM_ACCOUNT_NUMBER_MIN || number > RANDOM_ACCOUNT_NUMBER_MAX)
    {
        return std::string();
    }
    const std::string head = prefix.empty() ? std::string(EMPLOYEE_ACCOUNT_PREFIX) : prefix;
    char digits[ACCOUNT_NAME_BUF_LEN] = { 0 };
    snprintf(digits, sizeof(digits), "%0*d", RANDOM_ACCOUNT_DIGITS, number);
    return head + digits;
}

bool NormalizeAccountPrefix(const std::string& raw, std::string& out)
{
    //去掉首尾空白（空格、制表符、换行）
    size_t begin = 0;
    size_t end = raw.size();
    while (begin < end && (raw[begin] == ' ' || raw[begin] == '\t' || raw[begin] == '\r' || raw[begin] == '\n'))
        ++begin;
    while (end > begin && (raw[end - 1] == ' ' || raw[end - 1] == '\t' || raw[end - 1] == '\r' || raw[end - 1] == '\n'))
        --end;
    const std::string trimmed = raw.substr(begin, end - begin);

    //空串：清除前缀、改回默认
    if (trimmed.empty())
    {
        out.clear();
        return true;
    }
    if (trimmed.size() > ACCOUNT_PREFIX_MAX_LENGTH)
        return false;

    std::string upper;
    bool hasLetter = false;
    for (size_t i = 0; i < trimmed.size(); ++i)
    {
        const char c = trimmed[i];
        if (c >= 'a' && c <= 'z')
        {
            upper += static_cast<char>(c - 'a' + 'A');
            hasLetter = true;
        }
        else if (c >= 'A' && c <= 'Z')
        {
            upper += c;
            hasLetter = true;
        }
        else if (c == '.')
        {
            upper += c;
        }
        else
        {
            //数字、横线、汉字等其它字符一律不合格
            return false;
        }
    }
    if (!hasLetter)
        return false;
    out = upper;
    return true;
}

bool IsValidPhoneNumber(const std::string& phone)
{
    if (phone.size() != PHONE_NUMBER_LENGTH || phone[0] != '1')
        return false;
    for (size_t i = 0; i < phone.size(); ++i)
    {
        if (phone[i] < '0' || phone[i] > '9')
            return false;
    }
    return true;
}
