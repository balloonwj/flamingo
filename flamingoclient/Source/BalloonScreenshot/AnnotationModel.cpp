/**
 *  AnnotationModel.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-06-04
 */
#include "stdafx.h"
#include "AnnotationModel.h"

#include <math.h>

namespace balloonscreenshot {

namespace {

// 默认标注颜色（设计稿的红 #FF4D4F）。
const COLORREF kDefaultColor = RGB(0xFF, 0x4D, 0x4F);

// 默认线宽（中）。
const int kDefaultWidth = 4;

// 箭头两翼相对"轴线指回起点方向"的旋转夹角（弧度，约 28.6°）。
const double kArrowHeadAngle = 0.5;

} // anonymous namespace

Annotation::Annotation()
    : tool(kAnnNone)
    , color(kDefaultColor)
    , lineWidth(kDefaultWidth)
{
    start.x = 0;
    start.y = 0;
    end.x = 0;
    end.y = 0;
}

AnnotationStore::AnnotationStore()
{
}

void AnnotationStore::Add(const Annotation& a)
{
    m_items.push_back(a);
    m_redo.clear();   // 新分支：旧的重做记录作废
}

bool AnnotationStore::CanUndo() const
{
    return !m_items.empty();
}

bool AnnotationStore::CanRedo() const
{
    return !m_redo.empty();
}

void AnnotationStore::Undo()
{
    if (m_items.empty())
    {
        return;
    }
    m_redo.push_back(m_items.back());
    m_items.pop_back();
}

void AnnotationStore::Redo()
{
    if (m_redo.empty())
    {
        return;
    }
    m_items.push_back(m_redo.back());
    m_redo.pop_back();
}

void AnnotationStore::Clear()
{
    m_items.clear();
    m_redo.clear();
}

namespace {

// 把一个标注的所有坐标平移 (dx,dy)。
void TranslateOne(Annotation& a, int dx, int dy)
{
    a.start.x += dx;
    a.start.y += dy;
    a.end.x += dx;
    a.end.y += dy;
    for (size_t i = 0; i < a.points.size(); ++i)
    {
        a.points[i].x += dx;
        a.points[i].y += dy;
    }
}

} // anonymous namespace

void AnnotationStore::Translate(int dx, int dy)
{
    if (dx == 0 && dy == 0)
    {
        return;
    }
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        TranslateOne(m_items[i], dx, dy);
    }
    for (size_t i = 0; i < m_redo.size(); ++i)
    {
        TranslateOne(m_redo[i], dx, dy);
    }
}

AnnotationToolState::AnnotationToolState()
    : m_tool(kAnnNone)
    , m_color(kDefaultColor)
    , m_width(kDefaultWidth)
{
}

void AnnotationToolState::SetLineWidth(int w)
{
    m_width = (w > 0) ? w : 1;
}

void ComputeArrowHead(POINT start, POINT end, int headLen, POINT& wing1, POINT& wing2)
{
    // 自箭尖（end）指回起点（start）的方向单位向量。
    double dx = (double)(start.x - end.x);
    double dy = (double)(start.y - end.y);
    double len = ::sqrt(dx * dx + dy * dy);
    if (len < 1e-6)
    {
        // 退化：起止同点，两翼都取箭尖。
        wing1 = end;
        wing2 = end;
        return;
    }
    double ux = dx / len;
    double uy = dy / len;

    double ca = ::cos(kArrowHeadAngle);
    double sa = ::sin(kArrowHeadAngle);

    // 把 (ux,uy) 旋转 +角 与 -角，各乘 headLen，叠加到箭尖。
    double r1x = ux * ca - uy * sa;
    double r1y = ux * sa + uy * ca;
    double r2x = ux * ca + uy * sa;
    double r2y = -ux * sa + uy * ca;

    wing1.x = end.x + (LONG)(r1x * headLen);
    wing1.y = end.y + (LONG)(r1y * headLen);
    wing2.x = end.x + (LONG)(r2x * headLen);
    wing2.y = end.y + (LONG)(r2y * headLen);
}

namespace {

// ---- 马赛克 / 文字 映射常量 ----
const int kMosaicBlockPerWidth = 3;   // 马赛克块大小 = 线宽 × 此系数
const int kMosaicMinBlock      = 6;   // 马赛克最小块边长（像素，太小看不出打码）
const int kTextFontBaseSize    = 16;  // 线宽=2 档对应的文字字号（像素高）
const int kTextFontStepPerWidth= 3;   // 线宽每多 1 像素，字号 +此值（2→4→6 档即 +6 一档）
const int kTextFontBaseWidth   = 2;   // 基准线宽档（对应 kTextFontBaseSize）
const int kTextFontMinSize     = 16;  // 文字最小字号（像素高）

} // anonymous namespace

int MosaicBlockForWidth(int lineWidth)
{
    int block = lineWidth * kMosaicBlockPerWidth;
    if (block < kMosaicMinBlock)
    {
        block = kMosaicMinBlock;
    }
    return block;
}

int MosaicDownscaleExtent(int lengthPx, int block)
{
    if (block <= 0)
    {
        block = 1;   // 防除零；正常调用方传 >0 的块大小
    }
    int n = lengthPx / block;
    if (n < 1)
    {
        n = 1;       // 不足一块也至少 1 个，避免 0 尺寸位图
    }
    return n;
}

int TextFontSizeForWidth(int lineWidth)
{
    int size = kTextFontBaseSize + (lineWidth - kTextFontBaseWidth) * kTextFontStepPerWidth;
    if (size < kTextFontMinSize)
    {
        size = kTextFontMinSize;
    }
    return size;
}

} // namespace balloonscreenshot
