/**
 *  压缩工具类 ZlibUtil 的实现，说明见 ZlibUtil.h。
 *
 *  关于长度参数的类型：zlib 的长度参数是 uLong，在 Windows 上是 32 位；size_t 在 x64 下是 64 位，
 *  两者宽度不一定相同。以前把 size_t 变量的地址强转成 uLongf* 交给 zlib，x64 下 zlib 只写入
 *  低 4 字节，结果是否正确取决于高 4 字节原本是否为零。现在一律先用 uLong 类型的局部变量
 *  承接长度，调用结束后再使用，与服务端 flamingoserver/zlib1.2.11/ZlibUtil.cpp 的写法一致。
 *  balloonwj@qq.com   2018-03-09
 */
#include "stdafx.h"
#include "../../third-party/zlib1.2.11/zlib.h"
#include <string.h>
#include "ZlibUtil.h"

//最大支持压缩10M
#define MAX_COMPRESS_BUF_SIZE 10*1024*1024

bool ZlibUtil::CompressBuf(const std::string& strSrcBuf, std::string& strDestBuf)
{
    if (strSrcBuf.empty())
        return false;

    //长度用 size_t 保存，避免在下面的上限判断之前就因为收窄而得到错误的值
    size_t nSrcLength = strSrcBuf.length();
    if (nSrcLength > MAX_COMPRESS_BUF_SIZE)
        return false;

    //长度已确认不超过 MAX_COMPRESS_BUF_SIZE（10MB），转换为 zlib 要求的 uLong 不会丢数据
    const uLong nSrcLen = static_cast<uLong>(nSrcLength);
    uLong nDestBufLength = compressBound(nSrcLen);
    if (nDestBufLength == 0)
        return false;

    char* pDestBuf = new char[nDestBufLength];
    memset(pDestBuf, 0, nDestBufLength * sizeof(char));

    //压缩。compress 把实际压缩后的长度写回 nDestBufLength
    int ret = compress((Bytef*)pDestBuf, &nDestBufLength, (const Bytef*)strSrcBuf.c_str(), nSrcLen);
    if (ret != Z_OK)
    {
        delete[] pDestBuf;
        return false;
    }

    strDestBuf.append(pDestBuf, nDestBufLength);
    delete[] pDestBuf;

    return true;
}

bool ZlibUtil::UncompressBuf(const std::string& strSrcBuf, std::string& strDestBuf, size_t nDestBufLength)
{
    char* pDestBuf = new char[nDestBufLength];
    memset(pDestBuf, 0, nDestBufLength * sizeof(char));

    //目标长度来自帧头的 originlen（uint32_t），且已由 decodeFrame 校验不超过包体上限，
    //转换为 uLong 不会丢数据。uncompress 把实际解压出的长度写回 nDestLen
    uLong nDestLen = static_cast<uLong>(nDestBufLength);
    //解压缩
    //待解压的数据来自网络包体，长度远小于 uLong 能表示的上限，收窄是安全的
    int ret = uncompress((Bytef*)pDestBuf, &nDestLen, (const Bytef*)strSrcBuf.c_str(), static_cast<uLong>(strSrcBuf.length()));
    if (ret != Z_OK)
    {
        delete[] pDestBuf;
        return false;
    }

    strDestBuf.append(pDestBuf, nDestLen);
    delete[] pDestBuf;

    return true;
}