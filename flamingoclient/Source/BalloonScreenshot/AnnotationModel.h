/**
 *  AnnotationModel.h
 *
 *  截图标注的纯数据模型：标注对象、撤销/重做栈、当前工具状态、以及箭头头部几何。
 *  刻意与窗口 / GDI 解耦——只做数据与坐标运算，便于 FlamingoUnit 直接断言（对应
 *  第二期 a 用例 S / T / A）。实际绘制、鼠标交互、把标注烤进输出图等在画布里做。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（用到 CString）。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#pragma once

#include <windows.h>   // POINT / RECT / COLORREF
#include <vector>

namespace balloonscreenshot {

// 标注工具类型。
enum AnnotationTool
{
    kAnnNone    = 0,   // 未选标注工具：画布拖拽 = 重新框选（非画标注）
    kAnnRect    = 1,   // 矩形框
    kAnnEllipse = 2,   // 椭圆框
    kAnnArrow   = 3,   // 箭头
    kAnnPen     = 4,   // 画笔（自由曲线）
    kAnnMosaic  = 5,   // 马赛克（矩形区域：拖一个框，框内桌面像素打码）
    kAnnText    = 6,   // 文字（第二期 b）
    kAnnMosaicBrush = 7,   // 马赛克（画笔涂抹：拖动经过的地方打码，用 points 记路径）
};

// 一个标注对象。不同 tool 用到的字段不同（见各字段注释）。
struct Annotation
{
    AnnotationTool     tool;       // 标注类型
    COLORREF           color;      // 描边 / 文字颜色
    int                lineWidth;  // 线宽（像素）；马赛克复用为块大小
    POINT              start;      // 矩形/椭圆/箭头起点；马赛克矩形左上
    POINT              end;        // 矩形/椭圆/箭头终点；马赛克矩形右下
    std::vector<POINT> points;     // 画笔：自由路径采样点（屏幕坐标）
    CString            text;       // 文字内容

    // 默认值：无类型、红色、线宽 4、坐标清零、无路径 / 文字。
    Annotation();
};

// 标注存储 + 撤销 / 重做。撤销/重做是标准语义：Undo 把最后一个标注移到重做栈，
// Redo 反向移回；一旦 Add 新标注，重做栈被清空（不能再 Redo 旧的分支）。
class AnnotationStore
{
public:
    AnnotationStore();

    // 追加一个标注。会清空重做栈。
    void Add(const Annotation& a);

    // 是否可撤销 / 重做。
    bool CanUndo() const;
    bool CanRedo() const;

    // 撤销：把最后一个标注移到重做栈。无可撤销时为 no-op。
    void Undo();

    // 重做：把重做栈顶移回标注列表。无可重做时为 no-op。
    void Redo();

    // 全部清空（含重做栈）。
    void Clear();

    // 把所有标注（含重做栈）按 (dx,dy) 平移 —— 用于"移动选区时标注跟着一起移"。
    void Translate(int dx, int dy);

    // 当前可见标注（按绘制先后顺序）。
    const std::vector<Annotation>& Items() const { return m_items; }

    // 当前可见标注个数。
    size_t Count() const { return m_items.size(); }

private:
    std::vector<Annotation> m_items;   // 当前可见标注（栈底→栈顶 = 先画→后画）
    std::vector<Annotation> m_redo;    // 被撤销的标注（栈顶 = 最近被撤销的）
};

// 当前标注工具状态：选中的工具 / 颜色 / 线宽。供工具栏与画布共享。
class AnnotationToolState
{
public:
    // 默认：未选工具（kAnnNone）、红色、线宽 4（中）。
    AnnotationToolState();

    void           SetTool(AnnotationTool t) { m_tool = t; }
    AnnotationTool Tool() const              { return m_tool; }

    void           SetColor(COLORREF c) { m_color = c; }
    COLORREF       Color() const        { return m_color; }

    // 线宽（像素，>0）。<=0 时夹到 1。
    void           SetLineWidth(int w);
    int            LineWidth() const    { return m_width; }

private:
    AnnotationTool m_tool;    // 当前工具
    COLORREF       m_color;   // 当前颜色
    int            m_width;   // 当前线宽（像素）
};

// 计算箭头头部的两个翼点（纯几何）。方向由 end 指向（箭尖在 end），两翼自 end 沿
// "指回 start" 的方向各旋转一个固定夹角、长 headLen。start==end（退化）时两翼都取 end。
//   start / end：箭头起点 / 终点（箭尖在 end）；
//   headLen：    箭头翼长（像素，>0）；
//   wing1 / wing2：出参，两个翼点。
void ComputeArrowHead(POINT start, POINT end, int headLen, POINT& wing1, POINT& wing2);

// ---- 第二期 b：马赛克 / 文字 的纯映射（与 GDI 解耦，便于单测，对应用例 MOS / TXT）----

// 由当前线宽档位换算马赛克块大小（像素）。线宽越粗、块越大、打码越狠。
// 取 lineWidth*3，并夹到一个最小块（太小看不出马赛克）。
//   lineWidth：当前线宽（像素，三档 2/4/6）；
//   返回：    马赛克块边长（像素，>=最小块）。
int MosaicBlockForWidth(int lineWidth);

// 计算某一维上"先缩小再放大"打码时的降采样块数。= max(1, lengthPx / block)：
// 即把 lengthPx 长的区域按 block 分块，不足一块也至少留 1（避免 0 尺寸位图）。
//   lengthPx：区域某一维长度（像素，>=0）；
//   block：   马赛克块边长（像素，>0；<=0 时按 1 处理避免除零）；
//   返回：    该维降采样后的像素数（>=1）。
int MosaicDownscaleExtent(int lengthPx, int block);

// 由当前线宽档位换算文字标注字号（像素高）。线宽越粗、字越大。
// 三档 2/4/6 → 16/22/28，并夹到一个最小字号。
//   lineWidth：当前线宽（像素，三档 2/4/6）；
//   返回：    文字字号（像素高，>=最小字号）。
int TextFontSizeForWidth(int lineWidth);

} // namespace balloonscreenshot
