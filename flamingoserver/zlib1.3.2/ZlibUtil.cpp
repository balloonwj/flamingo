/*
 *  压缩工具类，ZlibUtil.cpp
 *  zhangyl 2018.03.09
 */
#include "zlib.h"
#include <string.h>
#include "ZlibUtil.h"

 //最大支持压缩10M
#define MAX_COMPRESS_BUF_SIZE 10*1024*1024

bool ZlibUtil::compressBuf(const char* pSrcBuf, size_t nSrcBufLength, char* pDestBuf, size_t & nDestBufLength)
{

    if (pSrcBuf == NULL || nSrcBufLength == 0 || nSrcBufLength > MAX_COMPRESS_BUF_SIZE || pDestBuf == NULL)
        return false;

    //计算缓冲区大小，并为其分配内存
    //压缩后的长度是不会超过nDestBufLength的
    //zlib 的长度参数类型是 uLong：Windows 上 32 位、Linux 上 64 位，与 size_t 都不一定同宽。
    //以前把 size_t* 直接强转成 uLongf* 交给 zlib，Linux 下等于让它往一个 4 字节的对象写 8 字节，
    //Windows x64 下则只写低 32 位，都是碰巧没出事。改用 uLong 中转变量，压缩完再写回
    uLong nSrcLen = static_cast<uLong>(nSrcBufLength);
    uLong nDestLen = compressBound(nSrcLen);
    nDestBufLength = nDestLen;

    //压缩
    int ret = compress((Bytef*)pDestBuf, &nDestLen, (const Bytef*)pSrcBuf, nSrcLen);
    if (ret != Z_OK)
        return false;

    //compress 会把实际压缩后的长度写回第二个参数
    nDestBufLength = nDestLen;

    return true;
}

bool ZlibUtil::compressBuf(const std::string& strSrcBuf, std::string& strDestBuf)
{
    if (strSrcBuf.empty())
        return false;

    //长度一律用 zlib 自己的 uLong，理由见上面 compressBuf 里的说明
    uLong nSrcLength = static_cast<uLong>(strSrcBuf.length());
    if (nSrcLength > static_cast<uLong>(MAX_COMPRESS_BUF_SIZE))
        return false;

    uLong nDestBufLength = compressBound(nSrcLength);
    if (nDestBufLength == 0)
        return false;

    char* pDestBuf = new char[nDestBufLength];
    memset(pDestBuf, 0, nDestBufLength * sizeof(char));

    //压缩
    int ret = compress((Bytef*)pDestBuf, &nDestBufLength, (const Bytef*)strSrcBuf.c_str(), nSrcLength);
    if (ret != Z_OK)
    {
        delete[] pDestBuf;
        return false;
    }

    strDestBuf.append(pDestBuf, nDestBufLength);
    delete[] pDestBuf;

    return true;
}

bool ZlibUtil::uncompressBuf(const std::string& strSrcBuf, std::string& strDestBuf, size_t nDestBufLength)
{
    char* pDestBuf = new char[nDestBufLength];
    memset(pDestBuf, 0, nDestBufLength * sizeof(char));
    //长度一律用 zlib 自己的 uLong，理由见上面 compressBuf 里的说明
    uLong nDestLen = static_cast<uLong>(nDestBufLength);
    //解压缩
    int ret = uncompress((Bytef*)pDestBuf, &nDestLen, (const Bytef*)strSrcBuf.c_str(),
                         static_cast<uLong>(strSrcBuf.length()));
    nDestBufLength = nDestLen;
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

bool ZlibUtil::deflate(const std::string& strSrc, std::string& strDest)
{
    int err = Z_DATA_ERROR;
    // Create stream
    z_stream zS = { 0 };
    // Set output data streams, do this here to avoid overwriting on recursive calls
    const int OUTPUT_BUF_SIZE = 8192;
    Bytef bytesOut[OUTPUT_BUF_SIZE] = { 0 };

    // Initialise the z_stream
    err = ::deflateInit2(&zS, 1, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    if (err != Z_OK)
    {
        //TRACE_UNZIP("; Error: new stream failed: %d\n", err);
        return false;
    }
    // Use whatever input is provided
    zS.next_in = (Bytef*)(strSrc.c_str());
    zS.avail_in = static_cast<uInt>(strSrc.length());

    do {
        try
        {
            // Initialise stream values
            //zS->zalloc = (alloc_func)0;
            //zS->zfree = (free_func)0;
            //zS->opaque = (voidpf)0;

            zS.next_out = bytesOut;
            zS.avail_out = OUTPUT_BUF_SIZE;

            // Try to unzip the data
            //TRACE_UNZIP("; inflate(ain=%6u tin=%6u aout=%6u tout=%6u)", zS->avail_in, zS->total_in, zS->avail_out, zS->total_out);
            err = ::deflate(&zS, Z_SYNC_FLUSH);

            // Is zip finished reading all currently available input and writing all generated output
            if (err == Z_STREAM_END)
            {
                // Finish up
                int kerr = ::deflateEnd(&zS);
                //不关心返回结果
                //if (err != Z_OK)
                //{
                //    //TRACE_UNZIP("; Error: end stream failed: %d\n", err);
                //    return false;
                //}
                //TRACE_UNZIP("; Z_STREAM_END\n");

                // Got a good result, set the size to the amount unzipped in this call (including all recursive calls)

                strDest.append((const char*)bytesOut, OUTPUT_BUF_SIZE - zS.avail_out);
                return true;
            }
            else if ((err == Z_OK) && (zS.avail_out == 0) && (zS.avail_in != 0))
            {
                // Output array was not big enough, call recursively until there is enough space
                //TRACE_UNZIP("; output array not big enough (ain=%u)\n", zS->avail_in);

                strDest.append((const char*)bytesOut, OUTPUT_BUF_SIZE - zS.avail_out);

                continue;
            }
            else if ((err == Z_OK) && (zS.avail_in == 0))
            {
                //TRACE_UNZIP("; all input processed\n");
                // All available input has been processed, everything ok.
                // Set the size to the amount unzipped in this call (including all recursive calls)              
                strDest.append((const char*)bytesOut, OUTPUT_BUF_SIZE - zS.avail_out);

                int kerr = ::deflateEnd(&zS);
                //不关心结果
                //if (err != Z_OK)
                //{
                //    //TRACE_UNZIP("; Error: end stream failed: %d\n", err);
                //    return false;
                //}

                break;
            }
            else
            {
                return false;
            }
        }
        catch (...)
        {
            return false;
        }
    } while (true);


    if (err == Z_OK)
    {
        //减去4是为了去掉deflat函数加在末尾多余的00 00 ff ff
        strDest = strDest.substr(0, strDest.length() - 4);
        return true;
    }

    return false;
}

bool ZlibUtil::inflate(const std::string& strSrc, std::string& strDest)
{
    int err = Z_DATA_ERROR;
    // Create stream
    z_stream zS = { 0 };
    // Set output data streams, do this here to avoid overwriting on recursive calls
    const int OUTPUT_BUF_SIZE = 8192;
    Bytef bytesOut[OUTPUT_BUF_SIZE] = { 0 };

    // Initialise the z_stream
    err = ::inflateInit2(&zS, -15);
    if (err != Z_OK)
    {
        //TRACE_UNZIP("; Error: new stream failed: %d\n", err);
        return false;
    }

    // Use whatever input is provided
    zS.next_in = (Bytef*)(strSrc.c_str());
    zS.avail_in = static_cast<uInt>(strSrc.length());

    do {
        try
        {
            // Initialise stream values
            //zS->zalloc = (alloc_func)0;
            //zS->zfree = (free_func)0;
            //zS->opaque = (voidpf)0;

            zS.next_out = bytesOut;
            zS.avail_out = OUTPUT_BUF_SIZE;

            // Try to unzip the data
            //TRACE_UNZIP("; inflate(ain=%6u tin=%6u aout=%6u tout=%6u)", zS->avail_in, zS->total_in, zS->avail_out, zS->total_out);
            err = ::inflate(&zS, Z_SYNC_FLUSH);

            // Is zip finished reading all currently available input and writing all generated output
            if (err == Z_STREAM_END)
            {
                // Finish up
                int kerr = ::inflateEnd(&zS);
                //不关心返回结果
                //if (err != Z_OK)
                //{
                //    //TRACE_UNZIP("; Error: end stream failed: %d\n", err);
                //    return false;
                //}
                //TRACE_UNZIP("; Z_STREAM_END\n");

                // Got a good result, set the size to the amount unzipped in this call (including all recursive calls)

                strDest.append((const char*)bytesOut, OUTPUT_BUF_SIZE - zS.avail_out);
                return true;
            }
            else if ((err == Z_OK) && (zS.avail_out == 0) && (zS.avail_in != 0))
            {
                // Output array was not big enough, call recursively until there is enough space
                //TRACE_UNZIP("; output array not big enough (ain=%u)\n", zS->avail_in);

                strDest.append((const char*)bytesOut, OUTPUT_BUF_SIZE - zS.avail_out);

                continue;
            }
            else if ((err == Z_OK) && (zS.avail_in == 0))
            {
                //TRACE_UNZIP("; all input processed\n");
                // All available input has been processed, everything ok.
                // Set the size to the amount unzipped in this call (including all recursive calls)
                strDest.append((const char*)bytesOut, OUTPUT_BUF_SIZE - zS.avail_out);

                int kerr = ::inflateEnd(&zS);
                //不关心结果
                //if (err != Z_OK)
                //{
                //    //TRACE_UNZIP("; Error: end stream failed: %d\n", err);
                //    return false;
                //}

                break;
            }
            else
            {
                return false;
            }
        }
        catch (...)
        {
            return false;
        }
    } while (true);


    return err == Z_OK;
}