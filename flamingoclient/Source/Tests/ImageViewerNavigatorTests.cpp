/**
 *  看图窗翻页回调的单元测试（Suite IVN，标签 [imageviewer]），ImageViewerNavigatorTests.cpp
 *  覆盖（文件共享第 2 期第 4 步：图片预览复用主程序的看图窗，左右键经回调交给文件共享模块翻页）：
 *    - IVN1 设了翻页回调时 ← / → 分别回调 -1 / +1；取消回调后不再回调；窗口关闭时回调被清空，
 *           再次打开（不带回调，等同于聊天图片）时方向键不回调；Esc 照常关闭窗口。
 *  看图窗真实创建（显示一张用 GDI+ 现做的图片），不模拟鼠标，按键直接发 WM_KEYDOWN。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>

#include "../UI/BalloonImageViewer/BalloonImageViewerWindow.h"

namespace {

//测试图片的边长（像素）
const int kImageEdge = 64;

//等窗口处理完消息的时长，单位：毫秒
const DWORD kSettleMs = 200;

//假的翻页回调：记下每次的方向
class FakeNavigator : public IImageViewerNavigator
{
public:
    virtual void OnImageViewerNavigate(int delta) override
    {
        m_deltas.push_back(delta);
    }

    std::vector<int> m_deltas;   //依次收到的方向
};

//处理一段时间的消息
void Pump(DWORD ms)
{
    const ULONGLONG end = ::GetTickCount64() + ms;
    while (::GetTickCount64() < end)
    {
        MSG msg;
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(1);
    }
}

//在测试程序目录下用 GDI+ 做一张 PNG，返回路径；失败返回空串
std::wstring MakeImage()
{
    wchar_t exe[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, exe, MAX_PATH);
    std::wstring path(exe);
    path = path.substr(0, path.find_last_of(L'\\')) + L"\\ivn_test.png";
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    Gdiplus::GetImageEncoders(num, size, codecs);
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            Gdiplus::Bitmap bmp(kImageEdge, kImageEdge, PixelFormat24bppRGB);
            Gdiplus::Graphics g(&bmp);
            g.Clear(Gdiplus::Color(0xFF808080));
            if (bmp.Save(path.c_str(), &codecs[i].Clsid, NULL) == Gdiplus::Ok)
                return path;
        }
    }
    return std::wstring();
}

} // namespace

TEST_CASE("IVN1 image viewer navigation callback", "[imageviewer]")
{
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    REQUIRE(Gdiplus::GdiplusStartup(&token, &input, NULL) == Gdiplus::Ok);
    const std::wstring image = MakeImage();
    REQUIRE_FALSE(image.empty());

    FakeNavigator nav;
    {
        CBalloonImageViewerWindow viewer;
        viewer.SetNavigator(&nav);
        viewer.ShowImage(NULL, image.c_str());
        REQUIRE(viewer.IsWindow());
        ::SendMessageW(viewer.m_hWnd, WM_KEYDOWN, VK_RIGHT, 0);
        ::SendMessageW(viewer.m_hWnd, WM_KEYDOWN, VK_LEFT, 0);
        REQUIRE(nav.m_deltas.size() == 2);
        CHECK(nav.m_deltas[0] == 1);
        CHECK(nav.m_deltas[1] == -1);

        //取消回调：不再回调
        viewer.SetNavigator(NULL);
        ::SendMessageW(viewer.m_hWnd, WM_KEYDOWN, VK_RIGHT, 0);
        CHECK(nav.m_deltas.size() == 2);

        //窗口关闭时回调被清空：再次打开（不带回调）后方向键不回调
        viewer.SetNavigator(&nav);
        ::SendMessageW(viewer.m_hWnd, WM_KEYDOWN, VK_ESCAPE, 0);
        Pump(kSettleMs);
        CHECK_FALSE(viewer.IsWindow());
        viewer.ShowImage(NULL, image.c_str());
        REQUIRE(viewer.IsWindow());
        ::SendMessageW(viewer.m_hWnd, WM_KEYDOWN, VK_RIGHT, 0);
        CHECK(nav.m_deltas.size() == 2);
        ::SendMessageW(viewer.m_hWnd, WM_CLOSE, 0, 0);
    }
    ::DeleteFileW(image.c_str());
    Gdiplus::GdiplusShutdown(token);
}
