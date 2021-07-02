/*
 *  压缩工具类，ZlibUtil.h
 *  zhangyl 2018.03.09
 */
#ifndef __ZLIB_UTIL_H__
#define __ZLIB_UTIL_H__
#include <string>

class ZlibUtil
{
private:
    ZlibUtil() = delete;
    ~ZlibUtil() = delete;
    ZlibUtil(const ZlibUtil& rhs) = delete;

public:
    static bool compressBuf(const char* pSrcBuf, size_t nSrcBufLength, char* pDestBuf, size_t& nDestBufLength);
    static bool compressBuf(const std::string& strSrcBuf, std::string& strDestBuf);
    static bool uncompressBuf(const std::string& strSrcBuf, std::string& strDestBuf, size_t nDestBufLength);

    //gzip压缩
    static bool inflate(const std::string& strSrc, std::string& dest);
    //gzip解压
    static bool deflate(const std::string& strSrc, std::string& strDest);

};



#endif //!__ZLIB_UTIL_H__