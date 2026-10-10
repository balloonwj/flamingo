/**
 *  v2 §7.1 连接与会话相关报文的编解码：cmd_login 的请求解析 / 应答构造 /
 *  客户端版本号比较，以及设备管理 cmd_get_devices / cmd_kick_device /
 *  cmd_device_changed_notify 的报文编解码。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，便于单元测试
 *  （见 tests/test_loginprotocol.cpp）。会话层 ChatSession 调用本模块完成
 *  报文层面的解析与组装，业务判定（账号查询、口令校验、顶号、踢设备）
 *  仍在会话层。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

//cmd_login 请求报文解析后的结构
struct LoginRequest
{
    std::string clientversion;   //客户端版本号，如 "3.2.1"
    int32_t     platform;        //客户端平台，取值见 enum platform_type；缺省时取 clienttype
    std::string account;         //登录账号（账号/手机号/邮箱）
    std::string password;        //口令哈希：客户端对明文做 SHA-256 后的十六进制串
    int32_t     clienttype;      //客户端类型，取值见 enum platform_type
    int32_t     presence;        //期望的初始在线状态，取值见 enum presence_type
    std::string deviceid;        //设备唯一标识
    std::string devicename;      //设备名称，可为空
};

//cmd_login 成功应答中 profile 对象需下发的字段
struct LoginProfile
{
    int32_t     usertype;        //用户类型：0 普通用户 1 服务号
    std::string username;        //账号
    std::string nickname;        //昵称
    int32_t     facetype;        //头像类型
    std::string customface;      //自定义头像文件标识
    int32_t     gender;          //性别
    int32_t     birthday;        //生日，形如 19891208
    std::string signature;       //个性签名
    std::string address;         //所在地
    std::string phonenumber;     //电话
    std::string mail;            //邮箱
    std::string landline;        //座机，未填为空串
    std::string post;            //岗位：本人填写的自由文本，与部门职位无关；未填为空串
    int64_t     enterpriseid;    //所属企业ID，0 表示无企业归属
    int32_t     phonevisible;    //手机号对同事的公开范围，取值见 enum profile_visibility（登录应答只发给本人，总是下发）
    int32_t     landlinevisible; //座机对同事的公开范围，取值同上
};

//cmd_get_devices 应答里的单个在线设备
struct DeviceInfo
{
    int  clienttype;  //客户端类型，取值见 enum platform_type（PC/Android/iOS/Mac）
    int  status;      //该设备的在线状态，取值见 enum presence_type
    bool iscurrent;   //是否为发起本次查询的当前设备

    DeviceInfo()
        : clienttype(0), status(0), iscurrent(false)
    {
    }
};

//登录与连接会话报文编解码工具类；全部为静态方法，不可实例化
class LoginProtocol
{
public:
    /**
     *  解析 cmd_login 请求 JSON 包体。
     *  必填字段：clientversion、account、password、clienttype；缺失或类型
     *  不符（或 account/password 为空串、JSON 本身非法）一律返回 false。
     *  选填字段 platform/presence/deviceid/devicename 缺失时取默认值。
     *  @param jsonbody 请求包体（UTF-8 JSON 文本）
     *  @param req      [出参] 解析结果；仅当返回 true 时各字段有效
     *  @return 解析并校验通过返回 true，否则返回 false
     */
    static bool parseLoginRequest(const std::string& jsonbody, LoginRequest& req);

    /**
     *  构造 cmd_login 成功应答 JSON（code=0）。
     *  @param userid     登录用户的 userid；按 v2 约定以字符串编码进 JSON
     *  @param profile    需下发的用户资料
     *  @param servertime 服务器当前时间，Unix 毫秒时间戳（UTC）
     *  @param features   本人所属企业的功能开关（功能名 → 0 / 1，见 FeatureSwitchManager），写成 features 对象；
     *                    为空时不写这个字段（客户端把缺这个字段视同全部关闭）
     *  @return 紧凑格式的应答 JSON 文本
     */
    static std::string buildLoginSuccessResponse(int64_t userid,
                                                 const LoginProfile& profile,
                                                 int64_t servertime,
                                                 const std::map<std::string, int>& features = std::map<std::string, int>());

    /**
     *  构造 cmd_login 失败应答 JSON。
     *  @param code 错误码（非 0），取值见 enum error_code
     *  @param msg  面向开发者的诊断信息
     *  @return 紧凑格式的应答 JSON 文本
     */
    static std::string buildLoginFailResponse(int32_t code, const std::string& msg);

    /**
     *  比较点分版本号：ver 是否不低于 minver。
     *  逐段按数值比较，段数不足的一方按 0 补齐（"3.2" 等价于 "3.2.0"）。
     *  @param ver    待判定的版本号，如 "3.2.1"
     *  @param minver 服务器要求的最低版本号，如 "3.0.0"
     *  @return ver >= minver 返回 true
     */
    static bool isVersionAtLeast(const std::string& ver, const std::string& minver);

    /**
     *  解析 cmd_kick_device 请求（请求体为 { "clienttype": <int> }）。
     *  @param clienttype [出参] 待踢设备的客户端类型
     *  @return clienttype 字段存在且为正返回 true
     */
    static bool parseKickDeviceRequest(const std::string& json, int& clienttype);

    /**
     *  构造 cmd_get_devices 应答 JSON：{ code, msg, devices:[ {clienttype,status,iscurrent} ] }。
     */
    static std::string buildGetDevicesResponse(int code, const std::string& msg,
                                               const std::vector<DeviceInfo>& devices);

    /**
     *  构造 cmd_device_changed_notify 推送 JSON：{ changetype, clienttype }。
     *  @param changetype 变更类型：1 设备上线 2 设备下线
     *  @param clienttype 发生上 / 下线的设备客户端类型
     */
    static std::string buildDeviceChangedNotify(int changetype, int clienttype);

    /**
     *  构造通用 { code, msg } 应答（cmd_kick_device 等用）。
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

private:
    LoginProtocol() = delete;
    ~LoginProtocol() = delete;
    LoginProtocol(const LoginProtocol&) = delete;
    LoginProtocol& operator=(const LoginProtocol&) = delete;
};
