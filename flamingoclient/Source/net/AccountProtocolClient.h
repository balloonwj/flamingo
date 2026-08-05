/**
 *  v2 账号相关报文编解码（客户端侧）：构造 cmd_change_password 请求 JSON、
 *  解析 cmd_change_password 应答 JSON。
 *
 *  本模块与服务端 chatserversrc/ProfileProtocol 互为镜像 —— 服务端解析请求
 *  /构造应答，客户端构造请求/解析应答。纯逻辑，只依赖 jsoncpp，不触及网络
 *  与 UI，便于单元测试，见 flamingoclient/Source/Tests/AccountProtocolTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#pragma once

#include <string>

namespace net {

//cmd_change_password 应答报文解析后的结果
struct ChangePasswordResponse
{
    int         code;   //结果码，0 成功；103 旧密码错误；107 改密失败；其它见 enum error_code
    std::string msg;    //结果描述（成功为 "ok"，失败为诊断信息）

    ChangePasswordResponse()
        : code(-1)      //-1：尚未解析出有效结果码，避免被误判为成功
    {
    }
};

//账号相关报文编解码工具类；全部为静态方法，不可实例化
class AccountProtocolClient
{
public:
    /**
     *  构造 cmd_change_password 请求 JSON 包体（紧凑格式，UTF-8）。
     *  @param oldPwdSha256Hex 旧口令的 SHA-256 小写 hex（调用方须已哈希）
     *  @param newPwdSha256Hex 新口令的 SHA-256 小写 hex（调用方须已哈希）
     *  @return 紧凑格式的请求 JSON 文本：{"oldpassword":"...","newpassword":"..."}
     */
    static std::string buildChangePasswordRequest(const std::string& oldPwdSha256Hex,
                                                  const std::string& newPwdSha256Hex);

    /**
     *  解析 cmd_change_password 应答 JSON 包体。
     *  无论成功应答还是失败应答，只要 JSON 合法且含整型 code 字段即返回 true，
     *  调用方据 resp.code 判定结果；JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param resp     [出参] 解析结果；返回 true 时 code/msg 有效
     *  @return JSON 合法且含整型 code 返回 true，否则 false
     */
    static bool parseChangePasswordResponse(const std::string& jsonbody,
                                            ChangePasswordResponse& resp);

private:
    AccountProtocolClient() = delete;
    ~AccountProtocolClient() = delete;
    AccountProtocolClient(const AccountProtocolClient&) = delete;
    AccountProtocolClient& operator=(const AccountProtocolClient&) = delete;
};

} // namespace net
