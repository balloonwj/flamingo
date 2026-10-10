/**
 *  屏幕共享房间里本人的标注输入与收发，见 ShareAnnotationSession.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "ShareAnnotationSession.h"
#include "ShareAnnotationGeometry.h"   // AnnotConstrainDrag：按住 Shift 时的终点约束（2026-10-07）

#include <math.h>
#include <stdlib.h>
#include "ShareFraming.h"
#include "../../Source/net/Protocol.h"

namespace screenshare {

ShareAnnotationSession::ShareAnnotationSession()
    : m_transport(NULL)
    , m_selfSeat(0)
    , m_canvas(kAnnotDefaultCanvas)
    , m_running(false)
    , m_nextId(1)
    , m_tool(annot_tool_pen)
    , m_color(kAnnotDefaultColor)
    , m_drawing(false)
    , m_strokeId(0)
    , m_lastFlushMs(0)
    , m_shapeActive(false)
    , m_widthLevel(annot_width_medium)
    , m_lineStyle(annot_line_solid)
    , m_arrowStartCap(annot_cap_none)
    , m_arrowEndCap(annot_cap_triangle)
    , m_lineStartCap(annot_cap_none)
    , m_lineEndCap(annot_cap_none)
    , m_routing(annot_route_straight)
    , m_fill(annot_fill_none)
    , m_blockVariant(annot_block_right)
    , m_badgeStyle(annot_badge_circle)
    , m_badgeSequence(annot_seq_digit)
    , m_badgeNext(1)
    , m_textBold(false)
    , m_textBg(annot_text_bg_none)
    , m_textBgColor(0xFFFFFF)
    , m_shiftDown(false)
    , m_pointerSent(false)
    , m_lastPointerMs(0)
    , m_aspect(kAnnotDefaultAspect)
    , m_iconKind(annot_icon_computer)
    , m_connRouting(annot_route_straight)
    , m_widthScale(1.0)
    , m_selectDrag(select_drag_none)
    , m_moveWantDx(0)
    , m_moveWantDy(0)
    , m_moveSentDx(0)
    , m_moveSentDy(0)
    , m_lastMoveMs(0)
    , m_dragHandle(annot_handle_none)
    , m_dragChanged(false)
    , m_dragTolerance(0)
{
}

void ShareAnnotationSession::SetWidthLevel(int level)
{
    m_widthLevel = level;
}

int ShareAnnotationSession::WidthLevel() const
{
    return m_widthLevel;
}

void ShareAnnotationSession::SetLineStyle(int lineStyle)
{
    m_lineStyle = lineStyle;
}

void ShareAnnotationSession::SetArrowCaps(int startCap, int endCap)
{
    m_arrowStartCap = startCap;
    m_arrowEndCap = endCap;
}

void ShareAnnotationSession::SetLineCaps(int startCap, int endCap)
{
    m_lineStartCap = startCap;
    m_lineEndCap = endCap;
}

void ShareAnnotationSession::SetRouting(int routing)
{
    m_routing = routing;
}

void ShareAnnotationSession::SetFill(int fill)
{
    m_fill = fill;
}

void ShareAnnotationSession::SetBlockVariant(int variant)
{
    m_blockVariant = variant;
}

void ShareAnnotationSession::SetBadgeStyle(int badgeStyle, int sequence)
{
    m_badgeStyle = badgeStyle;
    m_badgeSequence = sequence;
}

void ShareAnnotationSession::SetIconKind(int kind)
{
    if (kind >= 0 && kind < kAnnotIconCount)
        m_iconKind = kind;
}

int ShareAnnotationSession::IconKind() const
{
    return m_iconKind;
}

void ShareAnnotationSession::SetConnectorRouting(int routing)
{
    m_connRouting = routing;
}

void ShareAnnotationSession::SetWidthScale(double scale)
{
    if (scale > 0)
        m_widthScale = scale;
}

double ShareAnnotationSession::WidthScale() const
{
    return m_widthScale;
}

void ShareAnnotationSession::ResetBadgeNumber()
{
    m_badgeNext = 1;
}

int ShareAnnotationSession::NextBadgeNumber() const
{
    return m_badgeNext;
}

void ShareAnnotationSession::SetTextStyle(bool bold, int textBg, uint32_t bgColor)
{
    m_textBold = bold;
    m_textBg = textBg;
    m_textBgColor = bgColor;
}

bool ShareAnnotationSession::SetShiftDown(bool down)
{
    if (down == m_shiftDown)
        return false;
    m_shiftDown = down;
    //拖动形状中 Shift 变了：按最近一次的鼠标位置重新算终点，鼠标不动预览也跟着变
    if (!m_shapeActive)
        return false;
    const AnnotationPoint to = constrainedEnd(m_shapeFrom, m_shapeRaw);
    const bool changed = (to.m_x != m_shapeTo.m_x || to.m_y != m_shapeTo.m_y);
    m_shapeTo = to;
    return changed;
}

bool ShareAnnotationSession::FinishPending(int64_t nowMs)
{
    if (m_multiPoints.empty())
        return false;
    const std::vector<AnnotationPoint> points = m_multiPoints;
    m_multiPoints.clear();
    const size_t kMinVertices = 2;
    if (!m_running || !CanDraw() || points.size() < kMinVertices)
        return false;
    AnnotationMessage m;
    m.m_op = annot_op_begin;
    m.m_id = allocId();
    m.m_tool = m_tool;
    m.m_color = m_color;
    m.m_width = widthForTool(m_tool);
    m.m_points = points;
    m.m_style = styleForTool(m_tool);
    if (!m_model.Apply(m_selfSeat, m, nowMs))
        return false;
    send(m);
    AnnotationMessage end;
    end.m_op = annot_op_end;
    end.m_id = m.m_id;
    m_model.Apply(m_selfSeat, end, nowMs);
    send(end);
    return true;
}

void ShareAnnotationSession::PointerHover(const AnnotationPoint& pt, int64_t nowMs)
{
    if (!m_running || !CanDraw())
        return;
    //折线、曲线：最后一个点跟着光标走
    if (!m_multiPoints.empty())
        m_multiFloat = pt;
    const bool moved = !m_pointerSent || pt.m_x != m_lastPointer.m_x || pt.m_y != m_lastPointer.m_y;
    if (!moved || (m_pointerSent && nowMs - m_lastPointerMs < kAnnotPointerIntervalMs))
        return;
    AnnotationMessage m;
    m.m_op = annot_op_pointer;
    m.m_tool = m_tool;
    m.m_color = m_color;
    m.m_points.push_back(pt);
    m.m_canvas = m_canvas;
    send(m);
    m_pointerSent = true;
    m_lastPointer = pt;
    m_lastPointerMs = nowMs;
}

void ShareAnnotationSession::PointerLeave()
{
    if (!m_pointerSent)
        return;
    m_pointerSent = false;
    if (!m_running)
        return;
    AnnotationMessage m;
    m.m_op = annot_op_pointer_leave;
    send(m);
}

bool ShareAnnotationSession::Tick(int64_t nowMs)
{
    return m_model.ExpireTransient(nowMs);
}

void ShareAnnotationSession::SetSeatName(int seat, const std::wstring& name)
{
    if (name.empty())
        m_seatNames.erase(seat);
    else
        m_seatNames[seat] = name;
}

std::wstring ShareAnnotationSession::SeatName(int seat) const
{
    std::map<int, std::wstring>::const_iterator it = m_seatNames.find(seat);
    return it != m_seatNames.end() ? it->second : std::wstring();
}

uint32_t ShareAnnotationSession::allocId()
{
    const uint32_t id = m_nextId++;
    if (m_nextId == 0)
        m_nextId = 1;
    return id;
}

void ShareAnnotationSession::SetCanvas(int canvas)
{
    if (canvas == m_canvas)
        return;
    //正在拖的一笔、添加顶点中的折线与选择都属于原来那块画面
    CancelPointer();
    m_selection.clear();
    m_canvas = canvas;
}

int ShareAnnotationSession::Canvas() const
{
    return m_canvas;
}

bool ShareAnnotationSession::SendBoardView(int canvas, const AnnotationBoardView& view)
{
    if (!m_running || view.m_width <= 0)
        return false;
    AnnotationMessage m;
    m.m_op = annot_op_board;
    m.m_canvas = canvas;
    m.m_board = view;
    //自己发的视野只在本地记下，收到的时刻用不上
    m_model.Apply(m_selfSeat, m, 0);
    send(m);
    return true;
}

bool ShareAnnotationSession::ClearCanvasLocal(int canvas)
{
    if (canvas == m_canvas)
    {
        CancelPointer();
        m_selection.clear();
    }
    return m_model.ClearCanvas(canvas);
}

std::vector<AnnotationStroke> ShareAnnotationSession::canvasStrokes() const
{
    return AnnotStrokesOnCanvas(m_model.Strokes(), m_canvas);
}

AnnotationStyle ShareAnnotationSession::styleForTool(int tool) const
{
    AnnotationStyle s;
    s.m_lineStyle = m_lineStyle;
    //第 22 期 22e：画在当前画面上
    s.m_canvas = m_canvas;
    switch (tool)
    {
    //箭头：两端、走向
    case annot_tool_arrow:
        s.m_startCap = m_arrowStartCap;
        s.m_endCap = m_arrowEndCap;
        s.m_routing = m_routing;
        break;
    //直线、折线、曲线：两端
    case annot_tool_line:
    case annot_tool_polyline:
    case annot_tool_curve:
        s.m_startCap = m_lineStartCap;
        s.m_endCap = m_lineEndCap;
        break;
    //块状箭头：样式与填充
    case annot_tool_block_arrow:
        s.m_variant = m_blockVariant;
        s.m_fill = m_fill;
        break;
    //数字标号：样式、序号写法与号码（号码由 placeBadge 填）
    case annot_tool_badge:
        s.m_variant = m_badgeStyle;
        s.m_sequence = m_badgeSequence;
        s.m_lineStyle = annot_line_solid;
        break;
    //网络拓扑图元：图元种类（第 20 期）
    case annot_tool_icon:
        s.m_variant = m_iconKind;
        s.m_lineStyle = annot_line_solid;
        break;
    //连接线：走向，两端默认不带样式（第 20 期）
    case annot_tool_connector:
        s.m_routing = m_connRouting;
        break;
    //文字：加粗与背景
    case annot_tool_text:
        s.m_bold = m_textBold;
        s.m_textBg = m_textBg;
        s.m_bgColor = m_textBgColor;
        s.m_lineStyle = annot_line_solid;
        break;
    //自由线条不用线型
    case annot_tool_pen:
    case annot_tool_highlighter:
    case annot_tool_pencil:
    case annot_tool_marker:
    case annot_tool_brush:
    case annot_tool_laser:
    case annot_tool_eraser_area:
        s.m_lineStyle = annot_line_solid;
        break;
    //形状：填充
    default:
        s.m_fill = m_fill;
        break;
    }
    return s;
}

bool ShareAnnotationSession::isFreehandTool(int tool)
{
    return tool == annot_tool_pen || tool == annot_tool_highlighter || tool == annot_tool_pencil ||
           tool == annot_tool_marker || tool == annot_tool_brush || tool == annot_tool_laser ||
           tool == annot_tool_eraser_area;
}

bool ShareAnnotationSession::isTwoPointTool(int tool)
{
    switch (tool)
    {
    //直线、箭头、块状箭头与各种形状
    case annot_tool_arrow:
    case annot_tool_rect:
    case annot_tool_ellipse:
    case annot_tool_line:
    case annot_tool_block_arrow:
    case annot_tool_round_rect:
    case annot_tool_triangle:
    case annot_tool_diamond:
    case annot_tool_pentagon:
    case annot_tool_hexagon:
    case annot_tool_star:
    case annot_tool_parallelogram:
    case annot_tool_bubble:
    case annot_tool_cloud:
    case annot_tool_icon:
    case annot_tool_connector:
        return true;
    //其余不是
    default:
        return false;
    }
}

bool ShareAnnotationSession::isMultiPointTool(int tool)
{
    return tool == annot_tool_polyline || tool == annot_tool_curve;
}

AnnotationPoint ShareAnnotationSession::constrainedEnd(const AnnotationPoint& from, const AnnotationPoint& to) const
{
    if (!m_shiftDown)
        return to;
    //2026-10-07 起按内容的宽高比在等长坐标里算（AnnotConstrainDrag）：原先直接在归一化坐标里取 dx == dy，16:9 的内容上
    //画出的「正方形 / 圆」实际是 16:9、「45 度」实际约 29 度，五角星、三角形等也因外接框是正方形而被拉伸；连接线改按直线吸附
    const GeoPoint end = AnnotConstrainDrag(m_tool, GeoPoint(from.m_x, from.m_y), GeoPoint(to.m_x, to.m_y), m_aspect,
                                            static_cast<double>(kAnnotCoordMax));
    //夹到归一化坐标范围里（AnnotConstrainDrag 已保证不越界，这里防浮点误差），四舍五入
    double x = end.m_x < 0 ? 0 : (end.m_x > kAnnotCoordMax ? kAnnotCoordMax : end.m_x);
    double y = end.m_y < 0 ? 0 : (end.m_y > kAnnotCoordMax ? kAnnotCoordMax : end.m_y);
    return AnnotationPoint(static_cast<uint16_t>(x + 0.5), static_cast<uint16_t>(y + 0.5));
}

void ShareAnnotationSession::defaultIconRect(const AnnotationPoint& center, AnnotationPoint& a, AnnotationPoint& b) const
{
    //纵向 kAnnotIconDefaultSize，横向按宽高比换算成显示上的正方形；靠边时整体挪进画面
    const int halfY = kAnnotIconDefaultSize / 2;
    const int halfX = static_cast<int>(lround(kAnnotIconDefaultSize / m_aspect / 2));
    int left = static_cast<int>(center.m_x) - halfX;
    int top = static_cast<int>(center.m_y) - halfY;
    left = left < 0 ? 0 : (left + 2 * halfX > kAnnotCoordMax ? kAnnotCoordMax - 2 * halfX : left);
    top = top < 0 ? 0 : (top + 2 * halfY > kAnnotCoordMax ? kAnnotCoordMax - 2 * halfY : top);
    a = AnnotationPoint(static_cast<uint16_t>(left), static_cast<uint16_t>(top));
    b = AnnotationPoint(static_cast<uint16_t>(left + 2 * halfX), static_cast<uint16_t>(top + 2 * halfY));
}

void ShareAnnotationSession::placeBadge(const AnnotationPoint& pt, int64_t nowMs)
{
    AnnotationMessage m;
    m.m_op = annot_op_begin;
    m.m_id = allocId();
    m.m_tool = annot_tool_badge;
    m.m_color = m_color;
    m.m_width = widthForTool(annot_tool_badge);
    m.m_points.push_back(pt);
    m.m_style = styleForTool(annot_tool_badge);
    m.m_style.m_number = m_badgeNext;
    if (!m_model.Apply(m_selfSeat, m, nowMs))
        return;
    send(m);
    AnnotationMessage end;
    end.m_op = annot_op_end;
    end.m_id = m.m_id;
    m_model.Apply(m_selfSeat, end, nowMs);
    send(end);
    //号码只增不减；超出两字节时从 1 再来
    const int kMaxBadgeNumber = 65535;
    m_badgeNext = (m_badgeNext >= kMaxBadgeNumber) ? 1 : m_badgeNext + 1;
}

void ShareAnnotationSession::Start(ShareAudioTransport* transport, int selfSeat, uint32_t idSeed)
{
    m_model.Clear();
    m_model.SetViewersAllowed(true);
    //新的一场：主持人回到发起人，白板建立者清空（之后由控制器按房间快照设置，2026-10-07）
    m_model.SetHostSeat(kAnnotHostSeat);
    m_model.SetBoardOwners(std::map<int, int>());
    m_transport = transport;
    m_selfSeat = selfSeat;
    m_running = true;
    //编号 0 表示「没有正在画的笔」，不分配
    m_nextId = (idSeed == 0) ? 1 : idSeed;
    m_drawing = false;
    m_strokeId = 0;
    m_pendingPoints.clear();
    m_shapeActive = false;
    m_selection.clear();
    m_selectDrag = select_drag_none;
}

void ShareAnnotationSession::Stop()
{
    m_running = false;
    m_transport = NULL;
    m_drawing = false;
    m_strokeId = 0;
    m_pendingPoints.clear();
    m_shapeActive = false;
    m_selection.clear();
    m_selectDrag = select_drag_none;
    m_model.Clear();
}

bool ShareAnnotationSession::IsRunning() const
{
    return m_running;
}

void ShareAnnotationSession::SetTool(int tool)
{
    if (tool == m_tool)
        return;
    CancelPointer();
    //离开选择工具时取消选择
    if (m_tool == kAnnotSelect)
        m_selection.clear();
    m_tool = tool;
}

int ShareAnnotationSession::Tool() const
{
    return m_tool;
}

void ShareAnnotationSession::SetColor(uint32_t color)
{
    m_color = color;
}

uint32_t ShareAnnotationSession::Color() const
{
    return m_color;
}

int ShareAnnotationSession::widthForTool(int tool) const
{
    return scaledWidth(tool, m_widthLevel);
}

int ShareAnnotationSession::scaledWidth(int tool, int level) const
{
    //按缩放比例（第 21 期，白板用）四舍五入，不小于 1
    const long w = lround(widthForLevel(tool, level) * m_widthScale);
    return w < 1 ? 1 : static_cast<int>(w);
}

int ShareAnnotationSession::widthForLevel(int tool, int widthLevel)
{
    //各类工具细、中、粗三档的线宽（文字、数字标号为字号）；中档与第 6 期的默认值一致
    static const int kPenWidths[] = { 2, kAnnotPenWidth, 8 };
    static const int kBrushWidths[] = { 3, 6, 10 };
    static const int kMarkerWidths[] = { 6, 10, 16 };
    static const int kHighlighterWidths[] = { 12, kAnnotHighlighterWidth, 26 };
    static const int kLaserWidths[] = { 3, 5, 8 };
    static const int kEraserWidths[] = { 20, 40, 70 };
    static const int kShapeWidths[] = { 2, kAnnotShapeWidth, 8 };
    static const int kTextSizes[] = { 20, kAnnotTextSize, 40 };
    static const int kBadgeSizes[] = { 24, 32, 44 };
    const int level = (widthLevel < annot_width_thin || widthLevel > annot_width_thick) ? annot_width_medium
                                                                                         : widthLevel;
    switch (tool)
    {
    //画笔、铅笔
    case annot_tool_pen:
    case annot_tool_pencil:
        return kPenWidths[level];
    //毛笔
    case annot_tool_brush:
        return kBrushWidths[level];
    //马克笔
    case annot_tool_marker:
        return kMarkerWidths[level];
    //荧光笔较粗
    case annot_tool_highlighter:
        return kHighlighterWidths[level];
    //激光笔
    case annot_tool_laser:
        return kLaserWidths[level];
    //局部橡皮
    case annot_tool_eraser_area:
        return kEraserWidths[level];
    //文字为字号
    case annot_tool_text:
        return kTextSizes[level];
    //数字标号为字号
    case annot_tool_badge:
        return kBadgeSizes[level];
    //直线、箭头、形状
    default:
        return kShapeWidths[level];
    }
}

void ShareAnnotationSession::send(const AnnotationMessage& m)
{
    if (m_transport == NULL)
        return;
    const std::string payload = ShareAnnotationCodec::encode(m);
    if (payload.empty())
        return;
    ShareFrameHeader header;
    header.m_track = screenshare_track_annotation;
    header.m_senderSeat = m_selfSeat;
    header.m_flags = screenshare_flag_first_frag | screenshare_flag_last_frag;
    header.m_timestamp = 0;
    m_transport->SendRoomBody(ShareFraming::encodeBody(header, payload.data(), payload.size()));
}

void ShareAnnotationSession::PointerDown(const AnnotationPoint& pt, int tolerance, int64_t nowMs)
{
    if (!m_running || !CanDraw())
        return;
    //选择工具（第 20 期）
    if (m_tool == kAnnotSelect)
    {
        selectDown(pt, tolerance, nowMs);
        return;
    }
    //折线、曲线：每次按下添加一个顶点，「完成」时才发（不结束正在添加的顶点）
    if (isMultiPointTool(m_tool))
    {
        m_multiPoints.push_back(pt);
        m_multiFloat = pt;
        return;
    }
    CancelPointer();
    m_drawing = true;
    //橡皮：按下处有笔就擦
    if (m_tool == kAnnotEraser)
    {
        eraseAt(pt, tolerance);
        return;
    }
    //数字标号：点一下放一个
    if (m_tool == annot_tool_badge)
    {
        placeBadge(pt, nowMs);
        m_drawing = false;
        return;
    }
    //直线、箭头、形状、图元、连接线：只记起点，松开时才发；连接线松开时按容差找两端的吸附对象
    if (isTwoPointTool(m_tool))
    {
        m_shapeActive = true;
        m_shapeFrom = pt;
        m_shapeTo = pt;
        m_shapeRaw = pt;
        m_dragTolerance = tolerance;
        return;
    }
    //自由线条：立即发 begin
    if (isFreehandTool(m_tool))
    {
        AnnotationMessage m;
        m.m_op = annot_op_begin;
        m.m_id = allocId();
        m.m_tool = m_tool;
        m.m_color = m_color;
        m.m_width = widthForTool(m_tool);
        m.m_points.push_back(pt);
        m.m_style = styleForTool(m_tool);
        if (m_model.Apply(m_selfSeat, m, nowMs))
        {
            send(m);
            m_strokeId = m.m_id;
            m_lastFlushMs = nowMs;
        }
        return;
    }
    //文字工具：由界面弹出输入框后调 AddText，这里不画
    m_drawing = false;
}

void ShareAnnotationSession::PointerMove(const AnnotationPoint& pt, int tolerance, int64_t nowMs)
{
    //折线、曲线：最后一个点跟着光标走（按没按着都一样）
    if (m_running && !m_multiPoints.empty())
    {
        m_multiFloat = pt;
        return;
    }
    if (!m_running || !m_drawing)
        return;
    //选择工具（第 20 期）
    if (m_tool == kAnnotSelect)
    {
        selectMove(pt, nowMs);
        return;
    }
    if (m_tool == kAnnotEraser)
    {
        eraseAt(pt, tolerance);
        return;
    }
    if (m_shapeActive)
    {
        m_shapeRaw = pt;
        m_shapeTo = constrainedEnd(m_shapeFrom, pt);
        return;
    }
    if (m_strokeId == 0)
        return;
    //与上一个点相同的不要
    const AnnotationPoint* last = m_pendingPoints.empty() ? NULL : &m_pendingPoints.back();
    if (last == NULL)
    {
        const std::vector<AnnotationStroke>& strokes = m_model.Strokes();
        for (size_t i = strokes.size(); i > 0; --i)
        {
            if (strokes[i - 1].m_author == m_selfSeat && strokes[i - 1].m_id == m_strokeId)
            {
                if (!strokes[i - 1].m_points.empty())
                    last = &strokes[i - 1].m_points.back();
                break;
            }
        }
    }
    if (last != NULL && last->m_x == pt.m_x && last->m_y == pt.m_y)
        return;
    m_pendingPoints.push_back(pt);
    flushAppend(nowMs, false);
}

void ShareAnnotationSession::PointerUp(const AnnotationPoint& pt, int64_t nowMs)
{
    if (!m_running || !m_drawing)
        return;
    //选择工具（第 20 期）
    if (m_tool == kAnnotSelect)
    {
        selectUp(pt, nowMs);
        return;
    }
    //图形：松开时一次发出两点的 begin 与 end；只是点了一下（两点重合）不发
    if (m_shapeActive)
    {
        m_shapeTo = constrainedEnd(m_shapeFrom, pt);
        m_shapeActive = false;
        m_drawing = false;
        AnnotationPoint from = m_shapeFrom;
        AnnotationPoint to = m_shapeTo;
        AnnotationStyle style = styleForTool(m_tool);
        //图元：只是点了一下（或拖得太小）时以按下处为中心放一个默认大小的
        if (m_tool == annot_tool_icon)
        {
            if (abs(static_cast<int>(to.m_x) - from.m_x) < kAnnotMinObjectSize &&
                abs(static_cast<int>(to.m_y) - from.m_y) < kAnnotMinObjectSize)
                defaultIconRect(m_shapeFrom, from, to);
        }
        else if (to.m_x == from.m_x && to.m_y == from.m_y)
        {
            //其余只是点了一下（两点重合）：不发
            return;
        }
        //连接线：两端落在图元或形状上就吸附过去；两端在同一个对象上时只吸附起点
        if (m_tool == annot_tool_connector)
        {
            const std::vector<AnnotationStroke> all = canvasStrokes();
            const int a = AnnotHitAttachable(all, from, m_dragTolerance, m_aspect, AnnotationRef());
            const int b = AnnotHitAttachable(all, to, m_dragTolerance, m_aspect, AnnotationRef());
            if (a >= 0)
                style.m_attachStart = AnnotationRef(all[a].m_author, all[a].m_id);
            if (b >= 0 && b != a)
                style.m_attachEnd = AnnotationRef(all[b].m_author, all[b].m_id);
        }
        AnnotationMessage m;
        m.m_op = annot_op_begin;
        m.m_id = allocId();
        m.m_tool = m_tool;
        m.m_color = m_color;
        m.m_width = widthForTool(m_tool);
        m.m_points.push_back(from);
        m.m_points.push_back(to);
        m.m_style = style;
        if (m_model.Apply(m_selfSeat, m, nowMs))
        {
            send(m);
            AnnotationMessage end;
            end.m_op = annot_op_end;
            end.m_id = m.m_id;
            m_model.Apply(m_selfSeat, end, nowMs);
            send(end);
        }
        return;
    }
    if (m_tool != kAnnotEraser)
        PointerMove(pt, 0, nowMs);
    finishStroke(nowMs);
}

void ShareAnnotationSession::finishStroke(int64_t nowMs)
{
    if (m_strokeId != 0)
    {
        flushAppend(nowMs, true);
        AnnotationMessage end;
        end.m_op = annot_op_end;
        end.m_id = m_strokeId;
        m_model.Apply(m_selfSeat, end, nowMs);
        send(end);
    }
    m_strokeId = 0;
    m_pendingPoints.clear();
    m_drawing = false;
}

void ShareAnnotationSession::CancelPointer()
{
    //选择工具的拖动：发出剩下的位移或最终的修改（第 20 期）
    finishSelectDrag(m_lastMoveMs);
    m_shapeActive = false;
    //折线、曲线还没完成的顶点：丢弃
    m_multiPoints.clear();
    finishStroke(m_lastFlushMs);
}

void ShareAnnotationSession::flushAppend(int64_t nowMs, bool force)
{
    if (m_strokeId == 0 || m_pendingPoints.empty())
        return;
    if (!force && nowMs - m_lastFlushMs < kAnnotAppendIntervalMs && m_pendingPoints.size() < kAnnotAppendMaxPoints)
        return;
    AnnotationMessage m;
    m.m_op = annot_op_append;
    m.m_id = m_strokeId;
    m.m_points.swap(m_pendingPoints);
    if (m_model.Apply(m_selfSeat, m, nowMs))
        send(m);
    m_pendingPoints.clear();
    m_lastFlushMs = nowMs;
}

void ShareAnnotationSession::eraseAt(const AnnotationPoint& pt, int tolerance)
{
    //第 20 期起按对象命中：形状只认边线，图元、文字、标号点在里面也算，连接线按吸附后的实际位置；只看当前画面（第 22 期）
    const std::vector<AnnotationStroke> all = canvasStrokes();
    const int index = AnnotHitObject(all, pt, tolerance, m_aspect, false);
    if (index < 0)
        return;
    AnnotationMessage m;
    m.m_op = annot_op_erase;
    m.m_author = all[index].m_author;
    m.m_id = all[index].m_id;
    if (m_model.Apply(m_selfSeat, m))
        send(m);
}

bool ShareAnnotationSession::PendingShape(AnnotationStroke& out) const
{
    if (!m_shapeActive && m_multiPoints.empty())
        return false;
    out = AnnotationStroke();
    out.m_author = m_selfSeat;
    out.m_tool = m_tool;
    out.m_color = m_color;
    out.m_width = widthForTool(m_tool);
    out.m_style = styleForTool(m_tool);
    //折线、曲线：已定的顶点加跟着光标的点
    if (!m_multiPoints.empty())
    {
        out.m_points = m_multiPoints;
        out.m_points.push_back(m_multiFloat);
        return true;
    }
    out.m_points.push_back(m_shapeFrom);
    out.m_points.push_back(m_shapeTo);
    return true;
}

bool ShareAnnotationSession::AddText(const AnnotationPoint& pt, const std::string& utf8)
{
    if (!m_running || !CanDraw() || utf8.empty())
        return false;
    AnnotationMessage m;
    m.m_op = annot_op_text;
    m.m_id = allocId();
    m.m_tool = annot_tool_text;
    m.m_color = m_color;
    m.m_width = widthForTool(annot_tool_text);
    m.m_points.push_back(pt);
    m.m_text = utf8.size() > static_cast<size_t>(kAnnotMaxTextBytes) ? utf8.substr(0, kAnnotMaxTextBytes) : utf8;
    m.m_style = styleForTool(annot_tool_text);
    if (!m_model.Apply(m_selfSeat, m))
        return false;
    send(m);
    return true;
}

bool ShareAnnotationSession::UndoOwn()
{
    if (!m_running || !CanDraw())
        return false;
    CancelPointer();
    uint32_t id = 0;
    //只撤当前画面上本人的最后一笔（第 22 期）
    if (!m_model.LastStrokeOf(m_selfSeat, m_canvas, id))
        return false;
    AnnotationMessage m;
    m.m_op = annot_op_erase;
    m.m_author = m_selfSeat;
    m.m_id = id;
    if (!m_model.Apply(m_selfSeat, m))
        return false;
    send(m);
    pruneSelection();
    return true;
}

bool ShareAnnotationSession::ClearAll()
{
    if (!m_running || m_selfSeat != m_model.HostSeat())
        return false;
    CancelPointer();
    AnnotationMessage m;
    m.m_op = annot_op_clear;
    //只清当前画面（第 22 期）
    m.m_canvas = m_canvas;
    m_model.Apply(m_selfSeat, m);
    send(m);
    m_selection.clear();
    return true;
}

void ShareAnnotationSession::SendSnapshot()
{
    if (!m_running || m_selfSeat != m_model.HostSeat())
        return;
    const std::vector<AnnotationMessage> all = m_model.Snapshot();
    for (size_t i = 0; i < all.size(); ++i)
        send(all[i]);
}

bool ShareAnnotationSession::OnRemoteFrame(int senderSeat, const std::string& payload, int64_t nowMs)
{
    if (!m_running)
        return false;
    AnnotationMessage m;
    if (!ShareAnnotationCodec::decode(payload, m))
        return false;
    if (!m_model.Apply(senderSeat, m, nowMs))
        return false;
    //别人删掉了选中的对象：从选择里去掉（第 20 期）
    pruneSelection();
    return true;
}

bool ShareAnnotationSession::OnRemoteFrame(int senderSeat, const std::string& payload)
{
    return OnRemoteFrame(senderSeat, payload, 0);
}

void ShareAnnotationSession::SetViewersAllowed(bool allowed)
{
    //观看者被收回权限：先把进行中的一笔收尾（此时本地模型仍接受），再改权限；正画在自己的白板上时不受影响
    if (!allowed && m_selfSeat != m_model.HostSeat() && m_model.BoardOwnerOf(m_canvas) != m_selfSeat)
        CancelPointer();
    m_model.SetViewersAllowed(allowed);
}

void ShareAnnotationSession::SetBoardOwners(const std::map<int, int>& owners)
{
    m_model.SetBoardOwners(owners);
}

void ShareAnnotationSession::SetHostSeat(int seat)
{
    m_model.SetHostSeat(seat);
}

bool ShareAnnotationSession::CanDraw() const
{
    return CanDrawOn(m_canvas);
}

bool ShareAnnotationSession::CanDrawOn(int canvas) const
{
    return m_selfSeat == m_model.HostSeat() || m_model.ViewersAllowed() || m_model.BoardOwnerOf(canvas) == m_selfSeat;
}

void ShareAnnotationSession::ClearLocal()
{
    CancelPointer();
    m_model.Clear();
    m_selection.clear();
}

const ShareAnnotationModel& ShareAnnotationSession::Model() const
{
    return m_model;
}

} // namespace screenshare
