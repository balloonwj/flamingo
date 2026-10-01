/**
 *  ImageViewerCopyTests.cpp
 *
 *  看图窗底部工具栏「复制图片」按钮的测试（2026-09-30 新增功能）。
 *
 *  复制走的是与聊天气泡右键「复制」同一条路：把图片按本地文件路径写成 Flamingo 聊天
 *  内容格式（再附一份位图与纯文字），聊天输入框粘贴时据此把它插成一张可发送的图片。
 *  真正读写剪贴板会覆盖用户正在用的剪贴板内容，所以这里只测可以离线验证的部分：
 *
 *    IV1  工具栏里恰好有一个「复制图片」按钮，紧挨在「另存为」前面。
 *    IV2  「复制图片」按钮可点击，悬停提示文字为「复制图片」。
 *    IV3  画布还没加载图片（或加载失败）时，没有可复制的内容。
 *    IV4  加载图片后，可复制内容恰好是一段图片、路径就是源文件；序列化再解析回来不变
 *         —— 这正是粘进聊天输入框时还原图片所走的格式。
 *    IV5  源文件已经不存在时没有可复制的内容，不会把一个坏路径写进剪贴板。
 *
 *  用 Catch2 v3（amalgamated 形态）。不创建窗口；测试图片是在临时目录里手写的一张小 BMP。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../UI/BalloonImageViewer/ImageCanvas.h"
#include "../UI/BalloonImageViewer/ImageViewerToolbar.h"
#include "../UI/BalloonChatWnd/ChatClipboardContent.h"

using namespace balloonimageviewer;
using namespace balloonchatwnd;

namespace {

// 测试图片的边长（像素）。取值本身无意义，能被解码即可。
const int kBmpSide = 4;

// 24 位 BMP 每个像素占的字节数。
const int kBmpBytesPerPixel = 3;

// BMP 每一行按 4 字节对齐。
const int kBmpRowAlign = 4;

// BMP 文件头里的魔数 "BM"（小端存储的两个字节）。
const WORD kBmpMagic = 0x4D42;

// 在临时目录里写一张 kBmpSide × kBmpSide 的 24 位灰色 BMP。
//   name：文件名（不含目录）；调用方负责事后删除。
// @return 完整路径；写失败返回空串。
std::wstring WriteTestBmp(const wchar_t* name)
{
    wchar_t tempDir[MAX_PATH] = { 0 };
    if (::GetTempPathW(MAX_PATH, tempDir) == 0)
    {
        return std::wstring();
    }
    // 文件名里带上进程号，两个测试进程同时跑时不会撞到同一个文件。
    wchar_t fileName[MAX_PATH] = { 0 };
    ::swprintf_s(fileName, L"%s_%lu.bmp", name, ::GetCurrentProcessId());
    const std::wstring path = std::wstring(tempDir) + fileName;

    const int rowBytes = ((kBmpSide * kBmpBytesPerPixel + kBmpRowAlign - 1) / kBmpRowAlign) * kBmpRowAlign;
    const DWORD pixelBytes = (DWORD)(rowBytes * kBmpSide);

    BITMAPFILEHEADER fh;
    ::ZeroMemory(&fh, sizeof(fh));
    fh.bfType    = kBmpMagic;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize    = fh.bfOffBits + pixelBytes;

    BITMAPINFOHEADER ih;
    ::ZeroMemory(&ih, sizeof(ih));
    ih.biSize        = sizeof(BITMAPINFOHEADER);
    ih.biWidth       = kBmpSide;
    ih.biHeight      = kBmpSide;
    ih.biPlanes      = 1;
    ih.biBitCount    = (WORD)(kBmpBytesPerPixel * 8);
    ih.biCompression = BI_RGB;
    ih.biSizeImage   = pixelBytes;

    // 中灰色：每个通道取 0x80。
    std::vector<BYTE> pixels(pixelBytes, 0x80);

    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return std::wstring();
    }
    DWORD written = 0;
    bool ok = ::WriteFile(h, &fh, sizeof(fh), &written, NULL) != FALSE;
    ok = ok && ::WriteFile(h, &ih, sizeof(ih), &written, NULL) != FALSE;
    ok = ok && ::WriteFile(h, &pixels[0], pixelBytes, &written, NULL) != FALSE;
    ::CloseHandle(h);
    return ok ? path : std::wstring();
}

// 工具栏里第一个 kind 元素的下标；没有返回 -1。
int IndexOfKind(ToolbarItemKind kind)
{
    for (int i = 0; i < ImageViewerToolbar::ItemCount(); ++i)
    {
        if (ImageViewerToolbar::ItemKindAt(i) == kind)
        {
            return i;
        }
    }
    return -1;
}

// 工具栏里 kind 元素出现了几次。
int CountKind(ToolbarItemKind kind)
{
    int n = 0;
    for (int i = 0; i < ImageViewerToolbar::ItemCount(); ++i)
    {
        if (ImageViewerToolbar::ItemKindAt(i) == kind)
        {
            ++n;
        }
    }
    return n;
}

} // anonymous namespace

TEST_CASE("IV1 工具栏恰好有一个「复制图片」按钮，紧挨在「另存为」前面", "[imageviewer][copy]")
{
    CHECK(CountKind(kTbCopyImage) == 1);
    const int copyIdx = IndexOfKind(kTbCopyImage);
    const int saveIdx = IndexOfKind(kTbSaveAs);
    REQUIRE(copyIdx >= 0);
    REQUIRE(saveIdx >= 0);
    CHECK(copyIdx + 1 == saveIdx);
}

TEST_CASE("IV2 「复制图片」按钮可点击，悬停提示为「复制图片」", "[imageviewer][copy]")
{
    CHECK(ImageViewerToolbar::IsClickableKind(kTbCopyImage));
    CHECK(CString(ImageViewerToolbar::TooltipForKind(kTbCopyImage)) == CString(_T("复制图片")));
    // 与它相邻的「另存为」照旧可点、提示不变，防止编排时把两者弄混。
    CHECK(ImageViewerToolbar::IsClickableKind(kTbSaveAs));
    CHECK(CString(ImageViewerToolbar::TooltipForKind(kTbSaveAs)) == CString(_T("另存为")));
}

TEST_CASE("IV3 没有图片时没有可复制的内容", "[imageviewer][copy]")
{
    ImageCanvas canvas;
    CHECK(canvas.BuildCopySegments().empty());

    // 加载失败（文件不存在）同样没有。
    CHECK_FALSE(canvas.LoadImageFromFile(_T("C:\\__flamingo_iv3_not_exist__.bmp")));
    CHECK(canvas.BuildCopySegments().empty());
}

TEST_CASE("IV4 加载图片后可复制内容恰好是这张图，序列化往返不变", "[imageviewer][copy]")
{
    const std::wstring path = WriteTestBmp(L"flamingo_iv4");
    REQUIRE_FALSE(path.empty());

    std::vector<ChatClipSegment> segs;
    {
        ImageCanvas canvas;
        REQUIRE(canvas.LoadImageFromFile(path.c_str()));
        segs = canvas.BuildCopySegments();
    }

    REQUIRE(segs.size() == 1);
    CHECK(segs[0].m_kind == kChatClipImage);
    CHECK(segs[0].m_imagePath == path);

    // 粘进聊天输入框时读的就是这份序列化结果，往返之后必须还是这一张图。
    std::vector<ChatClipSegment> parsed;
    REQUIRE(ParseClipSegments(SerializeClipSegments(segs), parsed));
    REQUIRE(parsed.size() == 1);
    CHECK(parsed[0].m_kind == kChatClipImage);
    CHECK(parsed[0].m_imagePath == path);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("IV5 源文件不存在时没有可复制的内容", "[imageviewer][copy]")
{
    // 看图窗加载图片后通常一直占用着源文件，测试里未必删得掉，所以直接对判定函数
    // 传一个不存在的路径：「画布上有图」为真，但文件已经找不到。
    const CString missing(_T("C:\\__flamingo_iv5_not_exist__.bmp"));
    CHECK(BuildImageViewerCopySegments(missing, true).empty());
    // 路径为空同样没有。
    CHECK(BuildImageViewerCopySegments(CString(), true).empty());

    // 对照：文件在、画布上也有图时才有内容；文件在但画布上没图（解码失败）也没有。
    const std::wstring path = WriteTestBmp(L"flamingo_iv5");
    REQUIRE_FALSE(path.empty());
    CHECK(BuildImageViewerCopySegments(CString(path.c_str()), true).size() == 1);
    CHECK(BuildImageViewerCopySegments(CString(path.c_str()), false).empty());
    ::DeleteFileW(path.c_str());
}
