/**
 *  白板的视图，见 ShareWhiteboard.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "ShareWhiteboard.h"

#include <math.h>
#include <stdlib.h>
#include <algorithm>

namespace screenshare {

namespace {

//缩放倍数比较用的容差：两次倍数之差小于它算没有变化
const double kZoomEpsilon = 1e-6;
//四舍五入加的半
const double kRoundHalf = 0.5;
//背景线的最小间距（像素）：缩得很小时也不至于密成一片
const int kMinLineSpacingPx = 4;

//把 v 限制在 [lo, hi]；hi 小于 lo 时取 lo
int clampInt(int v, int lo, int hi)
{
    if (hi < lo)
        return lo;
    return v < lo ? lo : (v > hi ? hi : v);
}

//某个间距的背景线：世界坐标在 (0, canvas) 之间、可见区域坐标在 [0, view) 之间的倍数位置
void linesAt(int spacing, int scroll, int canvas, int view, std::vector<int>& out)
{
    //世界里第一条不小于滚动位置、且大于 0 的线
    int world = ((scroll + spacing - 1) / spacing) * spacing;
    if (world <= 0)
        world = spacing;
    for (; world < canvas; world += spacing)
    {
        const int v = world - scroll;
        if (v >= view)
            break;
        out.push_back(v);
    }
}

} // namespace

ShareWhiteboardView::ShareWhiteboardView()
    : m_worldW(1)
    , m_worldH(1)
    , m_canvasW(1)
    , m_canvasH(1)
    , m_viewW(1)
    , m_viewH(1)
    , m_scrollX(0)
    , m_scrollY(0)
    , m_shared(false)
    , m_baseWorldW(1)
    , m_baseWorldH(1)
    , m_zoom(1.0)
{
}

void ShareWhiteboardView::Init(int viewW, int viewH)
{
    m_viewW = viewW > 0 ? viewW : 1;
    m_viewH = viewH > 0 ? viewH : 1;
    m_worldW = m_viewW * kWhiteboardWorldScale;
    m_worldH = m_viewH * kWhiteboardWorldScale;
    m_canvasW = m_viewW;
    m_canvasH = m_viewH;
    m_scrollX = 0;
    m_scrollY = 0;
    m_shared = false;
    m_baseWorldW = m_worldW;
    m_baseWorldH = m_worldH;
    m_zoom = 1.0;
}

void ShareWhiteboardView::InitShared(int viewW, int viewH)
{
    m_viewW = viewW > 0 ? viewW : 1;
    m_viewH = viewH > 0 ? viewH : 1;
    m_shared = true;
    m_zoom = 1.0;
    m_baseWorldW = m_viewW * kWhiteboardWorldScale;
    m_baseWorldH = (m_baseWorldW * kWhiteboardSharedAspectH + kWhiteboardSharedAspectW / 2) / kWhiteboardSharedAspectW;
    if (m_baseWorldH < 1)
        m_baseWorldH = 1;
    m_worldW = m_baseWorldW;
    m_worldH = m_baseWorldH;
    //画布就是整个世界
    m_canvasW = m_worldW;
    m_canvasH = m_worldH;
    m_scrollX = 0;
    m_scrollY = 0;
}

bool ShareWhiteboardView::IsShared() const
{
    return m_shared;
}

double ShareWhiteboardView::Zoom() const
{
    return m_zoom;
}

bool ShareWhiteboardView::SetZoom(double zoom, int anchorX, int anchorY)
{
    if (!m_shared)
        return false;
    const double z = (std::max)(kWhiteboardMinZoom, (std::min)(kWhiteboardMaxZoom, zoom));
    if (fabs(z - m_zoom) < kZoomEpsilon)
        return false;
    //锚点下面的世界位置（按世界的比例）缩放前后不变
    const double fx = static_cast<double>(m_scrollX + anchorX) / m_worldW;
    const double fy = static_cast<double>(m_scrollY + anchorY) / m_worldH;
    m_zoom = z;
    m_worldW = (std::max)(1, static_cast<int>(floor(m_baseWorldW * z + kRoundHalf)));
    m_worldH = (std::max)(1, static_cast<int>(floor(m_baseWorldH * z + kRoundHalf)));
    m_canvasW = m_worldW;
    m_canvasH = m_worldH;
    m_scrollX = static_cast<int>(floor(fx * m_worldW + kRoundHalf)) - anchorX;
    m_scrollY = static_cast<int>(floor(fy * m_worldH + kRoundHalf)) - anchorY;
    clampScroll();
    return true;
}

void ShareWhiteboardView::VisibleRegion(int& left, int& top, int& width) const
{
    left = static_cast<int>((static_cast<int64_t>(m_scrollX) * kAnnotCoordMax + m_worldW / 2) / m_worldW);
    top = static_cast<int>((static_cast<int64_t>(m_scrollY) * kAnnotCoordMax + m_worldH / 2) / m_worldH);
    const int visible = (std::min)(m_viewW, m_worldW);
    width = static_cast<int>((static_cast<int64_t>(visible) * kAnnotCoordMax + m_worldW / 2) / m_worldW);
    if (width < 1)
        width = 1;
}

void ShareWhiteboardView::ShowRegion(int left, int top, int width)
{
    if (!m_shared || width <= 0)
        return;
    //可见区域的宽正好是世界的 width / kAnnotCoordMax：由此得出世界应有的宽与缩放倍数
    const double worldW = static_cast<double>(m_viewW) * kAnnotCoordMax / width;
    SetZoom(worldW / m_baseWorldW, 0, 0);
    m_scrollX = static_cast<int>((static_cast<int64_t>(left) * m_worldW + kAnnotCoordMax / 2) / kAnnotCoordMax);
    m_scrollY = static_cast<int>((static_cast<int64_t>(top) * m_worldH + kAnnotCoordMax / 2) / kAnnotCoordMax);
    clampScroll();
}

int ShareWhiteboardView::WorldWidth() const
{
    return m_worldW;
}

int ShareWhiteboardView::WorldHeight() const
{
    return m_worldH;
}

int ShareWhiteboardView::CanvasWidth() const
{
    return m_canvasW;
}

int ShareWhiteboardView::CanvasHeight() const
{
    return m_canvasH;
}

int ShareWhiteboardView::ViewWidth() const
{
    return m_viewW;
}

int ShareWhiteboardView::ViewHeight() const
{
    return m_viewH;
}

int ShareWhiteboardView::ScrollX() const
{
    return m_scrollX;
}

int ShareWhiteboardView::ScrollY() const
{
    return m_scrollY;
}

double ShareWhiteboardView::Aspect() const
{
    return static_cast<double>(m_worldW) / m_worldH;
}

void ShareWhiteboardView::SetViewSize(int w, int h)
{
    //共享白板（第 23 期）：世界跟着可见区域的宽按比例变化，缩放倍数与可见区域在世界里的相对位置保持不变
    if (m_shared)
    {
        const double fx = static_cast<double>(m_scrollX) / m_worldW;
        const double fy = static_cast<double>(m_scrollY) / m_worldH;
        m_viewW = w > 0 ? w : 1;
        m_viewH = h > 0 ? h : 1;
        m_baseWorldW = m_viewW * kWhiteboardWorldScale;
        m_baseWorldH = (std::max)(1, (m_baseWorldW * kWhiteboardSharedAspectH + kWhiteboardSharedAspectW / 2) /
                                         kWhiteboardSharedAspectW);
        m_worldW = (std::max)(1, static_cast<int>(floor(m_baseWorldW * m_zoom + kRoundHalf)));
        m_worldH = (std::max)(1, static_cast<int>(floor(m_baseWorldH * m_zoom + kRoundHalf)));
        m_canvasW = m_worldW;
        m_canvasH = m_worldH;
        m_scrollX = static_cast<int>(floor(fx * m_worldW + kRoundHalf));
        m_scrollY = static_cast<int>(floor(fy * m_worldH + kRoundHalf));
        clampScroll();
        return;
    }
    m_viewW = w > 0 ? w : 1;
    m_viewH = h > 0 ? h : 1;
    //画布至少与可见区域一样大，不超过世界
    m_canvasW = (std::max)(m_canvasW, (std::min)(m_viewW, m_worldW));
    m_canvasH = (std::max)(m_canvasH, (std::min)(m_viewH, m_worldH));
    clampScroll();
}

bool ShareWhiteboardView::ScrollBy(int dx, int dy)
{
    const int oldX = m_scrollX;
    const int oldY = m_scrollY;
    m_scrollX += dx;
    m_scrollY += dy;
    clampScroll();
    return m_scrollX != oldX || m_scrollY != oldY;
}

int ShareWhiteboardView::HitEdge(int viewX, int viewY) const
{
    //共享白板（第 23 期）的画布就是整个世界，不拖边
    if (m_shared)
        return wb_edge_none;
    //画布的右边与下边在可见区域里的位置
    const int right = m_canvasW - m_scrollX;
    const int bottom = m_canvasH - m_scrollY;
    const bool nearRight = abs(viewX - right) <= kWhiteboardEdgeHitPx && viewY <= bottom + kWhiteboardEdgeHitPx;
    const bool nearBottom = abs(viewY - bottom) <= kWhiteboardEdgeHitPx && viewX <= right + kWhiteboardEdgeHitPx;
    if (nearRight && nearBottom)
        return wb_edge_corner;
    if (nearRight)
        return wb_edge_right;
    if (nearBottom)
        return wb_edge_bottom;
    return wb_edge_none;
}

void ShareWhiteboardView::DragEdge(int edge, int viewX, int viewY)
{
    //共享白板（第 23 期）的画布就是整个世界，不拖边
    if (m_shared)
        return;
    //拖动的边移到这一点在世界里的位置：不小于可见区域（也不超过世界）、不超过世界
    if (edge == wb_edge_right || edge == wb_edge_corner)
        m_canvasW = clampInt(viewX + m_scrollX, (std::min)(m_viewW, m_worldW), m_worldW);
    if (edge == wb_edge_bottom || edge == wb_edge_corner)
        m_canvasH = clampInt(viewY + m_scrollY, (std::min)(m_viewH, m_worldH), m_worldH);
    clampScroll();
}

RECT ShareWhiteboardView::ContentRect() const
{
    RECT rc = { -m_scrollX, -m_scrollY, m_worldW - m_scrollX, m_worldH - m_scrollY };
    return rc;
}

RECT ShareWhiteboardView::CanvasRect() const
{
    RECT rc = { -m_scrollX, -m_scrollY, m_canvasW - m_scrollX, m_canvasH - m_scrollY };
    return rc;
}

AnnotationPoint ShareWhiteboardView::ViewToAnnot(int x, int y) const
{
    //换到世界里再按世界归一化（四舍五入），限制在世界以内
    const int64_t wx = clampInt(x + m_scrollX, 0, m_worldW);
    const int64_t wy = clampInt(y + m_scrollY, 0, m_worldH);
    const int64_t nx = (wx * kAnnotCoordMax + m_worldW / 2) / m_worldW;
    const int64_t ny = (wy * kAnnotCoordMax + m_worldH / 2) / m_worldH;
    return AnnotationPoint(static_cast<uint16_t>(nx), static_cast<uint16_t>(ny));
}

void ShareWhiteboardView::BackgroundLines(int background, std::vector<int>& xs, std::vector<int>& ys) const
{
    xs.clear();
    ys.clear();
    //共享白板（第 23 期）的背景线间距按世界的大小缩放；本机白板照旧
    const int grid = lineSpacing(kWhiteboardGridPx);
    const int line = lineSpacing(kWhiteboardLinePx);
    switch (background)
    {
    //网格：竖线与横线
    case wb_bg_grid:
        linesAt(grid, m_scrollX, m_canvasW, m_viewW, xs);
        linesAt(grid, m_scrollY, m_canvasH, m_viewH, ys);
        break;
    //横线：只有横线
    case wb_bg_lines:
        linesAt(line, m_scrollY, m_canvasH, m_viewH, ys);
        break;
    //纯白与未知取值：没有线
    default:
        break;
    }
}

int ShareWhiteboardView::lineSpacing(int basePx) const
{
    if (!m_shared)
        return basePx;
    const int px = static_cast<int>(floor(static_cast<double>(basePx) * m_worldW / kWhiteboardSharedRefWorldW + kRoundHalf));
    return (std::max)(kMinLineSpacingPx, px);
}

void ShareWhiteboardView::clampScroll()
{
    m_scrollX = clampInt(m_scrollX, 0, m_canvasW - m_viewW);
    m_scrollY = clampInt(m_scrollY, 0, m_canvasH - m_viewH);
}

} // namespace screenshare
