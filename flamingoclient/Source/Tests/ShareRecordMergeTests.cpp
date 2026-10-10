/**
 *  共享中的录制合进主工具条（标签 [sharerecmerge]），ShareRecordMergeTests.cpp
 *
 *  背景：docs/屏幕共享与录制方案.md 第 14.2 节（2026-10-07 用户确认）。共享中开始录制时不再弹出单独的录制条：主持人与其他
 *  共享者合进各自的共享控制条，观看者合进观看窗（全屏工具条与窗口模式的标题栏）；录制中显示闪烁的红点与录制时长，原来的
 *  「录制」按钮变为「停止录制」。本文件覆盖：
 *    - SRMG1 文字：「录制 03:21」；观看窗的标题栏只能写文字，前面的圆点以实心 / 空心交替表示闪烁；时长为空时整段为空；
 *    - SRMG2 共享控制条：录制中状态文字带上「录制 03:21」，时长变化时跟着变；停止录制后不再显示；
 *    - SRMG3 源文件守卫：录屏控制器的 StartShareRecording 不再显示录制条；共享控制器每秒刷新时长时把录制时长交给
 *            控制条与观看窗；观看窗的标题文字用 MakeShareRecordingText 拼出录制一段。
 *  SRMG3 在运行时读取源文件，位置取自 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>
#include "../../FlamingoMedia/Share/ShareControlBar.h"
#include "../../FlamingoMedia/Share/ShareRoomPolicy.h"
#include "../Common/I18n/I18n.h"

using namespace screenshare;

namespace {

//参照矩形（屏幕坐标）
const RECT kSrmgAnchor = { 0, 0, 1600, 900 };
//读文件时每次读的字节数
const size_t kSrmgReadChunk = 4096;
//本文件在 flamingoclient 下的层数（Source\Tests\本文件）
const int kSrmgDepth = 3;

//读一个源文件的全部内容；读不到时返回空串
std::string SrmgReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kSrmgReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

//本测试文件往上 kSrmgDepth 级就是 flamingoclient
std::string SrmgClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kSrmgDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

//取一个函数的函数体：从定义开始到下一个以 nextPrefix 开头的行（或文件末尾）
std::string SrmgFunctionBody(const std::string& source, const std::string& signature, const std::string& nextPrefix)
{
    const size_t begin = source.find(signature);
    if (begin == std::string::npos)
        return std::string();
    const size_t next = source.find("\n" + nextPrefix, begin + signature.size());
    return source.substr(begin, next == std::string::npos ? std::string::npos : next - begin);
}

//收命令的通知窗口
HWND SrmgNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

} // namespace

TEST_CASE("SRMG1 recording text for the main toolbar", "[sharerecmerge]")
{
    const std::wstring plain = MakeShareRecordingText(L"03:21", false, true);
    CHECK(plain == TRF(L"录制 {0}", std::wstring(L"03:21")));
    const std::wstring on = MakeShareRecordingText(L"03:21", true, true);
    const std::wstring off = MakeShareRecordingText(L"03:21", true, false);
    CHECK(on == std::wstring(L"● ") + plain);
    CHECK(off == std::wstring(L"○ ") + plain);
    CHECK(MakeShareRecordingText(L"", true, true).empty());
    CHECK(MakeShareRecordingText(L"", false, false).empty());
}

TEST_CASE("SRMG2 share control bar shows the recording duration", "[sharerecmerge]")
{
    const HWND notify = SrmgNotifyWindow();
    CShareControlBar bar;
    bar.Show(kSrmgAnchor, notify);
    REQUIRE(bar.IsWindow());
    const std::wstring part = MakeShareRecordingText(L"03:21", false, true);
    CHECK(bar.TipTextForTest().find(part) == std::wstring::npos);
    bar.SetRecording(true);
    bar.SetRecordDuration(L"03:21");
    CHECK(bar.TipTextForTest().find(part) != std::wstring::npos);
    bar.SetRecordDuration(L"03:22");
    CHECK(bar.TipTextForTest().find(MakeShareRecordingText(L"03:22", false, true)) != std::wstring::npos);
    //停止录制：不再显示录制时长
    bar.SetRecording(false);
    CHECK(bar.TipTextForTest().find(L"03:22") == std::wstring::npos);
    //控制器在停止之后可能还会交来空的时长：同样不显示
    bar.SetRecordDuration(L"");
    CHECK(bar.TipTextForTest().find(L"03:22") == std::wstring::npos);
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("SRMG3 share recording no longer pops a separate record bar", "[sharerecmerge]")
{
    const std::string root = SrmgClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string rec = SrmgReadFile(root + "\\FlamingoMedia\\Record\\RecordController.cpp");
    REQUIRE_FALSE(rec.empty());
    const std::string start =
        SrmgFunctionBody(rec, "bool CRecordController::StartShareRecording(", "void CRecordController::");
    REQUIRE_FALSE(start.empty());
    CHECK(start.find("m_bar.Show(") == std::string::npos);

    const std::string share = SrmgReadFile(root + "\\FlamingoMedia\\Share\\ShareController.cpp");
    REQUIRE_FALSE(share.empty());
    const std::string refresh = SrmgFunctionBody(share, "void CShareController::refreshDuration()", "void CShareController::");
    REQUIRE_FALSE(refresh.empty());
    CHECK(refresh.find("m_bar.SetRecordDuration(") != std::string::npos);
    CHECK(refresh.find("m_viewer.SetRecordDuration(") != std::string::npos);

    const std::string viewer = SrmgReadFile(root + "\\FlamingoMedia\\Share\\ShareViewerWindow.cpp");
    REQUIRE_FALSE(viewer.empty());
    const std::string title =
        SrmgFunctionBody(viewer, "std::wstring CShareViewerWindow::titleText() const", "void CShareViewerWindow::");
    REQUIRE_FALSE(title.empty());
    CHECK(title.find("MakeShareRecordingText(") != std::string::npos);
}
