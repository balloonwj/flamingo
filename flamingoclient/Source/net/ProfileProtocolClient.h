/**
 *  v2 账号资料报文编解码（客户端侧）：构造 cmd_update_profile 请求 JSON、解析
 *  其通用 {code,msg} 应答。改签名有单独的 buildUpdateSignatureRequest（主面板点签名
 *  原地编辑、回车提交）；改其它资料先用 diffProfileValues 比较编辑前后的值，
 *  再用 buildUpdateProfileRequest 只把改动过的字段放进请求（2026-10-04 起）。
 *
 *  与服务端 chatserversrc/ProfileProtocol 互为镜像：服务端 parseUpdateProfileRequest
 *  解析请求、buildSimpleResponse 组装 {code,msg} 应答；客户端反过来。纯逻辑、只依赖
 *  jsoncpp，不触网络与 UI，便于单元测试，见 Tests/ProfileProtocolClientTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#pragma once

#include <string>

#include "Protocol.h"   // enum profile_visibility（两个公开范围的默认值）

namespace net {

//本人资料里本人可以修改的八项，外加手机号、座机对同事的两个公开范围（手机号本身不在其中：
//注册时唯一、找回密码按它发验证码，不允许本人修改）。用来比较编辑前后的值，算出改资料请求要带哪些字段
struct ProfileEditableValues
{
    std::string m_nickname;   //昵称（UTF-8）
    std::string m_signature;  //个性签名（UTF-8）
    int         m_gender;     //性别，取值见 Protocol.h 的 enum profile_gender：0 未设置 / 1 男 / 2 女
    int         m_birthday;   //生日，YYYYMMDD 形式的整数；0 表示未填
    std::string m_address;    //所在地（UTF-8）
    std::string m_mail;       //邮箱（UTF-8）
    std::string m_landline;   //座机（UTF-8）
    std::string m_post;       //岗位（UTF-8）：本人填写的自由文本，与部门职位无关
    int         m_phoneVisible;    //手机号对同事的公开范围，取值见 Protocol.h 的 enum profile_visibility；默认不公开
    int         m_landlineVisible; //座机对同事的公开范围，取值同上；默认公开

    ProfileEditableValues()
        : m_gender(0)
        , m_birthday(0)
        , m_phoneVisible(profile_visibility_self_only)
        , m_landlineVisible(profile_visibility_colleagues)
    {
    }
};

//一次改资料请求要提交的内容：m_values 里哪几项进请求由对应的 m_hasXxx 决定，
//没标记的项不出现在请求里，服务端保持原值
struct ProfileUpdateFields
{
    ProfileEditableValues m_values;  //各项的新值；只有 m_hasXxx 为 true 的项有意义
    bool m_hasNickname;              //是否提交昵称
    bool m_hasSignature;             //是否提交个性签名
    bool m_hasGender;                //是否提交性别
    bool m_hasBirthday;              //是否提交生日
    bool m_hasAddress;               //是否提交所在地
    bool m_hasMail;                  //是否提交邮箱
    bool m_hasLandline;              //是否提交座机
    bool m_hasPost;                  //是否提交岗位
    bool m_hasPhoneVisible;          //是否提交手机号的公开范围
    bool m_hasLandlineVisible;       //是否提交座机的公开范围

    ProfileUpdateFields()
        : m_hasNickname(false)
        , m_hasSignature(false)
        , m_hasGender(false)
        , m_hasBirthday(false)
        , m_hasAddress(false)
        , m_hasMail(false)
        , m_hasLandline(false)
        , m_hasPost(false)
        , m_hasPhoneVisible(false)
        , m_hasLandlineVisible(false)
    {
    }

    /**
     *  是否一项都不提交（编辑前后的值完全相同）。
     *  @return 没有任何 m_hasXxx 为 true 时返回 true；调用方据此决定不发请求
     */
    bool IsEmpty() const
    {
        return !m_hasNickname && !m_hasSignature && !m_hasGender && !m_hasBirthday
            && !m_hasAddress && !m_hasMail && !m_hasLandline && !m_hasPost
            && !m_hasPhoneVisible && !m_hasLandlineVisible;
    }
};

//cmd_update_profile 应答报文解析后的结果（服务端 buildSimpleResponse 产出 {code,msg}）
struct UpdateProfileResponse
{
    int         code;   //结果码，0 成功；106 更新资料失败；其它见服务端 enum error_code
    std::string msg;    //结果描述（成功为 "ok"，失败为诊断信息）

    UpdateProfileResponse()
        : code(-1)      //-1：尚未解析出有效结果码，避免被误判为成功
    {
    }
};

//cmd_change_presence 应答报文解析后的结果（服务端同样用 buildSimpleResponse 产出 {code,msg}）
struct ChangePresenceResponse
{
    int         code;   //结果码，0 成功；400 bad request；其它见服务端 enum error_code
    std::string msg;    //结果描述（成功为 "ok"，失败为诊断信息）

    ChangePresenceResponse()
        : code(-1)      //-1：尚未解析出有效结果码，避免被误判为成功
    {
    }
};

//账号资料报文编解码工具类；全部为静态方法，不可实例化
class ProfileProtocolClient
{
public:
    /**
     *  构造"仅更新个性签名"的 cmd_update_profile 请求 JSON 包体（紧凑、UTF-8）。
     *  服务端按字段是否出现决定更新哪些项，故请求只放 signature 一项即只改签名。
     *  @param signatureUtf8 新个性签名（UTF-8；可为空串，表示清空签名）
     *  @return 紧凑 JSON 文本：{"signature":"..."}
     */
    static std::string buildUpdateSignatureRequest(const std::string& signatureUtf8);

    /**
     *  比较编辑前后的两份资料，得出改资料请求要提交哪几项：值不同的项标记为提交、新值取 after。
     *  只提交改动过的项，原因有二：服务端只对本次提交的项做长度与取值校验，库里遗留的旧值
     *  （如 2026-10-04 之前写入的超长昵称）不会连累本次修改被拒；敏感词检查也只针对真正改了的内容。
     *  @param before 编辑前的值（打开编辑时本人资料的当前值）
     *  @param after  编辑后的值（界面上的新值）
     *  @return 要提交的字段；两份完全相同时 IsEmpty() 为 true
     */
    static ProfileUpdateFields diffProfileValues(const ProfileEditableValues& before,
                                                 const ProfileEditableValues& after);

    /**
     *  构造"更新本人资料"的 cmd_update_profile 请求 JSON 包体（紧凑、UTF-8）。
     *  与服务端 ProfileProtocol::parseUpdateProfileRequest 镜像：各字段可选，服务端按
     *  "字段是否出现"决定更新项，本函数只放 m_hasXxx 为 true 的项。手机号任何情况下都不放。
     *  <u>注意邮箱字段 key 是 "mail"（不是 "email"）</u>，与服务端一致。
     *  @param fields 要提交的字段，通常由 diffProfileValues 得出
     *  @return 紧凑 JSON，如 {"post":"..","landline":".."}；一项都不提交时为 {}
     */
    static std::string buildUpdateProfileRequest(const ProfileUpdateFields& fields);

    /**
     *  构造"修改头像"的 cmd_change_avatar(1106) 请求 JSON 包体（紧凑、UTF-8）。
     *  字形头像（系统头像 / 首字母）与自定义图均走这条：facetype 区分类型，
     *  customface 承载"字形+底色槽"编码串（如 "L|0"）或上传得到的 fileid。
     *  编码约定见 BalloonMain/AvatarSpec.h；服务端落库并向联系人广播资料变更通知。
     *  @param facetype       头像类型（见 AvatarKind：0 自动 / 1 字形 / 2 自定义图）
     *  @param customfaceUtf8 头像标识（UTF-8；字形为 "glyph|idx"，自定义图为 fileid，
     *                        自动可为空串）
     *  @return 紧凑 JSON 文本：{"facetype":N,"customface":"..."}
     */
    static std::string buildChangeAvatarRequest(int facetype,
                                                const std::string& customfaceUtf8);

    /**
     *  解析 cmd_update_profile 应答 JSON 包体（通用 {code,msg}）。
     *  只要 JSON 合法且含整型 code 即返回 true，调用方据 resp.code 判定结果；
     *  JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param resp     [出参] 解析结果；返回 true 时 code/msg 有效
     *  @return JSON 合法且含整型 code 返回 true，否则 false
     */
    static bool parseUpdateProfileResponse(const std::string& jsonbody,
                                           UpdateProfileResponse& resp);

    /**
     *  构造"修改在线状态"的 cmd_change_presence 请求 JSON 包体（紧凑、UTF-8）。
     *  服务端据 presence 更新会话运行态并向好友广播；该状态不入库。
     *  @param presence 目标在线状态（v2 presence_type，取值见 net/Protocol.h：
     *                  0 离线 / 1 在线 / 2 离开 / 3 勿扰）
     *  @return 紧凑 JSON 文本：{"presence":N}
     */
    static std::string buildChangePresenceRequest(int presence);

    /**
     *  解析 cmd_change_presence 应答 JSON 包体（通用 {code,msg}）。
     *  只要 JSON 合法且含整型 code 即返回 true，调用方据 resp.code 判定结果；
     *  JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param resp     [出参] 解析结果；返回 true 时 code/msg 有效
     *  @return JSON 合法且含整型 code 返回 true，否则 false
     */
    static bool parseChangePresenceResponse(const std::string& jsonbody,
                                            ChangePresenceResponse& resp);

private:
    ProfileProtocolClient() = delete;
    ~ProfileProtocolClient() = delete;
    ProfileProtocolClient(const ProfileProtocolClient&) = delete;
    ProfileProtocolClient& operator=(const ProfileProtocolClient&) = delete;
};

} // namespace net
