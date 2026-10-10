/**
 *  锁屏 DLL 核心逻辑的测试（标签 [lockdll]），覆盖 docs/锁屏方案.md 第 7 节（2026-10-06 按用户要求自行确定）：
 *    - LP1~LP5 临时密码规则（CheckNewPassword）
 *    - LV1~LV6 密码校验（CLockPasswordVerifier）：正确、错误、盐与摘要、清除、冷却、计数清零
 *    - LU1~LU9 界面状态机（CLockUiState）
 *    - LK1~LK6 键盘与鼠标过滤（DecideLockKey / DecideLockMouse）
 *    - LB1~LB4 锁屏界面上的密码框（CLockPasswordBox）
 *    - LA1~LA4 锁屏画面的选择（LockAnimationChoice）
 *    - LL1~LL3 按钮栏、密码框与确认框的布局与命中测试（LockOverlayLayout）；LL4 预览提示胶囊的位置（2026-10-07）
 *    - PV1~PV4 设置窗口「预览」的状态与判定（CLockPreviewState，2026-10-07）：先播哪一种、轮流播放的换片、
 *              哪些输入结束预览与开始后的输入保护时间
 *  都是纯逻辑，不建窗口、不装钩子。时刻一律由用例传入。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <cstdlib>   //std::abs
#include <set>
#include <string>
#include <vector>

#include "../Core/LockPassword.h"
#include "../Core/LockUiState.h"
#include "../Core/LockAnimationChoice.h"
#include "../Core/LockOverlayLayout.h"
#include "../Core/LockPreviewState.h"

#pragma comment(lib, "bcrypt.lib")   //LockPassword.cpp 用 CNG 生成随机盐、计算 SHA-256

using namespace flamingolock;

namespace {

const uint64_t kT0 = 1000000;       //用例里的起始时刻（毫秒），取一个不为 0 的值
const uint64_t kOneMs = 1;          //一毫秒
const uint32_t kBullet = 0x25CF;    //密码框显示用的圆点（U+25CF）

//按虚拟键码与修饰键状态拼一个按键事件
LockKeyEvent Key(uint32_t vk, bool up, bool alt, bool ctrl, bool win, bool injected)
{
    LockKeyEvent e;
    e.m_vk = vk;
    e.m_keyUp = up;
    e.m_altDown = alt;
    e.m_ctrlDown = ctrl;
    e.m_winDown = win;
    e.m_injected = injected;
    return e;
}

//普通的按下事件（无修饰键、非合成）
LockKeyEvent Down(uint32_t vk)
{
    return Key(vk, false, false, false, false, false);
}

//矩形中心点
POINT Center(const RECT& rc)
{
    POINT pt;
    pt.x = (rc.left + rc.right) / 2;
    pt.y = (rc.top + rc.bottom) / 2;
    return pt;
}

//两个矩形是否相交
bool Overlaps(const RECT& a, const RECT& b)
{
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

} // namespace

// =====================================================================
// LP —— 临时密码规则
// =====================================================================

TEST_CASE("LP1 长度下限 4 与上限 32 的边界", "[lockdll]")
{
    CHECK(CheckNewPassword(L"abc", L"abc") == kPasswordTooShort);
    CHECK(CheckNewPassword(L"abcd", L"abcd") == kPasswordOk);
    const std::wstring max32(kMaxPasswordChars, L'x');
    const std::wstring over33(kMaxPasswordChars + 1, L'x');
    CHECK(CheckNewPassword(max32, max32) == kPasswordOk);
    CHECK(CheckNewPassword(over33, over33) == kPasswordTooLong);
}

TEST_CASE("LP2 空密码", "[lockdll]")
{
    CHECK(CheckNewPassword(L"", L"") == kPasswordEmpty);
    CHECK(CheckNewPassword(L"", L"abcd") == kPasswordEmpty);
}

TEST_CASE("LP3 含非 ASCII 或不可打印字符", "[lockdll]")
{
    CHECK(CheckNewPassword(L"密码1234", L"密码1234") == kPasswordBadChar);   //i18n-ignore: 测试数据
    CHECK(CheckNewPassword(L"ab\tcd", L"ab\tcd") == kPasswordBadChar);
    CHECK(CheckNewPassword(L"ＡＢＣＤ", L"ＡＢＣＤ") == kPasswordBadChar);   //全角字母
    CHECK(IsAllowedPasswordChar(L' '));
    CHECK(IsAllowedPasswordChar(L'~'));
    CHECK_FALSE(IsAllowedPasswordChar(static_cast<wchar_t>(0x7F)));
    CHECK_FALSE(IsAllowedPasswordChar(static_cast<wchar_t>(0x1F)));
}

TEST_CASE("LP4 两次输入不一致", "[lockdll]")
{
    CHECK(CheckNewPassword(L"abcd", L"abce") == kPasswordMismatch);
    CHECK(CheckNewPassword(L"abcd", L"") == kPasswordMismatch);
}

TEST_CASE("LP5 合规密码（含空格与符号）", "[lockdll]")
{
    CHECK(CheckNewPassword(L"a b!@#", L"a b!@#") == kPasswordOk);
    //先查第一个框：第一个框过短时即使两次不一致也报「过短」
    CHECK(CheckNewPassword(L"ab", L"abcd") == kPasswordTooShort);
}

// =====================================================================
// LV —— 密码校验
// =====================================================================

TEST_CASE("LV1 正确的密码通过，错误的不通过", "[lockdll]")
{
    CLockPasswordVerifier v;
    REQUIRE(v.Set(L"secret12"));
    CHECK(v.IsSet());
    CHECK(v.Verify(L"secret13", kT0) == kVerifyWrong);
    CHECK(v.FailureCount() == 1);
    CHECK(v.Verify(L"secret12", kT0 + kOneMs) == kVerifyOk);
}

TEST_CASE("LV2 同一密码两次设置，盐与摘要都不同", "[lockdll]")
{
    CLockPasswordVerifier a;
    CLockPasswordVerifier b;
    REQUIRE(a.Set(L"same-pass"));
    REQUIRE(b.Set(L"same-pass"));
    std::vector<uint8_t> saltA;
    std::vector<uint8_t> digestA;
    std::vector<uint8_t> saltB;
    std::vector<uint8_t> digestB;
    a.CopyForTest(saltA, digestA);
    b.CopyForTest(saltB, digestB);
    REQUIRE(saltA.size() == static_cast<size_t>(CLockPasswordVerifier::kSaltBytes));
    REQUIRE(digestA.size() == static_cast<size_t>(CLockPasswordVerifier::kDigestBytes));
    CHECK(saltA != saltB);
    CHECK(digestA != digestB);
}

TEST_CASE("LV3 清除后任何输入都不通过；从未设置时同样", "[lockdll]")
{
    CLockPasswordVerifier never;
    CHECK(never.Verify(L"", kT0) == kVerifyNotSet);
    CLockPasswordVerifier v;
    REQUIRE(v.Set(L"abcd"));
    v.Clear();
    CHECK_FALSE(v.IsSet());
    CHECK(v.Verify(L"abcd", kT0) == kVerifyNotSet);
    std::vector<uint8_t> salt;
    std::vector<uint8_t> digest;
    v.CopyForTest(salt, digest);
    bool allZero = true;
    for (size_t i = 0; i < salt.size(); ++i)
        allZero = allZero && (salt[i] == 0);
    for (size_t i = 0; i < digest.size(); ++i)
        allZero = allZero && (digest[i] == 0);
    CHECK(allZero);
}

TEST_CASE("LV4 连续输错 5 次后 30 秒内拒绝尝试，正确密码也不比对", "[lockdll]")
{
    CLockPasswordVerifier v;
    REQUIRE(v.Set(L"right-one"));
    for (int i = 0; i < kMaxPasswordFailures - 1; ++i)
        CHECK(v.Verify(L"wrong", kT0 + i) == kVerifyWrong);
    //第 5 次输错：进入冷却，计数归零
    CHECK(v.Verify(L"wrong", kT0 + kMaxPasswordFailures) != kVerifyOk);
    CHECK(v.FailureCount() == 0);
    const uint64_t coolStart = kT0 + kMaxPasswordFailures;
    CHECK(v.CooldownRemainingMs(coolStart) == kPasswordCooldownMs);
    CHECK(v.Verify(L"right-one", coolStart + kOneMs) == kVerifyCoolingDown);
    CHECK(v.Verify(L"right-one", coolStart + kPasswordCooldownMs - kOneMs) == kVerifyCoolingDown);
}

TEST_CASE("LV5 冷却 30 秒结束后恢复比对", "[lockdll]")
{
    CLockPasswordVerifier v;
    REQUIRE(v.Set(L"right-one"));
    uint64_t now = kT0;
    for (int i = 0; i < kMaxPasswordFailures; ++i)
        v.Verify(L"wrong", now++);
    const uint64_t coolStart = now - kOneMs;
    CHECK(v.CooldownRemainingMs(coolStart + kPasswordCooldownMs) == 0);
    CHECK(v.Verify(L"wrong", coolStart + kPasswordCooldownMs) == kVerifyWrong);
    CHECK(v.Verify(L"right-one", coolStart + kPasswordCooldownMs + kOneMs) == kVerifyOk);
}

TEST_CASE("LV6 输对后错误计数清零", "[lockdll]")
{
    CLockPasswordVerifier v;
    REQUIRE(v.Set(L"right-one"));
    v.Verify(L"x1", kT0);
    v.Verify(L"x2", kT0 + 1);
    v.Verify(L"x3", kT0 + 2);
    CHECK(v.FailureCount() == 3);
    CHECK(v.Verify(L"right-one", kT0 + 3) == kVerifyOk);
    CHECK(v.FailureCount() == 0);
    //清零后又可以连错 4 次而不进入冷却
    for (int i = 0; i < kMaxPasswordFailures - 1; ++i)
        CHECK(v.Verify(L"wrong", kT0 + 10 + i) == kVerifyWrong);
    CHECK(v.CooldownRemainingMs(kT0 + 20) == 0);
}

// =====================================================================
// LU —— 界面状态机
// =====================================================================

TEST_CASE("LU1 初始只有动画，没有按钮栏，也不接受键盘", "[lockdll]")
{
    CLockUiState s;
    CHECK(s.Phase() == kUiIdle);
    CHECK_FALSE(s.IsBarVisible());
    CHECK_FALSE(s.AcceptsKeyboard());
    CHECK(s.ActiveMonitor() == -1);
    //只有动画时按按钮无效
    CHECK(s.OnButton(kBtnUnlock, kT0) == kCmdNone);
    CHECK(s.Phase() == kUiIdle);
}

TEST_CASE("LU2 单击出现按钮栏，落在被点击的显示器", "[lockdll]")
{
    CLockUiState s;
    s.OnClick(1, kT0);
    CHECK(s.Phase() == kUiButtons);
    CHECK(s.ActiveMonitor() == 1);
    CHECK(s.IsBarVisible());
    CHECK_FALSE(s.AcceptsKeyboard());
}

TEST_CASE("LU3 点「解锁」出现密码框并要求取得焦点", "[lockdll]")
{
    CLockUiState s;
    s.OnClick(0, kT0);
    CHECK(s.OnButton(kBtnUnlock, kT0 + 1) == kCmdFocusPassword);
    CHECK(s.Phase() == kUiPassword);
    CHECK(s.AcceptsKeyboard());
}

TEST_CASE("LU4 按钮或密码状态下 15 秒无操作回到只有动画", "[lockdll]")
{
    CLockUiState s;
    s.OnClick(0, kT0);
    CHECK_FALSE(s.Tick(kT0 + kUiIdleTimeoutMs - kOneMs));
    CHECK(s.Tick(kT0 + kUiIdleTimeoutMs));
    CHECK(s.Phase() == kUiIdle);
    CHECK(s.ActiveMonitor() == -1);

    CLockUiState p;
    p.OnClick(0, kT0);
    p.OnButton(kBtnUnlock, kT0);
    //打字算操作：从最后一次操作起计时
    p.OnActivity(kT0 + 10000);
    CHECK_FALSE(p.Tick(kT0 + kUiIdleTimeoutMs));
    CHECK(p.Tick(kT0 + 10000 + kUiIdleTimeoutMs));
    CHECK(p.Phase() == kUiIdle);
}

TEST_CASE("LU5 确认框打开期间不计空闲", "[lockdll]")
{
    CLockUiState s;
    s.OnClick(0, kT0);
    s.OnButton(kBtnShutdown, kT0);
    CHECK(s.Phase() == kUiConfirmShutdown);
    CHECK_FALSE(s.Tick(kT0 + kUiIdleTimeoutMs * 10));
    CHECK(s.Phase() == kUiConfirmShutdown);
    //确认框打开时点框外不起作用
    s.OnClick(1, kT0 + 1);
    CHECK(s.Phase() == kUiConfirmShutdown);
}

TEST_CASE("LU6 确认框取消回到按钮；确认后按种类给出关机或重启", "[lockdll]")
{
    CLockUiState s;
    s.OnClick(0, kT0);
    s.OnButton(kBtnRestart, kT0);
    CHECK(s.Phase() == kUiConfirmRestart);
    CHECK(s.OnButton(kBtnConfirmCancel, kT0 + 1) == kCmdNone);
    CHECK(s.Phase() == kUiButtons);
    s.OnButton(kBtnRestart, kT0 + 2);
    CHECK(s.OnButton(kBtnConfirmOk, kT0 + 3) == kCmdRestart);
    s.OnButton(kBtnShutdown, kT0 + 4);
    CHECK(s.OnButton(kBtnConfirmOk, kT0 + 5) == kCmdShutdown);
    //没有确认框时点「确认」无效
    CHECK(s.OnButton(kBtnConfirmOk, kT0 + 6) == kCmdNone);
}

TEST_CASE("LU7 只有动画时按键被拦截，Esc 逐级退回", "[lockdll]")
{
    //只有动画时普通按键：前台是锁屏窗口也拦截（按键唤不出任何东西）
    CHECK(DecideLockKey(Down('A'), true, false) == kKeyBlock);
    CHECK(DecideLockKey(Down(VK_RETURN), true, false) == kKeyBlock);
    CLockUiState s;
    s.OnClick(0, kT0);
    s.OnButton(kBtnUnlock, kT0);
    CHECK_FALSE(s.OnEscape(kT0 + 1));
    CHECK(s.Phase() == kUiButtons);
    CHECK(s.OnEscape(kT0 + 2));
    CHECK(s.Phase() == kUiIdle);
    CHECK_FALSE(s.OnEscape(kT0 + 3));
}

TEST_CASE("LU8 密码阶段且前台是锁屏窗口时普通按键放行（交给密码框）", "[lockdll]")
{
    CHECK(DecideLockKey(Down('A'), true, true) == kKeyPass);
    CHECK(DecideLockKey(Down(VK_BACK), true, true) == kKeyPass);
    CHECK(DecideLockKey(Down(VK_RETURN), true, true) == kKeyPass);
    //前台不是锁屏窗口（被别的程序抢了前台）时一律拦截
    CHECK(DecideLockKey(Down('A'), false, true) == kKeyBlock);
}

TEST_CASE("LU9 按钮栏随单击挪到另一块显示器，阶段不变", "[lockdll]")
{
    CLockUiState s;
    s.OnClick(0, kT0);
    s.OnButton(kBtnUnlock, kT0);
    s.OnClick(2, kT0 + 1);
    CHECK(s.ActiveMonitor() == 2);
    CHECK(s.Phase() == kUiPassword);
    s.Reset();
    CHECK(s.Phase() == kUiIdle);
    CHECK(s.ActiveMonitor() == -1);
}

// =====================================================================
// LK —— 键盘与鼠标过滤
// =====================================================================

TEST_CASE("LK1 Win 键本身与带 Win 的组合一律拦截（含抬起）", "[lockdll]")
{
    CHECK(DecideLockKey(Down(VK_LWIN), true, true) == kKeyBlock);
    CHECK(DecideLockKey(Down(VK_RWIN), true, true) == kKeyBlock);
    CHECK(DecideLockKey(Key(VK_LWIN, true, false, false, false, false), true, true) == kKeyBlock);
    CHECK(DecideLockKey(Key('D', false, false, false, true, false), true, true) == kKeyBlock);
    CHECK(DecideLockKey(Key('L', true, false, false, true, false), true, true) == kKeyBlock);
}

TEST_CASE("LK2 Alt 组合（Alt+Tab、Alt+Esc、Alt+F4、Alt+Space）一律拦截，Alt 本身按下放行到下一条判定", "[lockdll]")
{
    const uint32_t keys[4] = { VK_TAB, VK_ESCAPE, VK_F4, VK_SPACE };
    for (int i = 0; i < 4; ++i)
    {
        INFO("vk = " << keys[i]);
        CHECK(DecideLockKey(Key(keys[i], false, true, false, false, false), true, true) == kKeyBlock);
    }
    //Alt 本身在密码阶段放行（不影响密码输入；单独按 Alt 不会切走窗口）
    CHECK(DecideLockKey(Key(VK_LMENU, false, true, false, false, false), true, true) == kKeyPass);
}

TEST_CASE("LK3 Ctrl+Esc 与 Ctrl+Shift+Esc 拦截", "[lockdll]")
{
    CHECK(DecideLockKey(Key(VK_ESCAPE, false, false, true, false, false), true, true) == kKeyBlock);
    //Ctrl+Shift+Esc：Shift 不单独维护，Ctrl 按着、键为 Esc 即拦截
    CHECK(DecideLockKey(Key(VK_ESCAPE, false, false, true, false, false), false, false) == kKeyBlock);
}

TEST_CASE("LK4 Apps 键与 PrintScreen 拦截", "[lockdll]")
{
    CHECK(DecideLockKey(Down(VK_APPS), true, true) == kKeyBlock);
    CHECK(DecideLockKey(Down(VK_SNAPSHOT), true, true) == kKeyBlock);
}

TEST_CASE("LK5 非 Win 键的抬起一律放行（锁屏前按住的键能正常松开）", "[lockdll]")
{
    CHECK(DecideLockKey(Key('A', true, false, false, false, false), false, false) == kKeyPass);
    CHECK(DecideLockKey(Key(VK_MENU, true, true, false, false, false), false, false) == kKeyPass);
}

TEST_CASE("LK6 程序合成的按键与鼠标点击拦截，合成的鼠标移动放行（防休眠）", "[lockdll]")
{
    CHECK(DecideLockKey(Key('A', false, false, false, false, true), true, true) == kKeyBlock);
    CHECK(DecideLockKey(Key('A', true, false, false, false, true), true, true) == kKeyBlock);
    CHECK(DecideLockMouse(WM_LBUTTONDOWN, true) == kKeyBlock);
    CHECK(DecideLockMouse(WM_MOUSEWHEEL, true) == kKeyBlock);
    CHECK(DecideLockMouse(WM_MOUSEMOVE, true) == kKeyPass);
    CHECK(DecideLockMouse(WM_LBUTTONDOWN, false) == kKeyPass);
}

// =====================================================================
// LB —— 锁屏界面上的密码框
// =====================================================================

TEST_CASE("LB1 可打印 ASCII 追加，其它字符忽略", "[lockdll]")
{
    CLockPasswordBox box;
    CHECK(box.InsertChar(L'a'));
    CHECK(box.InsertChar(L' '));
    CHECK_FALSE(box.InsertChar(L'中'));   //i18n-ignore: 测试数据
    CHECK_FALSE(box.InsertChar(L'\r'));
    CHECK(box.Text() == L"a ");
    CHECK(box.Length() == 2);
}

TEST_CASE("LB2 最多 32 个字符", "[lockdll]")
{
    CLockPasswordBox box;
    for (int i = 0; i < kMaxPasswordChars; ++i)
        CHECK(box.InsertChar(L'x'));
    CHECK_FALSE(box.InsertChar(L'y'));
    CHECK(box.Length() == kMaxPasswordChars);
}

TEST_CASE("LB3 退格删除最后一个字符，空时返回 false；清空", "[lockdll]")
{
    CLockPasswordBox box;
    box.InsertChar(L'a');
    box.InsertChar(L'b');
    CHECK(box.Backspace());
    CHECK(box.Text() == L"a");
    CHECK(box.Backspace());
    CHECK_FALSE(box.Backspace());
    box.InsertChar(L'z');
    box.Clear();
    CHECK(box.Length() == 0);
    CHECK(box.Text().empty());
}

TEST_CASE("LB4 显示为与字符数相同个数的圆点", "[lockdll]")
{
    CLockPasswordBox box;
    CHECK(box.MaskText().empty());
    box.InsertChar(L'p');
    box.InsertChar(L'w');
    box.InsertChar(L'1');
    const std::wstring mask = box.MaskText();
    REQUIRE(mask.size() == 3);
    for (size_t i = 0; i < mask.size(); ++i)
        CHECK(static_cast<uint32_t>(mask[i]) == kBullet);
}

// =====================================================================
// LA —— 锁屏画面的选择
// =====================================================================

TEST_CASE("LA1 配置值与选择互转；不认识的值退回八荣八耻", "[lockdll]")
{
    const char* tokens[kChoiceCount] = { "honor", "flamingo", "particle", "bounce", "rain", "random", "rotate" };
    for (int i = 0; i < kChoiceCount; ++i)
    {
        INFO("token = " << tokens[i]);
        const LockAnimationChoice c = ChoiceFromToken(tokens[i]);
        CHECK(std::string(ChoiceToToken(c)) == tokens[i]);
        CHECK(ChoiceIndex(c) == i);
        CHECK(ChoiceIndex(ChoiceAt(i)) == i);
    }
    const LockAnimationChoice def = DefaultChoice();
    CHECK(def.m_mode == kAnimModeFixed);
    CHECK(def.m_fixed == kAnimHonor);
    CHECK(ChoiceFromToken(NULL).m_fixed == kAnimHonor);
    CHECK(ChoiceFromToken("").m_mode == kAnimModeFixed);
    CHECK(ChoiceFromToken("Honor").m_fixed == kAnimHonor);   //大小写敏感：按不认识处理
    CHECK(ChoiceFromToken("snow").m_mode == kAnimModeFixed);
    CHECK(ChoiceAt(-1).m_fixed == kAnimHonor);
    CHECK(ChoiceAt(kChoiceCount).m_fixed == kAnimHonor);
}

TEST_CASE("LA2 固定方式播选中的那一种", "[lockdll]")
{
    for (int i = 0; i < kAnimCount; ++i)
    {
        const LockAnimationChoice c = ChoiceAt(i);
        CHECK(PickInitialAnimation(c, 12345u) == static_cast<LockAnimation>(i));
    }
}

TEST_CASE("LA3 「随机」只在五种动画里取，并且五种都取得到", "[lockdll]")
{
    const LockAnimationChoice c = ChoiceFromToken("random");
    std::set<int> seen;
    for (uint32_t r = 0; r < 100; ++r)
    {
        const LockAnimation a = PickInitialAnimation(c, r * 2654435761u);
        CHECK(static_cast<int>(a) >= 0);
        CHECK(static_cast<int>(a) < kAnimCount);
        seen.insert(static_cast<int>(a));
    }
    CHECK(seen.size() == static_cast<size_t>(kAnimCount));
}

TEST_CASE("LA4 「轮流播放」从八荣八耻开始，每 60 秒换下一种并循环", "[lockdll]")
{
    const LockAnimationChoice c = ChoiceFromToken("rotate");
    CHECK(PickInitialAnimation(c, 999u) == kAnimHonor);
    CHECK(kRotateIntervalMs == 60000u);
    LockAnimation a = kAnimHonor;
    for (int i = 0; i < kAnimCount; ++i)
        a = NextAnimation(a);
    CHECK(a == kAnimHonor);
    CHECK(NextAnimation(kAnimRain) == kAnimHonor);
    CHECK(NextAnimation(kAnimHonor) == kAnimFlamingo);
}

// =====================================================================
// LL —— 布局与命中测试
// =====================================================================

TEST_CASE("LL1 按钮栏在显示器底部水平居中，三个按钮的命中区域不重叠", "[lockdll]")
{
    const int w = 1920;
    const int h = 1080;
    const int content[kOverlayButtonCount] = { 60, 60, 60 };
    const LockOverlayLayout L = ComputeOverlayLayout(w, h, 1.0f, content);
    //整排按钮的中心落在显示器正中（允许 1 像素取整误差）
    const int left = L.m_unlockButton.left;
    const int right = L.m_shutdownButton.right;
    CHECK(std::abs((left + right) / 2 - w / 2) <= 1);
    //底边在距底 9% 处，按钮栏在下半屏
    CHECK(L.m_unlockButton.bottom <= h);
    CHECK(L.m_unlockButton.top > h / 2);
    CHECK_FALSE(Overlaps(L.m_unlockButton, L.m_restartButton));
    CHECK_FALSE(Overlaps(L.m_restartButton, L.m_shutdownButton));
    CHECK(L.m_unlockButton.right < L.m_restartButton.left);
    //按钮阶段点中各自的按钮
    CHECK(HitTestOverlay(L, kUiButtons, Center(L.m_unlockButton)) == kHitUnlock);
    CHECK(HitTestOverlay(L, kUiButtons, Center(L.m_restartButton)) == kHitRestart);
    CHECK(HitTestOverlay(L, kUiButtons, Center(L.m_shutdownButton)) == kHitShutdown);
    //只有动画时点哪里都不算点中
    CHECK(HitTestOverlay(L, kUiIdle, Center(L.m_unlockButton)) == kHitNothing);
}

TEST_CASE("LL2 密码框在按钮栏上方；只在密码阶段可点", "[lockdll]")
{
    const LockOverlayLayout L = ComputeOverlayLayout(2560, 1440, 1.5f, NULL);
    CHECK(L.m_passwordBox.bottom <= L.m_passwordMessage.top);
    CHECK(L.m_passwordMessage.bottom <= L.m_unlockButton.top);
    CHECK(L.m_passwordGo.left > L.m_passwordBox.right);
    CHECK(HitTestOverlay(L, kUiPassword, Center(L.m_passwordBox)) == kHitPasswordBox);
    CHECK(HitTestOverlay(L, kUiPassword, Center(L.m_passwordGo)) == kHitPasswordGo);
    CHECK(HitTestOverlay(L, kUiButtons, Center(L.m_passwordBox)) == kHitNothing);
    //密码阶段按钮栏照常可点
    CHECK(HitTestOverlay(L, kUiPassword, Center(L.m_unlockButton)) == kHitUnlock);
}

TEST_CASE("LL3 确认框在屏幕正中，打开时只测确认框，框外不算点中", "[lockdll]")
{
    const int w = 1366;
    const int h = 768;
    const LockOverlayLayout L = ComputeOverlayLayout(w, h, 1.0f, NULL);
    const POINT c = Center(L.m_confirmPanel);
    CHECK(std::abs(c.x - w / 2) <= 1);
    CHECK(std::abs(c.y - h / 2) <= 1);
    CHECK(HitTestOverlay(L, kUiConfirmShutdown, Center(L.m_confirmOk)) == kHitConfirmOk);
    CHECK(HitTestOverlay(L, kUiConfirmRestart, Center(L.m_confirmCancel)) == kHitConfirmCancel);
    CHECK(HitTestOverlay(L, kUiConfirmShutdown, Center(L.m_confirmTitle)) == kHitConfirmPanel);
    //确认框打开时，按钮栏被遮住不可点
    CHECK(HitTestOverlay(L, kUiConfirmShutdown, Center(L.m_unlockButton)) == kHitNothing);
    CHECK_FALSE(Overlaps(L.m_confirmOk, L.m_confirmCancel));
    //按钮内容很宽时按钮随之加宽，仍不重叠
    const int wide[kOverlayButtonCount] = { 300, 300, 300 };
    const LockOverlayLayout W = ComputeOverlayLayout(w, h, 1.0f, wide);
    CHECK(W.m_unlockButton.right - W.m_unlockButton.left >= 300);
    CHECK_FALSE(Overlaps(W.m_unlockButton, W.m_restartButton));
}

TEST_CASE("LL4 预览提示胶囊在画面上方水平居中，宽度随文字，过长时截到屏幕之内", "[lockdll]")
{
    const int w = 1920;
    const int h = 1080;
    const int textW = 200;
    const RECT r = ComputePreviewHintRect(w, h, 1.0f, textW);
    //水平居中（允许 1 像素取整误差），顶边在 6% 高度，高 40
    CHECK(std::abs((r.left + r.right) / 2 - w / 2) <= 1);
    CHECK(std::abs(r.top - h * 6 / 100) <= 1);
    CHECK(r.bottom - r.top == 40);
    //宽度 = 文字宽度 + 两侧各 24 的留白
    CHECK(r.right - r.left == textW + 48);
    //在画面上半部分，不与按钮栏（底部）重叠
    const LockOverlayLayout L = ComputeOverlayLayout(w, h, 1.0f, NULL);
    CHECK(r.bottom < L.m_passwordBox.top);
    //缩放 1.5 倍时高度与留白按比例放大
    const RECT big = ComputePreviewHintRect(w, h, 1.5f, textW);
    CHECK(big.bottom - big.top == 60);
    CHECK(big.right - big.left == textW + 72);
    //文字比屏幕还宽：截到距左右边缘各 16 像素
    const RECT clipped = ComputePreviewHintRect(800, 600, 1.0f, 5000);
    CHECK(clipped.left == 16);
    CHECK(clipped.right == 800 - 16);
    //负数文字宽度按 0 处理，只剩两侧留白
    const RECT empty = ComputePreviewHintRect(w, h, 1.0f, -5);
    CHECK(empty.right - empty.left == 48);
}

// =====================================================================
// PV —— 设置窗口「预览」的状态与判定（CLockPreviewState，2026-10-07）
// =====================================================================

TEST_CASE("PV1 预览先播哪一种：固定方式为所选，「随机」只在五种里取且都取得到，「轮流播放」从八荣八耻开始", "[lockdll]")
{
    for (int i = 0; i < kAnimCount; ++i)
    {
        CLockPreviewState st;
        CHECK(st.Start(ChoiceAt(i), 777u, kT0) == static_cast<LockAnimation>(i));
        CHECK(st.Current() == static_cast<LockAnimation>(i));
        CHECK_FALSE(st.IsEnding());
    }
    std::set<int> seen;
    for (uint32_t r = 0; r < 100; ++r)
    {
        CLockPreviewState st;
        const LockAnimation a = st.Start(ChoiceFromToken("random"), r * 2654435761u, kT0);
        CHECK(static_cast<int>(a) >= 0);
        CHECK(static_cast<int>(a) < kAnimCount);
        seen.insert(static_cast<int>(a));
    }
    CHECK(seen.size() == static_cast<size_t>(kAnimCount));
    CLockPreviewState rot;
    CHECK(rot.Start(ChoiceFromToken("rotate"), 999u, kT0) == kAnimHonor);
}

TEST_CASE("PV2 预览「轮流播放」每 5 秒换下一种并循环；固定与随机方式一直不换", "[lockdll]")
{
    CHECK(kPreviewRotateIntervalMs == 5000u);
    CLockPreviewState st;
    st.Start(ChoiceFromToken("rotate"), 0u, kT0);
    //不到 5 秒不换
    CHECK_FALSE(st.Tick(kT0 + kPreviewRotateIntervalMs - kOneMs));
    CHECK(st.Current() == kAnimHonor);
    //满 5 秒换下一种，并从这一刻重新计时
    CHECK(st.Tick(kT0 + kPreviewRotateIntervalMs));
    CHECK(st.Current() == kAnimFlamingo);
    CHECK(st.CurrentElapsedMs(kT0 + kPreviewRotateIntervalMs) == 0u);
    CHECK_FALSE(st.Tick(kT0 + kPreviewRotateIntervalMs * 2 - kOneMs));
    //五次换片后回到八荣八耻
    uint64_t now = kT0 + kPreviewRotateIntervalMs;
    for (int i = 1; i < kAnimCount; ++i)
    {
        now += kPreviewRotateIntervalMs;
        CHECK(st.Tick(now));
    }
    CHECK(st.Current() == kAnimHonor);
    //固定与随机方式：过多久都不换
    CLockPreviewState fixed;
    fixed.Start(ChoiceAt(kAnimRain), 0u, kT0);
    CHECK_FALSE(fixed.Tick(kT0 + kPreviewRotateIntervalMs * 10));
    CHECK(fixed.Current() == kAnimRain);
    CLockPreviewState random;
    const LockAnimation a = random.Start(ChoiceFromToken("random"), 3u, kT0);
    CHECK_FALSE(random.Tick(kT0 + kPreviewRotateIntervalMs * 10));
    CHECK(random.Current() == a);
}

TEST_CASE("PV3 窗口就绪后 400 毫秒内单击与按键不结束预览，之后结束；鼠标移动与字符输入任何时候都不结束", "[lockdll]")
{
    CHECK(kPreviewInputGraceMs == 400u);
    const LockPreviewInput endingInputs[] = { kPreviewInputMouseButton, kPreviewInputKey, kPreviewInputSysKey };
    for (size_t i = 0; i < sizeof(endingInputs) / sizeof(endingInputs[0]); ++i)
    {
        INFO("input = " << static_cast<int>(endingInputs[i]));
        CLockPreviewState st;
        st.Start(DefaultChoice(), 0u, kT0);
        //保护时间内（如双击「预览」按钮的第二下）不结束
        CHECK_FALSE(st.OnInput(endingInputs[i], kT0));
        CHECK_FALSE(st.OnInput(endingInputs[i], kT0 + kPreviewInputGraceMs - kOneMs));
        CHECK_FALSE(st.IsEnding());
        //保护时间过后结束
        CHECK(st.OnInput(endingInputs[i], kT0 + kPreviewInputGraceMs));
        CHECK(st.IsEnding());
        //已在结束中：再次输入返回 false
        CHECK_FALSE(st.OnInput(endingInputs[i], kT0 + kPreviewInputGraceMs * 2));
    }
    CLockPreviewState st;
    st.Start(DefaultChoice(), 0u, kT0);
    CHECK_FALSE(st.OnInput(kPreviewInputMouseMove, kT0 + kPreviewInputGraceMs * 10));
    CHECK_FALSE(st.OnInput(kPreviewInputChar, kT0 + kPreviewInputGraceMs * 10));
    CHECK_FALSE(st.IsEnding());

    //建窗与场景初始化用去 1 秒：RestartClock 之后保护时间从窗口就绪那一刻重新算起，「轮流播放」的计时同样
    const uint64_t kReadyDelayMs = 1000;
    CLockPreviewState late;
    late.Start(ChoiceFromToken("rotate"), 0u, kT0);
    late.RestartClock(kT0 + kReadyDelayMs);
    CHECK_FALSE(late.OnInput(kPreviewInputKey, kT0 + kReadyDelayMs + kPreviewInputGraceMs - kOneMs));
    CHECK_FALSE(late.Tick(kT0 + kPreviewRotateIntervalMs));
    CHECK(late.Tick(kT0 + kReadyDelayMs + kPreviewRotateIntervalMs));
    CHECK(late.OnInput(kPreviewInputKey, kT0 + kReadyDelayMs + kPreviewInputGraceMs));
    //已在结束中时 RestartClock 不清除结束标志
    late.RestartClock(kT0 + kReadyDelayMs * 10);
    CHECK(late.IsEnding());
}

TEST_CASE("PV4 失去激活、显示器变化、系统注销任何时候都立即结束预览；结束后不再换片", "[lockdll]")
{
    const LockPreviewInput inputs[] = { kPreviewInputDeactivate, kPreviewInputDisplayChange, kPreviewInputEndSession };
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
    {
        INFO("input = " << static_cast<int>(inputs[i]));
        CLockPreviewState st;
        st.Start(ChoiceFromToken("rotate"), 0u, kT0);
        //保护时间内同样立即结束
        CHECK(st.OnInput(inputs[i], kT0 + kOneMs));
        CHECK(st.IsEnding());
        //结束中的预览不再换片
        CHECK_FALSE(st.Tick(kT0 + kPreviewRotateIntervalMs * 3));
        CHECK(st.Current() == kAnimHonor);
    }
    //重新开始一次预览：结束标志清除
    CLockPreviewState st;
    st.Start(DefaultChoice(), 0u, kT0);
    st.OnInput(kPreviewInputDeactivate, kT0);
    CHECK(st.IsEnding());
    st.Start(DefaultChoice(), 0u, kT0 + kPreviewRotateIntervalMs);
    CHECK_FALSE(st.IsEnding());
}
