/**
 *  「屏幕共享清晰度」设置的单元测试（标签 [sharequality]），ShareQualitySettingTests.cpp
 *
 *  背景：docs/远程桌面H264与共享画质提升方案.md 4.2、6.1（2026-10-15）。发起人在系统设置「音视频」页选 1080p / 2K（默认）/ 4K，
 *  存进用户配置 [Settings] ShareQuality（0 / 1 / 2），登录后与改了设置后经 IMediaModule::SetShareQuality 交给 FlamingoMedia，
 *  由共享控制器交给主持人与共享者两条推流管线（ShareHostStreamer::SetQuality），推流中修改时下一帧按新的长边上限重建编码器
 *  （编码器一侧由 [sharehw] 的 SHSQ1 覆盖）。
 *    - SYS-SQ1 配置项：缺键时为 2K；0 / 2 写盘再读回不变；损坏的取值按 2K。设置页：「音视频」页有「屏幕共享清晰度」一行，
 *              三个选项按 1080p、2K、4K 排列，初值取自配置，改选后写进配置并交给 FlamingoMedia（读源码检查，设置窗没有编进本工程）；
 *    - SYS-SQ2 交给 FlamingoMedia 的路径：接口版本 18 新增 SetShareQuality，exe 登录后推送一次，模块交给共享控制器，
 *              控制器交给两条推流管线（读源码检查）。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-15
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <windows.h>
#include <string>
#include "../Session/UserConfig.h"

namespace {

//从本文件路径往上退几级到 flamingoclient 目录（Source\Tests\本文件）
const int kSqDepth = 3;
//读文件的块大小，单位字节
const size_t kSqReadChunk = 4096;

//本进程专用的临时配置文件路径，并删掉上一次残留的同名文件
std::wstring SqFreshIni(const wchar_t* tag)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t name[MAX_PATH] = { 0 };
    swprintf_s(name, L"%sflamingo_sharequality_%s_%lu.ini", dir, tag, ::GetCurrentProcessId());
    ::DeleteFileW(name);
    return name;
}

//flamingoclient 目录（依赖本文件按 /FC 编译，__FILE__ 为完整路径）
std::string SqClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kSqDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

//读一个源文件的全部内容；读不到时返回空串
std::string SqReadSource(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kSqReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

//取一个函数的函数体：从定义开始，到其后第一个位于行首的右花括号为止
std::string SqFunctionBody(const std::string& source, const std::string& signature)
{
    const size_t begin = source.find(signature);
    if (begin == std::string::npos)
        return std::string();
    const size_t end = source.find("\n}", begin);
    return source.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

bool SqHas(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("SYS-SQ1 屏幕共享清晰度：配置缺键时为 2K，三档写盘读回不变，设置页有这一行并交给 FlamingoMedia", "[sharequality]")
{
    //配置项：缺键时为 2K（1）
    const std::wstring path = SqFreshIni(L"sq1");
    {
        CUserConfig fresh;
        REQUIRE(fresh.LoadConfig(path.c_str()));
        CHECK(fresh.GetShareQuality() == 1);
    }
    //0（1080p）与 2（4K）写盘再读回不变
    const long values[] = { 0, 2, 1 };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        CAPTURE(values[i]);
        CUserConfig writer;
        REQUIRE(writer.LoadConfig(path.c_str()));
        writer.SetShareQuality(values[i]);
        REQUIRE(writer.SaveConfig(path.c_str()));
        CUserConfig reader;
        REQUIRE(reader.LoadConfig(path.c_str()));
        CHECK(reader.GetShareQuality() == values[i]);
    }
    //损坏的取值（手改配置文件）：按 2K
    ::WritePrivateProfileStringW(L"Settings", L"ShareQuality", L"7", path.c_str());
    {
        CUserConfig odd;
        REQUIRE(odd.LoadConfig(path.c_str()));
        CHECK(odd.GetShareQuality() == 1);
    }
    //Set 时同样只接受 0~2
    {
        CUserConfig cfg;
        cfg.SetShareQuality(-3);
        CHECK(cfg.GetShareQuality() == 1);
    }
    ::DeleteFileW(path.c_str());

    //设置页：「音视频」页的「屏幕共享清晰度」一行（读源码）
    const std::string root = SqClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string dlg = SqReadSource(root + "\\Source\\UI\\BalloonSettings\\BalloonSystemSettingDlg.cpp");
    REQUIRE_FALSE(dlg.empty());
    CHECK(SqHas(dlg, "kIdShareQuality"));
    //三个选项按 1080p、2K、4K 排列，与配置取值 0 / 1 / 2 一一对应
    const size_t options = dlg.find("kShareQualityOptions[] =");
    REQUIRE(options != std::string::npos);
    const std::string optionLine = dlg.substr(options, dlg.find('\n', options) - options);
    const size_t p1080 = optionLine.find("1080p");
    const size_t p2k = optionLine.find("2K");
    const size_t p4k = optionLine.find("4K");
    CHECK(p1080 != std::string::npos);
    CHECK(p2k != std::string::npos);
    CHECK(p4k != std::string::npos);
    CHECK(p1080 < p2k);
    CHECK(p2k < p4k);
    //初值取自配置；改选后写进配置并交给 FlamingoMedia
    CHECK(SqHas(dlg, "cfg->GetShareQuality()"));
    const size_t handler = dlg.find("case kIdShareQuality:");
    REQUIRE(handler != std::string::npos);
    const std::string branch = dlg.substr(handler, dlg.find("break;", handler) - handler);
    CHECK(SqHas(branch, "SetShareQuality("));
    CHECK(SqHas(branch, "PushShareQuality("));
}

TEST_CASE("SYS-SQ2 清晰度经接口版本 18 的 SetShareQuality 交给共享控制器与两条推流管线，登录后推送一次", "[sharequality]")
{
    const std::string root = SqClientRoot();
    REQUIRE_FALSE(root.empty());
    //接口：版本 18，新增 SetShareQuality
    const std::string api = SqReadSource(root + "\\FlamingoMedia\\Api\\FlamingoMediaApi.h");
    REQUIRE_FALSE(api.empty());
    CHECK(SqHas(api, "const uint32_t kApiVersion = 18;"));
    CHECK(SqHas(api, "virtual void SetShareQuality(int quality) = 0;"));
    //exe：入口函数交给共享中枢，中枢交给模块；登录后推送一次
    const std::string entry = SqReadSource(root + "\\Source\\Feature\\ScreenShare\\RecordEntry.cpp");
    CHECK(SqHas(SqFunctionBody(entry, "void PushShareQuality(int quality)"), "SetShareQuality(quality)"));
    const std::string hub = SqReadSource(root + "\\Source\\Feature\\ScreenShare\\ScreenShareHub.cpp");
    CHECK(SqHas(SqFunctionBody(hub, "void CScreenShareHub::SetShareQuality(int quality)"),
                "m_module->SetShareQuality(quality)"));
    const std::string session = SqReadSource(root + "\\Source\\UI\\BalloonMain\\MainPanelSessionController.cpp");
    CHECK(SqHas(session, "PushShareQuality((int)m_lpFMGClient->m_UserConfig.GetShareQuality())"));
    //DLL：模块交给共享控制器，控制器交给主持人与共享者两条推流管线
    const std::string module = SqReadSource(root + "\\FlamingoMedia\\Host\\MediaModule.cpp");
    CHECK(SqHas(SqFunctionBody(module, "void CMediaModule::SetShareQuality(int quality)"), "m_share->SetQuality(quality)"));
    const std::string ctl = SqReadSource(root + "\\FlamingoMedia\\Share\\ShareController.cpp");
    const std::string setQuality = SqFunctionBody(ctl, "void CShareController::SetQuality(int quality)");
    CHECK(SqHas(setQuality, "m_streamer.SetQuality(quality)"));
    CHECK(SqHas(setQuality, "m_publisher.SetQuality(quality)"));
}
