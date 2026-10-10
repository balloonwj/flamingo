/**
 *  ScreenshotToolbar.h
 *
 *  截图浮层工具栏的深色风格自绘控件（对齐 design/截图效果.JPG）：深色圆角条容器、
 *  图标按钮（白色图形图标，可带文字 / 橙色主按钮态）、圆形色点、红色线宽样条，以及
 *  统一的图标绘制函数。这些控件都是 balloonui DuiControl 子类，点击经 NotifyParent
 *  发 DUIN_CLICK 给宿主（遮罩窗口按 ctrlId 分发）。
 *
 *  截图浮层本就是一整块自绘表面，工具栏样式无法用 balloonui 默认按钮表达，故此处自绘；
 *  视觉常量集中在 .cpp 顶部便于微调。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonscreenshot {

// 工具栏图标种类。
enum ToolIconKind
{
    kIconNone = 0,
    kIconCursor,     // 选择 / 移动（鼠标指针箭头）
    kIconRect,       // 矩形
    kIconEllipse,    // 椭圆
    kIconArrow,      // 箭头
    kIconPen,        // 画笔
    kIconMosaic,     // 马赛克（矩形：方框内棋盘格）
    kIconMosaicBrush,// 马赛克（画笔：斜向方块刷痕）
    kIconText,       // 文字（"A" 字形，第二期 b）
    kIconUndo,       // 撤销
    kIconRedo,       // 重做
    kIconReselect,   // 重选（环形箭头）
    kIconCopy,       // 复制（叠放矩形）
    kIconSave,       // 保存（下载箭头）
    kIconSend,       // 发送（右向三角）
    kIconStage,      // 发送到对话框（气泡）
    kIconCancel,     // 取消（X）
    kIconDone,       // 完成（对勾）：截图不绑定会话时操作栏的主按钮，把截图放进剪贴板
    kIconOcr,        // 识别文字（四角取景框里一个「T」）：图片文字识别模块可用时出现在操作栏（docs/图片文字识别方案.md 第 8.5 节）
};

// 把一个工具图标画进 rc（白色线条，线宽自适配）。color 为线条 / 填充色。
void DrawToolIcon(HDC hdc, const RECT& rc, ToolIconKind kind, COLORREF color);

// ============================================================
// ITipSink —— 工具栏按钮悬停提示的接收方接口。
//
// 设计稿要求 tooltip 显示在<u>对应按钮正下方</u>（而非跟随光标），且本提示只服务于
// 截图浮层、<u>不</u>动用全局的 balloonui DuiToolTipMgr。故按钮悬停时把"在某矩形下方
// 显示某文字"这件事回调给宿主画布（CaptureCanvas 实现本接口），由画布自绘提示框。
// ============================================================
class ITipSink
{
public:
    virtual ~ITipSink() {}

    // 鼠标进入某按钮时调用：请在 anchorRect 正下方显示提示文字。
    //   anchorRect：触发按钮的宿主客户区矩形（与画布同一坐标系）。
    //   text：      提示文字（非空才显示）。
    virtual void ShowTip(const RECT& anchorRect, LPCTSTR text) = 0;

    // 鼠标离开按钮时调用：请隐藏提示。
    virtual void HideTip() = 0;
};

// ============================================================
// DarkBar —— 深色圆角条容器。自身画深色圆角背景，再由基类画子控件。
// ============================================================
class DarkBar : public balloonwjui::DuiControl
{
public:
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
};

// ============================================================
// IconButton —— 深色条上的图标按钮，可附文字（操作栏用）、可设为橙色主按钮。
// 点击发 DUIN_CLICK；hover / active 有视觉反馈。
// ============================================================
class IconButton : public balloonwjui::DuiControl
{
public:
    IconButton();

    void SetIcon(ToolIconKind k)   { m_icon = k; }
    void SetLabel(LPCTSTR s)       { m_label = (s != NULL) ? s : _T(""); }
    void SetActive(bool a);
    bool IsActive() const          { return m_active; }
    void SetPrimary(bool p)        { m_primary = p; }

    // 设置悬停提示文字（空串=无提示）。
    void SetTip(LPCTSTR s)         { m_tip = (s != NULL) ? s : _T(""); }
    // 设置提示接收方（宿主画布）。所有权不转移，须在按钮存活期间有效。
    void SetTipSink(ITipSink* sink){ m_tipSink = sink; }

    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnMouseEnter() override;
    bool OnMouseLeave() override;
    bool OnSetCursor(POINT pt) override;

private:
    ToolIconKind m_icon;     // 图标种类（kIconNone=只有文字）
    CString      m_label;    // 可选文字（白字，画在图标右侧 / 单独居中）
    bool         m_active;   // 选中态（当前工具 / 当前线宽）
    bool         m_primary;  // 橙色实心主按钮（发送到对话框）
    CString      m_tip;      // 悬停提示文字（空=不提示）
    ITipSink*    m_tipSink;  // 提示接收方（宿主画布，借用不持有）
};

// ============================================================
// ColorDot —— 圆形色点。点击发 DUIN_CLICK；选中画高亮环。
// ============================================================
class ColorDot : public balloonwjui::DuiControl
{
public:
    ColorDot();
    void     SetColor(COLORREF c) { m_color = c; }
    COLORREF GetColor() const     { return m_color; }
    void     SetActive(bool a);

    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;

private:
    COLORREF m_color;
    bool     m_active;
};

// ============================================================
// WidthDot —— 红色线宽样条（画一段该粗细的横线）。点击发 DUIN_CLICK；选中加底高亮。
// ============================================================
class WidthDot : public balloonwjui::DuiControl
{
public:
    WidthDot();
    void SetWidth(int w)  { m_width = w; }
    int  GetWidth() const { return m_width; }
    void SetActive(bool a);

    // 设置悬停提示文字（空串=无提示）。
    void SetTip(LPCTSTR s)         { m_tip = (s != NULL) ? s : _T(""); }
    // 设置提示接收方（宿主画布）。所有权不转移，须在控件存活期间有效。
    void SetTipSink(ITipSink* sink){ m_tipSink = sink; }

    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnMouseEnter() override;
    bool OnMouseLeave() override;
    bool OnSetCursor(POINT pt) override;

private:
    int       m_width;    // 线宽样本（像素）
    bool      m_active;
    CString   m_tip;      // 悬停提示文字（空=不提示）
    ITipSink* m_tipSink;  // 提示接收方（宿主画布，借用不持有）
};

} // namespace balloonscreenshot
