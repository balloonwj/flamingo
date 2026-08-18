/**
 *  文件本地落点登记库读写单元测试（Suite FLS），FileLocalStoreTests.cpp
 *
 *  用内存库（:memory:）驱动 balloonhistory::CFileLocalStore，覆盖：
 *    FLS1 登记后能按 (md5, 文件名) 查回同一路径；
 *    FLS2 重复登记覆盖为新路径（文件另存 / 重下到别处后登记须跟着走）；
 *    FLS3 未登记的键查不到，且出参被清空（不留上一次的残值）；
 *    FLS4 同一 md5 的不同文件名各自记账，互不覆盖；
 *    FLS5 参数非法（空 md5 / 空文件名 / 空路径）一律拒绝写入；
 *    FLS6 Remove 删掉登记后查不到，且重复删除是幂等的；
 *    FLS7 库未打开时读写不崩、一律返回失败。
 *
 *  注：CFileLocalStore 是单例，本套件各用例通过重新 Open(:memory:) 拿到干净的库
 *  ——同名重开会先关掉旧连接，内存库随之丢弃，等价于清空。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-03
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonHistory/FileLocalStore.h"

using namespace balloonhistory;

namespace {

//取一个干净的内存库：先关再开，确保不带上一个用例的数据。
static CFileLocalStore& FreshStore()
{
    CFileLocalStore& store = CFileLocalStore::GetInstance();
    store.Close();
    REQUIRE(store.Open(L":memory:"));
    return store;
}

const char* const kMd5A = "0123456789abcdef0123456789abcdef";
const char* const kMd5B = "fedcba9876543210fedcba9876543210";

} // namespace

TEST_CASE("FLS1 登记后能按 (md5, 文件名) 查回")
{
    CFileLocalStore& store = FreshStore();

    REQUIRE(store.Upsert(kMd5A, "report.zip", L"D:\\work\\report.zip"));

    std::wstring path;
    REQUIRE(store.Query(kMd5A, "report.zip", path));
    CHECK(path == L"D:\\work\\report.zip");
}

TEST_CASE("FLS2 重复登记覆盖为新路径")
{
    CFileLocalStore& store = FreshStore();

    REQUIRE(store.Upsert(kMd5A, "report.zip", L"D:\\work\\report.zip"));
    //源文件被移走后重新下载到缓存目录：登记须指向最新那份，否则还会去开已经没有的旧路径
    REQUIRE(store.Upsert(kMd5A, "report.zip", L"C:\\cache\\abc\\report.zip"));

    std::wstring path;
    REQUIRE(store.Query(kMd5A, "report.zip", path));
    CHECK(path == L"C:\\cache\\abc\\report.zip");
}

TEST_CASE("FLS3 未登记的键查不到且出参被清空")
{
    CFileLocalStore& store = FreshStore();
    REQUIRE(store.Upsert(kMd5A, "report.zip", L"D:\\work\\report.zip"));

    //出参先塞一个残值，验证查不到时会被清掉——否则调用方会拿着上一次的路径去开文件
    std::wstring path = L"D:\\stale\\leftover.bin";
    CHECK(store.Query(kMd5B, "report.zip", path) == false);
    CHECK(path.empty());

    path = L"D:\\stale\\leftover.bin";
    CHECK(store.Query(kMd5A, "other.zip", path) == false);
    CHECK(path.empty());
}

TEST_CASE("FLS4 同一 md5 的不同文件名各自记账")
{
    CFileLocalStore& store = FreshStore();

    //同一份内容被存成两个名字，各自的落点应分别记住
    REQUIRE(store.Upsert(kMd5A, "report.zip", L"D:\\work\\report.zip"));
    REQUIRE(store.Upsert(kMd5A, "report-copy.zip", L"E:\\backup\\report-copy.zip"));

    std::wstring p1;
    std::wstring p2;
    REQUIRE(store.Query(kMd5A, "report.zip", p1));
    REQUIRE(store.Query(kMd5A, "report-copy.zip", p2));
    CHECK(p1 == L"D:\\work\\report.zip");
    CHECK(p2 == L"E:\\backup\\report-copy.zip");
}

TEST_CASE("FLS5 参数非法一律拒绝写入")
{
    CFileLocalStore& store = FreshStore();

    //三者缺一这条登记都查不回来，与其写进去不如直接拒绝
    CHECK(store.Upsert("", "report.zip", L"D:\\work\\report.zip") == false);
    CHECK(store.Upsert(kMd5A, "", L"D:\\work\\report.zip") == false);
    CHECK(store.Upsert(kMd5A, "report.zip", L"") == false);

    //非法键的查询同样安全返回
    std::wstring path;
    CHECK(store.Query("", "report.zip", path) == false);
    CHECK(store.Query(kMd5A, "", path) == false);
}

TEST_CASE("FLS6 Remove 删掉登记且幂等")
{
    CFileLocalStore& store = FreshStore();
    REQUIRE(store.Upsert(kMd5A, "report.zip", L"D:\\work\\report.zip"));

    REQUIRE(store.Remove(kMd5A, "report.zip"));

    std::wstring path;
    CHECK(store.Query(kMd5A, "report.zip", path) == false);

    //再删一次不该报错（无匹配行也算成功）
    CHECK(store.Remove(kMd5A, "report.zip"));
}

TEST_CASE("FLS7 库未打开时读写不崩")
{
    CFileLocalStore& store = CFileLocalStore::GetInstance();
    store.Close();
    REQUIRE(store.IsOpen() == false);

    CHECK(store.Upsert(kMd5A, "report.zip", L"D:\\work\\report.zip") == false);

    std::wstring path = L"D:\\stale\\leftover.bin";
    CHECK(store.Query(kMd5A, "report.zip", path) == false);
    CHECK(path.empty());

    CHECK(store.Remove(kMd5A, "report.zip") == false);
}
