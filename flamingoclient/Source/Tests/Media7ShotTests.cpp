/**
 *  共享控制条、录制条、标注工具条（含「鼠标」与小把手）、轻量提示的截图（隐藏用例，标签 [.media7shot]），Media7ShotTests.cpp
 *  背景：2026-10-07 两条顶部条改成图标按钮、标注工具条加「鼠标」与小把手、新增轻量提示，界面上的效果要人工核对。
 *  本用例只把各窗口的控件树画进内存位图（不可见桌面上 PrintWindow 取不到内容）存成 PNG，不做断言之外的判断：
 *    - SHOT1 共享控制条（录制这场共享中、字幕可用）、录制条（独立录屏、暂停前后）、标注工具条（展开、「鼠标」状态、小把手）、
 *      轻量提示（普通、失败、两行）。
 *  输出目录：环境变量 MEDIA7_SHOT_DIR；没设时写到 %TEMP%。须放在不可见桌面上运行。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <objidl.h>
#include <gdiplus.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "../../FlamingoMedia/Common/MediaToast.h"
#include "../../FlamingoMedia/Record/RecordBar.h"
#include "../../FlamingoMedia/Share/ShareAnnotationBar.h"
#include "../../FlamingoMedia/Share/ShareControlBar.h"

namespace {

//参照矩形（屏幕坐标）
const RECT kShotAnchor = { 0, 0, 1600, 900 };
//截图前等界面画完的时长（毫秒）与每次处理消息的间隔
const DWORD kShotSettleMs = 300;
const DWORD kShotSliceMs = 10;
//输出目录环境变量的缓冲区长度（字符）
const size_t kShotDirChars = 512;

void ShotPump(DWORD ms)
{
    const DWORD start = ::GetTickCount();
    MSG msg;
    while (::GetTickCount() - start < ms)
    {
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(kShotSliceMs);
    }
}

bool ShotPngEncoder(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
        return false;
    std::vector<BYTE> buf(bytes);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

std::wstring ShotDir()
{
    wchar_t dir[kShotDirChars] = { 0 };
    size_t len = 0;
    if (_wgetenv_s(&len, dir, kShotDirChars, L"MEDIA7_SHOT_DIR") == 0 && len > 0)
        return std::wstring(dir) + L"\\";
    wchar_t temp[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, temp);
    return std::wstring(temp);
}

//把窗口的控件树画进内存位图，存成 <输出目录>\media7_<name>.png。不可见桌面上没有桌面合成，PrintWindow 取不到内容，
//所以直接让根控件画到内存设备上下文里
void Shot(balloonwjui::DuiHost& host, const wchar_t* name)
{
    ShotPump(kShotSettleMs);
    RECT rc;
    ::GetClientRect(host.m_hWnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    if (host.GetRoot() != NULL)
        host.GetRoot()->OnPaint(mem, rc);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
    {
        Gdiplus::Bitmap image(bmp, NULL);
        const std::wstring path = ShotDir() + L"media7_" + name + L".png";
        CLSID png;
        if (ShotPngEncoder(png))
            image.Save(path.c_str(), &png, NULL);
    }
    ::DeleteObject(bmp);
}

class ShotGdiplus
{
public:
    ShotGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_token, &input, NULL);
    }
    ~ShotGdiplus()
    {
        Gdiplus::GdiplusShutdown(m_token);
    }

private:
    ULONG_PTR m_token;   //GDI+ 的启动令牌
};

} // namespace

TEST_CASE("SHOT1 bars, handle and toast screenshots", "[.media7shot]")
{
    ShotGdiplus gdiplus;
    const HWND notify =
        ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
    //共享控制条：共享中、录制这场共享、字幕可用、麦克风开
    {
        screenshare::CShareControlBar bar;
        bar.Show(kShotAnchor, notify);
        bar.SetDurationText(L"12:40");
        bar.SetViewerCount(3);
        bar.SetMicOn(true);
        bar.SetCaptionState(true, true, true);
        bar.SetRecording(true);
        REQUIRE(bar.IsWindow());
        Shot(bar, L"sharebar");
        bar.SetPaused(true);
        bar.SetRecording(false);
        Shot(bar, L"sharebar_paused");
        bar.Hide();
    }
    //录制条：独立录屏，画笔、声音、摄像头、字幕开关都有
    {
        screenrecord::CRecordBar bar;
        bar.SetPenAvailable(true);
        bar.SetAudioToggles(true, true, false);
        bar.SetCameraToggle(true, false);
        bar.SetCaptionToggle(true, true);
        bar.Show(kShotAnchor, notify, 0);
        bar.SetState(192000, false);
        REQUIRE(bar.IsWindow());
        Shot(bar, L"recordbar");
        bar.SetState(193000, true);
        Shot(bar, L"recordbar_paused");
        bar.Hide();
    }
    //标注工具条：展开、「鼠标」状态、小把手
    {
        screenshare::CShareAnnotationBar bar;
        bar.Show(kShotAnchor, notify, true);
        REQUIRE(bar.IsWindow());
        Shot(bar, L"annotbar");
        bar.SetPointerMode(true);
        Shot(bar, L"annotbar_pointer");
        bar.SetPointerMode(false);
        bar.SetCollapsed(true);
        Shot(bar, L"annotbar_handle");
        bar.Hide();
    }
    //轻量提示
    {
        flamingomedia::CMediaToast toast;
        toast.Show(kShotAnchor, flamingomedia::media_toast_screen, L"分享链接已复制，可以粘贴到聊天里发给同事。",   //i18n-ignore: 截图用例的示例文字
                   flamingomedia::media_toast_info);
        ShotPump(kShotSettleMs);
        REQUIRE(toast.IsWindow());
        Shot(toast, L"toast_info");
        toast.Show(kShotAnchor, flamingomedia::media_toast_screen,
                   L"分享链接已重置，之前发出的链接都已失效。新的链接已复制。分享链接已重置，之前发出的链接都已失效。",   //i18n-ignore: 截图用例的示例文字
                   flamingomedia::media_toast_error);
        Shot(toast, L"toast_error_long");
        toast.Destroy();
    }
    ::DestroyWindow(notify);
}
