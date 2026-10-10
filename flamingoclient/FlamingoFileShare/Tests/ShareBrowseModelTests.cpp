/**
 *  浏览窗口纯逻辑单元测试（Suite SBM，标签 [sharebrowse]），ShareBrowseModelTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 13 步的 13a）：
 *    - SBM1 路径：拼接、取上一级、拆段，共享根为空串；
 *    - SBM2 排序：目录始终在前；按名称（不区分大小写）、大小、修改时间升序与降序；大小与时间相同时按名称；
 *    - SBM3 大小的显示文字：字节、KB、MB、GB 的进位与一位小数，目录为空；
 *    - SBM4 时间的显示文字：格式化到分钟，0 返回空串；
 *    - SBM5 提示文字：可用不显示横幅，暂不可用与已失效按原因给出文字，错误码有对应文字，未知错误码带上数值。
 *    - SBM10 多选（第 2 期第 5 步）：多选时右键菜单去掉单项操作、删除确认的文字、按所在目录分组（共享内路径与本机路径）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Engine/ShareBrowseModel.h"
#include "../../Source/net/Protocol.h"

using namespace fileshare;

namespace {

//SBM3 用到的几个字节数
const int64_t kOneKb = 1024;
const int64_t kOneAndHalfKb = 1536;
const int64_t kTwelveMb = 12LL * 1024 * 1024;
const int64_t kThreeGb = 3LL * 1024 * 1024 * 1024;

//SBM4：一个修改时间（任意正值），只检查格式
const int64_t kSomeMtime = 1759370000123LL;

//SBM5：一个不在文件共享段里的错误码
const int kUnknownCode = 12345;

ShareEntry Entry(const std::string& name, bool isDir, int64_t size, int64_t mtime)
{
    ShareEntry e;
    e.m_name = name;
    e.m_isDir = isDir;
    e.m_size = size;
    e.m_mtime = mtime;
    return e;
}

std::vector<std::string> Names(const std::vector<ShareEntry>& entries)
{
    std::vector<std::string> names;
    for (size_t i = 0; i < entries.size(); ++i)
        names.push_back(entries[i].m_name);
    return names;
}

std::vector<std::string> List(const char* a, const char* b, const char* c, const char* d, const char* e)
{
    std::vector<std::string> v;
    v.push_back(a);
    v.push_back(b);
    v.push_back(c);
    v.push_back(d);
    v.push_back(e);
    return v;
}

} // namespace

TEST_CASE("SBM1 路径的拼接、上一级与拆段", "[sharebrowse]")
{
    CHECK(ShareBrowseModel::JoinPath("", "docs") == "docs");
    CHECK(ShareBrowseModel::JoinPath("docs", "a.txt") == "docs/a.txt");
    CHECK(ShareBrowseModel::JoinPath("docs/2026", "q3") == "docs/2026/q3");

    CHECK(ShareBrowseModel::ParentPath("docs/2026/q3") == "docs/2026");
    CHECK(ShareBrowseModel::ParentPath("docs") == "");
    CHECK(ShareBrowseModel::ParentPath("") == "");

    CHECK(ShareBrowseModel::SplitPath("").empty());
    const std::vector<std::string> parts = ShareBrowseModel::SplitPath("docs/2026/q3");
    REQUIRE(parts.size() == 3);
    CHECK(parts[0] == "docs");
    CHECK(parts[2] == "q3");
}

TEST_CASE("SBM2 排序：目录在前，按列升序与降序", "[sharebrowse]")
{
    std::vector<ShareEntry> base;
    base.push_back(Entry("b.txt", false, 300, 2));
    base.push_back(Entry("Zeta", true, 0, 5));
    base.push_back(Entry("A.bin", false, 100, 3));
    base.push_back(Entry("alpha", true, 0, 1));
    base.push_back(Entry("c.log", false, 300, 1));

    //名称升序：目录 alpha、Zeta（不区分大小写），文件 A.bin、b.txt、c.log
    std::vector<ShareEntry> v = base;
    ShareBrowseModel::SortEntries(v, share_sort_name, true);
    CHECK(Names(v) == List("alpha", "Zeta", "A.bin", "b.txt", "c.log"));

    //名称降序：目录仍在前
    v = base;
    ShareBrowseModel::SortEntries(v, share_sort_name, false);
    CHECK(Names(v) == List("Zeta", "alpha", "c.log", "b.txt", "A.bin"));

    //大小升序：目录之间按名称；b.txt 与 c.log 同为 300 字节，按名称
    v = base;
    ShareBrowseModel::SortEntries(v, share_sort_size, true);
    CHECK(Names(v) == List("alpha", "Zeta", "A.bin", "b.txt", "c.log"));

    //大小降序
    v = base;
    ShareBrowseModel::SortEntries(v, share_sort_size, false);
    CHECK(Names(v) == List("Zeta", "alpha", "c.log", "b.txt", "A.bin"));

    //修改时间升序：目录 alpha(1)、Zeta(5)；文件 c.log(1)、b.txt(2)、A.bin(3)
    v = base;
    ShareBrowseModel::SortEntries(v, share_sort_mtime, true);
    CHECK(Names(v) == List("alpha", "Zeta", "c.log", "b.txt", "A.bin"));

    //修改时间降序
    v = base;
    ShareBrowseModel::SortEntries(v, share_sort_mtime, false);
    CHECK(Names(v) == List("Zeta", "alpha", "A.bin", "b.txt", "c.log"));
}

TEST_CASE("SBM3 大小的显示文字", "[sharebrowse]")
{
    CHECK(ShareBrowseModel::FormatSize(0, false) == L"0 B");
    CHECK(ShareBrowseModel::FormatSize(kOneKb - 1, false) == L"1023 B");
    CHECK(ShareBrowseModel::FormatSize(kOneKb, false) == L"1.0 KB");
    CHECK(ShareBrowseModel::FormatSize(kOneAndHalfKb, false) == L"1.5 KB");
    CHECK(ShareBrowseModel::FormatSize(kTwelveMb, false) == L"12.0 MB");
    CHECK(ShareBrowseModel::FormatSize(kThreeGb, false) == L"3.0 GB");
    CHECK(ShareBrowseModel::FormatSize(kTwelveMb, true) == L"");
}

TEST_CASE("SBM4 时间的显示文字", "[sharebrowse]")
{
    CHECK(ShareBrowseModel::FormatTimeParts(2026, 10, 2, 9, 5) == L"2026-10-02 09:05");
    CHECK(ShareBrowseModel::FormatTime(0) == L"");
    //本机时区不固定，只检查长度与分隔符
    const std::wstring text = ShareBrowseModel::FormatTime(kSomeMtime);
    REQUIRE(text.size() == 16);
    CHECK(text[4] == L'-');
    CHECK(text[10] == L' ');
    CHECK(text[13] == L':');
}

TEST_CASE("SBM5 状态与错误的提示文字", "[sharebrowse]")
{
    CHECK(ShareBrowseModel::StateText(share_state_available, 0).empty());
    CHECK(ShareBrowseModel::StateText(share_state_unavailable, share_unavailable_owner_offline)
          == L"共享暂不可用：分享者不在线");
    CHECK(ShareBrowseModel::StateText(share_state_invalid, share_invalid_member_removed)
          == L"共享已失效：你已被移出共享成员");
    CHECK(ShareBrowseModel::StateText(share_state_invalid, kUnknownCode) == L"共享已失效");

    CHECK(ShareBrowseModel::ErrorText(error_code_share_owner_timeout) == L"分享者暂时无法响应");
    CHECK(ShareBrowseModel::ErrorText(error_code_share_path_not_found) == L"文件或文件夹不存在");
    CHECK(ShareBrowseModel::ErrorText(kUnknownCode) == L"操作失败（错误码 12345）");
}

TEST_CASE("SBM6 右键菜单按权限与共享类型显示", "[sharebrowse]")
{
    const int all = share_perm_upload | share_perm_mkdir | share_perm_rename | share_perm_delete | share_perm_move;
    //点在目录上、全部权限
    CHECK(ShareBrowseModel::ContextMenuItems(all, share_kind_folder, true, true, false)
          == (share_menu_open | share_menu_download | share_menu_rename | share_menu_delete | share_menu_move
              | share_menu_new_folder | share_menu_refresh));
    //点在文件上、只有下载
    CHECK(ShareBrowseModel::ContextMenuItems(0, share_kind_folder, true, false, false) == (share_menu_download | share_menu_refresh));
    //点在空白处：只有新建与刷新
    CHECK(ShareBrowseModel::ContextMenuItems(all, share_kind_disk, false, false, false) == (share_menu_new_folder | share_menu_refresh));
    CHECK(ShareBrowseModel::ContextMenuItems(0, share_kind_disk, false, false, false) == share_menu_refresh);
    //各权限各管一项
    CHECK(ShareBrowseModel::ContextMenuItems(share_perm_delete, share_kind_folder, true, false, false)
          == (share_menu_download | share_menu_delete | share_menu_refresh));
    CHECK(ShareBrowseModel::ContextMenuItems(share_perm_move, share_kind_folder, true, false, false)
          == (share_menu_download | share_menu_move | share_menu_refresh));
    //图片文件多一项「预览」；单文件共享的图片同样可以预览
    CHECK(ShareBrowseModel::ContextMenuItems(0, share_kind_folder, true, false, true)
          == (share_menu_download | share_menu_preview | share_menu_refresh));
    CHECK(ShareBrowseModel::ContextMenuItems(0, share_kind_file, true, false, true)
          == (share_menu_download | share_menu_preview | share_menu_refresh));
    //单文件共享：权限位被忽略，只有下载与刷新
    CHECK(ShareBrowseModel::ContextMenuItems(all, share_kind_file, true, false, false) == (share_menu_download | share_menu_refresh));
}

TEST_CASE("SBM7 新名字的检查", "[sharebrowse]")
{
    std::wstring error;
    CHECK(ShareBrowseModel::CheckNewName(L"新建文件夹", error));
    CHECK(error.empty());
    CHECK(ShareBrowseModel::CheckNewName(L"report v2.docx", error));
    CHECK_FALSE(ShareBrowseModel::CheckNewName(L"", error));
    CHECK(error == L"名称不能为空");
    CHECK_FALSE(ShareBrowseModel::CheckNewName(L"a/b", error));
    CHECK(error.find(L"不能包含") != std::wstring::npos);
    CHECK_FALSE(ShareBrowseModel::CheckNewName(L"a\\b", error));
    CHECK_FALSE(ShareBrowseModel::CheckNewName(L"a:b", error));
    CHECK_FALSE(ShareBrowseModel::CheckNewName(L"a?", error));
    CHECK_FALSE(ShareBrowseModel::CheckNewName(L"end.", error));
    CHECK(error.find(L"系统保留") != std::wstring::npos);
    CHECK_FALSE(ShareBrowseModel::CheckNewName(L" lead", error));
    CHECK_FALSE(ShareBrowseModel::CheckNewName(L"CON", error));
    CHECK_FALSE(ShareBrowseModel::CheckNewName(L"..", error));
    CHECK_FALSE(ShareBrowseModel::CheckNewName(std::wstring(kShareNameMaxChars + 1, L'a'), error));
    CHECK(error == L"名称太长");
    CHECK(ShareBrowseModel::CheckNewName(std::wstring(kShareNameMaxChars, L'a'), error));
}

TEST_CASE("SBM8 写操作结果的提示文字", "[sharebrowse]")
{
    std::vector<ShareOpItem> none;
    CHECK(ShareBrowseModel::OpResultText(sf_mkdir, 0, none) == L"已新建文件夹");
    CHECK(ShareBrowseModel::OpResultText(sf_rename, 0, none) == L"已重命名");

    std::vector<ShareOpItem> items(3);
    CHECK(ShareBrowseModel::OpResultText(sf_delete, 0, items) == L"已删除 3 项");
    CHECK(ShareBrowseModel::OpResultText(sf_move, 0, items) == L"已移动 3 项");

    items[1].m_code = error_code_share_name_conflict;
    items[2].m_code = error_code_share_path_not_found;
    const std::wstring partial = ShareBrowseModel::OpResultText(sf_move, error_code_share_name_conflict, items);
    CHECK(partial.find(L"移动了 1 项，2 项失败：") == 0);
    CHECK(partial.find(ShareBrowseModel::ErrorText(error_code_share_name_conflict)) != std::wstring::npos);

    std::vector<ShareOpItem> one(1);
    one[0].m_code = error_code_share_recycle_unavailable;
    const std::wstring failed = ShareBrowseModel::OpResultText(sf_delete, error_code_share_recycle_unavailable, one);
    CHECK(failed == L"删除失败：" + ShareBrowseModel::ErrorText(error_code_share_recycle_unavailable));
    //没有逐条结果（会话失败、超时）
    CHECK(ShareBrowseModel::OpResultText(sf_rename, error_code_share_owner_timeout, none).find(L"重命名失败：") == 0);
}

TEST_CASE("SBM9 缩略图网格的排版", "[sharebrowse]")
{
    ShareGridMetrics m;
    m.m_tileWidth = 100;
    m.m_tileHeight = 120;
    m.m_gap = 10;
    m.m_padding = 5;
    //宽 340：5 + 100 + 10 + 100 + 10 + 100 + 5 = 330 放得下 3 格，放不下第 4 格
    CHECK(ShareBrowseModel::GridColumns(340, m) == 3);
    CHECK(ShareBrowseModel::GridColumns(330, m) == 3);
    CHECK(ShareBrowseModel::GridColumns(329, m) == 2);
    //再窄也至少 1 格
    CHECK(ShareBrowseModel::GridColumns(10, m) == 1);

    CHECK(ShareBrowseModel::GridRows(0, 3) == 0);
    CHECK(ShareBrowseModel::GridRows(3, 3) == 1);
    CHECK(ShareBrowseModel::GridRows(7, 3) == 3);

    RECT row = { 20, 200, 400, 330 };
    const RECT tile = ShareBrowseModel::GridTileRect(row, 2, m);
    CHECK(tile.left == 20 + 5 + 2 * 110);
    CHECK(tile.right == tile.left + 100);
    CHECK(tile.top == 200);
    CHECK(tile.bottom == 320);

    //第 1 行（下标从 0 起）、共 7 个条目、每行 3 格
    CHECK(ShareBrowseModel::GridIndexAt(1, 20 + 5 + 50, 20, 3, 7, m) == 3);
    CHECK(ShareBrowseModel::GridIndexAt(1, 20 + 5 + 110 + 99, 20, 3, 7, m) == 4);
    //格子右侧的间距里
    CHECK(ShareBrowseModel::GridIndexAt(1, 20 + 5 + 105, 20, 3, 7, m) == -1);
    //左侧留白里
    CHECK(ShareBrowseModel::GridIndexAt(1, 22, 20, 3, 7, m) == -1);
    //最后一行没有条目的格子
    CHECK(ShareBrowseModel::GridIndexAt(2, 20 + 5 + 110 + 50, 20, 3, 7, m) == -1);
    CHECK(ShareBrowseModel::GridIndexAt(2, 20 + 5 + 50, 20, 3, 7, m) == 6);
    //超出列数
    CHECK(ShareBrowseModel::GridIndexAt(0, 20 + 5 + 3 * 110 + 1, 20, 3, 7, m) == -1);
}

TEST_CASE("SBM10 multi-selection helpers", "[fileshare][sharebrowse]")
{
    //多选时去掉打开、预览、重命名，其余保留
    const int all = share_menu_open | share_menu_preview | share_menu_download | share_menu_rename | share_menu_delete
                    | share_menu_move | share_menu_new_folder | share_menu_refresh;
    CHECK(ShareBrowseModel::MenuItemsForSelection(all, 1) == all);
    CHECK(ShareBrowseModel::MenuItemsForSelection(all, 0) == all);
    CHECK(ShareBrowseModel::MenuItemsForSelection(all, 2)
          == (share_menu_download | share_menu_delete | share_menu_move | share_menu_new_folder | share_menu_refresh));

    //删除确认
    std::vector<std::wstring> names(1, L"报告.docx");
    CHECK(ShareBrowseModel::DeleteConfirmText(names) == L"确定删除「报告.docx」吗？\n删除的内容会放进分享者电脑的回收站。");
    names.push_back(L"b");
    names.push_back(L"c");
    CHECK(ShareBrowseModel::DeleteConfirmText(names) == L"确定删除「报告.docx」等 3 项吗？\n删除的内容会放进分享者电脑的回收站。");

    //共享内路径按所在目录分组：保持第一次出现的先后
    std::vector<std::string> paths;
    paths.push_back("a/x.txt");
    paths.push_back("b/y.txt");
    paths.push_back("a/z");
    paths.push_back("root.txt");
    paths.push_back("b/w.txt");
    const std::vector<std::vector<size_t> > groups = ShareBrowseModel::GroupByParent(paths);
    REQUIRE(groups.size() == 3);
    CHECK(groups[0] == std::vector<size_t>{ 0, 2 });
    CHECK(groups[1] == std::vector<size_t>{ 1, 4 });
    CHECK(groups[2] == std::vector<size_t>{ 3 });
    CHECK(ShareBrowseModel::GroupByParent(std::vector<std::string>()).empty());

    //本机路径分组：目录名不区分大小写
    std::vector<std::wstring> local;
    local.push_back(L"C:\\Data\\a.txt");
    local.push_back(L"D:\\b.txt");
    local.push_back(L"c:\\data\\c");
    const std::vector<std::vector<std::wstring> > localGroups = ShareBrowseModel::GroupLocalByParent(local);
    REQUIRE(localGroups.size() == 2);
    REQUIRE(localGroups[0].size() == 2);
    CHECK(localGroups[0][1] == L"c:\\data\\c");
    CHECK(localGroups[1][0] == L"D:\\b.txt");
}
