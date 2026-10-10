/**
 *  文件共享 DLL 的业务总控，ShareService.h
 *
 *  CFileShareModule 只负责与宿主之间的接口与线程切换（收帧排队、消息窗口），具体业务都交给本类，全部在
 *  UI 线程上执行。第 1 期第 12 步实现分享者一端（docs/文件共享方案.md 8.2、8.6）：
 *    - 登录：经宿主取账号与本机信息，打开本账号的 <个人目录>\ChatLog\fileshare.db，发 2205 拉取「我的共享」；
 *    - 2205 应答：与本机记录比对（ShareReconcile），删除服务端已没有的本机记录，检查需要报告的共享根是否存在，
 *      发 2210 报告能否提供；比对结果保存起来，供「文件共享」窗口标注「本机缺少记录」「位于另一台电脑」；
 *    - 2222（推给分享者的那一帧）：按本机记录建一个提供会话（CShareProviderSession），后台连接中转、发 sf_hello；
 *      本机没有记录或共享根已不存在时回 2221 拒绝，后者顺带补报一次 2210；
 *    - 2223：结束对应的提供会话；2203（已失效）：删除本机记录并结束该共享上的会话；2206：更新会话的权限；
 *    - 提供会话的事件（连接成败、访问者 sf_bye、通道断开）在后台线程上产生，经 PostSessionEvent 排队，
 *      再经通知窗口回到 UI 线程（DrainEvents）处理；通道异常断开与连接失败时回 2221 告诉服务端；
 *    - 退出登录：结束全部提供会话、关闭本机库；模块关闭：同样处理，并等待尚在连接中的线程结束。
 *  第 13 步加入访问者一端：
 *    - 浏览窗口经 AcquireVisitor 取得一个共享的访问状态机（CShareVisitor），关闭时 ReleaseVisitor；
 *    - 2220 应答按 seq、推给访问者的 2222 与 2223 按 accessid 交给对应的状态机；2203、2206（本人是成员）按 shareid 交给它；
 *    - 状态机要连中转时经内部的传输层建一条访问通道（CShareVisitorChannel），通道的连接结果、收到的应用帧、断开都作为
 *      事件排队，回到 UI 线程后交给状态机；
 *    - 模块的消息窗口每秒调用一次 OnTimer，驱动各状态机检查超时与空闲回收。
 *  第 14 步加入传输子会话（方案 8.3、8.4）：
 *    - 访问者经 StartDownload / StartUpload 发 2224，应答带回 transferid，随后的 2222（传输子会话）按 transferid 建
 *      CShareTransferSession；分享者收到 2222（传输子会话）时按其中的方向建发送端或接收端；
 *    - 传输的进度、完成、失败作为事件回到 UI 线程：两端都发 2225 结束传输子会话；访问者下载失败时把各文件的进度写进
 *      续传记录、完成时删除；分享者上传失败时删除半成品；分享者按文件批量上报 2230 审计并记一条最近操作；
 *    - 2223（传输子会话）、2206 收回上传权限、2203 共享失效时取消对应的传输；关闭浏览窗口时取消它发起的传输。
 *  第 15 步加入管理请求（方案 8.1、8.7）：
 *    - CreateShare：先以 clientshareid 写本机记录再发 2201；应答超时用同一个 clientshareid 重发（服务端去重），
 *      成功时补写 shareid 并立即发 2210，被拒或最终超时时删除本机记录；
 *    - CancelShare（2202）、UpdateMembers（2204）：发出后等应答，超时按没有应答处理，不重发；
 *    - 结果经 IShareManageObserver 回报；登录时清理上次进程遗留的、还没有 shareid 的本机记录。
 *  第 16 步加入：
 *    - RequestList：界面拉取共享列表（2205，可按会话筛选），结果经 IShareListObserver 回报；
 *    - 共享卡片的状态缓存：2203、推给本人的 2206、各次 2205 应答更新它，聊天窗画卡片时经 GetCardState 取用；
 *      2203 与推给本人的 2206 到达时经宿主 NotifyShareState 通知主程序刷新已打开窗口里的卡片。
 *  2026-10-03 加入：卡片状态有变化时另外通知 IShareCardStateListener（按会话统计可用共享个数的
 *  CShareConversationCounter 订阅它，聊天窗头部文件共享按钮的角标由此更新）。
 *  第 4 期（2026-10-07，docs/文件共享方案.md 第 14 节）加入：
 *    - 后台传输：浏览窗口关闭（ReleaseVisitor）时这个共享上还有访问者一端的传输，就只解除窗口、保留访问会话，传输继续；
 *      没有窗口也没有传输的访问状态机在每秒的定时检查里结束访问会话并销毁。窗口重新打开时经 AdoptVisitorTransfers 接回传输；
 *    - 访问会话未就绪时发起的传输排队并触发重新打开，就绪后依次发出；没有访问状态机时（窗口没开时重试）新建一个不挂窗口的；
 *    - 访问者一端的传输结束时经 IShareTransferFinishListener 交出结果，分享者一端的传输成功、写操作执行完时经
 *      IShareOwnerActivityListener 交出操作，模块据此弹出通知。
 *
 *  线程模型：除 PostSessionEvent 外的方法都只在 UI 线程上调用；PostSessionEvent 在任意线程上调用，
 *  只把事件放进由 m_eventMutex 保护的队列，队列由空变非空时向通知窗口投递一次消息。
 *
 *  典型用法（CFileShareModule 内部）：
 *      m_service.Attach(host, msgWnd, kMsgServiceEvents);
 *      m_service.OnLoggedIn();
 *      m_service.HandleServerFrame(cmd, seq, body);     //DrainFrames 里逐帧调用
 *      m_service.DrainEvents();                         //消息窗口收到 kMsgServiceEvents 时
 *      m_service.Detach();                              //Shutdown 时
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../FileShareApi.h"
#include "ShareNoticeModel.h"
#include "ShareLocalStore.h"
#include "ShareWorkerPool.h"
#include "ShareProtocolCodec.h"
#include "ShareProviderSession.h"
#include "ShareVisitor.h"
#include "ShareVisitorChannel.h"
#include "ShareTransferSession.h"

//文件共享传输的 P2P 业务方（Source/Feature/P2P/FolderP2PTransport.h）；本头文件只用到指针，前置声明即可
namespace balloonp2p {
class FolderP2PLinkBroker;
}

namespace fileshare {

//「我的共享」里一个共享在本机的提供状态（上线比对的结果，供「文件共享」窗口标注）
enum share_local_status
{
    share_local_status_reported      = 1,   //绑定本机且本机有记录：已用 2210 报告（能否提供看 available）
    share_local_status_missing_local = 2,   //绑定本机，但本机没有记录：无法提供，分享者可以取消
    share_local_status_other_device  = 3,   //绑定别的设备：位于另一台电脑，本机不报告
};

//审计上报（2230）每批最多带的记录条数：上万个文件的目录分若干批上报，每批的包体都远小于主连接的单帧上限
const size_t kShareAuditBatch = 200;

//访问者下载期间每隔多久把各文件的进度写一次续传记录，单位：毫秒（进程意外退出时最多损失这么久的进度）
const int64_t kShareResumeSaveIntervalMs = 5000;

//分享者每隔多久检查一次已报告的共享根是否仍可提供，单位：毫秒。U 盘拔出、目录被删或改名后最迟这么久补报 2210。
//不监听 WM_DEVICECHANGE：模块的消息窗口是纯消息窗口，收不到这条广播，而且目录被删、改名也不会有这条通知
const int64_t kShareRootCheckIntervalMs = 5000;

//发起、取消、调整成员等应答的超时，单位：毫秒。主连接正常时应答在一秒内到达，十秒仍未到达按超时处理
const int64_t kShareManageTimeoutMs = 10000;

//发起共享（2201）超时后用同一个 clientshareid 重发，最多发送的次数（含第一次）。服务端按「分享者 + clientshareid」
//去重，重发不会建出两条共享
const int kShareCreateMaxAttempts = 3;

//没有收到应答（超时、退出登录）时回给观察者的结果码。不是服务端的 error_code，取负数以免与之相撞
const int kShareManageNoResponse = -1;

//管理请求的种类
enum share_manage_kind
{
    share_manage_create = 1,   //发起共享（2201）
    share_manage_cancel = 2,   //取消共享（2202）
    share_manage_update = 3,   //调整成员（2204）
};

//「文件共享」窗口里「正在访问的人」的一项：一名访问者在某个共享上的访问会话与正在进行的传输
struct ShareActiveVisitor
{
    int64_t     m_visitorId;    //访问者 userid
    std::string m_visitorName;  //访问者显示名（服务端在 2222 里给的，UTF-8）
    int         m_downloads;    //正在进行的下载数
    int         m_uploads;      //正在进行的上传数
    int64_t     m_bytesDone;    //这些传输已完成的字节数之和
    int64_t     m_bytesTotal;   //这些传输的总字节数之和（还没有进度时为 0）

    ShareActiveVisitor()
        : m_visitorId(0)
        , m_downloads(0)
        , m_uploads(0)
        , m_bytesDone(0)
        , m_bytesTotal(0)
    {
    }
};

//一次管理请求的结果
struct ShareManageResult
{
    uint32_t             m_requestId;        //CreateShare / CancelShare / UpdateMembers 返回的编号
    int                  m_kind;             //请求种类，share_manage_kind
    int                  m_code;             //0 成功；服务端的错误码；kShareManageNoResponse 表示没有收到应答
    int64_t              m_shareId;          //共享 id：发起成功时为新共享的 id，取消与调整时为请求里的 id
    std::vector<int64_t> m_invalidMembers;   //回 819 / 821 时不满足条件的成员

    ShareManageResult()
        : m_requestId(0)
        , m_kind(0)
        , m_code(0)
        , m_shareId(0)
    {
    }
};

/**
 *  共享列表请求（2205）的观察者，由本会话共享浮层、「文件共享」窗口实现。回调都在 UI 线程上。
 */
class IShareListObserver
{
public:
    virtual ~IShareListObserver()
    {
    }

    /**
     *  一次列表请求有了结果。之后不再回调这个编号。
     *  @param requestId RequestList 返回的编号
     *  @param scope     请求的范围：kShareListScopeMine 或 kShareListScopeSharedMe
     *  @param code      0 成功；服务端的错误码；kShareManageNoResponse 表示没有收到应答
     *  @param shares    共享列表（只在本次调用期间有效）；失败时为空
     */
    virtual void OnShareListResult(uint32_t requestId, int scope, int code, const std::vector<ShareListEntry>& shares) = 0;
};

/**
 *  共享卡片状态变化的监听者（CShareConversationCounter 实现）。回调在 UI 线程上。
 */
class IShareCardStateListener
{
public:
    virtual ~IShareCardStateListener()
    {
    }

    /**
     *  一个共享对本人的状态或权限变了（2203、推给本人的 2206、2205 应答都会触发），新状态经 GetCardState 取得。
     *  @param shareId 共享 id
     */
    virtual void OnCardStateChanged(int64_t shareId) = 0;
};

//共享卡片的最新状态（聊天窗画卡片时取用）
struct ShareCardState
{
    int m_state;     //对本人的状态，share_state
    int m_reason;    //暂不可用或已失效的原因
    int m_myPerms;   //本人的写权限位；不知道（分享者本人、尚未收到）时为 -1

    ShareCardState()
        : m_state(0)
        , m_reason(0)
        , m_myPerms(-1)
    {
    }
};

/**
 *  管理请求的观察者，由发起共享对话框、调整成员窗口、「文件共享」窗口实现。回调都在 UI 线程上。
 */
class IShareManageObserver
{
public:
    virtual ~IShareManageObserver()
    {
    }

    /**
     *  一次管理请求有了结果（成功、被拒或没有应答）。之后不再回调这个编号。
     *  @param result 结果，只在本次调用期间有效
     */
    virtual void OnShareManageResult(const ShareManageResult& result) = 0;
};

/**
 *  访问者一侧传输的观察者，由浏览窗口实现。回调都在 UI 线程上。
 */
class IShareTransferObserver
{
public:
    virtual ~IShareTransferObserver()
    {
    }

    /**
     *  进度更新。
     *  @param localId    StartDownload / StartUpload 返回的编号
     *  @param bytesDone  已完成的字节数
     *  @param bytesTotal 总字节数
     *  @param filesDone  已完成的文件数
     *  @param filesTotal 文件总数
     */
    virtual void OnTransferProgress(uint32_t localId, int64_t bytesDone, int64_t bytesTotal, int64_t filesDone,
                                    int64_t filesTotal) = 0;

    /**
     *  传输结束（完成、失败或取消）。之后不再回调这个编号。
     *  @param localId 编号
     *  @param code    0 成功，否则为错误码
     */
    virtual void OnTransferFinished(uint32_t localId, int code) = 0;
};

/**
 *  一笔传输结束时的回调（第 4 期起，拖出任务用）：与观察者不同，浏览窗口关闭之后照样回调。回调在 UI 线程上，
 *  每笔传输至多一次。由发起方创建，经 ShareTransferOptions 交给 CShareService 共同持有。
 */
class IShareTransferCompletion
{
public:
    virtual ~IShareTransferCompletion()
    {
    }

    /**
     *  传输结束（完成、失败或取消）。
     *  @param code 0 成功，否则为错误码
     */
    virtual void OnTransferDone(int code) = 0;
};

//发起一笔访问者传输时的附加信息（第 4 期起）
struct ShareTransferOptions
{
    std::wstring m_label;     //传输列表与完成弹窗里显示的名称：一项时为它的名字，多项时为「第一项 等 N 项」
    bool         m_dragOut;   //是不是拖出到资源管理器的下载（结束时不弹完成窗）
    std::shared_ptr<IShareTransferCompletion> m_completion;   //结束回调（共同持有，可为空）

    ShareTransferOptions()
        : m_dragOut(false)
    {
    }
};

//浏览窗口重新打开时接回的一笔传输（AdoptVisitorTransfers 的结果）
struct ShareTransferSnapshot
{
    uint32_t     m_localId;     //本端传输编号
    int          m_direction;   //方向，share_transfer_direction
    std::wstring m_label;       //显示的名称
    int64_t      m_bytesDone;   //最近一次进度里已完成的字节数
    int64_t      m_bytesTotal;  //最近一次进度里的总字节数；还没有进度时为 0
    bool         m_queued;      //是否还在排队（2224 尚未发出）

    ShareTransferSnapshot()
        : m_localId(0)
        , m_direction(0)
        , m_bytesDone(0)
        , m_bytesTotal(0)
        , m_queued(false)
    {
    }
};

/**
 *  访问者一端传输结束的监听者（第 4 期起，模块的弹窗总控实现）。浏览窗口是否打开都会回调。回调在 UI 线程上。
 */
class IShareTransferFinishListener
{
public:
    virtual ~IShareTransferFinishListener()
    {
    }

    /**
     *  访问者一端的一笔传输结束了（完成、失败或取消）。
     *  @param result 结果，只在本次调用期间有效
     */
    virtual void OnVisitorTransferFinished(const ShareVisitorTransferResult& result) = 0;
};

/**
 *  分享者一端操作的监听者（第 4 期起，模块的弹窗总控实现）：访问者在本人的共享上完成了下载、上传、重命名、删除或移动。
 *  回调在 UI 线程上。
 */
class IShareOwnerActivityListener
{
public:
    virtual ~IShareOwnerActivityListener()
    {
    }

    /**
     *  访问者完成了一次操作（只含成功的部分）。
     *  @param activity 操作，只在本次调用期间有效
     */
    virtual void OnOwnerActivity(const ShareOwnerActivity& activity) = 0;
};

/**
 *  CShareService —— 文件共享 DLL 的业务总控（由 CFileShareModule 持有，进程内唯一）。不可拷贝。
 */
class CShareService : public IShareSessionEventSink
{
public:
    CShareService();
    virtual ~CShareService();

    CShareService(const CShareService&) = delete;
    CShareService& operator=(const CShareService&) = delete;

    /**
     *  模块初始化后登记宿主与通知窗口。
     *  @param host      宿主接口（不拥有，须在 Detach 之前一直有效），不可为 NULL
     *  @param notifyWnd 通知窗口：有新的提供会话事件时向它投递 notifyMsg
     *  @param notifyMsg 通知消息号（WM_APP 段，见 FileShareModule.cpp）
     */
    void Attach(IFileShareHost* host, HWND notifyWnd, UINT notifyMsg);

    /**
     *  模块关闭时调用：结束全部提供会话（等待尚在连接中的线程结束，最长约 5 秒）、关闭本机库、解除宿主。
     *  未 Attach 时调用、重复调用都是安全的。
     */
    void Detach();

    /**
     *  登录成功（含断线重连成功）：结束上一次登录遗留的提供会话，打开本账号的本机库，发 2205 拉取「我的共享」。
     */
    void OnLoggedIn();

    /**
     *  即将退出登录：结束全部提供会话，关闭本机库。
     */
    void OnLoggingOut();

    /**
     *  处理一帧服务端信令（UI 线程）。
     *  @param cmd  命令号，2200 段
     *  @param seq  帧序号；应答与请求的 seq 相同，推送为 0
     *  @param body 包体（UTF-8 JSON）
     */
    void HandleServerFrame(uint16_t cmd, uint32_t seq, const std::string& body);

    /**
     *  取出并处理排队的提供会话事件（UI 线程，通知窗口收到通知消息时调用）。
     */
    void DrainEvents();

    //IShareSessionEventSink：任意线程上投递一个事件
    virtual void PostSessionEvent(const ShareSessionEvent& ev) override;

    /**
     *  取一个共享在本机的提供状态（最近一次上线比对的结果）。
     *  @param shareId 共享 id
     *  @return share_local_status 的取值；不在「我的共享」里或尚未比对时返回 0
     */
    int GetLocalStatus(int64_t shareId) const;

    /**
     *  当前进行中的提供会话数（含正在连接、正在关闭的）。
     *  @return 会话数
     */
    size_t GetProviderSessionCount() const;

    /**
     *  取一个共享的访问状态机（浏览窗口打开时调用）；还没有时新建。每个共享至多一个。窗口关闭后仍在后台传输的状态机
     *  在这里被接回（状态可能已经是「就绪」，不会再报一次状态变化，窗口须自己按当前状态刷新界面）。
     *  @param shareId  共享 id
     *  @param observer 观察者（浏览窗口，不拥有）；已有状态机时替换为它
     *  @return 状态机（归本类所有，ReleaseVisitor 之前一直有效）；未 Attach 时返回 NULL
     */
    CShareVisitor* AcquireVisitor(int64_t shareId, IShareVisitorObserver* observer);

    /**
     *  浏览窗口关闭时调用：之后不再回调这个窗口（访问状态机与它发起的传输都不再回调）。这个共享上没有访问者一端的传输时
     *  结束访问会话、关闭通道、销毁状态机；有传输时（第 4 期起）保留访问会话，传输在后台继续，全部结束之后的那次定时检查
     *  再结束访问会话。
     *  @param shareId 共享 id；没有对应的状态机时调用无效果
     */
    void ReleaseVisitor(int64_t shareId);

    /**
     *  浏览窗口（重新）打开时接回这个共享上访问者一端的传输（第 4 期起）：此后这些传输的进度与结束都回调给 observer。
     *  @param shareId  共享 id
     *  @param observer 新的观察者（浏览窗口，不拥有），不可为 NULL
     *  @return 这些传输的名称、方向与进度，按编号从小到大
     */
    std::vector<ShareTransferSnapshot> AdoptVisitorTransfers(int64_t shareId, IShareTransferObserver* observer);

    /**
     *  登记两种通知的监听者（第 4 期起，模块的弹窗总控）。只有一组，后登记的替换先登记的。
     *  @param finishListener   访问者一端传输结束的监听者（不拥有），可为 NULL
     *  @param activityListener 分享者一端操作的监听者（不拥有），可为 NULL
     */
    void SetNoticeListeners(IShareTransferFinishListener* finishListener, IShareOwnerActivityListener* activityListener);

    //是否有这个共享的访问状态机（单元测试用）
    bool HasVisitor(int64_t shareId) const;

    //这个共享的访问状态机是否挂着浏览窗口（单元测试用）；没有状态机时返回 false
    bool IsVisitorAttached(int64_t shareId) const;

    /**
     *  定时检查（UI 线程，模块的消息窗口每秒调用一次）：驱动各访问状态机检查超时与空闲回收；分享者一端每隔
     *  kShareRootCheckIntervalMs 检查一次已报告的共享根，可提供与否有变化时补报 2210。
     */
    void OnTimer();

    /**
     *  访问者发起一次下载（发 2224）。服务端规定同一访问会话同时只能有一笔传输：这个共享上已有本端发起的传输（进行中或
     *  在排队）时，本次排在它们之后，前一笔结束后才发 2224（方案 5.4），排队期间 IsTransferQueued 为真。
     *  第 4 期起访问会话未就绪时同样排队并触发重新打开，就绪后再发；会话打开失败时以失败原因结束。这个共享还没有访问
     *  状态机时（浏览窗口没开时重试）新建一个不挂窗口的。
     *  @param shareId   共享 id
     *  @param items     要下载的条目（共享内的相对路径、是否目录、字节数；同一目录下）
     *  @param targetDir 本机目标目录（须已存在）
     *  @param observer  观察者（浏览窗口，不拥有，可为 NULL）
     *  @param options   显示的名称、是否拖出、结束回调
     *  @return 本次传输的编号（大于 0）；未 Attach、条目为空、主连接断开时返回 0
     */
    uint32_t StartDownload(int64_t shareId, const std::vector<ShareTransferEntry>& items, const std::wstring& targetDir,
                           IShareTransferObserver* observer, const ShareTransferOptions& options = ShareTransferOptions());

    /**
     *  访问者发起一次上传（在本机生成清单，发 2224）。与下载共用同一个按共享排队的规则，见 StartDownload。
     *  @param shareId    共享 id
     *  @param localItems 本机条目的完整路径（同一目录下）
     *  @param toDir      共享内的目标目录（相对共享根）
     *  @param observer   观察者（不拥有，可为 NULL）
     *  @param errCode    [出参] 返回 0 时的原因：清单生成失败的错误码，或主连接断开（817）
     *  @param options    显示的名称、结束回调（上传没有拖出）
     *  @return 本次传输的编号；失败返回 0
     */
    uint32_t StartUpload(int64_t shareId, const std::vector<std::wstring>& localItems, const std::string& toDir,
                         IShareTransferObserver* observer, int& errCode,
                         const ShareTransferOptions& options = ShareTransferOptions());

    /**
     *  访问者点「取消」取消一次传输。结束经观察者的 OnTransferFinished 回报（错误码 818），交给结束监听的结果标记为
     *  用户取消（不弹窗）。
     *  @param localId 编号；不存在时调用无效果
     */
    void CancelTransfer(uint32_t localId);

    /**
     *  一次传输是否还在排队（同一共享上前面的传输还没结束，2224 尚未发出）。
     *  @param localId StartDownload / StartUpload 返回的编号
     *  @return 在排队返回 true；已发出、已结束或不存在时返回 false
     */
    bool IsTransferQueued(uint32_t localId) const;

    /**
     *  一次传输此刻走的 P2P 链路实际路径（2026-10-04 起，供传输行上的「P2P」徽章）。在 UI 线程调用。
     *  @param localId StartDownload / StartUpload 返回的编号
     *  @return 已决定走 P2P 时返回链路路径（1 局域网直连、2 打洞直连、3 中转）；走 fileserver 中转、还没开始或不存在时返回 0
     */
    int TransferLinkPath(uint32_t localId) const;

    /**
     *  正在进行的传输数（含已发 2224 尚未建会话的）。
     *  @param isOwner 为真时数分享者一端的，否则数访问者一端的
     */
    size_t GetTransferCount(bool isOwner) const;

    /**
     *  发起共享：生成 clientshareid（params 里为空时）、写本机记录、发 2201。
     *  @param params   2201 的内容；成员不含本人
     *  @param rootPath 共享根的本机路径（磁盘为 "X:\"，文件夹不带结尾反斜杠，文件为完整路径）
     *  @param observer 观察者（不拥有，可为 NULL）；关闭前须调 DetachManageObserver
     *  @return 请求编号（大于 0）；未登录、本机库未打开、写本机记录或发送失败时返回 0
     */
    uint32_t CreateShare(const ShareCreateParams& params, const std::wstring& rootPath, IShareManageObserver* observer);

    /**
     *  取消共享（发 2202）：分享者本人，或群共享所在群的群主、管理员。成功后本机若是分享者，删除本机记录并结束
     *  该共享上的会话（服务端推来的 2203 也会做同样的事，两者先到的那个生效）。
     *  @param shareId  共享 id
     *  @param observer 观察者（不拥有，可为 NULL）
     *  @return 请求编号；发送失败返回 0
     */
    uint32_t CancelShare(int64_t shareId, IShareManageObserver* observer);

    /**
     *  调整成员（发 2204），仅分享者本人。三组都为空时不发送，返回 0。
     *  @param shareId  共享 id
     *  @param add      新增的成员及其权限
     *  @param remove   移除的成员
     *  @param update   改权限的成员
     *  @param observer 观察者（不拥有，可为 NULL）
     *  @return 请求编号；没有变化或发送失败返回 0
     */
    uint32_t UpdateMembers(int64_t shareId, const std::vector<ShareMemberPerm>& add, const std::vector<int64_t>& remove,
                           const std::vector<ShareMemberPerm>& update, IShareManageObserver* observer);

    /**
     *  观察者即将销毁：它发起的、尚未有结果的请求照常进行，只是不再回调它。
     *  @param observer 观察者
     */
    void DetachManageObserver(IShareManageObserver* observer);

    /**
     *  图片预览图的本机缓存目录：<个人目录>\ChatLog\FileSharePreview，不存在时创建。
     *  @return 目录（不带结尾反斜杠）；未登录或创建失败时为空串
     */
    std::wstring GetPreviewCacheDir();

    /**
     *  为一次拖出取一个新的本机临时目录：<个人目录>\ChatLog\FileShareDrag\<编号>。上一级目录不存在时创建，
     *  返回的目录本身不创建（开始下载时再建）。目录由拖出任务在最后一个引用释放时删除；登录时打开本机库会清掉
     *  上次进程异常退出时留下的整个 FileShareDrag 目录。
     *  @return 目录（不带结尾反斜杠）；未登录或创建失败时为空串
     */
    std::wstring NewDragTempDir();

    //尚未有结果的管理请求数（单元测试用）
    size_t GetPendingManageCount() const;

    //本人 userid（登录后经宿主取得）；未登录时为 0
    int64_t GetUserId() const;

    /**
     *  拉取共享列表（发 2205）。
     *  @param scope    kShareListScopeMine（我的共享）或 kShareListScopeSharedMe（共享给我的）
     *  @param ctype    会话类型；为 0 时不按会话筛选
     *  @param cid      会话 id（本人视角：单聊为对方 userid，群聊为群 id）
     *  @param observer 观察者（不拥有，可为 NULL）；关闭前须调 DetachListObserver
     *  @return 请求编号（大于 0）；发送失败返回 0
     */
    uint32_t RequestList(int scope, int ctype, int64_t cid, IShareListObserver* observer);

    //观察者即将销毁：它发起的、尚未有结果的列表请求不再回调它
    void DetachListObserver(IShareListObserver* observer);

    /**
     *  登记共享卡片状态变化的监听者（只有一个，后登记的替换先登记的）。
     *  @param listener 监听者（不拥有），为 NULL 时取消登记；须在销毁前取消登记
     */
    void SetCardStateListener(IShareCardStateListener* listener);

    /**
     *  取一个共享卡片的最新状态（来自 2203、推给本人的 2206、2205 应答）。
     *  @param shareId 共享 id
     *  @param out     [出参] 状态
     *  @return 有记录返回 true；本次登录还没有收到过这个共享的任何状态时返回 false
     */
    bool GetCardState(int64_t shareId, ShareCardState& out) const;

    /**
     *  本机作为分享者时，某个共享上正在访问的人（「文件共享」窗口用）：每名访问者一项，带上他正在进行的传输。
     *  只有访问会话、没有传输的访问者也列出（正在浏览）；同一访问者的多个访问会话合为一项。
     *  @param shareId 共享 id
     *  @return 按访问者 userid 升序
     */
    std::vector<ShareActiveVisitor> GetActiveVisitors(int64_t shareId) const;

    /**
     *  某个共享的最近操作记录（分享者一端写的 t_share_activity），最新的在前。
     *  @param shareId 共享 id
     *  @param limit   最多取的条数
     *  @param out     [出参] 记录
     *  @return 本机库已打开且读取成功返回 true
     */
    bool ListActivities(int64_t shareId, size_t limit, std::vector<ShareActivityRecord>& out);

    /**
     *  取本机作为分享者时某个共享的本机记录（「文件共享」窗口的「在本机打开」用）。
     *  @param shareId 共享 id
     *  @param out     [出参] 本机记录
     *  @return 本机库已打开且有这条记录返回 true
     */
    bool GetLocalRecord(int64_t shareId, ShareLocalRecord& out);

    /**
     *  改等待管理请求应答的超时（单元测试用，正式流程固定为 kShareManageTimeoutMs）。
     *  @param timeoutMs 超时，单位：毫秒，大于 0
     */
    void SetManageTimeoutMs(int64_t timeoutMs);

private:
    //处理 2205 应答：只处理本类发出的「我的共享」请求（按 seq 配对）
    void HandleListResponse(uint32_t seq, const std::string& body);

    //处理 2203：共享对本人的状态变化
    void HandleStateNotify(const std::string& body);

    //处理 2206：权限变化
    void HandlePermsNotify(const std::string& body);

    //处理 2222：中转参数
    void HandleRelayInfo(const std::string& body);

    //处理 2223：访问会话或传输子会话已结束
    void HandleAccessEnded(const std::string& body);

    //处理只有 {code, msg} 的应答：记一行日志
    void HandleSimpleResponse(uint16_t cmd, uint32_t seq, const std::string& body);

    //打开本账号的本机库；成功返回 true
    bool OpenStore(const FileShareSessionInfo& info);

    //经宿主发一条信令；返回帧 seq，失败返回 0（并记警告）
    uint32_t SendCommand(uint16_t cmd, const std::string& body);

    //发 2210
    void SendServeReport(const std::vector<ShareServeItem>& items);

    //发 2221 结束一个访问会话
    void SendCloseAccess(const std::string& accessId, int reason);

    /**
     *  结束一个提供会话：关闭成功时销毁；连接线程仍在进行时移进 m_closingSessions，等事件到达后再关。
     *  @param accessId   访问会话 id
     *  @param byeErrCode 连接已建立时 sf_bye 的结束原因；CShareProviderSession::kShareNoBye 表示不发
     *  @return 找到了该会话返回 true
     */
    bool CloseProviderSession(const std::string& accessId, int byeErrCode);

    /**
     *  结束全部提供会话。
     *  @param byeErrCode       sf_bye 的结束原因
     *  @param waitConnecting   是否等待尚在连接中的线程结束（模块关闭时为真；退出登录时为假，这些会话
     *                          移进 m_closingSessions 等事件到达后再关）
     */
    void CloseAllProviderSessions(int byeErrCode, bool waitConnecting);

    //处理一个事件
    void HandleEvent(const ShareSessionEvent& ev);

    //处理访问通道的事件（类型 11~14）
    void HandleVisitorEvent(const ShareSessionEvent& ev);

    //处理 2220 应答：按 seq 交给对应的访问状态机
    void HandleOpenResponse(uint32_t seq, const std::string& body);

    //释放全部访问状态机；waitConnecting 为真时等待尚在连接中的通道线程结束（模块关闭时）
    void ReleaseAllVisitors(bool waitConnecting);

    //一个访问状态机与它的传输层、通道，定义在 .cpp
    class VisitorLink;

    /**
     *  新建一个共享的访问状态机（第 4 期起由 AcquireVisitor 与「没有窗口时发起传输」共用）。
     *  @param shareId 共享 id（调用方已确认还没有）
     *  @param window  挂着的浏览窗口（不拥有），没有窗口时为 NULL
     *  @return 新建的连接（归 m_visitors 所有）
     */
    VisitorLink* CreateVisitorLink(int64_t shareId, IShareVisitorObserver* window);

    /**
     *  结束一个共享的访问会话并销毁访问状态机：取消它残留的传输，会话已建立时发 sf_bye 与 2221，关闭通道（仍在连接时挂起）。
     *  @param shareId 共享 id；没有对应的状态机时调用无效果
     */
    void DestroyVisitorLink(int64_t shareId);

    //定时检查：没有窗口、也没有访问者一端传输的访问状态机，结束访问会话并销毁（第 4 期起）
    void ReapDetachedVisitors();

    //这个共享上有没有访问者一端的传输（进行中、等应答或排队的都算）
    bool HasVisitorTransfers(int64_t shareId) const;

    /**
     *  访问状态机的状态变了（VisitorLink 在转告窗口之后调用，第 4 期起）：变为就绪时发出排队的传输，变为失败时以失败原因
     *  结束还在排队的传输。
     *  @param shareId 共享 id
     *  @param state   新状态，share_visitor_state
     *  @param errCode 失败的原因；其余状态为 0
     */
    void OnVisitorLinkState(int64_t shareId, int state, int errCode);

    //以 errCode 结束这个共享上还在排队（2224 尚未发出）的访问者传输
    void FailQueuedTransfers(int64_t shareId, int errCode);

    //分享者一端一次写操作执行完：只计成功的条目，归纳成一次操作交给操作监听（第 4 期起）
    void NotifyOwnerWrite(const std::vector<ShareAuditEntry>& audits);

    //补全一次分享者操作：访问者的后备名字（按访问会话找提供会话）、共享名称、要选中的本机路径、单文件共享的文件名
    void CompleteOwnerActivity(ShareOwnerActivity& activity, const std::string& accessId);

    //取消一笔传输（内部各种原因，不标记用户取消）
    void CancelTransferInternal(uint32_t localId);

    //为访问状态机建访问通道并开始连接；返回是否已开始
    bool StartVisitorChannel(VisitorLink& link, const ShareRelayParams& relay);

    //关闭访问状态机的通道：连接线程仍在进行时移进 m_closingChannels
    void CloseVisitorChannel(VisitorLink& link);

    //当前时间（毫秒，单调递增），交给访问状态机计算超时
    static int64_t NowMs();

    //一次传输的全部状态，定义在 .cpp
    struct TransferEntry;

    //一笔访问者传输结束：整理结果交给结束监听（第 4 期起）
    void NotifyVisitorFinished(const TransferEntry& entry, int code);

    //分享者一端一笔传输成功：归纳成一次操作交给操作监听（第 4 期起）
    void NotifyOwnerTransfer(const TransferEntry& entry);

    //处理 2224 应答：按 seq 找到访问者发起的那次传输，记下 transferid 或以错误结束
    void HandleTransferResponse(uint32_t seq, const std::string& body);

    //访问者收到传输子会话的 2222：建会话并启动
    void StartVisitorTransferSession(const ShareRelayParams& relay);

    //分享者收到传输子会话的 2222：按方向建发送端或接收端
    void StartOwnerTransferSession(const ShareRelayParams& relay);

    /**
     *  为一次传输建 P2P 通道工厂（docs/P2P通信方案.md 第 6 期）：2222 带 p2psessionid、且本端已登记用途 fileshare 时
     *  才建，功能会话号就是 transferid。
     *  @param relay 传输子会话的中转参数
     *  @return 通道工厂（交给 CShareTransferSession 持有）；不走 P2P 时为空
     */
    std::shared_ptr<foldertransfer::IFolderLaneTransportFactory> MakeTransportFactory(const ShareRelayParams& relay);

    //处理传输子会话的事件（类型 21~23）
    void HandleTransferEvent(const ShareSessionEvent& ev);

    //结束一次传输：发 2225、整理续传记录 / 半成品 / 审计、通知观察者、回收会话
    void FinishTransfer(uint32_t localId, int code);

    /**
     *  上报访问者一次写操作的审计记录（2230，按 kShareAuditBatch 分批），每条再记一条本机最近操作。
     *  @param audits 审计记录（来自分享者一端的请求处理器）
     */
    void ReportWriteAudit(const std::vector<ShareAuditEntry>& audits);

    //按 transferid 找传输；没有返回 0
    uint32_t FindTransfer(const std::string& transferId) const;

    //取消一个共享上的传输：owner 为真时取消分享者一端的（visitorId 非 0 时只取消该访问者的），否则取消访问者一端的
    void CancelTransfersOfShare(int64_t shareId, bool owner, int64_t visitorId);

    /**
     *  结束属于一个访问会话的全部传输（两种角色都算）。服务端结束访问会话时（2223 不带 transferid，例如成员被移除）
     *  一并结束它下面的传输子会话，否则中转上的数据通道不受影响、传输会照常进行到底。
     *  @param accessId 访问会话 id
     *  @param errCode  服务端给出的原因；还没有建会话的传输按它收尾，已建会话的经取消收尾
     *  @return 结束的传输个数
     */
    size_t EndTransfersOfAccess(const std::string& accessId, int errCode);

    //结束全部传输并等待线程退出（退出登录与模块关闭时）
    void StopAllTransfers();

    //访问者下载：把各文件的进度写进续传记录 / 删除续传记录
    void SaveDownloadResume(TransferEntry& entry);
    void ClearDownloadResume(TransferEntry& entry);

    //分享者：按文件批量上报审计，并记一条最近操作
    void ReportTransferAudit(TransferEntry& entry, int code);

    //分享者上传失败：删除半成品；无论成败都从正在上传的登记表里去掉
    void CleanupUploadParts(TransferEntry& entry, bool deleteFiles);

    //把访问状态机上正在进行的传输数同步过去（有传输时不做空闲回收）
    void UpdateVisitorTransferCount(int64_t shareId);

    /**
     *  把访问者新建的一笔传输放进传输表：这个共享上已有本端发起的传输时排队，否则立即发 2224。
     *  @param entry      已填好方向、共享、访问会话、条目等的传输（转移所有权）
     *  @param items      2224 的顶层条目
     *  @param totalBytes 总字节数
     *  @param totalFiles 总文件数
     *  @return 本端编号；立即发送失败时返回 0
     */
    uint32_t SubmitVisitorTransfer(std::unique_ptr<TransferEntry> entry, const std::vector<ShareTransferEntry>& items,
                                   int64_t totalBytes, int64_t totalFiles);

    //一个共享上访问者一端的传输结束后：没有进行中的传输时，发出排在最前面的那一笔的 2224
    void StartNextQueuedTransfer(int64_t shareId);

    //检查已报告的共享根是否仍可提供，有变化的补报 2210
    void CheckRootAvailability();

    //一次管理请求的全部状态，定义在 .cpp
    struct ManageRequest;

    //处理 2201 / 2202 / 2204 的应答：按 seq 找到请求（2201 另按 clientshareid 找，兜住重发前那一次的迟到应答）
    void HandleManageResponse(uint16_t cmd, uint32_t seq, const std::string& body);

    //检查管理请求是否超时：发起共享未到次数上限时重发，其余按没有应答结束
    void CheckManageTimeouts(int64_t now);

    //结束一次管理请求：从表里去掉，发起失败时删除本机记录，再回调观察者
    void FinishManage(uint32_t requestId, ShareManageResult& result);

    //结束全部管理请求（退出登录、模块关闭时），结果为 kShareManageNoResponse
    void FailAllManage();

    //登录时删除上一次进程遗留的、还没有 shareid 的本机记录（正在发起中的除外）
    void RemoveStalePendingShares();

    //本机作为分享者的共享已失效：删除本机记录、取消传输、结束提供会话；本机没有它的记录时不做任何事
    void DropOwnShare(int64_t shareId, int reason);

    //更新一个共享卡片的状态；notifyHost 为真且有变化时经宿主通知主程序刷新卡片。myPerms 为 -1 时保留原值
    void UpdateCardState(int64_t shareId, int state, int reason, int myPerms, bool notifyHost);

    //一次界面发起的列表请求
    struct ListRequest
    {
        int                 m_scope;      //请求的范围
        int64_t             m_deadlineMs; //等待应答的截止时间（NowMs 的时间轴）
        IShareListObserver* m_observer;   //观察者（不拥有，可为 NULL）

        ListRequest()
            : m_scope(0)
            , m_deadlineMs(0)
            , m_observer(NULL)
        {
        }
    };

    //结束一次列表请求：从表里去掉再回调观察者
    void FinishList(uint32_t seq, int code, const std::vector<ShareListEntry>& shares);

private:
    //提供会话表：访问会话 id → 会话
    typedef std::map<std::string, std::unique_ptr<CShareProviderSession> > SessionMap;

    IFileShareHost*      m_host;              //宿主（不拥有）；未 Attach 时为 NULL
    //用途 fileshare 的 P2P 业务方（拥有：构造时建、析构时删）。Attach 时宿主给了 P2P 服务才登记，Detach 时在全部传输
    //停止之后注销；没登记时 2222 带了 p2psessionid 也不走 P2P
    balloonp2p::FolderP2PLinkBroker* m_p2pBroker;
    HWND                 m_notifyWnd;         //通知窗口；未 Attach 时为 NULL
    UINT                 m_notifyMsg;         //通知消息号
    CShareLocalStore     m_store;             //本账号的本机库；登录时打开、退出登录时关闭
    CShareWorkerPool     m_workers;           //分享者一端执行搜索与写操作的工作线程池；Attach 时启动、Detach 时停止
    ULONG_PTR            m_gdiplusToken;      //本模块的 GDI+ 令牌（生成缩略图、解码缩略图用）；Attach 时启动、Detach 时
                                              //在线程池停止之后关闭；0 表示未启动
    std::wstring         m_storePath;         //本机库当前打开的文件路径；断线重连时路径不变就不重开
    uint32_t             m_dragSeq;           //拖出临时目录名里的序号，每取一个加一（同一毫秒内多次取也不重名）
    int64_t              m_userId;            //本人 userid；未登录时为 0
    uint32_t             m_listMineSeq;       //尚未收到应答的「我的共享」2205 请求的 seq；没有时为 0
    std::map<int64_t, int> m_localStatus;     //最近一次上线比对的结果：shareid → share_local_status
    std::map<int64_t, bool> m_rootAvailable;  //最近一次报告的共享根能否提供：shareid → 能否提供（只含已报告的共享）
    int64_t              m_lastRootCheckMs;   //最近一次检查共享根的时间
    SessionMap           m_sessions;          //进行中的提供会话
    SessionMap           m_closingSessions;   //已要求关闭、但连接线程尚未结束的会话

    //访问状态机表：共享 id → 状态机及其传输层、通道
    std::map<int64_t, std::unique_ptr<VisitorLink> > m_visitors;
    //已要求关闭、但连接线程尚未结束的访问通道：访问会话 id → 通道
    std::map<std::string, std::unique_ptr<CShareVisitorChannel> > m_closingChannels;

    //传输表：本端编号 → 传输
    std::map<uint32_t, std::unique_ptr<TransferEntry> > m_transfers;
    uint32_t                       m_nextTransferId;   //下一个本端传输编号，从 1 起递增
    bool                           m_holdTransferQueue;//为真时结束传输不发起排队的下一笔（批量结束传输的过程中）

    //管理请求表：请求编号 → 请求
    std::map<uint32_t, std::unique_ptr<ManageRequest> > m_manage;
    uint32_t                       m_nextManageId;     //下一个管理请求编号，从 1 起递增
    int64_t                        m_manageTimeoutMs;  //等待管理请求应答的超时，单位：毫秒

    //界面发起的列表请求：帧 seq → 请求（seq 就是请求编号）
    std::map<uint32_t, ListRequest> m_listRequests;

    //共享卡片的最新状态：shareid → 状态；退出登录时清空
    std::map<int64_t, ShareCardState> m_cardStates;
    IShareCardStateListener*          m_cardStateListener;   //卡片状态变化的监听者（不拥有），可为 NULL

    //本人发起的共享的名称（UTF-8）：shareid → 名称。取自「我的共享」列表的应答与发起成功时的参数，分享者操作通知里显示；
    //退出登录时清空
    std::map<int64_t, std::string>    m_ownShareNames;
    IShareTransferFinishListener*     m_finishListener;      //访问者一端传输结束的监听者（不拥有），可为 NULL
    IShareOwnerActivityListener*      m_activityListener;    //分享者一端操作的监听者（不拥有），可为 NULL

    std::mutex                     m_eventMutex;   //保护 m_events
    std::deque<ShareSessionEvent> m_events;       //待 UI 线程处理的事件，先进先出
};

} // namespace fileshare
