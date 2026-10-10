/**
 *  HotkeyCaptureBox.h
 *
 *  系统设置「快捷键」页里可录入的键帽（docs/截图快捷键与隐藏当前窗口方案.md 第 2 节）：
 *  平时显示当前组合（如 "Alt + Z"，停用时显示「未设置」）；被点击后由设置窗调用 BeginCapture
 *  进入录入状态，边框变为品牌橙色、提示「请按下快捷键」，按住修饰键时实时显示已按下的部分
 *  （如 "Ctrl + Alt + …"）。录入结果经通知交给设置窗，由设置窗校验、保存并重新注册：
 *    · 按下主键：发 DUIN_VALUECHANGED，extra 为合成的组合编码（见 ScreenshotHotkey.h），
 *      不在本控件里判断组合是否合规；
 *    · Backspace / Delete：发 DUIN_VALUECHANGED，extra 为 0（停用）；
 *    · Esc 或失去焦点：发 DUIN_HOTKEY_CAPTURE_CANCELLED，组合不变。
 *  显示的组合只由 SetHotkey 改变：设置窗校验通过后才调用它。
 *
 *  子类化 balloonui 的 DuiButton（复用点击与 DUIN_CLICK 通知），只覆写绘制与键盘处理，
 *  不改 balloonui。带 Alt 的按键是系统按键（WM_SYSKEYDOWN），经宿主的 OnRawMessage 转来。
 *
 *  典型用法：
 *      std::unique_ptr<CHotkeyCaptureBox> box(new CHotkeyCaptureBox());
 *      box->SetCtrlId(kIdShotHotkeyBox);
 *      box->SetHotkey(cfg.GetScreenshotHotkey());
 *      // 收到它的 DUIN_CLICK 时：暂停本程序的全局快捷键，再调用 box->BeginCapture()
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // DUIN_CUSTOM

namespace balloonsettings {

/**
 *  CHotkeyCaptureBox —— 可录入快捷键的键帽按钮。
 */
class CHotkeyCaptureBox : public balloonwjui::DuiButton
{
public:
    // 本控件特有的通知码。另开 DUIN_CUSTOM + 120 起的取值段，避开 balloonui 各控件已用的
    // +1~+11、+40、+60、+80 段（UI 约定第 4 条）；派发时仍须同时判断 ctrlId。
    enum
    {
        DUIN_HOTKEY_CAPTURE_CANCELLED = balloonwjui::DUIN_CUSTOM + 120,   // 录入被取消（Esc 或失去焦点），组合不变
    };

    // 键帽的推荐尺寸（像素）：宽度放得下 "Ctrl + Alt + Shift + F12" 与英文提示文字
    static const int kPreferredWidth  = 188;
    static const int kPreferredHeight = 26;

    CHotkeyCaptureBox();

    /**
     *  设置显示的组合。录入中调用时不打断录入，录入结束后显示新值。
     *  @param hotkey 组合编码（见 Feature/Screenshot/ScreenshotHotkey.h）；0 表示停用，显示「未设置」
     */
    void SetHotkey(DWORD hotkey);

    // 当前显示的组合编码
    DWORD GetHotkey() const { return m_hotkey; }

    /**
     *  进入录入状态：取得键盘焦点，等待用户按下新的组合。已在录入中时什么也不做。
     *  副作用：重画为录入外观
     */
    void BeginCapture();

    // 是否正在录入
    bool IsCapturing() const { return m_capturing; }

    /**
     *  结束录入但不发任何通知（例如录入中点了「恢复默认」，后续由设置窗自己处理）。
     *  未在录入时什么也不做。
     */
    void CancelCapture();

    // ---- DuiControl / DuiButton 覆写 ----
    // 画键帽：浅灰圆角底 + 细描边；录入中为白底、品牌橙描边
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    // 普通按键（不带 Alt）：录入中合成组合并通知设置窗
    bool OnKeyDown(UINT vk, UINT flags) override;
    // 录入中吞掉字符消息，避免触发其它控件的输入
    bool OnChar(TCHAR ch) override;
    // 带 Alt 的系统按键与按键抬起：录入中合成组合、刷新已按下的修饰键，并吞掉 WM_SYSCHAR 以免系统提示音
    bool OnRawMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& lResult) override;
    // 失去焦点：录入中则取消录入
    bool OnKillFocus() override;
    // 需要宿主窗口持有 Win32 键盘焦点，否则收不到按键
    bool NeedsWin32Focus() const override { return true; }

private:
    // 处理录入中的一次按键（WM_KEYDOWN 或 WM_SYSKEYDOWN 都走这里）。返回 true 表示已处理
    bool HandleCaptureKey(UINT vk);
    // 结束录入状态（不发通知），并重画
    void EndCapture();
    // 当前要显示的文字：录入中为提示或已按下的修饰键；平时为组合或「未设置」
    CString DisplayText() const;
    // 读取三个修饰键当前是否按着
    static void ReadModifierState(bool& ctrl, bool& alt, bool& shift);

    DWORD m_hotkey;      // 显示的组合编码；0 表示停用
    bool  m_capturing;   // 是否处于录入状态
};

} // namespace balloonsettings
