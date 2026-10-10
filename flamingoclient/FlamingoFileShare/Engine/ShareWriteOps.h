/**
 *  文件共享分享者一端的四种写操作：新建文件夹、重命名、删除（放进回收站）、移动，ShareWriteOps.h
 *
 *  分享者收到 sf_mkdir / sf_rename / sf_delete / sf_move 并通过权限检查之后，交给本类执行（docs/文件共享方案.md 8.5）。
 *  每个路径都先按 9.1 的顺序校验：写法与拼接（808）→ 共享根是否存在（809）→ 重解析点（808）→ 隐藏条目（809），
 *  正在上传的半成品按不存在处理（809）。各操作的规则：
 *    - 新建文件夹：父目录必须存在；同名的文件或文件夹已存在时拒绝（807），一律不覆盖；
 *    - 重命名：共享根本身不能改名（808）；新名字只能是一段名字（不含 '/' 与 '\'），写法按相对路径规则检查；
 *      同目录已有同名条目时拒绝（807），只改大小写时照常执行；
 *    - 删除：共享根本身不能删（808）；先按全部条目的总字节数判定能否放进回收站（见 ShareRecycle.h），
 *      不能则全部回 810、一个也不删；能放的逐条放进回收站，逐条给出结果；
 *    - 移动：目标目录必须存在；不能移到自己或自己的下级里（808）；目标位置已有同名条目时拒绝（807）；
 *      已经在目标目录里的条目视为成功、不做任何事；
 *    - 目录之下有正在上传的半成品时，重命名、移动、删除这个目录都拒绝（811，被占用）：否则上传改正式名时会失败。
 *  全部方法只碰文件系统、不打日志、不发网络，可在任意线程调用；同一个共享上的写操作由调用方串行执行。
 *
 *  单元测试见 FlamingoFileShare/Tests/ShareWriteOpsTests.cpp（标签 [sharewriteops]，在测试程序目录下建真实的文件与
 *  目录，删除用假回收站，不往真实的回收站里放东西）。
 *
 *  典型用法（分享者处理 sf_rename）：
 *      ShareWriteContext ctx;
 *      ctx.m_rootPath = share.m_rootPath;
 *      ...
 *      std::string newRel;
 *      int code = ShareWriteOps::Rename(ctx, req.m_path, req.m_newName, newRel);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "ShareControlFraming.h"   //ShareOpItem
#include "ShareRecycle.h"

namespace fileshare {

//计算目录总字节数时允许进入的最大层数，与 kShareScanMaxDepth 一致；超过时按「算不出」处理，删除因此被拒（810）
const int kShareWriteSizeMaxDepth = 64;

//写操作的上下文：一个共享在分享者一端的根与选项，以及删除用的回收站
struct ShareWriteContext
{
    std::wstring    m_rootPath;       //共享根的本机路径
    int             m_kind;           //共享类型，share_kind；单文件共享不支持任何写操作
    bool            m_includeHidden;  //共享是否包含隐藏文件与系统文件
    IShareRecycler* m_recycler;       //删除用的回收站（不拥有；只做删除时才用到，其余操作可为 NULL）

    ShareWriteContext()
        : m_kind(0)
        , m_includeHidden(false)
        , m_recycler(NULL)
    {
    }
};

/**
 *  ShareWriteOps —— 分享者一端的写操作；全部为静态方法，不可实例化。
 *  返回值一律是错误码，可直接填进 sf_op_result：error_code_ok、807 同名、808 路径非法、809 不存在、
 *  810 不能放进回收站、811 读写失败（被占用、无权限等）、812 磁盘空间不足、820 单文件共享不支持。
 */
class ShareWriteOps
{
public:
    /**
     *  把访问者给出的相对路径解析为本机绝对路径，校验顺序见文件头。与列目录用的是同一套规则。
     *  @param ctx     写操作上下文
     *  @param relPath 相对共享根的路径（UTF-8，'/' 分隔；空串为共享根）
     *  @param absPath [出参] 本机绝对路径
     *  @return error_code_ok、808 或 809
     */
    static int ResolvePath(const ShareWriteContext& ctx, const std::string& relPath, std::wstring& absPath);

    /**
     *  新建文件夹。
     *  @param ctx     写操作上下文
     *  @param relPath 新文件夹的相对路径（含它自己的名字）；不能为空
     *  @return error_code_ok；父目录不存在为 809；同名已存在为 807；其余见类注释
     */
    static int MakeDirectory(const ShareWriteContext& ctx, const std::string& relPath);

    /**
     *  重命名一个文件或文件夹。
     *  @param ctx        写操作上下文
     *  @param relPath    条目的相对路径；不能为空（共享根本身不能改名）
     *  @param newName    新名字（UTF-8，只是一段名字）
     *  @param newRelPath [出参] 改名后的相对路径（审计与界面刷新用）；失败时为空串
     *  @return error_code_ok；见类注释
     */
    static int Rename(const ShareWriteContext& ctx, const std::string& relPath, const std::string& newName,
                      std::string& newRelPath);

    /**
     *  删除若干条目（放进回收站）。
     *  @param ctx      写操作上下文；m_recycler 不可为 NULL
     *  @param relPaths 条目的相对路径
     *  @param results  [出参] 逐条结果，顺序与 relPaths 一致（调用前清空）
     *  @return 全部成功为 error_code_ok；否则为第一条失败的错误码
     */
    static int Delete(const ShareWriteContext& ctx, const std::vector<std::string>& relPaths,
                      std::vector<ShareOpItem>& results);

    /**
     *  把若干条目移到共享内的另一个目录。
     *  @param ctx      写操作上下文
     *  @param relPaths 条目的相对路径
     *  @param toDir    目标目录的相对路径（空串为共享根）
     *  @param results  [出参] 逐条结果，顺序与 relPaths 一致（调用前清空）
     *  @return 全部成功为 error_code_ok；目标目录不存在时全部为 809；否则为第一条失败的错误码
     */
    static int Move(const ShareWriteContext& ctx, const std::vector<std::string>& relPaths, const std::string& toDir,
                    std::vector<ShareOpItem>& results);

    /**
     *  一个条目占用的总字节数：文件为其大小，目录为其下全部文件之和（含隐藏文件；不进入重解析点）。
     *  @param absPath 本机绝对路径
     *  @param bytes   [出参] 总字节数
     *  @return 算出来返回 true；读不了目录或层数超过 kShareWriteSizeMaxDepth 时返回 false
     */
    static bool EntryBytes(const std::wstring& absPath, int64_t& bytes);

    //相对路径的上级目录（"a/b/c" 得 "a/b"，"a" 得空串）
    static std::string ParentOf(const std::string& relPath);

    //相对路径的最后一段（"a/b/c" 得 "c"）
    static std::string LastSegment(const std::string& relPath);

    //拼接目录与名字（目录为空串时就是名字）
    static std::string Join(const std::string& dir, const std::string& name);

    /**
     *  判断 relPath 是不是 dir 本身或在 dir 之下（按 '/' 分段，不区分大小写）。
     *  @return 是返回 true；dir 为空串（共享根）时总是 true
     */
    static bool IsSameOrUnder(const std::string& relPath, const std::string& dir);

    //两个 UTF-8 字符串是否在不区分大小写时相等（按 Windows 文件名的比较规则）
    static bool EqualsIgnoreCase(const std::string& a, const std::string& b);

private:
    ShareWriteOps() = delete;
};

} // namespace fileshare
