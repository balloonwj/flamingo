/**
 *  ChatAvatarCacheTests.cpp
 *
 *  聊天气泡头像位图缓存（ChatAvatarBitmapCache）的测试（标签 [avatarcache]）。
 *
 *  覆盖：
 *    AC1 同一路径取两次得到同一张位图，只解码一次
 *    AC2 文件不存在时返回空，之后不再重试；文件出现并 Invalidate 后能取到
 *    AC3 换了新路径得到另一张位图，内容是新图
 *    AC4 气泡换了头像路径后画的是新图
 *    AC5 同一路径的头像文件被重新下载覆盖后，气泡画的是新内容
 *
 *  测试头像是用 GDI+ 现生成的纯色 PNG（纯红、纯蓝），气泡用例把列表画到离屏位图上，
 *  按颜色数像素判断画的是哪一张。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/ChatAvatarBitmapCache.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"

using namespace balloonchatwnd;

namespace {

// 生成测试头像的边长（像素），取上传头像的实际尺寸。
const int kSrcAvatarPx = 256;

// 缓存取图时的边长与圆角（像素），与气泡头像一致即可。
const int kAvatarPx     = 34;
const int kAvatarRadius = 7;

// 两种测试头像的颜色：聊天窗里没有别的元素用这两个纯色，按颜色数像素不会误计。
const COLORREF kRed  = RGB(0xFF, 0x00, 0x00);
const COLORREF kBlue = RGB(0x00, 0x00, 0xFF);

// 气泡用例的列表尺寸（像素）。
const int kListW = 400;
const int kListH = 200;

// 认定"画的是这张头像"所需的最少同色像素数：34 见方的圆角头像约 1100 个像素，
// 取一个远小于它、又远大于偶然同色的值。
const int kMinAvatarPixels = 200;

// 在作用域内启动 GDI+，离开时关闭。
class ScopedGdiplus
{
public:
    // 启动 GDI+；失败时 Ok() 返回 false。
    ScopedGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    // 关闭 GDI+。
    ~ScopedGdiplus()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    // GDI+ 是否启动成功。
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，关闭时要用
    bool      m_ok;      // 是否启动成功
};

// 临时目录下的文件路径，文件名带进程 id（并发跑的测试进程互不干扰），并删掉旧文件。
std::wstring TempFile(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring path = dir;
    path += prefix;
    path += name;
    ::DeleteFileW(path.c_str());
    return path;
}

// 取 GDI+ 的 PNG 编码器。
bool PngEncoder(CLSID& out)
{
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0)
    {
        return false;
    }
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    Gdiplus::GetImageEncoders(num, size, codecs);
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

// 在 path 写一张 kSrcAvatarPx 见方的纯色 PNG。
void WriteSolidPng(const std::wstring& path, COLORREF color)
{
    CLSID png;
    REQUIRE(PngEncoder(png));
    Gdiplus::Bitmap bmp(kSrcAvatarPx, kSrcAvatarPx, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.Clear(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    }
    REQUIRE(bmp.Save(path.c_str(), &png, NULL) == Gdiplus::Ok);
}

// 读 DIB 位图正中那个像素的 RGB（位图是预乘透明通道，正中不透明，预乘不影响取值）。
COLORREF CenterPixel(HBITMAP bmp)
{
    DIBSECTION ds;
    REQUIRE(::GetObject(bmp, sizeof(ds), &ds) == sizeof(ds));
    REQUIRE(ds.dsBm.bmBits != NULL);
    const int w = ds.dsBm.bmWidth;
    const int h = (ds.dsBm.bmHeight < 0) ? -ds.dsBm.bmHeight : ds.dsBm.bmHeight;
    const BYTE* px = static_cast<const BYTE*>(ds.dsBm.bmBits) + ((h / 2) * w + w / 2) * 4;
    return RGB(px[2], px[1], px[0]);   // DIB 按 BGRA 存放
}

// 造一条对方发来、带真图头像的文字消息。
ChatMessage MakeAvatarMessage(const std::wstring& avatarPath)
{
    ChatMessage m = MakeTextMessage(false, _T("带头像的消息"));
    m.senderUserId          = 5308;
    m.senderAvatarText      = _T("韩");
    m.senderAvatarImagePath = avatarPath.c_str();
    return m;
}

// 把列表画到白底离屏位图上，数出指定颜色的像素个数。
int PaintAndCount(ChatMessageList& list, COLORREF want)
{
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    ::ReleaseDC(NULL, screen);
    REQUIRE(mem != NULL);

    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = kListW;
    bi.bmiHeader.biHeight      = -kListH;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = ::CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(dib != NULL);
    HGDIOBJ old = ::SelectObject(mem, dib);

    RECT all = { 0, 0, kListW, kListH };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(mem, &all, white);
    ::DeleteObject(white);
    list.OnPaint(mem, all);

    const unsigned int target = ((unsigned int)GetRValue(want) << 16)
                              | ((unsigned int)GetGValue(want) << 8)
                              |  (unsigned int)GetBValue(want);
    const unsigned int* px = static_cast<const unsigned int*>(bits);
    int count = 0;
    for (int i = 0; i < kListW * kListH; ++i)
    {
        if ((px[i] & 0x00FFFFFFu) == target)
        {
            ++count;
        }
    }

    ::SelectObject(mem, old);
    ::DeleteObject(dib);
    ::DeleteDC(mem);
    return count;
}

// 取列表第一行气泡。
ChatBubble* FirstBubble(ChatMessageList& list)
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().kind != kMsgTime)
        {
            return b;
        }
    }
    FAIL("no message bubble in list");
    return NULL;
}

} // anonymous namespace

TEST_CASE("AC1 同一路径取两次得到同一张位图，只解码一次", "[avatarcache]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ChatAvatarBitmapCache& cache = ChatAvatarBitmapCache::Inst();
    cache.Clear();

    const std::wstring path = TempFile(L"ac1_red.png");
    WriteSolidPng(path, kRed);

    HBITMAP first = cache.Get(path, kAvatarPx, kAvatarRadius);
    REQUIRE(first != NULL);
    HBITMAP second = cache.Get(path, kAvatarPx, kAvatarRadius);
    CHECK(second == first);
    CHECK(cache.DecodeCount() == 1);
    CHECK(CenterPixel(first) == kRed);
}

TEST_CASE("AC2 文件不存在时返回空、不再重试，出现并作废后能取到", "[avatarcache]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ChatAvatarBitmapCache& cache = ChatAvatarBitmapCache::Inst();
    cache.Clear();

    const std::wstring path = TempFile(L"ac2_missing.png");

    CHECK(cache.Get(path, kAvatarPx, kAvatarRadius) == NULL);
    CHECK(cache.Get(path, kAvatarPx, kAvatarRadius) == NULL);
    //失败被记住了：第二次没有再去碰磁盘
    CHECK(cache.DecodeCount() == 1);

    //文件出现后，没作废之前仍按失败处理
    WriteSolidPng(path, kBlue);
    CHECK(cache.Get(path, kAvatarPx, kAvatarRadius) == NULL);

    cache.Invalidate(path);
    HBITMAP bmp = cache.Get(path, kAvatarPx, kAvatarRadius);
    REQUIRE(bmp != NULL);
    CHECK(CenterPixel(bmp) == kBlue);
}

TEST_CASE("AC3 换了新路径得到另一张位图，内容是新图", "[avatarcache]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ChatAvatarBitmapCache& cache = ChatAvatarBitmapCache::Inst();
    cache.Clear();

    const std::wstring red = TempFile(L"ac3_red.png");
    const std::wstring blue = TempFile(L"ac3_blue.png");
    WriteSolidPng(red, kRed);
    WriteSolidPng(blue, kBlue);

    HBITMAP a = cache.Get(red, kAvatarPx, kAvatarRadius);
    HBITMAP b = cache.Get(blue, kAvatarPx, kAvatarRadius);
    REQUIRE(a != NULL);
    REQUIRE(b != NULL);
    CHECK(a != b);
    CHECK(CenterPixel(a) == kRed);
    CHECK(CenterPixel(b) == kBlue);
}

TEST_CASE("AC4 气泡换了头像路径后画的是新图", "[avatarcache]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ChatAvatarBitmapCache::Inst().Clear();

    const std::wstring red = TempFile(L"ac4_red.png");
    const std::wstring blue = TempFile(L"ac4_blue.png");
    WriteSolidPng(red, kRed);
    WriteSolidPng(blue, kBlue);

    ChatMessageList list;
    list.AppendMessage(MakeAvatarMessage(red));
    RECT rc = { 0, 0, kListW, kListH };
    list.ForceLayout(rc);
    CHECK(PaintAndCount(list, kRed) > kMinAvatarPixels);

    FirstBubble(list)->SetSenderAvatarImage(blue.c_str());
    list.ForceLayout(rc);
    CHECK(PaintAndCount(list, kBlue) > kMinAvatarPixels);
    CHECK(PaintAndCount(list, kRed) == 0);
}

TEST_CASE("AC5 同一路径的头像被重新下载覆盖后，气泡画的是新内容", "[avatarcache]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ChatAvatarBitmapCache::Inst().Clear();

    const std::wstring path = TempFile(L"ac5_avatar.png");
    WriteSolidPng(path, kRed);

    ChatMessageList list;
    list.AppendMessage(MakeAvatarMessage(path));
    RECT rc = { 0, 0, kListW, kListH };
    list.ForceLayout(rc);
    REQUIRE(PaintAndCount(list, kRed) > kMinAvatarPixels);

    //同一路径被新下载的头像覆盖，聊天窗随后用同一路径刷新气泡
    WriteSolidPng(path, kBlue);
    FirstBubble(list)->SetSenderAvatarImage(path.c_str());
    list.ForceLayout(rc);
    CHECK(PaintAndCount(list, kBlue) > kMinAvatarPixels);
    CHECK(PaintAndCount(list, kRed) == 0);
}
