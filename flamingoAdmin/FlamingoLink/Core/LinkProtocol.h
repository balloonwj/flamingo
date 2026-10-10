/**
 *  FlamingoLink.dll —— 管理端口报文的组装与解析，以及连接过程中的几条纯规则。
 *
 *  负责 2501 鉴权、2502 订阅、2503 退订请求的组装，各应答与 2504 在线名单推送的解析，
 *  以及与网络无关的规则：鉴权结果归类、重连间隔、订阅动作判定、心跳参数取值范围。
 *  全部是纯函数，单元测试见 flamingoAdmin/Tests/FlamingoLinkTests.cpp（FLK1~FLK3、FLK7）。
 *  协议见 docs/协议设计-v2.md 第 22 节。
 *
 *  本文件用 #include "json.h" 取 jsoncpp，由各工程的包含路径决定取哪一份：DLL 工程取 PC 客户端
 *  那份（与它编译的 FrameCrypto.cpp 一致），flamingoAdminTests 取控制台自己那份。
 *
 *  典型用法：
 *      std::string body = flamingolink::BuildAuthRequest(token, version);
 *      ...收到 2501 应答...
 *      flamingolink::AuthResult r;
 *      if (flamingolink::ParseAuthResponse(json, r) && flamingolink::ClassifyAuth(r.m_code) == flamingolink::auth_ok) { ... }
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <atlstr.h>

namespace flamingolink {

//控制台发心跳的间隔（秒）：服务端 2501 应答没带 heartbeat 时用这个值
const int kDefaultHeartbeatSec = 10;

//空闲判定时间（秒）：这么久收不到服务端任何数据即认为连接已断；服务端应答没带 timeout 时用这个值
const int kDefaultTimeoutSec = 30;

//服务端下发的心跳间隔的取值范围（秒），超出时夹到边界
const int kMinHeartbeatSec = 1;
const int kMaxHeartbeatSec = 60;

//服务端下发的空闲判定时间的取值范围（秒），超出时夹到边界
const int kMinTimeoutSec = 3;
const int kMaxTimeoutSec = 300;

//重连间隔：第 1 次 1 秒，之后逐次翻倍，最长 30 秒
const int kRetryBaseMs = 1000;
const int kRetryMaxMs = 30000;

//重连间隔的随机浮动幅度（百分比）：实际间隔在基准值的 ±20% 之内，避免多台控制台同时重连
const int kRetryJitterPercent = 20;

//连接超时（毫秒）：TCP 连接这么久还没建立就放弃这一次
const int kConnectTimeoutMs = 5000;

//鉴权应答的等待时间（毫秒）：发出 2501 后这么久还没收到应答就断开重连
const int kAuthReplyTimeoutMs = 10000;

/**
 *  在线名单里一台设备。
 */
struct LinkDevice
{
    LinkDevice() : m_clientType(0), m_presence(0), m_loginTimeMs(0) {}

    int     m_clientType;   //客户端类型：1 PC、2 Android、3 iOS、4 Mac、5 Web
    int     m_presence;     //在线状态：1 在线、2 离开、3 勿扰
    int64_t m_loginTimeMs;  //本次登录成功的时间，Unix 毫秒；未知时为 0
    CString m_ip;           //连接的来源地址（不含端口）；未知时为空串
};

/**
 *  在线名单里一个用户：资料加上他当前全部已登录的设备。m_devices 为空表示已离线。
 */
struct LinkUser
{
    LinkUser() : m_userId(0) {}

    int64_t                 m_userId;    //用户ID
    CString                 m_account;   //登录账号
    CString                 m_nickname;  //昵称
    std::vector<CString>    m_depts;     //所在部门名称，主部门在前
    std::vector<LinkDevice> m_devices;   //已登录的设备；为空表示已离线
};

/**
 *  一条 2504 推送的内容。
 */
struct OnlineNotify
{
    OnlineNotify() : m_enterpriseId(0), m_reset(false) {}

    int64_t               m_enterpriseId;  //企业ID
    bool                  m_reset;         //true：先清空名单再应用本条（订阅后的完整名单首帧）
    std::vector<LinkUser> m_users;         //本条带的用户，每个用户整体替换
};

/**
 *  2501 应答的内容。
 */
struct AuthResult
{
    AuthResult()
        : m_code(-1), m_adminId(0), m_enterpriseId(0)
        , m_heartbeatSec(kDefaultHeartbeatSec), m_timeoutSec(kDefaultTimeoutSec)
    {
    }

    int     m_code;          //错误码：0 成功、3 请求格式错误、1100 token 无效
    int64_t m_adminId;       //管理员ID（成功时有效）
    int64_t m_enterpriseId;  //管理员管辖的企业，0 表示平台超管（成功时有效）
    int     m_heartbeatSec;  //心跳间隔（秒），已夹到 [kMinHeartbeatSec, kMaxHeartbeatSec]
    int     m_timeoutSec;    //空闲判定时间（秒），已夹到 [kMinTimeoutSec, kMaxTimeoutSec]
};

//鉴权结果的归类，决定连接线程接下来怎么做
enum AuthOutcome
{
    auth_ok      = 0,   //鉴权成功，进入收发循环
    auth_expired = 1,   //token 已失效：停止重连，通知宿主回登录页
    auth_retry   = 2,   //其它失败（格式错误、服务端异常）：断开后按重连间隔再试
};

//订阅动作，见 DecideSubscription
enum SubscribeAction
{
    subscribe_none        = 0,   //不需要动作
    subscribe_send        = 1,   //发 2502 订阅期望的企业（含从一个企业换到另一个企业）
    subscribe_unsubscribe = 2,   //发 2503 退订
};

/**
 *  组装 2501 鉴权请求。
 *  @param token          HTTP 登录得到的 token
 *  @param clientVersion  控制台版本号，可为空串
 *  @return 紧凑的 JSON 文本
 */
std::string BuildAuthRequest(const std::string& token, const std::string& clientVersion);

/**
 *  组装 2502 订阅请求。
 *  @param enterpriseId 企业ID，大于 0
 */
std::string BuildSubscribeRequest(int64_t enterpriseId);

/**
 *  组装 2503 退订请求（空对象）。
 */
std::string BuildUnsubscribeRequest();

/**
 *  解析 2501 应答。心跳与超时缺失时取默认值，越界时夹到取值范围内。
 *  @param json 应答包体
 *  @param out  [出参]
 *  @return 不是 JSON 对象或缺少数值型 code 时返回 false
 */
bool ParseAuthResponse(const std::string& json, AuthResult& out);

/**
 *  把 2501 的错误码归类。
 *  @param code 2501 应答里的 code
 */
AuthOutcome ClassifyAuth(int code);

/**
 *  解析 2502 / 2503 应答里的 code 与 enterpriseid（后者只有 2502 成功应答才有）。
 *  @param json            应答包体
 *  @param outCode         [出参] 错误码
 *  @param outEnterpriseId [出参] 企业ID；应答里没有时为 0
 *  @return 不是 JSON 对象或缺少数值型 code 时返回 false
 */
bool ParseCodeResponse(const std::string& json, int& outCode, int64_t& outEnterpriseId);

/**
 *  解析 2504 推送。用户与设备里缺失的字段按默认值处理；userid 不是正整数文本的用户整条忽略。
 *  @param json 推送包体
 *  @param out  [出参]
 *  @return 不是 JSON 对象、enterpriseid 不是正整数文本、或 users 不是数组时返回 false
 */
bool ParseOnlineNotify(const std::string& json, OnlineNotify& out);

/**
 *  第 attempt 次重连之前要等多久。基准值从 kRetryBaseMs 起逐次翻倍、最长 kRetryMaxMs，
 *  再按 randomUnit 在基准值的 ±kRetryJitterPercent% 之内浮动。
 *  @param attempt    第几次重连，从 1 起；小于 1 时按 1 处理
 *  @param randomUnit [0, 1) 之间的随机数；0 得到下限，接近 1 得到上限，超出范围时夹到边界
 *  @return 等待毫秒数
 */
int ComputeRetryDelayMs(int attempt, double randomUnit);

/**
 *  页面当前想订阅的企业：页面处于活动状态、且设置了企业时才订阅。
 *  @param pageActive    页面是否处于活动状态
 *  @param enterpriseId  页面设置的企业ID，0 表示没有
 *  @return 想订阅的企业ID；0 表示不订阅
 */
int64_t DesiredEnterprise(bool pageActive, int64_t enterpriseId);

/**
 *  比较想订阅的企业与已发出订阅的企业，决定要发什么。
 *  @param desired            想订阅的企业，0 表示不订阅
 *  @param sent               已发出订阅、尚未退订的企业，0 表示没有
 *  @param generationChanged  界面线程在上次发订阅之后又要求过重新订阅（换过企业）。企业从甲换到乙再换回甲
 *                            时 desired 与 sent 相同，但界面已清空名单，必须重新订阅才能再拿到完整名单
 *  @return 订阅动作
 */
SubscribeAction DecideSubscription(int64_t desired, int64_t sent, bool generationChanged);

/**
 *  把企业ID的十进制文本解析成数值。
 *  @param text 文本；为 NULL、空串、含非数字字符或不大于 0 时返回 false
 *  @param out  [出参] 解析结果；失败时为 0
 */
bool ParseEnterpriseIdText(const wchar_t* text, int64_t& out);

/**
 *  UTF-8 字节串转成 CString（UTF-16）。
 */
CString Utf8ToCString(const std::string& text);

/**
 *  CString（UTF-16）转成 UTF-8 字节串。
 */
std::string CStringToUtf8(const CString& text);

} // namespace flamingolink
