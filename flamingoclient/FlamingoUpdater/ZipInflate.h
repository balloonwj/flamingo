/**
 *  zip 条目内容的解压，ZipInflate.h
 *
 *  从 ZipReader 拆出来单独成文件，只因为它依赖 zlib：这样 ZipReader 保持零依赖、
 *  能直接编进 FlamingoUnit 测试工程，而本文件不必进测试工程。
 *
 *  用的是仓库里已有的 zlib 1.2.11（`flamingoclient/Source/zlib1.2.11/`），主工程本来
 *  就在编译它。只需要其中的 inflate 一路：adler32 / crc32 / inffast / inflate /
 *  inftrees / zutil 六个 .c。不再引入老工程那份内嵌了一整套 zlib 的 unzip.cpp。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace iupdate {

/**
 *  把一段 raw deflate 数据解开。
 *
 *    src：            压缩数据起点
 *    srcLen：         压缩数据字节数
 *    expectedSize：   期望的解压后字节数（取自 zip 条目元信息）
 *    out：            [出参] 解压结果；失败时清空
 *    errmsg：         [出参] 失败原因（中文）
 *
 *  @return 解压成功且长度与 expectedSize 一致时返回 true。
 *
 *  这里用的是 <u>raw deflate</u>（inflateInit2 传 -MAX_WBITS）：zip 里存的是不带
 *  zlib 头尾的裸 deflate 流，按普通 zlib 流去解会在第一步就报数据错。
 *
 *  长度对不上一律判失败而不是"能解多少算多少"：升级包解出来短一截的文件写进安装
 *  目录，比解压失败更糟——程序会以一种难以排查的方式坏掉。
 */
bool InflateRaw(const unsigned char* src, size_t srcLen, size_t expectedSize,
                std::vector<unsigned char>& out, std::wstring& errmsg);

/**
 *  计算一段数据的 CRC-32，用于与 zip 条目里记录的校验值比对。
 *
 *    data：  数据起点；len 为 0 时可传 NULL
 *    len：   字节数
 *
 *  @return CRC-32 值。空数据返回 0，与 zip 规范一致。
 */
uint32_t ComputeCrc32(const unsigned char* data, size_t len);

} // namespace iupdate
