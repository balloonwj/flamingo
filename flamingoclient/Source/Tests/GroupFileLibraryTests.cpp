/**
 *  群文件库纯逻辑（筛选 / 搜索 / 分页 / 增删）单元测试（Suite GFLIB），
 *  GroupFileLibraryTests.cpp
 *
 *  覆盖：全量查询与顺序、分类筛选（全部/PDF/文档/表格/设计 与扩展名归类）、
 *       有效扩展名取值（m_ext 优先、空则从文件名取、大小写归一）、文件名搜索
 *       （子串 / 大小写不敏感 / 空关键字）、分类+搜索组合、分页（每页切分 / 末页
 *       不满 / 页码越界钳定 / 空结果算 1 页 / pageSize<=0 兜底）、变更推送的
 *       Upsert（新增 / 按 entryid 覆盖）与 RemoveByEntryId。框架 Catch2 v3。
 *  纯逻辑，不创建窗口、不联网。
 *  balloonwj@qq.com   2026-06-08
 */
#include "catch_amalgamated.hpp"
#include "../BalloonChatWnd/GroupFileLibrary.h"

using namespace balloonchatwnd;
using net::GroupFileEntry;

namespace {

// 便捷构造一条群文件条目（只填测试关心的 entryid / 文件名 / 扩展名）。
GroupFileEntry MakeEntry(const char* entryid, const char* name, const char* ext)
{
    GroupFileEntry e;
    e.m_entryid = entryid;
    e.m_name    = name;
    e.m_ext     = ext;
    return e;
}

// 造一批覆盖各分类的样本：pdf / docx / pptx / xlsx / csv / fig / sketch / png / zip。
std::vector<GroupFileEntry> SampleSet()
{
    std::vector<GroupFileEntry> v;
    v.push_back(MakeEntry("1", "用户研究报告.pdf",      "pdf"));
    v.push_back(MakeEntry("2", "群文件功能 PRD.docx",   "docx"));
    v.push_back(MakeEntry("3", "发布会演示.pptx",        "pptx"));
    v.push_back(MakeEntry("4", "v3.2-发布计划.xlsx",     "xlsx"));
    v.push_back(MakeEntry("5", "数据导出.csv",           "csv"));
    v.push_back(MakeEntry("6", "登录页 final.fig",       "fig"));
    v.push_back(MakeEntry("7", "原型交互稿.sketch",      "sketch"));
    v.push_back(MakeEntry("8", "首页截图.png",           "png"));
    v.push_back(MakeEntry("9", "源码归档.zip",           "zip"));
    return v;
}

} // anonymous namespace

// ============================ 静态工具 ============================

TEST_CASE("GFLIB1 ExtMatchesCategory 各分类按扩展名归类正确")
{
    // 全部：任何扩展名（含空）都匹配
    CHECK(GroupFileLibrary::ExtMatchesCategory("pdf", kGroupFileCatAll));
    CHECK(GroupFileLibrary::ExtMatchesCategory("",    kGroupFileCatAll));

    // PDF
    CHECK(GroupFileLibrary::ExtMatchesCategory("pdf", kGroupFileCatPdf));
    CHECK_FALSE(GroupFileLibrary::ExtMatchesCategory("docx", kGroupFileCatPdf));

    // 文档：doc/docx/ppt/pptx/txt/rtf/md，且不含 pdf（pdf 单列）
    CHECK(GroupFileLibrary::ExtMatchesCategory("docx", kGroupFileCatDoc));
    CHECK(GroupFileLibrary::ExtMatchesCategory("pptx", kGroupFileCatDoc));
    CHECK(GroupFileLibrary::ExtMatchesCategory("txt",  kGroupFileCatDoc));
    CHECK_FALSE(GroupFileLibrary::ExtMatchesCategory("pdf",  kGroupFileCatDoc));
    CHECK_FALSE(GroupFileLibrary::ExtMatchesCategory("xlsx", kGroupFileCatDoc));

    // 表格：xls/xlsx/csv
    CHECK(GroupFileLibrary::ExtMatchesCategory("xlsx", kGroupFileCatSheet));
    CHECK(GroupFileLibrary::ExtMatchesCategory("csv",  kGroupFileCatSheet));
    CHECK_FALSE(GroupFileLibrary::ExtMatchesCategory("docx", kGroupFileCatSheet));

    // 设计：fig/psd/ai/sketch/xd/cdr
    CHECK(GroupFileLibrary::ExtMatchesCategory("fig",    kGroupFileCatDesign));
    CHECK(GroupFileLibrary::ExtMatchesCategory("sketch", kGroupFileCatDesign));
    CHECK_FALSE(GroupFileLibrary::ExtMatchesCategory("png", kGroupFileCatDesign));
}

TEST_CASE("GFLIB2 EffectiveExtLower：m_ext 优先、空则取文件名、大小写归一")
{
    // m_ext 有值（大写）→ 归一小写
    GroupFileEntry a = MakeEntry("1", "Report.PDF", "PDF");
    CHECK(GroupFileLibrary::EffectiveExtLower(a) == "pdf");

    // m_ext 空 → 从文件名取最后一段
    GroupFileEntry b = MakeEntry("2", "归档.Tar.GZ", "");
    CHECK(GroupFileLibrary::EffectiveExtLower(b) == "gz");

    // 文件名无扩展名 → 空串
    GroupFileEntry c = MakeEntry("3", "README", "");
    CHECK(GroupFileLibrary::EffectiveExtLower(c).empty());
}

TEST_CASE("GFLIB3 NameMatchesSearch：子串 / 大小写不敏感 / 空关键字恒真")
{
    CHECK(GroupFileLibrary::NameMatchesSearch("API 接口文档 v3.docx", "接口"));
    CHECK(GroupFileLibrary::NameMatchesSearch("Login Final.fig", "final"));   // 大小写不敏感
    CHECK(GroupFileLibrary::NameMatchesSearch("Login Final.fig", "FINAL"));
    CHECK_FALSE(GroupFileLibrary::NameMatchesSearch("用户研究报告.pdf", "接口"));
    CHECK(GroupFileLibrary::NameMatchesSearch("任意文件.zip", ""));            // 空关键字恒真
}

// ============================ 查询：筛选 ============================

TEST_CASE("GFLIB4 Query 全部：返回全量、保持顺序、单页装得下")
{
    GroupFileLibrary lib;
    lib.SetAll(SampleSet());
    REQUIRE(lib.TotalAll() == 9);

    std::vector<GroupFileEntry> items;
    int total = 0, pageCount = 0, curPage = -1;
    lib.Query(kGroupFileCatAll, "", 0, 50, items, total, pageCount, curPage);

    CHECK(total == 9);
    CHECK(pageCount == 1);
    CHECK(curPage == 0);
    REQUIRE(items.size() == 9);
    // 顺序与 SetAll 一致
    CHECK(items[0].m_entryid == "1");
    CHECK(items[8].m_entryid == "9");
}

TEST_CASE("GFLIB5 Query 分类筛选：各 chip 只留对应扩展名")
{
    GroupFileLibrary lib;
    lib.SetAll(SampleSet());
    std::vector<GroupFileEntry> items;
    int total = 0, pageCount = 0, curPage = -1;

    // PDF：仅 1 条
    lib.Query(kGroupFileCatPdf, "", 0, 50, items, total, pageCount, curPage);
    CHECK(total == 1);
    REQUIRE(items.size() == 1);
    CHECK(items[0].m_entryid == "1");

    // 文档：docx + pptx = 2 条
    lib.Query(kGroupFileCatDoc, "", 0, 50, items, total, pageCount, curPage);
    CHECK(total == 2);

    // 表格：xlsx + csv = 2 条
    lib.Query(kGroupFileCatSheet, "", 0, 50, items, total, pageCount, curPage);
    CHECK(total == 2);

    // 设计：fig + sketch = 2 条
    lib.Query(kGroupFileCatDesign, "", 0, 50, items, total, pageCount, curPage);
    CHECK(total == 2);
}

TEST_CASE("GFLIB6 Query 分类+搜索组合")
{
    GroupFileLibrary lib;
    lib.SetAll(SampleSet());
    std::vector<GroupFileEntry> items;
    int total = 0, pageCount = 0, curPage = -1;

    // 文档分类 + 搜索 "PRD" → 仅 PRD.docx
    lib.Query(kGroupFileCatDoc, "PRD", 0, 50, items, total, pageCount, curPage);
    CHECK(total == 1);
    REQUIRE(items.size() == 1);
    CHECK(items[0].m_entryid == "2");

    // 文档分类 + 搜索 "发布" → 命中"发布会演示.pptx"（pptx 属文档分类）。
    // 注：xlsx 文件名也含"发布"，但 xlsx 不属文档分类，被分类筛掉。
    lib.Query(kGroupFileCatDoc, "发布", 0, 50, items, total, pageCount, curPage);
    CHECK(total == 1);
    REQUIRE(items.size() == 1);
    CHECK(items[0].m_entryid == "3");

    // 文档分类 + 搜索一个谁都不含的词 → 0；空结果仍算 1 页、当前页钳到 0
    lib.Query(kGroupFileCatDoc, "不存在的关键词", 0, 50, items, total, pageCount, curPage);
    CHECK(total == 0);
    CHECK(items.empty());
    CHECK(pageCount == 1);
    CHECK(curPage == 0);
}

// ============================ 查询：分页 ============================

TEST_CASE("GFLIB7 Query 分页：切分 / 末页不满 / 页码越界钳定")
{
    GroupFileLibrary lib;
    lib.SetAll(SampleSet());   // 9 条
    std::vector<GroupFileEntry> items;
    int total = 0, pageCount = 0, curPage = -1;

    // 每页 4 条 → 9 条共 3 页（4 + 4 + 1）
    lib.Query(kGroupFileCatAll, "", 0, 4, items, total, pageCount, curPage);
    CHECK(total == 9);
    CHECK(pageCount == 3);
    CHECK(curPage == 0);
    REQUIRE(items.size() == 4);
    CHECK(items[0].m_entryid == "1");
    CHECK(items[3].m_entryid == "4");

    // 第 2 页（0-based=1）：第 5~8 条
    lib.Query(kGroupFileCatAll, "", 1, 4, items, total, pageCount, curPage);
    CHECK(curPage == 1);
    REQUIRE(items.size() == 4);
    CHECK(items[0].m_entryid == "5");

    // 末页（0-based=2）：仅 1 条
    lib.Query(kGroupFileCatAll, "", 2, 4, items, total, pageCount, curPage);
    CHECK(curPage == 2);
    REQUIRE(items.size() == 1);
    CHECK(items[0].m_entryid == "9");

    // 页码越上界 → 钳到末页
    lib.Query(kGroupFileCatAll, "", 99, 4, items, total, pageCount, curPage);
    CHECK(curPage == 2);
    REQUIRE(items.size() == 1);
    CHECK(items[0].m_entryid == "9");

    // 页码负数 → 钳到 0
    lib.Query(kGroupFileCatAll, "", -5, 4, items, total, pageCount, curPage);
    CHECK(curPage == 0);
    CHECK(items[0].m_entryid == "1");
}

TEST_CASE("GFLIB8 Query pageSize<=0 视为每页 1 条")
{
    GroupFileLibrary lib;
    lib.SetAll(SampleSet());   // 9 条
    std::vector<GroupFileEntry> items;
    int total = 0, pageCount = 0, curPage = -1;

    lib.Query(kGroupFileCatAll, "", 0, 0, items, total, pageCount, curPage);
    CHECK(pageCount == 9);          // 每页 1 条 → 9 页
    REQUIRE(items.size() == 1);
    CHECK(items[0].m_entryid == "1");
}

// ============================ 变更推送：增删 ============================

TEST_CASE("GFLIB9 Upsert 新增与按 entryid 覆盖")
{
    GroupFileLibrary lib;
    lib.SetAll(SampleSet());   // 9 条
    REQUIRE(lib.TotalAll() == 9);

    // 新 entryid → 追加
    lib.Upsert(MakeEntry("10", "新上传.docx", "docx"));
    CHECK(lib.TotalAll() == 10);

    // 已有 entryid → 覆盖（数量不变，内容更新）
    lib.Upsert(MakeEntry("1", "改名后的报告.pdf", "pdf"));
    CHECK(lib.TotalAll() == 10);

    std::vector<GroupFileEntry> items;
    int total = 0, pageCount = 0, curPage = -1;
    lib.Query(kGroupFileCatAll, "改名后", 0, 50, items, total, pageCount, curPage);
    REQUIRE(items.size() == 1);
    CHECK(items[0].m_entryid == "1");
    CHECK(items[0].m_name == "改名后的报告.pdf");
}

TEST_CASE("GFLIB10 RemoveByEntryId 删除并返回是否命中")
{
    GroupFileLibrary lib;
    lib.SetAll(SampleSet());   // 9 条

    CHECK(lib.RemoveByEntryId("5"));    // 命中
    CHECK(lib.TotalAll() == 8);
    CHECK_FALSE(lib.RemoveByEntryId("5"));   // 再删同一条 → 未命中
    CHECK_FALSE(lib.RemoveByEntryId("404")); // 不存在 → 未命中
    CHECK(lib.TotalAll() == 8);
}

TEST_CASE("GFLIB11 FindByEntryId 命中返回完整条目、未命中返回 NULL")
{
    GroupFileLibrary lib;
    lib.SetAll(SampleSet());   // 9 条

    const GroupFileEntry* e = lib.FindByEntryId("6");   // 登录页 final.fig
    REQUIRE(e != NULL);
    CHECK(e->m_entryid == "6");
    CHECK(e->m_name == "登录页 final.fig");
    CHECK(e->m_ext == "fig");

    CHECK(lib.FindByEntryId("404") == NULL);   // 不存在
    CHECK(lib.FindByEntryId("") == NULL);      // 空
}

TEST_CASE("GFLIB12 CanDeleteGroupFile 权限规则与服务端一致")
{
    // 角色：group_role_member=0 / group_role_admin=1 / group_role_owner=2。
    // 群主 / 管理员可删群内任意（含别人上传的）。
    CHECK(GroupFileLibrary::CanDeleteGroupFile(2 /*群主*/,   false) == true);
    CHECK(GroupFileLibrary::CanDeleteGroupFile(1 /*管理员*/, false) == true);
    // 群主 / 管理员删自己上传的当然也行。
    CHECK(GroupFileLibrary::CanDeleteGroupFile(2, true) == true);
    CHECK(GroupFileLibrary::CanDeleteGroupFile(1, true) == true);
    // 普通成员：只能删自己上传的，删别人的不行。
    CHECK(GroupFileLibrary::CanDeleteGroupFile(0 /*成员*/, true)  == true);
    CHECK(GroupFileLibrary::CanDeleteGroupFile(0 /*成员*/, false) == false);
    // 非成员（role 取不到时上层会传 <0）：一律不可删别人的。
    CHECK(GroupFileLibrary::CanDeleteGroupFile(-1, false) == false);
    // 边界：非成员即便 isOwnUpload 这种矛盾输入也按"上传者本人"放行（上层保证 role>=0 才会出现 own）。
    CHECK(GroupFileLibrary::CanDeleteGroupFile(-1, true) == true);
}
