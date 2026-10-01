/**
 *  压缩工具类：对 zlib 的 compress / uncompress 做一层封装，供 v2 帧包体的压缩与解压使用。
 *
 *  典型用法（见 net/FramePayload.cpp）：
 *      std::string compressed;
 *      ZlibUtil::CompressBuf(plain, compressed);                      //发送侧压缩
 *      ZlibUtil::UncompressBuf(compressed, plain, originlen);         //接收侧按帧头 originlen 解压
 *  单元测试见 Tests/ZlibUtilTests.cpp。
 *  balloonwj@qq.com   2018-03-09
 */
#ifndef __ZLIB_UTIL_H__
#define __ZLIB_UTIL_H__
#include <string>

//压缩工具类，只有静态方法，不允许实例化
class ZlibUtil
{
private:
    ZlibUtil() = delete;
    ~ZlibUtil() = delete;
    ZlibUtil(const ZlibUtil& rhs) = delete;

public:
    /**
     *  压缩一段数据，结果追加到 strDestBuf 末尾。
     *  @param strSrcBuf  待压缩的数据，长度须在 1 字节到 10MB 之间
     *  @param strDestBuf [出参] 压缩结果追加到它的末尾；失败时不改动
     *  @return 成功返回 true；输入为空、超过 10MB 上限或 zlib 报错时返回 false
     */
    static bool CompressBuf(const std::string& strSrcBuf, std::string& strDestBuf);

    /**
     *  解压一段 zlib 数据，结果追加到 strDestBuf 末尾。
     *  @param strSrcBuf      待解压的数据
     *  @param strDestBuf     [出参] 解压结果追加到它的末尾；失败时不改动
     *  @param nDestBufLength 解压后的长度上限（字节），通常取帧头的 originlen。按它分配缓冲，
     *                        调用方须保证它已经过上限校验（帧路径上由 decodeFrame 校验）
     *  @return 成功返回 true；数据损坏、给出的长度不足以容纳解压结果时返回 false
     */
    static bool UncompressBuf(const std::string& strSrcBuf, std::string& strDestBuf, size_t nDestBufLength);
};



#endif //!__ZLIB_UTIL_H__