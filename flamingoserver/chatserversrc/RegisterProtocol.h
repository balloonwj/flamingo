/**
 *  v2 注册与找回密码相关报文（cmd_register / cmd_send_sms_code /
 *  cmd_reset_password）的编解码。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，便于单元测试
 *  （见 tests/test_registerprotocol.cpp）。短信验证码的存储与校验由
 *  SmsCodeManager 负责，账号落库由 UserManager 负责，均在会话层调用。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>
#include <string>

//cmd_register 请求。
//注意这里**没有** username：登录账号不由用户起名，一律由服务端按用户ID自动
//生成（见 AccountName.h），生成结果随应答回传给客户端。
struct RegisterRequest
{
    std::string nickname;  //昵称
    std::string password;  //口令哈希（客户端 SHA-256，十六进制串）
    std::string phone;     //手机号
    std::string company;   //公司全称，须与 t_enterprise.f_name 完全一致；服务端据此定位企业并把新账号挂进其公司根部门
};

//cmd_reset_password 请求
struct ResetPasswordRequest
{
    std::string phone;        //账号绑定的手机号
    std::string smscode;      //收到的短信验证码
    std::string newpassword;  //新口令哈希（客户端 SHA-256，十六进制串）
};

//注册 / 找回密码报文编解码工具类；全部为静态方法，不可实例化
class RegisterProtocol
{
public:
    /**
     *  解析 cmd_send_sms_code 请求。
     *  @param json  请求包体（UTF-8 JSON 文本）
     *  @param phone [出参] 目标手机号
     *  @param scene [出参] 场景：1 注册 2 找回密码；缺失时为 0
     *  @return JSON 合法且 phone 非空返回 true
     */
    static bool parseSendSmsCodeRequest(const std::string& json, std::string& phone, int& scene);

    /**
     *  解析 cmd_register 请求；nickname/password/phone/company 任一缺失或为空
     *  返回 false。
     *
     *  本函数**不看** username 与 smscode 两个字段：
     *    · 登录账号由服务端自动生成，客户端就算发上来也不作数；
     *    · 注册流程已不再收短信验证码（它只服务于找回密码，见
     *      parseResetPasswordRequest 与 SmsCodeManager）。
     *  这两个字段带在报文里（老客户端）不影响解析，不带也不影响。
     *
     *  company 会被去掉首尾空白后再判空并写入出参：用户手打公司名很容易多敲空格，
     *  而后续是按它做精确匹配的，不归一化会匹配不上。其余字段保持原样不做处理。
     */
    static bool parseRegisterRequest(const std::string& json, RegisterRequest& req);

    /**
     *  解析 cmd_reset_password 请求；phone/smscode/newpassword 任一缺失或为空返回 false。
     */
    static bool parseResetPasswordRequest(const std::string& json, ResetPasswordRequest& req);

    /**
     *  构造 cmd_register 成功应答 JSON：
     *  { code, msg, userid, username, enterpriseid, enterprisename }。
     *  @param userid         服务器分配的新用户ID；按 v2 约定以字符串编码
     *  @param username       服务器最终采用的登录账号
     *  @param enterpriseId   新账号所属企业的业务ID；同样以字符串编码
     *  @param enterpriseName 该企业的全称，供客户端在注册完成页回显"已加入 XX 公司"，
     *                        用库里的名字而不是用户输入的原文，让大小写等差异当场可见
     */
    static std::string buildRegisterResponse(int code, const std::string& msg,
                                             int64_t userid, const std::string& username,
                                             int64_t enterpriseId, const std::string& enterpriseName);

    /**
     *  构造通用 { code, msg } 应答。
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

private:
    RegisterProtocol() = delete;
    ~RegisterProtocol() = delete;
    RegisterProtocol(const RegisterProtocol&) = delete;
    RegisterProtocol& operator=(const RegisterProtocol&) = delete;
};
