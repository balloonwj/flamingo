/**
 *  SHA-256 摘要算法（FIPS 180-4），返回十六进制小写串。
 *
 *  引入它的直接动因：v2 的口令存储格式是 MD5(SHA256(明文) + salt)，其中第一层
 *  SHA-256 原本约定由客户端计算、服务端不接触明文（见 utils/PasswordHasher.h）。
 *  但管理端新建员工与重置员工口令这两个场景里，明文初始口令是<u>服务端自己生成</u>
 *  的，客户端根本不参与，服务端必须把两层都算完才能得到可供登录比对的存储哈希。
 *
 *  典型用法：
 *      std::string plain  = "a33a3b3903";                        //服务端生成的初始口令
 *      std::string salt   = PasswordHasher::generateSalt();
 *      std::string stored = PasswordHasher::hashPassword(Sha256::hexDigest(plain), salt);
 *      //stored 写入 t_user.f_password，salt 写入 t_user.f_salt
 *
 *  balloonwj@qq.com   2026-08-11
 */
#pragma once

#include <string>
#include <stddef.h>

//SHA-256 摘要工具类；全部为静态方法，不可实例化
class Sha256
{
public:
    /**
     *  计算一段内存的 SHA-256 摘要。
     *  @param data   待摘要数据的首地址；所有权仍属调用方，本函数只读不改。
     *                length 为 0 时允许传空指针。
     *  @param length 待摘要数据的字节数，可以为 0（空输入有确定的摘要值
     *                e3b0c442...b855，不是错误）。
     *  @return 长度固定 64 的十六进制小写串。
     */
    static std::string hexDigest(const void* data, size_t length);

    /**
     *  计算字符串的 SHA-256 摘要。
     *  @param input 待摘要的字符串，按其<u>原始字节</u>参与运算，函数不做任何编码
     *               转换。口令场景下调用方须保证传入的是 UTF-8 字节，否则与客户端
     *               算出的结果对不上。
     *  @return 长度固定 64 的十六进制小写串。
     */
    static std::string hexDigest(const std::string& input);

private:
    Sha256() = delete;
    ~Sha256() = delete;
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
};
