/**
 *  主程序一侧的帖子群接入：实现交给 FlamingoThreadGroup.dll 的宿主接口 IThreadGroupHost，负责按企业开关加载、初始化、
 *  通知与卸载这个 DLL，并把 2700 段的帧转给它（docs/帖子群方案.md 7.2）。
 *
 *  生命周期（只在 UI 线程上调用）：
 *    - 登录成功（含断线重连）：SetFeatureEnabled 之后 OnLoginSucceeded —— 开关打开时，本进程首次调用加载 DLL 并 Init，
 *      之后每次通知模块 OnLoggedIn。DLL 不存在、版本不符或 Init 失败时只记一条日志，本进程不再重试，客户端照常运行，
 *      只是帖子群全部隐藏（docs/帖子群方案.md 4.7）；
 *    - 运行中收到 1013：OnFeaturesChanged —— 开关由关变开时补加载并让模块进入已登录状态；由开变关时模块按退出登录收尾
 *      （关闭全部帖子群窗口），DLL 保持加载。返回值告诉主面板帖子群的显隐是否因此变化；
 *    - 退出登录：OnLoggingOut —— 通知模块关闭全部帖子群窗口，不卸载 DLL；
 *    - 进程退出：Shutdown —— 先解除帧路由，再关闭模块、卸载 DLL。
 *
 *  线程：Log 与 SendThreadCommand 可能在 DLL 的任意线程上被调用，OnServerFrame 在网络接收线程上被调用，均线程安全；
 *  其余方法只在 UI 线程上调用。
 *
 *  主面板的能力（显示名、头像、群概况、打开群资料与资料卡、已读、提醒判定）经 IThreadGroupDirectory 取；没有登记主面板时
 *  宿主按缺省值回答（显示名为「用户<id>」、群概况取不到），供单元测试与登录前使用。
 *
 *  典型用法：
 *      balloonthread::CThreadGroupHost& host = balloonthread::CThreadGroupHost::GetInstance();
 *      host.SetDirectory(pMainPanel);
 *      host.SetFeatureEnabled(userMgr.m_bThreadGroupFeature);
 *      host.OnLoginSucceeded(userId);
 *      if (balloonthread::DecideGroupOpen(type, host.IsAvailable()) == balloonthread::group_open_thread_window)
 *          host.OpenThreadWindow(groupId, 0);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include "../../UI/BalloonImageViewer/BalloonImageViewerWindow.h"   // IImageViewerNavigator

#include <stdint.h>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "../../App/PluginModuleLoader.h"
#include "../../../FlamingoThreadGroup/ThreadGroupApi.h"
#include "ThreadCardSync.h"   // ThreadCardUpdate：被改写的帖子卡片（2721 推送）

namespace net {
struct GroupMembersResult;
}

//表情库只在 .cpp 里用到。不在这里包含 FaceList.h：它带进来的 XmlDocument.h 用了 #import，本头文件被主面板头文件包含，
//会让大批源文件都带上 #import（/MP 并行编译时报 C2813）
class CFaceList;

namespace balloonthread {

//宿主的隐藏消息窗口收到的私有消息（开发约定文档的 WM_APP 占用表登记为 0x183、0x184）：
//图片下载、上传的结果，lParam 为 net::PlainTransferResult*，由消息窗口处理后 delete（结果对象在 exe 的堆上）
const UINT kMsgThreadHostImageDownloaded = WM_APP + 0x183;
const UINT kMsgThreadHostImageUploaded = WM_APP + 0x184;
//同一个消息窗口：网络线程收到 2721 推送、解析出被改写的卡片后投到界面线程（开发约定文档的 WM_APP 占用表
//登记为 0x186），lParam 为 ThreadCardBatch*，由消息窗口处理后 delete（docs/帖子群方案.md 14.6）
const UINT kMsgThreadHostCards = WM_APP + 0x186;

/**
 *  一个用户的头像该怎么画：主面板按通讯录里该用户的头像设置算好，经宿主写进 ThreadAvatarInfo 交给 DLL。
 */
struct ThreadUserAvatar
{
    std::wstring m_glyph;       //字形头像上画的字（1~2 个字符）；照片头像也填，照片不在本机时用它占位
    COLORREF     m_color;       //字形头像的底色
    std::wstring m_imagePath;   //照片头像在本机的完整路径；空串表示画字形

    ThreadUserAvatar()
        : m_color(0)
    {
    }
};

/**
 *  一个群的概况（宿主据此填 ThreadGroupBrief）。
 */
struct ThreadGroupBriefInfo
{
    std::wstring m_name;         //群名；群资料还没到时为空串
    uint32_t     m_memberCount;  //成员数
    int          m_myRole;       //本人角色：0 成员 1 管理员 2 群主；不是成员为 -1
    bool         m_mutedForMe;   //本人此刻不能发言：被禁言、仅通知模式下不是管理员，或入群必读还没确认
    int          m_atAllScope;   //群设置「谁可以 @ 全体」：0 仅群主和管理员 1 所有人；群资料还没到时为 0（docs/帖子群方案.md §15）

    ThreadGroupBriefInfo()
        : m_memberCount(0)
        , m_myRole(-1)
        , m_mutedForMe(false)
        , m_atAllScope(0)
    {
    }
};

/**
 *  宿主需要的主程序能力，由主面板实现（通讯录、群信息缓存与各窗口都在主面板里）。只在 UI 线程上调用。
 */
class IThreadGroupDirectory
{
public:
    virtual ~IThreadGroupDirectory()
    {
    }

    /**
     *  一个用户在某个帖子群里的显示名：与群聊窗成员列表同一口径（群名片、通讯录显示名、昵称依次取第一个非空的）。
     *  @param userId  用户 userid
     *  @param groupId 帖子群
     *  @return 显示名；不认识的用户为「用户<id>」
     */
    virtual std::wstring GetThreadUserDisplayName(int64_t userId, int64_t groupId) = 0;

    /**
     *  一个用户的头像：与主面板联系人列表同一口径。照片还没下载到本机时同时安排下载，此时 m_imagePath 为空；
     *  下载完成后主面板调 CThreadGroupHost::OnAvatarsUpdated。
     */
    virtual ThreadUserAvatar GetThreadUserAvatar(int64_t userId) = 0;

    /**
     *  一个群的概况。
     *  @param out [出参] 概况
     *  @return 本人在这个群里（群信息缓存里有它）返回 true
     */
    virtual bool GetThreadGroupBrief(int64_t groupId, ThreadGroupBriefInfo& out) = 0;

    //打开现有的群资料窗口（帖子群窗口的「群资料」按钮）
    virtual void OpenThreadGroupInfo(HWND owner, int64_t groupId) = 0;

    //打开现有的群管理窗口（帖子群窗口的「群管理」按钮）：帖子群的群管理窗口没有「群公告」页（docs/帖子群方案.md §15.1）
    virtual void OpenThreadGroupManage(HWND owner, int64_t groupId) = 0;

    /**
     *  弹出某个群成员的菜单并执行选中的一项（点帖子正文里的 @某人，docs/帖子群方案.md §15.1 第 4 条）：
     *  与群成员面板的右键菜单相同，没有「@TA」。
     *  @param owner      菜单与确认框的所有者窗口
     *  @param groupId    帖子群
     *  @param userId     被 @ 的人
     *  @param targetRole 对方的群角色：0 / 1 / 2，threadgroup::kMemberRoleLeft 或 kMemberRoleUnknown
     *  @param screenPt   菜单的锚点（屏幕坐标）
     */
    virtual void ShowThreadMemberMenu(HWND owner, int64_t groupId, int64_t userId, int targetRole,
                                      const POINT& screenPt) = 0;

    //打开某人的资料卡（点头像或名字）
    virtual void OpenThreadContactCard(HWND owner, int64_t userId) = 0;

    //把该群标为已读：按该群最新的 msgid 发 1506 并清掉会话列表上的未读
    virtual void MarkThreadGroupRead(int64_t groupId) = 0;

    //帖子群窗口「正在看」的状态变了：提醒判定据此决定该群的卡片消息要不要弹窗、响铃
    virtual void OnThreadWatchChanged(int64_t groupId, bool watching) = 0;

    /**
     *  帖子卡片或动态卡片被服务端改写了（删帖、删留言、删补充、编辑帖子之后的 2721 推送，docs/帖子群方案.md 14.6；
     *  离线期间的改写由按改写序号同步补上，不经这里）：改写本地消息记录，会话列表最后一条正是它时刷新摘要。
     *  @param groupId 帖子群
     *  @param cards   改写后的卡片
     */
    virtual void OnThreadCardsChanged(int64_t groupId, const std::vector<ThreadCardUpdate>& cards) = 0;

    /**
     *  在帖子群窗口右侧显示群成员面板（docs/帖子群方案.md 14.3）：第一次时建面板并请求成员名单（1304）。
     *  @param parent  帖子群窗口
     *  @param groupId 帖子群
     *  @param rc      面板在帖子群窗口客户区里的矩形
     *  @return 面板已显示返回 true
     */
    virtual bool ShowThreadMemberPanel(HWND parent, int64_t groupId, const RECT& rc) = 0;

    //帖子群窗口改变了大小：群成员面板跟随到新的矩形
    virtual void MoveThreadMemberPanel(int64_t groupId, const RECT& rc) = 0;

    //收起群成员面板
    virtual void HideThreadMemberPanel(int64_t groupId) = 0;
};

/**
 *  CThreadGroupHost —— 帖子群模块的宿主（进程内唯一）。
 */
class CThreadGroupHost : public threadgroup::IThreadGroupHost, public IImageViewerNavigator
{
public:
    /**
     *  取进程内唯一的宿主对象。
     */
    static CThreadGroupHost& GetInstance();

    /**
     *  登记主面板。登录成功处（OnLoginSucceeded 之前）调用；传 NULL 表示解除。
     *  @param directory 主面板（借用，不拥有）
     */
    void SetDirectory(IThreadGroupDirectory* directory);

    /**
     *  记下登录结果里的帖子群开关（登录成功处、OnLoginSucceeded 之前调用）。不加载、不通知模块。
     *  @param enabled features.threadgroup 是否为 1（老服务端没有这个字段时按关闭）
     */
    void SetFeatureEnabled(bool enabled);

    /**
     *  登录成功（含断线重连成功）时调用：开关打开时确保 DLL 已加载，并通知模块已登录。
     *  @param myUserId 本人 userid
     *  副作用：首次需要时加载 FlamingoThreadGroup.dll；加载或初始化失败时写一条日志，本进程不再重试。
     */
    void OnLoginSucceeded(int64_t myUserId);

    /**
     *  运行中收到 1013 时调用：记下新开关；由关变开时必要时先加载 DLL，再让模块进入已登录状态；由开变关时模块关闭全部
     *  帖子群窗口（DLL 保持加载）。
     *  @param enabled features.threadgroup 是否为 1
     *  @return IsAvailable 的结果是否因此改变（主面板据此重填群列表与会话列表）
     */
    bool OnFeaturesChanged(bool enabled);

    /**
     *  即将退出登录时调用：模块关闭全部帖子群窗口；DLL 保持加载。
     */
    void OnLoggingOut();

    /**
     *  进程退出时调用：解除帧路由、关闭模块、卸载 DLL。未加载时与重复调用都安全。
     *  须在主面板销毁之前调用（模块关闭时还要销毁自己的窗口），且在日志收尾之前。
     */
    void Shutdown();

    /**
     *  帖子群功能是否可用：已登录、DLL 已加载并初始化、企业开关打开。不可用时帖子群从各处隐藏（ThreadGroupGate.h）。
     */
    bool IsAvailable() const;

    /**
     *  打开（或激活）某个帖子群的窗口。
     *  @param groupId 帖子群
     *  @param postId  大于 0 时定位到这条帖子（从卡片、提醒点进来时）
     *  @return 功能可用且窗口已显示返回 true
     */
    bool OpenThreadWindow(int64_t groupId, int64_t postId);

    //关闭某个帖子群的窗口（本人退群、被移出、群被解散时）；功能不可用时什么也不做
    void CloseThreadWindow(int64_t groupId);

    /**
     *  某个帖子群的窗口是否「正在看」（存在、在前台、没有最小化）。提醒判定用。
     */
    bool IsWatching(int64_t groupId) const;

    //群资料变了（群名、成员数、本人角色、禁言状态）：已打开的窗口刷新标题与可用的操作
    void OnGroupChanged(int64_t groupId);

    /**
     *  群成员列表（cmd 1304 的应答）到达主面板后由主面板转来。只有本宿主为模块请求过的群才转给模块，
     *  其余忽略，因此主面板可以对每一份群成员列表都调用。
     */
    void OnGroupMembersArrived(const net::GroupMembersResult& result);

    //有用户的头像变了：已打开窗口里的头像重新取一遍
    void OnAvatarsUpdated();

    //群成员面板被用户关掉了（面板上的关闭按钮或 Esc）：转告模块，帖子群窗口记下面板已收起；功能不可用时什么也不做
    void OnMemberPanelClosed(int64_t groupId);

    /**
     *  收到一帧 2700 段的服务端帧（CIUSocket::HandleV2Frame 在网络接收线程上调用），原样转给模块；
     *  模块未加载时记一条日志后丢弃。线程安全。
     */
    void OnServerFrame(uint16_t cmd, uint32_t seq, const std::string& body);

    //======== threadgroup::IThreadGroupHost（语义见 FlamingoThreadGroup/ThreadGroupApi.h） ========
    virtual void Log(int level, const char* text) override;
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) override;
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) override;
    virtual uint32_t SendThreadCommand(uint16_t cmd, const char* json, uint32_t len) override;
    virtual int64_t GetMyUserId() override;
    virtual bool GetUserDisplayName(int64_t userId, int64_t groupId, wchar_t* buf, uint32_t chars) override;
    virtual bool GetUserAvatar(int64_t userId, threadgroup::ThreadAvatarInfo* out) override;
    virtual bool GetGroupBrief(int64_t groupId, threadgroup::ThreadGroupBrief* out) override;
    virtual void RequestGroupMembers(int64_t groupId) override;
    virtual void OpenGroupInfo(HWND owner, int64_t groupId) override;
    virtual void OpenGroupManage(HWND owner, int64_t groupId) override;
    virtual void ShowMemberMenu(HWND owner, int64_t groupId, int64_t userId, int targetRole, int screenX,
                                int screenY) override;
    virtual void OpenContactCard(HWND owner, int64_t userId) override;
    virtual bool UploadImage(uint32_t requestId, const wchar_t* localPath) override;
    virtual bool DownloadImage(uint32_t requestId, const char* fileId, const char* md5) override;
    virtual void OpenImageViewer(HWND owner, const wchar_t* const* paths, uint32_t count, uint32_t index) override;
    virtual uint32_t GetFaceCount() override;
    virtual bool GetFaceInfo(uint32_t index, threadgroup::ThreadFaceInfo* out) override;
    virtual void MarkGroupRead(int64_t groupId) override;
    virtual void OnWatchChanged(int64_t groupId, bool watching) override;
    virtual bool ShowMemberPanel(HWND parent, int64_t groupId, const RECT* rc) override;
    virtual void MoveMemberPanel(int64_t groupId, const RECT* rc) override;
    virtual void HideMemberPanel(int64_t groupId) override;

    //IImageViewerNavigator：看图窗里按了 ← / →，打开同一组图片里的上一张 / 下一张
    virtual void OnImageViewerNavigate(int delta) override;

private:
    CThreadGroupHost();
    ~CThreadGroupHost();
    CThreadGroupHost(const CThreadGroupHost&);
    CThreadGroupHost& operator=(const CThreadGroupHost&);

    //首次需要时加载并初始化 DLL；本进程只尝试一次。返回是否就绪
    bool EnsureModuleReady();

    //让模块进入已登录状态（通知 OnLoggedIn）
    void ActivateModule();

    //让模块退出已登录状态（通知 OnLoggingOut），DLL 保持加载
    void DeactivateModule();

    //首次需要时加载表情库
    void EnsureFaces();

    //看图窗口打开当前这组图片里的第 m_viewerIndex 张
    void ShowViewerImage();

    //接收图片上传、下载结果的隐藏消息窗口：模块就绪时创建，Shutdown 时销毁（销毁前删除还在队列里的结果对象）
    bool CreateMessageWindow();
    void DestroyMessageWindow();
    static LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    //把一份图片结果交给模块并删除结果对象（lParam 为 net::PlainTransferResult*）
    void HandleImageResult(UINT msg, LPARAM lParam);

    /**
     *  网络线程上截下 2721 推送：解析出被改写的卡片，投到消息窗口在界面线程上交给主面板。
     *  这条只给 exe 用，不转给模块。客户端 2026-10-08 起不再发 2716（docs/消息改写离线同步方案.md 5.5），
     *  万一收到它的应答，按其他 2700 段命令转给模块，由模块记日志后丢弃。调用方已持有 m_routeMutex。
     *  @return 是 2721（已处理、不再转给模块）时返回 true
     */
    bool InterceptCardFrame(uint16_t cmd, const std::string& body);

    //把一批被改写的卡片交给主面板并删除批对象（lParam 为 ThreadCardBatch*）
    void HandleCardBatch(LPARAM lParam);

private:
    balloonapp::CPluginModuleLoader<threadgroup::IThreadGroupModule> m_loader;   //DLL 加载器；Shutdown 时卸载
    bool                    m_loadAttempted;   //本进程是否已经尝试过加载（无论成败，只尝试一次）
    bool                    m_ready;           //DLL 已加载且 Init 成功；Shutdown 后为 false
    bool                    m_featureEnabled;  //本人所属企业的帖子群开关：登录结果与 1013 给出；默认关闭
    bool                    m_loggedIn;        //是否处于已登录状态：OnLoginSucceeded 置真，OnLoggingOut 置假
    bool                    m_moduleActive;    //模块是否处于已登录状态：通知过 OnLoggedIn 且之后没有通知 OnLoggingOut
    int64_t                 m_myUserId;        //本人 userid；未登录时为 0
    IThreadGroupDirectory*  m_directory;       //主面板（借用）；未登记时为 NULL
    std::set<int64_t>       m_pendingGroupMembers; //为模块请求过、应答尚未到达的群 id；退出登录时清空
    std::unique_ptr<CFaceList> m_faces;        //表情库（Face/FaceConfig.xml），首次 GetFaceCount 时创建并加载，进程内常驻；为空表示还没加载
    HWND                    m_msgWnd;          //接收图片上传、下载结果的隐藏消息窗口；模块就绪前与 Shutdown 后为 NULL
    HWND                    m_viewerOwner;     //看图窗口的所有者（最近一次 OpenImageViewer 传来的）
    std::vector<std::wstring> m_viewerPaths;   //看图窗口当前这组图片的本机路径
    size_t                  m_viewerIndex;     //看图窗口正在显示第几张
    mutable std::mutex      m_routeMutex;      //保护 m_routeTarget：网络线程读，界面线程写
    threadgroup::IThreadGroupModule* m_routeTarget; //网络线程转发帧的目标；未就绪或已卸载时为 NULL
};

} // namespace balloonthread
