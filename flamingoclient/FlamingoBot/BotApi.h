/**
 *  FlamingoBot.dll 与宿主 Flamingo.exe 之间的接口约定（机器人的管理界面，docs/机器人方案.md 8.2）。
 *
 *  本 DLL 提供三个窗口：群机器人管理窗（群主管理本群的群机器人、在本群关闭 / 打开企业机器人）、机器人编辑窗与规则编辑窗
 *  （群机器人与个人机器人共用）、「我的机器人」窗口（本人的个人机器人）。收发 2802～2809（cmd_get_bot_detail ~ cmd_try_bot），
 *  界面用 DLL 自己静态链接的一份 balloonui。exe 在登录成功、且企业开关 features.bot 打开时用 LoadLibrary 动态加载它；
 *  DLL 不存在或加载失败时 exe 照常运行，只是隐藏「管理」「我的机器人」入口，机器人消息的显示不受影响（显示在 exe 里）。
 *
 *  2801（本群可用的机器人）与 2820（本群机器人变了）由 exe 处理：exe 的群聊窗缓存 2801 的结果，本 DLL 经
 *  IBotHost::GetGroupBotList 读取，经 IBotHost::RequestGroupBots 请 exe 重拉；exe 收到新的 2801 应答后调
 *  IBotModule::OnGroupBotsChanged，打开着的管理窗随之刷新。
 *
 *  线程约定：OnServerFrame 在 exe 的网络接收线程上调用，DLL 拷贝后投递到自己的隐藏消息窗口；其余方法与回调都在界面线程。
 *
 *  两边都静态链接 CRT、各有独立的堆，因此接口只使用纯虚类、基本类型与固定布局的结构体：字符串一律由调用方持有、
 *  只在本次调用期间有效；不传递 STL 对象；不在一侧分配、另一侧释放。
 *
 *  修改规则：任何一侧增删接口方法、改动参数或结构体布局，都必须把 kApiVersion 加 1；加载时双方版本号必须完全相同。
 *
 *  典型用法（exe 一侧，见 Source/Feature/Bot/BotHost.cpp）：
 *      flamingobot::IBotModule* module = factory(flamingobot::kApiVersion);
 *      module->Init(&host, &params);
 *      module->OnLoggedIn();
 *      module->OpenGroupBotManager(hwndChat, groupId, L"项目群");
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <windows.h>
#include <stdint.h>

namespace flamingobot {

//接口版本号；加载时宿主与 DLL 必须完全相同。1：2026-10-08 第一版；
//2：2026-10-08 加选部门（IBotHost::GetDepartmentName / PickDepartments、IBotModule::OnDepartmentsPicked）
const uint32_t kApiVersion = 2;

//本 DLL 收发的命令号段（Protocol.h 的 cmd_get_bot_detail 2802 ~ cmd_try_bot 2809）；2801、2810、2811、2820 由 exe 处理
const uint16_t kBotCmdFirst = 2802;
const uint16_t kBotCmdLast = 2809;

//结构体里文字字段的容量（UTF-16 字符数，含结尾 0）。名称最多 20 字、简介最多 100 字（服务端 BotDefs.h），留出余量
const uint32_t kBotNameChars = 64;
const uint32_t kBotDescChars = 160;
const uint32_t kBotGlyphChars = 8;
const uint32_t kBotColorChars = 16;

//DLL 文件名与导出的工厂函数名
#define FLAMINGO_BOT_DLL_NAME       L"FlamingoBot.dll"
#define FLAMINGO_BOT_FACTORY_NAME   "FlamingoBot_Create"

//DLL 经 IBotHost::Log 写日志时的级别
enum BotLogLevel
{
    bot_log_info    = 1,   //普通信息
    bot_log_warning = 2,   //警告：可以继续运行的异常情况
    bot_log_error   = 3,   //错误：某项操作无法继续
};

//IBotModule::Init 的参数
struct BotInitParams
{
    uint32_t       m_cbSize;   //sizeof(BotInitParams)
    const wchar_t* m_appDir;   //exe 所在目录（不带结尾反斜杠）
};

//某个群的机器人能力说明（2801 应答的 caps，IBotHost::GetGroupBotList 的出参），DLL 分配并填好 m_cbSize
struct BotGroupCapsInfo
{
    uint32_t m_cbSize;           //sizeof(BotGroupCapsInfo)
    bool     m_isOwner;          //本人是这个群的群主（只有群主能管理群机器人）
    bool     m_groupBotAllowed;  //企业允许群主添加群机器人（且机器人总开关打开）
    bool     m_externalAllowed;  //企业允许群主给群机器人配置外部服务（外部系统推送与呼叫外部服务）
    int      m_groupBotMax;      //每群最多群机器人数（服务端给出，目前为 5）
};

//某个群可用的一个机器人（2801 应答 bots 数组的一项，IBotHost::GetGroupBotList 的出参数组元素），DLL 分配并填好 m_cbSize
struct BotGroupItemInfo
{
    uint32_t m_cbSize;                  //sizeof(BotGroupItemInfo)
    int64_t  m_botId;                   //机器人 ID
    int64_t  m_userId;                  //机器人在群里发消息用的账号 userid
    int      m_ownerType;               //归属：1 企业机器人、2 群机器人
    bool     m_isGroupBot;              //来源是本群（2801 的 source 为 "group"）；false 表示来自企业
    wchar_t  m_name[kBotNameChars];     //名称
    wchar_t  m_desc[kBotDescChars];     //简介
    wchar_t  m_glyph[kBotGlyphChars];   //头像字形；空串表示取名称的第一个字
    wchar_t  m_color[kBotColorChars];   //头像底色 "#RRGGBB"；空串表示默认色
    int      m_status;                  //状态：0 启用、1 停用
    bool     m_off;                     //企业机器人在本群被关闭
    bool     m_active;                  //在本群生效（启用且没有关闭）
    bool     m_canOff;                  //群主能否在本群关闭它（群机器人恒为 true，表示停用 / 启用）
    bool     m_atable;                  //能否被 @
};

/**
 *  判断一个命令号是否由本 DLL 收发（2802～2809）。
 */
inline bool IsBotDllCommand(uint32_t cmd)
{
    return cmd >= kBotCmdFirst && cmd <= kBotCmdLast;
}

/**
 *  判断宿主与 DLL 的接口版本是否兼容：必须完全相同。
 */
inline bool IsApiVersionCompatible(uint32_t hostVersion, uint32_t dllVersion)
{
    return hostVersion == dllVersion;
}

/**
 *  exe 实现、交给 DLL 的宿主接口。除注明外只在界面线程上调用。
 */
class IBotHost
{
public:
    /**
     *  写一行日志（任意线程）。
     *  @param level 级别，取 BotLogLevel
     *  @param text  UTF-8 文本，以 '\0' 结尾
     */
    virtual void Log(int level, const char* text) = 0;

    /**
     *  界面文字翻译（i18n::InitFromHost 经它取 exe 当前语言的译文）。可能在任意线程调用。
     *  @param context 上下文，可为 NULL
     *  @param source  简体中文原文
     *  @return 译文；没有译文时返回 source 本身。返回的指针由 exe 持有，进程内一直有效
     */
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) = 0;

    /**
     *  exe 当前的界面语言与界面字体。
     *  @param langBuf  [出参] 语言代码，如 "zh-CN"
     *  @param fontBuf  [出参] 界面字体名
     *  @return 已填写返回 true
     */
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) = 0;

    /**
     *  经主连接发送一条 2802～2809 命令（转调 CIUSocket::SendBotCommand）。可在任意线程调用。
     *  @param cmd  命令号
     *  @param json 包体（UTF-8 JSON），只在本次调用期间有效
     *  @param len  包体字节数
     *  @return 已放入发送队列时返回帧 seq（大于 0），DLL 据此对上应答；不放行（号段不对、包体为空、没有连上）时返回 0
     */
    virtual uint32_t SendBotCommand(uint16_t cmd, const char* json, uint32_t len) = 0;

    //本人 userid；未登录时为 0
    virtual int64_t GetMyUserId() = 0;

    /**
     *  本人的显示名（昵称）。
     *  @return 已填写返回 true
     */
    virtual bool GetMyDisplayName(wchar_t* buf, uint32_t chars) = 0;

    /**
     *  一个用户的显示名（通讯录显示名、昵称依次取第一个非空的）；个人机器人「指定的人」显示名字时用。
     *  @return 已填写返回 true；不认识的用户也填「用户<id>」并返回 true
     */
    virtual bool GetUserDisplayName(int64_t userId, wchar_t* buf, uint32_t chars) = 0;

    /**
     *  取 exe 缓存的某个群最近一次 2801 应答：能力说明与本群可用的机器人。
     *  @param groupId  群号
     *  @param caps     [出参] 能力说明，DLL 分配，m_cbSize 须等于 sizeof(BotGroupCapsInfo)
     *  @param items    [出参] 机器人数组，DLL 分配，每项的 m_cbSize 须等于 sizeof(BotGroupItemInfo)；可为 NULL（只取个数）
     *  @param capacity items 的元素个数
     *  @param total    [出参] 本群机器人的实际个数（可能大于 capacity，此时只填前 capacity 个）
     *  @return exe 有这个群的 2801 结果时返回 true；还没有拉到时返回 false（DLL 随后调 RequestGroupBots）
     */
    virtual bool GetGroupBotList(int64_t groupId, BotGroupCapsInfo* caps, BotGroupItemInfo* items, uint32_t capacity,
                                 uint32_t* total) = 0;

    /**
     *  请 exe 重新拉取某个群的机器人（发 2801）。应答到达、缓存更新后 exe 调 IBotModule::OnGroupBotsChanged(groupId)。
     */
    virtual void RequestGroupBots(int64_t groupId) = 0;

    /**
     *  弹出 exe 现成的选择联系人对话框（多选），个人机器人「指定的人」用。用户确认后 exe 调
     *  IBotModule::OnContactsPicked(requestId, ...)；用户取消时不回调。
     *  @param owner       发起的窗口，可为 NULL
     *  @param requestId   DLL 自己分配的请求号，原样带回
     *  @param preselected 预先勾选的人（userid 数组），只在本次调用期间有效；可为 NULL
     *  @param count       预先勾选的人数
     *  @return 已弹出返回 true；exe 没有提供选人对话框、未登录或通讯录还没加载时返回 false
     */
    virtual bool PickContacts(HWND owner, uint32_t requestId, const int64_t* preselected, uint32_t count) = 0;

    /**
     *  一个部门的名称（个人机器人「指定的部门」显示名字时用，接口版本 2 起）。
     *  @return 已填写返回 true；部门不存在（已删除）或组织架构还没加载时返回 false
     */
    virtual bool GetDepartmentName(int64_t deptId, wchar_t* buf, uint32_t chars) = 0;

    /**
     *  弹出 exe 的「选择部门」对话框（多选，含下级部门），个人机器人「指定的部门」用（接口版本 2 起）。用户确认后 exe 调
     *  IBotModule::OnDepartmentsPicked(requestId, ...)，结果已去掉被上级包含的下级部门；用户取消时不回调。
     *  @param owner       发起的窗口，可为 NULL；对话框打开期间禁用它
     *  @param requestId   DLL 自己分配的请求号，原样带回
     *  @param preselected 预先勾选的部门（部门ID数组），只在本次调用期间有效；可为 NULL
     *  @param count       预先勾选的部门数
     *  @return 已弹出返回 true；未登录或组织架构还没加载时返回 false
     */
    virtual bool PickDepartments(HWND owner, uint32_t requestId, const int64_t* preselected, uint32_t count) = 0;

protected:
    ~IBotHost()
    {
    }
};

/**
 *  DLL 实现、交给 exe 的模块接口。除 OnServerFrame 外只在界面线程上调用。
 */
class IBotModule
{
public:
    /**
     *  初始化：登记宿主、建隐藏的消息窗口、初始化本 DLL 那份 balloonui 与多语言。
     *  @return 成功返回 true；失败时宿主应卸载 DLL
     */
    virtual bool Init(IBotHost* host, const BotInitParams* params) = 0;

    //进程退出前调用：关闭全部窗口、销毁消息窗口；之后不再回调宿主
    virtual void Shutdown() = 0;

    //返回 DLL 编译时的接口版本号
    virtual uint32_t GetApiVersion() const = 0;

    //登录成功（含断线重连）：断线前发出的请求不再等应答，打开着的窗口重新加载
    virtual void OnLoggedIn() = 0;

    //即将退出登录（或企业关闭了机器人）：关闭全部窗口
    virtual void OnLoggingOut() = 0;

    /**
     *  收到一帧 2802～2809 的服务端应答（网络接收线程），DLL 拷贝后投递到界面线程处理。
     *  @param body 包体（UTF-8 JSON），只在本次调用期间有效
     */
    virtual void OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len) = 0;

    /**
     *  打开（或激活）某个群的群机器人管理窗。只有群主能管理；不是群主时窗口只读显示。
     *  @param owner     所有者窗口（群聊窗），可为 NULL
     *  @param groupId   群号
     *  @param groupName 群名（窗口标题用），可为 NULL
     *  @return 窗口已显示返回 true
     */
    virtual bool OpenGroupBotManager(HWND owner, int64_t groupId, const wchar_t* groupName) = 0;

    //关闭某个群的管理窗（本人退群、被移出、群被解散、不再是群主时）
    virtual void CloseGroupBotManager(int64_t groupId) = 0;

    /**
     *  打开（或激活）「我的机器人」窗口。
     *  @param owner 所有者窗口（主面板），可为 NULL
     *  @return 窗口已显示返回 true
     */
    virtual bool OpenMyBots(HWND owner) = 0;

    /**
     *  企业开关变了（登录 features 与 1013 推送）：打开着的窗口按新开关刷新可用的操作。
     *  @param bot           机器人总开关（features.bot）
     *  @param groupBot      允许群主添加群机器人（features.botgroup）
     *  @param personal      允许员工配置个人机器人（features.botpersonal）
     *  @param groupExternal 允许群主给群机器人配置外部服务（features.botgroupexternal）
     */
    virtual void OnFeaturesChanged(bool bot, bool groupBot, bool personal, bool groupExternal) = 0;

    /**
     *  某个群的机器人列表变了（exe 收到新的 2801 应答之后调用）：打开着的管理窗重读列表并重拉详情。
     *  @param groupId 群号；0 表示全部群（企业机器人在管理台有变化）
     */
    virtual void OnGroupBotsChanged(int64_t groupId) = 0;

    /**
     *  选择联系人对话框确认了（IBotHost::PickContacts 的结果）。
     *  @param requestId 发起时的请求号
     *  @param userIds   已选的人（userid 数组），只在本次调用期间有效；可为 NULL
     *  @param count     人数
     */
    virtual void OnContactsPicked(uint32_t requestId, const int64_t* userIds, uint32_t count) = 0;

    /**
     *  选择部门对话框确认了（IBotHost::PickDepartments 的结果，接口版本 2 起）。
     *  @param requestId 发起时的请求号
     *  @param deptIds   已选的部门（部门ID数组），只在本次调用期间有效；可为 NULL
     *  @param count     部门数
     */
    virtual void OnDepartmentsPicked(uint32_t requestId, const int64_t* deptIds, uint32_t count) = 0;

protected:
    ~IBotModule()
    {
    }
};

} // namespace flamingobot

//导出的工厂函数：版本兼容时返回模块对象（归 DLL 所有），否则返回 NULL
extern "C" flamingobot::IBotModule* __cdecl FlamingoBot_Create(uint32_t apiVersion);
