/**
 *  CaptionHostTests.cpp
 *
 *  语音字幕宿主纯逻辑（Feature/Caption/CaptionHostPolicy）的单元测试，标签 [captionhost]：
 *    CAPH1 —— 字幕能否使用：已登录、企业开关打开、DLL 没有加载失败过，三项缺一不可；
 *    CAPH2 —— 该不该加载 DLL：能使用且尚未加载时才加载；
 *    CAPH3 —— 设置文件路径：个人目录带不带结尾反斜杠结果相同，个人目录为空时为空串；
 *    CAPH4 —— 设置文件读写：写后读回一致；带 BOM 的文件读出时去掉 BOM；不存在的文件读失败；超过上限的文件不读。
 *
 *  CAPH4 在 E:\flamingo\tmp 下（取不到时用系统临时目录）建临时文件，用完删除。
 *
 *  balloonwj@qq.com   2026-10-06
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>

#include <string>

#include "../Feature/Caption/CaptionHostPolicy.h"

using namespace captionhost;

namespace {

//临时目录：优先用仓库的 tmp 目录（与本机其它测试的临时文件放在一起），没有时用系统临时目录
std::wstring TempDir()
{
    const wchar_t* const kRepoTmp = L"E:\\flamingo\\tmp\\";
    const DWORD attr = ::GetFileAttributesW(kRepoTmp);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return kRepoTmp;
    wchar_t buf[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, buf);
    return buf;
}

//按字节原样写一个文件
bool WriteRaw(const std::wstring& path, const std::string& bytes)
{
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const BOOL ok = bytes.empty() ? TRUE
                                  : ::WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, NULL);
    ::CloseHandle(file);
    return ok != FALSE;
}

} // namespace

TEST_CASE("CAPH1 字幕能否使用：已登录、企业开关打开、DLL 没有加载失败过，三项缺一不可", "[captionhost]")
{
    CaptionGateState s;
    CHECK_FALSE(IsCaptionAvailable(s));
    s.m_loggedIn = true;
    CHECK_FALSE(IsCaptionAvailable(s));
    s.m_featureEnabled = true;
    CHECK(IsCaptionAvailable(s));
    //管理员关闭开关
    s.m_featureEnabled = false;
    CHECK_FALSE(IsCaptionAvailable(s));
    s.m_featureEnabled = true;
    //退出登录
    s.m_loggedIn = false;
    CHECK_FALSE(IsCaptionAvailable(s));
    s.m_loggedIn = true;
    //DLL 加载失败过：本进程不再可用（D6）
    s.m_loadFailed = true;
    CHECK_FALSE(IsCaptionAvailable(s));
}

TEST_CASE("CAPH2 该不该加载 DLL：能使用且尚未加载时才加载", "[captionhost]")
{
    CaptionGateState s;
    s.m_loggedIn = true;
    s.m_featureEnabled = true;
    CHECK(ShouldLoadCaptionDll(s, false));
    CHECK_FALSE(ShouldLoadCaptionDll(s, true));
    s.m_featureEnabled = false;
    CHECK_FALSE(ShouldLoadCaptionDll(s, false));
    s.m_featureEnabled = true;
    s.m_loadFailed = true;
    CHECK_FALSE(ShouldLoadCaptionDll(s, false));
}

TEST_CASE("CAPH3 设置文件路径：个人目录带不带结尾反斜杠结果相同，个人目录为空时为空串", "[captionhost]")
{
    CHECK(SettingsFilePath(L"C:\\Users\\a\\Flamingo\\zhang\\") == L"C:\\Users\\a\\Flamingo\\zhang\\caption_settings.json");
    CHECK(SettingsFilePath(L"C:\\Users\\a\\Flamingo\\zhang") == L"C:\\Users\\a\\Flamingo\\zhang\\caption_settings.json");
    CHECK(SettingsFilePath(L"").empty());
}

TEST_CASE("CAPH4 设置文件读写：写后读回一致；去掉 BOM；不存在的文件读失败；超过上限的文件不读", "[captionhost]")
{
    const std::wstring dir = TempDir();
    const std::wstring path = dir + L"caph4_caption_settings.json";
    ::DeleteFileW(path.c_str());

    //不存在
    std::string text = "stale";
    CHECK_FALSE(ReadSettingsFile(path, text));
    CHECK(text.empty());

    //写后读回（含中文替换规则的 UTF-8 字节）
    const std::string json = "{\"replace\":[{\"from\":\"Sclight\",\"to\":\"SQLite\"},{\"from\":\"\xE7\x81\xAB\xE7\x83\x88\",\"to\":\"\xE7\x81\xAB\xE7\x83\x88\xE9\xB8\x9F\"}]}";
    REQUIRE(WriteSettingsFile(path, json));
    REQUIRE(ReadSettingsFile(path, text));
    CHECK(text == json);
    //临时文件已改名，不残留
    CHECK(::GetFileAttributesW((path + L".tmp").c_str()) == INVALID_FILE_ATTRIBUTES);

    //覆盖写：内容变短也不残留旧内容
    REQUIRE(WriteSettingsFile(path, "{}"));
    REQUIRE(ReadSettingsFile(path, text));
    CHECK(text == "{}");

    //带 BOM 的文件（用户用记事本改过）
    REQUIRE(WriteRaw(path, std::string("\xEF\xBB\xBF{\"a\":1}")));
    REQUIRE(ReadSettingsFile(path, text));
    CHECK(text == "{\"a\":1}");

    //空文件：读成功、内容为空（按默认设置）
    REQUIRE(WriteRaw(path, std::string()));
    REQUIRE(ReadSettingsFile(path, text));
    CHECK(text.empty());

    //超过上限：视为损坏，不读
    REQUIRE(WriteRaw(path, std::string(kCaptionSettingsMaxBytes + 1, ' ')));
    CHECK_FALSE(ReadSettingsFile(path, text));
    CHECK(text.empty());

    ::DeleteFileW(path.c_str());
    CHECK_FALSE(WriteSettingsFile(L"", "{}"));
    CHECK_FALSE(ReadSettingsFile(L"", text));
}
