/**
 *  升级包的落盘安装（含备份与回滚），Installer.h
 *
 *  老版本的做法是"边解边写，某个文件失败就置个错误标志继续解下一个"，中途出错
 *  客户端就成了新旧文件混在一起的半个程序，而且没有任何回滚手段。本类按两条原则重写：
 *
 *  <u>一、两遍法。</u>第一遍只校验不落盘：条目名是否安全、目标文件能否写入、磁盘空间
 *  是否够。全部通过才进第二遍真正写。这样绝大多数可预见的失败都发生在动手之前。
 *
 *  <u>二、写之前先备份，失败即回滚。</u>每个将被覆盖的旧文件先移动到
 *  `<安装目录>\update\backup\`，任何一步失败就把已经移走的文件全部搬回来。宁可升级
 *  不成功，也不能把客户端弄成起不来的状态 —— 这是硬要求。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace iupdate {

/**
 *  安装器。一次 Install 调用完成"解包 → 校验 → 备份 → 覆盖"的全过程。
 *  非线程安全，按单线程顺序使用。
 */
class Installer
{
public:
    Installer();
    ~Installer();

    /**
     *  把升级包安装到目标目录。
     *
     *    zipPath：   升级包完整路径
     *    targetDir： 安装目录（会被覆盖的那个目录）
     *    errmsg：    [出参] 失败原因（中文，可直接提示给用户）
     *
     *  @return 全部文件写入成功返回 true。
     *          返回 false 时<u>已保证回滚完毕</u>：所有被移走的旧文件都已搬回原位，
     *          安装目录处于升级前的状态。
     *
     *  过程中的每一步都会写进 UpdateLog。
     */
    bool Install(const std::wstring& zipPath, const std::wstring& targetDir,
                 std::wstring& errmsg);

private:
    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;

    //把整个文件读进内存。升级包通常几十 MB，一次读入省掉随机寻址的麻烦。
    //  path：文件路径；out：[出参] 文件内容；errmsg：[出参] 失败原因。
    bool ReadWholeFile(const std::wstring& path, std::vector<unsigned char>& out,
                       std::wstring& errmsg);

    //第一遍：只校验不落盘。逐条检查条目名安全性与目标文件可写性，并累计所需空间。
    //  失败时 errmsg 说明是哪一条出的问题。
    bool ValidateEntries(const std::vector<unsigned char>& zip, const std::wstring& targetDir,
                         std::wstring& errmsg);

    //第二遍：真正解压落盘。写之前先备份旧文件。
    bool ApplyEntries(const std::vector<unsigned char>& zip, const std::wstring& targetDir,
                      std::wstring& errmsg);

    //把一个已存在的目标文件移动到备份目录，并记入 m_backups 供回滚使用。
    //  fullPath：目标文件完整路径；relPath：它相对安装目录的路径（用于在备份目录里还原层级）。
    bool BackupExistingFile(const std::wstring& fullPath, const std::wstring& relPath,
                            std::wstring& errmsg);

    //把已经备份走的文件全部搬回原位。回滚本身失败只写日志，不再向上报错——
    //此时已经没有更好的补救手段，如实留痕比中断更有价值。
    void Rollback();

    //确保一个目录（含各级父目录）存在。
    //  @return 目录已存在或创建成功返回 true。
    bool EnsureDirectory(const std::wstring& dir);

    //确保某个文件路径的父目录存在。
    bool EnsureParentDirectory(const std::wstring& filePath);

private:
    //备份目录：<安装目录>\update\backup。每次 Install 开始时清空重建。
    std::wstring m_backupDir;

    //已备份的文件清单：first = 原始完整路径，second = 备份后的完整路径。
    //回滚时按相反顺序搬回。
    std::vector<std::pair<std::wstring, std::wstring> > m_backups;
};

} // namespace iupdate
