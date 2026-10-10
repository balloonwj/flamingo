/**
 *  主程序一侧的机器人管理界面接入：实现交给 FlamingoBot.dll 的宿主接口 IBotHost，负责按企业开关加载、初始化、通知与卸载
 *  这个 DLL，并把 2802～2809 的帧转给它（docs/机器人方案.md 8.2）。2801、2810、2811、2820 仍由 exe 自己处理。
 *
 *  生命周期（只在 UI 线程上调用）：
 *    - 登录成功（含断线重连）：SetFeatures 之后 OnLoginSucceeded —— 机器人总开关打开时，本进程首次调用加载 DLL 并 Init，
 *      之后每次通知模块 OnLoggedIn 与当前的开关。DLL 不存在、版本不符或 Init 失败时只记一条日志，本进程不再重试，客户端照常
 *      运行，只是「管理」「我的机器人」入口隐藏（机器人消息的显示在 exe 里，不受影响）；
 *    - 运行中收到 1013：再调 SetFeatures —— 总开关由关变开时补加载并让模块进入已登录状态；由开变关时模块关闭全部窗口
 *      （DLL 保持加载）；其余开关变化转给模块，打开着的窗口据此刷新。返回值告诉调用方入口的显隐是否因此变化；
 *    - 退出登录：OnLogout —— 模块关闭全部窗口；DLL 保持加载；
 *    - 进程退出：Shutdown —— 先撤销帧回调（CIUSocket::SetBotFrameSink），再关闭模块、卸载 DLL。
 *
 *  主程序的数据与能力经 BotHostCallbacks 取（主面板登记函数指针）：某个群最近一次的 2801 结果、请主面板重拉 2801、
 *  用户的显示名、弹出选择联系人对话框。没有登记时宿主按缺省回答（列表取不到、显示名为「用户<id>」、选人对话框弹不出）。
 *
 *  与界面入口桥接 Feature/Bot/BotUiBridge（botui）的关系：群聊窗、主面板、系统设置只调用 botui，不直接依赖本类。
 *  本类在构造时登记 botui 的开关变化与群机器人变化回调（botui::SetNotifyHooks → SetFeatures / OnGroupBotsChanged），
 *  模块就绪后登记可用判断与两个打开函数（botui::SetHooks → IsAvailable / OpenGroupBotManager / OpenMyBots），
 *  Shutdown 时全部清除。
 *
 *  线程：Log、Translate、SendBotCommand 可能在 DLL 的任意线程上被调用，帧回调在网络接收线程上被调用，均线程安全；
 *  其余方法只在 UI 线程上调用。
 *
 *  典型用法（主面板）：
 *      balloonbot::BotHostCallbacks cb;
 *      cb.m_ctx = this;
 *      cb.m_queryGroupBots = &CBalloonMainPanel::QueryGroupBotsThunk;
 *      balloonbot::CBotHost& host = balloonbot::CBotHost::GetInstance();
 *      host.SetCallbacks(cb);
 *      host.SetFeatures(userMgr.m_bBotFeature, userMgr.m_bBotGroupFeature, userMgr.m_bBotPersonalFeature,
 *                       userMgr.m_bBotGroupExternalFeature);
 *      host.OnLoginSucceeded(userId, nickname);
 *      ...
 *      host.OpenGroupBotManager(hwndChat, groupId, groupName);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <mutex>
#include <string>
#include <vector>

#include "../../App/PluginModuleLoader.h"
#include "../../UI/BalloonMain/CreateGroupDlg.h"   // IMemberPickSink（选择联系人对话框的结果）
#include "../../UI/BalloonMain/DeptPickDialog.h"   // balloonmain::IDeptPickSink（选择部门对话框的结果）
#include "../../net/BotProtocolClient.h"           // net::GroupBotsResult
#include "../../../FlamingoBot/BotApi.h"

namespace balloonbot {

/**
 *  取 exe 缓存的某个群最近一次 2801 结果（群聊窗打开时拉取、收到 2820 时重拉的那一份）。
 *  @param ctx     登记时传入的上下文（主面板）
 *  @param groupId 群号
 *  @param out     [出参] 2801 结果
 *  @return 有这个群的结果时返回 true；还没有拉到时返回 false（宿主随后调 BotGroupListRefresh）
 */
typedef bool (*BotGroupListQuery)(void* ctx, int64_t groupId, net::GroupBotsResult& out);

/**
 *  请 exe 重新拉取某个群的机器人（发 2801）。应答到达、缓存更新之后，exe 调 CBotHost::OnGroupBotsChanged(groupId)。
 */
typedef void (*BotGroupListRefresh)(void* ctx, int64_t groupId);

/**
 *  一个用户的显示名（通讯录显示名、昵称依次取第一个非空的）。
 *  @return 显示名；不认识的用户返回空串（宿主再换成「用户<id>」）
 */
typedef std::wstring (*BotUserNameQuery)(void* ctx, int64_t userId);

/**
 *  弹出选择联系人对话框（多选，个人机器人「指定的人」用）：建议复用 CCreateGroupDlg::OpenForPick，
 *  先 SetInitialSelection(preselected)、SetPickSink(sink)，候选为通讯录。确认后对话框调 sink->OnMembersPicked。
 *  @param owner       发起的窗口（规则编辑窗），可为 NULL
 *  @param preselected 预先勾选的人
 *  @param sink        结果接收方（就是 CBotHost 本身，进程内常驻）
 *  @return 已弹出返回 true；未登录、离线或通讯录还没加载时返回 false
 */
typedef bool (*BotContactPicker)(void* ctx, HWND owner, const std::vector<int64_t>& preselected, IMemberPickSink* sink);

/**
 *  取一个部门的名称（主面板缓存的组织架构）。
 *  @return 部门名称；部门不存在或组织架构还没加载时返回空串
 */
typedef std::wstring (*BotDeptNameQuery)(void* ctx, int64_t deptId);

/**
 *  弹出选择部门对话框（主面板持有的 CDeptPickDialog），个人机器人「指定的部门」用。确认后对话框调 sink->OnDepartmentsPicked。
 *  @param owner       发起的窗口（规则编辑窗），可为 NULL
 *  @param preselected 预先勾选的部门
 *  @param sink        结果接收方（就是 CBotHost 本身，进程内常驻）
 *  @return 已弹出返回 true；组织架构还没加载时返回 false
 */
typedef bool (*BotDeptPicker)(void* ctx, HWND owner, const std::vector<int64_t>& preselected,
                              balloonmain::IDeptPickSink* sink);

/**
 *  主面板登记给宿主的回调。任何一项可为 NULL，宿主按缺省回答。
 */
struct BotHostCallbacks
{
    void*               m_ctx;              //回调的上下文（主面板），不拥有
    BotGroupListQuery   m_queryGroupBots;   //取某个群的 2801 结果
    BotGroupListRefresh m_refreshGroupBots; //请 exe 重拉 2801；为 NULL 时宿主直接经 CIUSocket::SendGetGroupBots 发出
    BotUserNameQuery    m_userName;         //用户的显示名
    BotContactPicker    m_pickContacts;     //弹出选择联系人对话框
    BotDeptNameQuery    m_deptName;         //部门名称
    BotDeptPicker       m_pickDepartments;  //弹出选择部门对话框

    BotHostCallbacks()
        : m_ctx(NULL)
        , m_queryGroupBots(NULL)
        , m_refreshGroupBots(NULL)
        , m_userName(NULL)
        , m_pickContacts(NULL)
        , m_deptName(NULL)
        , m_pickDepartments(NULL)
    {
    }
};

/**
 *  CBotHost —— 机器人管理界面模块的宿主（进程内唯一）。
 */
class CBotHost : public flamingobot::IBotHost, public IMemberPickSink, public balloonmain::IDeptPickSink
{
public:
    /**
     *  取进程内唯一的宿主对象。
     */
    static CBotHost& GetInstance();

    /**
     *  登记主面板的回调。登录成功处（OnLoginSucceeded 之前）调用；传默认构造的结构体表示解除。
     */
    void SetCallbacks(const BotHostCallbacks& callbacks);

    /**
     *  记下企业开关（登录结果的 features 与运行中的 1013 推送都调用它）。已登录时立即生效：总开关由关变开时必要时加载 DLL、
     *  让模块进入已登录状态；由开变关时模块关闭全部窗口（DLL 保持加载）；其余开关转给模块。
     *  @param bot           features.bot（机器人总开关；老服务端没有这个字段时按关闭）
     *  @param groupBot      features.botgroup（允许群主添加群机器人）
     *  @param personal      features.botpersonal（允许员工配置个人机器人）
     *  @param groupExternal features.botgroupexternal（允许群主给群机器人配置外部服务）
     *  @return IsAvailable 或 IsMyBotsAvailable 的结果是否因此改变（调用方据此刷新「管理」「我的机器人」入口）
     */
    bool SetFeatures(bool bot, bool groupBot, bool personal, bool groupExternal);

    /**
     *  登录成功（含断线重连成功）时调用：总开关打开时确保 DLL 已加载，并通知模块已登录与当前的开关。
     *  @param myUserId 本人 userid
     *  @param myName   本人的显示名（昵称）
     *  副作用：首次需要时加载 FlamingoBot.dll 并登记帧回调；加载或初始化失败时写一条日志，本进程不再重试。
     */
    void OnLoginSucceeded(int64_t myUserId, const std::wstring& myName);

    /**
     *  即将退出登录时调用：模块关闭全部窗口；DLL 保持加载。
     */
    void OnLogout();

    /**
     *  进程退出时调用：撤销帧回调、关闭模块、卸载 DLL。未加载时与重复调用都安全。
     *  须在主面板销毁之前（模块关闭时还要销毁自己的窗口）、网络层反初始化之前调用。
     */
    void Shutdown();

    /**
     *  机器人管理界面是否可用：已登录、DLL 已加载并初始化、企业总开关打开。不可用时「管理」「我的机器人」入口隐藏。
     */
    bool IsAvailable() const;

    /**
     *  「我的机器人」入口是否显示：IsAvailable 且企业允许个人机器人。
     */
    bool IsMyBotsAvailable() const;

    /**
     *  打开（或激活）某个群的群机器人管理窗（群主在群聊窗成员页签「机器人」分组标题右侧点「管理」时）。
     *  @param owner     所有者窗口（群聊窗）
     *  @param groupId   群号
     *  @param groupName 群名（窗口标题用）
     *  @return 功能可用且窗口已显示返回 true
     */
    bool OpenGroupBotManager(HWND owner, int64_t groupId, const std::wstring& groupName);

    /**
     *  关闭某个群的管理窗（本人退群、被移出、群被解散、群主转让给了别人时）；功能不可用时什么也不做。
     */
    void CloseGroupBotManager(int64_t groupId);

    /**
     *  打开（或激活）「我的机器人」窗口（主面板系统菜单「我的机器人」、系统设置「聊天」页的链接）。
     *  @param owner 所有者窗口（主面板或设置窗）
     *  @return 功能可用且窗口已显示返回 true
     */
    bool OpenMyBots(HWND owner);

    /**
     *  某个群的机器人列表变了：exe 收到该群新的 2801 应答、更新缓存之后调用（不论应答是谁请求的）。
     *  打开着的管理窗随之重读列表并重拉详情。
     *  @param groupId 群号；0 表示全部群
     */
    void OnGroupBotsChanged(int64_t groupId);

    //======== flamingobot::IBotHost（语义见 FlamingoBot/BotApi.h） ========
    virtual void Log(int level, const char* text) override;
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) override;
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) override;
    virtual uint32_t SendBotCommand(uint16_t cmd, const char* json, uint32_t len) override;
    virtual int64_t GetMyUserId() override;
    virtual bool GetMyDisplayName(wchar_t* buf, uint32_t chars) override;
    virtual bool GetUserDisplayName(int64_t userId, wchar_t* buf, uint32_t chars) override;
    virtual bool GetGroupBotList(int64_t groupId, flamingobot::BotGroupCapsInfo* caps, flamingobot::BotGroupItemInfo* items,
                                 uint32_t capacity, uint32_t* total) override;
    virtual void RequestGroupBots(int64_t groupId) override;
    virtual bool PickContacts(HWND owner, uint32_t requestId, const int64_t* preselected, uint32_t count) override;
    virtual bool GetDepartmentName(int64_t deptId, wchar_t* buf, uint32_t chars) override;
    virtual bool PickDepartments(HWND owner, uint32_t requestId, const int64_t* preselected, uint32_t count) override;

    //IMemberPickSink：选择联系人对话框确认了，转给模块
    virtual void OnMembersPicked(const std::vector<int64_t>& userIds) override;

    //balloonmain::IDeptPickSink：选择部门对话框确认了，转给模块
    virtual void OnDepartmentsPicked(const std::vector<int64_t>& deptIds) override;

private:
    CBotHost();
    ~CBotHost();
    CBotHost(const CBotHost&);
    CBotHost& operator=(const CBotHost&);

    //CIUSocket::SetBotFrameSink 登记的回调（网络接收线程）：持锁转给模块
    static void FrameSinkThunk(void* ctx, uint16_t cmd, uint32_t seq, const std::string& body);

    //登记给 botui 的回调：转给进程内唯一的宿主对象
    static bool BridgeIsAvailable();
    static void BridgeOpenGroupManager(HWND owner, int64_t groupId, const wchar_t* groupName);
    static void BridgeOpenMyBots(HWND owner);
    static void BridgeFeatures(bool bot, bool groupBot, bool personal, bool groupExternal);
    static void BridgeGroupBotsChanged(int64_t groupId);

    //首次需要时加载并初始化 DLL、登记帧回调；本进程只尝试一次。返回是否就绪
    bool EnsureModuleReady();

    //让模块进入已登录状态（通知 OnLoggedIn 与当前的开关）
    void ActivateModule();

    //让模块退出已登录状态（通知 OnLoggingOut），DLL 保持加载
    void DeactivateModule();

private:
    balloonapp::CPluginModuleLoader<flamingobot::IBotModule> m_loader;   //DLL 加载器；Shutdown 时卸载
    bool                 m_loadAttempted;    //本进程是否已经尝试过加载（无论成败，只尝试一次）
    bool                 m_ready;            //DLL 已加载且 Init 成功；Shutdown 后为 false
    bool                 m_loggedIn;         //是否处于已登录状态：OnLoginSucceeded 置真，OnLogout 置假
    bool                 m_moduleActive;     //模块是否处于已登录状态：通知过 OnLoggedIn 且之后没有通知 OnLoggingOut
    bool                 m_botFeature;       //企业开关 features.bot；默认关闭（老服务端没有这个字段）
    bool                 m_groupBotFeature;  //企业开关 features.botgroup
    bool                 m_personalFeature;  //企业开关 features.botpersonal
    bool                 m_externalFeature;  //企业开关 features.botgroupexternal
    int64_t              m_myUserId;         //本人 userid；未登录时为 0
    std::wstring         m_myName;           //本人的显示名；未登录时为空
    BotHostCallbacks     m_callbacks;        //主面板登记的回调
    uint32_t             m_pendingPickId;    //正在等待的选人请求号（模块分配）；没有时为 0
    uint32_t             m_pendingDeptPickId; //正在等待的选部门请求号（模块分配）；没有时为 0
    mutable std::mutex   m_routeMutex;       //保护 m_routeTarget：网络线程读，界面线程写
    flamingobot::IBotModule* m_routeTarget;  //网络线程转发帧的目标；未就绪或已卸载时为 NULL
};

} // namespace balloonbot
