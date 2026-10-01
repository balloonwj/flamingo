/**
 *  ChatImageCacheTests.cpp
 *
 *  聊天图片缓存（ChatImageCache）解码失败记录的测试：文件不存在或内容损坏时要记下
 *  失败、不再反复排队解码；文件后来被下载下来或被替换时要自动清除失败记录，重新解码。
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
#include "../App/CustomMsgDef.h"   // WM_CHAT_IMAGE_READY

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
bool WaitDecodeSettled(const std::wstring& path, int w, int h)
{
    const DWORD start = ::GetTickCount();
    for (;;)
    {
        if (ChatImageCache::Inst().Get(path, w, h) != NULL
            || ChatImageCache::Inst().IsFailed(path, w, h))
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
