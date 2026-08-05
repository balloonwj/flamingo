/**
 *  文件字节流的流式上传 / 下载实现，FileStreamTransfer.cpp
 *  设计与约定见 FileStreamTransfer.h。
 *  balloonwj@qq.com   2026-08-03
 */
#include <windows.h>                // DeleteFileW / MoveFileExW（临时文件清理与原子落地）

#include "FileStreamTransfer.h"

#include "FileProtocolClient.h"     // buildUploadChunkDescriptor / parseUploadChunkAck 等
#include "FrameCodec.h"             // encodeFrame / encodeAttachmentBody / decodeAttachmentBody（内含 Protocol.h）
#include "ImageUploadDriver.h"      // IUploadProgressSink
#include "ImageDownloadDriver.h"    // IDownloadProgressSink
//取 MD5 的纯 C 接口（下载侧增量校验用）。刻意不引 MD5Sum.h——那里的 MD5Sum 类依赖
//MFC 的 CString，而本文件与其单元测试都不链接 MFC。
#include "../MD5Core.h"             // MD5Init / MD5Update / MD5Final

#include <fstream>
#include <vector>                   // 分片缓冲
#include <stdio.h>                  // sprintf_s（MD5 十六进制串）

namespace net {

bool FileStreamTransfer::uploadFile(IFileFrameChannel& channel, const std::wstring& path, int64_t size,
                                    int chunksize, const std::string& uploadToken,
                                    IUploadProgressSink* progress, std::atomic<bool>* cancel,
                                    int& errcode, std::string& errmsg)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
    {
        errcode = error_code_upload_fail;
        errmsg  = "open file for upload failed";
        return false;
    }

    int64_t chunk = (chunksize > 0) ? static_cast<int64_t>(chunksize)
                                    : static_cast<int64_t>(FILE_CHUNK_SIZE);
    std::vector<char> buf(static_cast<size_t>(chunk));   //只占一个分片的内存
    int64_t  offset     = 0;
    int      stallCount = 0;
    uint32_t frameSeq   = 1;

    while (true)
    {
        //取消检查：单聊发文件与群文件上传都会传入取消标志，用户点「取消」/ ✕ 后
        //在分片边界中止本次上传。放在循环最前面，使排队末尾刚被取消的那笔也能立即停。
        if (cancel != NULL && cancel->load())
        {
            errcode = error_code_upload_fail;
            errmsg  = "upload cancelled by user";
            return false;
        }

        //本片字节数：不超过分片上限、不越过文件尾（对齐到尾时为 0，发末片让服务端收齐）
        int64_t len = size - offset;
        if (len > chunk)
            len = chunk;
        if (len < 0)
            len = 0;

        //从磁盘读本片 [offset, offset+len)（按服务端续传位点 seek，支持非顺序续传）
        if (len > 0)
        {
            in.clear();   //清掉上一轮可能置位的 eofbit，确保 seek/read 可用
            in.seekg(offset, std::ios::beg);
            in.read(buf.data(), static_cast<std::streamsize>(len));
            if (in.gcount() != static_cast<std::streamsize>(len))
            {
                errcode = error_code_upload_fail;
                errmsg  = "read chunk from file failed";
                return false;
            }
        }

        //组 cmd_file_upload 附件帧（描述符 + 本片二进制），经通道发出
        std::string descriptor = FileProtocolClient::buildUploadChunkDescriptor(uploadToken, offset);
        std::string attBody;
        if (!net::encodeAttachmentBody(descriptor, buf.data(),
                                       static_cast<uint32_t>(len), attBody))
        {
            errcode = error_code_upload_fail;
            errmsg  = "encode attachment failed";
            return false;
        }
        std::string frame;
        if (!net::encodeFrame(cmd_file_upload, frameSeq++, frame_flag_has_attachment,
                              attBody.data(), static_cast<uint32_t>(attBody.size()),
                              static_cast<uint32_t>(attBody.size()), frame))
        {
            errcode = error_code_upload_fail;
            errmsg  = "encode frame failed";
            return false;
        }
        if (!channel.SendBytes(frame.data(), static_cast<int64_t>(frame.size()),
                               kChunkIoTimeoutSec))
        {
            errcode = error_code_upload_fail;
            errmsg  = "send chunk failed";
            return false;
        }

        //收回执帧（普通 JSON 包体，无附件）
        std::string ackBody;
        if (!channel.RecvFrame(ackBody, kChunkIoTimeoutSec))
        {
            errcode = error_code_upload_fail;
            errmsg  = "recv chunk ack failed";
            return false;
        }
        UploadChunkAck ack;
        if (!FileProtocolClient::parseUploadChunkAck(ackBody, ack))
        {
            errcode = error_code_upload_fail;
            errmsg  = "parse chunk ack failed";
            return false;
        }
        if (ack.m_code != error_code_ok)
        {
            errcode = ack.m_code;
            errmsg  = ack.m_msg.empty() ? "server rejected chunk" : ack.m_msg;
            return false;
        }
        if (ack.m_done)
        {
            if (progress != NULL)
                progress->onProgress(size, size);
            return true;   //服务端已收齐并校验通过
        }

        //未收齐：以服务端 nextoffset 为权威续传位点
        int64_t next = ack.m_nextoffset;
        if (next < 0 || next > size)
        {
            errcode = error_code_upload_fail;
            errmsg  = "server returned invalid nextoffset";
            return false;
        }
        //卡死保护：连续多轮未推进即终止
        if (next <= offset)
        {
            ++stallCount;
            if (stallCount >= kMaxStreamStallRounds)
            {
                errcode = error_code_upload_fail;
                errmsg  = "upload stalled, no progress from server";
                return false;
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

bool FileStreamTransfer::downloadFile(IFileFrameChannel& channel, const std::string& downloadToken,
                                      const std::string& expectMd5, int64_t expectSize,
                                      const std::wstring& destPath, IDownloadProgressSink* progress,
                                      std::atomic<bool>* cancel,
                                      std::wstring& outLocalPath, int& errcode, std::string& errmsg)
{
    (void)expectSize;   //大小以服务端逐片应答的 totalsize 为权威，expectSize 仅作语义占位

    //落地到 destPath（其目录由调用方保证已存在）。先写临时文件、校验 MD5 后原子 rename，
    //保证只要最终文件存在即为完整文件（崩在写中途只留临时文件）。
    std::wstring tmpPath = destPath + L".downloading";

    std::ofstream out(tmpPath.c_str(), std::ios::binary | std::ios::trunc);
    if (!out)
    {
        errcode = error_code_server_internal;
        errmsg  = "open temp file for download failed";
        return false;
    }

    MD5_CTX ctx;
    MD5Init(&ctx);

    int64_t  offset     = 0;
    int      stallCount = 0;
    uint32_t frameSeq   = 1;
    bool     ok         = false;

    while (true)
    {
        //取消检查：单聊收文件与群文件下载都会传入取消标志，用户点「取消」/ ✕ 后在分片
        //边界中止（循环外会删掉半成品临时文件）。
        if (cancel != NULL && cancel->load())
        {
            errcode = error_code_server_internal;
            errmsg  = "download cancelled by user";
            break;
        }

        //请求帧：普通 JSON 包体 { downloadtoken, offset }，无附件、不压缩
        std::string descriptor = FileProtocolClient::buildDownloadChunkDescriptor(downloadToken, offset);
        std::string frame;
        if (!net::encodeFrame(cmd_file_download, frameSeq++, descriptor, frame))
        {
            errcode = error_code_server_internal;
            errmsg  = "encode download frame failed";
            break;
        }
        if (!channel.SendBytes(frame.data(), static_cast<int64_t>(frame.size()),
                               kChunkIoTimeoutSec))
        {
            errcode = error_code_server_internal;
            errmsg  = "send download request failed";
            break;
        }

        //应答帧：HAS_ATTACHMENT，包体 = 4字节desclen + 描述符 + 本片二进制
        std::string body;
        if (!channel.RecvFrame(body, kChunkIoTimeoutSec))
        {
            errcode = error_code_server_internal;
            errmsg  = "recv download chunk failed";
            break;
        }
        std::string respDesc;
        size_t binoff = 0;
        size_t binlen = 0;
        if (!net::decodeAttachmentBody(body.data(), body.size(), respDesc, binoff, binlen))
        {
            errcode = error_code_server_internal;
            errmsg  = "decode download attachment failed";
            break;
        }
        DownloadChunkInfo info;
        if (!FileProtocolClient::parseDownloadChunkInfo(respDesc, info))
        {
            errcode = error_code_server_internal;
            errmsg  = "parse download chunk info failed";
            break;
        }
        if (info.m_code != error_code_ok)
        {
            errcode = info.m_code;
            errmsg  = info.m_msg.empty() ? "server download error" : info.m_msg;
            break;
        }

        //写本片到临时文件 + 增量算 MD5（只占一个分片的内存，不累积整文件）
        if (binlen > 0)
        {
            out.write(body.data() + binoff, static_cast<std::streamsize>(binlen));
            if (!out)
            {
                errcode = error_code_server_internal;
                errmsg  = "write temp download file failed";
                break;
            }
            MD5Update(&ctx, reinterpret_cast<const unsigned char*>(body.data() + binoff),
                      static_cast<unsigned int>(binlen));
        }

        int64_t total     = info.m_totalsize;
        int64_t newOffset = offset + static_cast<int64_t>(binlen);

        if (progress != NULL)
            progress->onProgress(newOffset, total);

        //收齐：服务端标记 done。校验累计大小与服务端 totalsize 一致（防御早 done）。
        if (info.m_done)
        {
            if (total > 0 && newOffset != total)
            {
                errcode = error_code_server_internal;
                errmsg  = "downloaded size mismatch";
                break;
            }
            ok = true;
            break;
        }
        //防御：累计偏移不得越过文件尾
        if (total > 0 && newOffset > total)
        {
            errcode = error_code_server_internal;
            errmsg  = "download overran total size";
            break;
        }
        //卡死保护：未完成却没收到任何新字节，连续多片即终止
        if (newOffset <= offset)
        {
            ++stallCount;
            if (stallCount >= kMaxStreamStallRounds)
            {
                errcode = error_code_server_internal;
                errmsg  = "download stalled, no progress from server";
                break;
            }
        }
        else
        {
            stallCount = 0;
        }
        offset = newOffset;
    }

    out.close();
    if (!ok)
    {
        ::DeleteFileW(tmpPath.c_str());   //失败 / 取消：清掉临时文件
        return false;
    }

    //MD5 校验：累计算出的小写 hex 与期望比对
    unsigned char digest[16] = { 0 };
    MD5Final(digest, &ctx);
    char hex[33] = { 0 };
    for (int i = 0; i < 16; ++i)
        sprintf_s(hex + i * 2, 3, "%02x", digest[i]);
    std::string actual(hex, 32);
    std::string expect = expectMd5;
    for (size_t i = 0; i < expect.size(); ++i)
    {
        if (expect[i] >= 'A' && expect[i] <= 'F')
            expect[i] = static_cast<char>(expect[i] - 'A' + 'a');
    }
    if (!expect.empty() && actual != expect)
    {
        ::DeleteFileW(tmpPath.c_str());
        errcode = error_code_md5_mismatch;
        errmsg  = "download md5 mismatch";
        return false;
    }

    //原子落地：rename 临时文件到目标路径
    if (!::MoveFileExW(tmpPath.c_str(), destPath.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        ::DeleteFileW(tmpPath.c_str());
        errcode = error_code_server_internal;
        errmsg  = "finalize download (rename) failed";
        return false;
    }

    outLocalPath = destPath;
    return true;
}

} // namespace net
