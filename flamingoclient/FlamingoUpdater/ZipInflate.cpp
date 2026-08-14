/**
 *  zip 条目解压的实现，ZipInflate.cpp
 *  balloonwj@qq.com   2026-08-12
 */
#include "ZipInflate.h"

#include "../Source/zlib1.2.11/zlib.h"

namespace iupdate {

namespace {

//一次喂给 inflate 的输出缓冲大小（字节）。解压是按块循环的，这个值只影响循环次数，
//取 64KiB 在内存占用与循环开销之间比较均衡。
const size_t kInflateChunkSize = 64 * 1024;

} // namespace

bool InflateRaw(const unsigned char* src, size_t srcLen, size_t expectedSize,
                std::vector<unsigned char>& out, std::wstring& errmsg)
{
    out.clear();
    errmsg.clear();

    //长度为 0 的条目是合法的（空文件），直接返回空内容
    if (expectedSize == 0)
    {
        return true;
    }
    if (src == NULL || srcLen == 0)
    {
        errmsg = L"压缩数据为空，但条目声明的解压后长度不为零。";
        return false;
    }

    z_stream stream;
    memset(&stream, 0, sizeof(stream));

    //-MAX_WBITS 表示 raw deflate：zip 存的是不带 zlib 头尾的裸流，
    //按普通 zlib 流去解会在第一步就报数据错。
    int ret = inflateInit2(&stream, -MAX_WBITS);
    if (ret != Z_OK)
    {
        errmsg = L"初始化解压器失败。";
        return false;
    }

    stream.next_in  = (Bytef*)src;
    stream.avail_in = (uInt)srcLen;

    //按期望长度预留，省掉反复扩容
    out.reserve(expectedSize);

    std::vector<unsigned char> chunk(kInflateChunkSize);
    bool ok = true;

    for (;;)
    {
        stream.next_out  = (Bytef*)&chunk[0];
        stream.avail_out = (uInt)chunk.size();

        ret = inflate(&stream, Z_NO_FLUSH);

        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
        {
            errmsg = L"解压失败，升级包内容已损坏。";
            ok = false;
            break;
        }

        const size_t produced = chunk.size() - (size_t)stream.avail_out;
        if (produced > 0)
        {
            out.insert(out.end(), chunk.begin(), chunk.begin() + produced);
        }

        if (ret == Z_STREAM_END)
        {
            break;
        }

        //既没产出、输入也用完了：再循环下去就是死循环，判为数据不完整
        if (produced == 0 && stream.avail_in == 0)
        {
            errmsg = L"压缩数据不完整，解压未能正常结束。";
            ok = false;
            break;
        }
    }

    inflateEnd(&stream);

    if (!ok)
    {
        out.clear();
        return false;
    }

    //长度必须与条目声明的一致。短一截的文件写进安装目录，比解压失败更糟——
    //程序会以一种难以排查的方式坏掉。
    if (out.size() != expectedSize)
    {
        errmsg = L"解压后长度与升级包记录的不一致，包已损坏。";
        out.clear();
        return false;
    }

    return true;
}

uint32_t ComputeCrc32(const unsigned char* data, size_t len)
{
    if (data == NULL || len == 0)
    {
        return 0;
    }
    return (uint32_t)crc32(0L, (const Bytef*)data, (uInt)len);
}

} // namespace iupdate
