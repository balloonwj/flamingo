/*
 *  —πÀıπ§æﬂ¿‡£¨ZlibUtil.h
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
    static bool CompressBuf(const char* pSrcBuf, size_t nSrcBufLength, char* pDestBuf, size_t& nDestBufLength);
    static bool CompressBuf(const std::string& strSrcBuf, std::string& strDestBuf);
    static bool UncompressBuf(const std::string& strSrcBuf, std::string& strDestBuf, size_t nDestBufLength);
};



#endif //!__ZLIB_UTIL_H__