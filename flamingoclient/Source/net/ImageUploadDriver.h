/**
 *  图片 / 文件上传分片驱动（客户端侧，纯逻辑）。
 *
 *  把「按分片循环上传、跟随服务端 nextoffset 续传、收到 done 收尾」这段控制
 *  逻辑从具体的 socket / 帧收发里剥离出来，做成可单元测试的纯驱动：真实运行
 *  时传入走 v2 帧 + imgserver 连接的传输实现，单元测试时传入模拟服务端行为的
 *  假传输，便于离线复现秒传 / 正常分片 / 断点续传 / MD5 不一致等路径。
 *
 *  本模块只依赖 net/FileProtocolClient（取 UploadChunkAck 结构），不触及网络与
 *  UI，便于单元测试，见 flamingoclient/Source/Tests/ImageUploadDriverTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include "FileProtocolClient.h"     // UploadChunkAck

namespace net {

//上传分片的传输抽象：把文件中 [offset, offset+len) 这一片发到 fileserver /
//imgserver，并取回该片回执的解析结果。真实实现负责组 v2 附件帧、走 img
//socket 收发、用 FileProtocolClient 解析回执；单元测试用假实现模拟服务端。
class IUploadChunkTransport
{
public:
    virtual ~IUploadChunkTransport() {}

    /**
     *  发送一片并等待其回执。
     *  @param offset 本片在文件中的起始偏移（字节）
     *  @param data   文件全量字节的起始地址（驱动按 data+offset 取本片）
     *  @param len    本片字节数（可能为 0，如对齐到文件尾或探测续传位点）
     *  @param ack    [出参] 回执解析结果（含 code / done / nextoffset）
     *  @return 发送 / 收取 / 解析任一失败返回 false（视为传输层错误）
     */
    virtual bool uploadChunk(int64_t offset, const char* data, int64_t len, UploadChunkAck& ack) = 0;
};

//上传进度回调抽象（不用 lambda，以契合本仓库代码风格）。
class IUploadProgressSink
{
public:
    virtual ~IUploadProgressSink() {}

    /**
     *  上传进度变更时回调。
     *  @param sent  已确认上传到服务器的字节数（取服务端权威续传位点）
     *  @param total 文件总字节数
     */
    virtual void onProgress(int64_t sent, int64_t total) = 0;
};

//一次完整上传的驱动结果。
struct UploadDriveResult
{
    bool        m_ok;       //是否整文件上传成功（服务端回执 done）
    int         m_errcode;  //失败时的错误码：传输层错误用 error_code_upload_fail，
                            //服务端回执错误透传其 code（见 enum error_code）
    std::string m_errmsg;   //失败描述（便于日志与 UI 提示）

    UploadDriveResult()
        : m_ok(false)
        , m_errcode(0)
    {
    }
};

//上传分片驱动；全部为静态方法，不可实例化。
class ImageUploadDriver
{
public:
    /**
     *  驱动一次完整上传：从偏移 0 起按 chunksize 分片，逐片经 transport 发送，
     *  跟随服务端回执的 nextoffset 续传，直到回执 done 收尾。
     *  服务端可能因 .part 续传而要求跳过已有字节（nextoffset 一次跨过多片），
     *  驱动以服务端 nextoffset 为权威续传位点，不自行猜测。
     *  @param data      文件全量字节起始地址（图片场景 ≤20MiB，整文件常驻内存）
     *  @param size      文件总字节数（>0）
     *  @param chunksize 单片字节数；<=0 时回退到 FILE_CHUNK_SIZE
     *  @param transport 分片传输实现（真实走 img socket，测试用假实现）
     *  @param progress  进度回调，可为 NULL
     *  @return 上传结果；m_ok 为 true 表示成功
     */
    static UploadDriveResult run(const char* data, int64_t size, int chunksize,
                                 IUploadChunkTransport& transport,
                                 IUploadProgressSink* progress);

private:
    ImageUploadDriver() = delete;
    ~ImageUploadDriver() = delete;
    ImageUploadDriver(const ImageUploadDriver&) = delete;
    ImageUploadDriver& operator=(const ImageUploadDriver&) = delete;
};

} // namespace net
