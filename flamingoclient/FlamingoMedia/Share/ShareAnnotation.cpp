/**
 *  屏幕共享标注的数据模型与编解码，见 ShareAnnotation.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "ShareAnnotation.h"

#include <math.h>
#include <stdlib.h>
#include <algorithm>

namespace screenshare {

namespace {

//各字段的字节数
const size_t kOpBytes = 1;
const size_t kIdBytes = 4;
const size_t kToolBytes = 1;
const size_t kColorBytes = 3;
const size_t kWidthBytes = 1;
const size_t kCountBytes = 2;
const size_t kPointBytes = 4;
const size_t kSeatBytes = 1;
const size_t kFlagBytes = 1;
const size_t kTextLenBytes = 2;
//字节运算用到的位移与掩码
const int kByteBits = 8;
const uint32_t kByteMask = 0xFF;
//线宽、字号的上限（1 字节）
const int kMaxWidthValue = 255;
//移动位移的范围（2 字节有符号数）
const int kMinMoveDelta = -32768;
const int kMaxMoveDelta = 32767;

//顺序写出大端字段
class Writer
{
public:
    void u8(uint32_t v)
    {
        m_out.push_back(static_cast<char>(v & kByteMask));
    }

    void u16(uint32_t v)
    {
        u8(v >> kByteBits);
        u8(v);
    }

    void u24(uint32_t v)
    {
        u8(v >> (2 * kByteBits));
        u8(v >> kByteBits);
        u8(v);
    }

    void u32(uint32_t v)
    {
        u16(v >> (2 * kByteBits));
        u16(v);
    }

    void points(const std::vector<AnnotationPoint>& pts, size_t count)
    {
        u16(static_cast<uint32_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            u16(pts[i].m_x);
            u16(pts[i].m_y);
        }
    }

    void bytes(const std::string& s, size_t count)
    {
        m_out.append(s.data(), count);
    }

    //样式块（第 19 期）：[长度 1B][线型][起点][终点][走向][填充][旋转 2B][样式][序号写法][号码 2B][加粗][文字背景][背景色 3B]，
    //第 20 期在其后追加 [起点吸附对象 5B][终点吸附对象 5B]，第 22 期再追加 [画面座位 1B]
    void style(const AnnotationStyle& s)
    {
        u8(static_cast<uint32_t>(kAnnotStyleBytesV3));
        u8(static_cast<uint32_t>(s.m_lineStyle));
        u8(static_cast<uint32_t>(s.m_startCap));
        u8(static_cast<uint32_t>(s.m_endCap));
        u8(static_cast<uint32_t>(s.m_routing));
        u8(static_cast<uint32_t>(s.m_fill));
        u16(static_cast<uint32_t>(s.m_rotation));
        u8(static_cast<uint32_t>(s.m_variant));
        u8(static_cast<uint32_t>(s.m_sequence));
        u16(static_cast<uint32_t>(s.m_number));
        u8(s.m_bold ? 1u : 0u);
        u8(static_cast<uint32_t>(s.m_textBg));
        u24(s.m_bgColor);
        ref(s.m_attachStart);
        ref(s.m_attachEnd);
        u8(static_cast<uint32_t>(s.m_canvas));
    }

    //一个对象的引用（第 20 期）：[作者座位号 1B][编号 4B]；空引用的座位号写 kAnnotNoAttachSeat、编号写 0
    void ref(const AnnotationRef& r)
    {
        u8(static_cast<uint32_t>(r.IsSet() ? r.m_author : kAnnotNoAttachSeat));
        u32(r.IsSet() ? r.m_id : 0);
    }

    std::string m_out;   //已写出的字节
};

//顺序读出大端字段；越界时置失败标志，之后的读取都返回 0
class Reader
{
public:
    explicit Reader(const std::string& in)
        : m_in(in)
        , m_pos(0)
        , m_ok(true)
    {
    }

    uint32_t u8()
    {
        if (!m_ok || m_pos + 1 > m_in.size())
        {
            m_ok = false;
            return 0;
        }
        return static_cast<uint32_t>(static_cast<unsigned char>(m_in[m_pos++]));
    }

    uint32_t u16()
    {
        const uint32_t hi = u8();
        return (hi << kByteBits) | u8();
    }

    uint32_t u24()
    {
        const uint32_t hi = u8();
        return (hi << (2 * kByteBits)) | u16();
    }

    uint32_t u32()
    {
        const uint32_t hi = u16();
        return (hi << (2 * kByteBits)) | u16();
    }

    bool points(std::vector<AnnotationPoint>& pts)
    {
        const uint32_t n = u16();
        if (!m_ok || n > static_cast<uint32_t>(kAnnotMaxStrokePoints) || m_pos + n * kPointBytes > m_in.size())
        {
            m_ok = false;
            return false;
        }
        pts.clear();
        pts.reserve(n);
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint16_t x = static_cast<uint16_t>(u16());
            const uint16_t y = static_cast<uint16_t>(u16());
            pts.push_back(AnnotationPoint(x, y));
        }
        return m_ok;
    }

    bool text(std::string& s)
    {
        const uint32_t n = u16();
        if (!m_ok || n > static_cast<uint32_t>(kAnnotMaxTextBytes) || m_pos + n > m_in.size())
        {
            m_ok = false;
            return false;
        }
        s.assign(m_in, m_pos, n);
        m_pos += n;
        return true;
    }

    /**
     *  读样式块（第 19 期）：长度不小于当前版本的样式字节数，读出当前版本认识的字段，多出的字节（以后版本加的字段）跳过。
     *  @return 长度足够且没有越界返回 true
     */
    bool style(AnnotationStyle& s)
    {
        const uint32_t len = u8();
        if (!m_ok || len < kAnnotStyleBytes || m_pos + len > m_in.size())
        {
            m_ok = false;
            return false;
        }
        const size_t end = m_pos + len;
        s.m_lineStyle = static_cast<int>(u8());
        s.m_startCap = static_cast<int>(u8());
        s.m_endCap = static_cast<int>(u8());
        s.m_routing = static_cast<int>(u8());
        s.m_fill = static_cast<int>(u8());
        s.m_rotation = static_cast<int>(u16());
        s.m_variant = static_cast<int>(u8());
        s.m_sequence = static_cast<int>(u8());
        s.m_number = static_cast<int>(u16());
        s.m_bold = (u8() != 0);
        s.m_textBg = static_cast<int>(u8());
        s.m_bgColor = u24();
        //第 20 期的吸附对象；第 19 期的样式块没有，保持空引用
        if (len >= kAnnotStyleBytesV2)
        {
            s.m_attachStart = ref();
            s.m_attachEnd = ref();
        }
        //第 22 期的画面座位；更早的样式块没有，按默认画面
        s.m_canvas = (len >= kAnnotStyleBytesV3) ? static_cast<int>(u8()) : kAnnotDefaultCanvas;
        m_pos = end;
        return m_ok;
    }

    //一个对象的引用（第 20 期）；座位号为 kAnnotNoAttachSeat 时为空引用
    AnnotationRef ref()
    {
        const int author = static_cast<int>(u8());
        const uint32_t id = u32();
        if (author == kAnnotNoAttachSeat)
            return AnnotationRef();
        return AnnotationRef(author, id);
    }

    //全部读完且没有越界
    bool done() const
    {
        return m_ok && m_pos == m_in.size();
    }

    bool ok() const
    {
        return m_ok;
    }

private:
    const std::string& m_in;    //输入
    size_t             m_pos;   //读到的位置
    bool               m_ok;    //是否还没有越界
};

bool validTool(int tool)
{
    return tool >= annot_tool_pen && tool <= kAnnotToolLast;
}

/**
 *  begin / text / full 的必有字段读完之后：还有字节就是样式块（第 19 期），没有就是老版本发的操作，按默认样式；
 *  老版本的箭头终点画成开口 V 形，与当时的样子一致。
 *  @return 样式块合法（或没有样式块）返回 true
 */
bool readOptionalStyle(Reader& r, AnnotationMessage& m)
{
    if (!r.ok())
        return false;
    if (r.done())
    {
        m.m_hasStyle = false;
        m.m_style = AnnotationStyle();
        if (m.m_tool == annot_tool_arrow)
            m.m_style.m_endCap = annot_cap_open;
        return true;
    }
    m.m_hasStyle = true;
    return r.style(m.m_style);
}

size_t cappedPoints(const std::vector<AnnotationPoint>& pts)
{
    return pts.size() > static_cast<size_t>(kAnnotMaxStrokePoints) ? static_cast<size_t>(kAnnotMaxStrokePoints)
                                                                     : pts.size();
}

size_t cappedText(const std::string& s)
{
    return s.size() > static_cast<size_t>(kAnnotMaxTextBytes) ? static_cast<size_t>(kAnnotMaxTextBytes) : s.size();
}

//移动位移限制在 2 字节有符号数的范围内
int clampDelta(int d)
{
    if (d < kMinMoveDelta)
        return kMinMoveDelta;
    return d > kMaxMoveDelta ? kMaxMoveDelta : d;
}

uint32_t clampWidth(int w)
{
    if (w < 0)
        return 0;
    return static_cast<uint32_t>(w > kMaxWidthValue ? kMaxWidthValue : w);
}

//点到线段的距离的平方（归一化单位）
int64_t distSqToSegment(int px, int py, const AnnotationPoint& a, const AnnotationPoint& b)
{
    const int64_t ax = a.m_x;
    const int64_t ay = a.m_y;
    const int64_t dx = static_cast<int64_t>(b.m_x) - ax;
    const int64_t dy = static_cast<int64_t>(b.m_y) - ay;
    const int64_t len2 = dx * dx + dy * dy;
    int64_t qx = ax;
    int64_t qy = ay;
    if (len2 > 0)
    {
        //投影系数 t = ((p - a)·d) / |d|²，夹到 [0, 1]；用整数比例算出投影点
        int64_t num = (px - ax) * dx + (py - ay) * dy;
        if (num < 0)
            num = 0;
        if (num > len2)
            num = len2;
        qx = ax + dx * num / len2;
        qy = ay + dy * num / len2;
    }
    const int64_t ex = px - qx;
    const int64_t ey = py - qy;
    return ex * ex + ey * ey;
}

//点到一笔的距离的平方：折线按各段，矩形按四条边，椭圆按外接矩形内接的折线近似（取 36 段），文字按左上角点
int64_t distSqToStroke(int x, int y, const AnnotationStroke& s)
{
    const int64_t kFar = INT64_MAX;
    if (s.m_points.empty())
        return kFar;
    if (s.m_tool == annot_tool_text || s.m_points.size() == 1)
        return distSqToSegment(x, y, s.m_points[0], s.m_points[0]);
    if (s.m_tool == annot_tool_rect)
    {
        const AnnotationPoint& p0 = s.m_points[0];
        const AnnotationPoint& p1 = s.m_points[1];
        const AnnotationPoint c1(p1.m_x, p0.m_y);
        const AnnotationPoint c2(p0.m_x, p1.m_y);
        int64_t best = distSqToSegment(x, y, p0, c1);
        int64_t d = distSqToSegment(x, y, c1, p1);
        if (d < best)
            best = d;
        d = distSqToSegment(x, y, p1, c2);
        if (d < best)
            best = d;
        d = distSqToSegment(x, y, c2, p0);
        if (d < best)
            best = d;
        return best;
    }
    if (s.m_tool == annot_tool_ellipse)
    {
        //椭圆周长上取若干点连成折线
        const int kSegments = 36;
        const double kTwoPi = 6.283185307179586;
        const double cx = (static_cast<double>(s.m_points[0].m_x) + s.m_points[1].m_x) / 2;
        const double cy = (static_cast<double>(s.m_points[0].m_y) + s.m_points[1].m_y) / 2;
        const double rx = (static_cast<double>(s.m_points[1].m_x) - s.m_points[0].m_x) / 2;
        const double ry = (static_cast<double>(s.m_points[1].m_y) - s.m_points[0].m_y) / 2;
        int64_t best = kFar;
        AnnotationPoint prev(static_cast<uint16_t>(cx + rx), static_cast<uint16_t>(cy));
        for (int i = 1; i <= kSegments; ++i)
        {
            const double t = kTwoPi * i / kSegments;
            const AnnotationPoint cur(static_cast<uint16_t>(cx + rx * cos(t)), static_cast<uint16_t>(cy + ry * sin(t)));
            const int64_t d = distSqToSegment(x, y, prev, cur);
            if (d < best)
                best = d;
            prev = cur;
        }
        return best;
    }
    int64_t best = kFar;
    for (size_t i = 1; i < s.m_points.size(); ++i)
    {
        const int64_t d = distSqToSegment(x, y, s.m_points[i - 1], s.m_points[i]);
        if (d < best)
            best = d;
    }
    return best;
}

bool samePoints(const std::vector<AnnotationPoint>& a, const std::vector<AnnotationPoint>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].m_x != b[i].m_x || a[i].m_y != b[i].m_y)
            return false;
    }
    return true;
}

//若干点的外接矩形的中心（第 20 期，连接线脱开时停的位置）；没有点时为原点
AnnotationPoint pointsCenter(const std::vector<AnnotationPoint>& pts)
{
    if (pts.empty())
        return AnnotationPoint();
    int minX = pts[0].m_x;
    int minY = pts[0].m_y;
    int maxX = minX;
    int maxY = minY;
    for (size_t i = 1; i < pts.size(); ++i)
    {
        minX = (std::min)(minX, static_cast<int>(pts[i].m_x));
        minY = (std::min)(minY, static_cast<int>(pts[i].m_y));
        maxX = (std::max)(maxX, static_cast<int>(pts[i].m_x));
        maxY = (std::max)(maxY, static_cast<int>(pts[i].m_y));
    }
    return AnnotationPoint(static_cast<uint16_t>((minX + maxX) / 2), static_cast<uint16_t>((minY + maxY) / 2));
}

} // namespace

AnnotationPoint::AnnotationPoint()
    : m_x(0)
    , m_y(0)
{
}

AnnotationPoint::AnnotationPoint(uint16_t x, uint16_t y)
    : m_x(x)
    , m_y(y)
{
}

AnnotationStyle::AnnotationStyle()
    : m_lineStyle(annot_line_solid)
    , m_startCap(annot_cap_none)
    , m_endCap(annot_cap_none)
    , m_routing(annot_route_straight)
    , m_fill(annot_fill_none)
    , m_rotation(0)
    , m_variant(0)
    , m_sequence(annot_seq_digit)
    , m_number(0)
    , m_bold(false)
    , m_textBg(annot_text_bg_none)
    , m_bgColor(0)
    , m_canvas(kAnnotDefaultCanvas)
{
}

bool AnnotationStyle::operator==(const AnnotationStyle& rhs) const
{
    return m_lineStyle == rhs.m_lineStyle && m_startCap == rhs.m_startCap && m_endCap == rhs.m_endCap &&
           m_routing == rhs.m_routing && m_fill == rhs.m_fill && m_rotation == rhs.m_rotation &&
           m_variant == rhs.m_variant && m_sequence == rhs.m_sequence && m_number == rhs.m_number &&
           m_bold == rhs.m_bold && m_textBg == rhs.m_textBg && m_bgColor == rhs.m_bgColor &&
           m_attachStart == rhs.m_attachStart && m_attachEnd == rhs.m_attachEnd && m_canvas == rhs.m_canvas;
}

AnnotationStroke::AnnotationStroke()
    : m_author(0)
    , m_id(0)
    , m_tool(0)
    , m_color(0)
    , m_width(0)
    , m_finished(false)
    , m_finishedMs(0)
{
}

AnnotationPointer::AnnotationPointer()
    : m_seat(0)
    , m_tool(0)
    , m_color(0)
    , m_updatedMs(0)
    , m_canvas(kAnnotDefaultCanvas)
{
}

AnnotationBoardView::AnnotationBoardView()
    : m_background(0)
    , m_left(0)
    , m_top(0)
    , m_width(kAnnotCoordMax)
    , m_senderSeat(0)
    , m_updatedMs(0)
{
}

AnnotationMessage::AnnotationMessage()
    : m_op(0)
    , m_author(0)
    , m_id(0)
    , m_tool(0)
    , m_color(0)
    , m_width(0)
    , m_finished(false)
    , m_hasStyle(false)
    , m_dx(0)
    , m_dy(0)
    , m_canvas(kAnnotDefaultCanvas)
{
}

AnnotationRef::AnnotationRef()
    : m_author(kAnnotNoAttachSeat)
    , m_id(0)
{
}

AnnotationRef::AnnotationRef(int author, uint32_t id)
    : m_author(author)
    , m_id(id)
{
}

bool AnnotationRef::IsSet() const
{
    return m_author != kAnnotNoAttachSeat;
}

bool AnnotationRef::operator==(const AnnotationRef& rhs) const
{
    return m_author == rhs.m_author && m_id == rhs.m_id;
}

bool AnnotationRef::operator<(const AnnotationRef& rhs) const
{
    if (m_author != rhs.m_author)
        return m_author < rhs.m_author;
    return m_id < rhs.m_id;
}

int AnnotLaserAlpha(const AnnotationStroke& s, int64_t nowMs)
{
    const int kOpaque = 255;
    if (s.m_tool != annot_tool_laser || !s.m_finished)
        return kOpaque;
    const int64_t elapsed = nowMs - s.m_finishedMs;
    if (elapsed <= 0)
        return kOpaque;
    if (elapsed >= kAnnotLaserFadeMs)
        return 0;
    return static_cast<int>(kOpaque - elapsed * kOpaque / kAnnotLaserFadeMs);
}

//==================== ShareAnnotationCodec ====================

std::string ShareAnnotationCodec::encode(const AnnotationMessage& m)
{
    Writer w;
    w.u8(static_cast<uint32_t>(m.m_op));
    switch (m.m_op)
    {
    //开始一笔：编号、种类、颜色、线宽、点，最后是样式块（第 19 期）
    case annot_op_begin:
        w.u32(m.m_id);
        w.u8(static_cast<uint32_t>(m.m_tool));
        w.u24(m.m_color);
        w.u8(clampWidth(m.m_width));
        w.points(m.m_points, cappedPoints(m.m_points));
        w.style(m.m_style);
        break;
    //追加点：编号、点
    case annot_op_append:
        w.u32(m.m_id);
        w.points(m.m_points, cappedPoints(m.m_points));
        break;
    //画完：编号
    case annot_op_end:
        w.u32(m.m_id);
        break;
    //文字：编号、颜色、字号、左上角、文字
    case annot_op_text:
        {
            w.u32(m.m_id);
            w.u24(m.m_color);
            w.u8(clampWidth(m.m_width));
            const AnnotationPoint p = m.m_points.empty() ? AnnotationPoint() : m.m_points[0];
            w.u16(p.m_x);
            w.u16(p.m_y);
            const size_t n = cappedText(m.m_text);
            w.u16(static_cast<uint32_t>(n));
            w.bytes(m.m_text, n);
            w.style(m.m_style);
        }
        break;
    //擦除：作者座位号、编号
    case annot_op_erase:
        w.u8(static_cast<uint32_t>(m.m_author));
        w.u32(m.m_id);
        break;
    //清空：画面座位（第 22 期；全部画面时不写，与老版本相同）
    case annot_op_clear:
        if (m.m_canvas >= 0)
            w.u8(static_cast<uint32_t>(m.m_canvas));
        break;
    //整笔补发：作者、编号、种类、颜色、线宽、是否画完、点、文字
    case annot_op_full:
        {
            w.u8(static_cast<uint32_t>(m.m_author));
            w.u32(m.m_id);
            w.u8(static_cast<uint32_t>(m.m_tool));
            w.u24(m.m_color);
            w.u8(clampWidth(m.m_width));
            w.u8(m.m_finished ? 1u : 0u);
            w.points(m.m_points, cappedPoints(m.m_points));
            const size_t n = cappedText(m.m_text);
            w.u16(static_cast<uint32_t>(n));
            w.bytes(m.m_text, n);
            w.style(m.m_style);
        }
        break;
    //画笔位置（第 19 期）：位置、工具、颜色
    case annot_op_pointer:
        {
            const AnnotationPoint p = m.m_points.empty() ? AnnotationPoint() : m.m_points[0];
            w.u16(p.m_x);
            w.u16(p.m_y);
            w.u8(static_cast<uint32_t>(m.m_tool));
            w.u24(m.m_color);
            //第 22 期：光标所在的画面
            w.u8(static_cast<uint32_t>(m.m_canvas));
        }
        break;
    //画笔离开（第 19 期）：没有其它字段
    case annot_op_pointer_leave:
        break;
    //修改（第 20 期）：作者、编号、颜色、线宽、点、文字、样式块
    case annot_op_update:
        {
            w.u8(static_cast<uint32_t>(m.m_author));
            w.u32(m.m_id);
            w.u24(m.m_color);
            w.u8(clampWidth(m.m_width));
            w.points(m.m_points, cappedPoints(m.m_points));
            const size_t n = cappedText(m.m_text);
            w.u16(static_cast<uint32_t>(n));
            w.bytes(m.m_text, n);
            w.style(m.m_style);
        }
        break;
    //移动（第 20 期）：对象个数、各对象、横向与纵向位移（有符号）；个数为 0 或超过上限时不编码
    case annot_op_move:
        {
            if (m.m_targets.empty() || m.m_targets.size() > static_cast<size_t>(kAnnotMaxMoveTargets))
                return std::string();
            w.u8(static_cast<uint32_t>(m.m_targets.size()));
            for (size_t i = 0; i < m.m_targets.size(); ++i)
                w.ref(m.m_targets[i]);
            w.u16(static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(clampDelta(m.m_dx)))));
            w.u16(static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(clampDelta(m.m_dy)))));
        }
        break;
    //白板视野（第 23 期）：画面座位、背景、左、上、宽；宽为 0 时不编码
    case annot_op_board:
        if (m.m_board.m_width <= 0)
            return std::string();
        w.u8(static_cast<uint32_t>(m.m_canvas));
        w.u8(static_cast<uint32_t>(m.m_board.m_background));
        w.u16(static_cast<uint32_t>(m.m_board.m_left));
        w.u16(static_cast<uint32_t>(m.m_board.m_top));
        w.u16(static_cast<uint32_t>(m.m_board.m_width));
        break;
    //未知操作：不编码
    default:
        return std::string();
    }
    return w.m_out;
}

bool ShareAnnotationCodec::decode(const std::string& payload, AnnotationMessage& m)
{
    m = AnnotationMessage();
    Reader r(payload);
    m.m_op = static_cast<int>(r.u8());
    if (!r.ok())
        return false;
    switch (m.m_op)
    {
    //开始一笔
    case annot_op_begin:
        m.m_id = r.u32();
        m.m_tool = static_cast<int>(r.u8());
        m.m_color = r.u24();
        m.m_width = static_cast<int>(r.u8());
        if (!r.points(m.m_points) || !validTool(m.m_tool) || m.m_tool == annot_tool_text)
            return false;
        if (!readOptionalStyle(r, m))
            return false;
        break;
    //追加点
    case annot_op_append:
        m.m_id = r.u32();
        if (!r.points(m.m_points))
            return false;
        break;
    //画完
    case annot_op_end:
        m.m_id = r.u32();
        break;
    //文字
    case annot_op_text:
        {
            m.m_id = r.u32();
            m.m_tool = annot_tool_text;
            m.m_color = r.u24();
            m.m_width = static_cast<int>(r.u8());
            const uint16_t x = static_cast<uint16_t>(r.u16());
            const uint16_t y = static_cast<uint16_t>(r.u16());
            m.m_points.push_back(AnnotationPoint(x, y));
            if (!r.text(m.m_text))
                return false;
            if (!readOptionalStyle(r, m))
                return false;
        }
        break;
    //擦除
    case annot_op_erase:
        m.m_author = static_cast<int>(r.u8());
        m.m_id = r.u32();
        break;
    //清空：带画面座位（第 22 期）只清那一块，不带（老版本）清全部
    case annot_op_clear:
        m.m_canvas = r.done() ? kAnnotAllCanvases : static_cast<int>(r.u8());
        break;
    //整笔补发
    case annot_op_full:
        m.m_author = static_cast<int>(r.u8());
        m.m_id = r.u32();
        m.m_tool = static_cast<int>(r.u8());
        m.m_color = r.u24();
        m.m_width = static_cast<int>(r.u8());
        m.m_finished = (r.u8() != 0);
        if (!r.points(m.m_points) || !r.text(m.m_text) || !validTool(m.m_tool))
            return false;
        if (!readOptionalStyle(r, m))
            return false;
        break;
    //画笔位置（第 19 期）
    case annot_op_pointer:
        {
            const uint16_t x = static_cast<uint16_t>(r.u16());
            const uint16_t y = static_cast<uint16_t>(r.u16());
            m.m_points.push_back(AnnotationPoint(x, y));
            m.m_tool = static_cast<int>(r.u8());
            m.m_color = r.u24();
            //第 22 期的画面座位；老版本没有，按默认画面
            m.m_canvas = r.done() ? kAnnotDefaultCanvas : static_cast<int>(r.u8());
        }
        break;
    //画笔离开（第 19 期）
    case annot_op_pointer_leave:
        break;
    //修改（第 20 期）
    case annot_op_update:
        m.m_author = static_cast<int>(r.u8());
        m.m_id = r.u32();
        m.m_color = r.u24();
        m.m_width = static_cast<int>(r.u8());
        if (!r.points(m.m_points) || !r.text(m.m_text))
            return false;
        if (!readOptionalStyle(r, m))
            return false;
        break;
    //移动（第 20 期）
    case annot_op_move:
        {
            const int count = static_cast<int>(r.u8());
            if (!r.ok() || count < 1 || count > kAnnotMaxMoveTargets)
                return false;
            for (int i = 0; i < count; ++i)
                m.m_targets.push_back(r.ref());
            m.m_dx = static_cast<int16_t>(static_cast<uint16_t>(r.u16()));
            m.m_dy = static_cast<int16_t>(static_cast<uint16_t>(r.u16()));
        }
        break;
    //白板视野（第 23 期）：宽为 0 的不合法
    case annot_op_board:
        m.m_canvas = static_cast<int>(r.u8());
        m.m_board.m_background = static_cast<int>(r.u8());
        m.m_board.m_left = static_cast<int>(r.u16());
        m.m_board.m_top = static_cast<int>(r.u16());
        m.m_board.m_width = static_cast<int>(r.u16());
        if (!r.ok() || m.m_board.m_width <= 0)
            return false;
        break;
    //未知操作
    default:
        return false;
    }
    return r.done();
}

//==================== ShareAnnotationModel ====================

ShareAnnotationModel::ShareAnnotationModel()
    : m_viewersAllowed(true)
    , m_version(0)
    , m_hostSeat(kAnnotHostSeat)
{
}

AnnotationStroke* ShareAnnotationModel::find(int author, uint32_t id)
{
    for (size_t i = 0; i < m_strokes.size(); ++i)
    {
        if (m_strokes[i].m_author == author && m_strokes[i].m_id == id)
            return &m_strokes[i];
    }
    return NULL;
}

void ShareAnnotationModel::touch()
{
    ++m_version;
}

bool ShareAnnotationModel::Apply(int senderSeat, const AnnotationMessage& m)
{
    return Apply(senderSeat, m, 0);
}

bool ShareAnnotationModel::ExpireTransient(int64_t nowMs)
{
    bool changed = false;
    //淡完的激光笔
    for (size_t i = m_strokes.size(); i > 0; --i)
    {
        const AnnotationStroke& s = m_strokes[i - 1];
        if (s.m_tool == annot_tool_laser && s.m_finished && nowMs - s.m_finishedMs >= kAnnotLaserFadeMs)
        {
            m_strokes.erase(m_strokes.begin() + (i - 1));
            changed = true;
        }
    }
    if (changed)
        touch();
    //太久没有更新的画笔位置
    for (size_t i = m_pointers.size(); i > 0; --i)
    {
        if (nowMs - m_pointers[i - 1].m_updatedMs >= kAnnotPointerStaleMs)
        {
            m_pointers.erase(m_pointers.begin() + (i - 1));
            changed = true;
        }
    }
    return changed;
}

std::vector<AnnotationPointer> ShareAnnotationModel::Pointers() const
{
    return m_pointers;
}

bool ShareAnnotationModel::Apply(int senderSeat, const AnnotationMessage& m, int64_t nowMs)
{
    //主持人：默认为发起人（0 号座位），移交后为新主持人的座位（2026-10-07，方案第 14.3 节第 1 条）
    const bool fromHost = (senderSeat == m_hostSeat);
    //清空与整笔补发只接受发起人的；其余操作观看者要有权限
    if ((m.m_op == annot_op_clear || m.m_op == annot_op_full) && !fromHost)
        return false;
    //观看者没有权限时拒绝；白板建立者在自己的白板上不受限（2026-10-07，方案第 14.1 节 ④-1）
    if (!fromHost && !m_viewersAllowed && !onOwnBoardOnly(senderSeat, m))
        return false;
    switch (m.m_op)
    {
    //开始一笔：作者取发送者座位号，同一笔重复的 begin 不生效
    case annot_op_begin:
        {
            if (find(senderSeat, m.m_id) != NULL || !validTool(m.m_tool) || m.m_points.empty())
                return false;
            AnnotationStroke s;
            s.m_author = senderSeat;
            s.m_id = m.m_id;
            s.m_tool = m.m_tool;
            s.m_color = m.m_color;
            s.m_width = m.m_width;
            s.m_points.assign(m.m_points.begin(), m.m_points.begin() + cappedPoints(m.m_points));
            s.m_style = m.m_style;
            m_strokes.push_back(s);
            touch();
            return true;
        }
    //追加点：只能给自己正在画的那一笔追加，超过点数上限的部分丢弃
    case annot_op_append:
        {
            AnnotationStroke* s = find(senderSeat, m.m_id);
            if (s == NULL || s->m_finished || m.m_points.empty())
                return false;
            const size_t room = static_cast<size_t>(kAnnotMaxStrokePoints) - s->m_points.size();
            const size_t n = m.m_points.size() < room ? m.m_points.size() : room;
            if (n == 0)
                return false;
            s->m_points.insert(s->m_points.end(), m.m_points.begin(), m.m_points.begin() + n);
            touch();
            return true;
        }
    //画完
    case annot_op_end:
        {
            AnnotationStroke* s = find(senderSeat, m.m_id);
            if (s == NULL || s->m_finished)
                return false;
            s->m_finished = true;
            //记下画完的时刻：激光笔据此淡出
            s->m_finishedMs = nowMs;
            touch();
            return true;
        }
    //文字：一次完成
    case annot_op_text:
        {
            if (find(senderSeat, m.m_id) != NULL || m.m_text.empty() || m.m_points.empty())
                return false;
            AnnotationStroke s;
            s.m_author = senderSeat;
            s.m_id = m.m_id;
            s.m_tool = annot_tool_text;
            s.m_color = m.m_color;
            s.m_width = m.m_width;
            s.m_points.push_back(m.m_points[0]);
            s.m_text = m.m_text.substr(0, cappedText(m.m_text));
            s.m_finished = true;
            s.m_finishedMs = nowMs;
            s.m_style = m.m_style;
            m_strokes.push_back(s);
            touch();
            return true;
        }
    //擦除指定作者的指定一笔；吸附在它上面的连接线先脱开（第 20 期）
    case annot_op_erase:
        for (size_t i = 0; i < m_strokes.size(); ++i)
        {
            if (m_strokes[i].m_author == m.m_author && m_strokes[i].m_id == m.m_id)
            {
                detachConnectorsFrom(m_strokes[i]);
                m_strokes.erase(m_strokes.begin() + i);
                touch();
                return true;
            }
        }
        return false;
    //清空（已确认来自发起人）：带画面座位时只清那一块画面（第 22 期）
    case annot_op_clear:
        if (m.m_canvas != kAnnotAllCanvases)
            return ClearCanvas(m.m_canvas);
        if (m_strokes.empty())
            return false;
        m_strokes.clear();
        touch();
        return true;
    //整笔补发（已确认来自发起人）：已有则覆盖、没有则添加，内容相同不算变化
    case annot_op_full:
        {
            if (!validTool(m.m_tool))
                return false;
            AnnotationStroke s;
            s.m_author = m.m_author;
            s.m_id = m.m_id;
            s.m_tool = m.m_tool;
            s.m_color = m.m_color;
            s.m_width = m.m_width;
            s.m_points.assign(m.m_points.begin(), m.m_points.begin() + cappedPoints(m.m_points));
            s.m_text = m.m_text.substr(0, cappedText(m.m_text));
            s.m_finished = m.m_finished;
            s.m_finishedMs = nowMs;
            s.m_style = m.m_style;
            AnnotationStroke* old = find(m.m_author, m.m_id);
            if (old != NULL)
            {
                if (old->m_tool == s.m_tool && old->m_color == s.m_color && old->m_width == s.m_width &&
                    old->m_text == s.m_text && old->m_finished == s.m_finished &&
                    samePoints(old->m_points, s.m_points) && old->m_style == s.m_style)
                    return false;
                //已画完的保留原来的画完时刻（激光笔不进补发，这里只是保持一致）
                if (old->m_finished && s.m_finished)
                    s.m_finishedMs = old->m_finishedMs;
                *old = s;
            }
            else
            {
                m_strokes.push_back(s);
            }
            touch();
            return true;
        }
    //修改一个对象（第 20 期）：有标注权限的人都能改任何人的
    case annot_op_update:
        return applyUpdate(m);
    //移动若干对象（第 20 期）
    case annot_op_move:
        return applyMove(m);
    //画笔位置（第 19 期）：每个座位只留最新的一条，按座位号排序；不算笔迹变化，不改版本号
    case annot_op_pointer:
        {
            if (m.m_points.empty())
                return false;
            AnnotationPointer p;
            p.m_seat = senderSeat;
            p.m_pt = m.m_points[0];
            p.m_tool = m.m_tool;
            p.m_color = m.m_color;
            p.m_updatedMs = nowMs;
            p.m_canvas = m.m_canvas;
            size_t i = 0;
            while (i < m_pointers.size() && m_pointers[i].m_seat < senderSeat)
                ++i;
            if (i < m_pointers.size() && m_pointers[i].m_seat == senderSeat)
                m_pointers[i] = p;
            else
                m_pointers.insert(m_pointers.begin() + i, p);
            return true;
        }
    //白板视野（第 23 期）：每块白板只留最新的一条，记下发送者与收到的时刻；不进笔迹，版本号照样变（界面据此重画背景）
    case annot_op_board:
        {
            AnnotationBoardView v = m.m_board;
            v.m_senderSeat = senderSeat;
            v.m_updatedMs = nowMs;
            m_boards[m.m_canvas] = v;
            touch();
            return true;
        }
    //画笔离开（第 19 期）
    case annot_op_pointer_leave:
        for (size_t i = 0; i < m_pointers.size(); ++i)
        {
            if (m_pointers[i].m_seat == senderSeat)
            {
                m_pointers.erase(m_pointers.begin() + i);
                return true;
            }
        }
        return false;
    //未知操作
    default:
        return false;
    }
}

void ShareAnnotationModel::SetViewersAllowed(bool allowed)
{
    m_viewersAllowed = allowed;
}

bool ShareAnnotationModel::ViewersAllowed() const
{
    return m_viewersAllowed;
}

void ShareAnnotationModel::SetBoardOwners(const std::map<int, int>& owners)
{
    m_boardOwners = owners;
}

void ShareAnnotationModel::SetHostSeat(int seat)
{
    m_hostSeat = seat;
}

int ShareAnnotationModel::HostSeat() const
{
    return m_hostSeat;
}

int ShareAnnotationModel::BoardOwnerOf(int canvas) const
{
    std::map<int, int>::const_iterator it = m_boardOwners.find(canvas);
    return (it == m_boardOwners.end()) ? -1 : it->second;
}

bool ShareAnnotationModel::onOwnBoardOnly(int senderSeat, const AnnotationMessage& m)
{
    if (m_boardOwners.empty())
        return false;
    switch (m.m_op)
    {
    //开始一笔、添加文字：画面在样式里
    case annot_op_begin:
    case annot_op_text:
        return BoardOwnerOf(m.m_style.m_canvas) == senderSeat;

    //追加点、画完：发送者自己正在画的那一笔所在的画面
    case annot_op_append:
    case annot_op_end:
        {
            const AnnotationStroke* s = find(senderSeat, m.m_id);
            return s != NULL && BoardOwnerOf(s->m_style.m_canvas) == senderSeat;
        }

    //擦除、修改：被操作的那一笔所在的画面
    case annot_op_erase:
    case annot_op_update:
        {
            const AnnotationStroke* s = find(m.m_author, m.m_id);
            return s != NULL && BoardOwnerOf(s->m_style.m_canvas) == senderSeat;
        }

    //移动：每个对象都要在发送者自己的白板上
    case annot_op_move:
        {
            if (m.m_targets.empty())
                return false;
            for (size_t i = 0; i < m.m_targets.size(); ++i)
            {
                const AnnotationStroke* s = find(m.m_targets[i].m_author, m.m_targets[i].m_id);
                if (s == NULL || BoardOwnerOf(s->m_style.m_canvas) != senderSeat)
                    return false;
            }
            return true;
        }

    //画笔位置、白板视野：画面在消息里
    case annot_op_pointer:
    case annot_op_board:
        return BoardOwnerOf(m.m_canvas) == senderSeat;

    //画笔离开：只撤掉发送者自己的画笔位置，不改笔迹
    case annot_op_pointer_leave:
        return true;

    //清空、整笔补发只认发起人（前面已拒绝），其余未知操作一律不放行
    default:
        return false;
    }
}

void ShareAnnotationModel::Clear()
{
    m_boards.clear();
    if (m_strokes.empty())
        return;
    m_strokes.clear();
    touch();
}

const std::vector<AnnotationStroke>& ShareAnnotationModel::Strokes() const
{
    return m_strokes;
}

bool ShareAnnotationModel::LastStrokeOf(int author, uint32_t& id) const
{
    for (size_t i = m_strokes.size(); i > 0; --i)
    {
        if (m_strokes[i - 1].m_author == author)
        {
            id = m_strokes[i - 1].m_id;
            return true;
        }
    }
    return false;
}

bool ShareAnnotationModel::LastStrokeOf(int author, int canvas, uint32_t& id) const
{
    for (size_t i = m_strokes.size(); i > 0; --i)
    {
        if (m_strokes[i - 1].m_author == author && m_strokes[i - 1].m_style.m_canvas == canvas)
        {
            id = m_strokes[i - 1].m_id;
            return true;
        }
    }
    return false;
}

bool ShareAnnotationModel::ClearCanvas(int canvas)
{
    //这块画面的白板视野一并清掉（第 23 期）
    const bool hadBoard = (m_boards.erase(canvas) > 0);
    if (hadBoard)
        touch();
    const size_t before = m_strokes.size();
    std::vector<AnnotationStroke> kept;
    for (size_t i = 0; i < m_strokes.size(); ++i)
    {
        if (m_strokes[i].m_style.m_canvas != canvas)
            kept.push_back(m_strokes[i]);
    }
    if (kept.size() == before)
        return hadBoard;
    m_strokes.swap(kept);
    touch();
    return true;
}

bool ShareAnnotationModel::BoardView(int canvas, AnnotationBoardView& out) const
{
    std::map<int, AnnotationBoardView>::const_iterator it = m_boards.find(canvas);
    if (it == m_boards.end())
        return false;
    out = it->second;
    return true;
}

std::vector<AnnotationStroke> AnnotStrokesOnCanvas(const std::vector<AnnotationStroke>& all, int canvas)
{
    std::vector<AnnotationStroke> out;
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].m_style.m_canvas == canvas)
            out.push_back(all[i]);
    }
    return out;
}

std::vector<AnnotationPointer> AnnotPointersOnCanvas(const std::vector<AnnotationPointer>& all, int canvas)
{
    std::vector<AnnotationPointer> out;
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].m_canvas == canvas)
            out.push_back(all[i]);
    }
    return out;
}

std::vector<AnnotationMessage> ShareAnnotationModel::Snapshot() const
{
    std::vector<AnnotationMessage> out;
    for (size_t i = 0; i < m_strokes.size(); ++i)
    {
        const AnnotationStroke& s = m_strokes[i];
        //激光笔转瞬即逝，后加入的人不需要
        if (s.m_tool == annot_tool_laser)
            continue;
        AnnotationMessage m;
        m.m_op = annot_op_full;
        m.m_author = s.m_author;
        m.m_id = s.m_id;
        m.m_tool = s.m_tool;
        m.m_color = s.m_color;
        m.m_width = s.m_width;
        m.m_points = s.m_points;
        m.m_text = s.m_text;
        m.m_finished = s.m_finished;
        m.m_style = s.m_style;
        out.push_back(m);
    }
    return out;
}

bool ShareAnnotationModel::HitTest(int x, int y, int tolerance, int& author, uint32_t& id) const
{
    const int64_t limit = static_cast<int64_t>(tolerance) * tolerance;
    //从最上面（最后添加）的一笔往下找
    for (size_t i = m_strokes.size(); i > 0; --i)
    {
        const AnnotationStroke& s = m_strokes[i - 1];
        if (distSqToStroke(x, y, s) <= limit)
        {
            author = s.m_author;
            id = s.m_id;
            return true;
        }
    }
    return false;
}

uint64_t ShareAnnotationModel::Version() const
{
    return m_version;
}

const AnnotationStroke* ShareAnnotationModel::Find(const AnnotationRef& ref) const
{
    for (size_t i = 0; i < m_strokes.size(); ++i)
    {
        if (m_strokes[i].m_author == ref.m_author && m_strokes[i].m_id == ref.m_id)
            return &m_strokes[i];
    }
    return NULL;
}

void ShareAnnotationModel::detachConnectorsFrom(const AnnotationStroke& target)
{
    const AnnotationRef ref(target.m_author, target.m_id);
    const AnnotationPoint center = pointsCenter(target.m_points);
    for (size_t i = 0; i < m_strokes.size(); ++i)
    {
        AnnotationStroke& c = m_strokes[i];
        if (c.m_tool != annot_tool_connector || c.m_points.empty())
            continue;
        if (c.m_style.m_attachStart == ref)
        {
            c.m_style.m_attachStart = AnnotationRef();
            c.m_points.front() = center;
        }
        if (c.m_style.m_attachEnd == ref)
        {
            c.m_style.m_attachEnd = AnnotationRef();
            c.m_points.back() = center;
        }
    }
}

bool ShareAnnotationModel::applyMove(const AnnotationMessage& m)
{
    if (m.m_dx == 0 && m.m_dy == 0)
        return false;
    bool changed = false;
    for (size_t t = 0; t < m.m_targets.size(); ++t)
    {
        AnnotationStroke* s = find(m.m_targets[t].m_author, m.m_targets[t].m_id);
        if (s == NULL || !s->m_finished || s->m_tool == annot_tool_laser || s->m_points.empty())
            continue;
        //位移限制在画面以内：每个对象按自己的点的范围各自限制
        int minX = kAnnotCoordMax;
        int minY = kAnnotCoordMax;
        int maxX = 0;
        int maxY = 0;
        for (size_t i = 0; i < s->m_points.size(); ++i)
        {
            minX = (std::min)(minX, static_cast<int>(s->m_points[i].m_x));
            minY = (std::min)(minY, static_cast<int>(s->m_points[i].m_y));
            maxX = (std::max)(maxX, static_cast<int>(s->m_points[i].m_x));
            maxY = (std::max)(maxY, static_cast<int>(s->m_points[i].m_y));
        }
        const int dx = (std::max)(-minX, (std::min)(m.m_dx, kAnnotCoordMax - maxX));
        const int dy = (std::max)(-minY, (std::min)(m.m_dy, kAnnotCoordMax - maxY));
        if (dx == 0 && dy == 0)
            continue;
        for (size_t i = 0; i < s->m_points.size(); ++i)
            s->m_points[i] = AnnotationPoint(static_cast<uint16_t>(s->m_points[i].m_x + dx),
                                             static_cast<uint16_t>(s->m_points[i].m_y + dy));
        //连接线：吸附的对象不在这一批里的那一端脱开，停在那个对象的中心加上位移（限制在画面以内）
        if (s->m_tool == annot_tool_connector)
        {
            detachMovedEnd(*s, s->m_style.m_attachStart, true, m.m_targets, dx, dy);
            detachMovedEnd(*s, s->m_style.m_attachEnd, false, m.m_targets, dx, dy);
        }
        changed = true;
    }
    if (changed)
        touch();
    return changed;
}

void ShareAnnotationModel::detachMovedEnd(AnnotationStroke& connector, AnnotationRef& attach, bool start,
                                          const std::vector<AnnotationRef>& batch, int dx, int dy)
{
    if (!attach.IsSet())
        return;
    for (size_t i = 0; i < batch.size(); ++i)
    {
        if (batch[i] == attach)
            return;
    }
    const AnnotationStroke* target = Find(attach);
    attach = AnnotationRef();
    if (target == NULL)
        return;
    const AnnotationPoint c = pointsCenter(target->m_points);
    const int x = (std::max)(0, (std::min)(kAnnotCoordMax, static_cast<int>(c.m_x) + dx));
    const int y = (std::max)(0, (std::min)(kAnnotCoordMax, static_cast<int>(c.m_y) + dy));
    AnnotationPoint& end = start ? connector.m_points.front() : connector.m_points.back();
    end = AnnotationPoint(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
}

bool ShareAnnotationModel::applyUpdate(const AnnotationMessage& m)
{
    AnnotationStroke* s = find(m.m_author, m.m_id);
    if (s == NULL || !s->m_finished || s->m_tool == annot_tool_laser || m.m_points.empty())
        return false;
    if (s->m_tool == annot_tool_text && m.m_text.empty())
        return false;
    AnnotationStroke next = *s;
    next.m_color = m.m_color;
    next.m_width = m.m_width;
    //文字只有一个点（左上角）
    const size_t n = (s->m_tool == annot_tool_text) ? 1 : cappedPoints(m.m_points);
    next.m_points.assign(m.m_points.begin(), m.m_points.begin() + n);
    next.m_text = m.m_text.substr(0, cappedText(m.m_text));
    next.m_style = m.m_style;
    if (next.m_color == s->m_color && next.m_width == s->m_width && next.m_text == s->m_text &&
        samePoints(next.m_points, s->m_points) && next.m_style == s->m_style)
        return false;
    *s = next;
    touch();
    return true;
}

} // namespace screenshare
