/**
 *  短信验证码管理器，SmsCodeManager.h
 *
 *  管理注册 / 找回密码用的手机验证码：按手机号在内存中存储验证码与过期
 *  时间，校验通过即一次性消费。验证码不落库。
 *  注意：实际短信下发为**桩实现**——本类只生成与存储验证码，由调用方
 *  （会话层）把验证码写入日志；生产环境应改为在调用方处接入短信网关。
 *  服务器级单例，经 Singleton<SmsCodeManager> 使用。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>
#include <string>
#include <map>
#include <mutex>

//短信验证码管理器（服务器级单例）
class SmsCodeManager
{
public:
    SmsCodeManager() = default;
    ~SmsCodeManager() = default;

    SmsCodeManager(const SmsCodeManager& rhs) = delete;
    SmsCodeManager& operator=(const SmsCodeManager& rhs) = delete;

    /**
     *  为指定手机号生成一个新的 6 位数字验证码并存入内存（覆盖该手机号旧的）。
     *  @param phone 目标手机号
     *  @return 新生成的 6 位数字验证码
     */
    std::string generateCode(const std::string& phone);

    /**
     *  校验某手机号的验证码是否正确且未过期；校验通过即消费（清除）该验证码。
     *  @param phone 手机号
     *  @param code  待校验的验证码
     *  @return 正确且未过期返回 true（并清除该记录）；否则返回 false
     */
    bool verifyCode(const std::string& phone, const std::string& code);

private:
    //一条验证码记录
    struct CodeEntry
    {
        std::string code;        //6 位数字验证码
        int64_t     expiretime;  //过期时间，Unix 秒时间戳
    };

    std::map<std::string, CodeEntry> m_codes;  //手机号 → 验证码记录
    std::mutex                       m_mutex;  //保护 m_codes，允许多线程并发访问
};
