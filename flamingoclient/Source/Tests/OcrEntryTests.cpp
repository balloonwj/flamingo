/**
 *  「图片文字识别」入口的单元测试（docs/图片文字识别方案.md 第 10.2 节「工具栏」，以及新增的 OCRH9）。
 *
 *    - OCRT1 看图窗工具栏：带识别按钮时多一个元素、排在最后、可点击、有悬停提示，整条宽一格；不带时没有这个按钮
 *    - OCRT2 截图操作栏：带识别按钮时「识别文字」紧跟「保存」（绑定会话与不绑定会话两套都是），主按钮仍只有一个；
 *            不带时两套表与原来完全相同
 *    - OCRH9 发起调度：没有注册实现时什么也不做；注册后路径、旋转方向、图像原样交给实现；
 *            看图窗画布「图片文字识别」按当前旋转发起识别；旋转角度换算成 90° 的次数
 *    - OCRM1 主面板系统菜单：识别模块可用时「消息与工具」组里紧跟「备忘与提醒」出现「图片文字识别」（命令 id 18、
 *            识别图标）；不可用时不出现，其余各项不变；所有菜单项 id 互不相同（docs/图片文字识别方案.md 第 14 节）
 *
 *  框架 Catch2 v3，标签 [ocrhost]。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "../Feature/Ocr/OcrLauncher.h"
#include "../UI/BalloonImageViewer/ImageCanvas.h"
#include "../UI/BalloonImageViewer/ImageViewerToolbar.h"
#include "../../FlamingoScreenshot/Capture/ScreenshotActions.h"
#include "../UI/BalloonMain/MainPanelSystemMenu.h"
#include "../UI/BalloonMain/MainPanelIcons.h"

using namespace balloonimageviewer;
using namespace balloonscreenshot;

namespace {

//测试图片的边长（像素）与每像素字节数（24 位 BMP），行按 4 字节对齐
const int kBmpSide = 4;
const int kBmpBytesPerPixel = 3;
const int kBmpRowAlign = 4;
//BMP 文件头里的魔数 "BM"
const WORD kBmpMagic = 0x4D42;
//中灰色
const BYTE kGray = 0x80;

//在临时目录里写一张 kBmpSide × kBmpSide 的灰色 BMP；返回完整路径，失败返回空串
std::wstring WriteTestBmp(const wchar_t* name)
{
    wchar_t tempDir[MAX_PATH] = { 0 };
    if (::GetTempPathW(MAX_PATH, tempDir) == 0)
        return std::wstring();
    wchar_t fileName[MAX_PATH] = { 0 };
    ::swprintf_s(fileName, L"%s_%lu.bmp", name, ::GetCurrentProcessId());
    const std::wstring path = std::wstring(tempDir) + fileName;

    const int rowBytes = ((kBmpSide * kBmpBytesPerPixel + kBmpRowAlign - 1) / kBmpRowAlign) * kBmpRowAlign;
    const DWORD pixelBytes = (DWORD)(rowBytes * kBmpSide);
    BITMAPFILEHEADER fh;
    ::ZeroMemory(&fh, sizeof(fh));
    fh.bfType = kBmpMagic;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + pixelBytes;
    BITMAPINFOHEADER ih;
    ::ZeroMemory(&ih, sizeof(ih));
    ih.biSize = sizeof(BITMAPINFOHEADER);
    ih.biWidth = kBmpSide;
    ih.biHeight = kBmpSide;
    ih.biPlanes = 1;
    ih.biBitCount = (WORD)(kBmpBytesPerPixel * 8);
    ih.biCompression = BI_RGB;
    ih.biSizeImage = pixelBytes;
    std::vector<BYTE> pixels(pixelBytes, kGray);

    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return std::wstring();
    DWORD written = 0;
    bool ok = ::WriteFile(h, &fh, sizeof(fh), &written, NULL) != FALSE;
    ok = ok && ::WriteFile(h, &ih, sizeof(ih), &written, NULL) != FALSE;
    ok = ok && ::WriteFile(h, &pixels[0], pixelBytes, &written, NULL) != FALSE;
    ::CloseHandle(h);
    return ok ? path : std::wstring();
}

//假的发起实现收到的调用
struct LaunchRecord
{
    int          m_fileCalls;     //识别文件的调用次数
    std::wstring m_path;          //最近一次的图片路径
    int          m_turns;         //最近一次的旋转次数
    int          m_imageCalls;    //识别内存图像的调用次数
    uint32_t     m_imageWidth;    //最近一次图像的宽
    uint32_t     m_imageHeight;   //最近一次图像的高
};

LaunchRecord g_launch;

bool __cdecl FakeLaunchFile(HWND owner, const std::wstring& path, int quarterTurns)
{
    (void)owner;
    ++g_launch.m_fileCalls;
    g_launch.m_path = path;
    g_launch.m_turns = quarterTurns;
    return true;
}

bool __cdecl FakeLaunchImage(HWND owner, const ocr::OcrBgraImage& image)
{
    (void)owner;
    ++g_launch.m_imageCalls;
    g_launch.m_imageWidth = image.m_width;
    g_launch.m_imageHeight = image.m_height;
    return true;
}

//作用域内注册假实现，离开时撤销，避免影响别的用例
class LauncherScope
{
public:
    LauncherScope()
    {
        g_launch = LaunchRecord();
        ocr::SetOcrLaunchers(FakeLaunchFile, FakeLaunchImage);
    }
    ~LauncherScope()
    {
        ocr::SetOcrLaunchers(NULL, NULL);
    }
};

//截图操作栏里某个 id 的下标；没有返回 -1
int IndexOfAction(const ActionItem* items, int count, UINT id)
{
    for (int i = 0; i < count; ++i)
    {
        if (items[i].m_id == id)
            return i;
    }
    return -1;
}

} // namespace

TEST_CASE("OCRT1 看图窗工具栏：识别模块可用时末尾多一个「图片文字识别」按钮，整条宽一格", "[ocrhost]")
{
    const int plain = ImageViewerToolbar::ItemCount(false);
    const int withOcr = ImageViewerToolbar::ItemCount(true);
    CHECK(withOcr == plain + 1);
    CHECK(ImageViewerToolbar::ItemCount() == plain);   //默认不带

    //带时排在最后，前面的元素与不带时逐个相同
    CHECK(ImageViewerToolbar::ItemKindAt(withOcr - 1, true) == kTbOcr);
    for (int i = 0; i < plain; ++i)
    {
        INFO("index " << i);
        CHECK(ImageViewerToolbar::ItemKindAt(i, true) == ImageViewerToolbar::ItemKindAt(i, false));
        CHECK(ImageViewerToolbar::ItemKindAt(i, false) != kTbOcr);
    }
    CHECK(ImageViewerToolbar::ItemKindAt(withOcr, true) == kTbSeparator);   //越界

    CHECK(ImageViewerToolbar::IsClickableKind(kTbOcr));
    CHECK(std::wstring(ImageViewerToolbar::TooltipForKind(kTbOcr)).size() > 0);
    CHECK(ImageViewerToolbar::BarWidth(true) > ImageViewerToolbar::BarWidth(false));

    //实例：画布在识别模块不可用时（测试进程里没有预加载）建出的工具栏不带按钮，尺寸与静态宽度一致
    ImageViewerToolbar bar(NULL, false);
    CHECK_FALSE(bar.HasOcrButton());
    CHECK(bar.GetBarSize().cx == ImageViewerToolbar::BarWidth(false));
    ImageViewerToolbar barOcr(NULL, true);
    CHECK(barOcr.HasOcrButton());
    CHECK(barOcr.GetBarSize().cx == ImageViewerToolbar::BarWidth(true));
}

TEST_CASE("OCRT2 截图操作栏：识别模块可用时「识别文字」紧跟「保存」，主按钮仍只有一个", "[ocrhost]")
{
    const bool targets[] = { true, false };
    for (size_t t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t)
    {
        const bool hasSendTarget = targets[t];
        INFO("has send target = " << hasSendTarget);

        int plainCount = 0;
        const ActionItem* plain = GetActionItems(hasSendTarget, plainCount);
        int ocrCount = 0;
        const ActionItem* withOcr = GetActionItems(hasSendTarget, ocrCount, true);

        //不带时没有「识别文字」
        CHECK(IndexOfAction(plain, plainCount, kIdToolOcr) < 0);

        //带时多一个，紧跟「保存」，其余按钮的相对顺序不变
        REQUIRE(ocrCount == plainCount + 1);
        const int ocrIndex = IndexOfAction(withOcr, ocrCount, kIdToolOcr);
        REQUIRE(ocrIndex >= 0);
        CHECK(ocrIndex == IndexOfAction(withOcr, ocrCount, kIdToolSave) + 1);
        CHECK(withOcr[ocrIndex].m_icon == balloonscreenshot::kIconOcr);
        CHECK_FALSE(withOcr[ocrIndex].m_primary);
        CHECK(std::wstring(withOcr[ocrIndex].m_label).size() > 0);
        CHECK(std::wstring(withOcr[ocrIndex].m_tip).size() > 0);
        int j = 0;
        for (int i = 0; i < ocrCount; ++i)
        {
            if (i == ocrIndex)
                continue;
            INFO("index " << i);
            CHECK(withOcr[i].m_id == plain[j].m_id);
            ++j;
        }

        //主按钮仍只有一个
        int primaries = 0;
        for (int i = 0; i < ocrCount; ++i)
            primaries += withOcr[i].m_primary ? 1 : 0;
        CHECK(primaries == 1);
    }
}

TEST_CASE("OCRH9 发起调度：未注册时不做事，注册后参数原样交给实现", "[ocrhost]")
{
    ocr::SetOcrLaunchers(NULL, NULL);
    ocr::OcrBgraImage image;
    image.m_width = 2;
    image.m_height = 3;
    image.m_pixels.assign(2 * 3 * 4, 0xFF);
    CHECK_FALSE(ocr::LaunchOcrForFile(NULL, L"C:\\a.png", 0));
    CHECK_FALSE(ocr::LaunchOcrForImage(NULL, image));
    CHECK_FALSE(ocr::IsOcrEntryVisible());   //没有注册实现，且测试进程里没有预加载识别模块

    LauncherScope scope;
    CHECK(ocr::LaunchOcrForFile(NULL, L"C:\\a.png", 3));
    CHECK(g_launch.m_fileCalls == 1);
    CHECK(g_launch.m_path == L"C:\\a.png");
    CHECK(g_launch.m_turns == 3);
    CHECK_FALSE(ocr::LaunchOcrForFile(NULL, L"", 0));   //空路径不交给实现
    CHECK(g_launch.m_fileCalls == 1);

    CHECK(ocr::LaunchOcrForImage(NULL, image));
    CHECK(g_launch.m_imageCalls == 1);
    CHECK(g_launch.m_imageWidth == 2);
    CHECK(g_launch.m_imageHeight == 3);
    ocr::OcrBgraImage empty;
    CHECK_FALSE(ocr::LaunchOcrForImage(NULL, empty));   //空图像不交给实现
    CHECK(g_launch.m_imageCalls == 1);

    //识别模块没有预加载（测试进程）：即使注册了实现，入口也不显示
    CHECK_FALSE(ocr::IsOcrEntryVisible());

    //旋转角度换算
    CHECK(ocr::QuarterTurnsOf(0) == 0);
    CHECK(ocr::QuarterTurnsOf(90) == 1);
    CHECK(ocr::QuarterTurnsOf(180) == 2);
    CHECK(ocr::QuarterTurnsOf(270) == 3);
    CHECK(ocr::QuarterTurnsOf(360) == 0);
    CHECK(ocr::QuarterTurnsOf(450) == 1);
    CHECK(ocr::QuarterTurnsOf(-90) == 3);
}

TEST_CASE("OCRH9 看图窗画布「图片文字识别」按当前旋转发起识别", "[ocrhost]")
{
    const std::wstring path = WriteTestBmp(L"flamingo_ocr_canvas");
    REQUIRE_FALSE(path.empty());
    {
        LauncherScope scope;
        ImageCanvas canvas;

        //没有图片时不发起
        canvas.RecognizeText();
        CHECK(g_launch.m_fileCalls == 0);

        REQUIRE(canvas.LoadImageFromFile(path.c_str()));
        canvas.RecognizeText();
        CHECK(g_launch.m_fileCalls == 1);
        CHECK(g_launch.m_path == path);
        CHECK(g_launch.m_turns == 0);

        //顺时针转一次：识别时按转正后的方向
        canvas.RotateClockwise();
        canvas.RecognizeText();
        CHECK(g_launch.m_fileCalls == 2);
        CHECK(g_launch.m_turns == 1);

        //再逆时针转两次：相当于逆时针 90°，即顺时针 270°
        canvas.RotateCounterClockwise();
        canvas.RotateCounterClockwise();
        canvas.RecognizeText();
        CHECK(g_launch.m_turns == 3);
    }
    ::DeleteFileW(path.c_str());
}

TEST_CASE("OCRM1 系统菜单：识别模块可用时「图片文字识别」紧跟「备忘与提醒」", "[ocrhost][sysmenu]")
{
    using namespace balloonmain;
    CHECK((UINT)kSysMenuOcr == 18u);

    //识别模块可用：紧跟「备忘与提醒」，带识别图标、可点击
    const std::vector<SystemMenuEntry> withOcr =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, true, true, true);
    size_t at = withOcr.size();
    size_t memoAt = withOcr.size();
    for (size_t i = 0; i < withOcr.size(); ++i)
    {
        if (withOcr[i].id == kSysMenuOcr)
            at = i;
        if (withOcr[i].id == kSysMenuMemo)
            memoAt = i;
    }
    REQUIRE(at < withOcr.size());
    REQUIRE(memoAt < withOcr.size());
    CHECK(at == memoAt + 1);
    CHECK(withOcr[at].iconKind == balloonmain::kIconOcr);
    CHECK(withOcr[at].enabled);
    CHECK_FALSE(withOcr[at].isSeparator);
    CHECK_FALSE(withOcr[at].isHeader);
    CHECK(std::wstring(withOcr[at].label).size() > 0);

    //所有功能项的 id 互不相同
    std::vector<UINT> ids;
    for (size_t i = 0; i < withOcr.size(); ++i)
    {
        if (!withOcr[i].isSeparator && !withOcr[i].isHeader)
            ids.push_back(withOcr[i].id);
    }
    const size_t before = ids.size();
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    CHECK(ids.size() == before);

    //识别模块不可用（默认参数）：不出现，其余各行与带识别项时去掉这一行完全相同
    const std::vector<SystemMenuEntry> noOcr =
        BuildSystemMenuModel(true, kRecordMenuStart, NULL, true, true, true, false);
    REQUIRE(noOcr.size() + 1 == withOcr.size());
    size_t j = 0;
    for (size_t i = 0; i < withOcr.size(); ++i)
    {
        if (i == at)
            continue;
        INFO("row " << i);
        CHECK(withOcr[i].id == noOcr[j].id);
        CHECK(withOcr[i].isSeparator == noOcr[j].isSeparator);
        CHECK(withOcr[i].isHeader == noOcr[j].isHeader);
        ++j;
    }
    const std::vector<SystemMenuEntry> defaults = BuildSystemMenuModel();
    for (size_t i = 0; i < defaults.size(); ++i)
        CHECK(defaults[i].id != (UINT)kSysMenuOcr);
}
