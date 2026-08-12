/**
 *  zip 容器格式解析（只读、纯逻辑），ZipReader.h
 *
 *  只做"读懂 zip 的目录结构"这一件事：找到中央目录、逐条读出条目元信息、以及定位
 *  某个条目的压缩数据在文件中的偏移。<u>不含解压</u> —— 真正的 inflate 在
 *  ZipInflate.{h,cpp} 里，那部分依赖 zlib。这么分是为了让本文件不依赖任何第三方库，
 *  能直接进 FlamingoUnit 测试工程（Suite ZIP）。
 *
 *  之所以自己写而不是继续用老工程里那份 4236 行的 Wischik unzip：解压所需的容器格式
 *  只有中央目录 + 本地文件头两块，两百行足够，逻辑直白且好单测；而那份老代码还内嵌了
 *  一份 zlib，与仓库里已有的 zlib 1.2.11 重复。
 *
 *  只支持解压所必需的最小子集：
 *    · 存储（method 0）与 deflate（method 8）两种压缩方法；
 *    · 不支持 zip64、加密、跨卷分卷 —— 遇到一律如实报错，不做半吊子处理。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace iupdate {

//zip 支持的压缩方法。只认这两种，其余一律报错。
enum ZipMethod
{
    //存储：数据原样存放，不压缩。小文件常见。
    zip_method_store   = 0,
    //deflate：zip 的默认压缩方法，用 zlib 的 raw inflate 解开。
    zip_method_deflate = 8,
};

//中央目录里的一条条目。
struct ZipEntry
{
    //条目名，已按通用位标记 bit 11 决定的编码（UTF-8 或本地代码页）转成宽字符。
    //注意这里是<u>原始</u>名字，尚未做安全校验；落盘前必须先过 NormalizeZipEntryName。
    std::wstring m_name;
    //压缩方法，取值见 ZipMethod。
    uint16_t     m_method;
    //压缩后字节数。
    uint32_t     m_compressedSize;
    //解压后字节数。用于落盘前预估空间、以及解压后核对长度。
    uint32_t     m_uncompressedSize;
    //条目内容的 CRC-32。解压后据它校验，能发现传输或解压过程中的损坏。
    uint32_t     m_crc32;
    //本地文件头在整个 zip 文件中的偏移。压缩数据紧跟在本地文件头之后。
    uint32_t     m_localHeaderOffset;
    //是否是目录项（条目名以分隔符结尾）。目录项没有内容，只需建目录。
    bool         m_isDirectory;

    ZipEntry()
        : m_method(0), m_compressedSize(0), m_uncompressedSize(0)
        , m_crc32(0), m_localHeaderOffset(0), m_isDirectory(false)
    {
    }
};

/**
 *  解析整个 zip 的中央目录。
 *
 *    data：   zip 文件的完整字节流；size 为 0 时可传 NULL
 *    size：   字节数
 *    out：    [出参] 解析出的条目列表，顺序与中央目录中一致；失败时清空
 *    errmsg： [出参] 失败原因（中文）
 *
 *  @return 成功返回 true。以下情形返回 false：字节流为空、找不到中央目录结束记录、
 *          中央目录偏移或长度越界、条目签名不对、遇到 zip64 或加密条目。
 *
 *  空包（一条条目都没有的合法 zip）返回 true 且 out 为空 —— 它格式上没问题，
 *  "包里没有内容"该由调用方按业务判断，不是解析错误。
 */
bool ParseCentralDirectory(const unsigned char* data, size_t size,
                           std::vector<ZipEntry>& out, std::wstring& errmsg);

/**
 *  定位一个条目的压缩数据在文件中的位置。
 *
 *  中央目录里记的是<u>本地文件头</u>的偏移，而本地文件头里的名字长度、扩展字段长度
 *  与中央目录中的可以不同（zip 规范允许），所以必须真去读一次本地文件头才能算出
 *  数据的起点，不能拿中央目录里的长度去推。
 *
 *    data：       zip 文件的完整字节流
 *    size：       字节数
 *    entry：      要定位的条目（来自 ParseCentralDirectory）
 *    dataOffset： [出参] 压缩数据在 data 中的起始偏移
 *    errmsg：     [出参] 失败原因（中文）
 *
 *  @return 成功返回 true。本地文件头签名不对、或按长度算出的数据范围越界时返回 false。
 */
bool LocateEntryData(const unsigned char* data, size_t size,
                     const ZipEntry& entry, size_t& dataOffset, std::wstring& errmsg);

} // namespace iupdate
