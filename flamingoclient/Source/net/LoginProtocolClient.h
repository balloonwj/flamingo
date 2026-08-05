/**
 *  v2 登录报文编解码（客户端侧）：构造 cmd_login 请求 JSON、解析
 *  cmd_login 应答 JSON。
 *
 *  本模块与服务端 chatserversrc/LoginProtocol 互为镜像——服务端解析请求
 *  /构造应答，客户端构造请求/解析应答。本模块是纯逻辑，只依赖 jsoncpp，
 *  不触及网络与 UI，便于单元测试，见
 *  flamingoclient/tests/test_loginprotocol.cpp。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include <stdint.h>
#include <string>

namespace net {

//cmd_login 请求所需的各项参数（由调用方填好后交给 buildLoginRequest）
struct LoginRequestParams
{
    std::string clientversion;   //客户端版本号，如 "3.2.1"，须不低于服务端要求的最低版本
    std::string account;         //登录账号（账号/手机号/邮箱）
    std::string password;        //口令哈希：明文密码 UTF-8 字节做 SHA-256 后的小写 hex
    int         clienttype;      //客户端类型，取值见 enum platform_type，PC 端固定 platform_pc
    int         presence;        //期望的初始在线状态，取值见 enum presence_type
    std::string deviceid;        //设备唯一标识（本机持久化的 GUID）
    std::string devicename;      //设备名称（如计算机名），可为空

    LoginRequestParams()
        : clienttype(0)
        , presence(0)
    {
    }
};

//cmd_login 成功应答中 profile 对象的字段
struct LoginResponseProfile
{
    int         usertype;        //用户类型：0 普通用户 1 服务号，见 enum user_type
    std::string username;        //账号
    std::string nickname;        //昵称
    int         facetype;        //头像类型
    std::string customface;      //自定义头像文件标识
    int         gender;          //性别
    int         birthday;        //生日，形如 19891208
    std::string signature;       //个性签名
    std::string address;         //所在地
    std::string phonenumber;     //电话
    std::string mail;            //邮箱
    int64_t     enterpriseid;    //所属企业 ID，0 表示无企业归属

    LoginResponseProfile()
        : usertype(0)
        , facetype(0)
        , gender(0)
        , birthday(0)
        , enterpriseid(0)
    {
    }
};

//cmd_login 应答报文解析后的结果
struct LoginResponse
{
    int                  code;        //结果码，0 成功，非 0 见 enum error_code
    std::string          msg;         //结果描述（成功为 "ok"，失败为诊断信息）
    int64_t              userid;      //登录用户的 userid，仅 code==0 时有效
    int64_t              servertime;  //服务器时间（Unix 毫秒，UTC），仅 code==0 时有效
    LoginResponseProfile profile;     //用户资料，仅 code==0 时有效

    LoginResponse()
        : code(-1)        //-1：尚未解析出有效结果码，避免被误判为成功
        , userid(0)
        , servertime(0)
    {
    }
};

//cmd_kickout(1006) 推送解析结果：本连接被服务器踢下线。
//服务端产出 {"reason":N,"msg":"..."}（见 chatserversrc/ChatSession.cpp 的顶号 / 停用 / 改密处），
//其中 reason 取值见 enum kickout_reason，msg 是面向开发者的英文诊断串，不直接展示给用户 ——
//界面文案由客户端按 reason 本地化（见 BalloonMain/KickoutNotice.h）。
struct KickoutNotify
{
    int         m_reason;   //被踢原因，取值见 enum kickout_reason；0 表示包体里没有或不是整型
    std::string m_msg;      //服务端给的诊断信息（英文），仅记日志用，不展示给用户

    KickoutNotify()
        : m_reason(0)       //0：未知原因，界面走兜底文案
    {
    }
};

//登录报文编解码工具类；全部为静态方法，不可实例化
class LoginProtocolClient
{
public:
    /**
     *  构造 cmd_login 请求 JSON 包体（紧凑格式，UTF-8）。
     *  @param params 登录请求各项参数；password 必须已是 SHA-256 小写 hex
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildLoginRequest(const LoginRequestParams& params);

    /**
     *  解析 cmd_login 应答 JSON 包体。
     *  无论成功应答还是失败应答，只要 JSON 合法且含整型 code 字段即返回
     *  true，调用方据 resp.code 判定登录结果；JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param resp     [出参] 解析结果；返回 true 时 code/msg 有效，
     *                  code==0 时 userid/servertime/profile 亦有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseLoginResponse(const std::string& jsonbody, LoginResponse& resp);

    /**
     *  解析 cmd_kickout 推送 JSON 包体。
     *  只要 JSON 合法且是对象即返回 true；reason 缺失或类型不对时按 0（未知原因）
     *  填出参，<u>不</u>视为失败 —— "被踢下线"这件事本身不依赖 reason，服务端哪天
     *  改了字段也不能让客户端漏掉下线动作，最多是提示文案走兜底。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param notify   [出参] 解析结果；返回 false 时保持构造后的默认值（reason=0）
     *  @return JSON 合法且是对象返回 true，否则 false
     */
    static bool parseKickoutNotify(const std::string& jsonbody, KickoutNotify& notify);

private:
    LoginProtocolClient() = delete;
    ~LoginProtocolClient() = delete;
    LoginProtocolClient(const LoginProtocolClient&) = delete;
    LoginProtocolClient& operator=(const LoginProtocolClient&) = delete;
};

} // namespace net
