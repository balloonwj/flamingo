/*
 *  压缩工具类，ZlibUtil.cpp
 *  zhangyl 2018.03.09
 */
#include "../zlib1.2.11/zlib.h"
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
    nDestBufLength = compressBound(nSrcBufLength);

    //压缩
    int ret = compress((Bytef*)pDestBuf, (uLongf*)&nDestBufLength, (const Bytef*)pSrcBuf, nSrcBufLength);
    if (ret != Z_OK)
        return false;

    return true;
}

bool ZlibUtil::CompressBuf(const std::string& strSrcBuf, std::string& strDestBuf)
{
    if (strSrcBuf.empty())
        return false;

    int nSrcLength = strSrcBuf.length();
    if (nSrcLength > MAX_COMPRESS_BUF_SIZE)
        return false;

    int nDestBufLength = compressBound(nSrcLength);
    if (nDestBufLength <= 0)
        return false;

    char* pDestBuf = new char[nDestBufLength];
    memset(pDestBuf, 0, nDestBufLength * sizeof(char));

    //压缩
    int ret = compress((Bytef*)pDestBuf, (uLongf*)&nDestBufLength, (const Bytef*)strSrcBuf.c_str(), nSrcLength);
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
    int nPrevDestBufLength = nDestBufLength;
    //解压缩
    int ret = uncompress((Bytef*)pDestBuf, (uLongf*)&nDestBufLength, (const Bytef*)strSrcBuf.c_str(), strSrcBuf.length());
    if (ret != Z_OK)
    {
        delete[] pDestBuf;
        return false;
    }

    //if (nPrevDestBufLength == nDestBufLength)
    //{
    //    int k = 0;
    //    k++;
    //}
    strDestBuf.append(pDestBuf, nDestBufLength);
    delete[] pDestBuf;

    return true;
}