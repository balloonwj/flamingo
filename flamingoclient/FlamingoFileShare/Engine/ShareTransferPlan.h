/**
 *  传输子会话的清单与落点规划，ShareTransferPlan.h
 *
 *  一次下载或上传就是把若干条目从一端传到另一端（docs/文件共享方案.md 5.4）。条目是用户在浏览窗口（下载）或本机
 *  （上传）同一个目录下选中的若干文件与目录；清单不带根文件夹名，顶层条目直接落在目标目录下：
 *    - 下载（分享者生成清单）：按 st_job 的相对路径逐个校验（写法、越界、重解析点、隐藏属性），文件直接成一项，目录
 *      递归扫描；读源文件时以条目所在的目录为根；
 *    - 上传（访问者生成清单）：按本机选中的条目生成，规则同上，不过滤隐藏文件（是用户自己选的）；
 *    - 下载的落点（访问者）：顶层名字在目标目录里已有同名的完整文件或目录时自动改名为「名字(2).扩展名」，
 *      与文件夹传输根目录重名时的做法一致；有续传记录的条目沿用原名接着下载；
 *    - 上传的落点（分享者）：顶层名字在目标目录里已存在时整个上传拒绝（807），不覆盖、不改名（方案 2.2、8.4）。
 *
 *  涉及文件系统的部分经 IShareNameProbe 判断名字是否已被占用，单元测试用假实现。
 *  单元测试见 FlamingoFileShare/Tests/ShareTransferPlanTests.cpp（标签 [sharetransfer]）。
 *
 *  典型用法（分享者处理下载的 st_job）：
 *      foldertransfer::FolderManifest manifest;
 *      std::wstring sourceRoot;
 *      int code = ShareTransferPlan::BuildDownloadManifest(share.m_rootPath, share.m_kind, share.m_includeHidden,
 *                                                          job.m_paths, manifest, sourceRoot);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../../Source/Feature/FolderTransfer/FolderManifest.h"
#include "../../Source/Feature/FolderTransfer/FolderRelayFraming.h"

namespace fileshare {

//给重名条目选新名字时最多尝试的序号（与文件夹传输 kFolderRecvMaxRenameAttempts 一致）
const int kShareRenameMaxAttempts = 1000;

//改名候选的第一个序号：原名不带后缀，带括号的候选从 2 起
const int kShareRenameFirstSuffix = 2;

/**
 *  判断一个本机路径是否已被占用（文件或目录都算）。真实实现查文件系统，单元测试用假实现。
 */
class IShareNameProbe
{
public:
    virtual ~IShareNameProbe()
    {
    }

    /**
     *  @param fullPath 本机完整路径
     *  @return 已存在返回 true
     */
    virtual bool Exists(const std::wstring& fullPath) = 0;
};

/**
 *  查文件系统的 IShareNameProbe 实现（经长路径前缀访问）。
 */
class CShareDiskProbe : public IShareNameProbe
{
public:
    virtual bool Exists(const std::wstring& fullPath) override;
};

/**
 *  ShareTransferPlan —— 传输子会话的清单与落点规划；全部为静态方法，不可实例化。
 */
class ShareTransferPlan
{
public:
    /**
     *  下载（分享者一端）：按 st_job 的条目生成清单。条目须是同一个目录下的条目；共享根本身不能作为条目，
     *  单文件共享除外（条目为空串，即共享的那个文件）。
     *  @param shareRoot     共享根的本机路径
     *  @param kind          共享类型
     *  @param includeHidden 共享是否包含隐藏与系统文件
     *  @param items         条目的相对路径（访问者送来的，不可信）
     *  @param manifest      [出参] 清单（根文件夹名为空，相对路径以各条目的名字开头）
     *  @param sourceRoot    [出参] 读源文件时与相对路径拼接的本机目录（条目所在的目录）
     *  @return error_code_ok；条目为空或不在同一目录、写法非法、越界为 808；不存在或经过隐藏条目为 809；
     *          单文件共享的条目不是空串为 820；扫描失败为 811
     */
    static int BuildDownloadManifest(const std::wstring& shareRoot, int kind, bool includeHidden,
                                     const std::vector<std::string>& items, foldertransfer::FolderManifest& manifest,
                                     std::wstring& sourceRoot);

    /**
     *  上传（访问者一端）：按本机选中的条目生成清单。条目须在同一个本机目录下。
     *  @param localItems 条目的本机完整路径
     *  @param manifest   [出参] 清单（根文件夹名为空）
     *  @param sourceRoot [出参] 条目所在的本机目录
     *  @return error_code_ok；条目为空或不在同一目录为 808；条目不存在为 809；扫描失败为 811
     */
    static int BuildUploadManifest(const std::vector<std::wstring>& localItems, foldertransfer::FolderManifest& manifest,
                                   std::wstring& sourceRoot);

    /**
     *  清单的顶层名字：各条相对路径的第一段，去重，保持第一次出现的顺序。
     */
    static std::vector<std::string> TopLevelNames(const std::vector<std::string>& dirs,
                                                  const std::vector<foldertransfer::FolderRelayFileEntry>& files);

    /**
     *  把相对路径的第一段由 from 换成 to；第一段不是 from 时原样返回。
     */
    static std::string ReplaceTopLevel(const std::string& relPath, const std::string& from, const std::string& to);

    /**
     *  在 dir 下为 name 选一个没有被占用的名字：先试原名，再试「名字(2).扩展名」「名字(3).扩展名」……；
     *  目录不拆扩展名（「名字(2)」）。
     *  @param dir   目录的本机完整路径
     *  @param name  原名
     *  @param isDir 是否为目录
     *  @param probe 名字占用的判断
     *  @param out   [出参] 选中的名字（不含目录）
     *  @return 找到返回 true；试满 kShareRenameMaxAttempts 次仍被占用返回 false
     */
    static bool ChooseFreeName(const std::wstring& dir, const std::wstring& name, bool isDir, IShareNameProbe& probe,
                               std::wstring& out);

    /**
     *  上传的顶层同名检查。
     *  @param targetDir 上传的目标目录（本机完整路径）
     *  @param topNames  清单的顶层名字
     *  @param probe     名字占用的判断
     *  @return 第一个已存在的顶层名字；都不存在时返回空串
     */
    static std::string FindTopLevelConflict(const std::wstring& targetDir, const std::vector<std::string>& topNames,
                                            IShareNameProbe& probe);

private:
    ShareTransferPlan() = delete;
};

} // namespace fileshare
