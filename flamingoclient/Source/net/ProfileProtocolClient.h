/**
 *  v2 账号资料报文编解码（客户端侧）：构造 cmd_update_profile 请求 JSON、解析
 *  其通用 {code,msg} 应答。当前只覆盖"更新个性签名"这一最小用途（主面板点签名
 *  原地编辑、回车提交），后续要更新昵称/性别/生日等可再加 build 方法。
 *
 *  与服务端 chatserversrc/ProfileProtocol 互为镜像：服务端 parseUpdateProfileRequest
 *  解析请求、buildSimpleResponse 组装 {code,msg} 应答；客户端反过来。纯逻辑、只依赖
 *  jsoncpp，不触网络与 UI，便于单元测试，见 Tests/ProfileProtocolClientTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#pragma once

#include <string>

namespace net {

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
     *  构造"更新本人资料（全字段）"的 cmd_update_profile 请求 JSON 包体（紧凑、UTF-8）。
     *  与服务端 ProfileProtocol::parseUpdateProfileRequest 镜像：6 字段全可选，服务端按
     *  "字段是否出现"决定更新项；本函数始终把 6 项都放进去（把对话框当前编辑值整体覆盖，
     *  幂等）。<u>注意邮箱字段 key 是 "mail"（不是 "email"）</u>，与服务端一致。
     *  @param nicknameUtf8  昵称（UTF-8）
     *  @param signatureUtf8 个性签名（UTF-8）
     *  @param gender        性别（0 男 / 1 女，与客户端 ProfileDataInput 约定一致）
     *  @param birthday      生日 YYYYMMDD 整数（如 19950402；0 表示未设）
     *  @param addressUtf8   所在地（UTF-8）
     *  @param mailUtf8      邮箱（UTF-8）
     *  @return 紧凑 JSON：{"nickname":..,"signature":..,"gender":N,"birthday":N,"address":..,"mail":..}
     */
    static std::string buildUpdateProfileRequest(const std::string& nicknameUtf8,
                                                 const std::string& signatureUtf8,
                                                 int gender, long birthday,
                                                 const std::string& addressUtf8,
                                                 const std::string& mailUtf8);

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
