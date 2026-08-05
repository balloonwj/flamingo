/**
 *  v2 注册报文编解码（客户端侧）：构造 cmd_register 请求 JSON、解析
 *  cmd_register 应答 JSON。
 *
 *  本模块与服务端 chatserversrc/RegisterProtocol 互为镜像——服务端解析请求
 *  /构造应答，客户端构造请求/解析应答。本模块是纯逻辑，只依赖 jsoncpp，
 *  不触及网络与 UI，便于单元测试。
 *
 *  企业版的注册语义是"加入一家已存在的企业"：用户填的公司全称须能在服务端
 *  精确匹配到唯一一家企业，注册成功后账号被自动挂进该企业的公司根部门。
 *  注册不收短信验证码（短信验证码只用于找回密码）。
 *
 *  balloonwj@qq.com   2026-08-04
 */
#pragma once

#include <stdint.h>
#include <string>

namespace net {

//cmd_register 请求所需的各项参数（由调用方填好后交给 buildRegisterRequest）。
//注意这里**没有** username：登录账号不由用户起名，一律由服务端按用户ID自动
//生成，生成结果在 RegisterResponse::username 里回传。
struct RegisterRequestParams
{
    std::string nickname;   //昵称，UTF-8
    std::string password;   //口令哈希：明文密码 UTF-8 字节做 SHA-256 后的小写 hex
    std::string phone;      //手机号，11 位；找回密码依赖它，不可为空
    std::string company;    //公司全称，UTF-8；须与服务端 t_enterprise.f_name 完全一致
};

//cmd_register 应答报文解析后的结果
struct RegisterResponse
{
    int         code;            //结果码，0 成功，非 0 见 enum error_code
    std::string msg;             //结果描述（英文诊断串，仅记日志，不直接展示给用户）
    int64_t     userid;          //新账号的 userid，仅 code==0 时有效
    std::string username;        //服务端自动生成的登录账号（形如 "EMP1024"），仅 code==0 时有效；须展示给用户并提醒记住
    int64_t     enterpriseid;    //所加入企业的业务ID，仅 code==0 时有效
    std::string enterprisename;  //所加入企业的全称（库中原文，非用户输入的原文），仅 code==0 时有效

    RegisterResponse()
        : code(-1)        //-1：尚未解析出有效结果码，避免被误判为成功
        , userid(0)
        , enterpriseid(0)
    {
    }
};

//注册报文编解码工具类；全部为静态方法，不可实例化
class RegisterProtocolClient
{
public:
    /**
     *  构造 cmd_register 请求 JSON 包体（紧凑格式，UTF-8）。
     *  @param params 注册请求各项参数；password 必须已是 SHA-256 小写 hex，
     *                各字符串均须是 UTF-8 字节序列
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildRegisterRequest(const RegisterRequestParams& params);

    /**
     *  解析 cmd_register 应答 JSON 包体。
     *  无论成功应答还是失败应答，只要 JSON 合法且含整型 code 字段即返回
     *  true，调用方据 resp.code 判定注册结果；JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param resp     [出参] 解析结果；返回 true 时 code/msg 有效，
     *                  code==0 时 userid/username/enterpriseid/enterprisename 亦有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseRegisterResponse(const std::string& jsonbody, RegisterResponse& resp);

private:
    RegisterProtocolClient() = delete;
    ~RegisterProtocolClient() = delete;
    RegisterProtocolClient(const RegisterProtocolClient&) = delete;
    RegisterProtocolClient& operator=(const RegisterProtocolClient&) = delete;
};

} // namespace net
