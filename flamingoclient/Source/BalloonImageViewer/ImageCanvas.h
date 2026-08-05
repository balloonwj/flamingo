/**
 *  ImageCanvas.h
 *
 *  看图窗的图片画布 —— 自绘的 balloonui DuiControl 子类。在深色背景上等比
 *  绘制一张图片，支持缩放（滚轮 / 按钮 / 双击适应↔100%）、顺时针旋转 90°、
 *  按住拖动平移。所有缩放 / 旋转 / 平移的数值运算都委托给 ImageViewerGeometry
 *  里的纯函数（已有单测覆盖），本控件只负责把结果落到 GDI+ 绘制与鼠标交互。
 *
 *  控件本身不拥有 HWND（balloonui DUI 节点的不变量），所有坐标默认是宿主
 *  客户区坐标；内部把它换算成"画布局部坐标"（画布左上角为原点）再交给几何
 *  纯函数。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#pragma once

#include "../../balloonui/balloonui/DuiControl.h"

// 只用到 Gdiplus::Bitmap 的指针，前置声明以免在头文件里拉入沉重的 <gdiplus.h>。
namespace Gdiplus { class Bitmap; }

namespace balloonimageviewer {

class ImageViewerToolbar;

// 看图画布控件。
class ImageCanvas : public balloonwjui::DuiControl
{
public:
    ImageCanvas();
    ~ImageCanvas() override;

    // 从本地文件加载图片，并重置为"适应窗口"居中显示。解码失败则清空当前图
    //（画布只剩深色背景）。
    //   path：图片本地完整路径（UTF-16，非空）。
    //   返回：解码成功返回 true，失败返回 false。
    bool LoadImageFromFile(LPCTSTR path);

    // 当前是否已成功载入一张图片。
    bool HasImage() const;

    // ---- 看图动作（增量 3 由底部工具栏按钮调用）----

    // 放大 / 缩小一档（以画布中心为锚点）。
    void ZoomIn();
    void ZoomOut();
    // 切到"适应窗口"（等比放进画布、只缩不放），并居中。
    void ZoomToFit();
    // 切到 100%（原始像素 1:1），以画布中心为锚点。
    void ZoomToActual();
    // 顺时针 / 逆时针旋转 90°，旋转后按新朝向重新适应窗口。
    void RotateClockwise();
    void RotateCounterClockwise();

    // 另存为：弹保存对话框，把当前图片的源文件另存到所选路径（按原文件字节拷贝，
    // 无损、保持原格式）。无图或无源路径时静默返回。由底部工具栏「另存为」调用。
    void SaveImageAs();

    // 当前缩放比的百分数读数（形如 "96%"），供工具栏显示。
    CString GetZoomReadout() const;

    // ---- DuiControl 覆写：绘制与鼠标交互 ----
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    bool OnMouseMove(POINT pt, UINT mkFlags) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;
    void Layout(const RECT& rcAvail) override;

private:
    // 画布可视区尺寸（= 控件矩形尺寸）。
    SIZE ViewportSize() const;
    // 当前图片在当前旋转下、未缩放的"显示尺寸"（90/270 时宽高对调）。
    SIZE DisplaySize() const;
    // 当前缩放比缩放后的图片在屏矩形尺寸（= DisplaySize × m_scale，四舍五入）。
    SIZE ScaledBoxSize() const;
    // 把宿主客户区坐标点换算成画布局部坐标（画布左上角为原点）。
    POINT ToLocal(POINT ptHost) const;
    // 把 m_origin 经 ClampImageOrigin 夹取 / 居中（小图锁定居中）。
    void ClampOrigin();
    // 以锚点 anchorLocal（画布局部坐标）把缩放比设为 newScale、修正落点后刷新。
    void ApplyZoomAtAnchor(double newScale, POINT anchorLocal);
    // 图片是否在任一维大于视口（即可平移）。无图时为 false。
    bool IsPannable() const;

    // 把底部悬浮工具栏摆到画布底部中央（在 Layout 里调用）。
    void PositionToolbar();

    // 按给定增量旋转（deltaDegreesCW 为顺时针度数，可负），归一到 [0,360)，
    // 再按新朝向重新适应窗口并刷新。RotateClockwise/CounterClockwise 的共用实现。
    void ApplyRotation(int deltaDegreesCW);

private:
    Gdiplus::Bitmap* m_image;       // 解码后的图片位图（本类持有，析构 / 重载时释放）；NULL = 无图
    int      m_imgW;                // 原始（未旋转）图片像素宽
    int      m_imgH;                // 原始（未旋转）图片像素高
    int      m_rotation;            // 旋转角，取 0 / 90 / 180 / 270（顺时针累加并归一）
    double   m_scale;               // 当前缩放比（1.0 = 100%）
    POINT    m_origin;              // 缩放后图片在屏矩形左上角（画布局部坐标），平移即改它
    bool     m_fitOnLayout;         // 是否处于"适应窗口"态：为真时窗口尺寸变化会跟随重新适应
    bool     m_dragging;            // 是否正在按住左键拖动平移
    POINT    m_dragLastPt;          // 上一次拖动鼠标点（宿主客户区坐标），用于算平移增量
    CString  m_sourcePath;          // 当前图片的源文件路径（另存为用）；空表示无图
    ImageViewerToolbar* m_pToolbar; // 底部悬浮工具栏（子控件，所有权在控件树）
};

} // namespace balloonimageviewer
