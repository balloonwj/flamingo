/**
 *  文件夹本地落点登记库读写单元测试（Suite FLR），FolderLocalStoreTests.cpp
 *
 *  用内存库（:memory:）驱动 balloonhistory::CFolderLocalStore，覆盖：
 *    FLR1 登记后能按会话 id 查回同一根目录；
 *    FLR2 同一会话重复登记覆盖为新路径（接收端应答时只知道父目录、收尾时才知道
 *         自动改名后的最终目录，后者须胜出）；
 *    FLR3 未登记的会话查不到，且出参被清空（不留上一次的残值）——"换了台机器只显示
 *         记录、不给「打开文件夹」"就落在这条上；
 *    FLR4 不同会话各自记账，互不覆盖（同一个人先后发来两个文件夹）；
 *    FLR5 参数非法（空会话 id / 空路径）一律拒绝写入；
 *    FLR6 Remove 删掉登记后查不到，且重复删除是幂等的；
 *    FLR7 库未打开时读写不崩、一律返回失败。
 *
 *  注：CFolderLocalStore 是单例，本套件各用例通过重新 Open(:memory:) 拿到干净的库
 *  ——同名重开会先关掉旧连接，内存库随之丢弃，等价于清空。写法与同目录的
 *  FileLocalStoreTests（Suite FLS）一致。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-14
 */
#include "catch_amalgamated.hpp"
#include "../BalloonHistory/FolderLocalStore.h"

using namespace balloonhistory;

namespace {

//取一个干净的内存库：先关再开，确保不带上一个用例的数据。
static CFolderLocalStore& FreshFolderStore()
{
    CFolderLocalStore& store = CFolderLocalStore::GetInstance();
    store.Close();
    REQUIRE(store.Open(L":memory:"));
    return store;
}

//两个互不相同的会话 id，模拟同一个人先后发来的两个文件夹。
const char* const kSessionA = "folder-sess-1001";
const char* const kSessionB = "folder-sess-1002";

} // namespace

TEST_CASE("FLR1 登记后能按会话 id 查回根目录")
{
    CFolderLocalStore& store = FreshFolderStore();
    REQUIRE(store.Upsert(kSessionA, L"D:\\Work\\项目资料"));

    std::wstring path;
    REQUIRE(store.Query(kSessionA, path));
    CHECK(path == L"D:\\Work\\项目资料");
}

TEST_CASE("FLR2 同一会话重复登记覆盖为新路径")
{
    CFolderLocalStore& store = FreshFolderStore();
    //接收端应答时先记下父目录下的预期位置
    REQUIRE(store.Upsert(kSessionA, L"D:\\Downloads\\项目资料"));
    //收尾时才知道根目录重名被自动改成了 项目资料(2)，登记须跟着走
    REQUIRE(store.Upsert(kSessionA, L"D:\\Downloads\\项目资料(2)"));

    std::wstring path;
    REQUIRE(store.Query(kSessionA, path));
    CHECK(path == L"D:\\Downloads\\项目资料(2)");
}

TEST_CASE("FLR3 未登记的会话查不到，且出参被清空")
{
    CFolderLocalStore& store = FreshFolderStore();
    REQUIRE(store.Upsert(kSessionA, L"D:\\Work\\a"));

    //出参先塞一个残值，验证未命中时会被清掉 —— 调用方据"返回 false"判定，
    //但残值留着迟早有人只看出参不看返回值
    std::wstring path = L"残值";
    CHECK_FALSE(store.Query(kSessionB, path));
    CHECK(path.empty());
}

TEST_CASE("FLR4 不同会话各自记账，互不覆盖")
{
    CFolderLocalStore& store = FreshFolderStore();
    REQUIRE(store.Upsert(kSessionA, L"D:\\Work\\一期资料"));
    REQUIRE(store.Upsert(kSessionB, L"E:\\Archive\\二期资料"));

    std::wstring pathA;
    std::wstring pathB;
    REQUIRE(store.Query(kSessionA, pathA));
    REQUIRE(store.Query(kSessionB, pathB));
    CHECK(pathA == L"D:\\Work\\一期资料");
    CHECK(pathB == L"E:\\Archive\\二期资料");
}

TEST_CASE("FLR5 参数非法一律拒绝写入")
{
    CFolderLocalStore& store = FreshFolderStore();
    //空会话 id：这条登记查不回来
    CHECK_FALSE(store.Upsert("", L"D:\\Work\\a"));
    //空路径：查回来是空串，与"没查到"无从区分
    CHECK_FALSE(store.Upsert(kSessionA, L""));

    std::wstring path;
    CHECK_FALSE(store.Query("", path));
    CHECK_FALSE(store.Query(kSessionA, path));
}

TEST_CASE("FLR6 Remove 删掉登记后查不到，重复删除幂等")
{
    CFolderLocalStore& store = FreshFolderStore();
    REQUIRE(store.Upsert(kSessionA, L"D:\\Work\\a"));

    CHECK(store.Remove(kSessionA));
    std::wstring path;
    CHECK_FALSE(store.Query(kSessionA, path));

    //无匹配行时同样算成功：删除是幂等的
    CHECK(store.Remove(kSessionA));
    //空会话 id 不是"无匹配行"而是参数非法，仍返回 false
    CHECK_FALSE(store.Remove(""));
}

TEST_CASE("FLR7 库未打开时读写一律失败且不崩")
{
    CFolderLocalStore& store = CFolderLocalStore::GetInstance();
    store.Close();
    CHECK_FALSE(store.IsOpen());

    CHECK_FALSE(store.Upsert(kSessionA, L"D:\\Work\\a"));
    std::wstring path;
    CHECK_FALSE(store.Query(kSessionA, path));
    CHECK_FALSE(store.Remove(kSessionA));

    //空指针路径同样安全失败
    CHECK_FALSE(store.Open(NULL));
}
