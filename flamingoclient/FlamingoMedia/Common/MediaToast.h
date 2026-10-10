/**
 *  FlamingoMedia 通用的轻量提示（toast），MediaToast.h
 *
 *  屏幕共享、录制等功能里「做完了一件事、告诉用户一声」的提示（2026-10-07 起替换分享链接复制 / 重置 / 停用的 MessageBox，
 *  以及共享与录制互斥被拦下时的提示，docs/屏幕共享与录制方案.md 第 13 节）。形态：
 *    - 置顶、不抢焦点、不进任务栏的小窗，排除在屏幕采集之外（不会被共享或录进去）；
 *    - 一行或两行文字（超过最大宽度时折行），圆角深色底；失败提示用深红底；
 *    - 淡入（200 毫秒）→ 停留约 2.5 秒 → 淡出（300 毫秒）后自动隐藏；鼠标不拦截（点击穿透到下面的程序）；
 *    - 摆放：默认贴在参照矩形（如共享控制条，收起成小把手时就是小把手）正下方居中；下方放不下时放进参照矩形底部；
 *      没有参照矩形时放在屏幕中央偏下。
 *  控件用 balloonui：窗口由 DuiHost 接管，文字是 DuiLabel（可折行）；淡入淡出用整窗透明度（分层窗口的 LWA_ALPHA，
 *  这种分层窗口能排除采集），圆角用窗口区域。
 *  摆放与淡入淡出的计算是纯函数，单元测试见 Source/Tests/MediaToastTests.cpp（标签 [mediatoast]）。
 *
 *  典型用法：
 *      m_toast.Show(barRect, flamingomedia::media_toast_below, TR(L"分享链接已复制"), flamingomedia::media_toast_info);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include "../../../third-party/balloonui/balloonui/DuiHost.h"

namespace balloonwjui
{
    class DuiLabel;
    class DuiVBox;
}

namespace flamingomedia {

//提示的样式
enum MediaToastStyle
{
    media_toast_info  = 0,   //普通提示：深灰底、白字（复制成功、已重置等）
    media_toast_error = 1,   //失败提示：深红底、白字，比普通提示醒目
};

//提示摆在哪里
enum MediaToastPlacement
{
    media_toast_below        = 0,   //参照矩形正下方居中（下方放不下时放进参照矩形底部）；用于控制条
    media_toast_inside_bottom = 1,  //参照矩形里面、靠底边居中；用于发起入口所在的窗口（聊天窗等）
    media_toast_screen       = 2,   //参照矩形所在显示器（参照矩形为空时为主显示器）的中央偏下
};

//淡入、停留、淡出的时长（毫秒）
const int kMediaToastFadeInMs = 200;
const int kMediaToastHoldMs = 2500;
const int kMediaToastFadeOutMs = 300;

/**
 *  提示窗口的位置（纯函数）。结果总在工作区以内。
 *  @param anchor    参照矩形（屏幕坐标）；为空矩形时一律按 media_toast_screen 处理
 *  @param placement 摆放方式，取值见 MediaToastPlacement
 *  @param size      提示窗口的宽高（像素）
 *  @param work      参照矩形所在显示器的工作区（屏幕坐标）
 *  @return 提示窗口的矩形（屏幕坐标）
 */
RECT ComputeMediaToastRect(const RECT& anchor, int placement, SIZE size, const RECT& work);

/**
 *  显示开始后某一时刻的整窗不透明度（纯函数）：淡入时由 0 升到 255，停留时为 255，淡出时降到 0。
 *  @param elapsedMs 距显示开始的毫秒数
 *  @param finished  [出参] 淡出已结束（应隐藏窗口）时为 true
 *  @return 不透明度 0~255
 */
int MediaToastAlphaAt(int64_t elapsedMs, bool& finished);

//轻量提示窗口（UI 线程上使用）
class CMediaToast : public balloonwjui::DuiHost
{
public:
    CMediaToast();
    ~CMediaToast();

    /**
     *  显示一条提示：已在显示时换成新文字、重新计时（不叠加多个窗口）。
     *  @param anchor    参照矩形（屏幕坐标）；空矩形表示没有参照，放在主显示器中央偏下
     *  @param placement 摆放方式，取值见 MediaToastPlacement
     *  @param text      提示文字（已经过 TR 翻译）；为空时什么也不做
     *  @param style     样式，取值见 MediaToastStyle
     */
    void Show(const RECT& anchor, int placement, const std::wstring& text, int style);

    /**
     *  以某个窗口为参照显示（发起入口所在的窗口）：窗口有效且可见时放在它里面靠底边，否则放在屏幕中央偏下。
     *  @param wnd   参照窗口；可为 NULL
     *  @param text  提示文字
     *  @param style 样式
     */
    void ShowNearWindow(HWND wnd, const std::wstring& text, int style);

    /**
     *  以本进程当前的前台窗口为参照显示（发起入口所在的窗口：聊天窗、通话窗、主面板等）：前台窗口不属于本进程时
     *  （如从托盘菜单发起）放在屏幕中央偏下。
     *  @param text  提示文字
     *  @param style 样式
     */
    void ShowNearActiveWindow(const std::wstring& text, int style);

    //立即隐藏（不销毁窗口）；可重复调用
    void Hide();

    //销毁窗口；可重复调用（模块关闭前调用：窗口过程在本 DLL 里）
    void Destroy();

    //是否正在显示（含淡入、淡出）
    bool IsShowing() const;

    //当前文字（单元测试用）
    const std::wstring& Text() const;

    //当前样式（单元测试用）
    int Style() const;

    BEGIN_MSG_MAP(CMediaToast)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTest)
        MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
        CHAIN_MSG_MAP(balloonwjui::DuiHost)
    END_MSG_MAP()

private:
    //淡入淡出的定时器：每 kTickMs 毫秒按 MediaToastAlphaAt 更新一次透明度
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //鼠标穿透：整窗返回 HTTRANSPARENT
    LRESULT OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //点击不激活
    LRESULT OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //建窗口与控件树（只建一次）
    bool ensureWindow();

    balloonwjui::DuiVBox*  m_root;       //根容器（控件树持有，本指针仅引用），底色随样式变化
    balloonwjui::DuiLabel* m_label;      //提示文字（同上）
    std::wstring           m_text;       //当前文字
    int                    m_style;      //当前样式，取值见 MediaToastStyle
    int64_t                m_shownAtMs;  //本次显示开始的时刻（GetTickCount64 毫秒）
    bool                   m_showing;    //是否正在显示
};

} // namespace flamingomedia
