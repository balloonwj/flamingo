/**
 *  聊天文件传输管理器（客户端侧，收发双向）。
 *
 *  与图片传输管理器 ImageTransfer 镜像，区别在于：文件走 fileserver（图片走
 *  imgserver）、上传场景为 file_scene_chat_file 并携带原始文件名、发出的是文件
 *  消息（cmd_send_message，msgtype=3）、下载按原始文件名落地（图片按 md5 命名）。
 *
 *  把一次「发文件」/「收文件」拆成的多步异步流程收口到一个串行工作线程里，按
 *  clientmsgid / msgid / 帧 seq 关联，UI 只管发起与显示进度：
 *    发送：① 流式扫一遍本地文件算 MD5；② 在 chatserver 上 cmd_request_upload(1801)
 *    申请上传（异步，应答经 CIUSocket::HandleV2Frame 按 seq 转交本类等待槽）；③ 命中
 *    秒传则跳过字节流，否则连 fileserver、用 FileStreamTransfer::uploadFile 跑
 *    cmd_file_upload(1851) 流式分片上传并回投进度；④ 上传完成后 cmd_send_message
 *    (1503, msgtype=3) 发出文件消息，回执沿用既有 FMG_MSG_V2_SEND_CHATMSG_ACK 路径。
 *    接收：① cmd_request_download(1802) 申请下载；② 连 fileserver、用
 *    FileStreamTransfer::downloadFile 跑 cmd_file_download(1852) 流式分片下载，收一片
 *    写一片盘并增量算 MD5、回投进度；③ 校验 MD5；④ 原子 rename 落地到下载缓存目录；
 *    ⑤ 回投本地路径供 UI 渲染「打开 / 另存为」。
 *
 *  收发全程流式（只占一个分片的内存）：聊天文件与群文件都已取消大小上限，任何把整
 *  文件读进内存的做法都会在 32 位客户端进程 bad_alloc。图片仍走 ImageTransfer 的内存
 *  版驱动（图片有 20MiB 上限，无需流式）。
 *
 *  传输可取消：每笔任务入队时经 ChatTransferCancelRegistry 登记一个取消标志，UI 线程
 *  调 CancelSend / CancelDownload 置位，worker 在分片边界读到即中止并回投
 *  m_cancelled=true 的结果，由聊天窗把气泡置「已取消」态。
 *
 *  信令走 chatserver 的异步帧管道，字节流走 fileserver 的阻塞 socket（本线程独占、
 *  不占主连接、不卡 UI）。本类是单例，worker 线程在 CIUSocket::Init / Uninit 处启停。
 *
 *  注：本类的信令等待槽 / 早到应答暂存机制与 ImageTransfer 同构（有意复制而非抽取
 *  公共基类，以免改动已验证的图片传输内部）；两者共用 net/FileProtocolClient，
 *  文件的字节流收发则收敛在 net/FileStreamTransfer（可单测的纯逻辑）。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <deque>
#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <atomic>
#include "FileProtocolClient.h"     // RequestUploadResult / RequestDownloadResult
#include "GroupTransferQueue.h"     // GroupTransferQueue / GroupTransferItem（群文件并发池）
#include "ChatTransferCancelRegistry.h"  // ChatTransferCancelRegistry（单聊收发的取消簿记）
#include "FileStreamTransfer.h"     // IFileFrameChannel / FileStreamTransfer（流式收发实现）
#include "UploadInflightGate.h"     // UploadInflightGate（同一文件只上传一份）

namespace net {

//上传 / 下载分片进度 sink 接口（分别定义在 ImageUploadDriver.h / ImageDownloadDriver.h）。
//流式收发用它逐片回报进度——单聊文件传更新气泡进度条的 sink，群文件传回投群文件进度
//消息的 sink；不需进度时传 nullptr。
class IUploadProgressSink;
class IDownloadProgressSink;

//群文件并发池里的一条独立 fileserver 连接（自带 socket，实现 IFileFrameChannel）。
//前置声明用于 m_activeConns 集合；完整定义在 FileTransfer.cpp。
class FileServerConn;

//发文件上传进度（回投 UI，堆对象由 UI 处理后 delete）。
struct FileSendProgress
{
    std::string m_clientmsgid;  //关联本端"发送中"气泡
    int64_t     m_sent;         //已确认上传字节数
    int64_t     m_total;        //文件总字节数

    FileSendProgress()
        : m_sent(0)
        , m_total(0)
    {
    }
};

//收文件下载进度（回投 UI，堆对象由 UI 处理后 delete）。
struct FileDownloadProgress
{
    int64_t m_msgid;     //关联接收方那条文件消息气泡（按 msgid 定位本行）
    int64_t m_received;  //已接收字节数
    int64_t m_total;     //文件总字节数

    FileDownloadProgress()
        : m_msgid(0)
        , m_received(0)
        , m_total(0)
    {
    }
};

//发文件最终结果（回投 UI，堆对象由 UI 处理后 delete）。
//成功仅表示"文件消息已发出"，其服务器发送回执仍经 FMG_MSG_V2_SEND_CHATMSG_ACK
//异步回投（按 clientmsgid 回填 msgid / 时间），与文本 / 图片消息一致。
struct FileSendResult
{
    std::string m_clientmsgid;  //关联本端"发送中"气泡
    int         m_ctype;        //会话类型（便于 UI 路由）
    int64_t     m_cid;          //会话ID（便于 UI 路由）
    bool        m_ok;           //上传并发出是否成功
    int         m_errcode;      //失败错误码（见 enum error_code，传输层错误用 error_code_upload_fail）
    std::string m_errmsg;       //失败描述
    std::string m_fileid;       //成功时：文件ID
    std::string m_md5;          //成功时：文件MD5
    std::string m_filename;     //成功时：原始文件名（UTF-8，供气泡显示）
    int64_t     m_size;         //成功时：文件字节数
    bool        m_cancelled;    //是否因用户点「取消」而中止（true 时 m_ok 必为 false）：
                                //UI 据此把气泡置「已取消」态而非弹失败提示
    std::wstring m_localpath;   //本端选中的源文件完整路径：成功后登记进文件本地落点库，
                                //使日后翻历史时直接打开这份原文件，不必重新下载

    FileSendResult()
        : m_ctype(0)
        , m_cid(0)
        , m_ok(false)
        , m_errcode(0)
        , m_size(0)
        , m_cancelled(false)
    {
    }
};

//收文件下载最终结果（回投 UI，堆对象由 UI 处理后 delete）。
struct FileDownloadResult
{
    int64_t      m_msgid;     //关联收到的那条文件消息气泡（接收方按 msgid 定位本行）
    bool         m_ok;        //下载并校验是否成功
    int          m_errcode;   //失败错误码（见 enum error_code）
    std::wstring m_localpath; //成功时：文件按原始名落地的本地完整路径
    bool         m_cancelled; //是否因用户点「取消」而中止（true 时 m_ok 必为 false）：
                              //UI 据此把气泡置「已取消」态而非按失败处理
    std::string  m_md5;       //文件内容MD5（小写十六进制）：与 m_filename 一道作为
                              //文件本地落点库的键，成功后据此登记 m_localpath
    std::string  m_filename;  //原始文件名（UTF-8），同上作登记键的一半

    FileDownloadResult()
        : m_msgid(0)
        , m_ok(false)
        , m_errcode(0)
        , m_cancelled(false)
    {
    }
};

//群文件上传最终结果（回投 UI，堆对象由 UI 处理后 delete）。群文件上传与聊天发文件
//的区别：上传完成后不发聊天消息，而是回投本结果，由群聊窗据 m_fileid 发
//cmd_add_group_file 登记进群文件库（服务端再推 1806 变更通知刷新各端）。
struct GroupFileUploadResult
{
    int64_t     m_groupid;   //目标群ID（回投后据此路由到对应群聊窗）
    int         m_uploadid;  //上传ID（客户端分配，与该笔上传的"上传中"行/取消对应）
    bool        m_ok;        //上传是否成功
    int         m_errcode;   //失败错误码（见 enum error_code）
    std::string m_errmsg;    //失败描述
    std::string m_fileid;    //成功时：文件ID（群聊窗据此发 cmd_add_group_file 登记）
    std::string m_filename;  //原始文件名（UTF-8，日志 / 提示用）
    int64_t     m_size;      //文件字节数
    std::string m_md5;       //成功时：文件内容的 MD5（2026-10-05 起带上：发送录像时还要用它在群里发文件消息）

    GroupFileUploadResult()
        : m_groupid(0)
        , m_uploadid(0)
        , m_ok(false)
        , m_errcode(0)
        , m_size(0)
    {
    }
};

//群文件上传进度（回投 UI，堆对象由 UI 处理后 delete）。群文件上传现为多笔并发，每笔
//在面板顶部各占一行「上传中」，故除 groupid 路由到面板外，还需 m_uploadid 定位是哪一行。
struct GroupFileUploadProgress
{
    int64_t m_groupid;   //目标群ID（路由到对应群聊窗的群文件面板）
    int     m_uploadid;  //上传ID（定位面板顶部那一行「上传中」）
    int64_t m_sent;      //已确认上传字节数
    int64_t m_total;     //文件总字节数

    GroupFileUploadProgress()
        : m_groupid(0)
        , m_uploadid(0)
        , m_sent(0)
        , m_total(0)
    {
    }
};

//群文件下载进度（回投 UI，堆对象由 UI 处理后 delete）。按 entryid 定位面板里那一行，
//在该行上就地显示进度。
struct GroupFileDownloadProgress
{
    int64_t     m_groupid;   //目标群ID（路由到对应群聊窗）
    std::string m_entryid;   //群文件条目ID（定位面板里那一行）
    int64_t     m_received;  //已接收字节数
    int64_t     m_total;     //文件总字节数

    GroupFileDownloadProgress()
        : m_groupid(0)
        , m_received(0)
        , m_total(0)
    {
    }
};

//群文件下载最终结果（回投 UI，堆对象由 UI 处理后 delete）。群文件下载与聊天收文件
//的区别：无气泡按 msgid 关联，改按 m_groupid 路由回群聊窗、按 m_entryid 关联面板行，
//成功后群聊窗据 m_localpath 直接打开文件。
struct GroupFileDownloadResult
{
    int64_t      m_groupid;   //目标群ID（回投后据此路由到对应群聊窗）
    std::string  m_entryid;   //群文件条目ID（关联面板里那一行；日志 / 未来行状态用）
    bool         m_ok;        //下载并校验是否成功
    int          m_errcode;   //失败错误码（见 enum error_code）
    std::wstring m_localpath; //成功时：文件按原始名落地的本地完整路径（群聊窗据此打开）

    GroupFileDownloadResult()
        : m_groupid(0)
        , m_ok(false)
        , m_errcode(0)
    {
    }
};

//语音消息上传最终结果（回投 UI，堆对象由 UI 处理后 delete；语音消息第 3c 步）。
//成功只表示语音文件已上传、语音消息（cmd_send_message，msgtype=4）已发出；服务器的发送回执仍经
//FMG_MSG_V2_SEND_CHATMSG_ACK 按 clientmsgid 回投，与文本 / 图片 / 文件消息一致。
struct VoiceSendResult
{
    std::string m_clientmsgid;  //关联本端「发送中」的语音气泡
    int         m_ctype;        //会话类型（主面板据此路由到单聊窗或群聊窗）
    int64_t     m_cid;          //会话ID（单聊=对端 userid，群聊=群id）
    bool        m_ok;           //上传并发出是否成功
    int         m_errcode;      //失败错误码（见 enum error_code，传输层错误用 error_code_upload_fail）
    std::string m_errmsg;       //失败描述（日志用）
    std::string m_fileid;       //成功时：语音文件ID
    std::string m_md5;          //语音文件内容MD5（小写十六进制）

    VoiceSendResult()
        : m_ctype(0)
        , m_cid(0)
        , m_ok(false)
        , m_errcode(0)
    {
    }
};

//语音文件下载最终结果（回投 UI，堆对象由 UI 处理后 delete；语音消息第 3d-3 步）。
//按 md5 而不按 msgid 关联：同一段语音被转发多次时有好几条消息共用一个文件，下载一次即可全部就绪。
struct VoiceDownloadResult
{
    std::string  m_md5;        //语音文件内容 MD5（小写十六进制），聊天窗据此找到所有该段语音的气泡
    bool         m_ok;         //下载并校验是否成功
    int          m_errcode;    //失败错误码（见 enum error_code）
    std::wstring m_localpath;  //成功时：文件落地的本地完整路径（Users\<账号>\ChatVoice\<md5>.ogg）

    VoiceDownloadResult()
        : m_ok(false)
        , m_errcode(0)
    {
    }
};

//聊天文件传输管理器（单例）。
class FileTransfer
{
public:
    //取全局单例。
    static FileTransfer& GetInstance();

    /**
     *  取某文件在本地下载缓存中的完整路径（仅拼路径，不创建目录、不判断存在）。
     *  缓存按登录用户分隔、按内容 MD5 分目录、按原始文件名命名：
     *  <主目录>Users\<登录账号>\ChatFile\<md5>\<filename>。按 md5 分目录天然去重并
     *  支持"是否已下载"判定，文件保留原始名与扩展名以便直接打开。
     *  @param account  当前登录账号（形如 MY0309 / EMP5321）。Users 下一律按登录账号
     *                  分目录，与配置、聊天记录、头像同一口径（见 net/UserCachePath.h）；
     *                  为空串（未登录）时返回空串
     *  @param md5      文件内容MD5（小写十六进制）
     *  @param filename 原始文件名（UTF-8，含扩展名）
     *  @return 缓存文件完整路径；参数非法返回空串
     */
    static std::wstring CacheFilePathFor(const std::wstring& account, const std::string& md5,
                                         const std::string& filename);

    //启动工作线程；幂等，重复调用无副作用。在 CIUSocket::Init 中调用。
    void Start();

    //停止工作线程并 join；在 CIUSocket::Uninit 中调用。
    void Stop();

    /**
     *  发起一次发文件：把本地文件作为文件消息发到会话 (ctype, cid)。线程安全，可在
     *  UI 线程调用，立即返回；过程经 FMG_MSG_V2_FILE_SEND_PROGRESS /
     *  FMG_MSG_V2_FILE_SEND_RESULT 异步回投到 UI。
     *  @param ctype       会话类型（单聊=1、群聊=2，见 enum conversation_type）
     *  @param cid         会话ID（单聊=对端 userid，群聊=群id）
     *  @param clientMsgId 客户端消息ID（UUID，非空），与本端"发送中"气泡对回
     *  @param localPath   本地文件完整路径（宽字符）；其文件名部分即随消息发出的原始名
     */
    void EnqueueSend(int ctype, int64_t cid, const std::string& clientMsgId,
                     const std::wstring& localPath);

    /**
     *  发起一次收文件（下载）：把某条文件消息的原文件从 fileserver 下载并按原始名
     *  落地。线程安全，可在 UI 线程调用，立即返回；进度经 FMG_MSG_V2_FILE_DOWNLOAD_PROGRESS、
     *  完成 / 失败经 FMG_MSG_V2_FILE_DOWNLOAD_RESULT 异步回投到 UI（按 msgid 定位气泡）。
     *  @param msgid      该文件消息的服务器 msgid（与气泡对回）
     *  @param fileid     文件ID（来自消息 content，传给 cmd_request_download）
     *  @param filename   原始文件名（UTF-8，来自消息 content，决定落地文件名）
     *  @param expectMd5  期望MD5（来自消息 content，下载完成后校验；可空则用应答里的 md5）
     *  @param expectSize  期望字节数（来自消息 content，便于日志；可为 0）
     *  @param selfAccount 当前登录账号，决定落地到哪个账号的下载目录（Users\<账号>\ChatFile\）
     */
    void EnqueueDownload(int64_t msgid, const std::string& fileid,
                         const std::string& filename, const std::string& expectMd5,
                         int64_t expectSize, const std::wstring& selfAccount);

    /**
     *  取消一笔单聊发文件（用户点「发送中」气泡的「取消」）。线程安全，可在 UI 线程
     *  调用：置位该笔的取消标志——若它还在队列里没轮到，worker 取出时即跳过；若正在
     *  传字节，worker 在下一个分片边界中止。两种情形都会经 FMG_MSG_V2_FILE_SEND_RESULT
     *  回投一个 m_cancelled=true 的结果，UI 据此把气泡置「已取消」态。
     *  未知ID（已完成 / 从未发起）无操作。
     *  @param clientMsgId 发起发送时用的客户端消息ID（与气泡对应）
     */
    void CancelSend(const std::string& clientMsgId);

    /**
     *  取消一笔单聊收文件（用户点「下载中」气泡的「取消」）。语义与 CancelSend 对称：
     *  中止后删掉半成品临时文件、经 FMG_MSG_V2_FILE_DOWNLOAD_RESULT 回投
     *  m_cancelled=true 的结果。未知ID无操作。
     *  @param msgid 该文件消息的服务器 msgid（与气泡对应）
     */
    void CancelDownload(int64_t msgid);

    /**
     *  发起一次群文件上传：把本地文件上传到 fileserver（scene=file_scene_group_file），
     *  上传完成后<不>发聊天消息，而是经 FMG_MSG_V2_GROUP_FILE_UPLOAD_RESULT 回投结果
     *  （含 fileid + uploadId），由群聊窗据此发 cmd_add_group_file 登记进群文件库。
     *  进群文件并发池：最多 kMaxConcurrentGroupTransfers 笔同时传，超出的排队；每笔独占
     *  一条新建的 fileserver 连接，传完即断。线程安全，立即返回。
     *  @param groupId    目标群ID（>0，随结果回投以路由群聊窗）
     *  @param localPath  本地文件完整路径（宽字符）；其文件名部分即登记进库的原始名
     *  @param selfUserId 当前登录用户 userid（日志 / 一致性用）
     *  @return 本笔上传的客户端上传ID（>0）；UI 据它显示 / 更新 / 取消那一行「上传中」。
     */
    int EnqueueGroupFileUpload(int64_t groupId, const std::wstring& localPath,
                               int64_t selfUserId);

    /**
     *  取消一笔群文件上传（用户点「上传中」行的 ✕）。线程安全，可在 UI 线程调用：
     *  若该笔还在队列里没轮到就直接移除（永不开始）；若已在传则置位其取消标志，传输
     *  worker 在下一个分片边界中止并回投失败结果（UI 侧已移除该行，故据上传ID查不到
     *  会静默忽略该失败、不弹框）。未知ID（已完成 / 不存在）无操作。
     *  @param uploadId EnqueueGroupFileUpload 返回的上传ID
     */
    void CancelGroupFileUpload(int uploadId);

    /**
     *  取消一笔群文件下载（用户点「下载中」行的 ✕）。线程安全，可在 UI 线程调用：
     *  若该笔还在队列里没轮到就直接移除（永不开始）；若已在传则置位其取消标志，传输
     *  worker 在下一个分片边界中止并删掉半成品临时文件、回投失败结果（下载失败本就静默，
     *  UI 侧已把该行恢复成下载箭头）。未知 entryid（已完成 / 不存在）无操作。
     *  @param entryid 群文件条目ID（与发起下载时一致）
     */
    void CancelGroupFileDownload(const std::string& entryid);

    /**
     *  发起一次群文件下载：把某群文件从 fileserver 下载并按原始名落地，完成 / 失败经
     *  FMG_MSG_V2_GROUP_FILE_DOWNLOAD_RESULT 回投（按 groupid 路由群聊窗、按 entryid
     *  关联面板行），成功后群聊窗据落地路径打开文件。线程安全，立即返回。
     *  @param groupId    目标群ID（>0，随结果回投以路由群聊窗）
     *  @param entryid    群文件条目ID（随结果回投以关联面板行）
     *  @param fileid     文件ID（传给 cmd_request_download）
     *  @param filename   原始文件名（UTF-8，决定落地命名）
     *  @param expectMd5  期望MD5（下载完成后校验；可空则用应答里的 md5）
     *  @param expectSize 期望字节数（日志用，可为 0）
     *  @param selfUserId 当前登录用户 userid（日志 / 一致性用）
     *  @param destPath   下载落地的完整路径（用户在「另存为」对话框选定，含文件名）
     */
    void EnqueueGroupFileDownload(int64_t groupId, const std::string& entryid,
                                  const std::string& fileid, const std::string& filename,
                                  const std::string& expectMd5, int64_t expectSize,
                                  int64_t selfUserId, const std::wstring& destPath);

    /**
     *  发起一次语音文件下载（语音消息第 3d-3 步，docs/语音消息方案.md V5：收到语音即在后台下载到本机缓存）。
     *  与单聊收文件走同一条串行工作线程与共享 file 连接；不报进度、不可取消（语音文件最大约 1 MiB）。
     *  同一 md5 已在队列里或正在下载时直接忽略，在途那一笔完成后按 md5 回投，所有该段语音的气泡一并更新。
     *  完成 / 失败经 FMG_MSG_V2_VOICE_DOWNLOAD_RESULT 回投到 UI。线程安全，立即返回。
     *  @param fileid     语音文件ID（来自消息 content，传给 cmd_request_download）
     *  @param md5        文件内容MD5（小写十六进制，来自消息 content，下载后校验，也是回投时的关联键）
     *  @param expectSize 期望字节数（来自消息 content，日志用，可为 0）
     *  @param destPath   落地完整路径（ChatVoice\<md5>.ogg）；所在目录不存在时由工作线程逐级创建
     */
    void EnqueueVoiceDownload(const std::string& fileid, const std::string& md5, int64_t expectSize,
                              const std::wstring& destPath);

    /**
     *  发起一次发语音（语音消息第 3c 步，docs/语音消息方案.md 第 7.3 节）：把录好的语音文件按聊天文件场景
     *  （file_scene_chat_file，方案 V3）上传，再发语音消息（cmd_send_message，msgtype=4）。与单聊收发文件走同一条
     *  串行工作线程；不报进度、不可取消（语音文件最大约 360 KB）。服务端已有同 md5 文件时跳过传字节。
     *  完成 / 失败经 FMG_MSG_V2_VOICE_SEND_RESULT 回投；发送回执另经 FMG_MSG_V2_SEND_CHATMSG_ACK 回投。
     *  线程安全，可在 UI 线程调用，立即返回。重发时沿用原 clientMsgId 再调一次即可。
     *  @param ctype           会话类型（单聊=1、群聊=2）
     *  @param cid             会话ID（单聊=对端 userid，群聊=群id）
     *  @param clientMsgId     客户端消息ID（开始录音时生成，非空），与本端「发送中」的语音气泡对回
     *  @param localPath       录音文件完整路径（ChatVoice\<md5>.ogg）
     *  @param durationSeconds 时长（整秒，1~120），写进消息 content.duration
     *  @param waveBase64      40 个波形值的 Base64，写进 content.wave；空串表示不带波形
     */
    void EnqueueSendVoice(int ctype, int64_t cid, const std::string& clientMsgId, const std::wstring& localPath,
                          int durationSeconds, const std::string& waveBase64);

    /**
     *  由 CIUSocket::HandleV2Frame 在收到 cmd_request_upload(1801) 应答、且经
     *  TransferOwnerRegistry 判定归属为文件传输时调用，把信令应答按 seq 交给等待槽。
     *  可能在网络接收线程上调用。
     *  @param seq 申请上传请求所用的帧 seq（RequestUpload 返回值）
     *  @param r   解析出的上传申请应答
     */
    void OnRequestUploadResponse(uint32_t seq, const RequestUploadResult& r);

    /**
     *  由 CIUSocket::HandleV2Frame 在收到 cmd_request_download(1802) 应答、且归属为
     *  文件传输时调用，把下载信令应答按 seq 交给等待槽。可能在网络接收线程上调用。
     *  @param seq 申请下载请求所用的帧 seq（RequestDownload 返回值）
     *  @param r   解析出的下载申请应答
     */
    void OnRequestDownloadResponse(uint32_t seq, const RequestDownloadResult& r);

    /**
     *  阻塞收取 fileserver 连接上的一个完整 v2 帧，回传其包体（明文）。供上传 /
     *  下载分片传输实现取分片回执 / 分片数据用。仅在工作线程上调用。
     *  @param body       [出参] 帧包体（上传 ack / 下载分片描述符 + 二进制等）
     *  @param timeoutSec 收取超时时间，单位秒
     *  @return 成功返回 true；收取失败 / 帧非法返回 false
     */
    bool RecvOneFileFrame(std::string& body, int timeoutSec);

private:
    FileTransfer();
    ~FileTransfer();
    FileTransfer(const FileTransfer&) = delete;
    FileTransfer& operator=(const FileTransfer&) = delete;

    //任务类型：发文件（上传）/ 收文件（下载）。群文件上传 / 下载不再走本串行队列，
    //改走下方的并发传输池（m_poolQueue + m_poolWorkers），见 GroupTransferQueue。
    enum TaskKind
    {
        task_send     = 0,   //发文件：读本地文件→申请上传→传字节→发文件消息
        task_download = 1,   //收文件：申请下载→拉字节→校验→按原名落地
        task_voice_download = 2,   //收语音：申请下载→拉字节→校验→落地到 m_destPath（ChatVoice\<md5>.ogg），按 md5 回投
        task_send_voice = 3,       //发语音：读录音文件→申请上传→传字节→发语音消息（msgtype=4）
    };

    //一项串行 worker 队列任务（仅单聊收发文件用）。按 m_kind 区分发送 / 下载。
    struct Task
    {
        int          m_kind;         //任务类型，取值见 TaskKind

        //==== 发送（task_send）字段 ====
        int          m_ctype;        //会话类型
        int64_t      m_cid;          //会话ID
        std::string  m_clientmsgid;  //客户端消息ID
        std::wstring m_localpath;    //本地文件路径（其文件名部分即随消息发出的原始名）

        //==== 下载（task_download）字段 ====
        int64_t      m_msgid;        //文件消息的服务器 msgid（与气泡对回）
        std::string  m_fileid;       //文件ID
        std::string  m_filename;     //原始文件名（UTF-8，决定落地命名）
        std::string  m_expectMd5;    //期望MD5（校验用，可空）
        int64_t      m_expectSize;   //期望字节数（日志用，可为 0）
        std::wstring m_selfAccount;  //当前登录账号（决定落地到 Users\<账号>\ChatFile\ 哪个目录）

        //==== 语音下载（task_voice_download）字段：另用上面的 m_fileid、m_expectMd5、m_expectSize ====
        std::wstring m_destPath;     //落地完整路径（ChatVoice\<md5>.ogg）

        //==== 发语音（task_send_voice）字段：另用上面的 m_ctype、m_cid、m_clientmsgid、m_localpath ====
        int          m_voiceDuration;  //时长（整秒），写进消息 content.duration
        std::string  m_voiceWave;      //波形值的 Base64，写进消息 content.wave；空串表示不带

        //取消标志（入队时由 m_cancelReg 登记并装入）：worker 不持锁地 load 它，UI 线程
        //经 CancelSend / CancelDownload 持锁 store。worker 持有这份 shared_ptr，故簿记表
        //即便已 CompleteXxx 清理，这里读取依然安全。
        std::shared_ptr<std::atomic<bool> > m_cancelFlag;

        Task()
            : m_kind(task_send)
            , m_ctype(0)
            , m_cid(0)
            , m_msgid(0)
            , m_expectSize(0)
            , m_voiceDuration(0)
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

    //串行工作线程主循环（仅单聊收发文件）：从 m_queue 取任务、逐个处理。
    void WorkerProc();

    //处理一项发文件任务（读文件→申请上传→传字节→发消息），全程串行。
    void ProcessSend(const Task& task);

    //处理一项收文件任务（申请下载→拉字节→校验→按原名落地→回投本地路径），全程串行。
    void ProcessDownload(const Task& task);

    //处理一项语音下载任务（申请下载→拉字节→校验→落地到 task.m_destPath→按 md5 回投），全程串行。
    void ProcessVoiceDownload(const Task& task);

    //处理一项发语音任务（读文件算 md5→申请上传→传字节→发语音消息→回投结果），全程串行。
    void ProcessSendVoice(const Task& task);

    //========== 群文件并发传输池（最多 kMaxConcurrentGroupTransfers 笔并发） ==========

    //并发池工作线程主循环：从 m_poolQueue 取一笔（上传/下载）、用一条独立连接处理完
    //再取下一笔。共 kMaxConcurrentGroupTransfers 个本函数实例并发跑。
    void PoolWorkerProc();

    //处理一笔群文件上传（申请上传→建独立连接→流式传字节→回投 fileid+uploadId）。
    //item.m_cancelFlag 在传输途中被置位则中止并回投失败。
    void ProcessPooledUpload(const GroupTransferItem& item);

    //处理一笔群文件下载（申请下载→建独立连接→流式拉字节边写盘→校验→回投本地路径）。
    void ProcessPooledDownload(const GroupTransferItem& item);

    //登记 / 注销一条在传的独立连接（受 m_poolMtx 保护）。Stop 时据此把仍在阻塞收发的
    //连接全部关掉，让池线程尽快从 socket 收发里返回、退出。
    void RegisterActiveConn(FileServerConn* conn);
    void UnregisterActiveConn(FileServerConn* conn);

    //向 UI 回投发文件最终结果（成功 / 失败统一走此）。
    void PostSendResult(const Task& task, bool ok, int errcode, const std::string& errmsg,
                        const std::string& fileid, const std::string& md5,
                        const std::string& filename, int64_t size);

    //向 UI 回投收文件最终结果（成功 / 失败统一走此）。md5 / filename 随成功结果带给 UI，
    //由它按 (md5, 文件名) 把 localPath 登记进文件本地落点库；失败时二者不被使用。
    void PostDownloadResult(int64_t msgid, bool ok, int errcode, const std::wstring& localPath,
                            const std::string& md5, const std::string& filename);

    //向 UI 回投语音下载结果（成功 / 失败统一走此），并把该 md5 移出在途集合。
    void PostVoiceDownloadResult(const std::string& md5, bool ok, int errcode, const std::wstring& localPath);

    //向 UI 回投发语音的结果（成功 / 失败统一走此）。
    void PostVoiceSendResult(const Task& task, bool ok, int errcode, const std::string& errmsg,
                             const std::string& fileid, const std::string& md5);

    //向 UI 回投群文件上传最终结果（成功 / 失败统一走此）。uploadId 让 UI 定位是哪一笔。
    void PostGroupFileUploadResult(int64_t groupId, int uploadId, bool ok, int errcode,
                                   const std::string& errmsg, const std::string& fileid,
                                   const std::string& filename, int64_t size,
                                   const std::string& md5 = std::string());

    //向 UI 回投群文件下载最终结果（成功 / 失败统一走此）。
    void PostGroupFileDownloadResult(int64_t groupId, const std::string& entryid,
                                     bool ok, int errcode, const std::wstring& localPath);

    //提交申请后阻塞等待 seq 对应的上传申请应答；超时返回 false。
    bool WaitUploadSignal(uint32_t seq, RequestUploadResult& out, int timeoutSec);

    //提交申请后阻塞等待 seq 对应的下载申请应答；超时返回 false。
    bool WaitDownloadSignal(uint32_t seq, RequestDownloadResult& out, int timeoutSec);

    std::unique_ptr<std::thread> m_worker;     //串行工作线程（单聊收发文件）
    std::mutex                   m_queueMtx;    //保护任务队列
    std::condition_variable      m_queueCv;     //队列非空 / 停止信号
    std::deque<Task>             m_queue;       //待处理任务（发送 / 下载）
    std::set<int64_t>            m_inflightDownloads;  //在途下载的 msgid 集合（受 m_queueMtx 保护）：
                                                       //同一条文件被 live 追加 + 历史渲染各触发一次下载时去重
    std::set<std::string>        m_inflightVoices;     //在途语音下载的 md5 集合（受 m_queueMtx 保护）：同一段语音
                                                       //出现在多条消息里、或收到推送与翻历史各触发一次时去重
    ChatTransferCancelRegistry   m_cancelReg;   //单聊收发的取消簿记（纯逻辑，受 m_queueMtx 保护）
    UploadInflightGate           m_uploadGate;  //同一文件只上传一份的门闩（自身加锁）：单聊串行 worker 与群文件
                                                //并发池在申请上传之前按 MD5 排队，同一文件同时发给群和单聊时
                                                //后来者等前一笔传完再申请、命中秒传。随单例存活到进程退出
    bool                         m_stop;        //停止标志（串行 worker 与并发池共用）
    bool                         m_started;     //是否已启动（Start 幂等用）

    //========== 群文件并发传输池 ==========
    std::vector<std::unique_ptr<std::thread> > m_poolWorkers;  //并发池工作线程（共 kMaxConcurrentGroupTransfers 个）
    std::mutex                   m_poolMtx;     //保护 m_poolQueue 与 m_activeConns 的全部访问
    std::condition_variable      m_poolCv;      //池队列非空 / 停止信号
    GroupTransferQueue           m_poolQueue;   //群文件待处理队列 + 取消 / 去重簿记（纯逻辑）
    std::set<FileServerConn*>    m_activeConns; //在传的独立连接集合（Stop 时逐个关掉以解阻塞）

    //上传信令应答关联（seq → 等待槽 / 早到应答暂存，防接收线程抢在注册前到达）。
    std::mutex                              m_waiterMtx;  //保护下面四个映射
    std::map<uint32_t, UploadWaiter*>       m_waiters;          //seq → 等待中的上传 worker 槽
    std::map<uint32_t, RequestUploadResult> m_earlyResponses;   //上传应答早到暂存
    //下载信令应答关联（同上，下载侧）。
    std::map<uint32_t, DownloadWaiter*>       m_dlWaiters;       //seq → 等待中的下载 worker 槽
    std::map<uint32_t, RequestDownloadResult> m_dlEarlyResponses;  //下载应答早到暂存
};

} // namespace net
