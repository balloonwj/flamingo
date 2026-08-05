/**
 *  v2 账号资料相关报文（cmd_get_user_info / update_profile / change_password /
 *  change_avatar / change_presence 及两个变更通知）的编解码。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，便于单元测试
 *  （见 tests/test_profileprotocol.cpp）。会话层 ChatSession 的对应处理器
 *  调用本模块完成报文解析与组装，账号查询/落库/推送仍在会话层。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>
#include <string>

//用户资料：cmd_get_user_info 应答与 cmd_user_info_changed_notify 下发的资料对象
struct UserProfile
{
    int64_t     userid;       //用户ID
    int         usertype;     //用户类型：0 普通用户 1 服务号
    std::string username;     //账号
    std::string nickname;     //昵称
    int         facetype;     //头像类型
    std::string customface;   //自定义头像文件标识
    int         gender;       //性别
    int         birthday;     //生日，形如 19891208
    std::string signature;    //个性签名
    std::string address;      //所在地
    std::string phonenumber;  //电话
    std::string mail;         //邮箱
    int64_t     enterpriseid; //所属企业ID，0 表示无企业归属
    int         presence;     //在线状态，取值见 enum presence_type
    int         clienttype;   //在线设备类型，取值见 enum platform_type
};

//cmd_update_profile 请求：更新本人资料；字段全可选，仅 has_* 为真的字段参与更新
struct UpdateProfileRequest
{
    bool        has_nickname;   //本次是否更新昵称
    std::string nickname;       //新昵称
    bool        has_signature;  //本次是否更新个性签名
    std::string signature;      //新个性签名
    bool        has_gender;     //本次是否更新性别
    int         gender;         //新性别
    bool        has_birthday;   //本次是否更新生日
    int         birthday;       //新生日，形如 19891208
    bool        has_address;    //本次是否更新所在地
    std::string address;        //新所在地
    bool        has_mail;       //本次是否更新邮箱
    std::string mail;           //新邮箱
};

//cmd_change_password 请求
struct ChangePasswordRequest
{
    std::string oldpassword;  //旧口令哈希（客户端 SHA-256，十六进制串）
    std::string newpassword;  //新口令哈希（客户端 SHA-256，十六进制串）
};

//cmd_change_avatar 请求
struct ChangeAvatarRequest
{
    int         facetype;    //头像类型
    std::string customface;  //自定义头像文件标识
};

//cmd_change_presence 请求
struct ChangePresenceRequest
{
    int presence;  //目标在线状态，取值见 enum presence_type
};

//账号资料报文编解码工具类；全部为静态方法，不可实例化
class ProfileProtocol
{
public:
    /**
     *  解析 cmd_get_user_info 请求，取出目标用户ID。
     *  @param json   请求包体（UTF-8 JSON 文本）
     *  @param userid [出参] 目标用户ID
     *  @return JSON 合法且含字符串 userid 返回 true
     */
    static bool parseGetUserInfoRequest(const std::string& json, int64_t& userid);

    /**
     *  解析 cmd_update_profile 请求。JSON 非法或非对象返回 false；合法时
     *  按字段是否出现填充 req 的 has_* 标志（缺失或类型不符的字段视为不更新）。
     *  @param json 请求包体
     *  @param req  [出参] 解析结果
     *  @return JSON 合法返回 true
     */
    static bool parseUpdateProfileRequest(const std::string& json, UpdateProfileRequest& req);

    /**
     *  解析 cmd_change_password 请求；oldpassword/newpassword 缺失或为空返回 false。
     */
    static bool parseChangePasswordRequest(const std::string& json, ChangePasswordRequest& req);

    /**
     *  解析 cmd_change_avatar 请求；facetype 字段缺失返回 false。
     */
    static bool parseChangeAvatarRequest(const std::string& json, ChangeAvatarRequest& req);

    /**
     *  解析 cmd_change_presence 请求；presence 字段缺失返回 false。
     */
    static bool parseChangePresenceRequest(const std::string& json, ChangePresenceRequest& req);

    /**
     *  构造 cmd_get_user_info 成功应答 JSON：{ code, msg, profile }。
     */
    static std::string buildGetUserInfoResponse(int code, const std::string& msg,
                                                const UserProfile& profile);

    /**
     *  构造 cmd_user_info_changed_notify 推送 JSON：{ userid, profile }。
     */
    static std::string buildUserInfoChangedNotify(const UserProfile& profile);

    /**
     *  构造 cmd_presence_changed_notify 推送 JSON：{ userid, presence, clienttype }。
     */
    static std::string buildPresenceChangedNotify(int64_t userid, int presence, int clienttype);

    /**
     *  构造通用 { code, msg } 应答，供 update_profile/change_* 等使用。
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

private:
    ProfileProtocol() = delete;
    ~ProfileProtocol() = delete;
    ProfileProtocol(const ProfileProtocol&) = delete;
    ProfileProtocol& operator=(const ProfileProtocol&) = delete;
};
