/**
 *  短信验证码管理器实现，SmsCodeManager.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "SmsCodeManager.h"
#include <ctime>
#include <random>

//验证码有效期，单位：秒
constexpr int64_t SMS_CODE_TTL_SECONDS = 5 * 60;
//验证码位数
constexpr int SMS_CODE_DIGITS = 6;
//十进制单数字最大值
constexpr int DECIMAL_DIGIT_MAX = 9;

std::string SmsCodeManager::generateCode(const std::string& phone)
{
    //生成 SMS_CODE_DIGITS 位纯数字验证码；生成器按线程各持一份并跨调用保持推进
    static thread_local std::mt19937 gen(static_cast<uint32_t>(
        std::random_device{}() ^ static_cast<uint32_t>(time(nullptr))));
    std::uniform_int_distribution<int> dist(0, DECIMAL_DIGIT_MAX);

    std::string code;
    code.reserve(SMS_CODE_DIGITS);
    for (int i = 0; i < SMS_CODE_DIGITS; ++i)
        code.push_back(static_cast<char>('0' + dist(gen)));

    CodeEntry entry;
    entry.code = code;
    entry.expiretime = static_cast<int64_t>(time(nullptr)) + SMS_CODE_TTL_SECONDS;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_codes[phone] = entry;
    }

    //短信实际下发为桩实现：本类不负责发送，由调用方（会话层）记录验证码日志，
    //生产环境应改为在调用方处接入短信网关
    return code;
}

bool SmsCodeManager::verifyCode(const std::string& phone, const std::string& code)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    auto it = m_codes.find(phone);
    if (it == m_codes.end())
        return false;

    //已过期：清除记录并判失败
    if (static_cast<int64_t>(time(nullptr)) > it->second.expiretime)
    {
        m_codes.erase(it);
        return false;
    }

    //验证码不匹配
    if (it->second.code != code)
        return false;

    //校验通过：一次性消费，清除记录
    m_codes.erase(it);
    return true;
}
