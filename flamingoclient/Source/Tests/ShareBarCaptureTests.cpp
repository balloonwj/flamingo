/**
 *  「允许截图」：工具条与控制条能否被截图（标签 [barcapture]），ShareBarCaptureTests.cpp
 *
 *  背景：docs/屏幕共享与录制方案.md 第 14.11 节第 5 条（2026-10-08 用户确认）。主持人与共享者的标注工具条、共享控制条、
 *  共享者控制条、录制条都设了「不允许采集」，免得被录进共享画面与录像，副作用是截图工具也截不到它们。默认保持不允许；
 *  在共享控制条「更多」、录制条「鼠标效果」菜单、「主持人 ▾」菜单与共享者控制条新加的「更多」里各加一项「允许截图」，
 *  任何一处打开，本机当前的这几种条都允许采集；标注层、边框、右下角提示、摄像头小窗仍然排除；只对这一场共享或这一次录制
 *  有效，结束后恢复不允许。
 *  覆盖：
 *    - SBCA1 SetWindowCaptureAllowed：允许后窗口的显示属性为 WDA_NONE，不允许时为 WDA_EXCLUDEFROMCAPTURE；
 *    - SBCA2 标注工具条按开关设置：主持人模式下开关关闭时排除、打开时允许（打开之后才显示的工具条同样允许）；
 *            观看者模式的工具条本来就不排除，开关不影响它；
 *    - SBCA3 共享控制条、共享者控制条、录制条同样按开关设置；
 *    - SBCA4 源文件守卫：四种条都改用 ApplyBarCaptureState、不再直接排除；标注层、边框、摄像头小窗仍直接排除；
 *            共享控制条、录制条、共享者控制条的菜单里有「允许截图」，共享者控制条有「更多」按钮；共享结束与录制结束时
 *            把开关设回关闭。
 *  「主持人 ▾」菜单里的这一项见 ShareHostTransferTests.cpp 的 HTR1。会建几个工具条窗口（屏幕外的位置），测完即销毁。
 *  源文件守卫在运行时读取源文件，位置取自 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC）。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>
#include "../../FlamingoMedia/Share/ShareSources.h"
#include "../../FlamingoMedia/Share/ShareAnnotationBar.h"
#include "../../FlamingoMedia/Share/ShareControlBar.h"
#include "../../FlamingoMedia/Share/ShareSharerBar.h"
#include "../../FlamingoMedia/Record/RecordBar.h"

using namespace screenshare;

namespace {

//GetWindowDisplayAffinity 的取值：不排除、排除在采集之外（Windows 10 2004 起）
const DWORD kSbcaWdaNone = 0x00000000;
const DWORD kSbcaWdaExclude = 0x00000011;
//工具条的参照矩形（屏幕坐标）
const RECT kSbcaAnchor = { 100, 100, 1500, 800 };
//读文件时每次读的字节数、本文件在 flamingoclient 下的层数（Source\Tests\本文件）
const size_t kSbcaReadChunk = 4096;
const int kSbcaDepth = 3;

DWORD SbcaAffinity(HWND hwnd)
{
    DWORD affinity = 0xFFFFFFFF;
    if (!::GetWindowDisplayAffinity(hwnd, &affinity))
        return 0xFFFFFFFF;
    return affinity;
}

HWND SbcaNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

std::string SbcaReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kSbcaReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

std::string SbcaClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kSbcaDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

bool SbcaHas(const std::string& text, const char* what)
{
    return text.find(what) != std::string::npos;
}

} // namespace

TEST_CASE("SBCA1 a window can be allowed or excluded from capture", "[barcapture]")
{
    const HWND wnd = ::CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"sbca1", WS_POPUP, -3000, -3000, 100, 40, NULL,
                                       NULL, ::GetModuleHandleW(NULL), NULL);
    REQUIRE(wnd != NULL);
    CHECK(SetWindowCaptureAllowed(wnd, false));
    CHECK(SbcaAffinity(wnd) == kSbcaWdaExclude);
    CHECK(SetWindowCaptureAllowed(wnd, true));
    CHECK(SbcaAffinity(wnd) == kSbcaWdaNone);
    CHECK(SetWindowCaptureAllowed(wnd, false));
    CHECK(SbcaAffinity(wnd) == kSbcaWdaExclude);
    ::DestroyWindow(wnd);
}

TEST_CASE("SBCA2 the annotation bar follows the allow-screenshot switch", "[barcapture]")
{
    const HWND notify = SbcaNotifyWindow();
    SetBarCaptureAllowed(false);
    {
        //主持人模式、开关关闭：排除
        CShareAnnotationBar bar;
        bar.Show(kSbcaAnchor, notify, true);
        REQUIRE(bar.IsWindow());
        CHECK(SbcaAffinity(bar.m_hWnd) == kSbcaWdaExclude);
        //显示着的时候打开开关，再套用一次：允许
        SetBarCaptureAllowed(true);
        CHECK(IsBarCaptureAllowed());
        CHECK(ApplyBarCaptureState(bar.m_hWnd));
        CHECK(SbcaAffinity(bar.m_hWnd) == kSbcaWdaNone);
        bar.Hide();
    }
    {
        //开关打开之后才显示的工具条：同样允许
        CShareAnnotationBar bar;
        bar.Show(kSbcaAnchor, notify, true);
        REQUIRE(bar.IsWindow());
        CHECK(SbcaAffinity(bar.m_hWnd) == kSbcaWdaNone);
        bar.Hide();
    }
    {
        //观看者模式：本来就不排除，开关关闭时也不排除
        SetBarCaptureAllowed(false);
        CShareAnnotationBar bar;
        bar.Show(kSbcaAnchor, notify, false);
        REQUIRE(bar.IsWindow());
        CHECK(SbcaAffinity(bar.m_hWnd) == kSbcaWdaNone);
        bar.Hide();
    }
    SetBarCaptureAllowed(false);
    CHECK_FALSE(IsBarCaptureAllowed());
    ::DestroyWindow(notify);
}

TEST_CASE("SBCA3 the control bar, the sharer bar and the record bar follow the switch", "[barcapture]")
{
    const HWND notify = SbcaNotifyWindow();
    for (int allowed = 0; allowed <= 1; ++allowed)
    {
        SetBarCaptureAllowed(allowed != 0);
        const DWORD expected = (allowed != 0) ? kSbcaWdaNone : kSbcaWdaExclude;
        CShareControlBar control;
        control.Show(kSbcaAnchor, notify);
        REQUIRE(control.IsWindow());
        CHECK(SbcaAffinity(control.m_hWnd) == expected);
        control.Hide();
        CShareSharerBar sharer;
        sharer.Show(kSbcaAnchor, notify);
        REQUIRE(sharer.IsWindow());
        CHECK(SbcaAffinity(sharer.m_hWnd) == expected);
        sharer.Hide();
        screenrecord::CRecordBar record;
        record.Show(kSbcaAnchor, notify);
        REQUIRE(record.IsWindow());
        CHECK(SbcaAffinity(record.m_hWnd) == expected);
        record.Hide();
    }
    SetBarCaptureAllowed(false);
    ::DestroyWindow(notify);
}

TEST_CASE("SBCA4 bars use the switch, overlays stay excluded, menus offer it and it resets", "[barcapture]")
{
    const std::string root = SbcaClientRoot();
    REQUIRE_FALSE(root.empty());
    //四种条：改用 ApplyBarCaptureState，不再直接排除
    const char* const bars[] = { "\\FlamingoMedia\\Share\\ShareAnnotationBar.cpp", "\\FlamingoMedia\\Share\\ShareControlBar.cpp",
                                 "\\FlamingoMedia\\Share\\ShareSharerBar.cpp", "\\FlamingoMedia\\Record\\RecordBar.cpp" };
    for (size_t i = 0; i < sizeof(bars) / sizeof(bars[0]); ++i)
    {
        const std::string src = SbcaReadFile(root + bars[i]);
        INFO(bars[i]);
        REQUIRE_FALSE(src.empty());
        CHECK(SbcaHas(src, "ApplyBarCaptureState("));
        CHECK_FALSE(SbcaHas(src, "ExcludeWindowFromCapture("));
    }
    //共享者在自己屏幕上标注时的工具条：同样按开关
    const std::string controller = SbcaReadFile(root + "\\FlamingoMedia\\Share\\ShareController.cpp");
    REQUIRE_FALSE(controller.empty());
    CHECK(SbcaHas(controller, "ApplyBarCaptureState(m_annotBar.m_hWnd)"));
    CHECK_FALSE(SbcaHas(controller, "ExcludeWindowFromCapture(m_annotBar.m_hWnd)"));
    //标注层、边框、摄像头小窗：仍直接排除
    const char* const overlays[] = { "\\FlamingoMedia\\Share\\ShareAnnotationOverlay.cpp",
                                     "\\FlamingoMedia\\Share\\ShareBorderWindow.cpp",
                                     "\\FlamingoMedia\\Share\\ShareCameraWindow.cpp",
                                     "\\FlamingoMedia\\Record\\RecordCameraWindow.cpp" };
    for (size_t i = 0; i < sizeof(overlays) / sizeof(overlays[0]); ++i)
    {
        const std::string src = SbcaReadFile(root + overlays[i]);
        INFO(overlays[i]);
        REQUIRE_FALSE(src.empty());
        CHECK(SbcaHas(src, "ExcludeWindowFromCapture("));
        CHECK_FALSE(SbcaHas(src, "ApplyBarCaptureState("));
    }
    //菜单里的「允许截图」：共享控制条「更多」、录制条菜单、共享者控制条新加的「更多」
    const std::string controlBar = SbcaReadFile(root + "\\FlamingoMedia\\Share\\ShareControlBar.cpp");
    const std::string recordBar = SbcaReadFile(root + "\\FlamingoMedia\\Record\\RecordBar.cpp");
    const std::string sharerBar = SbcaReadFile(root + "\\FlamingoMedia\\Share\\ShareSharerBar.cpp");
    CHECK(SbcaHas(controlBar, "kMenuAllowCapture"));
    CHECK(SbcaHas(recordBar, "kMenuAllowCapture"));
    CHECK(SbcaHas(sharerBar, "kMenuAllowCapture"));
    CHECK(SbcaHas(sharerBar, "kIdMore"));
    //只对这一场共享、这一次录制有效：结束时设回关闭
    CHECK(SbcaHas(controller, "SetBarCaptureAllowed(false)"));
    const std::string recordController = SbcaReadFile(root + "\\FlamingoMedia\\Record\\RecordController.cpp");
    REQUIRE_FALSE(recordController.empty());
    CHECK(SbcaHas(recordController, "SetBarCaptureAllowed(false)"));
}
