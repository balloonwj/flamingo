/**
 *  分享者一端按文件名搜索单元测试（Suite SSR，标签 [sharesearch]），ShareSearchTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 2 期第 3 步的 3a）：
 *    - SSR1 关键字检查与匹配：不区分大小写的子串匹配、中文；空关键字、超长、含控制字符的关键字被拒；
 *    - SSR2 遍历：从起点目录往下逐层查找，命中带相对共享根的路径、目录标志、大小；最后一批 done 为真、没有截断；
 *           命中多于一批时先分批交出；
 *    - SSR3 上限：命中到条数上限时提前结束并标明截断；耗时上限为 0 时立即按超时结束并标明截断；
 *    - SSR4 中止：取消标志已置位时中止、不交出最后一批；接收方返回 false 时中止；
 *    - SSR5 隐藏、重解析点与半成品：未含隐藏文件时隐藏条目不报告也不进入，含隐藏文件时照常；目录联接不报告也不进入；
 *           正在上传的半成品不报告；
 *    - SSR6 列整棵目录树（第 2 期第 5 步）：每个条目都算命中、关键字不用，父目录在子条目之前；条数上限照常生效。
 *  本文件由 FlamingoUnit 编译，被测源文件 Engine/ShareSearch.cpp 同时编进 FlamingoFileShare.dll。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include "../Engine/ShareFileOps.h"
#include "../Engine/ShareSearch.h"
#include "ShareTestFs.h"

using namespace fileshare;

namespace {

//本套用例在测试程序目录下建的临时目录名（每个用例开始前整个删掉）
const wchar_t* const kSsrTestDir = L"ssr_test";

//测试文件的字节数
const size_t kFileBytes = 123;

//SSR2：命中多于一批时的文件个数（多于 kShareSearchFlushHits）
const size_t kManyFiles = 250;

//SSR3：条数上限
const int kSmallMax = 5;

//接收方：收集各批命中
class CollectSink : public IShareSearchSink
{
public:
    CollectSink()
        : m_batches(0)
        , m_doneCount(0)
        , m_truncated(false)
        , m_stopAfter(0)
    {
    }

    virtual bool OnSearchHits(const std::vector<ShareEntry>& hits, bool done, bool truncated) override
    {
        ++m_batches;
        m_hits.insert(m_hits.end(), hits.begin(), hits.end());
        if (done)
        {
            ++m_doneCount;
            m_truncated = truncated;
        }
        return m_stopAfter == 0 || m_batches < m_stopAfter;
    }

    //命中的路径，排序后便于比较
    std::vector<std::string> Paths() const
    {
        std::vector<std::string> v;
        for (size_t i = 0; i < m_hits.size(); ++i)
            v.push_back(m_hits[i].m_name);
        std::sort(v.begin(), v.end());
        return v;
    }

    std::vector<ShareEntry> m_hits;      //全部命中
    int                     m_batches;   //交出的批数
    int                     m_doneCount; //done 为真的批数
    bool                    m_truncated; //最后一批是否标明截断
    int                     m_stopAfter; //交出第几批之后返回 false；0 表示一直返回 true
};

//本套用例的临时目录（每次调用都先清空再建），返回共享根
std::wstring FreshRoot()
{
    const std::wstring base = sharetestfs::ExeDir() + L"\\" + kSsrTestDir;
    sharetestfs::DeleteTree(base);
    const std::wstring root = base + L"\\root";
    sharetestfs::MakeDirs(root);
    return root;
}

ShareSearchParams Params(const std::wstring& startAbs, const std::string& startRel, const wchar_t* keyword)
{
    ShareSearchParams p;
    p.m_startAbs = startAbs;
    p.m_startRel = startRel;
    p.m_keyword = keyword;
    p.m_includeHidden = false;
    return p;
}

std::vector<std::string> Expect(const char* a, const char* b = NULL, const char* c = NULL, const char* d = NULL)
{
    std::vector<std::string> v(1, a);
    if (b != NULL)
        v.push_back(b);
    if (c != NULL)
        v.push_back(c);
    if (d != NULL)
        v.push_back(d);
    std::sort(v.begin(), v.end());
    return v;
}

} // namespace

TEST_CASE("SSR1 keyword check and matching", "[fileshare][sharesearch]")
{
    CHECK(ShareSearch::Matches(L"Quarterly Report.docx", L"report"));
    CHECK(ShareSearch::Matches(L"季度报告.docx", L"报告"));
    CHECK(ShareSearch::Matches(L"ABC", L"abc"));
    CHECK_FALSE(ShareSearch::Matches(L"abc", L"abd"));
    CHECK_FALSE(ShareSearch::Matches(L"abc", L""));

    std::wstring wide;
    CHECK(ShareSearch::CheckKeyword("\xE6\x8A\xA5\xE5\x91\x8A", wide));   //「报告」
    CHECK(wide == L"报告");
    CHECK_FALSE(ShareSearch::CheckKeyword("", wide));
    CHECK_FALSE(ShareSearch::CheckKeyword(std::string(kShareSearchKeywordMaxChars + 1, 'a'), wide));
    CHECK(ShareSearch::CheckKeyword(std::string(kShareSearchKeywordMaxChars, 'a'), wide));
    CHECK_FALSE(ShareSearch::CheckKeyword(std::string("a\tb"), wide));
}

TEST_CASE("SSR2 traversal and batches", "[fileshare][sharesearch]")
{
    const std::wstring root = FreshRoot();
    REQUIRE(sharetestfs::MakeDirs(root + L"\\docs\\report-2025"));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\other"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\Report.txt", kFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\report-2025\\final REPORT.pdf", kFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\notes.txt", kFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\other\\x.txt", kFileBytes));

    CollectSink sink;
    CHECK(ShareSearch::Run(Params(root, "", L"report"), NULL, &sink) == share_search_finished);
    CHECK(sink.Paths() == Expect("Report.txt", "docs/report-2025", "docs/report-2025/final REPORT.pdf"));
    CHECK(sink.m_doneCount == 1);
    CHECK_FALSE(sink.m_truncated);
    for (size_t i = 0; i < sink.m_hits.size(); ++i)
    {
        if (sink.m_hits[i].m_name == "Report.txt")
        {
            CHECK_FALSE(sink.m_hits[i].m_isDir);
            CHECK(sink.m_hits[i].m_size == static_cast<int64_t>(kFileBytes));
            CHECK(sink.m_hits[i].m_mtime > 0);
        }
        if (sink.m_hits[i].m_name == "docs/report-2025")
            CHECK(sink.m_hits[i].m_isDir);
    }

    //从子目录开始：路径仍相对共享根
    CollectSink sub;
    CHECK(ShareSearch::Run(Params(root + L"\\docs", "docs", L"REPORT"), NULL, &sub) == share_search_finished);
    CHECK(sub.Paths() == Expect("docs/report-2025", "docs/report-2025/final REPORT.pdf"));

    //没有命中：仍然交出一批 done 为真的空结果
    CollectSink none;
    CHECK(ShareSearch::Run(Params(root, "", L"zzz"), NULL, &none) == share_search_finished);
    CHECK(none.m_hits.empty());
    CHECK(none.m_doneCount == 1);

    //命中多于一批：先分批交出
    REQUIRE(sharetestfs::MakeDirs(root + L"\\many"));
    for (size_t i = 0; i < kManyFiles; ++i)
    {
        wchar_t name[32] = { 0 };
        ::wsprintfW(name, L"\\many\\hit%03u.dat", static_cast<unsigned>(i));
        REQUIRE(sharetestfs::WriteFileBytes(root + name, 0));
    }
    CollectSink many;
    CHECK(ShareSearch::Run(Params(root, "", L"hit"), NULL, &many) == share_search_finished);
    CHECK(many.m_hits.size() == kManyFiles);
    CHECK(many.m_batches >= static_cast<int>(kManyFiles / kShareSearchFlushHits));
    CHECK(many.m_doneCount == 1);
}

TEST_CASE("SSR3 limits", "[fileshare][sharesearch]")
{
    const std::wstring root = FreshRoot();
    for (int i = 0; i < kSmallMax * 4; ++i)
    {
        wchar_t name[32] = { 0 };
        ::wsprintfW(name, L"\\match%02d.txt", i);
        REQUIRE(sharetestfs::WriteFileBytes(root + name, 0));
    }

    ShareSearchParams p = Params(root, "", L"match");
    p.m_maxResults = kSmallMax;
    CollectSink capped;
    CHECK(ShareSearch::Run(p, NULL, &capped) == share_search_truncated_count);
    CHECK(capped.m_hits.size() == static_cast<size_t>(kSmallMax));
    CHECK(capped.m_doneCount == 1);
    CHECK(capped.m_truncated);

    ShareSearchParams q = Params(root, "", L"match");
    q.m_timeoutMs = 0;
    CollectSink timed;
    CHECK(ShareSearch::Run(q, NULL, &timed) == share_search_truncated_timeout);
    CHECK(timed.m_doneCount == 1);
    CHECK(timed.m_truncated);
}

TEST_CASE("SSR4 cancel and stop", "[fileshare][sharesearch]")
{
    const std::wstring root = FreshRoot();
    for (size_t i = 0; i < kManyFiles; ++i)
    {
        wchar_t name[32] = { 0 };
        ::wsprintfW(name, L"\\hit%03u.dat", static_cast<unsigned>(i));
        REQUIRE(sharetestfs::WriteFileBytes(root + name, 0));
    }

    std::atomic<bool> cancel(true);
    CollectSink canceled;
    CHECK(ShareSearch::Run(Params(root, "", L"hit"), &cancel, &canceled) == share_search_canceled);
    CHECK(canceled.m_doneCount == 0);

    //接收方在第一批之后返回 false
    CollectSink stop;
    stop.m_stopAfter = 1;
    CHECK(ShareSearch::Run(Params(root, "", L"hit"), NULL, &stop) == share_search_stopped);
    CHECK(stop.m_batches == 1);
    CHECK(stop.m_doneCount == 0);
}

TEST_CASE("SSR5 hidden entries, junctions and upload parts", "[fileshare][sharesearch]")
{
    const std::wstring root = FreshRoot();
    const std::wstring outside = sharetestfs::ExeDir() + L"\\" + kSsrTestDir + L"\\outside";
    REQUIRE(sharetestfs::MakeDirs(outside));
    REQUIRE(sharetestfs::WriteFileBytes(outside + L"\\key-outside.txt", kFileBytes));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\secret"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\secret\\key-inner.txt", kFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\key-visible.txt", kFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\key-hidden.txt", kFileBytes));
    ::SetFileAttributesW(ShareToLongPath(root + L"\\secret").c_str(), FILE_ATTRIBUTE_HIDDEN);
    ::SetFileAttributesW(ShareToLongPath(root + L"\\key-hidden.txt").c_str(), FILE_ATTRIBUTE_HIDDEN);
    REQUIRE(sharetestfs::MakeJunction(root + L"\\key-link", outside,
                                      sharetestfs::ExeDir() + L"\\" + kSsrTestDir + L"\\jtmp"));
    const std::wstring part = root + L"\\key-uploading.part";
    REQUIRE(sharetestfs::WriteFileBytes(part, kFileBytes));
    CShareUploadRegistry::Instance().Add(part);

    CollectSink hiddenOff;
    CHECK(ShareSearch::Run(Params(root, "", L"key"), NULL, &hiddenOff) == share_search_finished);
    CHECK(hiddenOff.Paths() == Expect("key-visible.txt"));

    ShareSearchParams withHidden = Params(root, "", L"key");
    withHidden.m_includeHidden = true;
    CollectSink hiddenOn;
    CHECK(ShareSearch::Run(withHidden, NULL, &hiddenOn) == share_search_finished);
    CHECK(hiddenOn.Paths() == Expect("key-hidden.txt", "key-visible.txt", "secret/key-inner.txt"));

    CShareUploadRegistry::Instance().Remove(part);
}

TEST_CASE("SSR6 match all lists the whole tree", "[fileshare][sharesearch]")
{
    const std::wstring root = FreshRoot();
    REQUIRE(sharetestfs::MakeDirs(root + L"\\docs\\sub"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\a.txt", kFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\sub\\b.txt", kFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\other.txt", kFileBytes));

    ShareSearchParams p = Params(root + L"\\docs", "docs", L"");
    p.m_matchAll = true;
    CollectSink sink;
    CHECK(ShareSearch::Run(p, NULL, &sink) == share_search_finished);
    CHECK(sink.Paths() == Expect("docs/a.txt", "docs/sub", "docs/sub/b.txt"));
    //父目录在子条目之前
    size_t subPos = 0;
    size_t childPos = 0;
    for (size_t i = 0; i < sink.m_hits.size(); ++i)
    {
        if (sink.m_hits[i].m_name == "docs/sub")
            subPos = i;
        if (sink.m_hits[i].m_name == "docs/sub/b.txt")
            childPos = i;
    }
    CHECK(subPos < childPos);

    //条数上限照常生效
    p.m_maxResults = 2;
    CollectSink capped;
    CHECK(ShareSearch::Run(p, NULL, &capped) == share_search_truncated_count);
    CHECK(capped.m_hits.size() == 2);
    CHECK(capped.m_truncated);
}
