/**
 *  文件共享分享者一端的文件系统操作，ShareFileOps.h
 *
 *  分享者处理访问者的请求时，路径先经 SharePathGuard 检查、拼成本机绝对路径，再交本类访问文件系统。
 *  第 1 期第 12 步实现读类操作：列目录、查单个条目、判断路径上是否经过隐藏条目、判断共享根能否提供；
 *  写操作（新建、重命名、删除、移动）与搜索、缩略图随第 2 期加入（docs/文件共享方案.md 7.1、9.1）。
 *
 *  约定：
 *    - 所有文件系统调用都经 ShareToLongPath 加 \\?\ 前缀，共享根下超过 MAX_PATH 的深层路径照常可用；
 *    - 返回值一律是错误码：error_code_ok、error_code_share_path_not_found（809，不存在、不是目录、驱动器未就绪）
 *      或 error_code_share_io_fail（811，被占用、无系统权限等其余失败），可直接填进应答帧的 code；
 *    - 条目名与时间按控制通道的约定给出：名字为 UTF-8，修改时间为 Unix 毫秒，属性只保留只读、隐藏、系统三位。
 *
 *  本类不打日志，可在任意线程调用。单元测试见 FlamingoFileShare/Tests/ShareFileOpsTests.cpp（标签 [sharefileops]，
 *  在测试程序目录下建真实的文件与目录）。
 *
 *  典型用法（分享者处理 sf_list）：
 *      std::vector<ShareEntry> entries;
 *      int code = ShareFileOps::ListDirectory(absDir, share.m_includeHidden, entries);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "ShareControlFraming.h"   //ShareEntry
#include "../../Source/Feature/FolderTransfer/FolderManifest.h"   //FolderManifestFile：扫描结果直接用作传输清单的条目

namespace fileshare {

//回报给访问者的文件属性位：只读、隐藏、系统；其余属性（存档、压缩、重解析点等）对访问者没有意义，不外传
const DWORD kShareEntryAttrMask = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;

//共享未勾选「包含隐藏文件和系统文件」时，带这两种属性之一的条目对访问者不可见
const DWORD kShareHiddenAttrMask = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;

//递归扫描目录树时允许进入的最大层数（与文件夹传输 FolderScanOptions 的 64 层一致）：防止异常结构让扫描停不下来
const int kShareScanMaxDepth = 64;

/**
 *  正在上传的半成品文件登记表（进程内一份）：分享者列目录时据此把它们滤掉，即使共享勾选了包含隐藏文件，
 *  其他访问者也看不到（方案 7.6）。线程安全。
 */
class CShareUploadRegistry
{
public:
    /**
     *  取进程内唯一的登记表。
     */
    static CShareUploadRegistry& Instance();

    //登记一个半成品文件的完整路径（不区分大小写）
    void Add(const std::wstring& partPath);

    //去掉一个半成品文件
    void Remove(const std::wstring& partPath);

    //一个完整路径是否是正在上传的半成品
    bool Contains(const std::wstring& fullPath) const;

    //一个目录之下（任意深度）是否有正在上传的半成品；重命名、移动、删除这个目录会让上传在改正式名时失败，须拒绝
    bool HasAnyUnder(const std::wstring& dir) const;

private:
    CShareUploadRegistry()
    {
    }

    //统一成小写，作为比较的键
    static std::wstring Key(const std::wstring& path);

    mutable std::mutex     m_mutex;   //保护 m_paths
    std::set<std::wstring> m_paths;   //正在上传的半成品路径（小写）
};

/**
 *  ShareFileOps —— 分享者一端的文件系统操作；全部为静态方法，不可实例化。
 */
class ShareFileOps
{
public:
    /**
     *  列出一个目录下的全部条目（不含 . 与 ..）。结果先列目录、再列文件，同类按名字排序（不区分大小写的序数比较）。
     *  名字不能写成合法相对路径的条目（例如以点或空格结尾、借助 \\?\ 前缀才建得出来的名字）一并略去：访问者无法
     *  再用这样的名字发出请求，列出来只会让后续操作必然失败。正在上传的半成品（CShareUploadRegistry 里登记的）也不列出。
     *  @param absDir        目录的本机绝对路径（不带 \\?\ 前缀，盘符根目录写作 "X:\"）
     *  @param includeHidden 是否列出带隐藏或系统属性的条目
     *  @param out           [出参] 条目；失败时为空
     *  @return error_code_ok；目录不存在或路径指向文件时为 809；其余失败为 811
     */
    static int ListDirectory(const std::wstring& absDir, bool includeHidden, std::vector<ShareEntry>& out);

    /**
     *  查一个条目。
     *  @param absPath 条目的本机绝对路径（不带 \\?\ 前缀）
     *  @param out     [出参] 条目；名字取路径的最后一段，盘符根目录取 "X:"
     *  @return error_code_ok；不存在为 809；其余失败为 811
     */
    static int StatEntry(const std::wstring& absPath, ShareEntry& out);

    /**
     *  判断从共享根往下到 absPath 的各级（不含共享根本身，含 absPath 自身）是否有带隐藏或系统属性的。
     *  共享根本身不算：磁盘共享的根目录（如 "D:\"）天然带隐藏与系统属性。
     *  遇到不存在的一级即停止，按「没有」返回，条目是否存在由后续的列目录或查条目判定。
     *  @param root    共享根的本机路径
     *  @param absPath 共享根之下的本机绝对路径（ResolveUnderRoot 的结果）
     *  @return 有带隐藏或系统属性的一级时返回 true
     */
    static bool HasHiddenComponent(const std::wstring& root, const std::wstring& absPath);

    /**
     *  递归扫描一个目录（广度优先，父目录先于子目录），产出传输清单用的目录与文件列表，相对路径都以 prefix 开头
     *  （prefix + "/" + 下级名字）。跳过重解析点（目录联接、符号链接，可能指向共享根之外或形成环）；includeHidden
     *  为假时跳过带隐藏或系统属性的条目；名字写不成合法相对路径的条目跳过。结果追加在 dirs、files 末尾。
     *  @param absDir        目录的本机绝对路径
     *  @param prefix        相对路径前缀（通常是该目录自己的名字），不可为空
     *  @param includeHidden 是否包含隐藏与系统条目
     *  @param dirs          [入出参] 目录的相对路径
     *  @param files         [入出参] 文件条目（相对路径、字节数、修改时间）
     *  @return error_code_ok；目录不存在为 809；其余失败（无权限、层数过深）为 811
     */
    static int ScanTree(const std::wstring& absDir, const std::string& prefix, bool includeHidden,
                        std::vector<std::string>& dirs, std::vector<foldertransfer::FolderManifestFile>& files);

    /**
     *  判断共享根此刻能否提供：存在，且类型与共享类型相符（单文件共享必须是文件，其余必须是目录）。
     *  @param rootPath 共享根的本机路径
     *  @param kind     共享类型，取值见 share_kind
     *  @return 能提供返回 true
     */
    static bool IsRootAvailable(const std::wstring& rootPath, int kind);

    /**
     *  把 FILETIME（1601 年起的 100 纳秒数）换算成 Unix 毫秒。
     *  @param ft 文件时间
     *  @return Unix 毫秒；早于 1970-01-01 的时间返回 0
     */
    static int64_t FileTimeToUnixMs(const FILETIME& ft);

    /**
     *  把 Win32 错误码归成控制通道的错误码。
     *  @param err GetLastError() 的值
     *  @return 不存在类（文件、路径、网络路径不存在，名字非法，驱动器未就绪，不是目录）为 809，其余为 811
     */
    static int MapWin32Error(DWORD err);

private:
    ShareFileOps() = delete;
};

} // namespace fileshare
