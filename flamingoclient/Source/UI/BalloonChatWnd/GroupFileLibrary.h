/**
 *  GroupFileLibrary.h
 *
 *  群文件库的纯逻辑数据模型 —— 持有某个群「拉全量后」的文件清单，提供
 *  按分类筛选（全部 / PDF / 文档 / 表格 / 设计）、按文件名搜索、分页的纯查询，
 *  以及变更推送用的增量增 / 删。
 *
 *  为什么客户端做筛选 / 搜索 / 分页（而非服务端）：服务端 cmd_get_group_files
 *  只支持 filetype(服务端粗分类) / offset / count，没有按文件名搜索的参数，且其
 *  filetype 分类（文档/图片/视频/音频/压缩包/其它）与设计稿要的 PDF/表格/设计
 *  更细分类对不上。故按 Option B：客户端把整群文件分页拉全后缓存进本模块，
 *  搜索 / 分类 / 分页全在客户端做。
 *
 *  本模块是纯逻辑：只依赖 net::GroupFileEntry 与 std，不触及 UI / 网络 / ATL，
 *  便于单元测试，见 flamingoclient/Source/Tests/GroupFileLibraryTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-08
 */

#pragma once

#include <string>
#include <vector>
#include "../../net/GroupFileProtocolClient.h"   // net::GroupFileEntry

namespace balloonchatwnd {

// 群文件面板分类筛选 chip 的分类枚举（客户端按文件扩展名归类；与服务端
// group_file_type 不同，是更贴合设计稿的细分类）。
enum GroupFileChipCategory
{
    kGroupFileCatAll    = 0,   // 全部：不筛选
    kGroupFileCatPdf    = 1,   // PDF：扩展名 pdf
    kGroupFileCatDoc    = 2,   // 文档：doc/docx/ppt/pptx/txt/rtf/md（不含 pdf，pdf 单列）
    kGroupFileCatSheet  = 3,   // 表格：xls/xlsx/csv
    kGroupFileCatDesign = 4,   // 设计：fig/psd/ai/sketch/xd/cdr
};

/**
 *  GroupFileLibrary —— 群文件库的内存数据模型（纯逻辑）。
 *
 *  典型用法：拉全量后 SetAll；UI 持有「当前分类 / 搜索词 / 页码」三个状态，每次
 *  渲染调 Query(分类, 搜索, 页, 每页条数, ...) 取本页条目与分页信息；收到
 *  cmd_group_files_changed_notify 时 Upsert / RemoveByEntryId 增量更新后重渲染。
 */
class GroupFileLibrary
{
public:
    GroupFileLibrary();

    // 用拉全量结果替换整库（清空旧数据）。
    void SetAll(const std::vector<net::GroupFileEntry>& all);

    // 增量插入或按 entryid 替换一条（变更推送「新增」用；entryid 已存在则覆盖）。
    void Upsert(const net::GroupFileEntry& e);

    // 按 entryid 删除一条（变更推送「删除」用）。返回是否确有删除。
    bool RemoveByEntryId(const std::string& entryid);

    // 清空整库。
    void Clear();

    // 全量文件数（未筛选）。
    int TotalAll() const { return (int)m_all.size(); }

    // 只读访问全量（供调试 / 测试）。
    const std::vector<net::GroupFileEntry>& All() const { return m_all; }

    // 按 entryid 查一条（下载时据面板行的 entryid 取回完整条目拿 fileid/name/md5/size）。
    // 命中返回指向库内条目的指针（生命周期随库；调用方用完即弃，勿长持），未命中返回 NULL。
    const net::GroupFileEntry* FindByEntryId(const std::string& entryid) const;

    /**
     *  纯查询：按分类 + 文件名搜索过滤全量，再按每页条数分页取第 page 页。
     *    category：       分类筛选，取值见 GroupFileChipCategory。
     *    search：         文件名搜索关键字（大小写不敏感子串匹配；空串=不按名筛）。
     *    page：           期望页码（0-based）；越界自动钳到 [0, pageCount-1]。
     *    pageSize：       每页条数；<=0 视为 1。
     *    outItems：   [出] 本页文件条目（保持库内顺序，即服务端给的登记时间倒序）。
     *    outTotalFiltered：[出] 筛选后（分页前）的总条数。
     *    outPageCount：[出] 总页数（筛选后至少 1，空结果也算 1 页）。
     *    outCurPage： [出] 钳定后的实际当前页（0-based）。
     */
    void Query(int category, const std::string& search, int page, int pageSize,
               std::vector<net::GroupFileEntry>& outItems,
               int& outTotalFiltered, int& outPageCount, int& outCurPage) const;

    // ---- 静态纯工具（便于单测） ----

    // 取一条目的「有效扩展名」（小写）：优先用 m_ext，为空则从文件名取最后一个点之后。
    static std::string EffectiveExtLower(const net::GroupFileEntry& e);

    // 给定（小写）扩展名是否属于某 chip 分类。
    static bool ExtMatchesCategory(const std::string& extLower, int category);

    // 文件名是否命中搜索关键字（大小写不敏感子串；空关键字恒真）。
    static bool NameMatchesSearch(const std::string& name, const std::string& search);

    /**
     *  判断当前用户能否删除某群文件。与服务端 onDeleteGroupFile 的权限规则严格一致：
     *  群主 / 管理员可删群内任意文件，登记者本人可删自己上传的；普通成员删别人的、
     *  非成员一律不可。
     *    myGroupRole： 当前用户在该群的角色（group_member_role：0 成员 / 1 管理员 / 2 群主）。
     *    isOwnUpload： 该文件是否为当前用户上传（uploaderid == 自己 userid）。
     *  返回：可删返回 true。
     */
    static bool CanDeleteGroupFile(int myGroupRole, bool isOwnUpload);

private:
    std::vector<net::GroupFileEntry> m_all;   // 全量文件（拉全后缓存；顺序=服务端登记时间倒序）
};

} // namespace balloonchatwnd
