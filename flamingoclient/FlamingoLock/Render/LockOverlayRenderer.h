/**
 *  锁屏界面叠加层的绘制：左下角的时间与日期、按钮栏（解锁 / 重启 / 关机）、密码框、关机与重启的确认框；
 *  设置窗口「预览」时还在画面上方画一行退出提示（OverlayFrame::m_hint，2026-10-07）。
 *  画在动画之上，样式按样稿 tmp/lockscreen/lock_preview.html（半透明深色底、白色细边框、品牌橙主按钮）。
 *
 *  每块显示器的锁屏窗口各有一个本类对象；按钮栏只在 ActiveMonitor 那块显示器上画，出现与消失时淡入淡出。
 *
 *  典型用法（锁屏线程）：
 *      r.Init(dwrite, scale, w, h);
 *      HitTestOverlay(r.Layout(), phase, pt);           // 命中测试用同一份布局
 *      每帧：r.Draw(rt, frame);
 *      渲染目标重建时：r.DiscardDeviceResources();
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>

#include "LockD2D.h"
#include "../Core/LockOverlayLayout.h"

namespace flamingolock {

/**
 *  画一帧叠加层所需的状态，由锁屏会话每帧填好。
 */
struct OverlayFrame
{
    LockUiPhase  m_phase;          // 当前阶段
    bool         m_barHere;        // 按钮栏是否在本显示器上
    int          m_maskChars;      // 密码框里已输入的字符数
    std::wstring m_message;        // 密码框下方的提示（空串表示不显示）
    float        m_dt;             // 距上一帧的秒数，用于淡入淡出
    uint64_t     m_nowMs;          // 单调时刻（毫秒），用于光标闪烁与抖动
    POINT        m_mouse;          // 鼠标在本窗口客户区的位置；不在本窗口时为 (-1, -1)
    bool         m_showClock;      // 是否画左下角的时间
    uint64_t     m_wallMs;         // 当前时刻（自 1970-01-01 起的毫秒），用于左下角时间
    std::wstring m_hint;           // 画面上方居中的一行提示（设置窗口「预览」时为「按 Esc 或单击退出预览」）；
                                   // 空串表示不画。锁屏会话不填，保持为空
};

/**
 *  叠加层绘制器。非线程安全，只在锁屏线程上使用。
 */
class CLockOverlayRenderer
{
public:
    CLockOverlayRenderer();

    /**
     *  按画面尺寸创建文字格式、量出按钮文字宽度并计算布局。尺寸变化后再调用一次。
     *  @param dwrite DirectWrite 工厂（借用，须在本对象销毁前一直有效）
     *  @param scale  缩放比例（DPI / 96）
     *  @param width  画面宽（物理像素）
     *  @param height 画面高（物理像素）
     */
    void Init(IDWriteFactory* dwrite, float scale, int width, int height);

    /**
     *  当前布局，供命中测试。
     */
    const LockOverlayLayout& Layout() const;

    /**
     *  密码输错时调用，让密码框左右抖动一次。
     *  @param nowMs 单调时刻（毫秒）
     */
    void StartShake(uint64_t nowMs);

    /**
     *  渲染目标即将重建：释放画刷。
     */
    void DiscardDeviceResources();

    /**
     *  画一帧叠加层（调用方已 BeginDraw）。
     *  @param rt    渲染目标
     *  @param frame 本帧状态
     */
    void Draw(ID2D1RenderTarget* rt, const OverlayFrame& frame);

private:
    // 画左下角的时间与日期（带一层深色投影，保证在浅色画面上也看得清）
    void DrawClock(ID2D1RenderTarget* rt, uint64_t wallMs);
    // 画按钮栏与密码框；alpha 为淡入淡出的不透明度
    void DrawBar(ID2D1RenderTarget* rt, const OverlayFrame& frame, LockUiPhase phase, float alpha);
    // 画确认框（连同压暗整屏的半透明遮罩）
    void DrawConfirm(ID2D1RenderTarget* rt, const OverlayFrame& frame, LockUiPhase phase, float alpha);
    // 画画面上方的一行提示（半透明深色胶囊 + 白字），预览时使用；文字宽度按文字缓存，文字不变时不重新测量
    void DrawHint(ID2D1RenderTarget* rt, const std::wstring& hint);
    // 画一个圆角按钮：底色、边框、图标与文字；icon 取 kIcon*，为 -1 时不画图标
    void DrawButton(ID2D1RenderTarget* rt, const RECT& rc, const std::wstring& text, int icon,
                    uint32_t fill, float fillAlpha, uint32_t border, float borderAlpha, float alpha);
    // 在 (x, y) 起、边长 size 的方框里描一个线条图标
    void DrawIcon(ID2D1RenderTarget* rt, int icon, float x, float y, float size, float alpha);
    // 用同一个画刷设色（画刷不存在时先创建）
    ID2D1SolidColorBrush* Brush(ID2D1RenderTarget* rt, uint32_t rgb, float alpha);

private:
    IDWriteFactory*                 m_dwrite;          // DirectWrite 工厂（借用）
    float                           m_scale;           // 缩放比例
    int                             m_width;           // 画面宽
    int                             m_height;          // 画面高
    LockOverlayLayout               m_layout;          // 当前布局
    int                             m_contentWidths[kOverlayButtonCount];   // 三个按钮的内容宽度（图标 + 间距 + 文字）
    CComPtr<IDWriteTextFormat>      m_buttonFormat;    // 按钮文字
    CComPtr<IDWriteTextFormat>      m_inputFormat;     // 密码框的圆点与占位文字
    CComPtr<IDWriteTextFormat>      m_messageFormat;   // 密码框下方的提示
    CComPtr<IDWriteTextFormat>      m_titleFormat;     // 确认框标题
    CComPtr<IDWriteTextFormat>      m_textFormat;      // 确认框说明
    CComPtr<IDWriteTextFormat>      m_clockFormat;     // 左下角时间
    CComPtr<IDWriteTextFormat>      m_dateFormat;      // 左下角日期
    CComPtr<IDWriteTextFormat>      m_hintFormat;      // 画面上方的提示（预览时）
    std::wstring                    m_hintText;        // 上次测量过宽度的提示文字；与本帧文字不同时重新测量
    RECT                            m_hintRect;        // m_hintText 对应的胶囊矩形
    CComPtr<ID2D1SolidColorBrush>   m_brush;           // 复用的纯色画刷（每次绘制前 SetColor）
    float                           m_barAlpha;        // 按钮栏当前的不透明度（淡入淡出中）
    LockUiPhase                     m_lastVisiblePhase;// 按钮栏最后一次可见时的阶段，淡出时仍按它画
    uint64_t                        m_shakeStartMs;    // 抖动开始的时刻；0 表示不在抖动
};

} // namespace flamingolock
