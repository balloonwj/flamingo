/**
 *  ImageViewerToolbar.h
 *
 *  看图窗底部的悬浮工具栏 —— 自绘的 balloonui DuiControl 子类，对照设计稿
 *  dd/图片放大.png 底部那条半透明深色圆角条。一体化绘制：圆角半透明背景 + 一排
 *  按钮（缩小 / 缩放比例读数 / 放大 / 1:1 原始尺寸 / 适应窗口 / 旋转 / 另存为），
 *  按钮命中在本控件内部完成，点击后调用所属 ImageCanvas 的对应看图动作。
 *
 *  作为 ImageCanvas 的子控件浮在画布底部中央（不占画布布局宽度）；balloonui 的
 *  HitTest 会把落在本条上的鼠标分派给本控件，落在画布其它处的分派给画布，互不打架。
 *  缩放比例读数实时取自 ImageCanvas，画布每次缩放变化重绘即随之刷新。
 *
 *  本轮（增量 3）按钮范围：缩小 / 比例 / 放大 / 1:1 / 适应 / 旋转 / 另存为；编辑标注、
 *  转发、更多菜单留待后续。2026-09-30 在「另存为」左边补了「复制图片」：复制后可以直接
 *  粘进聊天输入框发送（走与聊天气泡右键「复制」相同的剪贴板格式）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonimageviewer {

class ImageCanvas;

// 工具栏按钮 / 元素种类（也决定其宽度与是否可点击）。
enum ToolbarItemKind
{
    kTbZoomOut = 0,   // 缩小（放大镜带减号）
    kTbZoomPct,       // 当前缩放比例读数（不可点击的文字标签）
    kTbZoomIn,        // 放大（放大镜带加号）
    kTbSeparator,     // 分隔竖线（不可点击）
    kTbActual,        // 1:1 原始尺寸（文字按钮）
    kTbFit,           // 适应窗口（四角框）
    kTbRotateLeft,    // 向左旋转 90°（逆时针圆形箭头）
    kTbRotate,        // 向右旋转 90°（顺时针圆形箭头）
    kTbSaveAs,        // 另存为（下载箭头）
    kTbCopyImage,     // 复制图片（两个叠放的圆角矩形）：复制后可粘进聊天输入框直接发送
};

// 看图工具栏控件。
class ImageViewerToolbar : public balloonwjui::DuiControl
{
public:
    // 构造。
    //   pCanvas：所属画布（本控件不持有，仅回调其看图动作）；不可为空。
    explicit ImageViewerToolbar(ImageCanvas* pCanvas);
    ~ImageViewerToolbar() override;

    // 工具栏整体固定尺寸（逻辑像素），供画布把它摆到底部中央。
    SIZE GetBarSize() const;

    // ---- 按钮编排查询（纯数据，不依赖实例；供单元测试核对按钮顺序与属性）----

    // 工具栏元素个数（含比例读数与分隔线）。
    static int ItemCount();

    // 从左往右第 index 个元素的种类。
    //   index：0 ~ ItemCount()-1；越界返回 kTbSeparator（不可点击的占位）。
    static ToolbarItemKind ItemKindAt(int index);

    // 某种元素是否可点击：图标 / 文字按钮可点，比例读数与分隔线不可点。
    static bool IsClickableKind(ToolbarItemKind kind);

    // 某种元素的悬停提示文字；没有提示的元素（比例读数、分隔线）返回空串。
    //   返回：静态字符串，调用方不得释放。
    static LPCTSTR TooltipForKind(ToolbarItemKind kind);

    // ---- DuiControl 覆写 ----
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    bool OnMouseMove(POINT pt, UINT mkFlags) override;
    bool OnMouseLeave() override;
    bool OnSetCursor(POINT pt) override;

private:
    // 命中测试：宿主坐标点落在第几个元素上；返回元素下标，未命中返回 -1。
    int HitItemIndex(POINT ptHost) const;
    // 第 index 个元素是否可点击（图标 / 文字按钮可点，比例读数与分隔线不可点）。
    bool IsClickable(int index) const;
    // 执行第 index 个元素对应的看图动作。
    void TriggerItem(int index);
    // 第 index 个按钮的 tooltip 文字；无对应提示返回空串。
    LPCTSTR TooltipFor(int index) const;
    // 按当前悬停按钮驱动 balloonui DuiToolTipMgr：先离开旧的、再按新按钮重挂
    //（本控件是单一控件、内部多按钮，故须手动逐按钮切换 tooltip 文字）。
    //   ptHost：当前鼠标点（宿主客户区坐标），用于定位 tooltip 弹出位置。
    void UpdateTooltip(POINT ptHost);

private:
    ImageCanvas* m_pCanvas;   // 所属画布（引用，不持有）
    int          m_hoverIndex; // 当前鼠标悬停的元素下标；-1 表示无悬停（用于按钮高亮）
};

} // namespace balloonimageviewer
