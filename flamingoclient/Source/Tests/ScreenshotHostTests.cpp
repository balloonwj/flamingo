/**
 *  ScreenshotHostTests.cpp
 *
 *  主程序一侧截图宿主 CScreenshotHost 的用例（docs/截图独立模块方案.md 第 4 节，2026-10-07 与用户确认）：
 *    · SH1：FlamingoScreenshot.dll 不存在时，宿主报告不可用，发起截图直接返回 false、不崩溃，重复关闭安全；
 *    · SH2：发起截图的请求与结果接收方的包装（ScreenshotRequestBuilder）；
 *    · SH3：宿主接口的 IsOcrAvailable / LaunchOcrForImage 转发到 ocr:: 的对应函数，像素原样拷过去（方案原定在 S6，
 *      宿主接口在 S5 已经写好，一并补上）；
 *    · SH4：截图模块不可用时聊天窗的剪刀与 ▾ 隐藏、不留空位（ApplyScreenshotButtons）。
 *  每条用例都用局部的宿主对象，不碰进程内的单例。用 Catch2 v3（amalgamated）。
 *
 *  balloonwj@qq.com   2026-10-07
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <vector>
#include <memory>

#include "../Feature/Screenshot/ScreenshotHost.h"
#include "../Feature/Ocr/OcrLauncher.h"
#include "../Feature/Screenshot/ScreenshotRequestBuilder.h"
#include "../UI/BalloonChatWnd/ChatScreenshotMenu.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace {

// SH3：假的识别实现记下收到的发起窗口与图像
HWND g_sh3Owner = NULL;
ocr::OcrBgraImage g_sh3Image;
int g_sh3Calls = 0;

bool Sh3LaunchImage(HWND owner, const ocr::OcrBgraImage& image)
{
    ++g_sh3Calls;
    g_sh3Owner = owner;
    g_sh3Image = image;
    return true;
}

bool Sh3LaunchFile(HWND /*owner*/, const std::wstring& /*path*/, int /*quarterTurns*/)
{
    return true;
}

// SH3 的图像：2×1 像素，BGRA
const uint32_t kSh3Width = 2;
const uint32_t kSh3Height = 1;
const uint8_t kSh3Pixels[] = { 1, 2, 3, 255, 4, 5, 6, 255 };

} // namespace

TEST_CASE("SH1 截图模块 DLL 不存在时：宿主报告不可用，发起截图直接返回 false、不崩溃", "[shothost][screenshot]")
{
    balloonscreenshot::CScreenshotHost host;
    wchar_t temp[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, temp);
    const std::wstring missing = std::wstring(temp) + L"FlamingoUnitNoSuchDir\\" + FLAMINGO_SCREENSHOT_DLL_NAME;
    CHECK_FALSE(host.Load(missing.c_str(), L"C:\\FlamingoUnitNoSuchDir"));
    CHECK_FALSE(host.IsModuleReady());
    CHECK_FALSE(host.IsRunning());

    flamingoshot::ScreenshotRequest request;
    request.m_cbSize = sizeof(request);
    request.m_owner = NULL;
    request.m_excludeHwnd = NULL;
    request.m_startFullScreen = false;
    CHECK_FALSE(host.Run(request, NULL));

    //参数为空同样返回 false
    CHECK_FALSE(host.Load(NULL, L"C:\\FlamingoUnitNoSuchDir"));
    CHECK_FALSE(host.Load(missing.c_str(), NULL));
    CHECK_FALSE(host.IsModuleReady());

    //没有加载过也可以关闭，重复关闭安全
    host.Shutdown();
    host.Shutdown();
    CHECK_FALSE(host.IsModuleReady());
}

TEST_CASE("SH3 宿主接口的「识别文字」转发到 ocr::，像素原样拷过去；参数无效时不转发", "[shothost][screenshot]")
{
    balloonscreenshot::CScreenshotHost host;
    //是否显示「识别文字」与 ocr::IsOcrEntryVisible 一致（单测里识别模块没有加载，二者都为 false）
    CHECK(host.IsOcrAvailable() == ocr::IsOcrEntryVisible());

    ocr::SetOcrLaunchers(Sh3LaunchFile, Sh3LaunchImage);
    g_sh3Calls = 0;
    g_sh3Owner = NULL;
    g_sh3Image = ocr::OcrBgraImage();
    const HWND owner = reinterpret_cast<HWND>(static_cast<INT_PTR>(0x1234));
    CHECK(host.LaunchOcrForImage(owner, kSh3Pixels, kSh3Width, kSh3Height));
    CHECK(g_sh3Calls == 1);
    CHECK(g_sh3Owner == owner);
    CHECK(g_sh3Image.m_width == kSh3Width);
    CHECK(g_sh3Image.m_height == kSh3Height);
    REQUIRE(g_sh3Image.m_pixels.size() == sizeof(kSh3Pixels));
    CHECK(std::vector<uint8_t>(kSh3Pixels, kSh3Pixels + sizeof(kSh3Pixels)) == g_sh3Image.m_pixels);

    //参数无效：不转发
    CHECK_FALSE(host.LaunchOcrForImage(owner, NULL, kSh3Width, kSh3Height));
    CHECK_FALSE(host.LaunchOcrForImage(owner, kSh3Pixels, 0, kSh3Height));
    CHECK_FALSE(host.LaunchOcrForImage(owner, kSh3Pixels, kSh3Width, 0));
    CHECK(g_sh3Calls == 1);
    ocr::SetOcrLaunchers(NULL, NULL);
}

namespace {

// SH2：假的「发送」「发送到对话框」回调记下收到的上下文与路径
void* g_sh2SendCtx = NULL;
std::wstring g_sh2SendPath;
void* g_sh2StageCtx = NULL;
std::wstring g_sh2StagePath;

void Sh2Send(void* ctx, const wchar_t* pngPath)
{
    g_sh2SendCtx = ctx;
    g_sh2SendPath = pngPath;
}

void Sh2Stage(void* ctx, const wchar_t* pngPath)
{
    g_sh2StageCtx = ctx;
    g_sh2StagePath = pngPath;
}

// SH4：工具栏里三个按钮的编号（剪刀、▾、紧跟其后的另一个按钮）与宽度
const UINT kSh4ShotId = 9101;
const UINT kSh4MenuId = 9102;
const UINT kSh4NextId = 9103;
const int kSh4ButtonPx = 30;
const int kSh4ToolbarW = 400;
const int kSh4ToolbarH = 30;

} // namespace

TEST_CASE("SH2 发起截图的请求：无会话截图不排除窗口、没有接收方；聊天窗截图带排除窗口，两个回调原样转给发起方",
          "[shothost][screenshot]")
{
    using namespace balloonscreenshot;
    //无会话（在聊天窗之外按快捷键、托盘「截图」）：选项全空
    ScreenshotOptions none;
    flamingoshot::ScreenshotRequest req = MakeScreenshotRequest(NULL, none);
    CHECK(req.m_cbSize == sizeof(flamingoshot::ScreenshotRequest));
    CHECK(req.m_owner == NULL);
    CHECK(req.m_excludeHwnd == NULL);
    CHECK_FALSE(req.m_startFullScreen);
    CScreenshotCallbackSink noneSink(none);
    CHECK_FALSE(noneSink.HasTarget());
    //没有回调时转发什么也不做、不崩溃
    noneSink.OnSend(L"C:/a.png");
    noneSink.OnStage(L"C:/a.png");

    //聊天窗：排除自己、截取全屏、两个回调与上下文
    const HWND chat = reinterpret_cast<HWND>(static_cast<INT_PTR>(0x5678));
    int ctxObject = 0;
    ScreenshotOptions opts;
    opts.m_excludeHwnd = chat;
    opts.m_startFullScreen = true;
    opts.m_sendFn = Sh2Send;
    opts.m_stageFn = Sh2Stage;
    opts.m_ctx = &ctxObject;
    req = MakeScreenshotRequest(chat, opts);
    CHECK(req.m_owner == chat);
    CHECK(req.m_excludeHwnd == chat);
    CHECK(req.m_startFullScreen);
    CScreenshotCallbackSink sink(opts);
    CHECK(sink.HasTarget());
    g_sh2SendCtx = NULL;
    g_sh2StageCtx = NULL;
    sink.OnSend(L"C:/shot_send.png");
    CHECK(g_sh2SendCtx == &ctxObject);
    CHECK(g_sh2SendPath == L"C:/shot_send.png");
    CHECK(g_sh2StageCtx == NULL);
    sink.OnStage(L"C:/shot_stage.png");
    CHECK(g_sh2StageCtx == &ctxObject);
    CHECK(g_sh2StagePath == L"C:/shot_stage.png");

    //只有「发送到对话框」一个回调也算绑定了会话
    ScreenshotOptions stageOnly;
    stageOnly.m_stageFn = Sh2Stage;
    CHECK(CScreenshotCallbackSink(stageOnly).HasTarget());
}

TEST_CASE("SH4 截图模块不可用时聊天窗的剪刀与 ▾ 隐藏、不留空位；可用时显示", "[shothost][screenshot]")
{
    using namespace balloonwjui;
    std::unique_ptr<DuiHBox> toolbar(new DuiHBox());
    const UINT ids[3] = { kSh4ShotId, kSh4MenuId, kSh4NextId };
    for (int i = 0; i < 3; ++i)
    {
        std::unique_ptr<DuiButton> b(new DuiButton());
        b->SetCtrlId(ids[i]);
        toolbar->AddChild(std::move(b), DuiLayout::Hint().Fixed(kSh4ButtonPx));
    }
    const RECT area = { 0, 0, kSh4ToolbarW, kSh4ToolbarH };
    toolbar->ForceLayout(area);
    DuiControl* shot = toolbar->FindCtrlById(kSh4ShotId);
    DuiControl* menu = toolbar->FindCtrlById(kSh4MenuId);
    DuiControl* next = toolbar->FindCtrlById(kSh4NextId);
    REQUIRE(shot != NULL);
    REQUIRE(menu != NULL);
    REQUIRE(next != NULL);
    CHECK(next->GetRect().left == 2 * kSh4ButtonPx);

    //不可用：两个按钮都隐藏，后面的按钮挪到最前面（重排过）
    CHECK_FALSE(balloonchatwnd::ApplyScreenshotButtons(toolbar.get(), kSh4ShotId, kSh4MenuId, false));
    CHECK_FALSE(shot->IsVisible());
    CHECK_FALSE(menu->IsVisible());
    CHECK(next->GetRect().left == 0);

    //可用：恢复显示，位置复原
    CHECK(balloonchatwnd::ApplyScreenshotButtons(toolbar.get(), kSh4ShotId, kSh4MenuId, true));
    CHECK(shot->IsVisible());
    CHECK(menu->IsVisible());
    CHECK(next->GetRect().left == 2 * kSh4ButtonPx);

    //参数无效
    CHECK_FALSE(balloonchatwnd::ApplyScreenshotButtons(NULL, kSh4ShotId, kSh4MenuId, true));
    CHECK_FALSE(balloonchatwnd::ApplyScreenshotButtons(toolbar.get(), 0, kSh4MenuId, true));
    CHECK_FALSE(balloonchatwnd::ApplyScreenshotButtons(toolbar.get(), kSh4ShotId, 0, true));
}
