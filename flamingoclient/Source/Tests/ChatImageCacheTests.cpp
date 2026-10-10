/**
 *  ChatImageCacheTests.cpp
 *
 *  聊天图片缓存（ChatImageCache）解码失败记录的测试：文件不存在或内容损坏时要记下
 *  失败、不再反复排队解码；文件后来被下载下来或被替换时要自动清除失败记录，重新解码。
 *  CIC6 ~ CIC8 是居中裁切缩放（kImageFitCover，2026-10-08）：裁切区域的计算、两种缩放方式
 *  的像素结果，以及历史窗口的缩略图控件（「图片」网格的格子）画出来不被压扁。
 *
 *  ChatImageCache 在后台线程解码，用例里用"轮询 + 超时"等待结果，每条最多等
 *  kWaitTimeoutMs。解码完成的通知（WM_CHAT_IMAGE_READY）用一个仅接收消息的隐藏窗口
 *  来接收和计数。临时文件名里带进程 id，避免两个测试进程同时运行时共用同一个文件。
 *
 *  用 Catch2 v3（amalgamated）作框架。全局 _Module 由 BalloonChatLayoutTests.cpp
 *  统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatImageCache.h"
#include "../UI/BalloonChatWnd/ImagePlaceholder.h"   // CIC8：kHistoryThumbImage
#include "../UI/BalloonHistory/HistoryRow.h"         // CIC8：「图片」网格的格子用的缩略图控件
#include "../App/CustomMsgDef.h"   // WM_CHAT_IMAGE_READY

#include <gdiplus.h>   // CIC7：读缩放结果的像素

#include <string>
#include <vector>

using namespace balloonchatwnd;

namespace {

// 等待后台解码结果的超时（毫秒）。
const DWORD kWaitTimeoutMs = 2000;

// 轮询间隔（毫秒）。
const DWORD kPollIntervalMs = 20;

// 判定"不再收到通知"时观察的时长（毫秒）：远大于解码一个不存在的文件所需的时间。
const DWORD kQuietWindowMs = 400;

// 测试用的展示尺寸（像素）。
const int kTestW = 40;
const int kTestH = 30;

// 模拟"界面重绘后再次请求"的次数。
const int kRepaintRounds = 3;

// ---- 测试用小 BMP 的参数 ----
// 图片宽高（像素）。
const LONG kBmpSide = 2;
// 每像素字节数（24 位色）与对应的位深。
const DWORD kBmpBytesPerPixel = 3;
const WORD  kBmpBitCount = 24;
// BMP 每行字节数须按 4 字节对齐。
const DWORD kBmpRowAlign = 4;
// BMP 文件头的魔数 "BM"（小端存储）。
const WORD  kBmpMagic = 0x4D42;
// 像素填充值（中灰）。
const BYTE  kBmpFillByte = 0x80;
// 损坏图片的内容：一段不是任何图片格式的字节。
const char  kGarbageBytes[] = "this is not an image file";

// 写一个很小的 24 位 BMP；成功返回 true。
bool WriteTinyBmp(const std::wstring& path)
{
    const DWORD rowBytes = ((kBmpSide * kBmpBytesPerPixel + kBmpRowAlign - 1) / kBmpRowAlign) * kBmpRowAlign;

    BITMAPINFOHEADER info;
    ::ZeroMemory(&info, sizeof(info));
    info.biSize        = sizeof(info);
    info.biWidth       = kBmpSide;
    info.biHeight      = kBmpSide;
    info.biPlanes      = 1;
    info.biBitCount    = kBmpBitCount;
    info.biCompression = BI_RGB;
    info.biSizeImage   = rowBytes * kBmpSide;

    BITMAPFILEHEADER file;
    ::ZeroMemory(&file, sizeof(file));
    file.bfType    = kBmpMagic;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize    = file.bfOffBits + info.biSizeImage;

    std::vector<BYTE> pixels(info.biSizeImage, kBmpFillByte);

    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    DWORD written = 0;
    bool ok = ::WriteFile(h, &file, sizeof(file), &written, NULL) != FALSE;
    ok = ok && (::WriteFile(h, &info, sizeof(info), &written, NULL) != FALSE);
    ok = ok && (::WriteFile(h, &pixels[0], (DWORD)pixels.size(), &written, NULL) != FALSE);
    ::CloseHandle(h);
    return ok;
}

// ---- CIC7 / CIC8 用的「左右带绿边」的图 ----
// 图片宽高（像素）：宽是高的两倍，按正方形居中裁切时左右各裁掉四分之一。
const LONG kBandBmpW = 200;
const LONG kBandBmpH = 100;
// 左右两条绿边各自的宽度（像素），正好等于居中裁切时被裁掉的宽度。
const LONG kBandBmpEdgeW = 50;
// 24 位 BMP 一个像素的三个字节按 B、G、R 排列，这是它们在像素里的下标。
const int kBmpBlue  = 0;
const int kBmpGreen = 1;
const int kBmpRed   = 2;
// 颜色分量的满值。
const BYTE kChannelFull = 0xFF;

// 写一张 kBandBmpW x kBandBmpH 的 24 位 BMP：左右两条 kBandBmpEdgeW 宽的边为纯绿，中间为纯红。
// 成功返回 true。
bool WriteBandBmp(const std::wstring& path)
{
    const DWORD rowBytes = ((kBandBmpW * kBmpBytesPerPixel + kBmpRowAlign - 1) / kBmpRowAlign) * kBmpRowAlign;

    BITMAPINFOHEADER info;
    ::ZeroMemory(&info, sizeof(info));
    info.biSize        = sizeof(info);
    info.biWidth       = kBandBmpW;
    info.biHeight      = kBandBmpH;
    info.biPlanes      = 1;
    info.biBitCount    = kBmpBitCount;
    info.biCompression = BI_RGB;
    info.biSizeImage   = rowBytes * kBandBmpH;

    BITMAPFILEHEADER file;
    ::ZeroMemory(&file, sizeof(file));
    file.bfType    = kBmpMagic;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize    = file.bfOffBits + info.biSizeImage;

    std::vector<BYTE> pixels(info.biSizeImage, 0);
    for (LONG y = 0; y < kBandBmpH; ++y)
    {
        for (LONG x = 0; x < kBandBmpW; ++x)
        {
            const bool edge = (x < kBandBmpEdgeW) || (x >= kBandBmpW - kBandBmpEdgeW);
            BYTE* px = &pixels[y * rowBytes + x * kBmpBytesPerPixel];
            px[kBmpBlue]  = 0;
            px[kBmpGreen] = edge ? kChannelFull : 0;
            px[kBmpRed]   = edge ? 0 : kChannelFull;
        }
    }

    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    DWORD written = 0;
    bool ok = ::WriteFile(h, &file, sizeof(file), &written, NULL) != FALSE;
    ok = ok && (::WriteFile(h, &info, sizeof(info), &written, NULL) != FALSE);
    ok = ok && (::WriteFile(h, &pixels[0], (DWORD)pixels.size(), &written, NULL) != FALSE);
    ::CloseHandle(h);
    return ok;
}

// 判断颜色是不是「基本是红色」或「基本是绿色」时用的门限：主分量至少这么亮，另一个分量至多这么暗。
const BYTE kDominantMin = 200;
const BYTE kOtherMax    = 60;

// 颜色基本是红色。
bool IsMostlyRed(BYTE r, BYTE g)
{
    return r >= kDominantMin && g <= kOtherMax;
}

// 颜色基本是绿色。
bool IsMostlyGreen(BYTE r, BYTE g)
{
    return g >= kDominantMin && r <= kOtherMax;
}

// 写一个内容损坏的"图片"文件；成功返回 true。
bool WriteGarbageFile(const std::wstring& path)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    DWORD written = 0;
    const bool ok = ::WriteFile(h, kGarbageBytes, (DWORD)sizeof(kGarbageBytes), &written, NULL) != FALSE;
    ::CloseHandle(h);
    return ok;
}

// 请求解码后等待结果落定（解码成功进了缓存，或者记下了失败）；超时返回 false。
//   fit：缩放方式（ImageFitMode），须与 Request 时相同。
bool WaitDecodeSettled(const std::wstring& path, int w, int h, int fit = kImageFitStretch)
{
    const DWORD start = ::GetTickCount();
    for (;;)
    {
        if (ChatImageCache::Inst().Get(path, w, h, fit) != NULL
            || ChatImageCache::Inst().IsFailed(path, w, h, fit))
        {
            return true;
        }
        if (::GetTickCount() - start >= kWaitTimeoutMs)
        {
            return false;
        }
        ::Sleep(kPollIntervalMs);
    }
}

// 生成本进程专用的临时文件路径：%TEMP%\flamingo_imgcache_<pid>_<name>。
std::wstring TempPath(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t pid[32] = { 0 };
    ::wsprintfW(pid, L"%lu", ::GetCurrentProcessId());
    return std::wstring(dir) + L"flamingo_imgcache_" + pid + L"_" + name;
}

// 在 timeoutMs 内等待隐藏窗口收到一条 WM_CHAT_IMAGE_READY；收到返回 true。
bool WaitReadyMessage(HWND hwnd, DWORD timeoutMs)
{
    const DWORD start = ::GetTickCount();
    for (;;)
    {
        MSG msg;
        if (::PeekMessageW(&msg, hwnd, WM_CHAT_IMAGE_READY, WM_CHAT_IMAGE_READY, PM_REMOVE))
        {
            return true;
        }
        if (::GetTickCount() - start >= timeoutMs)
        {
            return false;
        }
        ::Sleep(kPollIntervalMs);
    }
}

// 在 windowMs 时长内统计隐藏窗口收到的 WM_CHAT_IMAGE_READY 条数。
int CountReadyMessages(HWND hwnd, DWORD windowMs)
{
    int count = 0;
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < windowMs)
    {
        MSG msg;
        while (::PeekMessageW(&msg, hwnd, WM_CHAT_IMAGE_READY, WM_CHAT_IMAGE_READY, PM_REMOVE))
        {
            ++count;
        }
        ::Sleep(kPollIntervalMs);
    }
    return count;
}

// 仅接收消息的隐藏窗口，在作用域结束时自动注销监听并销毁。
class ScopedListenerWindow
{
public:
    ScopedListenerWindow()
    {
        m_hwnd = ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                   HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
        if (m_hwnd != NULL)
        {
            ChatImageCache::Inst().RegisterListener(m_hwnd);
        }
    }

    ~ScopedListenerWindow()
    {
        if (m_hwnd != NULL)
        {
            ChatImageCache::Inst().UnregisterListener(m_hwnd);
            ::DestroyWindow(m_hwnd);
        }
    }

    HWND Handle() const
    {
        return m_hwnd;
    }

private:
    HWND m_hwnd;   //隐藏窗口句柄；创建失败时为 NULL
};

} // anonymous namespace

TEST_CASE("CIC1 文件不存在：解码后记为失败，Get 取不到图", "[imgcache]")
{
    const std::wstring missing = TempPath(L"cic1_missing.png");
    ::DeleteFileW(missing.c_str());

    CHECK_FALSE(ChatImageCache::Inst().IsFailed(missing, kTestW, kTestH));   //还没解码过
    ChatImageCache::Inst().Request(missing, kTestW, kTestH);
    REQUIRE(WaitDecodeSettled(missing, kTestW, kTestH));

    CHECK(ChatImageCache::Inst().IsFailed(missing, kTestW, kTestH));
    CHECK(ChatImageCache::Inst().Get(missing, kTestW, kTestH) == NULL);
}

TEST_CASE("CIC2 文件内容损坏：解码后记为失败", "[imgcache]")
{
    const std::wstring garbage = TempPath(L"cic2_garbage.png");
    REQUIRE(WriteGarbageFile(garbage));

    ChatImageCache::Inst().Request(garbage, kTestW, kTestH);
    REQUIRE(WaitDecodeSettled(garbage, kTestW, kTestH));

    CHECK(ChatImageCache::Inst().IsFailed(garbage, kTestW, kTestH));
    CHECK(ChatImageCache::Inst().Get(garbage, kTestW, kTestH) == NULL);
    ::DeleteFileW(garbage.c_str());
}

TEST_CASE("CIC3 正常图片：Get 取得到图，不记为失败", "[imgcache]")
{
    const std::wstring valid = TempPath(L"cic3_valid.bmp");
    REQUIRE(WriteTinyBmp(valid));

    ChatImageCache::Inst().Request(valid, kTestW, kTestH);
    REQUIRE(WaitDecodeSettled(valid, kTestW, kTestH));

    CHECK(ChatImageCache::Inst().Get(valid, kTestW, kTestH) != NULL);
    CHECK_FALSE(ChatImageCache::Inst().IsFailed(valid, kTestW, kTestH));
    ::DeleteFileW(valid.c_str());
}

TEST_CASE("CIC4 先失败、后来文件出现：失败记录自动清除，再请求能解码成功", "[imgcache]")
{
    const std::wstring later = TempPath(L"cic4_later.bmp");
    ::DeleteFileW(later.c_str());

    //第一轮：文件还不存在，记为失败。
    ChatImageCache::Inst().Request(later, kTestW, kTestH);
    REQUIRE(WaitDecodeSettled(later, kTestW, kTestH));
    REQUIRE(ChatImageCache::Inst().IsFailed(later, kTestW, kTestH));

    //文件被下载下来（这里直接写一张正常图片）：文件状态变了，失败记录应当作废。
    REQUIRE(WriteTinyBmp(later));
    CHECK_FALSE(ChatImageCache::Inst().IsFailed(later, kTestW, kTestH));

    //第二轮：重新请求，这次解码成功。
    ChatImageCache::Inst().Request(later, kTestW, kTestH);
    REQUIRE(WaitDecodeSettled(later, kTestW, kTestH));
    CHECK(ChatImageCache::Inst().Get(later, kTestW, kTestH) != NULL);
    CHECK_FALSE(ChatImageCache::Inst().IsFailed(later, kTestW, kTestH));
    ::DeleteFileW(later.c_str());
}

TEST_CASE("CIC5 解码失败之后再请求，不会再排队解码、不会再发出重绘通知", "[imgcache]")
{
    ScopedListenerWindow listener;
    REQUIRE(listener.Handle() != NULL);

    const std::wstring missing = TempPath(L"cic5_missing.png");
    ::DeleteFileW(missing.c_str());

    //第一次请求：文件不存在，解码失败，照常收到一条通知（界面要据此画出破图）。
    ChatImageCache::Inst().Request(missing, kTestW, kTestH);
    REQUIRE(WaitReadyMessage(listener.Handle(), kWaitTimeoutMs));

    //模拟界面收到通知后重绘、再次请求：失败已经记下，不该再排队解码，也就不该再有通知。
    int extra = 0;
    for (int round = 0; round < kRepaintRounds; ++round)
    {
        ChatImageCache::Inst().Request(missing, kTestW, kTestH);
        extra += CountReadyMessages(listener.Handle(), kQuietWindowMs);
    }
    INFO("失败之后又收到的重绘通知条数=" << extra);
    CHECK(extra == 0);
}

TEST_CASE("CIC6 居中裁切：裁切区域与展示尺寸同比例、位于原图正中、不超出原图", "[imgcache]")
{
    //一组输入与期望的裁切区域
    struct Case
    {
        int m_srcW;    // 原图宽
        int m_srcH;    // 原图高
        int m_dstW;    // 展示宽
        int m_dstH;    // 展示高
        int m_x;       // 期望的裁切左上角横坐标
        int m_y;       // 期望的裁切左上角纵坐标
        int m_w;       // 期望的裁切宽
        int m_h;       // 期望的裁切高
        const char* m_what;   // 这一组测的是什么
    };
    const Case cases[] =
    {
        { 200, 100, 120, 120,  50,   0, 100, 100, "wide image into square: crop left and right" },
        { 100, 300, 120, 120,   0, 100, 100, 100, "tall image into square: crop top and bottom" },
        { 400, 300, 200, 150,   0,   0, 400, 300, "same aspect ratio: whole image" },
        { 100, 100, 200, 100,   0,  25, 100,  50, "square image into wide box: crop top and bottom" },
        { 333, 100, 100, 100, 116,   0, 100, 100, "odd margin: left side gets the smaller half" },
        {   1, 1000, 120, 120,  0, 499,   1,   1, "1 px wide strip: crop is at least 1 px" },
        { 60000, 50000, 7, 3,   0, 12143, 60000, 25714, "large image: no int overflow" },
        {   0, 100, 120, 120,   0,   0,   0, 100, "invalid source: whole image" },
        { 200, 100,   0,  50,   0,   0, 200, 100, "invalid display size: whole image" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        const Case& c = cases[i];
        INFO(c.m_what);
        int x = -1;
        int y = -1;
        int w = -1;
        int h = -1;
        ChatImageCache::ComputeCoverCrop(c.m_srcW, c.m_srcH, c.m_dstW, c.m_dstH, x, y, w, h);
        CHECK(x == c.m_x);
        CHECK(y == c.m_y);
        CHECK(w == c.m_w);
        CHECK(h == c.m_h);
    }
}

TEST_CASE("CIC7 两种缩放方式：整张缩放带着两条绿边，居中裁切只剩中间的红色、四角不透明", "[imgcache]")
{
    //展示尺寸：正方形，比原图窄一半
    const int kSide = 40;
    //检查颜色的那一行（纵坐标），取中间一行
    const int kRow = kSide / 2;
    //检查颜色的列：离左右边缘留几个像素，避开高质量插值在边缘取到邻近像素的影响
    const int kLeftCol  = 4;
    const int kRightCol = kSide - 1 - kLeftCol;
    //整张缩放时检查绿边的列：绿边缩放后宽 10 像素，取其中间
    const int kEdgeCol = 2;

    const std::wstring band = TempPath(L"cic7_band.bmp");
    REQUIRE(WriteBandBmp(band));

    ChatImageCache::Inst().Request(band, kSide, kSide, kImageFitCover);
    REQUIRE(WaitDecodeSettled(band, kSide, kSide, kImageFitCover));
    ChatImageCache::Inst().Request(band, kSide, kSide);
    REQUIRE(WaitDecodeSettled(band, kSide, kSide));

    Gdiplus::Bitmap* cover = ChatImageCache::Inst().Get(band, kSide, kSide, kImageFitCover);
    Gdiplus::Bitmap* stretch = ChatImageCache::Inst().Get(band, kSide, kSide);
    REQUIRE(cover != NULL);
    REQUIRE(stretch != NULL);
    //两种方式各缓存一份，键不能相同
    CHECK(cover != stretch);
    CHECK((int)cover->GetWidth() == kSide);
    CHECK((int)cover->GetHeight() == kSide);

    Gdiplus::Color c;
    //居中裁切：左右两侧都是中间那块红色
    REQUIRE(cover->GetPixel(kLeftCol, kRow, &c) == Gdiplus::Ok);
    CHECK(IsMostlyRed(c.GetR(), c.GetG()));
    REQUIRE(cover->GetPixel(kRightCol, kRow, &c) == Gdiplus::Ok);
    CHECK(IsMostlyRed(c.GetR(), c.GetG()));
    //居中裁切：四角完全不透明（边缘按镜像取样，不会混进图外的透明像素）
    const int corners[][2] = { { 0, 0 }, { kSide - 1, 0 }, { 0, kSide - 1 }, { kSide - 1, kSide - 1 } };
    for (size_t i = 0; i < sizeof(corners) / sizeof(corners[0]); ++i)
    {
        REQUIRE(cover->GetPixel(corners[i][0], corners[i][1], &c) == Gdiplus::Ok);
        INFO("corner " << i << " alpha=" << (int)c.GetA());
        CHECK(c.GetA() == kChannelFull);
    }
    //整张缩放（原来的行为，聊天气泡仍用它）：左右两侧是被压窄的绿边
    REQUIRE(stretch->GetPixel(kEdgeCol, kRow, &c) == Gdiplus::Ok);
    CHECK(IsMostlyGreen(c.GetR(), c.GetG()));
    REQUIRE(stretch->GetPixel(kSide - 1 - kEdgeCol, kRow, &c) == Gdiplus::Ok);
    CHECK(IsMostlyGreen(c.GetR(), c.GetG()));
    ::DeleteFileW(band.c_str());
}

TEST_CASE("CIC8 历史窗口的缩略图控件按居中裁切取图：正方形格子里的宽图不被压扁", "[imgcache]")
{
    //格子边长（像素），与「图片」网格一样是正方形
    const int kCell = 48;
    //检查颜色的列与行，含义同 CIC7
    const int kRow = kCell / 2;
    const int kLeftCol  = 6;
    const int kRightCol = kCell - 1 - kLeftCol;

    const std::wstring band = TempPath(L"cic8_band.bmp");
    REQUIRE(WriteBandBmp(band));

    balloonhistory::HistoryRowThumb thumb;
    thumb.SetImage(band, balloonchatwnd::kHistoryThumbImage, true);
    const RECT rc = { 0, 0, kCell, kCell };
    thumb.SetRect(rc);

    //画到一张内存位图上：第一次画时缓存未命中，控件按自己的缩放方式发起请求
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, kCell, kCell);
    ::ReleaseDC(NULL, screen);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    thumb.OnPaint(mem, rc);

    //控件请求的必须是居中裁切的那一份：等它解码好，并确认整张缩放的那份没有被请求
    REQUIRE(WaitDecodeSettled(band, kCell, kCell, kImageFitCover));
    REQUIRE(ChatImageCache::Inst().Get(band, kCell, kCell, kImageFitCover) != NULL);
    CHECK(ChatImageCache::Inst().Get(band, kCell, kCell) == NULL);

    //第二次画：命中缓存，画出真图。左右两侧都应是中间的红色，而不是被压窄的绿边
    thumb.OnPaint(mem, rc);
    const COLORREF left = ::GetPixel(mem, kLeftCol, kRow);
    const COLORREF right = ::GetPixel(mem, kRightCol, kRow);
    INFO("left=" << std::hex << left << " right=" << right);
    CHECK(IsMostlyRed(GetRValue(left), GetGValue(left)));
    CHECK(IsMostlyRed(GetRValue(right), GetGValue(right)));

    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::DeleteFileW(band.c_str());
}
