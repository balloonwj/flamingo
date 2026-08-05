/**
 *  SHA-256 哈希工具：对任意字节序列求 SHA-256，输出小写十六进制字符串。
 *
 *  v2 登录协议要求客户端把明文密码的 UTF-8 字节做 SHA-256、以小写 hex
 *  形式放入 cmd_login 请求的 password 字段（服务端再 MD5 加盐入库）。
 *  本类即为此服务，底层用 Windows CNG（bcrypt），不引入第三方加密库。
 *
 *  本模块无业务依赖，可独立单元测试，见
 *  flamingoclient/tests/test_sha256.cpp。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include <stddef.h>
#include <string>

namespace net {

//SHA-256 哈希工具类；全部为静态方法，不可实例化
class Sha256
{
public:
    /**
     *  对一段字节序列求 SHA-256，返回 64 个字符的小写十六进制摘要。
     *  @param data 待哈希数据起始地址；len 为 0 时可为 nullptr
     *  @param len  待哈希字节数
     *  @return 成功返回 64 字符小写 hex 摘要；底层 CNG 调用失败返回空串
     */
    static std::string hexDigest(const void* data, size_t len);

    /**
     *  对一个 std::string 的全部字节求 SHA-256（不含结尾 '\0'）。
     *  调用方需自行保证传入的是 UTF-8 字节（v2 口令哈希要求 UTF-8）。
     *  @param data 待哈希字符串
     *  @return 64 字符小写 hex 摘要；失败返回空串
     */
    static std::string hexDigest(const std::string& data);

private:
    Sha256() = delete;
    ~Sha256() = delete;
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
};

} // namespace net
