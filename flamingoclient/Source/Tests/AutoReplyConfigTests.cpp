/**
 *  AutoReplyConfigTests.cpp
 *
 *  自动回复配置的测试：CUserConfig 的 [AutoReply] 段读写（ARS1，与用户确认过的用例，2026-10-03），
 *  以及它所用的文字编码 cfgtext::EncodeConfigText / DecodeConfigText（CTC1～CTC2，实现时新增）。
 *  用户配置文件是 ANSI 编码，文字先编码成 ASCII 再存，表情等字符才不会在系统代码页转换时丢失。
 *
 *  每条用例都用独立的临时配置文件，文件名带进程 id，两个测试进程同时跑也不会共用。
 *  用 Catch2 v3（amalgamated）作框架。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <vector>

#include "../Session/UserConfig.h"
#include "../Session/ConfigTextCodec.h"

namespace {

// 离开时的回复文字：中文、一个表情（U+1F600，代理对写出，不依赖源文件编码）与换行
const wchar_t* const kAwaySample = L"我在开会\xD83D\xDE00\r\n一小时后回复";
// 忙碌时的回复文字：中文与全角标点
const wchar_t* const kBusySample = L"正在处理紧急事务，稍后联系。";

// 读 ini 原文时用的缓冲区长度（字符数），足够放下本文件写出的编码串
const DWORD kRawValueChars = 4096;

// GetPrivateProfileString 的缺省值，用来判断某个键在文件里不存在
const wchar_t* const kMissingMark = L"<missing>";

//生成本进程专用的临时配置文件路径，并删掉上一次残留的同名文件。
//  tag：区分同一进程内不同用例的文件名片段
std::wstring FreshIniPath(const wchar_t* tag)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t name[MAX_PATH] = { 0 };
    swprintf_s(name, L"%sflamingo_autoreply_%s_%lu.ini", dir, tag, ::GetCurrentProcessId());
    ::DeleteFileW(name);
    return name;
}

//读出 ini 里某个键的原文；不存在时返回 kMissingMark
std::wstring RawValue(const std::wstring& path, const wchar_t* section, const wchar_t* key)
{
    std::vector<wchar_t> buf(kRawValueChars, L'\0');
    ::GetPrivateProfileStringW(section, key, kMissingMark, &buf[0], kRawValueChars, path.c_str());
    return std::wstring(&buf[0]);
}

//字符串是不是只含 ASCII 可打印字符
bool IsPrintableAscii(const std::wstring& s)
{
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] < L' ' || s[i] > L'~')
        {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

TEST_CASE("ARS1 两个开关与两段文字存进配置再读出一字不差，旧键不再读写", "[autoreply][autoreplycfg]")
{
    SECTION("配置里没有 [AutoReply] 段：两个开关都关闭、文字为空（发送时用默认文字）")
    {
        const std::wstring path = FreshIniPath(L"ars1a");
        CUserConfig cfg;
        REQUIRE(cfg.LoadConfig(path.c_str()));
        CHECK_FALSE(cfg.IsEnableAutoReplyAway());
        CHECK_FALSE(cfg.IsEnableAutoReplyBusy());
        CHECK(std::wstring(cfg.GetAutoReplyAwayText()).empty());
        CHECK(std::wstring(cfg.GetAutoReplyBusyText()).empty());
        ::DeleteFileW(path.c_str());
    }

    SECTION("往返：中文、表情、换行原样读回，两个开关各自独立")
    {
        const std::wstring path = FreshIniPath(L"ars1b");
        CUserConfig writer;
        REQUIRE(writer.LoadConfig(path.c_str()));
        writer.EnableAutoReplyAway(TRUE);
        writer.EnableAutoReplyBusy(FALSE);
        writer.SetAutoReplyAwayText(kAwaySample);
        writer.SetAutoReplyBusyText(kBusySample);
        REQUIRE(writer.SaveConfig(path.c_str()));

        CUserConfig reader;
        REQUIRE(reader.LoadConfig(path.c_str()));
        CHECK(reader.IsEnableAutoReplyAway());
        CHECK_FALSE(reader.IsEnableAutoReplyBusy());
        CHECK(std::wstring(reader.GetAutoReplyAwayText()) == kAwaySample);
        CHECK(std::wstring(reader.GetAutoReplyBusyText()) == kBusySample);

        //两个开关反过来再存一次
        reader.EnableAutoReplyAway(FALSE);
        reader.EnableAutoReplyBusy(TRUE);
        REQUIRE(reader.SaveConfig(path.c_str()));
        CUserConfig again;
        REQUIRE(again.LoadConfig(path.c_str()));
        CHECK_FALSE(again.IsEnableAutoReplyAway());
        CHECK(again.IsEnableAutoReplyBusy());
        CHECK(std::wstring(again.GetAutoReplyAwayText()) == kAwaySample);

        //写进文件的只有 ASCII：文字经过编码，不受系统代码页影响
        CHECK(IsPrintableAscii(RawValue(path, L"AutoReply", L"AwayText")));
        CHECK(IsPrintableAscii(RawValue(path, L"AutoReply", L"BusyText")));
        ::DeleteFileW(path.c_str());
    }

    SECTION("旧键 [App] AutoReply / AutoReplyContent：不读，存盘时也不写")
    {
        const std::wstring path = FreshIniPath(L"ars1c");
        ::WritePrivateProfileStringW(L"App", L"AutoReply", L"1", path.c_str());
        ::WritePrivateProfileStringW(L"App", L"AutoReplyContent", L"old", path.c_str());

        CUserConfig cfg;
        REQUIRE(cfg.LoadConfig(path.c_str()));
        CHECK_FALSE(cfg.IsEnableAutoReplyAway());
        CHECK_FALSE(cfg.IsEnableAutoReplyBusy());
        CHECK(std::wstring(cfg.GetAutoReplyAwayText()).empty());

        //新文件存盘后不出现这两个键
        const std::wstring fresh = FreshIniPath(L"ars1c_fresh");
        REQUIRE(cfg.SaveConfig(fresh.c_str()));
        CHECK(RawValue(fresh, L"App", L"AutoReply") == kMissingMark);
        CHECK(RawValue(fresh, L"App", L"AutoReplyContent") == kMissingMark);
        ::DeleteFileW(path.c_str());
        ::DeleteFileW(fresh.c_str());
    }

    SECTION("编码串被手工改坏：按没有设置处理，文字为空")
    {
        const std::wstring path = FreshIniPath(L"ars1d");
        ::WritePrivateProfileStringW(L"AutoReply", L"AwayEnabled", L"1", path.c_str());
        ::WritePrivateProfileStringW(L"AutoReply", L"AwayText", L"not-hex", path.c_str());
        CUserConfig cfg;
        REQUIRE(cfg.LoadConfig(path.c_str()));
        CHECK(cfg.IsEnableAutoReplyAway());
        CHECK(std::wstring(cfg.GetAutoReplyAwayText()).empty());
        ::DeleteFileW(path.c_str());
    }
}

TEST_CASE("CTC1 配置文字编码往返不变，编码结果只含十六进制字符", "[autoreply][autoreplycfg]")
{
    const std::wstring samples[] = { L"", L"abc", kAwaySample, kBusySample, L"\x3000\t" };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
    {
        INFO("样本 index = " << i);
        const std::wstring encoded = cfgtext::EncodeConfigText(samples[i]);
        CHECK(encoded.find_first_not_of(L"0123456789abcdef") == std::wstring::npos);
        std::wstring decoded;
        REQUIRE(cfgtext::DecodeConfigText(encoded, decoded));
        CHECK(decoded == samples[i]);
    }
    //UTF-8 按字节写成两位小写十六进制：「我」是 E6 88 91
    CHECK(cfgtext::EncodeConfigText(L"\x6211") == L"e68891");
}

TEST_CASE("CTC2 编码串不合法时解码失败、出参清空", "[autoreply][autoreplycfg]")
{
    std::wstring out = L"leftover";
    //长度为奇数
    CHECK_FALSE(cfgtext::DecodeConfigText(L"e6889", out));
    CHECK(out.empty());
    //含十六进制以外的字符
    out = L"leftover";
    CHECK_FALSE(cfgtext::DecodeConfigText(L"zz", out));
    CHECK(out.empty());
    //字节不是合法 UTF-8（单独的续字节）
    out = L"leftover";
    CHECK_FALSE(cfgtext::DecodeConfigText(L"80", out));
    CHECK(out.empty());
    //大写十六进制也认
    CHECK(cfgtext::DecodeConfigText(L"E68891", out));
    CHECK(out == L"\x6211");
}
