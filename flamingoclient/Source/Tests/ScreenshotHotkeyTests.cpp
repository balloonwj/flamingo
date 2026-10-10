/**
 *  ScreenshotHotkeyTests.cpp
 *
 *  截图快捷键与「截图时隐藏当前窗口」的测试（docs/截图快捷键与隐藏当前窗口方案.md 第 4 节，
 *  2026-10-05 按用户要求自行确认）：
 *    · SK1~SK6：快捷键的编码与解码、默认值、两种显示文字、组合规则校验与冲突检查、按键事件
 *      合成、剪刀悬停提示与 ▾ 菜单的内容；
 *    · SX1~SX3：排除方式的选择；真实窗口被排除后抓屏看不到它、恢复后又能看到（SX2 会在屏幕上
 *      短暂显示一个红色窗口，桌面不可用时跳过）；空窗口与重复恢复不出错；
 *    · SC1~SC2：CUserConfig 两个新配置项的缺省值与读写；
 *    · SHK1~SHK2（2026-10-07，docs/截图独立模块方案.md）：未登录时也注册截图快捷键，解锁后的恢复按是否登录区分。
 *
 *  每条读写配置的用例都用独立的临时配置文件，文件名带进程 id，两个测试进程同时跑也不会共用。
 *  用 Catch2 v3（amalgamated）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <vector>

#include "../Feature/Screenshot/ScreenshotHotkey.h"
#include "../../FlamingoScreenshot/Capture/CaptureExclusion.h"
#include "../UI/BalloonChatWnd/ChatScreenshotMenu.h"
#include "../Session/UserConfig.h"
#include "../Feature/Lock/LockSuppressPolicy.h"   // SHK2：解锁后恢复快捷键的策略
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"

using namespace balloonscreenshot;

namespace {

// ---- SX2 用的真实窗口 ----
const wchar_t* const kRedWindowClass = L"FlamingoUnitShotExcludeRed";   // 测试窗口的窗口类名
const int  kRedWindowX = 160;          // 测试窗口位置与尺寸（像素）
const int  kRedWindowY = 160;
const int  kRedWindowW = 360;
const int  kRedWindowH = 240;
const int  kSampleInset = 24;          // 取样区域比窗口每边缩进的像素数，避开边缘的抗锯齿与阴影
const DWORD kSettleMs = 400;           // 显示窗口后等待桌面合成的时长（毫秒）
const DWORD kPumpSliceMs = 10;         // 等待期间每轮处理消息后休眠的时长（毫秒）
const double kMostlyRed = 0.95;        // 「几乎全是红色」的阈值
const double kAlmostNoRed = 0.02;      // 「几乎没有红色」的阈值
const DWORD kRedPixel = 0x00FF0000;    // 32 位位图里纯红像素的值（BGRA 排列，读成 DWORD 为 0x00RRGGBB）
const DWORD kRgbMask = 0x00FFFFFF;     // 去掉 alpha 字节
// SX3 的测试窗口位置（像素）：SX3 只需要一个已最小化的窗口，放在屏幕外，运行时不会出现在屏幕上
const int  kOffscreenPos = -32000;

// SetThreadDpiAwarenessContext 的函数原型（Win10 1607 起才有，按名字动态获取）
typedef HANDLE (WINAPI* FN_SetThreadDpiCtx)(HANDLE);
// DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 的哨兵值（与 ScreenCaptureOverlay.cpp 相同，避免依赖较新的 SDK 头文件）
const HANDLE kPerMonitorV2Ctx = (HANDLE)(INT_PTR)-4;

/**
 *  在作用域内把本线程切到 Per-Monitor-V2，离开作用域时恢复原来的上下文，与截图界面抓屏时的做法相同。
 *  测试程序本身不感知 DPI：系统缩放不是 100% 时，窗口用的是缩放后的逻辑坐标，而 GDI 抓屏取的是物理坐标，
 *  取样区域会偏出红色窗口（2026-10-05 在 125% 缩放下实测红色只占 0.869658，与按坐标偏移算出的 0.86966 一致，
 *  SX2 因此被误判为「桌面不可截」而跳过）。在这个上下文里创建的窗口与抓屏都用物理坐标，两者一致。
 *  系统没有 SetThreadDpiAwarenessContext 时不切换。
 */
class ScopedPerMonitorDpi
{
public:
    // 切换本线程的 DPI 上下文；系统不支持时什么也不做
    ScopedPerMonitorDpi()
        : m_setCtx(NULL)
        , m_oldCtx(NULL)
    {
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        if (user32 != NULL)
        {
            m_setCtx = (FN_SetThreadDpiCtx)::GetProcAddress(user32, "SetThreadDpiAwarenessContext");
        }
        if (m_setCtx != NULL)
        {
            m_oldCtx = m_setCtx(kPerMonitorV2Ctx);
        }
    }

    // 恢复切换之前的 DPI 上下文
    ~ScopedPerMonitorDpi()
    {
        if (m_setCtx != NULL && m_oldCtx != NULL)
        {
            m_setCtx(m_oldCtx);
        }
    }

private:
    FN_SetThreadDpiCtx m_setCtx;   // SetThreadDpiAwarenessContext 的地址；系统没有该函数时为 NULL
    HANDLE m_oldCtx;               // 切换之前的线程 DPI 上下文，析构时恢复；没有切换成功时为 NULL
};

// 测试窗口的窗口过程：背景画成纯红
LRESULT CALLBACK RedWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_ERASEBKGND)
    {
        RECT rc;
        ::GetClientRect(h, &rc);
        HBRUSH br = ::CreateSolidBrush(RGB(255, 0, 0));
        ::FillRect((HDC)w, &rc, br);
        ::DeleteObject(br);
        return 1;
    }
    return ::DefWindowProcW(h, m, w, l);
}

// 处理消息一段时间，让窗口画出来、桌面合成完毕
void PumpFor(DWORD ms)
{
    DWORD end = ::GetTickCount() + ms;
    while (::GetTickCount() < end)
    {
        MSG msg;
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(kPumpSliceMs);
    }
}

// 抓屏幕上 rc 区域，返回纯红像素的占比（0~1）
double RedRatio(const RECT& rc)
{
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    HDC scr = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(scr);
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = NULL;
    HBITMAP dib = ::CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = ::SelectObject(mem, dib);
    ::BitBlt(mem, 0, 0, w, h, scr, rc.left, rc.top, SRCCOPY | CAPTUREBLT);
    ::GdiFlush();
    long red = 0;
    const DWORD* p = (const DWORD*)bits;
    for (int i = 0; p != NULL && i < w * h; ++i)
    {
        if ((p[i] & kRgbMask) == kRedPixel)
        {
            ++red;
        }
    }
    ::SelectObject(mem, old);
    ::DeleteObject(dib);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, scr);
    return (w * h > 0) ? (double)red / (double)(w * h) : 0.0;
}

// 在 (x, y) 处建一个置顶的纯红窗口并等它显示出来；返回窗口句柄，失败返回 NULL。
// 窗口不激活（WS_EX_NOACTIVATE），不会抢走用户当前窗口的焦点
HWND CreateRedWindow(int x, int y)
{
    WNDCLASSW wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = RedWndProc;
    wc.hInstance = ::GetModuleHandleW(NULL);
    wc.lpszClassName = kRedWindowClass;
    ::RegisterClassW(&wc);   // 重复注册失败无妨，类已存在
    HWND hwnd = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kRedWindowClass, L"shot-exclude",
                                  WS_POPUP | WS_VISIBLE, x, y, kRedWindowW, kRedWindowH,
                                  NULL, NULL, wc.hInstance, NULL);
    if (hwnd != NULL)
    {
        PumpFor(kSettleMs);
    }
    return hwnd;
}

// 测试窗口内部的取样区域（屏幕坐标）
RECT SampleRect(HWND hwnd)
{
    RECT rc;
    ::GetWindowRect(hwnd, &rc);
    ::InflateRect(&rc, -kSampleInset, -kSampleInset);
    return rc;
}

// 生成本进程专用的临时配置文件路径，并删掉上一次残留的同名文件
std::wstring FreshIniPath(const wchar_t* tag)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t name[MAX_PATH] = { 0 };
    swprintf_s(name, L"%sflamingo_shotcfg_%s_%lu.ini", dir, tag, ::GetCurrentProcessId());
    ::DeleteFileW(name);
    return name;
}

} // anonymous namespace

// =====================================================================
// SK. 快捷键的纯逻辑
// =====================================================================

TEST_CASE("SK1 编码与解码互逆；默认值为 Alt+Z；0 表示停用", "[screenshot][hotkey]")
{
    DWORD hk = MakeHotkey(MOD_CONTROL | MOD_SHIFT, 'S');
    CHECK(HotkeyModifiers(hk) == (UINT)(MOD_CONTROL | MOD_SHIFT));
    CHECK(HotkeyVirtualKey(hk) == (UINT)'S');

    CHECK(HotkeyModifiers(kDefaultScreenshotHotkey) == (UINT)MOD_ALT);
    CHECK(HotkeyVirtualKey(kDefaultScreenshotHotkey) == (UINT)'Z');

    // Win 键等不认可的修饰键位被过滤掉
    DWORD withWin = MakeHotkey(MOD_ALT | MOD_WIN, 'Q');
    CHECK(HotkeyModifiers(withWin) == (UINT)MOD_ALT);

    CHECK(MakeHotkey(MOD_ALT, 0) == 0);
}

TEST_CASE("SK2 显示文字：键帽格式带空格，紧凑格式不带；修饰键按 Ctrl、Alt、Shift 排列", "[screenshot][hotkey]")
{
    CHECK(FormatHotkey(kDefaultScreenshotHotkey, kHotkeyTextKeyCap) == CString(L"Alt + Z"));
    CHECK(FormatHotkey(kDefaultScreenshotHotkey, kHotkeyTextCompact) == CString(L"Alt+Z"));

    DWORD all = MakeHotkey(MOD_SHIFT | MOD_ALT | MOD_CONTROL, VK_F12);
    CHECK(FormatHotkey(all, kHotkeyTextKeyCap) == CString(L"Ctrl + Alt + Shift + F12"));
    CHECK(FormatHotkey(MakeHotkey(MOD_CONTROL, '7'), kHotkeyTextCompact) == CString(L"Ctrl+7"));

    CHECK(FormatHotkey(0, kHotkeyTextKeyCap).IsEmpty());
}

TEST_CASE("SK3 组合校验：合规、停用、缺修饰键、主键不支持、与本程序快捷键重复", "[screenshot][hotkey]")
{
    ReservedHotkey reserved[2];
    reserved[0].m_hotkey = MakeHotkey(MOD_CONTROL | MOD_ALT, 'D');
    reserved[0].m_name = L"打开主面板";
    reserved[1].m_hotkey = MakeHotkey(MOD_CONTROL | MOD_ALT, 'R');
    reserved[1].m_name = L"录屏";
    int conflict = -2;

    CHECK(CheckHotkey(kDefaultScreenshotHotkey, reserved, 2, &conflict) == kHotkeyOk);
    CHECK(conflict == -1);
    CHECK(CheckHotkey(MakeHotkey(MOD_CONTROL | MOD_SHIFT, VK_F5), reserved, 2, NULL) == kHotkeyOk);

    CHECK(CheckHotkey(0, reserved, 2, NULL) == kHotkeyDisabled);

    CHECK(CheckHotkey(MakeHotkey(0, 'A'), reserved, 2, NULL) == kHotkeyNeedModifier);
    CHECK(CheckHotkey(MakeHotkey(MOD_SHIFT, 'A'), reserved, 2, NULL) == kHotkeyNeedModifier);

    CHECK(CheckHotkey(MakeHotkey(MOD_ALT, VK_SPACE), reserved, 2, NULL) == kHotkeyUnsupportedKey);
    CHECK(CheckHotkey(MakeHotkey(MOD_CONTROL, VK_F13), reserved, 2, NULL) == kHotkeyUnsupportedKey);

    CHECK(CheckHotkey(MakeHotkey(MOD_CONTROL | MOD_ALT, 'R'), reserved, 2, &conflict) == kHotkeyReserved);
    CHECK(conflict == 1);
    CHECK(CheckHotkey(MakeHotkey(MOD_CONTROL | MOD_ALT, 'D'), reserved, 2, &conflict) == kHotkeyReserved);
    CHECK(conflict == 0);
    // 修饰键不同就不算重复
    CHECK(CheckHotkey(MakeHotkey(MOD_CONTROL | MOD_ALT | MOD_SHIFT, 'D'), reserved, 2, NULL) == kHotkeyOk);
    // 没有保留表时不查重
    CHECK(CheckHotkey(MakeHotkey(MOD_CONTROL | MOD_ALT, 'D'), NULL, 0, NULL) == kHotkeyOk);
}

TEST_CASE("SK4 按键事件合成：只按修饰键时还在等主键；带上修饰键状态合成组合", "[screenshot][hotkey]")
{
    CHECK(ComposeHotkey(VK_MENU, false, true, false) == 0);
    CHECK(ComposeHotkey(VK_LCONTROL, true, false, false) == 0);
    CHECK(ComposeHotkey(VK_RSHIFT, false, false, true) == 0);
    CHECK(ComposeHotkey(VK_LWIN, false, false, false) == 0);

    CHECK(ComposeHotkey('Z', false, true, false) == kDefaultScreenshotHotkey);
    CHECK(ComposeHotkey('A', true, true, true) == MakeHotkey(MOD_CONTROL | MOD_ALT | MOD_SHIFT, 'A'));
    // 不带修饰键也照样合成，由 CheckHotkey 判为缺修饰键
    CHECK(ComposeHotkey('A', false, false, false) == MakeHotkey(0, 'A'));

    CHECK(IsSupportedHotkeyVk('0'));
    CHECK(IsSupportedHotkeyVk(VK_F1));
    CHECK_FALSE(IsSupportedHotkeyVk(VK_RETURN));
    CHECK(IsModifierVk(VK_CONTROL));
    CHECK_FALSE(IsModifierVk('Z'));
}

TEST_CASE("SK5 剪刀悬停提示与菜单第一项：有快捷键时带括号，停用时只写「截图」", "[screenshot][hotkey]")
{
    CHECK(ScreenshotCommandText(kDefaultScreenshotHotkey) == CString(L"截图（Alt+Z）"));
    CHECK(ScreenshotCommandText(MakeHotkey(MOD_CONTROL | MOD_ALT, 'A')) == CString(L"截图（Ctrl+Alt+A）"));
    CHECK(ScreenshotCommandText(0) == CString(L"截图"));
}

TEST_CASE("SK6 截图选项菜单：截图 / 截取全屏 / 分隔线 / 截图时隐藏当前窗口（勾选随设置）", "[screenshot][hotkey][menu]")
{
    balloonwjui::DuiMenu menu;
    balloonchatwnd::BuildScreenshotMenu(menu, kDefaultScreenshotHotkey, true);
    const std::vector<balloonwjui::DuiMenu::Item>& items = menu.GetItems();
    REQUIRE(items.size() == 4);
    CHECK(items[0].id == balloonchatwnd::kShotMenuIdCapture);
    CHECK(items[0].kind == balloonwjui::DuiMenu::ItemText);
    CHECK(items[0].text == CString(L"截图（Alt+Z）"));
    CHECK(items[1].id == balloonchatwnd::kShotMenuIdFullScreen);
    CHECK(items[1].text == CString(L"截取全屏"));
    CHECK(items[2].kind == balloonwjui::DuiMenu::ItemSeparator);
    CHECK(items[3].id == balloonchatwnd::kShotMenuIdHideWindow);
    CHECK(items[3].kind == balloonwjui::DuiMenu::ItemCheckable);
    CHECK(items[3].text == CString(L"截图时隐藏当前窗口"));
    CHECK(items[3].checked);

    // 重新搭建时先清空：勾选状态与快捷键文字跟着变
    balloonchatwnd::BuildScreenshotMenu(menu, 0, false);
    REQUIRE(menu.GetItems().size() == 4);
    CHECK(menu.GetItems()[0].text == CString(L"截图"));
    CHECK_FALSE(menu.GetItems()[3].checked);

    CHECK(balloonchatwnd::ScreenshotMenuChoiceFromId(balloonchatwnd::kShotMenuIdCapture) == balloonchatwnd::kShotMenuCapture);
    CHECK(balloonchatwnd::ScreenshotMenuChoiceFromId(balloonchatwnd::kShotMenuIdFullScreen) == balloonchatwnd::kShotMenuFullScreen);
    CHECK(balloonchatwnd::ScreenshotMenuChoiceFromId(balloonchatwnd::kShotMenuIdHideWindow) == balloonchatwnd::kShotMenuToggleHide);
    CHECK(balloonchatwnd::ScreenshotMenuChoiceFromId(0) == balloonchatwnd::kShotMenuNone);
    CHECK(balloonchatwnd::ScreenshotMenuChoiceFromId(99) == balloonchatwnd::kShotMenuNone);
}

// =====================================================================
// SX. 截图时把窗口排除在外
// =====================================================================

TEST_CASE("SX1 排除方式：窗口不可用时不排除；Win10 2004 起用显示属性，更早的系统隐藏窗口", "[screenshot][exclude]")
{
    CHECK(ChooseExclusionMode(false, 19045) == kExcludeNone);
    CHECK(ChooseExclusionMode(false, 7601) == kExcludeNone);
    CHECK(ChooseExclusionMode(true, kExcludeFromCaptureMinBuild) == kExcludeByAffinity);
    CHECK(ChooseExclusionMode(true, 22631) == kExcludeByAffinity);
    CHECK(ChooseExclusionMode(true, kExcludeFromCaptureMinBuild - 1) == kExcludeByHiding);
    CHECK(ChooseExclusionMode(true, 0) == kExcludeByHiding);
    CHECK(GetWindowsBuildNumber() > 0);
}

TEST_CASE("SX2 真实窗口被排除后抓屏看不到它，恢复后又能看到（两种方式各一次）", "[screenshot][exclude][desktop]")
{
    // 建窗口与抓屏都在 Per-Monitor-V2 上下文里进行，坐标一致（见 ScopedPerMonitorDpi 的说明）
    ScopedPerMonitorDpi dpi;
    HWND hwnd = CreateRedWindow(kRedWindowX, kRedWindowY);
    REQUIRE(hwnd != NULL);
    RECT sample = SampleRect(hwnd);
    double baseline = RedRatio(sample);
    if (baseline < kMostlyRed)
    {
        ::DestroyWindow(hwnd);
        SKIP("desktop is not capturable (locked screen or no interactive session), red ratio=" << baseline);
    }

    // 显示属性方式（只在支持的系统上测）
    if (GetWindowsBuildNumber() >= kExcludeFromCaptureMinBuild)
    {
        CCaptureExclusion ex;
        CHECK(ex.Begin(hwnd) == kExcludeByAffinity);
        CHECK(::IsWindowVisible(hwnd));              // 窗口照常显示
        CHECK(RedRatio(sample) < kAlmostNoRed);      // 但抓屏抓不到它
        ex.Restore();
        CHECK(ex.Mode() == kExcludeNone);
        CHECK(RedRatio(sample) > kMostlyRed);        // 恢复后立刻又能抓到
    }

    // 隐藏窗口方式（老系统上的退路）
    {
        CCaptureExclusion ex;
        CHECK(ex.BeginAs(hwnd, kExcludeByHiding) == kExcludeByHiding);
        CHECK_FALSE(::IsWindowVisible(hwnd));
        CHECK(RedRatio(sample) < kAlmostNoRed);
        ex.Restore();
        CHECK(::IsWindowVisible(hwnd));
        PumpFor(kSettleMs);
        CHECK(RedRatio(sample) > kMostlyRed);
    }

    // 对象析构时自动恢复
    {
        CCaptureExclusion ex;
        ex.BeginAs(hwnd, kExcludeByHiding);
    }
    CHECK(::IsWindowVisible(hwnd));

    ::DestroyWindow(hwnd);
}

TEST_CASE("SX3 空窗口、已最小化的窗口不排除；重复恢复不出错", "[screenshot][exclude]")
{
    CCaptureExclusion ex;
    CHECK(ex.Begin(NULL) == kExcludeNone);
    CHECK(ex.BeginAs(NULL, kExcludeByHiding) == kExcludeNone);
    ex.Restore();
    ex.Restore();
    CHECK(ex.Mode() == kExcludeNone);

    // 窗口建在屏幕外并以不激活的方式最小化：本用例不抓屏，不需要窗口出现在屏幕上，也不改变用户的前台窗口
    HWND hwnd = CreateRedWindow(kOffscreenPos, kOffscreenPos);
    REQUIRE(hwnd != NULL);
    ::ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
    REQUIRE(::IsIconic(hwnd));
    CHECK(ex.Begin(hwnd) == kExcludeNone);
    CHECK(ex.BeginAs(hwnd, kExcludeByAffinity) == kExcludeNone);
    ::DestroyWindow(hwnd);
    // 窗口销毁之后再恢复也不出错
    CCaptureExclusion ex2;
    ex2.Restore();
    CHECK(ex2.Mode() == kExcludeNone);
}

// =====================================================================
// SC. 用户配置
// =====================================================================

TEST_CASE("SC1 配置缺省值：截图快捷键为 Alt+Z，截图时隐藏当前窗口开启", "[screenshot][config]")
{
    {
        CUserConfig fresh;
        CHECK(fresh.GetScreenshotHotkey() == kDefaultScreenshotHotkey);
        CHECK(fresh.IsScreenshotHideWindow() != FALSE);
    }
    std::wstring path = FreshIniPath(L"default");
    CUserConfig cfg;
    REQUIRE(cfg.LoadConfig(path.c_str()));
    CHECK(cfg.GetScreenshotHotkey() == kDefaultScreenshotHotkey);
    CHECK(cfg.IsScreenshotHideWindow() != FALSE);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("SC2 写入后读回一致，含停用（0）与关闭隐藏", "[screenshot][config]")
{
    std::wstring path = FreshIniPath(L"roundtrip");
    DWORD custom = MakeHotkey(MOD_CONTROL | MOD_SHIFT, 'S');
    {
        CUserConfig writer;
        REQUIRE(writer.LoadConfig(path.c_str()));
        writer.SetScreenshotHotkey(custom);
        writer.EnableScreenshotHideWindow(FALSE);
        REQUIRE(writer.SaveConfig(path.c_str()));
    }
    {
        CUserConfig reader;
        REQUIRE(reader.LoadConfig(path.c_str()));
        CHECK(reader.GetScreenshotHotkey() == custom);
        CHECK(reader.IsScreenshotHideWindow() == FALSE);
        reader.SetScreenshotHotkey(0);
        REQUIRE(reader.SaveConfig(path.c_str()));
    }
    {
        // 停用要原样保存为 0，不能被读成缺省的 Alt+Z
        CUserConfig again;
        REQUIRE(again.LoadConfig(path.c_str()));
        CHECK(again.GetScreenshotHotkey() == 0);
    }
    ::DeleteFileW(path.c_str());
}

// =====================================================================
// SHK1~SHK2：未登录时也注册截图快捷键（docs/截图独立模块方案.md 3.5 节、第 4 节，2026-10-07 与用户确认）
// =====================================================================

TEST_CASE("SHK1 未登录时只注册截图快捷键，不注册打开主面板与录屏；登录后三个都按设置注册", "[shothotkey][screenshot]")
{
    const DWORD altZ = kDefaultScreenshotHotkey;
    //未登录：录屏设置开着也不注册录屏与 Ctrl+Alt+D，截图快捷键照常注册
    AppHotkeyPlan plan = PlanAppHotkeys(false, true, altZ);
    CHECK_FALSE(plan.m_showPanel);
    CHECK_FALSE(plan.m_record);
    CHECK(plan.m_screenshot == altZ);
    //未登录且截图快捷键停用：什么都不注册
    plan = PlanAppHotkeys(false, true, 0);
    CHECK_FALSE(plan.m_showPanel);
    CHECK_FALSE(plan.m_record);
    CHECK(plan.m_screenshot == 0);
    //已登录：三个都按设置注册
    plan = PlanAppHotkeys(true, true, altZ);
    CHECK(plan.m_showPanel);
    CHECK(plan.m_record);
    CHECK(plan.m_screenshot == altZ);
    //已登录、录屏快捷键关闭
    plan = PlanAppHotkeys(true, false, altZ);
    CHECK(plan.m_showPanel);
    CHECK_FALSE(plan.m_record);
    CHECK(plan.m_screenshot == altZ);
}

TEST_CASE("SHK2 未登录时解锁后的恢复只恢复截图快捷键", "[shothotkey][screenshot][lock]")
{
    const DWORD altZ = kDefaultScreenshotHotkey;
    //锁屏策略只决定「登录后才有的两个快捷键」要不要恢复：未登录不恢复，已登录恢复
    CHECK_FALSE(balloonlock::ShouldResumeHotkeysAfterUnlock(false));
    CHECK(balloonlock::ShouldResumeHotkeysAfterUnlock(true));
    //解锁时按策略的结果求注册计划：未登录只剩截图快捷键
    AppHotkeyPlan plan = PlanAppHotkeys(balloonlock::ShouldResumeHotkeysAfterUnlock(false), true, altZ);
    CHECK_FALSE(plan.m_showPanel);
    CHECK_FALSE(plan.m_record);
    CHECK(plan.m_screenshot == altZ);
    plan = PlanAppHotkeys(balloonlock::ShouldResumeHotkeysAfterUnlock(true), true, altZ);
    CHECK(plan.m_showPanel);
    CHECK(plan.m_record);
    CHECK(plan.m_screenshot == altZ);
}
