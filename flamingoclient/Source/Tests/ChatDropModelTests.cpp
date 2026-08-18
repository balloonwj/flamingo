/**
 *  拖放落点归类单元测试（Suite CDM），ChatDropModelTests.cpp
 *
 *  覆盖 balloonchatwnd::BuildDropPlan / IsImageDropPath / ProbeDropPath：
 *    CDM1  单个普通文件走文件链路；
 *    CDM2  单张图片走图片链路；
 *    CDM3  图片与文件混拖时各归各类、且顺序与拖入顺序一致；
 *    CDM4  按住 Shift（forceAsFile）时图片也走文件链路；
 *    CDM5  文件夹归入 m_folders（单独一类落点，不再算跳过项）；
 *    CDM6  路径不存在归入跳过项；
 *    CDM7  空列表产出空计划且不要确认；
 *    CDM8  可发送项超过阈值时要确认，恰好等于阈值时不要；
 *    CDM9  0 字节文件归入跳过项；
 *    CDM10 扩展名大小写不敏感，且目录名里的点号不算扩展名；
 *    CDM11 ProbeDropPath 对真实文件 / 目录 / 不存在路径的判定（本条碰磁盘）；
 *    CDM12 只拖一个文件夹：进 m_folders，其余两个列表都空；
 *    CDM13 拖多个文件夹：全部按拖入顺序进 m_folders；
 *    CDM14 文件与文件夹混拖：各归各类、两侧顺序都不乱、互不影响；
 *    CDM15 文件夹与不存在的路径混拖：文件夹进 m_folders、失效路径进跳过项，
 *          且汇总文案里不再出现"文件夹"字样。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-11
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonChatWnd/ChatDropModel.h"

#include <windows.h>
#include <string>
#include <vector>

using namespace balloonchatwnd;

namespace {

//造一条"存在的普通文件"记录，省得每条用例都手填四个字段。
DropPathInfo MakeFile(const std::wstring& path, long long size = 1024)
{
    DropPathInfo info;
    info.m_path       = path;
    info.m_exists     = true;
    info.m_isDirectory = false;
    info.m_sizeBytes  = size;
    return info;
}

//造一条"存在的目录"记录。
DropPathInfo MakeDir(const std::wstring& path)
{
    DropPathInfo info;
    info.m_path        = path;
    info.m_exists      = true;
    info.m_isDirectory = true;
    info.m_sizeBytes   = 0;
    return info;
}

//造一条"路径不存在"记录。
DropPathInfo MakeMissing(const std::wstring& path)
{
    DropPathInfo info;
    info.m_path = path;
    return info;
}

} // namespace

TEST_CASE("CDM1 单个普通文件走文件链路")
{
    std::vector<DropPathInfo> infos;
    infos.push_back(MakeFile(L"D:\\work\\report.zip"));

    const DropPlan plan = BuildDropPlan(infos, false);

    REQUIRE(plan.m_items.size() == 1);
    CHECK(plan.m_items[0].m_path == L"D:\\work\\report.zip");
    CHECK(plan.m_items[0].m_kind == kDropSendFile);
    CHECK(plan.m_skipped.empty());
    CHECK_FALSE(plan.m_needConfirm);
}

TEST_CASE("CDM2 单张图片走图片链路")
{
    std::vector<DropPathInfo> infos;
    infos.push_back(MakeFile(L"D:\\pic\\shot.png"));

    const DropPlan plan = BuildDropPlan(infos, false);

    REQUIRE(plan.m_items.size() == 1);
    CHECK(plan.m_items[0].m_kind == kDropSendImage);
}

TEST_CASE("CDM3 图片与文件混拖时各归各类且保持拖入顺序")
{
    //顺序要保住——用户一次拖三个进来，期望消息也按这个顺序出现
    std::vector<DropPathInfo> infos;
    infos.push_back(MakeFile(L"D:\\a.txt"));
    infos.push_back(MakeFile(L"D:\\b.jpg"));
    infos.push_back(MakeFile(L"D:\\c.pdf"));

    const DropPlan plan = BuildDropPlan(infos, false);

    REQUIRE(plan.m_items.size() == 3);
    CHECK(plan.m_items[0].m_path == L"D:\\a.txt");
    CHECK(plan.m_items[0].m_kind == kDropSendFile);
    CHECK(plan.m_items[1].m_path == L"D:\\b.jpg");
    CHECK(plan.m_items[1].m_kind == kDropSendImage);
    CHECK(plan.m_items[2].m_path == L"D:\\c.pdf");
    CHECK(plan.m_items[2].m_kind == kDropSendFile);
}

TEST_CASE("CDM4 forceAsFile 时图片也走文件链路")
{
    //按住 Shift 拖入：用户想把图片当原文件发（保原图、不走图片压缩那条路）
    std::vector<DropPathInfo> infos;
    infos.push_back(MakeFile(L"D:\\pic\\shot.png"));
    infos.push_back(MakeFile(L"D:\\a.txt"));

    const DropPlan plan = BuildDropPlan(infos, true);

    REQUIRE(plan.m_items.size() == 2);
    CHECK(plan.m_items[0].m_kind == kDropSendFile);
    CHECK(plan.m_items[1].m_kind == kDropSendFile);
}

TEST_CASE("CDM5 文件夹归入 m_folders 而不是跳过项")
{
    std::vector<DropPathInfo> infos;
    infos.push_back(MakeDir(L"D:\\work\\素材"));
    infos.push_back(MakeFile(L"D:\\work\\report.zip"));

    const DropPlan plan = BuildDropPlan(infos, false);

    //文件夹交给文件夹传输链路，不再算"发不出去"
    REQUIRE(plan.m_folders.size() == 1);
    CHECK(plan.m_folders[0] == L"D:\\work\\素材");
    CHECK(plan.m_skipped.empty());
    //同一批里的普通文件照旧走文件链路，两条链路互不影响
    REQUIRE(plan.m_items.size() == 1);
    CHECK(plan.m_items[0].m_path == L"D:\\work\\report.zip");
}

TEST_CASE("CDM6 路径不存在归入跳过项")
{
    //拖动过程中文件被移走 / 删掉，或来源本就是虚拟项
    std::vector<DropPathInfo> infos;
    infos.push_back(MakeMissing(L"D:\\gone.txt"));

    const DropPlan plan = BuildDropPlan(infos, false);

    CHECK(plan.m_items.empty());
    REQUIRE(plan.m_skipped.size() == 1);
    CHECK(plan.m_skipped[0].m_reason == kDropSkipNotFound);
}

TEST_CASE("CDM7 空列表产出空计划且不要确认")
{
    const std::vector<DropPathInfo> infos;

    const DropPlan plan = BuildDropPlan(infos, false);

    CHECK(plan.m_items.empty());
    CHECK(plan.m_skipped.empty());
    CHECK_FALSE(plan.m_needConfirm);
}

TEST_CASE("CDM8 可发送项超过阈值才要确认")
{
    //恰好等于阈值：不拦
    std::vector<DropPathInfo> exact;
    for (int i = 0; i < kDropConfirmThreshold; ++i)
    {
        std::wstring path = L"D:\\f";
        path += (wchar_t)(L'0' + i);
        path += L".txt";
        exact.push_back(MakeFile(path));
    }
    const DropPlan planExact = BuildDropPlan(exact, false);
    REQUIRE((int)planExact.m_items.size() == kDropConfirmThreshold);
    CHECK_FALSE(planExact.m_needConfirm);

    //多一个：要确认
    std::vector<DropPathInfo> over = exact;
    over.push_back(MakeFile(L"D:\\one-more.txt"));
    const DropPlan planOver = BuildDropPlan(over, false);
    CHECK(planOver.m_needConfirm);

    //阈值只看真正能发的那些：一堆文件夹不该把确认框顶出来
    std::vector<DropPathInfo> dirsAndOne;
    for (int i = 0; i < kDropConfirmThreshold + 5; ++i)
    {
        std::wstring path = L"D:\\dir";
        path += (wchar_t)(L'0' + i);
        dirsAndOne.push_back(MakeDir(path));
    }
    dirsAndOne.push_back(MakeFile(L"D:\\only.txt"));
    const DropPlan planDirs = BuildDropPlan(dirsAndOne, false);
    CHECK_FALSE(planDirs.m_needConfirm);
    //那些文件夹都进了 m_folders，笔数闸门由聊天窗那一层把关，与本阈值无关
    CHECK((int)planDirs.m_folders.size() == kDropConfirmThreshold + 5);
    REQUIRE(planDirs.m_items.size() == 1);
}

TEST_CASE("CDM9 零字节文件归入跳过项")
{
    //空文件在发送链路那层本来就会被拒，这里先拦下才能告诉用户原因
    std::vector<DropPathInfo> infos;
    infos.push_back(MakeFile(L"D:\\empty.txt", 0));
    infos.push_back(MakeFile(L"D:\\empty.png", 0));

    const DropPlan plan = BuildDropPlan(infos, false);

    CHECK(plan.m_items.empty());
    REQUIRE(plan.m_skipped.size() == 2);
    CHECK(plan.m_skipped[0].m_reason == kDropSkipEmpty);
    CHECK(plan.m_skipped[1].m_reason == kDropSkipEmpty);
}

TEST_CASE("CDM10 扩展名判定大小写不敏感且不误取目录名里的点号")
{
    CHECK(IsImageDropPath(L"D:\\pic\\A.PNG"));
    CHECK(IsImageDropPath(L"D:\\pic\\b.JpEg"));
    CHECK(IsImageDropPath(L"shot.gif"));
    CHECK_FALSE(IsImageDropPath(L"D:\\doc\\report.docx"));
    //目录名里带点号、文件名本身没有扩展名：不能把 "folder\readme" 当扩展名
    CHECK_FALSE(IsImageDropPath(L"D:\\my.folder\\readme"));
    //以点号结尾、没有扩展名、空串
    CHECK_FALSE(IsImageDropPath(L"D:\\weird."));
    CHECK_FALSE(IsImageDropPath(L"D:\\noext"));
    CHECK_FALSE(IsImageDropPath(L""));
    //扩展名相近但不是图片，不能误判
    CHECK_FALSE(IsImageDropPath(L"D:\\pic\\a.png.txt"));
}

TEST_CASE("CDM11 ProbeDropPath 判定真实文件 / 目录 / 不存在路径")
{
    //本条用例碰磁盘：在临时目录里造一个 3 字节文件，用完删掉
    wchar_t tempDir[MAX_PATH] = { 0 };
    const DWORD n = ::GetTempPathW(MAX_PATH, tempDir);
    REQUIRE(n > 0);

    //文件名里带上进程号，避免两份测试进程同时跑时争同一个文件
    //（同 HistoryModelTests::FreshTempDb 的做法）。
    wchar_t probeName[64] = { 0 };
    ::swprintf_s(probeName, L"flamingo_cdm_probe_%u.bin",
                 static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring filePath = tempDir;
    filePath += probeName;

    HANDLE h = ::CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    DWORD wrote = 0;
    ::WriteFile(h, "abc", 3, &wrote, NULL);
    ::CloseHandle(h);

    const DropPathInfo fileInfo = ProbeDropPath(filePath);
    CHECK(fileInfo.m_exists);
    CHECK_FALSE(fileInfo.m_isDirectory);
    CHECK(fileInfo.m_sizeBytes == 3);

    //临时目录本身是个目录
    const DropPathInfo dirInfo = ProbeDropPath(tempDir);
    CHECK(dirInfo.m_exists);
    CHECK(dirInfo.m_isDirectory);

    //不存在的路径与空串
    std::wstring missing = tempDir;
    missing += L"flamingo_cdm_no_such_file.bin";
    const DropPathInfo missingInfo = ProbeDropPath(missing);
    CHECK_FALSE(missingInfo.m_exists);
    CHECK_FALSE(ProbeDropPath(std::wstring()).m_exists);

    ::DeleteFileW(filePath.c_str());
}

TEST_CASE("CDM12 只拖一个文件夹时它是唯一落点")
{
    //最常见的一次操作：把一个目录拖进发送框，期望它走文件夹传输链路
    std::vector<DropPathInfo> infos;
    infos.push_back(MakeDir(L"D:\\work\\项目资料"));

    const DropPlan plan = BuildDropPlan(infos, false);

    REQUIRE(plan.m_folders.size() == 1);
    CHECK(plan.m_folders[0] == L"D:\\work\\项目资料");
    CHECK(plan.m_items.empty());
    CHECK(plan.m_skipped.empty());
    CHECK_FALSE(plan.m_needConfirm);
    //跳过项为空时汇总文案也应为空，聊天窗据此不弹提示
    CHECK(DescribeDropSkipped(plan.m_skipped).empty());
}

TEST_CASE("CDM13 拖多个文件夹时全部保留且顺序不乱")
{
    //单聊窗会按这个顺序逐个发起，达到本机并发笔数上限后剩下的不再发起，
    //故顺序必须与用户拖入的一致，否则"哪几个没发起"就对不上了
    std::vector<DropPathInfo> infos;
    infos.push_back(MakeDir(L"D:\\a"));
    infos.push_back(MakeDir(L"D:\\b"));
    infos.push_back(MakeDir(L"D:\\c"));

    const DropPlan plan = BuildDropPlan(infos, false);

    REQUIRE(plan.m_folders.size() == 3);
    CHECK(plan.m_folders[0] == L"D:\\a");
    CHECK(plan.m_folders[1] == L"D:\\b");
    CHECK(plan.m_folders[2] == L"D:\\c");
    CHECK(plan.m_items.empty());
    CHECK(plan.m_skipped.empty());
}

TEST_CASE("CDM14 文件与文件夹混拖时各归各类且两侧顺序都不乱")
{
    std::vector<DropPathInfo> infos;
    infos.push_back(MakeFile(L"D:\\a.txt"));
    infos.push_back(MakeDir(L"D:\\素材"));
    infos.push_back(MakeFile(L"D:\\b.png"));
    infos.push_back(MakeDir(L"D:\\备份"));

    const DropPlan plan = BuildDropPlan(infos, false);

    //文件那一侧：普通文件走文件链路、图片走图片链路，顺序与拖入一致
    REQUIRE(plan.m_items.size() == 2);
    CHECK(plan.m_items[0].m_path == L"D:\\a.txt");
    CHECK(plan.m_items[0].m_kind == kDropSendFile);
    CHECK(plan.m_items[1].m_path == L"D:\\b.png");
    CHECK(plan.m_items[1].m_kind == kDropSendImage);

    //文件夹那一侧独立成列，顺序同样保持
    REQUIRE(plan.m_folders.size() == 2);
    CHECK(plan.m_folders[0] == L"D:\\素材");
    CHECK(plan.m_folders[1] == L"D:\\备份");

    CHECK(plan.m_skipped.empty());

    //按住 Shift 只改图片的走向，文件夹照旧是文件夹
    const DropPlan forced = BuildDropPlan(infos, true);
    REQUIRE(forced.m_items.size() == 2);
    CHECK(forced.m_items[1].m_kind == kDropSendFile);
    REQUIRE(forced.m_folders.size() == 2);
    CHECK(forced.m_folders[0] == L"D:\\素材");
}

TEST_CASE("CDM15 文件夹与不存在的路径混拖时互不干扰")
{
    //拖动过程中某一项被移走 / 删掉：它进跳过项，文件夹照常发起
    std::vector<DropPathInfo> infos;
    infos.push_back(MakeMissing(L"D:\\gone.txt"));
    infos.push_back(MakeDir(L"D:\\work\\素材"));
    infos.push_back(MakeMissing(L"D:\\gone2.txt"));

    const DropPlan plan = BuildDropPlan(infos, false);

    REQUIRE(plan.m_folders.size() == 1);
    CHECK(plan.m_folders[0] == L"D:\\work\\素材");
    CHECK(plan.m_items.empty());
    REQUIRE(plan.m_skipped.size() == 2);
    CHECK(plan.m_skipped[0].m_reason == kDropSkipNotFound);
    CHECK(plan.m_skipped[1].m_reason == kDropSkipNotFound);

    //汇总文案只说失效的那两条，不能再冒出"文件夹暂不支持发送"这句旧话
    const std::wstring note = DescribeDropSkipped(plan.m_skipped);
    CHECK(note == L"2 个文件已不存在");
    CHECK(note.find(L"文件夹") == std::wstring::npos);
}
