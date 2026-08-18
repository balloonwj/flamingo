/*
 *  压缩工具类，ZlibUtil.cpp
 *  zhangyl 2018.03.09
 */
#include "stdafx.h"
#include "../../third-party/zlib1.2.11/zlib.h"
#include <string.h>
#include "ZlibUtil.h"

//最大支持压缩10M
#define MAX_COMPRESS_BUF_SIZE 10*1024*1024

bool ZlibUtil::CompressBuf(const char* pSrcBuf, size_t nSrcBufLength, char* pDestBuf, size_t& nDestBufLength)
{
    
    if (pSrcBuf == NULL || nSrcBufLength == 0 || nSrcBufLength > MAX_COMPRESS_BUF_SIZE || pDestBuf == NULL)
        return false;

    //计算缓冲区大小，并为其分配内存
    //压缩后的长度是不会超过nDestBufLength的
    //上面已经限定 nSrcBufLength 不超过 MAX_COMPRESS_BUF_SIZE（10MB），收窄到 uLong 不会丢数据
    nDestBufLength = compressBound(static_cast<uLong>(nSrcBufLength));

    //压缩
    //同上，源数据长度受 MAX_COMPRESS_BUF_SIZE 限制，转换为 uLong 是安全的
    int ret = compress((Bytef*)pDestBuf, (uLongf*)&nDestBufLength, (const Bytef*)pSrcBuf, static_cast<uLong>(nSrcBufLength));
    if (ret != Z_OK)
        return false;

    return true;
}

bool ZlibUtil::CompressBuf(const std::string& strSrcBuf, std::string& strDestBuf)
{
    if (strSrcBuf.empty())
        return false;

    //长度用 size_t 保存，避免在下面的上限判断之前就因为收窄而得到错误的值
    size_t nSrcLength = strSrcBuf.length();
    if (nSrcLength > MAX_COMPRESS_BUF_SIZE)
        return false;

    //长度已确认不超过 MAX_COMPRESS_BUF_SIZE（10MB），转换为 zlib 要求的 uLong 不会丢数据
    int nDestBufLength = compressBound(static_cast<uLong>(nSrcLength));
    if (nDestBufLength <= 0)
        return false;

    char* pDestBuf = new char[nDestBufLength];
    memset(pDestBuf, 0, nDestBufLength * sizeof(char));

    //压缩
    //同上，源数据长度受 MAX_COMPRESS_BUF_SIZE 限制，转换为 uLong 是安全的
    int ret = compress((Bytef*)pDestBuf, (uLongf*)&nDestBufLength, (const Bytef*)strSrcBuf.c_str(), static_cast<uLong>(nSrcLength));
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
    //与 nDestBufLength 保持同一类型，便于下面直接比较解压前后的长度
    size_t nPrevDestBufLength = nDestBufLength;
    //解压缩
    //待解压的数据来自网络包体，长度远小于 uLong 能表示的上限，收窄是安全的
    int ret = uncompress((Bytef*)pDestBuf, (uLongf*)&nDestBufLength, (const Bytef*)strSrcBuf.c_str(), static_cast<uLong>(strSrcBuf.length()));
    if (ret != Z_OK)
    {
        delete[] pDestBuf;
        return false;
    }

    if (nPrevDestBufLength == nDestBufLength)
    {
        int k = 0;
        k++;
    }
    strDestBuf.append(pDestBuf, nDestBufLength);
    delete[] pDestBuf;

    return true;
}