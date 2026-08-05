/**
 *  FileStreamTransfer.h
 *
 *  文件字节流的「流式上传 / 流式下载」实现（纯逻辑核心，不依赖 socket / UI / 线程）。
 *
 *  背景：聊天文件与群文件都已取消大小上限，收发必须全程流式——上传分片从磁盘读、
 *  读一片发一片；下载收一片就写盘 + 增量算 MD5。任何"把整文件读进内存"的写法都会
 *  在 32 位客户端进程里 bad_alloc，故本文件的两个方法是收发大文件的唯一正路。
 *
 *  为什么独立成一个文件（而非留在 FileTransfer.cpp）：这两个方法只经 IFileFrameChannel
 *  收发帧、经参数拿到一切输入，不碰任何实例状态，也不认识 CIUSocket / 窗口消息。抽出来
 *  之后可单元测试（见 flamingoclient/Source/Tests/FileStreamTransferTests.cpp，Suite FST），
 *  用假通道把「取消标志在分片边界生效」「服务端 nextoffset 续传」「卡死保护」这些行为
 *  锁死。组织方式与 net/ImageUploadDriver、net/GroupTransferQueue 一致。
 *
 *  同一份实现服务两条链路：单聊收发文件跑在 CIUSocket 那条共享 file socket 上（串行），
 *  群文件收发跑在并发池里每笔各自独立的连接上。二者的差异全部收敛在传入的通道对象里。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include <stdint.h>
#include <string>
#include <atomic>

namespace net {

//上传 / 下载分片进度 sink 接口（分别定义在 ImageUploadDriver.h / ImageDownloadDriver.h，
//同属 net 命名空间）。流式收发用它逐片回报进度——单聊文件传更新气泡进度条的 sink，
//群文件传回投群文件进度消息的 sink；不需进度时传 nullptr。
class IUploadProgressSink;
class IDownloadProgressSink;

//分片收发单次 I/O 的超时，单位秒：单片 512KiB，30 秒足够，超时即判连接异常。
const int kChunkIoTimeoutSec = 30;

//连续多少轮回执都未推进续传位点即判服务端卡死、终止本次传输。正常每片都应推进，
//阈值取小即可。上传与下载同口径。
const int kMaxStreamStallRounds = 8;

/**
 *  IFileFrameChannel —— 文件帧通道接口。
 *
 *  把「在文件连接上发一整帧字节 / 收一整帧」抽象出来，让流式上传 / 下载的同一份实现
 *  既能跑在 CIUSocket 那条共享 file socket 上（单聊收发文件），也能跑在群文件并发池里
 *  每条独立的 FileServerConn 上，还能在单元测试里换成假通道。
 */
class IFileFrameChannel
{
public:
    virtual ~IFileFrameChannel() {}

    /**
     *  在文件连接上发送一整帧的原始字节（帧已由调用方组好）。
     *  @param data       帧字节起始地址（调用方拥有，本方法只读不持有）
     *  @param size       帧字节数
     *  @param timeoutSec 发送超时，单位秒
     *  @return 整帧发出返回 true；超时 / 连接异常返回 false
     */
    virtual bool SendBytes(const char* data, int64_t size, int timeoutSec) = 0;

    /**
     *  从文件连接上收取一个完整 v2 帧，回传其明文包体。
     *  @param body       [出参] 帧包体（上传回执 JSON，或下载的描述符 + 二进制）
     *  @param timeoutSec 收取超时，单位秒
     *  @return 收到完整帧返回 true；超时 / 帧非法返回 false
     */
    virtual bool RecvFrame(std::string& body, int timeoutSec) = 0;
};

/**
 *  FileStreamTransfer —— 文件字节流的流式收发（全静态方法，不可实例化）。
 *
 *  两个方法都在工作线程上、连接已就绪时调用，内部阻塞直到传完 / 失败 / 被取消。
 */
class FileStreamTransfer
{
public:
    /**
     *  流式上传一个文件的字节：分片从磁盘读、逐片发 cmd_file_upload(1851)、收回执，按
     *  服务端 nextoffset 续传，全程只占一个分片的内存（不把整文件读入）。
     *  @param channel     文件帧通道（共享 socket 或独立连接），帧从此发 / 回执从此收
     *  @param path        本地文件完整路径（宽字符）
     *  @param size        文件总字节数（>0）
     *  @param chunksize   分片字节数（来自上传应答，<=0 回退 FILE_CHUNK_SIZE）
     *  @param uploadToken 上传令牌（来自 cmd_request_upload 应答）
     *  @param progress    进度 sink（每片确认后回报已传字节）；nullptr 表示不报进度
     *  @param cancel      取消标志（每片开传前检查一次；nullptr=不可取消）。调用方在
     *                     UI 线程置位它即可中止本次传输，无需关连接
     *  @param errcode [出] 失败时的错误码（见 enum error_code）
     *  @param errmsg  [出] 失败时的诊断信息（仅供日志；判断「是否被取消」应直接读
     *                 cancel 标志，不要比对本串）
     *  @return 全部分片上传并被服务端收齐返回 true；任一步失败 / 被取消返回 false
     */
    static bool uploadFile(IFileFrameChannel& channel, const std::wstring& path, int64_t size,
                           int chunksize, const std::string& uploadToken,
                           IUploadProgressSink* progress, std::atomic<bool>* cancel,
                           int& errcode, std::string& errmsg);

    /**
     *  流式下载一个文件：分片请求 cmd_file_download(1852)、收一片就写临时文件 + 增量算
     *  MD5，全程只占一个分片的内存；下完校验 MD5 与大小后原子 rename 到 destPath。
     *  @param channel       文件帧通道（单聊用共享 socket，群文件用各自独立连接）
     *  @param downloadToken 下载令牌（来自 cmd_request_download 应答）
     *  @param expectMd5     期望MD5（小写十六进制）；空串表示跳过校验
     *  @param expectSize    期望字节数（语义占位，实际以服务端逐片应答的 totalsize 为准）
     *  @param destPath      落地目标完整路径（含文件名，其目录须已存在）；先写
     *                       destPath+".downloading" 临时文件、校验后原子 rename 到 destPath
     *  @param progress      进度 sink（每片落盘后回报已收字节）；nullptr 表示不报进度
     *  @param cancel        取消标志（每片请求前检查一次；nullptr=不可取消）。中止时
     *                       半成品临时文件会被删除，不留垃圾
     *  @param outLocalPath [出] 成功时文件落地的本地完整路径（即 destPath）
     *  @param errcode [出] 失败时的错误码
     *  @param errmsg  [出] 失败时的诊断信息（仅供日志；判断取消同上，读标志）
     *  @return 下载并校验通过、落地成功返回 true；任一步失败 / 被取消返回 false
     */
    static bool downloadFile(IFileFrameChannel& channel, const std::string& downloadToken,
                             const std::string& expectMd5, int64_t expectSize,
                             const std::wstring& destPath, IDownloadProgressSink* progress,
                             std::atomic<bool>* cancel,
                             std::wstring& outLocalPath, int& errcode, std::string& errmsg);

private:
    FileStreamTransfer() = delete;
    ~FileStreamTransfer() = delete;
    FileStreamTransfer(const FileStreamTransfer&) = delete;
    FileStreamTransfer& operator=(const FileStreamTransfer&) = delete;
};

} // namespace net
