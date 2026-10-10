/**
 *  导出聊天记录时打包 zip 的写入器，HistoryZipWriter.h
 *
 *  聊天记录管理器的「导出」可以把网页 / 文本与图片、文件一起打成一个 zip（docs/聊天记录管理器方案.md §12.7）。
 *  本类用程序自带的 zlib（third-party/zlib1.2.11）写标准 zip：每个条目先写本地文件头，再流式写 deflate 压缩后的
 *  数据（大文件分块读，不整个读进内存），写完回到本地文件头补上 CRC-32 与长短，最后写中央目录。条目名按 UTF-8
 *  编码并置通用位标记 bit 11，Windows 资源管理器与常见解压工具都能正确显示中文名。
 *
 *  不写 ZIP64：任一条目或整个文件超过 4 GB、条目超过 65535 个时这次写入失败（TooLarge() 为 true），调用方提示用户
 *  改为导出到文件夹。不加密、不分卷。
 *
 *  解压一侧（FlamingoUpdater/ZipReader）读得懂本类写出的文件，单测据此做往返校验（Tests/HistoryExportTests.cpp，
 *  标签 [histexport]）。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  典型用法：
 *      CHistoryZipWriter zip;
 *      if (zip.Open(L"D:\\out.zip"))
 *      {
 *          zip.AddData("产品研发群/聊天记录.html", html);
 *          zip.AddFile("产品研发群/聊天记录_files/1.png", L"C:\\cache\\abc");
 *          zip.Close();
 *      }
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace balloonhistory {

/**
 *  CHistoryZipWriter —— 顺序写入一个 zip 文件。同一个对象只能写一个文件；任一步失败后后续调用都返回 false，
 *  Close 仍会关闭文件句柄，调用方据 Failed() 决定要不要删掉写了一半的文件。
 */
class CHistoryZipWriter
{
public:
    CHistoryZipWriter();

    //析构时若还没 Close 就关闭文件（不写中央目录，留下的是不完整的 zip）。
    ~CHistoryZipWriter();

    /**
     *  新建（覆盖）zip 文件。
     *  @param zipPath  zip 文件完整路径；所在目录须已存在
     *  @return 打开成功返回 true
     */
    bool Open(const std::wstring& zipPath);

    /**
     *  添加一个条目，内容取自内存。
     *  @param nameUtf8  条目在 zip 里的路径（UTF-8，用 '/' 分隔，不以 '/' 开头）
     *  @param data      内容（任意字节）
     *  @return 写入成功返回 true
     */
    bool AddData(const std::string& nameUtf8, const std::string& data);

    /**
     *  添加一个条目，内容取自磁盘文件（分块读取、边读边压缩）。
     *  @param nameUtf8    条目在 zip 里的路径（UTF-8）
     *  @param sourcePath  源文件完整路径
     *  @return 写入成功返回 true；源文件打不开、读取出错或超过 4 GB 时返回 false
     */
    bool AddFile(const std::string& nameUtf8, const std::wstring& sourcePath);

    /**
     *  写中央目录并关闭文件。
     *  @return 之前各步都成功、中央目录也写成功时返回 true
     */
    bool Close();

    //是否有任一步失败过
    bool Failed() const;

    //失败是不是因为超出了不写 ZIP64 时的上限（4 GB、65535 个条目）
    bool TooLarge() const;

    //已写入的条目个数
    int EntryCount() const;

    //目前为止写入 zip 文件的字节数
    uint64_t BytesWritten() const;

private:
    //中央目录里一个条目要记的信息
    struct Entry
    {
        std::string m_name;            //条目名（UTF-8）
        uint32_t    m_crc;             //内容的 CRC-32
        uint32_t    m_compressed;      //压缩后字节数
        uint32_t    m_uncompressed;    //原始字节数
        uint32_t    m_offset;          //本地文件头在 zip 里的偏移
        uint16_t    m_dosTime;         //修改时间（DOS 格式）
        uint16_t    m_dosDate;         //修改日期（DOS 格式）
    };

    //写一个条目：先写本地文件头，再由 reader 回调分块提供原始数据边读边压缩，最后回填 CRC 与长短
    bool WriteEntry(const std::string& nameUtf8, FILE* source, const std::string* memory);

    //把一段字节写进 zip，累计已写字节数；失败时置失败标志
    bool WriteRaw(const void* data, size_t len);

    //检查一个偏移或长度是否超过不写 ZIP64 时的上限；超过时置失败与超限标志
    bool CheckLimit(uint64_t value);

    FILE*              m_file;       //zip 文件句柄；NULL 表示未打开或已关闭
    std::vector<Entry> m_entries;    //已写入的条目（写中央目录用）
    uint64_t           m_written;    //已写入的字节数（即下一个本地文件头的偏移）
    bool               m_failed;     //是否有任一步失败过
    bool               m_tooLarge;   //失败是否因为超出上限
};

} // namespace balloonhistory
