/**
 *  机器人模块对象 CBotModule：IBotModule 的实现（docs/机器人方案.md 8.2）。
 *
 *  职责：
 *    - 两种顶层窗口：每个群至多一个群机器人管理窗，全进程至多一个「我的机器人」窗口，经工厂创建；
 *    - 收帧：OnServerFrame 在网络线程上把帧拷贝后投递到隐藏的消息窗口（WM_APP + 0x1B0），在界面线程上由 ProcessFrame
 *      解析（BotCodec::ParseReply）后按发请求时登记的 seq 交回发起的窗口（CBotRequestBook）；
 *    - 选人：窗口经本对象请宿主弹出选人对话框，请求号登记到窗口名下，结果交回那个窗口；
 *    - 宿主数据：群机器人列表（2801 的缓存）经 IBotHost::GetGroupBotList 取来换成 Core 的 GroupBotList；
 *    - 断线重连（OnLoggedIn）：清掉 seq 登记，通知打开着的窗口重新加载；退出登录（OnLoggingOut）：关闭全部窗口；
 *    - 用户关闭窗口：等窗口的消息处理返回之后（投递 WM_APP + 0x1B1）再释放窗口对象。
 *
 *  进程内经 Instance() 取唯一对象；单元测试直接构造，设为无窗口模式后用 ProcessFrame 在测试线程上驱动。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../BotApi.h"
#include "../Core/BotRequestBook.h"
#include "BotWindowApi.h"

namespace flamingobot {

//消息窗口收到「有新帧」的私有消息号（开发约定文档的 WM_APP 占用表登记为 0x1B0），lParam 为堆上的帧，由消息窗口 delete
const UINT kMsgBotFrameArrived = WM_APP + 0x1B0;

//消息窗口收到「释放已关闭的窗口」的私有消息号（0x1B1）
const UINT kMsgBotReleaseWindows = WM_APP + 0x1B1;

/**
 *  CBotModule —— 机器人模块对象。
 */
class CBotModule : public IBotModule, public IBotWindowOwner
{
public:
    /**
     *  取进程内唯一的模块对象（DLL 导出函数返回它）。
     */
    static CBotModule& Instance();

    CBotModule();
    ~CBotModule();

    /**
     *  登记创建窗口的工厂（DllMain 的导出函数在返回模块对象之前调用）。
     *  @param factory 工厂（借用）；为 NULL 时打开窗口直接返回 false（单元测试）
     */
    void SetWindowFactory(IBotWindowFactory* factory);

    /**
     *  单元测试用：不建隐藏的消息窗口，帧由测试直接调用 ProcessFrame 交入；关闭的窗口在 ReleaseClosedWindows 时释放。
     *  须在 Init 之前调用。
     */
    void SetHeadlessForTest(bool headless);

    /**
     *  在界面线程上处理一帧（消息窗口取出帧后调用；单元测试直接调用）：解析后交回发起的窗口。
     */
    void ProcessFrame(uint16_t cmd, uint32_t seq, const std::string& body);

    //释放已被用户关闭的窗口对象（消息窗口收到 kMsgBotReleaseWindows 时调用；单元测试直接调用）
    void ReleaseClosedWindows();

    //登记着、还没收到应答的请求条数（测试用）
    size_t PendingRequestCount() const;

    //打开着（含已关闭待释放）的群机器人管理窗个数（测试用）
    size_t ManagerCount() const;

    //======== IBotModule ========
    virtual bool Init(IBotHost* host, const BotInitParams* params) override;
    virtual void Shutdown() override;
    virtual uint32_t GetApiVersion() const override;
    virtual void OnLoggedIn() override;
    virtual void OnLoggingOut() override;
    virtual void OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len) override;
    virtual bool OpenGroupBotManager(HWND owner, int64_t groupId, const wchar_t* groupName) override;
    virtual void CloseGroupBotManager(int64_t groupId) override;
    virtual bool OpenMyBots(HWND owner) override;
    virtual void OnFeaturesChanged(bool bot, bool groupBot, bool personal, bool groupExternal) override;
    virtual void OnGroupBotsChanged(int64_t groupId) override;
    virtual void OnContactsPicked(uint32_t requestId, const int64_t* userIds, uint32_t count) override;
    virtual void OnDepartmentsPicked(uint32_t requestId, const int64_t* deptIds, uint32_t count) override;

    //======== IBotWindowOwner ========
    virtual IBotHost* Host() override;
    virtual BotFeatures Features() const override;
    virtual int64_t MyUserId() override;
    virtual std::wstring UserName(int64_t userId) override;
    virtual std::wstring DeptName(int64_t deptId) override;
    virtual uint32_t RegisterClient(IBotReplySink* sink) override;
    virtual void UnregisterClient(uint32_t clientId) override;
    virtual uint32_t SendCommand(uint32_t clientId, uint16_t cmd, const std::string& body) override;
    virtual bool GetGroupBots(int64_t groupId, GroupBotList& out) override;
    virtual void RequestGroupBots(int64_t groupId) override;
    virtual uint32_t PickContacts(uint32_t clientId, HWND owner, const std::vector<int64_t>& preselected) override;
    virtual uint32_t PickDepartments(uint32_t clientId, HWND owner, const std::vector<int64_t>& preselected) override;
    virtual void OnTopWindowClosed(int kind, int64_t groupId) override;

private:
    //登记一个选择请求（选人或选部门）：分配请求号并记在发起方名下
    uint32_t BeginPick(uint32_t clientId);

    /**
     *  选择对话框确认了：按请求号取出发起方并整理结果（去掉不大于 0 的 id）。请求号只认一次。
     *    what：日志里的种类，如 "contacts"、"departments"。
     *    out： [出参] 整理后的 id。
     *  @return 发起方；请求号已经作废或发起方已撤销时返回 NULL
     */
    IBotReplySink* TakePick(uint32_t requestId, const int64_t* ids, uint32_t count, const char* what,
                            std::vector<int64_t>& out);

    //一个顶层窗口
    struct TopEntry
    {
        std::unique_ptr<IBotTopWindow> m_window;  //窗口对象
        bool                           m_closed;  //窗口已被用户关闭，等待释放

        TopEntry()
            : m_closed(false)
        {
        }
    };

    //网络线程投递到界面线程的一帧
    struct FramePacket
    {
        uint16_t    m_cmd;   //命令号
        uint32_t    m_seq;   //帧 seq
        std::string m_body;  //包体
    };

    typedef std::map<int64_t, std::shared_ptr<TopEntry> > ManagerMap;

    //关闭并释放全部窗口（退出登录、关闭模块）
    void CloseAll();

    //投递一条释放消息（无窗口模式下不投递，由测试调用 ReleaseClosedWindows）
    void ScheduleRelease();

    //消息窗口
    bool CreateMessageWindow();
    void DestroyMessageWindow();
    static LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    IBotHost*                         m_host;          //宿主接口（借用）；Init 前与 Shutdown 后为 NULL
    IBotWindowFactory*                m_factory;       //窗口工厂（借用）；为 NULL 时不创建窗口
    bool                              m_initialized;   //是否已初始化
    bool                              m_headless;      //测试模式：不建消息窗口
    bool                              m_loggedIn;      //是否处于已登录状态
    BotFeatures                       m_features;      //企业开关；默认按服务端的缺省值（外部服务默认关）
    HWND                              m_hwnd;          //隐藏的消息窗口；测试模式下为 NULL
    mutable std::mutex                m_wndMutex;      //保护 m_postTarget：网络线程读，界面线程写
    HWND                              m_postTarget;    //网络线程投递帧的目标；未初始化或已关闭时为 NULL
    ManagerMap                        m_managers;      //群机器人管理窗：群号 → 窗口
    std::shared_ptr<TopEntry>         m_myBots;        //「我的机器人」窗口；没有打开过时为空
    CBotRequestBook                   m_book;          //未收到应答的请求：seq → 命令号与发起方
    std::map<uint32_t, IBotReplySink*> m_clients;      //应答接收方：发起方编号 → 接收方（借用）
    uint32_t                          m_nextClientId;  //下一个发起方编号
    std::map<uint32_t, uint32_t>      m_picks;         //未确认的选人请求：请求号 → 发起方编号
    uint32_t                          m_nextPickId;    //下一个选人请求号
};

} // namespace flamingobot
