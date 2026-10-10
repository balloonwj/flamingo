/**
 *  聊天图片传输管理器（客户端侧，发送方向）。
 *
 *  把一次「发图」拆成的多步异步流程收口到一个串行工作线程里，按 clientmsgid /
 *  帧 seq 关联，UI 只管发起与显示进度：
 *    ① 读本地图片、算 MD5、用 GDI+ 取宽高；
 *    ② 在 chatserver 上 cmd_request_upload(1801) 申请上传（异步，应答由
 *       CIUSocket::HandleV2Frame 转交给本类的等待槽）；
 *    ③ 命中秒传则跳过字节流，否则连 imgserver、用 ImageUploadDriver 跑
 *       cmd_file_upload(1851) 分片上传，过程回投上传进度给 UI；
 *    ④ 上传完成后在 chatserver 上 cmd_send_message(1503, msgtype=2) 发出图片
 *       消息——发送回执沿用既有的 FMG_MSG_V2_SEND_CHATMSG_ACK 路径。
 *
 *  信令走 chatserver 的异步帧管道，字节流走 imgserver 的阻塞 socket（本线程
 *  独占、不占主连接、不卡 UI）。本类是单例，worker 线程在 CIUSocket::Init /
 *  Uninit 处启停。本期只做发送，接收（下载渲染）留后续。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include "FileProtocolClient.h"     // RequestUploadResult

namespace net {

//发图上传进度（回投 UI，堆对象由 UI 处理后 delete）。
struct ImageSendProgress
{
    std::string m_clientmsgid;  //关联本端"发送中"气泡
    int64_t     m_sent;         //已确认上传字节数
    int64_t     m_total;        //文件总字节数

    ImageSendProgress()
        : m_sent(0)
        , m_total(0)
    {
    }
};

//通用上传 / 下载的结果（帖子群的图片用，2026-10-07，docs/帖子群方案.md 7.2）：投递到调用方指定的窗口与消息号，
//lParam 为本结构的堆对象，由接收窗口处理后 delete（接收窗口必须在 exe 里，不能在插件 DLL 里：两边的堆不同）。
struct PlainTransferResult
{
    uint32_t     m_requestId;   //调用方给的请求号，原样带回
    bool         m_ok;          //是否成功
    int          m_errcode;     //失败错误码（见 enum error_code）
    std::string  m_fileid;      //上传：服务端给的 fileid；下载：请求的 fileid
    std::string  m_md5;         //内容 MD5（小写十六进制）
    int          m_width;       //上传：图片宽（像素）
    int          m_height;      //上传：图片高（像素）
    int64_t      m_size;        //字节数
    std::wstring m_localpath;   //成功时：本地缓存文件完整路径（上传与下载都写进 ChatImage 缓存）

    PlainTransferResult()
        : m_requestId(0)
        , m_ok(false)
        , m_errcode(0)
        , m_width(0)
        , m_height(0)
        , m_size(0)
    {
    }
};

//收图（下载）最终结果（回投 UI，堆对象由 UI 处理后 delete）。
struct ImageDownloadResult
{
    int64_t      m_msgid;     //关联收到的那条图片消息气泡（接收方按 msgid 定位本行）
    bool         m_ok;        //下载并校验是否成功
    int          m_errcode;   //失败错误码（见 enum error_code）
    std::wstring m_localpath; //成功时：原图落地的本地缓存文件完整路径

    ImageDownloadResult()
        : m_msgid(0)
        , m_ok(false)
        , m_errcode(0)
    {
    }
};

//发图最终结果（回投 UI，堆对象由 UI 处理后 delete）。
//成功仅表示"图片消息已发出"，其服务器发送回执仍经 FMG_MSG_V2_SEND_CHATMSG_ACK
//异步回投（按 clientmsgid 回填 msgid/时间），与文本消息一致。
struct ImageSendResult
{
    std::string m_clientmsgid;  //关联本端"发送中"气泡
    int         m_ctype;        //会话类型（便于 UI 路由）
    int64_t     m_cid;          //会话ID（便于 UI 路由）
    bool        m_ok;           //上传并发出是否成功
    int         m_errcode;      //失败错误码（见 enum error_code，传输层错误用 error_code_upload_fail）
    std::string m_errmsg;       //失败描述
    std::string m_fileid;       //成功时：图片文件ID
    std::string m_md5;          //成功时：图片MD5
    int         m_width;        //成功时：图片像素宽
    int         m_height;       //成功时：图片像素高
    int64_t     m_size;         //成功时：图片字节数

    ImageSendResult()
        : m_ctype(0)
        , m_cid(0)
        , m_ok(false)
        , m_errcode(0)
        , m_width(0)
        , m_height(0)
        , m_size(0)
    {
    }
};

//头像图片上传最终结果（回投 UI，堆对象由 UI 处理后 delete）。仅上传到 imgserver
//拿到 fileid，不发任何聊天消息；UI 收到成功结果后再发 cmd_change_avatar 落库。
struct AvatarUploadResult
{
    bool         m_ok;        //上传是否成功
    int          m_errcode;   //失败错误码（见 enum error_code）
    std::string  m_errmsg;    //失败描述
    std::string  m_fileid;    //成功时：头像文件ID（存进 cmd_change_avatar 的 customface）
    std::string  m_md5;       //成功时：头像内容MD5（小写十六进制）
    std::wstring m_localpath; //成功时：已落地的本地缓存文件完整路径（供本端即时渲染）

    AvatarUploadResult()
        : m_ok(false)
        , m_errcode(0)
    {
    }
};

//头像图片下载最终结果（回投 UI，堆对象由 UI 处理后 delete）。按 fileid 把某联系人
//的自定义头像图从 imgserver 下载到本地缓存后回投，UI 据 fileid 找到对应节点刷新头像。
struct AvatarDownloadResult
{
    std::string  m_fileid;    //下载的头像文件ID（UI 据此定位要刷新的节点）
    bool         m_ok;        //下载是否成功
    std::wstring m_localpath; //成功时：已落地的本地缓存文件完整路径

    AvatarDownloadResult()
        : m_ok(false)
    {
    }
};

//聊天图片传输管理器（单例）。
class ImageTransfer
{
public:
    //取全局单例。
    static ImageTransfer& GetInstance();

    /**
     *  取某 md5 图片在本地缓存中的完整路径（仅拼路径，不创建目录、不判断存在）。
     *  缓存按登录账号分隔、按内容 MD5 命名：<主目录>Users\<登录账号>\ChatImage\<md5>，
     *  收发共用。接收侧在触发下载前可据此判断缓存是否已命中，命中则直接渲染、
     *  免重复下载。
     *  @param account 当前登录账号（形如 MY0309 / EMP5321）。Users 下一律按登录账号
     *                 分目录，与配置、聊天记录、头像同一口径（见 net/UserCachePath.h）
     *  @param md5     图片内容MD5（小写十六进制）
     *  @return 缓存文件完整路径；账号为空（未登录）或 md5 为空返回空串
     */
    static std::wstring CacheFilePathForMd5(const std::wstring& account, const std::string& md5);

    //启动工作线程；幂等，重复调用无副作用。在 CIUSocket::Init 中调用。
    void Start();

    //停止工作线程并 join；在 CIUSocket::Uninit 中调用。
    void Stop();

    /**
     *  发起一次发图：把本地图片作为图片消息发到会话 (ctype, cid)。线程安全，
     *  可在 UI 线程调用，立即返回；过程经 FMG_MSG_V2_IMAGE_SEND_PROGRESS /
     *  FMG_MSG_V2_IMAGE_SEND_RESULT 异步回投到 UI。
     *  @param ctype       会话类型（单聊=1、群聊=2，见 enum conversation_type）
     *  @param cid         会话ID（单聊=对端 userid，群聊=群id）
     *  @param clientMsgId 客户端消息ID（UUID，非空），与本端"发送中"气泡对回
     *  @param localPath   本地图片完整路径（宽字符）
     */
    void EnqueueSend(int ctype, int64_t cid, const std::string& clientMsgId,
                     const std::wstring& localPath);

    /**
     *  发起一次收图（下载）：把某条图片消息的原图从 imgserver 下载到本地缓存。
     *  线程安全，可在 UI 线程调用，立即返回；完成 / 失败经 FMG_MSG_V2_IMAGE_DOWNLOAD_RESULT
     *  异步回投到 UI（按 msgid 定位接收方那条图片气泡，成功后用 localpath 渲染）。
     *  @param msgid      该图片消息的服务器 msgid（与气泡对回）
     *  @param fileid     图片文件ID（来自消息 content，传给 cmd_request_download）
     *  @param expectMd5  期望MD5（来自消息 content，下载完成后校验；可空则用应答里的 md5）
     *  @param expectSize  期望字节数（来自消息 content，便于日志；可为 0）
     *  @param selfAccount 当前登录账号，决定落地到哪个账号的缓存目录（Users\<账号>\ChatImage\）
     */
    void EnqueueDownload(int64_t msgid, const std::string& fileid,
                         const std::string& expectMd5, int64_t expectSize,
                         const std::wstring& selfAccount);

    /**
     *  发起一次头像上传：把已处理好的本地头像图片（建议先居中裁方 + 缩放）上传到
     *  imgserver，<b>不发任何聊天消息</b>，成功后把该文件落地到头像缓存
     *  Users\<登录账号>\Avatar\<fileid>，并经 FMG_MSG_V2_AVATAR_UPLOAD_RESULT 异步回投
     *  fileid / md5 / 本地缓存路径。UI 收到成功结果后再发 cmd_change_avatar 落库。
     *  线程安全，可在 UI 线程调用，立即返回。
     *  @param localPath   待上传的本地图片完整路径（宽字符；调用方已做裁剪/缩放/校验）
     *  @param selfAccount 当前登录账号（决定缓存目录；为空视为未登录、任务失败）
     */
    void EnqueueAvatarUpload(const std::wstring& localPath, const std::wstring& selfAccount);

    /**
     *  取某 fileid 头像在本地缓存中的完整路径（仅拼路径，不创建目录、不判存在）。
     *  缓存按登录账号分隔：<主目录>Users\<登录账号>\Avatar\<fileid>。
     *  @param account 当前登录账号（为空返回空串）
     *  @param fileid  头像文件ID（来自上传结果 / 联系人 customface）
     *  @return 缓存文件完整路径；账号为空或 fileid 为空返回空串
     */
    static std::wstring AvatarCacheFilePathForFileId(const std::wstring& account, const std::string& fileid);

    /**
     *  发起一次头像下载：把某 fileid 的头像图从 imgserver 下载到本地头像缓存
     *  Users\<登录账号>\Avatar\<fileid>，成功后经 FMG_MSG_V2_AVATAR_DOWNLOAD_RESULT 异步
     *  回投 fileid + 本地路径（UI 据 fileid 找到对应联系人节点刷新头像）。线程安全。
     *  内部按 fileid 去重（在途不重复发），缓存已存在则直接回投成功、不重复下载。
     *  @param fileid      头像文件ID（来自联系人 customface，facetype==自定义图时）
     *  @param selfAccount 当前登录账号（决定缓存目录；为空则任务失败）
     */
    void EnqueueAvatarDownload(const std::string& fileid, const std::wstring& selfAccount);

    /**
     *  通用上传：把一张本地图片上传到 imgserver（scene=聊天图片），<b>不发任何消息</b>，成功后写进 ChatImage 缓存，
     *  结果以 PlainTransferResult* 投递到 notifyWnd 的 notifyMsg。帖子群发帖、补充用它（docs/帖子群方案.md 7.2）。
     *  线程安全，立即返回。
     *  @param requestId 调用方的请求号，原样带回
     *  @param localPath 本地图片完整路径
     *  @param notifyWnd 接收结果的窗口（必须在 exe 里）
     *  @param notifyMsg 结果消息号
     */
    void EnqueuePlainUpload(uint32_t requestId, const std::wstring& localPath, HWND notifyWnd, UINT notifyMsg);

    /**
     *  通用下载：按 fileid 把图片下载到 ChatImage 缓存，结果以 PlainTransferResult* 投递到 notifyWnd 的 notifyMsg。
     *  md5 已知且缓存里已有时不下载，直接投递成功。线程安全，立即返回。
     *  @param requestId 调用方的请求号，原样带回
     *  @param fileid    图片文件 ID
     *  @param md5       期望 MD5（可空，空时不能命中缓存、下载后也不校验）
     *  @param notifyWnd 接收结果的窗口（必须在 exe 里）
     *  @param notifyMsg 结果消息号
     */
    void EnqueuePlainDownload(uint32_t requestId, const std::string& fileid, const std::string& md5, HWND notifyWnd,
                              UINT notifyMsg);

    /**
     *  由 CIUSocket::HandleV2Frame 在收到 cmd_request_upload(1801) 应答时调用，
     *  把信令应答交给正在等待该 seq 的工作线程。可能在网络接收线程上调用。
     *  @param seq 申请上传请求所用的帧 seq（RequestUpload 返回值）
     *  @param r   解析出的上传申请应答
     */
    void OnRequestUploadResponse(uint32_t seq, const RequestUploadResult& r);

    /**
     *  由 CIUSocket::HandleV2Frame 在收到 cmd_request_download(1802) 应答时调用，
     *  把下载信令应答交给正在等待该 seq 的工作线程。可能在网络接收线程上调用。
     *  @param seq 申请下载请求所用的帧 seq（RequestDownload 返回值）
     *  @param r   解析出的下载申请应答
     */
    void OnRequestDownloadResponse(uint32_t seq, const RequestDownloadResult& r);

    /**
     *  阻塞收取 imgserver 连接上的一个完整 v2 帧，回传其包体（明文）。供上传
     *  分片传输实现取分片回执用。仅在工作线程上调用。
     *  @param body      [出参] 帧包体（上传 ack / 下载分片描述符等 JSON 文本）
     *  @param timeoutSec 收取超时时间，单位秒
     *  @return 成功返回 true；收取失败 / 帧非法返回 false
     */
    bool RecvOneImgFrame(std::string& body, int timeoutSec);

private:
    ImageTransfer();
    ~ImageTransfer();
    ImageTransfer(const ImageTransfer&) = delete;
    ImageTransfer& operator=(const ImageTransfer&) = delete;

    //任务类型：发图（上传）或收图（下载）。
    enum TaskKind
    {
        task_send         = 0,   //发图：读本地图片→申请上传→传字节→发图片消息
        task_download     = 1,   //收图：申请下载→拉字节→校验→落地缓存
        task_avatar_upload = 2,  //传头像：读本地图片→申请上传(scene=avatar)→传字节→落地缓存(不发消息)
        task_avatar_download = 3, //下头像：按 fileid 申请下载→拉字节→落地 Avatar 缓存→回投 fileid
        task_plain_upload = 4,   //通用上传：读本地图片→申请上传→传字节→落地 ChatImage 缓存（不发消息）→投递给指定窗口
        task_plain_download = 5, //通用下载：按 fileid 申请下载→拉字节→落地 ChatImage 缓存→投递给指定窗口
    };

    //一项 worker 队列任务。按 m_kind 区分发图 / 收图，对应字段才有意义。
    struct Task
    {
        int          m_kind;         //任务类型，取值见 TaskKind

        //==== 发图（task_send）字段 ====
        int          m_ctype;        //会话类型
        int64_t      m_cid;          //会话ID
        std::string  m_clientmsgid;  //客户端消息ID
        std::wstring m_localpath;    //本地图片路径

        //==== 收图（task_download）字段 ====
        int64_t      m_msgid;        //图片消息的服务器 msgid（与气泡对回）
        std::string  m_fileid;       //图片文件ID
        std::string  m_expectMd5;    //期望MD5（校验用，可空）
        int64_t      m_expectSize;   //期望字节数（日志用，可为 0）
        std::wstring m_selfAccount;  //当前登录账号（决定落地到 Users\<账号>\ 下哪个缓存目录）

        //==== 通用上传 / 下载（task_plain_*）字段 ====
        uint32_t     m_requestId;    //调用方的请求号
        HWND         m_notifyWnd;    //接收结果的窗口
        UINT         m_notifyMsg;    //结果消息号

        Task()
            : m_kind(task_send)
            , m_ctype(0)
            , m_cid(0)
            , m_msgid(0)
            , m_expectSize(0)
            , m_requestId(0)
            , m_notifyWnd(NULL)
            , m_notifyMsg(0)
        {
        }
    };

    //上传信令应答等待槽：worker 提交申请后阻塞在此，接收线程填充并唤醒。
    struct UploadWaiter
    {
        std::mutex              m_mtx;        //保护 m_responded / m_result
        std::condition_variable m_cv;         //应答到达时唤醒 worker
        bool                    m_responded;  //是否已收到应答
        RequestUploadResult     m_result;     //收到的应答

        UploadWaiter()
            : m_responded(false)
        {
        }
    };

    //下载信令应答等待槽（结构同 UploadWaiter，仅应答类型不同）。
    struct DownloadWaiter
    {
        std::mutex              m_mtx;
        std::condition_variable m_cv;
        bool                    m_responded;
        RequestDownloadResult   m_result;

        DownloadWaiter()
            : m_responded(false)
        {
        }
    };

    //工作线程主循环：从队列取任务、按类型逐个处理。
    void WorkerProc();

    //处理一项发图任务（读文件→申请上传→传字节→发消息），全程串行。
    void ProcessSend(const Task& task);

    //处理一项收图任务（申请下载→拉字节→校验→落地缓存→回投本地路径），全程串行。
    void ProcessDownload(const Task& task);

    //处理一项头像上传任务（读图→申请上传 scene=avatar→传字节→落地缓存→回投 fileid），全程串行。
    void ProcessAvatarUpload(const Task& task);

    //处理一项头像下载任务（按 fileid 申请下载→拉字节→落地 Avatar 缓存→回投 fileid），全程串行。
    void ProcessAvatarDownload(const Task& task);

    //处理一项通用上传任务（读图→申请上传→传字节→落地缓存→投递结果），全程串行。
    void ProcessPlainUpload(const Task& task);

    //处理一项通用下载任务（申请下载→拉字节→校验→落地缓存→投递结果），全程串行。
    void ProcessPlainDownload(const Task& task);

    //把通用任务的结果投递给调用方指定的窗口；窗口已不存在时丢弃。
    static void PostPlainResult(const Task& task, PlainTransferResult* result);

    //向 UI 回投发图最终结果（成功/失败统一走此）。
    void PostSendResult(const Task& task, bool ok, int errcode, const std::string& errmsg,
                        const std::string& fileid, const std::string& md5,
                        int width, int height, int64_t size);

    //向 UI 回投收图最终结果（成功/失败统一走此）。
    void PostDownloadResult(int64_t msgid, bool ok, int errcode, const std::wstring& localPath);

    //向 UI 回投头像上传最终结果（成功/失败统一走此）。
    void PostAvatarUploadResult(bool ok, int errcode, const std::string& errmsg,
                                const std::string& fileid, const std::string& md5,
                                const std::wstring& localPath);

    //向 UI 回投头像下载最终结果（成功/失败统一走此）。
    void PostAvatarDownloadResult(const std::string& fileid, bool ok, const std::wstring& localPath);

    //提交申请后阻塞等待 seq 对应的上传申请应答；超时返回 false。
    bool WaitUploadSignal(uint32_t seq, RequestUploadResult& out, int timeoutSec);

    //提交申请后阻塞等待 seq 对应的下载申请应答；超时返回 false。
    bool WaitDownloadSignal(uint32_t seq, RequestDownloadResult& out, int timeoutSec);

    std::unique_ptr<std::thread> m_worker;     //工作线程
    std::mutex                   m_queueMtx;    //保护任务队列
    std::condition_variable      m_queueCv;     //队列非空 / 停止信号
    std::deque<Task>             m_queue;       //待处理任务（发图 / 收图）
    std::set<std::string>        m_inflightAvatarDownloads;  //在途头像下载的 fileid 集合（受 m_queueMtx 保护），按 fileid 去重
    std::set<int64_t>            m_inflightDownloads;  //在途下载的 msgid 集合（受 m_queueMtx 保护）：
                                                       //同一条图被 live 追加 + 历史渲染各触发一次下载时去重
    bool                         m_stop;        //停止标志
    bool                         m_started;     //是否已启动（Start 幂等用）

    //上传信令应答关联（seq → 等待槽 / 早到应答暂存，防接收线程抢在注册前到达）。
    std::mutex                            m_waiterMtx;  //保护下面四个映射
    std::map<uint32_t, UploadWaiter*>     m_waiters;        //seq → 等待中的上传 worker 槽
    std::map<uint32_t, RequestUploadResult> m_earlyResponses;  //上传应答早到暂存
    //下载信令应答关联（同上，下载侧）。
    std::map<uint32_t, DownloadWaiter*>     m_dlWaiters;       //seq → 等待中的下载 worker 槽
    std::map<uint32_t, RequestDownloadResult> m_dlEarlyResponses;  //下载应答早到暂存
};

} // namespace net
