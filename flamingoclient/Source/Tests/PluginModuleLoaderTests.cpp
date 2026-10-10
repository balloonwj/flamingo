/**
 *  功能模块 DLL 通用加载器单元测试（Suite PML，标签 [pluginloader]），PluginModuleLoaderTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 2 步，2026-10-02 与用户确认的用例）：
 *    - PML1 传空路径、空工厂函数名、不存在的路径；
 *    - PML2 加载一个不是本模块的系统 DLL（version.dll），不留下已加载状态；
 *    - PML5 每种加载结果都有对应的英文描述，不认识的值返回 "unknown"；
 *    - PML6 插件文件存在但不是 DLL：按「无法加载」处理，加载前后线程的错误模式不变（2026-10-04 补）。
 *  PML3（接口版本不一致）与 PML4（正常加载卸载两轮）需要真实加载 FlamingoFileShare.dll，
 *  放在 FileShareModuleTests.cpp。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <stdio.h>   // PML6：写一个不是 DLL 的临时文件

#include "../App/PluginModuleLoader.h"

using namespace balloonapp;

namespace {

//失败路径的用例用不到真实的模块接口，以一个空结构体充当模板参数
struct DummyModule
{
};

//一个任意的接口版本号；失败路径上工厂函数根本不会被调用，取值无关紧要
const uint32_t kDummyApiVersion = 1;

//一个任意的工厂函数名，系统 DLL 里不会有这个导出
const char* const kDummyFactoryName = "FlamingoDummy_Create";

//DescribeResult 遇到不认识的取值时应返回的固定文字
const char* const kUnknownDescription = "unknown";

//一个不属于 PluginLoadResult 的取值，用来检查 DescribeResult 的兜底分支
const int kInvalidLoadResult = 99;

//测试程序所在目录（不带结尾反斜杠）
std::wstring ExeDir()
{
    wchar_t path[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring dir(path);
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        dir.erase(pos);
    return dir;
}

} // namespace

TEST_CASE("PML1 传空路径、空工厂函数名、不存在的路径", "[pluginloader]")
{
    CPluginModuleLoader<DummyModule> loader;

    CHECK(loader.Load(NULL, kDummyFactoryName, kDummyApiVersion) == plugin_load_bad_path);
    CHECK(loader.Load(L"", kDummyFactoryName, kDummyApiVersion) == plugin_load_bad_path);
    CHECK_FALSE(loader.IsLoaded());

    std::wstring missing = ExeDir() + L"\\NoSuchDir_pml\\NoSuchModule.dll";
    CHECK(loader.Load(missing.c_str(), NULL, kDummyApiVersion) == plugin_load_bad_path);
    CHECK(loader.Load(missing.c_str(), "", kDummyApiVersion) == plugin_load_bad_path);
    CHECK(loader.Load(missing.c_str(), kDummyFactoryName, kDummyApiVersion) == plugin_load_not_found);
    CHECK_FALSE(loader.IsLoaded());
    CHECK(loader.GetModule() == NULL);

    //卸载未加载的加载器是安全的，重复卸载也是
    loader.Unload();
    loader.Unload();
    CHECK_FALSE(loader.IsLoaded());
}

TEST_CASE("PML2 加载不是本模块的系统 DLL", "[pluginloader]")
{
    wchar_t sysDir[MAX_PATH] = { 0 };
    REQUIRE(::GetSystemDirectoryW(sysDir, MAX_PATH) > 0);
    std::wstring path = std::wstring(sysDir) + L"\\version.dll";

    //底层的装卸层：找不到导出函数时出参置空、不留下已加载状态
    CPluginLibrary library;
    FARPROC entry = reinterpret_cast<FARPROC>(1);
    CHECK(library.Load(path.c_str(), kDummyFactoryName, entry) == plugin_load_entry_missing);
    CHECK(entry == NULL);
    CHECK_FALSE(library.IsLoaded());

    //模板外壳：同样返回「缺少导出函数」，且模块指针为空
    CPluginModuleLoader<DummyModule> loader;
    CHECK(loader.Load(path.c_str(), kDummyFactoryName, kDummyApiVersion) == plugin_load_entry_missing);
    CHECK_FALSE(loader.IsLoaded());
    CHECK(loader.GetModule() == NULL);
}

TEST_CASE("PML5 每种加载结果都有对应的英文描述", "[pluginloader]")
{
    const PluginLoadResult all[] =
    {
        plugin_load_ok,
        plugin_load_bad_path,
        plugin_load_not_found,
        plugin_load_failed,
        plugin_load_entry_missing,
        plugin_load_version_mismatch,
    };
    const size_t count = sizeof(all) / sizeof(all[0]);

    for (size_t i = 0; i < count; ++i)
    {
        std::string text = CPluginLibrary::DescribeResult(all[i]);
        CHECK_FALSE(text.empty());
        CHECK(text != kUnknownDescription);

        //各条描述互不相同，日志里才分得清是哪一种失败
        for (size_t j = i + 1; j < count; ++j)
        {
            CHECK(text != std::string(CPluginLibrary::DescribeResult(all[j])));
        }
    }

    CHECK(std::string(CPluginLibrary::DescribeResult(static_cast<PluginLoadResult>(kInvalidLoadResult)))
          == kUnknownDescription);
}

TEST_CASE("PML6 插件文件存在但不是 DLL：按无法加载处理，不改变线程的错误模式", "[pluginloader]")
{
    //2026-10-04 起加载期间屏蔽系统的「损坏的映像」对话框。测试进程已整体屏蔽错误框
    //（TestCrashDialogSetup.cpp），这里只核对返回值与错误模式的恢复；不弹框由 tmp/i18n_phase5/fallback_check.ps1
    //对真实程序做端到端检查
    const std::wstring path = ExeDir() + L"\\FlamingoPml6NotDll.dll";
    FILE* fp = NULL;
    REQUIRE(_wfopen_s(&fp, path.c_str(), L"wb") == 0);
    fputs("this is not a dll", fp);
    fclose(fp);

    const DWORD modeBefore = ::GetThreadErrorMode();
    CPluginLibrary library;
    FARPROC entry = reinterpret_cast<FARPROC>(1);
    const PluginLoadResult result = library.Load(path.c_str(), kDummyFactoryName, entry);
    const DWORD modeAfter = ::GetThreadErrorMode();
    ::DeleteFileW(path.c_str());

    CHECK(result == plugin_load_failed);
    CHECK(entry == NULL);
    CHECK_FALSE(library.IsLoaded());
    CHECK(modeAfter == modeBefore);
}
