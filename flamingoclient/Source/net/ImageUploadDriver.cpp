/**
 *  图片 / 文件上传分片驱动（客户端侧，纯逻辑）实现，ImageUploadDriver.cpp
 *  balloonwj@qq.com   2026-06-03
 */
#include "ImageUploadDriver.h"
#include "Protocol.h"   // FILE_CHUNK_SIZE / error_code_upload_fail

namespace net {

//连续若干次回执都未推进续传位点（nextoffset <= 当前 offset）即判定为卡死，
//避免服务端异常时驱动陷入死循环。正常续传每片都应推进，故阈值取较小值即可。
static const int kMaxStallRounds = 8;

UploadDriveResult ImageUploadDriver::run(const char* data, int64_t size, int chunksize,
                                         IUploadChunkTransport& transport,
                                         IUploadProgressSink* progress)
{
    UploadDriveResult result;

    //入参兜底：无数据或大小非正，直接判失败（不应发生，调用方应先校验）
    if (data == NULL || size <= 0)
    {
        result.m_errcode = error_code_upload_fail;
        result.m_errmsg  = "invalid upload data or size";
        return result;
    }

    //分片大小：服务端在 cmd_request_upload 应答里下发，缺省回退到协议常量
    int64_t chunk = (chunksize > 0) ? static_cast<int64_t>(chunksize)
                                     : static_cast<int64_t>(FILE_CHUNK_SIZE);

    int64_t offset     = 0;
    int     stallCount = 0;

    while (true)
    {
        //本片字节数：不超过分片上限，且不越过文件尾（对齐到尾时可能为 0）
        int64_t len = size - offset;
        if (len > chunk)
            len = chunk;
        if (len < 0)
            len = 0;

        UploadChunkAck ack;
        if (!transport.uploadChunk(offset, data, len, ack))
        {
            //传输层错误（发送 / 收取 / 解析失败）：连接多半已断，终止
            result.m_errcode = error_code_upload_fail;
            result.m_errmsg  = "chunk transport failed";
            return result;
        }

        //服务端回执错误码非 0（如 MD5 不一致 605 / 大小越界 600）：透传后终止
        if (ack.m_code != error_code_ok)
        {
            result.m_errcode = ack.m_code;
            result.m_errmsg  = ack.m_msg.empty() ? "server rejected chunk" : ack.m_msg;
            return result;
        }

        //收齐：服务端已校验 MD5 并转正，上传成功
        if (ack.m_done)
        {
            if (progress != NULL)
                progress->onProgress(size, size);
            result.m_ok = true;
            return result;
        }

        //未收齐：以服务端 nextoffset 为权威续传位点。
        int64_t next = ack.m_nextoffset;

        //防御：续传位点须落在 [0, size] 内；越界视为服务端异常
        if (next < 0 || next > size)
        {
            result.m_errcode = error_code_upload_fail;
            result.m_errmsg  = "server returned invalid nextoffset";
            return result;
        }

        //卡死保护：连续多轮未推进（服务端反复回相同 / 更小位点）即终止
        if (next <= offset)
        {
            ++stallCount;
            if (stallCount >= kMaxStallRounds)
            {
                result.m_errcode = error_code_upload_fail;
                result.m_errmsg  = "upload stalled, no progress from server";
                return result;
            }
        }
        else
        {
            stallCount = 0;
        }

        offset = next;
        if (progress != NULL)
            progress->onProgress(offset, size);
    }
}

} // namespace net
