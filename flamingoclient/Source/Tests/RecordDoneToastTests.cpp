/**
 *  录制完成提示 CRecordDoneToast 的单元测试（标签 [recorddone]），RecordDoneToastTests.cpp
 *  覆盖：
 *    - RDT1 提示不再自动关闭（2026-10-05 用户要求）：标题只写「屏幕录制」、不带「…后关闭」的倒计时；投来原倒计时
 *      定时器的 WM_TIMER 也不关闭；点标题栏关闭（WM_CLOSE）才关闭。
 *    - RDT2 窗口为 400×228、按钮分两行（2026-10-06 用户嫌原来的 508×178 太长、太矮）：第一行从左到右依次为「打开」
 *      「打开所在文件夹」「打开我的录制」，互不重叠；「发送」在第二行、贴内容区右缘；按钮都在内容区之内，宽度与高度
 *      都与空间充裕时相同（没有被压窄、压扁），底部留白也与空间充裕时相同（按设计尺寸排得下）。
 *    - RDT3 点「打开我的录制」向通知窗口投 kCmdOpenManager，提示保留（便于接着发送）。
 *    - RDT4 有字幕草稿时多一个「编辑字幕」（语音字幕，2026-10-06）：放在第二行左侧、「发送」在同一行右侧，两者不重叠、
 *      宽度不被压窄；窗口大小与不带字幕时相同；点它投 kCmdEditCaption、提示保留；不带字幕时没有这个按钮。
 *    - RDTS 效果图（标签 [.][snapshot][recorddone]，默认不运行）：设了环境变量 FLAMINGO_SNAPSHOT_PATH（一个目录）时，
 *      弹出不带与带「编辑字幕」的两种提示，只截提示窗口本身（PrintWindow），存成 record_done_toast*.png，供人工核对界面。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include <gdiplus.h>   // RDTS 效果图存成 PNG

#include "../../FlamingoMedia/Record/RecordDoneToast.h"
#include "../../FlamingoMedia/Record/RecordMessages.h"
#include "../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "LayoutFitCheck.h"   // RDT2：按设计尺寸排得下的检查

namespace {

//第 15 期倒计时用过的定时器 id（RecordDoneToast.cpp 原 kCountdownTimerId），用来确认改动后它不再起作用
const UINT_PTR kOldCountdownTimerId = 311;

//处理本线程已排队的消息
void PumpRecordToastMessages()
{
    MSG msg;
    while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

//窗口标题
std::wstring RecordToastTitle(HWND hwnd)
{
    wchar_t buf[256] = { 0 };
    ::GetWindowTextW(hwnd, buf, static_cast<int>(sizeof(buf) / sizeof(buf[0])));
    return buf;
}

//RDT2：排参照布局时在内容区宽度上额外加的宽度（像素），足够让四个按钮都按原宽排下即可
const int kRdtRoomyExtraWidth = 1000;

//RDT2 / RDT4：提示窗口的设计尺寸（整窗，像素），与 RecordDoneToast.cpp 的 kToastW / kToastH 相同
const int kRdtToastW = 400;
const int kRdtToastH = 228;
//标题栏高（像素），与 RecordDoneToast.cpp 的 kTitleBarH 相同；内容区高度 = 整窗高 - 标题栏高
const int kRdtTitleBarH = 28;

//按 id 取按钮的矩形；找不到时返回全 0
RECT RecordToastButtonRect(balloonwjui::DuiControl* root, UINT id)
{
    RECT rc = { 0, 0, 0, 0 };
    balloonwjui::DuiControl* ctrl = root->FindCtrlById(id);
    if (ctrl != NULL)
        rc = ctrl->GetRect();
    return rc;
}

//整窗的宽、高
SIZE RecordToastWindowSize(HWND hwnd)
{
    RECT rc = { 0, 0, 0, 0 };
    ::GetWindowRect(hwnd, &rc);
    SIZE sz = { rc.right - rc.left, rc.bottom - rc.top };
    return sz;
}

//检查第一行的三个按钮：从左到右依次排列、顶边对齐、互不重叠、都在内容区之内。返回第一行最下缘
int CheckRecordToastFirstRow(balloonwjui::DuiControl* root, const RECT& content)
{
    const UINT ids[] = { screenrecord::CRecordDoneToast::kIdPlay, screenrecord::CRecordDoneToast::kIdFolder,
                         screenrecord::CRecordDoneToast::kIdManager };
    const size_t count = sizeof(ids) / sizeof(ids[0]);
    const RECT first = RecordToastButtonRect(root, ids[0]);
    RECT prev = { 0, 0, 0, 0 };
    int rowBottom = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const RECT rc = RecordToastButtonRect(root, ids[i]);
        INFO("first row button index " << i);
        CHECK(rc.top == first.top);
        CHECK(rc.left >= content.left);
        CHECK(rc.right <= content.right);
        if (i > 0)
            CHECK(rc.left >= prev.right);
        if (rc.bottom > rowBottom)
            rowBottom = rc.bottom;
        prev = rc;
    }
    return rowBottom;
}

//模拟点了某个按钮：向窗口发一条 DUIN_CLICK 通知
void ClickRecordToastButton(HWND hwnd, UINT ctrlId)
{
    balloonwjui::DuiNotify n;
    memset(&n, 0, sizeof(n));
    n.code = balloonwjui::DUIN_CLICK;
    n.ctrlId = ctrlId;
    ::SendMessage(hwnd, WM_DUI_NOTIFY, static_cast<WPARAM>(ctrlId), reinterpret_cast<LPARAM>(&n));
}

} // namespace

TEST_CASE("RDT1 record done toast never closes by itself", "[recorddone]")
{
    screenrecord::CRecordDoneToast toast;
    toast.Show(L"Recording finished", L"20261005_120000.mp4", L"00:12  1.2 MB", L"Send", NULL);
    REQUIRE(toast.IsWindow());

    //标题固定为「屏幕录制」，不带剩余时间
    CHECK(RecordToastTitle(toast.m_hWnd) == std::wstring(TR(L"屏幕录制")));

    //原倒计时的定时器消息不再关闭提示
    toast.PostMessage(WM_TIMER, kOldCountdownTimerId, 0);
    PumpRecordToastMessages();
    CHECK(toast.IsWindow());
    CHECK(RecordToastTitle(toast.m_hWnd) == std::wstring(TR(L"屏幕录制")));

    //点标题栏的关闭按钮才关闭
    toast.SendMessage(WM_CLOSE, 0, 0);
    CHECK_FALSE(toast.IsWindow());
}

TEST_CASE("RDT2 record done toast is 400x228 with buttons in two rows", "[recorddone]")
{
    screenrecord::CRecordDoneToast toast;
    toast.Show(L"Recording finished", L"20261006_120000.mp4", L"00:12  1.2 MB", TR(L"发送到会话…"), NULL);
    REQUIRE(toast.IsWindow());
    const SIZE wnd = RecordToastWindowSize(toast.m_hWnd);
    CHECK(wnd.cx == kRdtToastW);
    CHECK(wnd.cy == kRdtToastH);
    balloonwjui::DuiControl* root = toast.GetClientContent();
    REQUIRE(root != NULL);
    const RECT content = root->GetRect();

    const UINT ids[] = { screenrecord::CRecordDoneToast::kIdPlay, screenrecord::CRecordDoneToast::kIdFolder,
                         screenrecord::CRecordDoneToast::kIdManager, screenrecord::CRecordDoneToast::kIdSend };
    const size_t count = sizeof(ids) / sizeof(ids[0]);

    //新按钮的文字
    balloonwjui::DuiButton* manager =
        dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(screenrecord::CRecordDoneToast::kIdManager));
    REQUIRE(manager != NULL);
    CHECK(std::wstring(manager->GetText()) == std::wstring(TR(L"打开我的录制")));

    //参照：内容区加宽之后各按钮的宽度
    int roomyWidth[count] = { 0 };
    RECT roomy = content;
    roomy.right += kRdtRoomyExtraWidth;
    root->ForceLayout(roomy);
    for (size_t i = 0; i < count; ++i)
    {
        const RECT rc = RecordToastButtonRect(root, ids[i]);
        roomyWidth[i] = rc.right - rc.left;
        CHECK(roomyWidth[i] > 0);
    }

    //按设计尺寸排版：宽度不变；第一行三个按钮从左到右排列；「发送」在第二行、与第一行左边距相同地贴右缘
    root->ForceLayout(content);
    for (size_t i = 0; i < count; ++i)
    {
        const RECT rc = RecordToastButtonRect(root, ids[i]);
        INFO("button index " << i);
        CHECK(rc.right - rc.left == roomyWidth[i]);
    }
    const int firstRowBottom = CheckRecordToastFirstRow(root, content);
    const RECT play = RecordToastButtonRect(root, screenrecord::CRecordDoneToast::kIdPlay);
    const RECT send = RecordToastButtonRect(root, screenrecord::CRecordDoneToast::kIdSend);
    CHECK(send.top >= firstRowBottom);
    CHECK(send.right <= content.right);
    CHECK(content.right - send.right == play.left - content.left);

    //按设计高度排得下：第二行的「发送」与第一行的按钮都没有被压扁，底部留白与空间充裕时相同
    const int contentW = content.right - content.left;
    const int contentH = kRdtToastH - kRdtTitleBarH;
    const layoutfit::FitResult sendFit =
        layoutfit::MeasureAt(root, screenrecord::CRecordDoneToast::kIdSend, contentW, contentH);
    REQUIRE(sendFit.m_found);
    CHECK(sendFit.m_bottom <= contentH);
    CHECK(sendFit.m_height == sendFit.m_roomyHeight);
    CHECK(sendFit.m_bottomGap == sendFit.m_roomyBottomGap);
    const layoutfit::FitResult playFit =
        layoutfit::MeasureAt(root, screenrecord::CRecordDoneToast::kIdPlay, contentW, contentH);
    REQUIRE(playFit.m_found);
    CHECK(playFit.m_height == playFit.m_roomyHeight);
    toast.Hide();
}

TEST_CASE("RDT3 open-manager button posts command and keeps toast", "[recorddone]")
{
    //通知窗口用一个只收消息的窗口代替录屏控制器，投来的命令留在队列里由用例取出
    HWND notify = ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    REQUIRE(notify != NULL);

    screenrecord::CRecordDoneToast toast;
    toast.Show(L"Recording finished", L"20261006_120000.mp4", L"00:12  1.2 MB", L"Send", notify);
    REQUIRE(toast.IsWindow());
    ClickRecordToastButton(toast.m_hWnd, screenrecord::CRecordDoneToast::kIdManager);

    MSG msg;
    memset(&msg, 0, sizeof(msg));
    const BOOL got = ::PeekMessageW(&msg, notify, screenrecord::kMsgRecordDoneCommand,
                                    screenrecord::kMsgRecordDoneCommand, PM_REMOVE);
    CHECK(got != FALSE);
    CHECK(msg.wParam == static_cast<WPARAM>(screenrecord::CRecordDoneToast::kCmdOpenManager));
    CHECK(toast.IsWindow());

    toast.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("RDT4 caption button fits and posts edit-caption command", "[recorddone]")
{
    HWND notify = ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    REQUIRE(notify != NULL);

    screenrecord::CRecordDoneToast toast;
    //不带字幕：没有「编辑字幕」
    toast.Show(L"Recording finished", L"20261006_120000.mp4", L"00:12  1.2 MB", L"Send", notify);
    REQUIRE(toast.IsWindow());
    CHECK(toast.GetClientContent()->FindCtrlById(screenrecord::CRecordDoneToast::kIdCaption) == NULL);

    //带字幕：替换内容。窗口大小不变；「编辑字幕」在第二行左侧，「发送」在同一行右侧
    toast.Show(L"Recording finished", L"20261006_120000.mp4", L"00:12  1.2 MB", TR(L"发送到会话…"), notify, true);
    const SIZE wnd = RecordToastWindowSize(toast.m_hWnd);
    CHECK(wnd.cx == kRdtToastW);
    CHECK(wnd.cy == kRdtToastH);
    balloonwjui::DuiControl* root = toast.GetClientContent();
    REQUIRE(root != NULL);
    const RECT content = root->GetRect();
    const UINT ids[] = { screenrecord::CRecordDoneToast::kIdPlay, screenrecord::CRecordDoneToast::kIdFolder,
                         screenrecord::CRecordDoneToast::kIdManager, screenrecord::CRecordDoneToast::kIdCaption,
                         screenrecord::CRecordDoneToast::kIdSend };
    const size_t count = sizeof(ids) / sizeof(ids[0]);
    int roomyWidth[count] = { 0 };
    RECT roomy = content;
    roomy.right += kRdtRoomyExtraWidth;
    root->ForceLayout(roomy);
    for (size_t i = 0; i < count; ++i)
    {
        const RECT rc = RecordToastButtonRect(root, ids[i]);
        roomyWidth[i] = rc.right - rc.left;
        CHECK(roomyWidth[i] > 0);
    }
    root->ForceLayout(content);
    for (size_t i = 0; i < count; ++i)
    {
        const RECT rc = RecordToastButtonRect(root, ids[i]);
        INFO("button index " << i);
        CHECK(rc.right - rc.left == roomyWidth[i]);
        CHECK(rc.left >= content.left);
        CHECK(rc.right <= content.right);
    }
    const int firstRowBottom = CheckRecordToastFirstRow(root, content);
    const RECT play = RecordToastButtonRect(root, screenrecord::CRecordDoneToast::kIdPlay);
    const RECT caption = RecordToastButtonRect(root, screenrecord::CRecordDoneToast::kIdCaption);
    const RECT send = RecordToastButtonRect(root, screenrecord::CRecordDoneToast::kIdSend);
    CHECK(caption.top >= firstRowBottom);
    CHECK(caption.top == send.top);
    CHECK(caption.left == play.left);
    CHECK(caption.right <= send.left);

    //点「编辑字幕」：投 kCmdEditCaption，提示保留
    ClickRecordToastButton(toast.m_hWnd, screenrecord::CRecordDoneToast::kIdCaption);
    MSG msg;
    memset(&msg, 0, sizeof(msg));
    const BOOL got = ::PeekMessageW(&msg, notify, screenrecord::kMsgRecordDoneCommand,
                                    screenrecord::kMsgRecordDoneCommand, PM_REMOVE);
    CHECK(got != FALSE);
    CHECK(msg.wParam == static_cast<WPARAM>(screenrecord::CRecordDoneToast::kCmdEditCaption));
    CHECK(toast.IsWindow());

    toast.Hide();
    ::DestroyWindow(notify);
}

namespace {

//RDTS：截图前处理消息的时长（毫秒），让提示窗口完成首次绘制
const DWORD kRdtsSettleMs = 400;
//RDTS：处理消息时每轮之间的等待（毫秒）
const DWORD kRdtsPumpStepMs = 20;
//PrintWindow 的「按合成后的完整内容绘制」标志（Windows 8.1 起；老 SDK 头文件里没有这个宏）
const UINT kRdtsPrintFullContent = 0x00000002;

//在 GDI+ 已注册的图像编码器里找 PNG 编码器的 CLSID
bool RdtsPngEncoder(CLSID& out)
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

//处理一段时间的消息，让窗口完成绘制
void RdtsSettle()
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < kRdtsSettleMs)
    {
        PumpRecordToastMessages();
        ::Sleep(kRdtsPumpStepMs);
    }
}

//只截窗口本身（PrintWindow，不碰屏幕上的其他内容），存成 PNG。返回是否保存成功
bool RdtsShot(HWND hwnd, const std::wstring& path)
{
    RdtsSettle();
    RECT rc;
    ::GetWindowRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    ::PrintWindow(hwnd, mem, kRdtsPrintFullContent);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
    bool saved = false;
    {
        Gdiplus::Bitmap image(bmp, NULL);
        CLSID png;
        if (RdtsPngEncoder(png))
            saved = (image.Save(path.c_str(), &png, NULL) == Gdiplus::Ok);
    }
    ::DeleteObject(bmp);
    return saved;
}

} // namespace

TEST_CASE("RDTS record done toast snapshot", "[.][snapshot][recorddone]")
{
    wchar_t dir[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariableW(L"FLAMINGO_SNAPSHOT_PATH", dir, MAX_PATH) == 0)
    {
        SUCCEED("FLAMINGO_SNAPSHOT_PATH not set, snapshot skipped");
        return;
    }
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    REQUIRE(Gdiplus::GdiplusStartup(&token, &input, NULL) == Gdiplus::Ok);
    {
        screenrecord::CRecordDoneToast toast;
        toast.Show(L"录制完成，可发送给 韩雪飞、韩娟丹、韩晓燕888", L"2026-10-06 14-23-05.mp4", L"01:00 · 32.0 MB",
                   TR(L"发送"), NULL);
        REQUIRE(toast.IsWindow());
        CHECK(RdtsShot(toast.m_hWnd, std::wstring(dir) + L"/record_done_toast.png"));
        toast.Show(L"录制完成", L"2026-10-06 14-23-05.mp4", L"01:00 · 32.0 MB", TR(L"发送到会话…"), NULL, true);
        CHECK(RdtsShot(toast.m_hWnd, std::wstring(dir) + L"/record_done_toast_caption.png"));
        toast.Hide();
    }
    Gdiplus::GdiplusShutdown(token);
}
