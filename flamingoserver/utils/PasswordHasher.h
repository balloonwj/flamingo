/**
 *  v2 登录口令的哈希与校验工具。
 *
 *  口令存储格式：  storedHash = MD5( clienthash + salt )
 *  其中 clienthash = SHA256(明文密码的 UTF-8 字节)，由客户端计算后上送，
 *  服务端全程不接触明文密码。salt 为每个账号独立的随机盐，存于
 *  数据库 t_user.f_salt；storedHash 存于 t_user.f_password。
 *
 *  注：服务端只做第二层 MD5，不计算 SHA-256；SHA-256 由客户端负责。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <string>

//口令哈希工具类；全部为静态方法，不可实例化
class PasswordHasher
{
public:
    /**
     *  由客户端上送的口令哈希与账号盐值，计算用于入库/比对的存储哈希。
     *  @param clienthash 客户端上送的 SHA-256 口令哈希（十六进制小写串）
     *  @param salt       账号盐值（t_user.f_salt 的值）
     *  @return MD5(clienthash + salt) 的十六进制小写串，长度固定 32
     */
    static std::string hashPassword(const std::string& clienthash,
                                    const std::string& salt);

    /**
     *  校验客户端本次上送的口令哈希是否与库中存储哈希一致。
     *  采用定长常量时间比较，避免按字节提前返回引入计时侧信道。
     *  @param clienthash 客户端本次登录上送的 SHA-256 口令哈希
     *  @param storedHash 数据库 t_user.f_password 中存储的哈希；为空一律视为校验失败
     *  @param salt       账号盐值 t_user.f_salt
     *  @return 一致返回 true，否则返回 false
     */
    static bool verifyPassword(const std::string& clienthash,
                               const std::string& storedHash,
                               const std::string& salt);

    /**
     *  生成一个随机盐值，供注册新账号时写入 t_user.f_salt。
     *  @return 长度 24 的十六进制小写随机串
     */
    static std::string generateSalt();

private:
    PasswordHasher() = delete;
    ~PasswordHasher() = delete;
    PasswordHasher(const PasswordHasher&) = delete;
    PasswordHasher& operator=(const PasswordHasher&) = delete;
};
