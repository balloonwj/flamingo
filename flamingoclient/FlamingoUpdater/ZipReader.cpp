/**
 *  zip 容器格式解析的实现，ZipReader.cpp
 *  balloonwj@qq.com   2026-08-12
 */
#include "ZipReader.h"

#include <windows.h>   // MultiByteToWideChar（条目名转宽字符）

#include "ZipPathSafety.h"   // IsDirectoryEntry（目录项的判定只写一处）
#include "../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace iupdate {

namespace {

//—— zip 规范里的几个签名与固定长度。数值出处：APPNOTE.TXT 4.3 节 ——

//中央目录结束记录（End of Central Directory）的签名 "PK\5\6"。
const uint32_t kSigEndOfCentralDir = 0x06054b50;
//中央目录条目（Central File Header）的签名 "PK\1\2"。
const uint32_t kSigCentralHeader   = 0x02014b50;
//本地文件头（Local File Header）的签名 "PK\3\4"。
const uint32_t kSigLocalHeader     = 0x04034b50;
//zip64 中央目录结束定位记录的签名 "PK\6\7"。见到它说明是 zip64，本更新器不支持。
const uint32_t kSigZip64Locator    = 0x07064b50;

//中央目录结束记录的固定部分长度（不含尾部注释）。
const size_t kEocdFixedSize    = 22;
//中央目录条目的固定部分长度（不含名字 / 扩展字段 / 注释）。
const size_t kCentralFixedSize = 46;
//本地文件头的固定部分长度（不含名字 / 扩展字段）。
const size_t kLocalFixedSize   = 30;

//zip 尾部注释最长 65535 字节，故从文件末尾往前找结束记录时，最多回溯这么多。
const size_t kMaxCommentSize   = 65535;

//通用位标记里的两个位。bit 0 置位表示条目加密；bit 11 置位表示条目名是 UTF-8。
const uint16_t kFlagEncrypted  = 0x0001;
const uint16_t kFlagUtf8Name   = 0x0800;

//zip64 在 32 位字段里用全 1 表示"真实值在扩展字段里"。见到它就是 zip64。
const uint32_t kZip64Marker    = 0xFFFFFFFF;

//从小端字节序读取 16 / 32 位无符号数。zip 的所有多字节字段都是小端。
uint16_t ReadU16(const unsigned char* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t ReadU32(const unsigned char* p)
{
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

/**
 *  把条目名从窄字节转成宽字符。
 *    bytes / len： 原始字节
 *    isUtf8：      通用位标记 bit 11 是否置位
 *  @return 转换后的宽字符串；转换失败返回空串。
 *
 *  按 zip 规范，bit 11 置位时名字是 UTF-8，否则是本地代码页（历史包袱）。打包侧统一
 *  带 UTF-8 标记就不会出现中文文件名乱码。
 */
std::wstring DecodeEntryName(const unsigned char* bytes, size_t len, bool isUtf8)
{
    if (len == 0)
    {
        return std::wstring();
    }

    const UINT codePage = isUtf8 ? CP_UTF8 : CP_ACP;
    const int needed = ::MultiByteToWideChar(codePage, 0, (LPCSTR)bytes, (int)len, NULL, 0);
    if (needed <= 0)
    {
        return std::wstring();
    }

    std::wstring result;
    result.resize((size_t)needed);
    const int written = ::MultiByteToWideChar(codePage, 0, (LPCSTR)bytes, (int)len,
                                              &result[0], needed);
    if (written <= 0)
    {
        return std::wstring();
    }
    result.resize((size_t)written);
    return result;
}

/**
 *  从文件末尾往前找中央目录结束记录。
 *    data / size： 整个 zip 字节流
 *    eocdOffset：  [出参] 结束记录的起始偏移
 *  @return 找到返回 true。
 *
 *  必须从后往前找而不是从前往后：zip 允许在末尾带注释，结束记录的位置不固定；
 *  而且自解压包前面还会挂一段可执行体。
 */
bool FindEndOfCentralDir(const unsigned char* data, size_t size, size_t& eocdOffset)
{
    if (size < kEocdFixedSize)
    {
        return false;
    }

    //回溯范围：固定部分 + 注释最大长度，但不超过整个文件
    size_t maxBack = kEocdFixedSize + kMaxCommentSize;
    if (maxBack > size)
    {
        maxBack = size;
    }

    const size_t lowest = size - maxBack;
    //从最靠后的可能位置往前逐字节比签名
    for (size_t i = size - kEocdFixedSize + 1; i-- > lowest; )
    {
        if (ReadU32(data + i) == kSigEndOfCentralDir)
        {
            eocdOffset = i;
            return true;
        }
    }

    return false;
}

} // namespace

bool ParseCentralDirectory(const unsigned char* data, size_t size,
                           std::vector<ZipEntry>& out, std::wstring& errmsg)
{
    out.clear();
    errmsg.clear();

    if (data == NULL || size == 0)
    {
        errmsg = TR(L"升级包内容为空。");
        return false;
    }

    size_t eocd = 0;
    if (!FindEndOfCentralDir(data, size, eocd))
    {
        errmsg = TR(L"找不到 zip 中央目录结束记录，文件可能被截断或不是 zip 包。");
        return false;
    }

    //结束记录布局：签名(4) 本卷号(2) 中央目录起始卷号(2) 本卷条目数(2)
    //              总条目数(2) 中央目录字节数(4) 中央目录偏移(4) 注释长度(2)
    const uint16_t totalEntries = ReadU16(data + eocd + 10);
    const uint32_t cdSize       = ReadU32(data + eocd + 12);
    const uint32_t cdOffset     = ReadU32(data + eocd + 16);

    //zip64 的标志：这几个字段被写成全 1，真实值放在 zip64 扩展记录里。本更新器不支持。
    if (cdSize == kZip64Marker || cdOffset == kZip64Marker || totalEntries == 0xFFFF)
    {
        errmsg = TR(L"该升级包是 zip64 格式，更新器不支持。");
        return false;
    }

    //顺带看一眼有没有 zip64 定位记录，有的话同样不支持
    if (eocd >= 20 && ReadU32(data + eocd - 20) == kSigZip64Locator)
    {
        errmsg = TR(L"该升级包是 zip64 格式，更新器不支持。");
        return false;
    }

    //空包：合法的 zip，只是一条条目都没有。格式没问题，如实返回空列表。
    if (totalEntries == 0)
    {
        return true;
    }

    //中央目录必须整体落在文件内
    if ((size_t)cdOffset > size || (size_t)cdSize > size - (size_t)cdOffset)
    {
        errmsg = TR(L"zip 中央目录位置越界，文件已损坏。");
        return false;
    }

    size_t pos = (size_t)cdOffset;
    const size_t cdEnd = (size_t)cdOffset + (size_t)cdSize;

    for (uint16_t n = 0; n < totalEntries; ++n)
    {
        if (pos + kCentralFixedSize > cdEnd)
        {
            errmsg = TR(L"zip 中央目录被截断。");
            return false;
        }
        if (ReadU32(data + pos) != kSigCentralHeader)
        {
            errmsg = TR(L"zip 中央目录条目签名不正确，文件已损坏。");
            return false;
        }

        //中央目录条目布局（偏移量相对条目起点）：
        //  4 签名  6 通用位标记  10 压缩方法  16 CRC32  20 压缩后大小
        //  24 解压后大小  28 名字长度  30 扩展字段长度  32 注释长度
        //  42 本地文件头偏移
        const uint16_t flags       = ReadU16(data + pos + 8);
        const uint16_t method      = ReadU16(data + pos + 10);
        const uint32_t crc         = ReadU32(data + pos + 16);
        const uint32_t compSize    = ReadU32(data + pos + 20);
        const uint32_t uncompSize  = ReadU32(data + pos + 24);
        const uint16_t nameLen     = ReadU16(data + pos + 28);
        const uint16_t extraLen    = ReadU16(data + pos + 30);
        const uint16_t commentLen  = ReadU16(data + pos + 32);
        const uint32_t localOffset = ReadU32(data + pos + 42);

        if ((flags & kFlagEncrypted) != 0)
        {
            errmsg = TR(L"升级包中含有加密条目，更新器不支持。");
            return false;
        }
        if (method != zip_method_store && method != zip_method_deflate)
        {
            errmsg = TR(L"升级包中含有不支持的压缩方法。");
            return false;
        }
        if (compSize == kZip64Marker || uncompSize == kZip64Marker
            || localOffset == kZip64Marker)
        {
            errmsg = TR(L"该升级包是 zip64 格式，更新器不支持。");
            return false;
        }

        const size_t varStart = pos + kCentralFixedSize;
        const size_t varLen = (size_t)nameLen + (size_t)extraLen + (size_t)commentLen;
        if (varStart + varLen > cdEnd)
        {
            errmsg = TR(L"zip 中央目录条目被截断。");
            return false;
        }

        ZipEntry entry;
        entry.m_name = DecodeEntryName(data + varStart, nameLen,
                                       (flags & kFlagUtf8Name) != 0);
        if (entry.m_name.empty())
        {
            errmsg = TR(L"zip 条目名为空或无法解码。");
            return false;
        }

        entry.m_method            = method;
        entry.m_crc32             = crc;
        entry.m_compressedSize    = compSize;
        entry.m_uncompressedSize  = uncompSize;
        entry.m_localHeaderOffset = localOffset;
        entry.m_isDirectory       = IsDirectoryEntry(entry.m_name);

        out.push_back(entry);
        pos = varStart + varLen;
    }

    return true;
}

bool LocateEntryData(const unsigned char* data, size_t size,
                     const ZipEntry& entry, size_t& dataOffset, std::wstring& errmsg)
{
    dataOffset = 0;
    errmsg.clear();

    if (data == NULL || size == 0)
    {
        errmsg = TR(L"升级包内容为空。");
        return false;
    }

    const size_t local = (size_t)entry.m_localHeaderOffset;
    if (local + kLocalFixedSize > size)
    {
        errmsg = TR(L"zip 本地文件头位置越界。");
        return false;
    }
    if (ReadU32(data + local) != kSigLocalHeader)
    {
        errmsg = TR(L"zip 本地文件头签名不正确。");
        return false;
    }

    //本地文件头里的名字长度与扩展字段长度，与中央目录里的可以不同，必须从这里读。
    const uint16_t nameLen  = ReadU16(data + local + 26);
    const uint16_t extraLen = ReadU16(data + local + 28);

    const size_t start = local + kLocalFixedSize + (size_t)nameLen + (size_t)extraLen;
    if (start > size || (size_t)entry.m_compressedSize > size - start)
    {
        errmsg = TR(L"zip 条目数据范围越界，文件已损坏。");
        return false;
    }

    dataOffset = start;
    return true;
}

} // namespace iupdate
