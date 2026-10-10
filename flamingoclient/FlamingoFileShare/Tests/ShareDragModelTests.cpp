/**
 *  选择集与拖出条目表单元测试（Suite SDM，标签 [sharedrag]），ShareDragModelTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 2 期第 5 步）：
 *    - SDM1 选择集：单击只选一项；Ctrl + 单击加选与减选；Shift + 单击从起点连选（替换原有选择）；Ctrl + Shift 追加一段；
 *           当前项是最近一次单击的那一项；SelectOnly 与 Clear；
 *    - SDM2 拖出条目表：单个文件落地为它的名字；选中的目录带上整棵目录树，名字相对拖出位置、父目录在前；
 *           搜索结果里深层的条目同样只保留从它自己开始的那一段；落地名字用反斜杠；
 *    - SDM3 拒绝：没有选中、目录还没有目录树、总条目数超过上限、顶层条目同名；拒绝原因的文字。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <map>
#include <string>
#include <vector>

#include "../Engine/ShareDragModel.h"
#include "../Engine/ShareSearch.h"

using namespace fileshare;

namespace {

//文件大小与修改时间
const int64_t kSize = 123;
const int64_t kMtime = 1700000000000LL;

ShareEntry Entry(const char* name, bool isDir)
{
    ShareEntry e;
    e.m_name = name;
    e.m_isDir = isDir;
    e.m_size = isDir ? 0 : kSize;
    e.m_mtime = kMtime;
    return e;
}

ShareDragSource Source(const char* path, const char* name, bool isDir)
{
    ShareDragSource s;
    s.m_sharePath = path;
    s.m_entry = Entry(name, isDir);
    return s;
}

std::vector<int> Ints(int a, int b = -1, int c = -1, int d = -1)
{
    std::vector<int> v(1, a);
    if (b >= 0)
        v.push_back(b);
    if (c >= 0)
        v.push_back(c);
    if (d >= 0)
        v.push_back(d);
    return v;
}

} // namespace

TEST_CASE("SDM1 selection", "[fileshare][sharedrag]")
{
    ShareSelection sel;
    CHECK(sel.Count() == 0);
    CHECK(sel.Primary() == -1);

    sel.Click(2, false, false);
    CHECK(sel.Sorted() == Ints(2));
    sel.Click(5, false, false);
    CHECK(sel.Sorted() == Ints(5));

    //Ctrl：加选、再点一次减选
    sel.Click(1, true, false);
    CHECK(sel.Sorted() == Ints(1, 5));
    CHECK(sel.Primary() == 1);
    sel.Click(5, true, false);
    CHECK(sel.Sorted() == Ints(1));

    //Shift：从起点（最近一次不带 Shift 的单击，这里是 Ctrl 单击的 5）连选到 3，替换原有选择
    sel.Click(3, false, true);
    CHECK(sel.Sorted() == Ints(3, 4, 5));
    CHECK(sel.Primary() == 3);
    //起点不变：再 Shift 到 6
    sel.Click(6, false, true);
    CHECK(sel.Sorted() == Ints(5, 6));

    //Ctrl + Shift：追加一段
    sel.Click(0, false, false);
    sel.Click(1, false, true);
    sel.Click(4, true, false);
    sel.Click(5, true, true);
    CHECK(sel.Sorted() == Ints(0, 1, 4, 5));

    sel.SelectOnly(7);
    CHECK(sel.Sorted() == Ints(7));
    CHECK(sel.Contains(7));
    CHECK_FALSE(sel.Contains(0));
    sel.Clear();
    CHECK(sel.Count() == 0);
    CHECK(sel.Primary() == -1);
    //负数下标不处理
    sel.Click(-1, false, false);
    CHECK(sel.Count() == 0);
}

TEST_CASE("SDM2 drag item list", "[fileshare][sharedrag]")
{
    std::vector<ShareDragSource> sources;
    sources.push_back(Source("docs/a.txt", "a.txt", false));
    sources.push_back(Source("docs/sub", "sub", true));
    std::map<std::string, std::vector<ShareEntry> > trees;
    std::vector<ShareEntry> tree;
    tree.push_back(Entry("docs/sub/inner", true));
    tree.push_back(Entry("docs/sub/b.txt", false));
    tree.push_back(Entry("docs/sub/inner/c.txt", false));
    trees["docs/sub"] = tree;

    std::vector<ShareDragItem> items;
    REQUIRE(ShareDragModel::Build(sources, trees, items) == share_drag_build_ok);
    REQUIRE(items.size() == 5);
    CHECK(items[0].m_dropName == L"a.txt");
    CHECK(items[0].m_sharePath == "docs/a.txt");
    CHECK_FALSE(items[0].m_isDir);
    CHECK(items[0].m_size == kSize);
    CHECK(items[1].m_dropName == L"sub");
    CHECK(items[1].m_isDir);
    CHECK(items[2].m_dropName == L"sub\\inner");
    CHECK(items[3].m_dropName == L"sub\\b.txt");
    CHECK(items[4].m_dropName == L"sub\\inner\\c.txt");
    CHECK(items[4].m_sharePath == "docs/sub/inner/c.txt");
    CHECK(items[4].m_mtime == kMtime);

    //共享根下的条目、中文名
    CHECK(ShareDragModel::DropNameFor("报告.docx", "") == L"报告.docx");
    CHECK(ShareDragModel::DropNameFor("a/b/c/d.txt", "a/b") == L"c\\d.txt");
    //不在拖出位置之下：只保留最后一段
    CHECK(ShareDragModel::DropNameFor("x/y.txt", "a") == L"y.txt");
    //"ab" 不算 "a" 之下
    CHECK(ShareDragModel::DropNameFor("ab/y.txt", "a") == L"y.txt");
}

TEST_CASE("SDM3 drag item list refusals", "[fileshare][sharedrag]")
{
    std::map<std::string, std::vector<ShareEntry> > trees;
    std::vector<ShareDragItem> items;
    CHECK(ShareDragModel::Build(std::vector<ShareDragSource>(), trees, items) == share_drag_build_empty);

    std::vector<ShareDragSource> sources(1, Source("docs", "docs", true));
    CHECK(ShareDragModel::Build(sources, trees, items) == share_drag_build_missing_tree);
    CHECK(items.empty());

    //目录本身加整棵树超过上限
    std::vector<ShareEntry> big;
    for (int i = 0; i < kShareTreeMaxItems; ++i)
        big.push_back(Entry("docs/f", false));
    trees["docs"] = big;
    CHECK(ShareDragModel::Build(sources, trees, items) == share_drag_build_too_many);
    CHECK(items.empty());
    //恰好在上限之内
    big.pop_back();
    trees["docs"] = big;
    CHECK(ShareDragModel::Build(sources, trees, items) == share_drag_build_ok);
    CHECK(items.size() == static_cast<size_t>(kShareTreeMaxItems));

    //搜索结果里来自不同目录的同名条目（不区分大小写）：落地会互相覆盖，拒绝
    std::vector<ShareDragSource> dup;
    dup.push_back(Source("a/Note.txt", "Note.txt", false));
    dup.push_back(Source("b/note.TXT", "note.TXT", false));
    CHECK(ShareDragModel::Build(dup, trees, items) == share_drag_build_duplicate);
    CHECK(items.empty());

    //拒绝原因的文字
    CHECK(ShareDragModel::RefusalText(share_drag_build_ok).empty());
    CHECK(ShareDragModel::RefusalText(share_drag_build_too_many) == L"选中的内容超过 5000 项，无法拖出，请使用「下载」");
    CHECK_FALSE(ShareDragModel::RefusalText(share_drag_build_missing_tree).empty());
    CHECK_FALSE(ShareDragModel::RefusalText(share_drag_build_empty).empty());
    CHECK_FALSE(ShareDragModel::RefusalText(share_drag_build_duplicate).empty());
}
