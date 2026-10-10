/**
 *  分享者一端一个访问会话的请求处理器，ShareProvider.h
 *
 *  访问者打开共享后，服务端经 2222 把中转参数推给分享者；分享者连上 fileserver、完成报到（lane 0），
 *  之后这条控制通道上的全部应用帧（docs/文件共享方案.md 5.3）都交给本类处理：
 *    - Start：握手后首先发 sf_hello，告诉访问者协议版本、共享类型、根名、是否含隐藏文件与此刻的权限；
 *    - sf_list：先查权限、再按 9.1 的顺序校验路径（写法与拼接 → 重解析点 → 隐藏属性），然后列目录、按游标逐批
 *      回 sf_list_result（拉取方式：访问者首次不带游标，more 为真时带上一帧的游标再取）。同一目录的条目在
 *      取完之前缓存在本对象里，后续各批不再重新列目录；
 *    - sf_stat：同样的校验后查一个条目，回 sf_stat_result；
 *    - sf_bye：访问者主动结束，转告监听者，由它关闭通道；
 *    - sf_mkdir / sf_rename / sf_delete / sf_move（第 2 期第 2 步）：先查权限，再在「同一个共享串行」的锁里交
 *      ShareWriteOps 执行，回 sf_op_result（多条目操作逐条给出结果），并把每一项的结果作为审计记录交给监听者上报；
 *    - sf_search（第 2 期第 3 步）：校验关键字与起点目录后交 ShareSearch 执行，命中按批回 sf_search_result，
 *      条数与耗时上限来自 2222；失败时回一个带结果码的、已结束的空结果；sf_cancel 中止该请求号的搜索；
 *    - sf_thumb（第 2 期第 4 步）：校验路径后交 ShareThumbnail 在工作线程上生成，回 sf_thumb_result（JPEG 作为附件）；
 *      尚未开始的可经 sf_cancel 取消（取消后不回结果）。
 *  会话中途权限变化（2206）时调用 SetPerms：之后的请求按新权限判定，并发 sf_perms 让访问者刷新界面。
 *
 *  线程模型：OnFrame 在中转通道的接收线程上调用，列目录缓存只在这条线程上读写；SetPerms、SendBye 在 UI 线程上
 *  调用，只读写原子化的权限位并经通道发帧（IShareChannel 的实现须保证发送线程安全）。写操作与搜索包成任务交给上下文里
 *  的工作线程池（方案 7.5；没有线程池时在接收线程上同步执行），同一个共享上的写操作经进程内的锁串行。任务与本对象
 *  共享一个核心对象（通道、监听者、正在进行的搜索的取消标志）：本对象销毁时断开核心并置位全部取消标志，之后才执行完的
 *  任务不再发帧、不再回报，因此销毁时不必等任务结束。
 *
 *  单元测试见 FlamingoFileShare/Tests/ShareProviderTests.cpp（标签 [shareprovider]，用假通道收集发出的帧）。
 *
 *  典型用法（ShareService 在 lane 0 报到成功后）：
 *      CShareProvider provider(ctx, &channel, &listener);
 *      provider.Start();
 *      ...（通道接收线程上）provider.OnFrame(data, len);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include <string>
#include <vector>

#include <memory>

#include "ShareControlFraming.h"
#include "SharePermission.h"
#include "ShareProtocolCodec.h"   //ShareAuditEntry
#include "ShareRecycle.h"
#include "ShareSearch.h"
#include "ShareWorkerPool.h"

namespace fileshare {

//分享者在收到访问者的第一帧之前重发 sf_hello 的间隔，单位：毫秒。fileserver 在中转两端都报到之前会丢弃数据帧，
//取 300 毫秒：访问者晚到时最多多等这么久，重发的帧很小，持续到 opentimeout 为止也只有几十帧
const int kShareHelloResendMs = 300;

//2222 里没有带 opentimeoutms 时，分享者重发 sf_hello 的最长时间，单位：毫秒（与服务端 shareopentimeout 的默认值一致）
const int kShareDefaultOpenTimeoutMs = 15000;

/**
 *  访问会话控制通道的发送端抽象。真实实现包装 FolderRelayClient 的 lane 0；单元测试用假通道收集发出的帧。
 *  SendFrame 可能在接收线程与 UI 线程上同时被调用，实现方须保证线程安全。
 */
class IShareChannel
{
public:
    virtual ~IShareChannel()
    {
    }

    /**
     *  发出一个应用帧。
     *  @param frame 帧包体（ShareControlFraming 的产物）
     *  @return 已发出返回 true；通道已断开或帧超过上限时返回 false
     */
    virtual bool SendFrame(const std::string& frame) = 0;
};

/**
 *  请求处理器的事件监听者，由 ShareService 实现。回调在中转通道的接收线程上触发，实现方只能做轻量工作
 *  （典型做法是投递消息到 UI 线程）。
 */
class IShareProviderListener
{
public:
    virtual ~IShareProviderListener()
    {
    }

    /**
     *  访问者发来 sf_bye，主动结束访问会话。
     *  @param accessId 访问会话 id
     *  @param errCode  访问者给出的结束原因，0 为正常关闭
     */
    virtual void OnVisitorBye(const std::string& accessId, int errCode) = 0;

    /**
     *  访问者的一次写操作执行完毕（无论成败都回报，含权限不足被拒的）。默认什么也不做。
     *  @param accessId 访问会话 id
     *  @param audits   审计记录，每个条目一条（共享 id、访问会话、访问者、动作、路径、目标、结果、本机时间都已填好）
     */
    virtual void OnWriteOpDone(const std::string& accessId, const std::vector<ShareAuditEntry>& audits)
    {
        (void)accessId;
        (void)audits;
    }
};

//一个访问会话在分享者一端的上下文：来自本机的共享记录与 2222 推给分享者的那一帧
struct ShareProviderContext
{
    std::string  m_accessId;        //访问会话 id（也是中转 sessionid）
    int64_t      m_shareId;         //共享 id
    int64_t      m_visitorId;       //访问者 userid
    std::string  m_visitorName;     //访问者显示名（日志与「正在访问的人」用）
    std::wstring m_rootPath;        //共享根的本机路径，来自本机 t_share_local
    int          m_kind;            //共享类型，share_kind
    bool         m_includeHidden;   //是否包含隐藏文件与系统文件
    int          m_perms;           //访问者此刻的写权限位，share_perm 的组合
    int          m_searchMaxResults;//一次搜索最多返回的条数，来自 2222；0 或负数时取 kShareSearchDefaultMaxResults
    int          m_searchTimeoutMs; //一次搜索最长耗时毫秒数，来自 2222；0 或负数时取 kShareSearchDefaultTimeoutMs
    CShareWorkerPool* m_pool;       //执行搜索与写操作的工作线程池（不拥有，须在本会话结束之前一直有效）；NULL 时同步执行

    ShareProviderContext()
        : m_shareId(0)
        , m_visitorId(0)
        , m_kind(0)
        , m_includeHidden(false)
        , m_perms(0)
        , m_searchMaxResults(0)
        , m_searchTimeoutMs(0)
        , m_pool(NULL)
    {
    }
};

class CShareProviderCore;

/**
 *  CShareProvider —— 分享者一端一个访问会话的请求处理器。不可拷贝。
 */
class CShareProvider
{
public:
    /**
     *  @param ctx      访问会话的上下文（拷贝保存）
     *  @param channel  控制通道（不拥有，须在本对象销毁之前一直有效），不可为 NULL
     *  @param listener 事件监听者（不拥有，可为 NULL；非 NULL 时须在本对象销毁之前一直有效）
     */
    CShareProvider(const ShareProviderContext& ctx, IShareChannel* channel, IShareProviderListener* listener);

    //断开与在途任务共享的核心并置位全部取消标志；不等任务结束
    ~CShareProvider();

    CShareProvider(const CShareProvider&) = delete;
    CShareProvider& operator=(const CShareProvider&) = delete;

    /**
     *  发出握手后的首帧 sf_hello 并记一行日志。lane 0 报到成功后调用一次。
     *  @return 已发出返回 true
     */
    bool Start();

    /**
     *  重发一次 sf_hello（不记日志）。fileserver 在两端都报到之前会丢弃数据帧，而分享者与访问者谁先报到并不确定，
     *  所以分享者在收到访问者的第一帧之前每隔 kShareHelloResendMs 重发一次（见 CShareProviderSession），
     *  访问者只处理第一个 sf_hello。
     *  @return 已发出返回 true
     */
    bool ResendHello();

    /**
     *  是否已经收到过访问者的帧（任何一帧，包括解不开的）。收到即说明中转两端都已报到，sf_hello 不必再重发。
     *  可在任意线程调用。
     *  @return 收到过返回 true
     */
    bool HasHeardFromVisitor() const;

    /**
     *  处理访问者送来的一个应用帧，按需回应答帧。在中转通道的接收线程上调用。
     *  解不开的帧、方向不对的帧（访问者不应发送的 *_result、sf_hello 等）记一条警告后忽略，不结束会话。
     *  @param data 应用帧字节（借用，只在本次调用期间有效）
     *  @param len  字节数
     */
    void OnFrame(const char* data, size_t len);

    /**
     *  会话中途权限变化：之后的请求按新权限判定，并发 sf_perms 通知访问者。可在 UI 线程上调用。
     *  @param perms 新的写权限位
     *  @return sf_perms 已发出返回 true
     */
    bool SetPerms(int perms);

    /**
     *  主动结束访问会话前发出最后一帧 sf_bye。可在 UI 线程上调用。
     *  @param errCode 结束原因，0 为正常关闭（如分享者取消共享时为 error_code_share_canceled）
     *  @return 已发出返回 true
     */
    bool SendBye(int errCode);

    //访问会话 id
    const std::string& GetAccessId() const;

    //共享 id
    int64_t GetShareId() const;

    //访问者 userid
    int64_t GetVisitorId() const;

    //访问者此刻的写权限位
    int GetPerms() const;

    /**
     *  替换删除用的回收站（单元测试注入假实现）。须在第一个写请求到达之前调用。
     *  @param recycler 回收站（不拥有，须在本对象及其全部在途任务结束之前一直有效）；传 NULL 恢复为系统回收站
     */
    void SetRecycler(IShareRecycler* recycler);

    //正在进行（含排队）的搜索与缩略图请求个数（测试用）
    size_t GetRunningSearchCount() const;

    /**
     *  取共享根的显示名：盘符根目录为 "X:"，其余为路径最后一段（UTF-8）。sf_hello 的 rootname 取这个值。
     *  @param rootPath 共享根的本机路径
     *  @return 显示名
     */
    static std::string RootDisplayName(const std::wstring& rootPath);

private:
    //处理 sf_list：校验后列目录（或沿用缓存），回一批 sf_list_result
    void HandleList(const ShareControlMessage& req);

    //处理 sf_stat：校验后查条目，回 sf_stat_result
    void HandleStat(const ShareControlMessage& req);

    //处理四种写操作：查权限后包成任务（串行执行、回 sf_op_result、回报审计）
    void HandleWriteOp(const ShareControlMessage& req);

    //处理 sf_search：校验后包成搜索任务；校验失败时回带结果码的、已结束的空结果
    void HandleSearch(const ShareControlMessage& req);

    //处理 sf_cancel：中止该请求号的搜索或缩略图
    void HandleCancel(const ShareControlMessage& req);

    //处理 sf_thumb：校验后包成缩略图任务；校验失败时直接回带结果码的 sf_thumb_result
    void HandleThumb(const ShareControlMessage& req);

    //有线程池时投递任务，否则（或投递失败时）在本线程上立即执行
    void PostOrRun(std::unique_ptr<IShareTask> task);

    /**
     *  按方案 9.1 的顺序校验访问者送来的相对路径：写法与拼接、重解析点、隐藏属性。
     *  @param relPath 相对路径（UTF-8）
     *  @param absPath [出参] 本机绝对路径
     *  @return error_code_ok；写法非法或越出共享根为 808；共享未含隐藏文件而路径上有隐藏条目为 809
     */
    int ResolveRequestPath(const std::string& relPath, std::wstring& absPath) const;

    //编码并发出一帧；编码失败或通道发送失败时记一条警告并返回 false
    bool SendControlMessage(const ShareControlMessage& msg);

    //回一个只带结果码的 sf_list_result
    void SendListError(int64_t reqId, int code);

    //构造并发出 sf_hello；Start 与 ResendHello 共用
    bool SendHello();

private:
    ShareProviderContext     m_ctx;           //访问会话的上下文；构造后只读
    IShareChannel*           m_channel;       //控制通道（不拥有）
    IShareProviderListener*  m_listener;      //事件监听者（不拥有，可为 NULL）
    SharePermission          m_permission;    //共享类型与访问者的写权限位；权限位可在 UI 线程上更新
    std::atomic<bool>        m_heardVisitor;  //是否已收到过访问者的帧；接收线程置真，连接线程读取以决定是否停止重发 sf_hello

    //列目录缓存：只在接收线程上读写。访问者取完一个目录的全部批次后清空
    std::string              m_listPath;      //缓存的目录（相对路径，UTF-8）；无缓存时与 m_listValid 一起失效
    std::vector<ShareEntry>  m_listEntries;   //该目录的全部条目
    bool                     m_listValid;     //缓存是否有效

    std::shared_ptr<CShareProviderCore> m_core; //与在途任务共享的核心（通道、监听者、注入的回收站、搜索的取消标志）
};

} // namespace fileshare
