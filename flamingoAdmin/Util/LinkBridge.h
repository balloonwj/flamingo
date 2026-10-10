/**
 *  flamingoAdmin —— 与 FlamingoLink.dll 的桥接（进程级单例）。
 *
 *  负责三件事（docs/FlamingoLink方案.md 第 8 节）：
 *    - 启动时按完整路径加载 exe 同目录的 FlamingoLink.dll，取工厂函数、核对接口版本并初始化模块；
 *      任一步失败只记日志，之后 IsAvailable() 为 false，控制台没有「在线用户」页签，其余功能照常；
 *    - 实现宿主接口 ILinkHost：DLL 的日志写进控制台日志（日志器已加锁，连接线程可直接调用）；
 *      DLL 报告会话过期时向主窗口投递一条消息，由主窗口回到登录页；
 *    - 把主窗口要做的调用（启停连接、页面位置与状态、当前企业）转交 DLL，DLL 不可用时这些调用什么都不做。
 *  除 ILinkHost::Log 以外全部方法只在 UI 线程上调用。
 *
 *  典型用法：
 *      LinkBridge::Instance().Load(hwndMain, WM_APP_LINK_EXPIRED);
 *      HWND page = LinkBridge::Instance().CreatePage(hwndMain);
 *      ...登录成功... LinkBridge::Instance().Start(host, port, pubkey, token);
 *      ...退出前... LinkBridge::Instance().Unload();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <atlstr.h>
#include <string>

#include "FlamingoLink/FlamingoLinkApi.h"
#include "Util/PluginLibrary.h"
#include "Util/LinkPageState.h"

namespace flamingoadmin {

// 与 FlamingoLink.dll 的桥接。
class LinkBridge : public flamingolink::ILinkHost
{
public:
    // 取全局唯一实例。
    static LinkBridge& Instance();

    // 加载并初始化 FlamingoLink.dll。重复调用安全（已加载则直接返回 true）。
    //   expiredHwnd：DLL 报告会话过期时接收通知的窗口（主窗口）。
    //   expiredMsg：通知的消息号。
    //   返回：DLL 可用返回 true；失败已记日志。
    bool Load(HWND expiredHwnd, UINT expiredMsg);

    // DLL 是否可用（已加载并初始化成功）。
    bool IsAvailable() const { return m_module != nullptr; }

    // 关闭并卸载 DLL：销毁页面、停止连接、关闭模块、释放 DLL。重复调用安全。
    // 须在主窗口销毁之前或之中调用（页面窗口是主窗口的子窗口）。
    void Unload();

    // 建立长连接（登录成功后调用）。DLL 不可用时什么都不做。
    //   host：服务器主机名或 IP（取网络设置里 HTTP 的主机）。
    //   port：login.do 下发的管理端口；0 表示服务器未开启。
    //   pubkey：login.do 下发的服务端公钥 base64；空串表示未开启加密。
    //   token：HTTP 登录得到的会话 token。
    void Start(const CString& host, int port, const std::string& pubkey, const CString& token);

    // 断开长连接并清空名单（退出登录、会话过期时调用）。DLL 不可用时什么都不做。
    void Stop();

    // 创建「在线用户」页面窗口（主窗口的子窗口，初始隐藏）。
    //   parent：主窗口。
    //   返回：页面窗口句柄；DLL 不可用或创建失败时为 nullptr。
    HWND CreatePage(HWND parent);

    // 设置页面窗口的位置与大小。
    //   rc：主窗口客户区坐标。
    void SetPageBounds(const RECT& rc);

    // 设置当前企业：企业 ID 或名称变化时才转交 DLL。
    //   enterpriseId：企业 ID 的十进制文本，空串表示没有企业。
    //   enterpriseName：企业名称。
    void SetEnterprise(const CString& enterpriseId, const CString& enterpriseName);

    // 设置页面窗口的状态（活动与否、是否被盖住），有变化时才转交 DLL。
    //   state：见 Util/LinkPageState.h。
    void ApplyPageState(const LinkPageState& state);

    // 取连接与订阅的当前状态；DLL 不可用时状态为 link_state_stopped、其余为 0。
    //   out：[出参]。
    void GetStatus(flamingolink::LinkStatus& out);

    // ---- ILinkHost 的实现 ----

    // 把 DLL 的一行日志写进控制台日志。可能在 DLL 的连接线程上调用。
    //   level：取值见 flamingolink::LinkLogLevel。text：UTF-8 英文日志。
    void Log(int level, const char* text) override;

    // DLL 报告鉴权时 token 已失效：向主窗口投递通知，由主窗口回到登录页。
    void OnSessionExpired() override;

private:
    LinkBridge();

    PluginLibrary               m_lib;            // FlamingoLink.dll
    flamingolink::ILinkModule*  m_module;         // 模块对象，归 DLL 所有；DLL 不可用时为 nullptr
    HWND                        m_expiredHwnd;    // 会话过期通知的目标窗口
    UINT                        m_expiredMsg;     // 会话过期通知的消息号
    CString                     m_enterpriseId;   // 上一次转交 DLL 的企业 ID
    CString                     m_enterpriseName; // 上一次转交 DLL 的企业名称
    bool                        m_stateKnown;     // m_pageState 是否已转交过 DLL（首次一定转交）
    LinkPageState               m_pageState;      // 上一次转交 DLL 的页面状态
};

} // namespace flamingoadmin
