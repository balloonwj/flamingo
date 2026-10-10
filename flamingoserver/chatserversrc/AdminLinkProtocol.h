/**
 *  管理控制台长连接（管理端口 2501~2504）的报文编解码与纯规则，AdminLinkProtocol.h
 *
 *  负责鉴权、订阅请求的解析，应答与在线名单推送（cmd_adminlink_online_notify）的组装，
 *  以及与网络、数据库无关的几条规则：订阅权限判定、按用户归并在线设备、名单分帧、
 *  空闲与未鉴权超时判定、login.do 里长连接字段的取值。会话收发在 AdminLinkSession，
 *  订阅表与推送在 AdminLinkServer。方案见 docs/FlamingoLink方案.md，协议见
 *  docs/协议设计-v2.md 第 22 节。
 *
 *  本头文件<u>不包含 Protocol.h</u>：HttpSession.cpp 要用 resolveLoginLinkInfo，而它属于
 *  AdminApiCodes.h 一侧，两个头文件里有同名枚举，不能进同一个编译单元。错误码等取值在
 *  .cpp 里取自 Protocol.h，头文件只出现 int32_t。
 *
 *  典型用法：
 *      std::string token, ver;
 *      if (!AdminLinkProtocol::parseAuthRequest(body, token, ver)) { 回 error_code_bad_request; }
 *      ...
 *      std::vector<AdminLinkUser> users;
 *      AdminLinkProtocol::buildUsers(userids, facts, profiles, users);
 *      std::vector<std::string> bodies;
 *      AdminLinkProtocol::buildOnlineNotifyBodies(entId, true, users, kAdminLinkUsersPerFrame, bodies);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <map>

namespace dept {
struct DepartmentInfo;
}

//建议控制台发心跳的间隔，单位：秒。随 2501 应答下发，控制台按它发 1002
const int kAdminLinkHeartbeatSec = 10;

//空闲断开时间，单位：秒。服务端这么久没收到某条管理连接的任何帧就断开它；控制台同样以此判定断线
const int kAdminLinkIdleTimeoutSec = 30;

//未鉴权断开时间，单位：秒。连上之后这么久还没有鉴权成功就断开，防止无效连接长期占着名额
const int kAdminLinkAuthTimeoutSec = 10;

//服务端检查空闲与未鉴权超时的周期，单位：秒
const int kAdminLinkTimeoutCheckSec = 5;

//服务端复查已鉴权连接 token 的周期，单位：秒。token 过期、被注销或管理员被停用后最迟这么久断开
const int kAdminLinkTokenRecheckSec = 60;

//待推送用户的攒批周期，单位：毫秒。期间同一用户多次变化只推一次
const int kAdminLinkFlushIntervalMs = 1000;

//一条 2504 最多携带的用户数。单帧包体上限 1 MiB，一个用户连同设备约 200~300 字节，取 1000 留足余量
const size_t kAdminLinkUsersPerFrame = 1000;

//鉴权请求里 token 的最大字节数。正常 token 是 32 位十六进制，超长的请求直接按格式错误拒绝
const size_t kAdminLinkTokenMaxBytes = 128;

/**
 *  在线名单里一台设备（一个已登录的会话）。
 */
struct AdminLinkDevice
{
    AdminLinkDevice() : clienttype(0), presence(0), logintime(0) {}

    int32_t     clienttype;   //客户端类型，取值见 Protocol.h 的 enum platform_type
    int32_t     presence;     //在线状态，取值见 Protocol.h 的 enum presence_type
    int64_t     logintime;    //本次登录成功的时间，Unix 毫秒；未记录时为 0
    std::string ip;           //连接的来源地址，不含端口；取不到时为空串
};

/**
 *  在线名单里一个用户：资料加上他当前全部已登录的设备。devices 为空表示已离线。
 */
struct AdminLinkUser
{
    AdminLinkUser() : userid(0) {}

    int64_t                      userid;    //用户ID
    std::string                  account;   //登录账号；取不到资料时为空串
    std::string                  nickname;  //昵称；取不到资料时为空串
    std::vector<std::string>     depts;     //所在部门名称，主部门在前；不在任何部门时为空
    std::vector<AdminLinkDevice> devices;   //已登录的设备，按 clienttype 升序；为空表示离线
};

/**
 *  从会话表取出的一个会话的事实，是归并设备的输入。由 AdminLinkServer 在推送线程上从
 *  ChatServer 的会话快照里逐个读出，读完即与会话对象脱钩。
 */
struct AdminLinkSessionFact
{
    AdminLinkSessionFact() : userid(0), loggedIn(false), clienttype(0), presence(0), logintime(0) {}

    int64_t     userid;       //会话的用户ID；0 表示未登录或已被置为失效
    bool        loggedIn;     //会话是否处于登录态（主动注销后为 false，此时不再计入在线）
    int32_t     clienttype;   //客户端类型，取值见 enum platform_type
    int32_t     presence;     //在线状态，取值见 enum presence_type
    int64_t     logintime;    //本次登录成功的时间，Unix 毫秒
    std::string ip;           //连接的来源地址，不含端口
};

/**
 *  一个用户在名单里显示的资料，取自组织架构缓存。
 */
struct AdminLinkUserProfile
{
    std::string              account;   //登录账号
    std::string              nickname;  //昵称
    std::vector<std::string> depts;     //所在部门名称，主部门在前
};

/**
 *  管理端口报文与规则的静态方法集合，无状态，可从任意线程调用。
 */
class AdminLinkProtocol
{
public:
    /**
     *  解析 2501 鉴权请求。
     *  @param json              请求包体（UTF-8 JSON）
     *  @param outToken          [出参] token；必须是 1~kAdminLinkTokenMaxBytes 字节的字符串
     *  @param outClientVersion  [出参] 控制台版本号；缺失时为空串，只用于日志
     *  @return 包体不是 JSON 对象、token 缺失 / 不是字符串 / 为空 / 超长时返回 false
     */
    static bool parseAuthRequest(const std::string& json, std::string& outToken, std::string& outClientVersion);

    /**
     *  组装 2501 应答。code 为 0 时附带管理员 ID、管辖企业与心跳参数，否则只有 code 与 msg。
     *  @param code          错误码，取值见 Protocol.h 的 enum error_code
     *  @param msg           面向开发者的英文说明，不得为 NULL
     *  @param adminid       管理员ID（仅成功时写入）
     *  @param enterpriseid  管辖企业ID，0 表示平台超管（仅成功时写入）
     *  @return 紧凑的 JSON 文本
     */
    static std::string buildAuthResponse(int32_t code, const char* msg, int64_t adminid, int64_t enterpriseid);

    /**
     *  解析 2502 订阅请求。
     *  @param json             请求包体
     *  @param outEnterpriseId  [出参] 要订阅的企业ID
     *  @return enterpriseid 缺失、不是十进制数字字符串、或不大于 0 时返回 false
     */
    static bool parseSubscribeRequest(const std::string& json, int64_t& outEnterpriseId);

    /**
     *  组装 2502 应答。成功时回带企业ID。
     *  @param code          错误码
     *  @param msg           英文说明，不得为 NULL
     *  @param enterpriseId  订阅的企业ID（仅成功时写入）
     */
    static std::string buildSubscribeResponse(int32_t code, const char* msg, int64_t enterpriseId);

    /**
     *  组装只有 code 与 msg 的应答（2503 等）。
     */
    static std::string buildSimpleResponse(int32_t code, const char* msg);

    /**
     *  判定一次订阅是否允许。
     *  @param adminEnterpriseId      管理员的管辖企业，0 表示平台超管
     *  @param requestedEnterpriseId  要订阅的企业，调用方已保证大于 0
     *  @param enterpriseExists       要订阅的企业在库里是否存在
     *  @return error_code_ok、error_code_adminlink_forbidden（企业管理员订阅别的企业，先于存在性判定，
     *          免得企业管理员借此探测别的企业 ID 是否存在）或 error_code_adminlink_enterprise_not_exist
     */
    static int32_t checkSubscribePermission(int64_t adminEnterpriseId, int64_t requestedEnterpriseId,
                                            bool enterpriseExists);

    /**
     *  从会话事实里收集所有处于登录态的用户ID，升序、去重。用于订阅时组完整名单。
     *  @param facts   会话事实
     *  @param outIds  [出参] 先清空再填充
     */
    static void collectOnlineUserIds(const std::vector<AdminLinkSessionFact>& facts, std::vector<int64_t>& outIds);

    /**
     *  按用户归并设备，组出名单条目。每个 userid 产出一个条目（按 userids 的顺序，重复的只取一次）：
     *  设备取 facts 里 userid 相同、处于登录态的会话，按 clienttype 升序；资料取 profiles，
     *  查不到时账号与昵称为空串、部门为空。
     *  @param userids   要组条目的用户
     *  @param facts     会话事实（可以包含别的企业、别的用户的会话，本函数只取相关的）
     *  @param profiles  userid → 资料
     *  @param outUsers  [出参] 先清空再填充
     */
    static void buildUsers(const std::vector<int64_t>& userids,
                           const std::vector<AdminLinkSessionFact>& facts,
                           const std::map<int64_t, AdminLinkUserProfile>& profiles,
                           std::vector<AdminLinkUser>& outUsers);

    /**
     *  由一个企业的组织架构组出 userid → 资料。账号、昵称取第一次遇到的那条成员记录；
     *  部门按组织架构顺序收集，主部门（isprimary=1）挪到最前，同一部门不重复。
     *  @param depts        DepartmentManager::getOrgStructure 的输出
     *  @param outProfiles  [出参] 先清空再填充
     */
    static void buildProfilesFromOrg(const std::vector<dept::DepartmentInfo>& depts,
                                     std::map<int64_t, AdminLinkUserProfile>& outProfiles);

    /**
     *  把名单分帧，组出若干条 2504 包体。
     *  @param enterpriseId    企业ID
     *  @param snapshot        true 表示订阅后的完整名单：第一帧带 reset=1，名单为空时也发一帧；
     *                         false 表示变化推送：名单为空时不产出任何帧
     *  @param users           名单
     *  @param usersPerFrame   每帧最多的用户数；传 0 按 1 处理
     *  @param outBodies       [出参] 先清空再填充，按发送顺序排列
     */
    static void buildOnlineNotifyBodies(int64_t enterpriseId, bool snapshot,
                                        const std::vector<AdminLinkUser>& users, size_t usersPerFrame,
                                        std::vector<std::string>& outBodies);

    /**
     *  去掉「地址:端口」末尾的端口。没有冒号时原样返回。只处理 IPv4 的写法，与 InetAddress 一致。
     *  @param ipPort  形如 "192.168.1.10:51234"
     *  @return 形如 "192.168.1.10"
     */
    static std::string stripPort(const std::string& ipPort);

    /**
     *  是否已空闲超时：nowSec - lastRecvSec 不小于 timeoutSec 即超时。
     *  @param nowSec       当前时间，Unix 秒
     *  @param lastRecvSec  最近一次收到帧的时间，Unix 秒
     *  @param timeoutSec   超时秒数
     */
    static bool isIdleTimeout(int64_t nowSec, int64_t lastRecvSec, int timeoutSec);

    /**
     *  是否已未鉴权超时：尚未鉴权且 nowSec - connectSec 不小于 timeoutSec 即超时；已鉴权恒为 false。
     *  @param nowSec      当前时间，Unix 秒
     *  @param connectSec  连接建立的时间，Unix 秒
     *  @param authed      是否已鉴权成功
     *  @param timeoutSec  超时秒数
     */
    static bool isAuthTimeout(int64_t nowSec, int64_t connectSec, bool authed, int timeoutSec);

    /**
     *  决定 login.do 应答里的 linkport 与 linkpubkey。
     *  @param listening          管理端口是否在监听
     *  @param port               管理端口号
     *  @param encryptionEnabled  服务端是否开启协议加密
     *  @param pubkeyBase64       服务端身份公钥的 base64
     *  @param outPort            [出参] 未监听时为 0
     *  @param outPubkey          [出参] 未监听或未开启加密时为空串
     */
    static void resolveLoginLinkInfo(bool listening, int port, bool encryptionEnabled,
                                     const std::string& pubkeyBase64,
                                     int& outPort, std::string& outPubkey);
};
