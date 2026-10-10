/**
 *  P2P 服务的实现（IP2PService）：按「用途」登记业务方。
 *
 *  第 1b 步只做登记表：同一用途同时只能有一个业务方，注销后可以重新登记；模块未初始化（或已关闭）时
 *  登记一律失败，模块关闭时清空全部登记。收到 cmd_p2p_session_notify 后按用途找业务方、建链并回调，
 *  在第 3 期接入。
 *
 *  所有方法都可以在任意线程上调用，内部用互斥量保护登记表。登记表有变化时通知观察者（模块对象），由它让引擎
 *  重新报到（第 5 期起 2301 带上已登记的用途）。
 *
 *  典型用法（见 P2PModule.cpp）：
 *      m_service.SetEnabled(true);                      // 模块 Init 时
 *      service->RegisterHandler("folder", &handler);    // 业务方
 *      m_service.SetEnabled(false);                     // 模块 Shutdown 时，同时清空登记
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "../FlamingoP2PApi.h"

namespace p2p {

/**
 *  P2P 服务登记表的观察者：业务方登记或注销成功之后被调用（在调用 RegisterHandler / UnregisterHandler 的线程上，
 *  调用时不持有服务的互斥量）。模块对象据此让引擎重新报到。析构函数受保护：服务只借用它。
 */
class IP2PServiceObserver
{
public:
    //登记表有变化：新登记了一个用途，或注销了一个用途
    virtual void OnHandlersChanged() = 0;

protected:
    ~IP2PServiceObserver()
    {
    }
};

/**
 *  P2P 服务：IP2PService 的实现，由模块对象持有（进程内唯一）。
 */
class CP2PService : public IP2PService
{
public:
    CP2PService();

    /**
     *  打开或关闭服务。模块 Init 成功后打开，Shutdown 时关闭；关闭的同时清空全部登记。
     *  @param enabled true 表示打开，此后可以登记；false 表示关闭，此后登记一律失败
     */
    void SetEnabled(bool enabled);

    /**
     *  按用途查找已登记的业务方（第 3 期收到会话推送后使用）。
     *  @param purpose 用途名，规则同 RegisterHandler
     *  @return 已登记的业务方；没有登记、服务已关闭或参数非法时返回 NULL
     */
    IP2PLinkHandler* FindHandler(const char* purpose);

    /**
     *  列出已登记业务方的用途名（2301 报到时带给服务端）。
     *  @param purposes [出参] 用途名，按名字排序；服务已关闭时为空
     */
    void ListPurposes(std::vector<std::string>& purposes);

    /**
     *  设置登记表的观察者。模块 Init 时设置。
     *  @param observer 观察者（借用，须比本对象活得久）；NULL 表示不通知
     */
    void SetObserver(IP2PServiceObserver* observer);

    //以下为 IP2PService 的实现

    /**
     *  按用途登记业务方，见 IP2PService::RegisterHandler。
     *  @param purpose 用途名（ASCII，以 '\0' 结尾，1~kMaxPurposeChars 个字符）
     *  @param handler 回调接口（不转移所有权），不可为 NULL
     *  @return 登记成功返回 true；参数非法、该用途已有业务方或服务未打开时返回 false
     */
    virtual bool RegisterHandler(const char* purpose, IP2PLinkHandler* handler) override;

    /**
     *  注销一个用途的业务方，见 IP2PService::UnregisterHandler。
     *  @param purpose 用途名，规则同 RegisterHandler
     *  @return 注销成功返回 true；该用途没有登记或参数非法时返回 false
     */
    virtual bool UnregisterHandler(const char* purpose) override;

private:
    /**
     *  检查用途名是否合法：非空指针、长度 1~kMaxPurposeChars。
     *  @param purpose 用途名
     *  @return 合法返回 true
     */
    static bool IsValidPurpose(const char* purpose);

private:
    std::mutex                               m_mutex;      //保护以下三个成员；各方法都可能在任意线程上被调用
    bool                                     m_enabled;    //服务是否打开：模块 Init 成功后为 true，Shutdown 后为 false
    std::map<std::string, IP2PLinkHandler*>  m_handlers;   //用途名 → 业务方（不持有所有权，业务方在注销前保证有效）
    IP2PServiceObserver*                     m_observer;   //登记表的观察者（借用）；NULL 表示不通知
};

} // namespace p2p
