/**
 *  模块对象与窗口之间的接口（DLL 内部）。
 *
 *  模块对象（Host/BotModule）只管路由与生命周期，不直接依赖界面代码：两种顶层窗口（群机器人管理窗、「我的机器人」窗口）
 *  经 IBotWindowFactory 创建，以 IBotTopWindow 的形式交给模块；窗口反过来经 IBotWindowOwner 向模块发命令、取宿主数据。
 *  机器人编辑窗与规则编辑窗由顶层窗口自己持有，模块不感知。
 *
 *  发命令的窗口先经 RegisterClient 登记一个应答接收方（IBotReplySink），拿到发起方编号；之后经 SendCommand 发出的请求，
 *  应答到达时模块按 seq 交回这个接收方。窗口关闭前 UnregisterClient，之后到达的应答直接丢弃。
 *
 *  这样单元测试可以只编译模块与 Core（不带 balloonui），用假的工厂与假的接收方验证路由。真正的工厂在
 *  UI/BotUiFactory.cpp，由 DllMain.cpp 的导出函数登记给模块。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "../BotApi.h"
#include "../Core/BotCodec.h"
#include "../Core/BotTypes.h"

namespace flamingobot {

//顶层窗口的种类
enum BotTopWindowKind
{
    //某个群的群机器人管理窗（每个群至多一个）
    bot_window_group_manager = 1,
    //「我的机器人」窗口（全进程至多一个）
    bot_window_my_bots       = 2,
};

/**
 *  应答接收方（窗口实现）。只在界面线程上调用。
 */
class IBotReplySink
{
public:
    /**
     *  经 SendCommand 发出的请求有了应答。
     *  @param reply 解析好的应答（m_parsed 为 false 表示包体解析不出，按失败处理）
     */
    virtual void OnBotReply(const BotReply& reply) = 0;

    /**
     *  经 PickContacts 弹出的选人对话框确认了。
     *  @param requestId PickContacts 返回的请求号
     *  @param userIds   已选的人
     */
    virtual void OnContactsPicked(uint32_t requestId, const std::vector<int64_t>& userIds) = 0;

    /**
     *  经 PickDepartments 弹出的选择部门对话框确认了。只有规则编辑窗会发起选部门，其余窗口沿用这个空的默认实现。
     *  @param requestId PickDepartments 返回的请求号
     *  @param deptIds   已选的部门（宿主已去掉被上级包含的下级部门）
     */
    virtual void OnDepartmentsPicked(uint32_t requestId, const std::vector<int64_t>& deptIds)
    {
        (void)requestId;
        (void)deptIds;
    }

protected:
    ~IBotReplySink()
    {
    }
};

/**
 *  窗口向模块发命令、取数据的接口（模块实现）。只在界面线程上调用。
 */
class IBotWindowOwner
{
public:
    //宿主接口；模块关闭后为 NULL
    virtual IBotHost* Host() = 0;

    //当前的企业开关
    virtual BotFeatures Features() const = 0;

    //本人 userid；未登录时为 0
    virtual int64_t MyUserId() = 0;

    //一个用户的显示名；宿主取不到时为「用户<id>」
    virtual std::wstring UserName(int64_t userId) = 0;

    //一个部门的名称；宿主取不到时（部门已删除、组织架构还没加载）为「部门<id>」
    virtual std::wstring DeptName(int64_t deptId) = 0;

    /**
     *  登记一个应答接收方。
     *  @param sink 接收方（借用，UnregisterClient 之前须一直有效）
     *  @return 发起方编号（大于 0）
     */
    virtual uint32_t RegisterClient(IBotReplySink* sink) = 0;

    //撤销应答接收方：它名下未收到应答的请求作废，选人请求也作废
    virtual void UnregisterClient(uint32_t clientId) = 0;

    /**
     *  经宿主发一条 2802～2809 命令，并按 seq 登记到发起方名下。
     *  @param clientId 发起方编号
     *  @param cmd      命令号
     *  @param body     包体（UTF-8 JSON）
     *  @return 帧 seq；没能发出（没有连上服务器、模块已关闭）时返回 0
     */
    virtual uint32_t SendCommand(uint32_t clientId, uint16_t cmd, const std::string& body) = 0;

    /**
     *  取宿主缓存的某个群的机器人列表（最近一次 2801 的结果）。
     *  @param out [出参] 列表
     *  @return 宿主有这个群的结果时返回 true
     */
    virtual bool GetGroupBots(int64_t groupId, GroupBotList& out) = 0;

    //请宿主重新拉取某个群的机器人（2801）；结果到达后模块调 IBotTopWindow::OnGroupBotsChanged
    virtual void RequestGroupBots(int64_t groupId) = 0;

    /**
     *  弹出宿主的选择联系人对话框。确认后结果经 IBotReplySink::OnContactsPicked 交回发起方。
     *  @param clientId    发起方编号
     *  @param owner       发起的窗口
     *  @param preselected 预先勾选的人
     *  @return 请求号（大于 0）；宿主没能弹出时返回 0
     */
    virtual uint32_t PickContacts(uint32_t clientId, HWND owner, const std::vector<int64_t>& preselected) = 0;

    /**
     *  弹出宿主的选择部门对话框（个人机器人规则「发送者 ▸ 指定的部门」）。确认后结果经 IBotReplySink::OnDepartmentsPicked
     *  交回发起方。请求号与选人共用一个序列，同一时刻宿主只有一个选择对话框。
     *  @param clientId    发起方编号
     *  @param owner       发起的窗口
     *  @param preselected 预先勾选的部门
     *  @return 请求号（大于 0）；宿主没能弹出时返回 0
     */
    virtual uint32_t PickDepartments(uint32_t clientId, HWND owner, const std::vector<int64_t>& preselected) = 0;

    /**
     *  顶层窗口已经被用户关闭（窗口已销毁）：模块稍后释放窗口对象。
     *  @param kind    窗口种类，取值见 BotTopWindowKind
     *  @param groupId 群机器人管理窗的群号；「我的机器人」窗口为 0
     */
    virtual void OnTopWindowClosed(int kind, int64_t groupId) = 0;

protected:
    ~IBotWindowOwner()
    {
    }
};

/**
 *  一个顶层窗口（界面实现）。只在界面线程上调用。
 */
class IBotTopWindow
{
public:
    virtual ~IBotTopWindow()
    {
    }

    /**
     *  显示（首次调用时创建）并激活窗口。
     *  @param owner 所有者窗口，可为 NULL
     *  @return 窗口已显示返回 true
     */
    virtual bool Open(HWND owner) = 0;

    //销毁窗口（不再回调 OnTopWindowClosed），连同它打开的编辑窗
    virtual void Close() = 0;

    //群机器人管理窗：群名变了（再次打开时宿主传来的群名），刷新标题；其余窗口忽略
    virtual void SetGroupName(const std::wstring& groupName) = 0;

    //群机器人管理窗：本群的机器人列表变了（宿主有了新的 2801 结果），重读列表并重拉详情；其余窗口忽略
    virtual void OnGroupBotsChanged() = 0;

    //企业开关变了：刷新可用的操作
    virtual void OnFeaturesChanged() = 0;

    //断线重连成功：未收到应答的请求已作废，重新加载
    virtual void OnReconnected() = 0;
};

/**
 *  创建顶层窗口的工厂（界面实现，DllMain 登记给模块）。
 */
class IBotWindowFactory
{
public:
    /**
     *  模块初始化时调用一次：界面库的多语言与字体（i18n::InitFromHost、balloonui 的默认字体与内置文字）。
     *  @return 成功返回 true
     */
    virtual bool InitUi(IBotHost* host) = 0;

    //模块关闭时调用一次：解除界面库与宿主的关联
    virtual void ShutdownUi() = 0;

    /**
     *  新建一个群机器人管理窗对象（Open 时才创建窗口）。
     *  @param groupId   群号
     *  @param groupName 群名（标题用），可为空
     *  @param owner     模块（借用）
     *  @return 窗口对象，所有权交给模块（用 delete 释放）
     */
    virtual IBotTopWindow* CreateGroupManager(int64_t groupId, const std::wstring& groupName, IBotWindowOwner* owner) = 0;

    /**
     *  新建「我的机器人」窗口对象（Open 时才创建窗口）。
     *  @param owner 模块（借用）
     *  @return 窗口对象，所有权交给模块（用 delete 释放）
     */
    virtual IBotTopWindow* CreateMyBots(IBotWindowOwner* owner) = 0;

protected:
    ~IBotWindowFactory()
    {
    }
};

} // namespace flamingobot
