/**
 *  主程序一侧的文件共享接入：实现交给 FlamingoFileShare.dll 的宿主接口 IFileShareHost，并负责
 *  按需加载、初始化、通知与卸载这个 DLL（docs/文件共享方案.md 7.3）。
 *
 *  生命周期（只在 UI 线程上调用）：
 *    - 登录成功（含断线重连成功）：OnLoginSucceeded —— 本进程首次调用时加载 DLL 并 Init，
 *      之后每次都通知模块 OnLoggedIn。DLL 不存在、版本不符或 Init 失败时只记一条日志，
 *      本进程不再重试，客户端照常运行，只是没有文件共享功能；
 *    - 退出登录（含被踢下线、切换账号）：OnLoggingOut —— 通知模块关闭窗口、结束会话，不卸载 DLL；
 *    - 进程退出：Shutdown —— 先关闭模块，再卸载 DLL。须在主面板销毁之前、日志收尾之前调用。
 *
 *  第 1 期第 2 步（DLL 骨架）接通了加载、初始化、登录登出通知、写日志与取服务器公钥；第 10 步接通经主连接
 *  收发 2200 段信令：SendShareCommand 经 CIUSocket::SendShareCommand 发出，CIUSocket::HandleV2Frame 在网络
 *  接收线程上把收到的 2200 段帧交给 OnServerFrame，再经 CShareFrameRouter 转给模块。
 *  第 15 步接通取会话成员与显示名：单聊直接回送对方；群聊经 CIUSocket::RequestGroupMembers 发 1304，应答到达主面板
 *  后由主面板调 OnGroupMembersArrived 转回来，再交给模块。显示名经主面板实现的 IFileShareDirectory 取。
 *  第 16 步接通主程序的三处入口：系统菜单（OpenShareCenter）、聊天窗工具栏（OpenConversationShares）、共享卡片
 *  （OpenShareFromCard、GetShareState）；DLL 通知卡片状态变化（NotifyShareState）时经 IFileShareDirectory 交给主面板刷新。
 *  第 3 期第 1 步接入按企业的功能开关（docs/文件共享方案.md 5.8）：登录结果里的 features.fileshare 经 SetFeatureEnabled
 *  记下，开关关着时登录成功也不加载、不激活模块；运行中收到 1013 时调 OnFeaturesChanged，按 FileShareGateAction 让模块
 *  进入已登录状态或按退出登录收尾（不卸载 DLL）。三处入口一律以 IsModuleReady（已加载且开关打开）为准。
 *
 *  典型用法：
 *      balloonfileshare::CFileShareHost::GetInstance().SetDirectory(pPanel);       // 登录成功处，先登记主面板
 *      balloonfileshare::CFileShareHost::GetInstance().OnLoginSucceeded(login);   // 登录成功处，login 为账号快照
 *      balloonfileshare::CFileShareHost::GetInstance().OnLoggingOut();       // 回到登录界面处
 *      balloonfileshare::CFileShareHost::GetInstance().Shutdown();           // CAppShell::Shutdown
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include "../../UI/BalloonImageViewer/BalloonImageViewerWindow.h"   // IImageViewerNavigator

#include <stdint.h>

#include <set>
#include <string>
#include <utility>

#include "../../App/PluginModuleLoader.h"
#include "../../UI/BalloonMain/CreateGroupDlg.h"   // IMemberPickSink（选择共享对象的结果）
#include "../../../FlamingoFileShare/FileShareApi.h"
#include "ShareChannel.h"
#include "ShareMemberInfo.h"

namespace net {
struct GroupMembersResult;
}

namespace balloonfileshare {

//登录成功时交给宿主的账号快照：DLL 经 GetSessionInfo 取用。由登录成功处（MainPanelSessionController）从
//CUserMgr 填写，宿主不直接依赖主程序的全局客户端对象
struct FileShareLoginInfo
{
    int64_t      m_userId;        //本人 userid
    std::wstring m_account;       //账号
    std::wstring m_nickName;      //昵称
    std::wstring m_personalDir;   //个人目录（CUserMgr::GetPersonalFolder 的结果，带结尾反斜杠）

    FileShareLoginInfo()
        : m_userId(0)
    {
    }
};

/**
 *  CFileShareHost —— 文件共享模块的宿主（进程内唯一）。
 *  IFileShareHost 的 Log、SendShareCommand、GetServerPublicKey 可能在 DLL 的任意线程上被调用，均为线程安全；
 *  GetSessionInfo 与其余方法只在 UI 线程上调用。
 */
class CFileShareHost : public fileshare::IFileShareHost, public IImageViewerNavigator, public IMemberPickSink
{
public:
    /**
     *  取进程内唯一的宿主对象。
     *  @return 宿主对象（静态存储）
     */
    static CFileShareHost& GetInstance();

    /**
     *  登录成功（含断线重连成功）时调用。本进程首次调用时加载并初始化 DLL，之后通知模块已登录。
     *  @param login 本次登录的账号快照，宿主拷贝保存，供 DLL 经 GetSessionInfo 取用
     *  副作用：首次调用时加载 FlamingoFileShare.dll；加载或初始化失败时写一条日志，本进程不再重试。
     *          每次调用都读一次本机设备标识（CIUSocket::GetOrCreateDeviceId）一并保存。
     */
    void OnLoginSucceeded(const FileShareLoginInfo& login);

    /**
     *  即将回到登录界面（主动退出登录、被踢下线、切换账号）时调用。
     *  模块已就绪时通知它关闭全部窗口；DLL 保持加载。未就绪时调用无效果。
     */
    void OnLoggingOut();

    /**
     *  进程退出时调用：关闭模块并卸载 DLL。未加载时调用、重复调用都是安全的。
     *  须在主面板销毁之前调用（模块关闭时还要销毁自己的窗口），且在日志收尾（CIULog::Uninit）之前。
     */
    void Shutdown();

    /**
     *  模块是否可用：已加载并初始化成功，且本人所属企业的文件共享开关打开。三处入口据此显隐。
     *  @return 可用返回 true
     */
    bool IsModuleReady() const;

    /**
     *  记下登录结果里的功能开关（登录成功处、OnLoginSucceeded 之前调用）。不加载、不通知模块。
     *  @param enabled 登录应答的 features.fileshare 是否为 1（老服务端没有这个字段时按关闭）
     */
    void SetFeatureEnabled(bool enabled);

    /**
     *  运行中收到 1013（本企业的功能开关有变化）时调用：记下新开关，按 FileShareGateAction 让模块进入已登录状态
     *  （必要时先加载 DLL）或按退出登录收尾（关闭全部窗口、结束会话，不卸载 DLL）。
     *  @param enabled features.fileshare 是否为 1
     *  @return IsModuleReady 的结果是否因此改变（主面板据此刷新已打开聊天窗的「文件共享」按钮与卡片）
     */
    bool OnFeaturesChanged(bool enabled);

    /**
     *  本人所属企业的文件共享是否被管理员关闭（已登录、开关关着）。卡片据此显示「暂不可用：功能已被关闭」。
     *  @return 已登录且开关关闭时返回 true
     */
    bool IsFeatureDisabled() const;

    /**
     *  收到一帧 2200 段的服务端帧（由 CIUSocket::HandleV2Frame 在网络接收线程上调用）。原样转给模块，
     *  模块拷贝包体后投递到自己的窗口；模块未加载或已卸载时记一条日志后丢弃。线程安全。
     *  @param cmd  命令号，2200~2299
     *  @param seq  帧序号（应答为对应请求的 seq，推送为 0）
     *  @param body 包体（借用，调用期间有效）
     */
    void OnServerFrame(uint16_t cmd, uint32_t seq, const std::string& body);

    /**
     *  登记取显示名用的主面板能力。登录成功时（OnLoginSucceeded 之前）调用；主面板比本对象先销毁之前，
     *  进程退出流程会先调 Shutdown，此后不再使用它。
     *  @param directory 主面板（借用，不拥有），可为 NULL（此时显示名一律为「用户<id>」）
     */
    void SetDirectory(IFileShareDirectory* directory);

    /**
     *  群成员列表（cmd 1304 的应答）到达主面板后由主面板转来（UI 线程）。只有本宿主为模块请求过的群才转给模块，
     *  其余忽略，因此主面板可以对每一份群成员列表都调用。
     *  @param result 群成员列表
     */
    void OnGroupMembersArrived(const net::GroupMembersResult& result);

    /**
     *  打开「文件共享」窗口（系统菜单入口）。只在 UI 线程上调用。
     *  @param owner 所有者窗口，可为 NULL
     *  @return 模块就绪且窗口已显示返回 true
     */
    bool OpenShareCenter(HWND owner);

    /**
     *  弹出本会话的共享列表浮层（聊天窗工具栏入口）。只在 UI 线程上调用。
     *  @param owner  聊天窗
     *  @param ctype  会话类型：1 单聊 2 群聊
     *  @param cid    会话 id：单聊为对方 userid，群聊为群 id
     *  @param anchor 按钮的屏幕矩形，可为 NULL
     *  @return 模块就绪且浮层已弹出返回 true
     */
    bool OpenConversationShares(HWND owner, int ctype, int64_t cid, const RECT* anchor);

    /**
     *  打开一个共享（点共享卡片）。只在 UI 线程上调用。
     *  @param owner   聊天窗（浏览窗口据它居中），可为 NULL
     *  @param shareId 共享 id
     *  @return 模块就绪且浏览窗口已显示返回 true
     */
    bool OpenShareFromCard(HWND owner, int64_t shareId);

    /**
     *  取共享卡片的最新状态（聊天窗画卡片时调用）。只在 UI 线程上调用。
     *  @param shareId 共享 id
     *  @param state   [出参] 状态（share_state）
     *  @param reason  [出参] 原因
     *  @return 模块就绪且 DLL 有这个共享的记录时返回 true
     */
    bool GetShareState(int64_t shareId, int& state, int& reason);

    /**
     *  登记一个会话，统计其中可用共享的个数（聊天窗头部文件共享按钮的角标，2026-10-03 起）。聊天窗打开时调用一次，
     *  收到新的共享卡片时再调用（重新拉取）。模块尚未进入已登录状态时只记下，模块进入已登录状态后自动转给它。
     *  个数经 IFileShareDirectory::OnConversationShareCount 交给主面板。只在 UI 线程上调用。
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id：单聊为对方 userid，群聊为群 id
     */
    void WatchConversationShares(int ctype, int64_t cid);

    /**
     *  注销一个会话（聊天窗关闭时调用）。只在 UI 线程上调用。
     *  @param ctype 会话类型
     *  @param cid   会话 id
     */
    void UnwatchConversationShares(int ctype, int64_t cid);

    /**
     *  在本机打开本人发起的共享（点本人发出的共享卡片，2026-10-03 起）。打不开时 DLL 以 owner 为所有者弹出提示说明原因。
     *  只在 UI 线程上调用。
     *  @param owner   聊天窗，可为 NULL
     *  @param shareId 共享 id
     *  @return 模块就绪且已交给资源管理器返回 true
     */
    bool OpenOwnShareLocally(HWND owner, int64_t shareId);

    /**
     *  打开某个会话的发起共享对话框（会话右键菜单「共享文件到本群」，2026-10-03 起），与在聊天窗里点「发起共享」相同：
     *  候选成员经 RequestConversationMembers 取得，逐人勾选。只在 UI 线程上调用。
     *  @param owner 所有者窗口（通常是该会话的聊天窗），可为 NULL
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id
     *  @return 模块就绪且对话框已显示返回 true
     */
    bool OpenCreateShare(HWND owner, int ctype, int64_t cid);

    /**
     *  主动把文件共享给一个人（联系人与单聊会话右键菜单「共享文件给TA」，2026-10-03 起）：等同于在「文件共享」窗口的选人窗口里
     *  只选了这个人——打开发起共享对话框、对象固定为他，成功后打开与他的单聊窗。只在 UI 线程上调用。
     *  @param userId 对方 userid
     *  @return 模块就绪且已交给模块返回 true
     */
    bool StartDirectShareTo(int64_t userId);

    /**
     *  主面板代为发起的建群有了结果（2026-10-03 起）：转给模块的 OnShareGroupCreated。只在 UI 线程上调用。
     *  @param code    0 成功，否则为服务端的错误码
     *  @param groupId 新群的群 id；失败时为 0
     */
    void OnShareGroupCreated(int code, int64_t groupId);

    /**
     *  有用户的头像变了（2026-10-03 起）：主面板在照片头像下载完成、联系人修改头像后调用。模块已进入已登录状态时转给模块的
     *  OnAvatarsUpdated，让已打开窗口里的成员头像重新取一遍。只在 UI 线程上调用。
     */
    void OnAvatarsUpdated();

    //IMemberPickSink：选人对话框里选好了共享对象，带上显示名转给模块的 OnShareTargetsPicked
    virtual void OnMembersPicked(const std::vector<int64_t>& userIds) override;

    //以下为 IFileShareHost 的实现，语义见 FlamingoFileShare/FileShareApi.h
    virtual void Log(int level, const char* text) override;
    virtual uint32_t SendShareCommand(uint16_t cmd, const char* json, uint32_t len) override;
    virtual const unsigned char* GetServerPublicKey() override;
    virtual bool GetSessionInfo(fileshare::FileShareSessionInfo* info) override;
    virtual bool RequestConversationMembers(int ctype, int64_t cid) override;
    virtual bool GetUserDisplayName(int64_t userId, char* buf, uint32_t bufBytes) override;
    virtual void NotifyShareState(int64_t shareId, int state, int reason, int myPerms) override;
    virtual bool GetConversationName(int ctype, int64_t cid, char* buf, uint32_t bufBytes) override;
    virtual bool OpenConversation(int ctype, int64_t cid) override;
    virtual bool OpenImageViewer(HWND owner, const wchar_t* imagePath, bool navigable) override;

    /**
     *  一个会话里可用的共享个数变了（DLL 调用）：经 IFileShareDirectory 交给主面板，设置该会话聊天窗头部文件共享按钮的角标。
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id（本人视角）
     *  @param count 可用共享的个数；0 时角标消失
     */
    virtual void NotifyConversationShareCount(int ctype, int64_t cid, int count) override;

    /**
     *  弹出选择联系人窗口，选择共享对象（DLL 在「文件共享」窗口点「发起共享」时调用）：经 IFileShareDirectory 交给主面板
     *  打开选人对话框；确认后结果回到本类的 OnMembersPicked，再转给模块。
     *  @param owner 发起的窗口，可为 NULL
     *  @return 已弹出返回 true；未登录、离线或通讯录还没加载时返回 false
     */
    virtual bool PickShareTargets(HWND owner) override;

    /**
     *  代文件共享新建一个由本人与这些人组成的群（DLL 在多人共享时调用）：经 IFileShareDirectory 交给主面板发建群请求，
     *  应答由主面板按 seq 交回本类的 OnShareGroupCreated，再转给模块。
     *  @param groupName 群名（UTF-8，DLL 拼好的「共享 <共享名> 给 <成员名>」），只在本次调用期间有效
     *  @param memberIds 成员 userid 数组（不含本人），只在本次调用期间有效
     *  @param count     成员数，至少 2
     *  @return 请求已发出返回 true；未登录、离线或参数非法返回 false
     */
    virtual bool CreateShareGroup(const char* groupName, const int64_t* memberIds, uint32_t count) override;

    /**
     *  取一个用户的头像（DLL 画成员行时调用）：经 IFileShareDirectory 问主面板，写进接口结构体；没有登记主面板时按显示名首字
     *  与按 userid 取的底色画字形。照片还没下载到本机时主面板会安排下载，下载完成后经 OnAvatarsUpdated 通知模块。
     *  @param userId 用户 userid
     *  @param out    [出参] 由 DLL 分配，m_cbSize 须等于 sizeof(FileShareAvatarInfo)
     *  @return 已填写返回 true；参数非法返回 false
     */
    virtual bool GetUserAvatar(int64_t userId, fileshare::FileShareAvatarInfo* out) override;

    /**
     *  见 IFileShareHost::Translate（第 11 版起）：转给 exe 的语言模块 i18n::Translate。可在任意线程调用。
     *  @param context 上下文，可为 NULL
     *  @param source  简体中文原文；为 NULL 时返回 NULL
     *  @return 当前语言的译文，在进程生命周期内有效；没有译文时返回 source
     */
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) override;

    /**
     *  见 IFileShareHost::GetUiLanguage（第 11 版起）：取 exe 当前生效的界面语言与界面字体。可在任意线程调用。
     *  @param langBuf   [出参] 语言代码，写不下时截断，以 '\0' 结尾
     *  @param langChars langBuf 的容量（字符）
     *  @param fontBuf   [出参] 界面字体名，写不下时截断，以 '\0' 结尾
     *  @param fontChars fontBuf 的容量（字符）
     *  @return 写入成功返回 true；参数非法返回 false
     */
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) override;

    /**
     *  见 IFileShareHost::GetP2PService：转交 exe 的 P2P 宿主已加载的服务对象。只在 UI 线程上调用。
     *  @return P2P 服务（归 FlamingoP2P.dll 所有）；P2P 模块没有加载成功时返回 NULL
     */
    virtual p2p::IP2PService* GetP2PService() override;

    /**
     *  见 IFileShareHost::QueryNoticePolicy（第 12 版起）：勿扰时段内丢弃，锁屏期间推迟，其余照常（ShareNoticePolicy）。
     *  只在 UI 线程上调用。
     *  @return fileshare::FileShareNoticePolicy 的取值
     */
    virtual int QueryNoticePolicy() override;

    /**
     *  见 IFileShareHost::PlayNoticeSound（第 12 版起）：响一声新消息提示音，「提示音」关着或锁屏期间不响。只在 UI 线程上调用。
     */
    virtual void PlayNoticeSound() override;

    /**
     *  本人作为访问者、尚未结束的文件共享传输数（第 12 版起）：退出程序、退出登录之前据此请用户确认。只在 UI 线程上调用。
     *  @return 传输数；模块没有加载或不在已登录状态时为 0
     */
    uint32_t GetActiveTransferCount();

    //IImageViewerNavigator：看图窗里按了 ← / →，转交模块翻到上一张 / 下一张
    virtual void OnImageViewerNavigate(int delta) override;

private:
    CFileShareHost();
    ~CFileShareHost();

    CFileShareHost(const CFileShareHost&) = delete;
    CFileShareHost& operator=(const CFileShareHost&) = delete;

    /**
     *  加载 DLL 并初始化模块。本进程只真正尝试一次，之后直接返回第一次的结果。
     *  @return 模块就绪返回 true
     */
    bool EnsureModuleReady();

    //模块刚进入已登录状态（首次登录、开关由关变开）：把登记过的会话逐个转给模块
    void ForwardWatchedConversations();

    //模块退出已登录状态（退出登录、开关由开变关）：登记过的会话的角标清零（模块不再通知它们）
    void ClearWatchedBadges();

private:
    balloonapp::CPluginModuleLoader<fileshare::IFileShareModule> m_loader;   //DLL 加载器；Shutdown 时卸载
    CShareFrameRouter m_router;   //网络线程上收到的 2200 段帧经它转给模块；模块就绪时挂接，卸载 DLL 之前解除挂接
    bool m_loadAttempted;   //本进程是否已经尝试过加载（无论成败，只尝试一次）
    bool m_ready;           //模块是否已加载并 Init 成功；Shutdown 后为 false
    FileShareLoginInfo m_login;      //最近一次登录成功时的账号快照；只在 UI 线程上读写（GetSessionInfo 也只在 UI 线程上被调用）
    std::string        m_deviceId;   //最近一次登录成功时读到的本机设备标识（ASCII）
    bool               m_loggedIn;   //是否处于已登录状态：OnLoginSucceeded 置真，OnLoggingOut 置假
    bool               m_featureEnabled; //本人所属企业的文件共享开关：登录结果与 1013 给出；默认关闭（老服务端不下发即视同关闭）
    bool               m_moduleActive;   //模块是否处于已登录状态：通知过 OnLoggedIn 且之后没有通知 OnLoggingOut
    IFileShareDirectory* m_directory;          //取显示名用的主面板能力（借用）；未登记时为 NULL
    std::set<int64_t>    m_pendingGroupMembers; //为模块请求过、应答尚未到达的群 id；退出登录时清空
    //登记了可用共享个数的会话：(会话类型, 会话 id)。聊天窗打开时加入、关闭时去掉；模块尚未进入已登录状态时先记在这里
    std::set<std::pair<int, int64_t> > m_watchedConvs;
};

} // namespace balloonfileshare
