/**
 *  ShareCaptionOverlayTests.cpp
 *
 *  字幕浮层窗口（FlamingoMedia/Caption/ShareCaptionOverlay，录屏与屏幕共享共用）的单元测试，标签 [captionoverlay]。
 *  会建窗口（放在屏幕外），按约定放到不可见桌面里运行：
 *    SCAPO1 —— 要求排除在采集之外时：浮层显示出来，是置顶的分层窗口，显示亲和性为 WDA_EXCLUDEFROMCAPTURE。
 *              2026-10-06 用户实测录屏时字幕不显示，原因是先建分层窗口再设排除一律失败（错误码 8），浮层因此隐藏；
 *              改为先建普通窗口、设好排除再加 WS_EX_LAYERED（docs/语音字幕方案.md 14.12）。
 *    SCAPO2 —— 观看者（有所有者窗口、不排除）：从共享者的设置切换过来时重建窗口，所有者为给定窗口，不置顶，不排除采集，
 *              仍是分层窗口。
 *
 *  字幕的绘制用替身（把每个像素填成半透明灰色），不加载字幕 DLL。
 *
 *  balloonwj@qq.com   2026-10-06
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <vector>

#include "../../FlamingoMedia/Caption/ShareCaptionOverlay.h"

using screenshare::CShareCaptionOverlay;
using screenshare::ShareCaptionLine;

namespace {

//SetWindowDisplayAffinity 的「排除在采集之外」（Windows 10 2004 起）
const DWORD kScapoExcludeFromCapture = 0x00000011;
//浮层的显示区域：放在屏幕外，800 × 600
const RECT kScapoArea = { -3000, -3000, -2200, -2400 };
//替身画出的像素：预乘透明度后的半透明灰色（BGRA 各 0x40，透明度 0x80）
const uint32_t kScapoPixel = 0x80404040u;

//字幕模块的替身：只实现 RenderOverlay（把每个像素填成半透明灰色），其余方法不会被浮层调用
class FakeOverlayModule : public caption::ICaptionModule
{
public:
    virtual uint32_t GetApiVersion() const override
    {
        return caption::kApiVersion;
    }

    virtual bool Init(caption::ICaptionHost* host, const caption::CaptionInitParams* params) override
    {
        (void)host;
        (void)params;
        return true;
    }

    virtual void Shutdown() override
    {
    }

    virtual void SetSettings(const char* settingsJson) override
    {
        (void)settingsJson;
    }

    virtual void Preload() override
    {
    }

    virtual int GetEngineState() override
    {
        return 0;
    }

    virtual caption::ICaptionStream* CreateStream(int purpose, caption::ICaptionStreamListener* listener) override
    {
        (void)purpose;
        (void)listener;
        return NULL;
    }

    virtual caption::ICaptionRecording* BeginRecording(const caption::CaptionRecordingParams* params) override
    {
        (void)params;
        return NULL;
    }

    virtual bool OpenEditor(HWND owner, const wchar_t* videoPath) override
    {
        (void)owner;
        (void)videoPath;
        return false;
    }

    virtual int GetFileStatus(const wchar_t* videoPath) override
    {
        (void)videoPath;
        return 0;
    }

    virtual void OnVideoRenamed(const wchar_t* oldPath, const wchar_t* newPath) override
    {
        (void)oldPath;
        (void)newPath;
    }

    virtual void OnVideoDeleted(const wchar_t* videoPath) override
    {
        (void)videoPath;
    }

    virtual bool GetPreference(int pref) override
    {
        (void)pref;
        return false;
    }

    virtual void SetPreference(int pref, bool value) override
    {
        (void)pref;
        (void)value;
    }

    virtual bool RenderOverlay(const caption::CaptionOverlayLine* lines, uint32_t count, uint32_t width, uint32_t height,
                               uint32_t* bgra, int fontPx) override
    {
        (void)lines;
        (void)count;
        (void)fontPx;
        if (bgra == NULL)
            return false;
        const size_t total = static_cast<size_t>(width) * height;
        for (size_t i = 0; i < total; ++i)
            bgra[i] = kScapoPixel;
        return true;
    }
};

//一行字幕
std::vector<ShareCaptionLine> ScapoLines()
{
    ShareCaptionLine line;
    line.m_seat = 0;
    line.m_text = L"字幕";
    std::vector<ShareCaptionLine> lines;
    lines.push_back(line);
    return lines;
}

} // namespace

TEST_CASE("SCAPO1 overlay excluded from capture is a shown layered topmost window", "[captionoverlay]")
{
    FakeOverlayModule module;
    CShareCaptionOverlay overlay;
    overlay.Update(kScapoArea, true, NULL, ScapoLines(), &module);
    const HWND h = overlay.WindowHandle();
    REQUIRE(h != NULL);
    CHECK(overlay.IsShown());
    const LONG_PTR ex = ::GetWindowLongPtrW(h, GWL_EXSTYLE);
    CHECK((ex & WS_EX_LAYERED) != 0);
    CHECK((ex & WS_EX_TOPMOST) != 0);
    CHECK((ex & WS_EX_TRANSPARENT) != 0);
    DWORD affinity = 0;
    REQUIRE(::GetWindowDisplayAffinity(h, &affinity));
    CHECK(affinity == kScapoExcludeFromCapture);
    //再更新一次：同样的设置不重建窗口
    overlay.Update(kScapoArea, true, NULL, ScapoLines(), &module);
    CHECK(overlay.WindowHandle() == h);
    overlay.Destroy();
    CHECK(overlay.WindowHandle() == NULL);
}

TEST_CASE("SCAPO2 viewer overlay is owned, not topmost and not excluded", "[captionoverlay]")
{
    FakeOverlayModule module;
    const HWND owner = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, -3000, -3000, 100, 100, NULL, NULL,
                                         ::GetModuleHandleW(NULL), NULL);
    REQUIRE(owner != NULL);
    CShareCaptionOverlay overlay;
    overlay.Update(kScapoArea, true, NULL, ScapoLines(), &module);
    const HWND first = overlay.WindowHandle();
    REQUIRE(first != NULL);
    overlay.Update(kScapoArea, false, owner, ScapoLines(), &module);
    const HWND second = overlay.WindowHandle();
    REQUIRE(second != NULL);
    CHECK(::IsWindow(first) == FALSE);
    CHECK(::GetWindow(second, GW_OWNER) == owner);
    const LONG_PTR ex = ::GetWindowLongPtrW(second, GWL_EXSTYLE);
    CHECK((ex & WS_EX_LAYERED) != 0);
    CHECK((ex & WS_EX_TOPMOST) == 0);
    DWORD affinity = kScapoExcludeFromCapture;
    REQUIRE(::GetWindowDisplayAffinity(second, &affinity));
    CHECK(affinity == 0);
    CHECK(overlay.IsShown());
    overlay.Destroy();
    ::DestroyWindow(owner);
}
