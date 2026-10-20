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
#include "../../FlamingoMedia/Share/ShareSharerBar.h"
#include "../../FlamingoMedia/Share/ShareCameraWindow.h"
#include "../../FlamingoMedia/Share/ViewerFloatBar.h"
#include "../../FlamingoMedia/Share/ShareViewerWindow.h"   // SHOT3：真实观看窗的浮动工具条（帮助第 18 章配图）

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

TEST_CASE("SHOT2 viewer float bar and controlled sharer bar screenshots", "[.media7shot]")
{
    //浮动观看工具条（2026-10-12，docs/远程桌面控制权限与观看工具条方案.md 第 4.3 节）：屏幕共享观看者与远程桌面控制端两套按钮；
    //共享者条被控制时的「收回控制」
    ShotGdiplus gdiplus;
    const HWND notify =
        ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
    const HWND owner = ::CreateWindowExW(0, L"STATIC", L"shot-owner", WS_OVERLAPPEDWINDOW, 0, 0, 1600, 900, NULL, NULL,
                                         ::GetModuleHandleW(NULL), NULL);
    ::ShowWindow(owner, SW_SHOWNOACTIVATE);
    const UINT notifyMsg = WM_APP + 0x7FF0;
    const UINT firstId = screenshare::kViewerBarUserIdFirst;
    //屏幕共享观看者：麦克风关、摄像头开、标注、录制中、字幕、申请控制（申请中）、共享我的屏幕、新建白板、N 人观看、共享说明、
    //主持人、全屏、离开
    {
        std::vector<screenshare::ViewerBarButtonSpec> v;
        v.push_back(screenshare::ViewerBarButtonSpec(firstId, screenshare::share_bar_mic, screenshare::share_bar_tri_off, L""));
        v.push_back(screenshare::ViewerBarButtonSpec(firstId + 1, screenshare::share_bar_camera, screenshare::share_bar_tri_on, L""));
        v.push_back(screenshare::ViewerBarButtonSpec(firstId + 2, screenshare::share_bar_annotate, screenshare::share_bar_tri_off, L""));
        v.push_back(screenshare::ViewerBarButtonSpec(firstId + 3, screenshare::share_bar_record, screenshare::share_bar_tri_on, L""));
        v.push_back(screenshare::ViewerBarButtonSpec(firstId + 4, screenshare::share_bar_caption, screenshare::share_bar_tri_off, L""));
        v.push_back(screenshare::ViewerBarButtonSpec(firstId + 5, screenshare::share_bar_control, screenshare::share_bar_tri_pending, L""));
        v.push_back(screenshare::ViewerBarButtonSpec(firstId + 6, screenshare::share_bar_share_mine, screenshare::share_bar_tri_off, L""));
        v.push_back(screenshare::ViewerBarButtonSpec(firstId + 7, screenshare::share_bar_board, screenshare::share_bar_tri_on, L""));
        screenshare::ViewerBarButtonSpec viewers(firstId + 8, screenshare::share_bar_viewers, screenshare::share_bar_tri_off, L"");
        viewers.m_text = L"3 人观看 ▾";
        v.push_back(viewers);
        v.push_back(screenshare::ViewerBarButtonSpec(firstId + 9, screenshare::share_bar_info, screenshare::share_bar_tri_off, L""));
        v.push_back(screenshare::ViewerBarButtonSpec(firstId + 10, screenshare::share_bar_host, screenshare::share_bar_tri_off, L""));
        v.push_back(screenshare::ViewerBarButtonSpec(firstId + 11, screenshare::share_bar_fullscreen, screenshare::share_bar_tri_on, L""));
        v.push_back(screenshare::ViewerBarButtonSpec(firstId + 12, screenshare::share_bar_leave, screenshare::share_bar_tri_off, L""));
        screenshare::CViewerFloatBar bar;
        bar.SetCaptionText(L"tom 的屏幕共享");
        bar.SetBarButtons(v);
        bar.Show(owner, notify, notifyMsg, kShotAnchor);
        Shot(bar, L"viewerbar_share");
        bar.Hide();
    }
    //远程桌面控制端：状态文字、申请控制（三种状态各一张）、全屏、断开
    {
        const int states[] = { screenshare::share_bar_tri_off, screenshare::share_bar_tri_pending, screenshare::share_bar_tri_on };
        const wchar_t* const names[] = { L"viewerbar_rd_view", L"viewerbar_rd_pending", L"viewerbar_rd_control" };
        const wchar_t* const status[] = { L"仅查看", L"申请中", L"控制中" };
        for (int i = 0; i < 3; ++i)
        {
            std::vector<screenshare::ViewerBarButtonSpec> v;
            v.push_back(screenshare::ViewerBarButtonSpec(firstId, screenshare::share_bar_control, states[i], L""));
            v.push_back(screenshare::ViewerBarButtonSpec(firstId + 1, screenshare::share_bar_fullscreen, screenshare::share_bar_tri_off, L""));
            v.push_back(screenshare::ViewerBarButtonSpec(firstId + 2, screenshare::share_bar_leave, screenshare::share_bar_tri_off, L""));
            screenshare::CViewerFloatBar bar;
            bar.SetCaptionText(L"远程桌面");
            bar.SetStatusText(status[i]);
            bar.SetBarButtons(v);
            bar.Show(owner, notify, notifyMsg, kShotAnchor);
            Shot(bar, names[i]);
            bar.Hide();
        }
    }
    //共享者条（2026-10-14 起派生自 CShareTopBar，图标按钮）：只共享一个窗口时提供「屏幕当白板」、标注中；收起后的小把手
    {
        screenshare::CShareSharerBar bar;
        bar.SetScreenBoard(true, false);
        bar.Show(kShotAnchor, notify);
        bar.SetAnnotating(true);
        Shot(bar, L"sharerbar");
        balloonwjui::DuiNotify n;
        n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
        n.ctrlId = screenshare::CShareSharerBar::kIdToggle;
        n.pCtrl = NULL;
        n.extra = 0;
        ::SendMessageW(bar.m_hWnd, WM_DUI_NOTIFY, static_cast<WPARAM>(n.ctrlId), reinterpret_cast<LPARAM>(&n));
        Shot(bar, L"sharerbar_handle");
        bar.Hide();
    }
    //共享者条：被控制时显示控制者与「收回控制」
    {
        screenshare::CShareSharerBar bar;
        bar.Show(kShotAnchor, notify);
        bar.SetControlledBy(L"tom");
        Shot(bar, L"sharerbar_controlled");
        bar.Hide();
    }
    //主持人的摄像头小窗收起后的小把手（2026-10-12）
    {
        std::vector<screenshare::ShareCameraTile> tiles(2);
        tiles[0].m_name = L"me";
        tiles[0].m_self = true;
        tiles[1].m_seat = 1;
        tiles[1].m_name = L"tom";
        screenshare::CShareCameraWindow cam;
        cam.Update(tiles, 0, kShotAnchor);
        ::SendMessageW(cam.m_hWnd, WM_CLOSE, 0, 0);
        Shot(cam, L"camera_handle");
        cam.Close();
    }
    ::DestroyWindow(owner);
    ::DestroyWindow(notify);
}

TEST_CASE("SHOT3 the real share viewer's floating bar in fullscreen, for the help site", "[.media7shot]")
{
    //帮助第 18 章「观看窗口」的配图 t_share_viewer_bar.png（2026-10-12 起按钮在浮动工具条上）：真实的观看窗，按默认状态生成按钮
    ShotGdiplus gdiplus;
    const HWND notify =
        ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
    {
        screenshare::CShareViewerWindow wnd;
        screenshare::ShareRelayInfo relay;
        relay.m_roomId = "shot3-room";
        const int pubKeyLen = 32;
        unsigned char pubKey[pubKeyLen] = { 0 };
        wnd.Open(relay, pubKey, L"host", notify, NULL, std::shared_ptr<balloonp2p::P2PLinkSlot>());
        REQUIRE(wnd.IsWindow());
        wnd.SetShareInfo(L"登录页改版效果演示", L"本周改版的登录页，欢迎提意见");   //i18n-ignore: 截图用的演示数据
        wnd.SetViewerCount(1);
        wnd.SetDurationText(L"04:45");
        //用例不连中转，观看窗很快按「连接已断开」改写标题条：先把这些消息处理完，再按正常观看时的格式（共享标题 · 时长）写回
        ShotPump(kShotSettleMs);
        wnd.BarForTest().SetCaptionText(L"登录页改版效果演示 · 04:45");   //i18n-ignore: 截图用的演示数据
        Shot(wnd.BarForTest(), L"viewerbar_share_real");
        wnd.CloseViewer();
    }
    ::DestroyWindow(notify);
}

