/**
 *  图片 / 文件下载分片驱动（客户端侧，纯逻辑）。
 *
 *  与 ImageUploadDriver 镜像：把「从偏移 0 起逐片请求下载、按服务端 done 收尾、
 *  拼成完整字节」这段控制逻辑从具体的 socket / 帧收发里剥离出来，做成可单元
 *  测试的纯驱动。真实运行时传入走 v2 帧 + imgserver 连接的传输实现，单元测试
 *  时传入模拟服务端行为的假传输，便于离线复现单片 / 多片 / 服务端错误 / 传输
 *  失败 / 大小不符 / 卡死等路径。
 *
 *  下载分片由服务端按 FILE_CHUNK_SIZE 切分，客户端只按当前偏移请求、收到多少
 *  就推进多少（与上传由客户端切分不同），故本驱动无 chunksize 参数。
 *
 *  本模块只依赖 net/FileProtocolClient（取 DownloadChunkInfo 结构），不触及网络
 *  与 UI，便于单元测试，见 flamingoclient/Source/Tests/ImageDownloadDriverTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include "FileProtocolClient.h"     // DownloadChunkInfo

namespace net {

//下载分片的传输抽象：请求文件从 offset 开始的一片，取回该片的描述符解析结果
//与二进制数据。真实实现负责组 cmd_file_download 请求帧、走 img socket 收发、
//用 FrameCodec 拆附件、用 FileProtocolClient 解析描述符；单元测试用假实现。
class IDownloadChunkTransport
{
public:
    virtual ~IDownloadChunkTransport() {}

    /**
     *  请求并接收从 offset 开始的一片。
     *  @param offset 请求的起始偏移（字节）
     *  @param info   [出参] 该片应答帧描述符解析结果（含 code / offset / totalsize / done）
     *  @param chunk  [出参] 该片二进制数据（本方法内填充为「本片」字节，非累计）
     *  @return 发送 / 收取 / 解析任一失败返回 false（视为传输层错误）
     */
    virtual bool downloadChunk(int64_t offset, DownloadChunkInfo& info, std::string& chunk) = 0;
};

//下载进度回调抽象（不用 lambda，以契合本仓库代码风格）。
class IDownloadProgressSink
{
public:
    virtual ~IDownloadProgressSink() {}

    /**
     *  下载进度变更时回调。
     *  @param received 已接收并拼接的字节数
     *  @param total    文件总字节数（服务端在每片描述符里给出）
     */
    virtual void onProgress(int64_t received, int64_t total) = 0;
};

//一次完整下载的驱动结果。
struct DownloadDriveResult
{
    bool        m_ok;       //是否整文件下载成功（服务端回执 done 且大小一致）
    int         m_errcode;  //失败时的错误码：传输层错误用 error_code_server_internal，
                            //服务端应答错误透传其 code（见 enum error_code）
    std::string m_errmsg;   //失败描述

    DownloadDriveResult()
        : m_ok(false)
        , m_errcode(0)
    {
    }
};

//下载分片驱动；全部为静态方法，不可实例化。
class ImageDownloadDriver
{
public:
    /**
     *  驱动一次完整下载：从偏移 0 起逐片经 transport 请求，按服务端每片返回的
     *  字节数推进偏移，直到某片 done 收尾；各片二进制依次拼进 out。
     *  @param transport 分片传输实现（真实走 img socket，测试用假实现）
     *  @param out       [出参] 下载到的完整字节（进入前会被清空）
     *  @param progress  进度回调，可为 NULL
     *  @return 下载结果；m_ok 为 true 表示成功（done 且累计大小等于服务端 totalsize）
     */
    static DownloadDriveResult run(IDownloadChunkTransport& transport,
                                   std::string& out, IDownloadProgressSink* progress);

private:
    ImageDownloadDriver() = delete;
    ~ImageDownloadDriver() = delete;
    ImageDownloadDriver(const ImageDownloadDriver&) = delete;
    ImageDownloadDriver& operator=(const ImageDownloadDriver&) = delete;
};

} // namespace net
