/**
 *  窗口最小尺寸随 DPI 等比换算，DpiScaledMinSize.h
 *
 *  背景（2026-10-17，远程桌面联调时发现）：DuiFrameWindow::SetMinSize 设的是物理像素的下限，不随 DPI 变化；DuiHost 收到
 *  WM_DPICHANGED 时直接按系统给的建议矩形摆窗口。显示器缩放由 125% 变成 100%（例如临时把分辨率由 1920×1080 改成 1600×900，
 *  实测 DPI 由 120 变成 96）时，主面板 338 宽的建议宽度约 270，被 300 的下限抬到 300；缩放变回 125% 时又按 1.25 倍放大成 375，
 *  主面板从此比原来宽了约 11%。
 *  本类记下最小尺寸与它所对应的 DPI，窗口收到 WM_DPICHANGED 时（在基类套用建议矩形之前）按新 DPI 等比换算并重设最小尺寸，
 *  缩放来回变一次之后窗口回到原来的大小（系统换算建议矩形时的取整可能差 1 像素）。
 *
 *  用法：
 *      建窗之后：m_minSize.Init(*this, kMinWidth, kMinHeight, GetDpi());   //代替 SetMinSize(kMinWidth, kMinHeight)
 *      消息映射里，排在 CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow) 之前：
 *          MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChangedMsg)
 *      处理函数：m_minSize.OnDpiChanged(*this, LOWORD(wParam)); bHandled = FALSE;
 *  单元测试见 Source/Tests/DpiScaledMinSizeTests.cpp（[dpiminsize]）。
 *
 *  balloonwj@qq.com   2026-10-17
 */
#pragma once

namespace balloonwjui {
class DuiFrameWindow;
}

namespace balloonmain {

//按 DPI 等比换算并维护一个窗口的最小尺寸；只在窗口所在的 UI 线程上使用
class CDpiScaledMinSize
{
public:
    CDpiScaledMinSize();

    /**
     *  记下最小尺寸与它对应的 DPI，并立即设给窗口（代替直接调 SetMinSize）。
     *  @param wnd    目标窗口（调用方拥有，本类只在调用期间使用）
     *  @param minW   最小宽度（整窗，物理像素，按 dpi 定的值）
     *  @param minH   最小高度（整窗，物理像素，按 dpi 定的值）
     *  @param dpi    minW / minH 对应的 DPI；不大于 0 时按 96
     */
    void Init(balloonwjui::DuiFrameWindow& wnd, int minW, int minH, int dpi);

    /**
     *  窗口收到 WM_DPICHANGED 时调用（须在基类按建议矩形摆窗口之前）：按新 DPI 换算最小尺寸并设给窗口。
     *  @param wnd    目标窗口
     *  @param newDpi 新的 DPI（WM_DPICHANGED 的 LOWORD(wParam)）；不大于 0 时忽略
     */
    void OnDpiChanged(balloonwjui::DuiFrameWindow& wnd, int newDpi);

    /**
     *  按指定 DPI 换算后的最小宽度。
     *  @param dpi 目标 DPI；不大于 0 时返回 Init 时的原值
     *  @return 物理像素，四舍五入
     */
    int ScaledWidth(int dpi) const;

    /**
     *  按指定 DPI 换算后的最小高度。
     *  @param dpi 目标 DPI；不大于 0 时返回 Init 时的原值
     *  @return 物理像素，四舍五入
     */
    int ScaledHeight(int dpi) const;

private:
    int m_baseW;     //Init 时的最小宽度（物理像素）
    int m_baseH;     //Init 时的最小高度（物理像素）
    int m_baseDpi;   //m_baseW / m_baseH 对应的 DPI
};

} // namespace balloonmain
