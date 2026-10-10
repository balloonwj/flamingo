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
#include <vector>

//用户资料：cmd_get_user_info 应答与 cmd_user_info_changed_notify 下发的资料对象
//改资料请求里文字字段的长度上限，按 UTF-8 字符数计（中文、英文都算一个），与 t_user 的列宽一致
const int kProfileShortTextMaxChars = 64;    //昵称、岗位、座机
const int kProfileLongTextMaxChars  = 256;   //个性签名、所在地、邮箱

//UserManager.h 里的用户记录；applyProfileUpdate 把改资料请求合并进它
struct User;

struct UserProfile
{
    int64_t     userid;       //用户ID
    int         usertype;     //用户类型：0 普通用户 1 服务号
    std::string username;     //账号
    std::string nickname;     //昵称
    int         facetype;     //头像类型
    std::string customface;   //自定义头像文件标识
    int         gender;       //性别，取值见 enum profile_gender
    int         birthday;     //生日，形如 19891208
    std::string signature;    //个性签名
    std::string address;      //所在地
    std::string phonenumber;  //电话
    std::string mail;         //邮箱
    std::string landline;     //座机，未填为空串
    std::string post;         //岗位：本人填写的自由文本，与部门职位无关；未填为空串
    int64_t     enterpriseid; //所属企业ID，0 表示无企业归属
    int         presence;     //在线状态，取值见 enum presence_type
    int         clienttype;   //在线设备类型，取值见 enum platform_type
    int         phonevisible;    //手机号对同事的公开范围，取值见 enum profile_visibility；只在 hasvisibility 为真时下发
    int         landlinevisible; //座机对同事的公开范围，取值同上；只在 hasvisibility 为真时下发
    bool        hasvisibility;   //资料 JSON 是否带上述两个标志：只有发给本人的那一份为真（见 applyViewer）
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
    bool        has_landline;   //本次是否更新座机
    std::string landline;       //新座机
    bool        has_post;       //本次是否更新岗位
    std::string post;           //新岗位
    bool        has_phonevisible;    //本次是否更新手机号的公开范围
    int         phonevisible;        //新的手机号公开范围，取值见 enum profile_visibility
    bool        has_landlinevisible; //本次是否更新座机的公开范围
    int         landlinevisible;     //新的座机公开范围，取值同上
    //手机号不在请求里：它是注册时唯一、找回密码时按它发验证码的凭据，不允许本人修改，请求带了也忽略
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
     *  出现的字段还要通过校验，否则返回 false（调用方回 error_code_bad_request）：
     *  昵称、岗位、座机不超过 kProfileShortTextMaxChars 个字，签名、所在地、邮箱不超过
     *  kProfileLongTextMaxChars 个字（按 UTF-8 字符数计）；性别只接受 enum profile_gender 的三个值；
     *  生日只接受 0（清空）或合法的 YYYYMMDD 日期；phonevisible / landlinevisible 只接受
     *  enum profile_visibility 的两个值。请求里的 phonenumber 一律忽略。
     *  @param json 请求包体
     *  @param req  [出参] 解析结果
     *  @return JSON 合法且出现的字段都通过校验返回 true
     */
    static bool parseUpdateProfileRequest(const std::string& json, UpdateProfileRequest& req);

    /**
     *  把一次改资料请求里带了的字段合并进本人的用户记录（带了才改，没带的保持原值）。
     *  手机号不在请求里，任何情况下都不会被改动。
     *  @param req 已解析、已通过校验的改资料请求
     *  @param u   [入出] 本人的用户记录；合并后由调用方整体写回数据库
     */
    static void applyProfileUpdate(const UpdateProfileRequest& req, User& u);

    /**
     *  把用户记录里的资料字段（含手机号、座机与两个公开范围标志）原样填进 UserProfile，得到一份
     *  完整资料；hasvisibility 置假，发给谁再由 applyViewer 决定。企业ID、在线状态、设备类型
     *  不在用户记录里，不在此处填写，调用后这三项保持调用前的值。
     *  @param p [入出] 资料
     *  @param u 用户记录（UserManager 缓存）
     */
    static void fillProfileFromUser(UserProfile& p, const User& u);

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
     *  按查看者处理资料里的手机号与座机（docs/联系人资料字段扩充方案.md 第 9 节）。
     *  查看者就是资料本人时号码原样保留；否则公开范围不是「对同事可见」的号码置为空串
     *  （库里出现约定之外的取值时也按不公开处理）。
     *  @param phonevisible    手机号的公开范围，取值见 enum profile_visibility
     *  @param landlinevisible 座机的公开范围，取值同上
     *  @param viewerIsOwner   查看者是否就是资料本人
     *  @param phonenumber     [入出] 手机号；需要隐藏时被置为空串
     *  @param landline        [入出] 座机；需要隐藏时被置为空串
     */
    static void maskNumbersForViewer(int phonevisible, int landlinevisible, bool viewerIsOwner,
                                     std::string& phonenumber, std::string& landline);

    /**
     *  把一份完整资料处理成发给某个查看者的版本：查看者就是本人时号码完整并带上两个公开范围标志
     *  （hasvisibility 置真）；否则按公开范围隐藏号码、不带标志（hasvisibility 置假）。
     *  用于 cmd_get_user_info 应答与 cmd_user_info_changed_notify 推送。
     *  @param profile      [入出] 资料，调用前须是完整资料（号码与两个标志都已填好）
     *  @param viewerUserId 查看者的用户ID
     */
    static void applyViewer(UserProfile& profile, int64_t viewerUserId);

    /**
     *  构造一次资料变更要推送的两份 cmd_user_info_changed_notify：推给本人（含其它在线设备）的
     *  一份号码完整、带公开范围标志；推给同事的一份按公开范围隐藏号码、不带标志。
     *  @param full          本人的完整资料（号码与两个标志都已填好）
     *  @param ownerBody     [出参] 推给本人的包体
     *  @param colleagueBody [出参] 推给同事的包体
     */
    static void buildUserInfoChangedNotifies(const UserProfile& full, std::string& ownerBody,
                                             std::string& colleagueBody);

    /**
     *  构造 cmd_presence_changed_notify 推送 JSON：{ userid, presence, clienttype }。
     */
    static std::string buildPresenceChangedNotify(int64_t userid, int presence, int clienttype);

    /**
     *  构造通用 { code, msg } 应答，供 update_profile/change_* 等使用。
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

    /**
     *  取出一次改资料（cmd_update_profile）里需要做敏感词检查的文字，追加到 out
     *  （docs/敏感词方案.md 第 6.3 节）。只取昵称、签名、所在地、邮箱、岗位五项中本次提交、
     *  与当前值不同、且不为空串的那几项：客户端可能整份资料一起提交，原样带上来的旧值
     *  属于加词之前就有的内容，不检查。取的顺序为昵称、签名、所在地、邮箱、岗位。
     *  座机是号码，不做敏感词检查。
     *  @param req     已解析的改资料请求
     *  @param current 本人当前的资料（只用到上述四项）
     *  @param out     [出参] 取出的文字（UTF-8）追加到末尾；原有内容保留
     */
    static void collectChangedProfileTexts(const UpdateProfileRequest& req, const UserProfile& current,
                                           std::vector<std::string>& out);

private:
    ProfileProtocol() = delete;
    ~ProfileProtocol() = delete;
    ProfileProtocol(const ProfileProtocol&) = delete;
    ProfileProtocol& operator=(const ProfileProtocol&) = delete;
};
