/**
 *  图片 / 文件下载分片驱动（客户端侧，纯逻辑）实现，ImageDownloadDriver.cpp
 *  balloonwj@qq.com   2026-06-03
 */
#include "ImageDownloadDriver.h"
#include "Protocol.h"   // error_code_*

namespace net {

//连续若干片都未推进（收到空片且未完成）即判定卡死，避免服务端异常时死循环。
static const int kMaxStallRounds = 8;

DownloadDriveResult ImageDownloadDriver::run(IDownloadChunkTransport& transport,
                                             std::string& out, IDownloadProgressSink* progress)
{
    DownloadDriveResult result;
    out.clear();

    int64_t offset     = 0;
    int     stallCount = 0;

    while (true)
    {
        DownloadChunkInfo info;
        std::string       chunk;
        if (!transport.downloadChunk(offset, info, chunk))
        {
            //传输层错误（发送 / 收取 / 拆包 / 解析失败）：连接多半已断，终止
            result.m_errcode = error_code_server_internal;
            result.m_errmsg  = "chunk transport failed";
            return result;
        }

        //服务端应答错误码非 0（如文件不存在 601 / 令牌无效 603）：透传后终止
        if (info.m_code != error_code_ok)
        {
            result.m_errcode = info.m_code;
            result.m_errmsg  = info.m_msg.empty() ? "server download error" : info.m_msg;
            return result;
        }

        //累计本片字节
        if (!chunk.empty())
            out.append(chunk);

        int64_t total     = info.m_totalsize;
        int64_t newOffset  = offset + static_cast<int64_t>(chunk.size());

        if (progress != NULL)
            progress->onProgress(newOffset, total);

        //收齐：服务端标记 done。校验累计大小与服务端 totalsize 一致（防御早 done）。
        if (info.m_done)
        {
            if (total > 0 && static_cast<int64_t>(out.size()) != total)
            {
                result.m_errcode = error_code_server_internal;
                result.m_errmsg  = "downloaded size mismatch";
                return result;
            }
            result.m_ok = true;
            return result;
        }

        //防御：累计偏移不得越过文件尾
        if (total > 0 && newOffset > total)
        {
            result.m_errcode = error_code_server_internal;
            result.m_errmsg  = "download overran total size";
            return result;
        }

        //卡死保护：未完成却没收到任何新字节，连续多片即终止
        if (newOffset <= offset)
        {
            ++stallCount;
            if (stallCount >= kMaxStallRounds)
            {
                result.m_errcode = error_code_server_internal;
                result.m_errmsg  = "download stalled, no progress from server";
                return result;
            }
        }
        else
        {
            stallCount = 0;
        }

        offset = newOffset;
    }
}

} // namespace net
