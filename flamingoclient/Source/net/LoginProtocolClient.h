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
#include <map>
#include <string>

#include "Protocol.h"   // enum profile_visibility（两个公开范围的默认值）

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
    int         gender;          //性别，取值见 Protocol.h 的 enum profile_gender：0 未设置 / 1 男 / 2 女
    int         birthday;        //生日，形如 19891208
    std::string signature;       //个性签名
    std::string address;         //所在地
    std::string phonenumber;     //电话
    std::string mail;            //邮箱
    std::string landline;        //座机，未填为空串
    std::string post;            //岗位：本人填写的自由文本，与部门职位无关；未填为空串
    int64_t     enterpriseid;    //所属企业 ID，0 表示无企业归属
    int         phonevisible;    //手机号对同事的公开范围，取值见 Protocol.h 的 enum profile_visibility；
                                 //老服务端不下发时取服务端的默认值「仅本人可见」
    int         landlinevisible; //座机对同事的公开范围，取值同上；老服务端不下发时取默认值「对同事可见」

    LoginResponseProfile()
        : usertype(0)
        , facetype(0)
        , gender(0)
        , birthday(0)
        , enterpriseid(0)
        , phonevisible(profile_visibility_self_only)
        , landlinevisible(profile_visibility_colleagues)
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
    std::map<std::string, int> features;  //本人所属企业的功能开关（功能名 → 0 / 1），仅 code==0 时有效；
                                          //服务端没有下发 features 字段时为空，按全部关闭处理（docs/文件共享方案.md 5.8）

    LoginResponse()
        : code(-1)        //-1：尚未解析出有效结果码，避免被误判为成功
        , userid(0)
        , servertime(0)
    {
    }
};

//功能名：文件共享（登录应答与 cmd_features_notify(1013) 的 features 对象里的键）
extern const char* const kFeatureFileShare;
//功能名：邀请入群需被邀请人同意（docs/邀请入群方案.md §3.5）。打开时邀请对话框的说明行提示「对方同意后才会入群」
extern const char* const kFeatureInviteConfirm;
//功能名：群语音 / 群视频通话（docs/群音视频通话方案.md）。关闭时群聊窗隐藏「语音通话」「视频通话」按钮
extern const char* const kFeatureGroupCall;
//功能名：语音字幕（docs/语音字幕方案.md 第 10 节）。关闭时不加载字幕模块，录屏与屏幕共享都不提供字幕
extern const char* const kFeatureCaption;
//功能名：云端录制（docs/屏幕共享与录制方案.md 12.6，默认关闭）。打开时共享控制条与录制选择窗才提供云端录制的入口
extern const char* const kFeatureCloudRecord;
//功能名：单聊密聊（docs/密聊方案.md 4.9，默认开启）。打开时才加载 FlamingoSecretChat.dll，单聊窗才有「密聊」按钮
extern const char* const kFeatureSecretChat;
//功能名：帖子群（docs/帖子群方案.md 4.7，默认开启）。打开时才加载 FlamingoThreadGroup.dll，帖子群才在各列表里显示
extern const char* const kFeatureThreadGroup;
//功能名：机器人总开关（docs/机器人方案.md §4.1，默认开启）。关闭时不加载 FlamingoBot.dll，群聊窗不显示机器人分组与管理入口
extern const char* const kFeatureBot;
//功能名：允许群主添加群机器人（默认开启）
extern const char* const kFeatureBotGroup;
//功能名：允许员工配置个人机器人（默认开启）。关闭时「我的机器人」入口隐藏，离开 / 忙碌自动回复也不生效
extern const char* const kFeatureBotPersonal;
//功能名：允许群主给群机器人配置外部服务（默认关闭）
extern const char* const kFeatureBotGroupExternal;

//cmd_features_notify(1013) 推送解析结果：本人所属企业的功能开关有变化，包体与登录应答里的 features 对象同构
struct FeaturesNotify
{
    std::map<std::string, int> m_features;   //功能名 → 0 / 1；包体里不是整数的项略去

    FeaturesNotify()
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

    /**
     *  解析 cmd_features_notify(1013) 推送 JSON 包体（与登录应答里的 features 对象同构，如 {"fileshare":1}）。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param notify   [出参] 解析结果
     *  @return JSON 合法且是对象返回 true，否则 false
     */
    static bool parseFeaturesNotify(const std::string& jsonbody, FeaturesNotify& notify);

    /**
     *  取一项功能是否开启。
     *  @param features 功能名 → 0 / 1（登录应答或 1013 解析出的结果）
     *  @param feature  功能名，如 kFeatureFileShare
     *  @return 有这一项且为 1 返回 true；没有这一项（老服务端）或为 0 返回 false
     */
    static bool isFeatureOn(const std::map<std::string, int>& features, const char* feature);

private:
    LoginProtocolClient() = delete;
    ~LoginProtocolClient() = delete;
    LoginProtocolClient(const LoginProtocolClient&) = delete;
    LoginProtocolClient& operator=(const LoginProtocolClient&) = delete;
};

} // namespace net
