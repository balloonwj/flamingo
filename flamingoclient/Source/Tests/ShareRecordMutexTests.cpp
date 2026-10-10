/**
 *  屏幕共享与独立录制互斥规则的单元测试（标签 [sharerecmutex]），ShareRecordMutexTests.cpp
 *  背景：2026-10-07 用户确认，独立录制与本人的屏幕共享互斥（docs/屏幕共享与录制方案.md 第 13 节）。
 *  覆盖：
 *    - SRMX1 独立录制占用中时发起共享被拦下，提示「正在录制屏幕……」；不占用时放行；
 *    - SRMX2 自己正在共享时发起独立录制被拦下，提示「正在共享屏幕……」；只是在观看（shareBusy 为假）时放行；
 *    - SRMX3 CheckScreenStartConflict 不管同类冲突（共享中再发起共享、录制中再发起录制都放行），未知种类放行，
 *      放行时没有提示文字；
 *    - SRMX4 发起共享的完整检查 CheckShareStartConflict（2026-10-07 补）：自己正在共享、正在观看别人的共享、独立录制占用中
 *      依次拦下，提示文字与原先消息框里的相同；
 *    - SRMX5 源文件守卫：CShareController::StartShare 用 CheckShareStartConflict 判断、被拦下时显示轻量提示，不再弹消息框
 *      （共享控制器依赖网络与窗口，不在单元测试工程里）。
 *  源文件位置：首选 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Share/ShareRecordMutex.h"
#include "../Common/I18n/I18n.h"

#include <stdio.h>
#include <string>

using namespace screenshare;

namespace {

//读文件时每次读的字节数
const size_t kSrmxReadChunk = 4096;
//本文件在 flamingoclient 下的层数（Source\Tests\本文件）：去掉这么多级就是 flamingoclient
const int kSrmxDepth = 3;

//读一个源文件的全部内容；读不到时返回空串
std::string SrmxReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kSrmxReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

//本测试文件所在目录往上两级就是 flamingoclient（Source\Tests\本文件）
std::string SrmxClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kSrmxDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

} // namespace

TEST_CASE("SRMX1 starting a share is blocked while a standalone recording is busy", "[sharerecmutex]")
{
    CHECK(CheckScreenStartConflict(screen_start_share, true, false) == screen_start_blocked_by_record);
    CHECK(CheckScreenStartConflict(screen_start_share, false, false) == screen_start_ok);
    CHECK(ScreenStartConflictText(screen_start_blocked_by_record) ==
          std::wstring(TR(L"正在录制屏幕，请先结束录制再发起屏幕共享")));
}

TEST_CASE("SRMX2 starting a standalone recording is blocked while sharing", "[sharerecmutex]")
{
    CHECK(CheckScreenStartConflict(screen_start_record, false, true) == screen_start_blocked_by_share);
    //只是在观看别人的共享：shareBusy 为假，不限制
    CHECK(CheckScreenStartConflict(screen_start_record, false, false) == screen_start_ok);
    CHECK(ScreenStartConflictText(screen_start_blocked_by_share) ==
          std::wstring(TR(L"正在共享屏幕，请先结束共享再开始录制")));
}

TEST_CASE("SRMX3 same-kind conflicts and unknown kinds are not handled here", "[sharerecmutex]")
{
    //共享中再发起共享、录制中再发起录制：由各控制器原有判断处理
    CHECK(CheckScreenStartConflict(screen_start_share, false, true) == screen_start_ok);
    CHECK(CheckScreenStartConflict(screen_start_record, true, false) == screen_start_ok);
    //未知种类
    const int kUnknownKind = 99;
    CHECK(CheckScreenStartConflict(kUnknownKind, true, true) == screen_start_ok);
    CHECK(ScreenStartConflictText(screen_start_ok).empty());
}

TEST_CASE("SRMX4 starting a share checks own share, watching and recording in order", "[sharerecmutex]")
{
    CHECK(CheckShareStartConflict(false, false, false) == screen_start_ok);
    CHECK(CheckShareStartConflict(true, false, false) == screen_start_blocked_by_hosting);
    CHECK(CheckShareStartConflict(false, true, false) == screen_start_blocked_by_watching);
    CHECK(CheckShareStartConflict(false, false, true) == screen_start_blocked_by_record);
    //几种同时成立时按「自己在共享 → 在观看 → 在录制」的顺序报第一种
    CHECK(CheckShareStartConflict(true, true, true) == screen_start_blocked_by_hosting);
    CHECK(CheckShareStartConflict(false, true, true) == screen_start_blocked_by_watching);
    //提示文字与 2026-10-07 之前的消息框相同
    CHECK(ScreenStartConflictText(screen_start_blocked_by_hosting) ==
          std::wstring(TR(L"你正在共享屏幕，请先结束当前的共享。")));
    CHECK(ScreenStartConflictText(screen_start_blocked_by_watching) ==
          std::wstring(TR(L"你正在观看屏幕共享，请先离开后再发起。")));
}

TEST_CASE("SRMX5 StartShare shows a toast instead of a message box when blocked", "[sharerecmutex]")
{
    const std::string root = SrmxClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string source = SrmxReadFile(root + "\\FlamingoMedia\\Share\\ShareController.cpp");
    REQUIRE_FALSE(source.empty());
    const size_t begin = source.find("bool CShareController::StartShare(");
    REQUIRE(begin != std::string::npos);
    //函数体到其后第一个位于行首的右花括号为止（本仓库的函数体都以行首的 } 结束）
    const size_t end = source.find("\n}", begin);
    const std::string body = source.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    CHECK(body.find("CheckShareStartConflict(") != std::string::npos);
    CHECK(body.find("m_noticeToast.ShowNearActiveWindow(ScreenStartConflictText(conflict)") != std::string::npos);
    //「你正在共享屏幕」「你正在观看屏幕共享」不再经 showError 弹消息框
    CHECK(body.find("showError(m_hostPhase") == std::string::npos);
}
