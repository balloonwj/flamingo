/**
 *  FlamingoLink.dll（管理控制台专用）与 flamingoAdmin.exe 之间的接口约定。
 *
 *  FlamingoLink.dll 负责管理控制台与 chatserver 管理端口之间的长连接（加密握手、鉴权、心跳、
 *  断线重连），以及企业详情页里「在线用户」页签的全部界面。flamingoAdmin.exe 启动时按完整路径
 *  加载它；加载失败（文件不存在、位数不符、导出函数缺失、接口版本不符）时控制台照常运行，
 *  只是没有「在线用户」页签。方案见 docs/FlamingoLink方案.md 第 7、8 节。
 *
 *  PC 客户端将来也会有一个同名的 FlamingoLink.dll（用于数据上报），那是另一个工程、另一套接口，
 *  与本文件无关。两个程序都只从自己所在的目录按完整路径加载，不会互相加载错。
 *
 *  接口约定：
 *    - exe 与 DLL 各自静态链接 CRT、各有各的堆。跨边界只传 POD 结构、基本类型与以 NUL 结尾的字符串，
 *      字符串指针只在调用期间有效，任何一方都不释放对方分配的内存；
 *    - 除 ILinkHost::Log 以外的全部调用都发生在界面线程上；
 *    - 增删接口方法、改动参数或结构体布局时，两侧一起编译（产品尚未发布，kApiVersion 不随之递增）。
 *
 *  典型用法（exe 侧）：
 *      FlamingoLink_CreateFn create = (FlamingoLink_CreateFn)GetProcAddress(h, FLAMINGO_LINK_FACTORY_NAME);
 *      flamingolink::ILinkModule* m = create(flamingolink::kApiVersion);
 *      m->Init(&host);
 *      HWND page = m->CreatePage(hwndMainFrame);
 *      ...登录成功后... m->Start(&params);
 *      ...进入某企业的「在线用户」页签... m->SetEnterprise(L"123", L"某某公司"); m->SetPageActive(true);
 *      ...退出登录... m->Stop();
 *      ...程序退出... m->DestroyPage(); m->Stop(); m->Shutdown(); FreeLibrary(h);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <windows.h>
#include <stdint.h>

namespace flamingolink {

//接口版本。工厂函数只接受与自己编译时相同的版本号
const uint32_t kApiVersion = 1;

//DLL 文件名：与 flamingoAdmin.exe 放在同一目录
#define FLAMINGO_LINK_DLL_NAME      L"FlamingoLink.dll"

//导出的工厂函数名，由 FlamingoLink.def 固定，32 位与 64 位相同
#define FLAMINGO_LINK_FACTORY_NAME  "FlamingoLink_Create"

//日志级别，ILinkHost::Log 的 level 参数
enum LinkLogLevel
{
    link_log_info    = 0,   //一般信息：连接建立、鉴权成功、订阅成功等
    link_log_warning = 1,   //可以自动恢复的异常：连接断开、重连失败、应答超时等
    link_log_error   = 2,   //需要关注的错误：握手失败、协议错误、参数非法等
};

/**
 *  宿主（flamingoAdmin.exe）实现、交给 DLL 回调的接口。对象由宿主持有，生命期须覆盖
 *  ILinkModule::Init 到 ILinkModule::Shutdown 之间。
 */
class ILinkHost
{
public:
    virtual ~ILinkHost() {}

    /**
     *  写一行日志。<b>可能在 DLL 的连接线程上调用</b>，宿主的实现必须线程安全。
     *  @param level 日志级别，取值见 enum LinkLogLevel
     *  @param text  一行英文日志（ASCII 为主，昵称等数据可能含 UTF-8），以 NUL 结尾，不含换行；只在调用期间有效
     */
    virtual void Log(int level, const char* text) = 0;

    /**
     *  长连接鉴权时服务端报告 token 已失效（过期、被注销、管理员被停用）。在界面线程上调用。
     *  DLL 此时已停止重连；宿主应当像 HTTP 接口返回「会话已过期」那样回到登录页。
     */
    virtual void OnSessionExpired() = 0;
};

/**
 *  ILinkModule::Start 的参数。字符串只在调用期间有效，DLL 内部会复制。
 */
struct LinkStartParams
{
    const wchar_t* m_host;           //服务器主机名或 IP，取控制台网络设置里 HTTP 的主机；不得为 NULL
    int            m_port;           //管理端口，取 login.do 应答的 linkport；为 0 表示服务器未开启管理端口
    const char*    m_pubkeyBase64;   //服务端身份公钥的 base64，取 login.do 的 linkpubkey；为空串表示服务器未开启加密，跳过握手
    const char*    m_token;          //HTTP 登录得到的会话 token；不得为空
    const char*    m_clientVersion;  //控制台版本号，只用于服务端日志；可为空串
};

//连接状态，LinkStatus::m_state 的取值
enum LinkState
{
    link_state_stopped      = 0,   //未启动（尚未登录或已退出登录）
    link_state_disabled     = 1,   //服务器未开启管理端口（login.do 的 linkport 为 0），不连接
    link_state_connecting   = 2,   //正在连接、握手或鉴权
    link_state_connected    = 3,   //已鉴权，连接正常
    link_state_waiting      = 4,   //连接断开或连接失败，等待下一次重连
    link_state_expired      = 5,   //鉴权时 token 已失效，不再重连
};

/**
 *  连接与订阅的当前状态快照，供宿主记日志与单元测试使用。
 */
struct LinkStatus
{
    int     m_state;              //连接状态，取值见 enum LinkState
    int     m_retryAttempt;       //m_state 为 link_state_waiting 时是第几次重连（从 1 起），其余为 0
    int     m_retryDelayMs;       //m_state 为 link_state_waiting 时距下次重连的总等待时间（毫秒），其余为 0
    int64_t m_subscribedEnterprise; //服务端已确认订阅的企业ID；未订阅时为 0
    int     m_onlineUsers;        //当前名单里的在线人数
    int     m_onlineDevices;      //当前名单里的在线设备数（即表格行数）
    int     m_lastError;          //最近一次订阅失败的错误码（1101 无权查看、1102 企业不存在等）；没有失败时为 0
};

/**
 *  DLL 实现、交给宿主调用的模块接口。对象归 DLL 所有，宿主不得释放。
 */
class ILinkModule
{
public:
    virtual ~ILinkModule() {}

    /**
     *  @return 编译 DLL 时的 kApiVersion
     */
    virtual uint32_t GetApiVersion() const = 0;

    /**
     *  初始化：记下宿主接口、初始化密码学库、在界面线程上建一个只收消息的窗口。须先于其它方法调用一次。
     *  @param host 宿主接口，不得为 NULL；所有权归宿主
     *  @return 成功返回 true；失败时宿主应当当作 DLL 不可用（卸载它，不显示页签）
     */
    virtual bool Init(ILinkHost* host) = 0;

    /**
     *  关闭：停止连接线程、销毁页面与消息窗口。之后宿主才能卸载 DLL。重复调用安全。
     */
    virtual void Shutdown() = 0;

    /**
     *  建立长连接（登录成功后调用）。已在运行时先停掉旧连接再按新参数开始。
     *  m_port 为 0 时不连接，状态置为 link_state_disabled，页面提示服务器未开启。
     *  @param params 连接参数，不得为 NULL
     */
    virtual void Start(const LinkStartParams* params) = 0;

    /**
     *  断开长连接并停止重连（退出登录、会话过期时调用），清空在线名单。未启动时调用是安全的。
     *  本函数会等待连接线程结束，最长约为一次连接超时。
     */
    virtual void Stop() = 0;

    /**
     *  创建「在线用户」页面窗口：parent 的子窗口，初始隐藏。已创建时直接返回现有窗口。
     *  @param parent 父窗口（控制台主窗口）
     *  @return 页面窗口句柄；失败返回 NULL
     */
    virtual HWND CreatePage(HWND parent) = 0;

    /**
     *  销毁页面窗口。未创建时调用是安全的。
     */
    virtual void DestroyPage() = 0;

    /**
     *  设置页面窗口的位置与大小。
     *  @param rc 父窗口客户区坐标；为 NULL 时忽略
     */
    virtual void SetPageBounds(const RECT* rc) = 0;

    /**
     *  设置当前要看的企业。与 SetPageActive 一起决定订阅：页面处于活动状态且企业 ID 大于 0 时订阅该企业，
     *  否则退订。换企业时清空表格并改订。
     *  @param enterpriseId   企业ID的十进制文本；为 NULL、空串或 "0" 表示不看任何企业
     *  @param enterpriseName 企业名称，只用于页面提示；可为 NULL
     */
    virtual void SetEnterprise(const wchar_t* enterpriseId, const wchar_t* enterpriseName) = 0;

    /**
     *  页面是否处于活动状态（企业详情页可见、且选中了「在线用户」页签）。活动时显示页面窗口并订阅，
     *  不活动时隐藏并退订。
     *  @param active true 表示活动
     */
    virtual void SetPageActive(bool active) = 0;

    /**
     *  页面是否暂时被宿主的叠层弹窗盖住。被盖住时只隐藏页面窗口（原生子窗口会画在叠层弹窗之上），
     *  不退订，弹窗关掉后照原样显示。
     *  @param covered true 表示被盖住
     */
    virtual void SetPageCovered(bool covered) = 0;

    /**
     *  取连接与订阅的当前状态。
     *  @param out [出参] 不得为 NULL
     */
    virtual void GetStatus(LinkStatus* out) = 0;
};

/**
 *  判定宿主与 DLL 的接口版本是否兼容。产品未发布期间要求完全相等。
 *  @param hostVersion   宿主编译时的 kApiVersion
 *  @param moduleVersion DLL 编译时的 kApiVersion
 */
inline bool IsApiVersionCompatible(uint32_t hostVersion, uint32_t moduleVersion)
{
    return hostVersion == moduleVersion;
}

} // namespace flamingolink

//工厂函数的类型：传入宿主的 kApiVersion，版本兼容时返回模块对象（归 DLL 所有），不兼容返回 NULL
typedef flamingolink::ILinkModule* (__cdecl *FlamingoLink_CreateFn)(uint32_t apiVersion);
