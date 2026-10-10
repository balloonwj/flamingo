/**
 *  屏幕共享标注会话的选择工具（第 20 期，docs/屏幕共享与录制方案.md 第 2 节第 37 条），ShareAnnotationSession 的一部分，
 *  声明见 ShareAnnotationSession.h。
 *
 *  选择工具的操作：点中对象选中它（按住 Shift 加选或取消）、拖动选中的对象移动（每 kAnnotMoveIntervalMs 发一次移动）、
 *  单选时拖句柄缩放或拖直线类对象的一端（拖动中只改本地，松开时发一次修改；连接线的一端落在图元或形状上就吸附过去）、
 *  在空白处拖出选择框框选；双击改文字、工具条改样式、删除选中的对象由界面调用对应的方法。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "ShareAnnotationSession.h"

#include <stdlib.h>
#include <algorithm>

namespace screenshare {

namespace {

//号码的取值范围（两字节）
const long kMinBadgeNumber = 1;
const long kMaxBadgeNumber = 65535;
//十进制
const int kDecimalBase = 10;
//0x00RRGGBB 的有效位
const uint32_t kRgbMask = 0x00FFFFFF;

//选择里是否有这个对象
bool containsRef(const std::vector<AnnotationRef>& refs, const AnnotationRef& r)
{
    return std::find(refs.begin(), refs.end(), r) != refs.end();
}

//两个点张成的矩形
AnnotRect bandRect(const AnnotationPoint& a, const AnnotationPoint& b)
{
    return AnnotRect((std::min)(a.m_x, b.m_x), (std::min)(a.m_y, b.m_y), (std::max)(a.m_x, b.m_x),
                     (std::max)(a.m_y, b.m_y));
}

//是否用得上线型的种类：直线、折线、曲线、箭头、连接线与封闭形状
bool usesLineStyle(int tool)
{
    return tool == annot_tool_line || tool == annot_tool_polyline || tool == annot_tool_curve ||
           tool == annot_tool_arrow || tool == annot_tool_connector || AnnotIsClosedShape(tool);
}

//是否用得上两端样式的种类：直线、折线、曲线、箭头、连接线
bool usesCaps(int tool)
{
    return tool == annot_tool_line || tool == annot_tool_polyline || tool == annot_tool_curve ||
           tool == annot_tool_arrow || tool == annot_tool_connector;
}

} // namespace

AnnotSelectionVisual::AnnotSelectionVisual()
    : m_endpointHandles(false)
    , m_hasBand(false)
{
}

void ShareAnnotationSession::SetContentAspect(double aspect)
{
    if (aspect > 0)
        m_aspect = aspect;
}

double ShareAnnotationSession::ContentAspect() const
{
    return m_aspect;
}

const std::vector<AnnotationRef>& ShareAnnotationSession::Selection() const
{
    return m_selection;
}

void ShareAnnotationSession::ClearSelection()
{
    m_selection.clear();
}

bool ShareAnnotationSession::DeleteSelection()
{
    if (!m_running || !CanDraw())
        return false;
    bool any = false;
    const std::vector<AnnotationRef> refs = m_selection;
    m_selection.clear();
    for (size_t i = 0; i < refs.size(); ++i)
    {
        AnnotationMessage m;
        m.m_op = annot_op_erase;
        m.m_author = refs[i].m_author;
        m.m_id = refs[i].m_id;
        if (m_model.Apply(m_selfSeat, m))
        {
            send(m);
            any = true;
        }
    }
    return any;
}

AnnotSelectionVisual ShareAnnotationSession::SelectionVisual() const
{
    AnnotSelectionVisual v;
    const std::vector<AnnotationStroke> all = canvasStrokes();
    for (size_t i = 0; i < m_selection.size(); ++i)
    {
        const AnnotationStroke* s = m_model.Find(m_selection[i]);
        if (s != NULL)
            v.m_boxes.push_back(AnnotObjectBounds(*s, all, m_aspect));
    }
    //单选时画句柄
    if (m_selection.size() == 1)
    {
        const AnnotationStroke* s = m_model.Find(m_selection[0]);
        if (s != NULL)
        {
            v.m_handles = AnnotHandlePositions(*s, all, m_aspect);
            v.m_endpointHandles = AnnotIsEndpointObject(*s);
        }
    }
    if (m_selectDrag == select_drag_band)
    {
        v.m_hasBand = true;
        v.m_band = bandRect(m_dragOrigin, m_bandTo);
    }
    return v;
}

bool ShareAnnotationSession::ObjectAt(const AnnotationPoint& pt, int tolerance) const
{
    const std::vector<AnnotationStroke> all = canvasStrokes();
    //单选对象的句柄：拖它缩放或拖一端，同样显示移动光标
    if (m_selection.size() == 1)
    {
        const AnnotationStroke* s = m_model.Find(m_selection[0]);
        if (s != NULL && AnnotHitHandle(AnnotHandlePositions(*s, all, m_aspect), AnnotIsEndpointObject(*s), pt,
                                        tolerance) != annot_handle_none)
            return true;
    }
    return AnnotHitObject(all, pt, tolerance, m_aspect, true) >= 0;
}

bool ShareAnnotationSession::EditableAt(const AnnotationPoint& pt, int tolerance, AnnotationRef& ref,
                                        std::string& text) const
{
    const std::vector<AnnotationStroke> all = canvasStrokes();
    //从上往下找第一个点中的、能改文字的对象
    for (size_t i = all.size(); i > 0; --i)
    {
        const AnnotationStroke& s = all[i - 1];
        const bool editable = s.m_tool == annot_tool_text || s.m_tool == annot_tool_icon ||
                              s.m_tool == annot_tool_connector || s.m_tool == annot_tool_badge;
        if (!editable || !AnnotIsSelectable(s) || !AnnotHitTest(s, all, pt, tolerance, m_aspect, true))
            continue;
        ref = AnnotationRef(s.m_author, s.m_id);
        text = (s.m_tool == annot_tool_badge) ? std::to_string(static_cast<long long>(s.m_style.m_number)) : s.m_text;
        return true;
    }
    return false;
}

bool ShareAnnotationSession::SetObjectText(const AnnotationRef& ref, const std::string& utf8)
{
    if (!m_running || !CanDraw())
        return false;
    const AnnotationStroke* s = m_model.Find(ref);
    if (s == NULL || !AnnotIsSelectable(*s))
        return false;
    AnnotationStroke next = *s;
    switch (s->m_tool)
    {
    //文字：改成空串就删掉
    case annot_tool_text:
        if (utf8.empty())
        {
            AnnotationMessage m;
            m.m_op = annot_op_erase;
            m.m_author = ref.m_author;
            m.m_id = ref.m_id;
            if (!m_model.Apply(m_selfSeat, m))
                return false;
            send(m);
            pruneSelection();
            return true;
        }
        next.m_text = utf8;
        break;
    //图元的名字、连接线上的文字：可以为空
    case annot_tool_icon:
    case annot_tool_connector:
        next.m_text = utf8;
        break;
    //数字标号：文字当号码，须是 1~65535 的十进制数
    case annot_tool_badge:
        {
            if (utf8.empty())
                return false;
            char* end = NULL;
            const long n = strtol(utf8.c_str(), &end, kDecimalBase);
            if (end == NULL || *end != '\0' || n < kMinBadgeNumber || n > kMaxBadgeNumber)
                return false;
            next.m_style.m_number = static_cast<int>(n);
        }
        break;
    //其余对象没有文字
    default:
        return false;
    }
    return sendUpdate(next);
}

bool ShareAnnotationSession::restyled(const AnnotationStroke& s, int attribute, int value, AnnotationStroke& out) const
{
    out = s;
    switch (attribute)
    {
    //颜色：全部对象
    case annot_restyle_color:
        out.m_color = static_cast<uint32_t>(value) & kRgbMask;
        return true;
    //粗细档位：按这个对象的种类换成线宽或字号
    case annot_restyle_width:
        out.m_width = scaledWidth(s.m_tool, value);
        return true;
    //线型
    case annot_restyle_line_style:
        if (!usesLineStyle(s.m_tool))
            return false;
        out.m_style.m_lineStyle = value;
        return true;
    //填充：封闭形状
    case annot_restyle_fill:
        if (!AnnotIsClosedShape(s.m_tool))
            return false;
        out.m_style.m_fill = value;
        return true;
    //两端样式
    case annot_restyle_start_cap:
        if (!usesCaps(s.m_tool))
            return false;
        out.m_style.m_startCap = value;
        return true;
    case annot_restyle_end_cap:
        if (!usesCaps(s.m_tool))
            return false;
        out.m_style.m_endCap = value;
        return true;
    //走向：箭头、连接线
    case annot_restyle_routing:
        if (s.m_tool != annot_tool_arrow && s.m_tool != annot_tool_connector)
            return false;
        out.m_style.m_routing = value;
        return true;
    //文字的加粗与背景
    case annot_restyle_text_bold:
        if (s.m_tool != annot_tool_text)
            return false;
        out.m_style.m_bold = (value != 0);
        return true;
    case annot_restyle_text_bg:
        if (s.m_tool != annot_tool_text)
            return false;
        out.m_style.m_textBg = value;
        return true;
    //图元种类
    case annot_restyle_icon:
        if (s.m_tool != annot_tool_icon || value < 0 || value >= kAnnotIconCount)
            return false;
        out.m_style.m_variant = value;
        return true;
    //未知的样式项
    default:
        return false;
    }
}

bool ShareAnnotationSession::RestyleSelection(int attribute, int value)
{
    if (!m_running || !CanDraw() || m_selection.empty())
        return false;
    bool any = false;
    for (size_t i = 0; i < m_selection.size(); ++i)
    {
        const AnnotationStroke* s = m_model.Find(m_selection[i]);
        AnnotationStroke next;
        if (s != NULL && restyled(*s, attribute, value, next) && sendUpdate(next))
            any = true;
    }
    return any;
}

void ShareAnnotationSession::selectDown(const AnnotationPoint& pt, int tolerance, int64_t nowMs)
{
    finishSelectDrag(nowMs);
    pruneSelection();
    m_drawing = true;
    m_dragOrigin = pt;
    m_dragTolerance = tolerance;
    m_dragChanged = false;
    const std::vector<AnnotationStroke> all = canvasStrokes();
    //单选时先看句柄：缩放（按住 Shift 拖角保持比例）或拖一端
    if (m_selection.size() == 1)
    {
        const AnnotationStroke* s = m_model.Find(m_selection[0]);
        if (s != NULL)
        {
            const bool endpoints = AnnotIsEndpointObject(*s);
            const int handle = AnnotHitHandle(AnnotHandlePositions(*s, all, m_aspect), endpoints, pt, tolerance);
            if (handle != annot_handle_none)
            {
                m_dragHandle = handle;
                m_dragOriginal = *s;
                m_dragFromRect = AnnotObjectBounds(*s, all, m_aspect);
                m_selectDrag = endpoints ? select_drag_endpoint : select_drag_resize;
                return;
            }
        }
    }
    //点中对象：按住 Shift 时加选或取消，否则选中它（已在选择里时保持整组）并开始移动
    const int index = AnnotHitObject(all, pt, tolerance, m_aspect, true);
    if (index >= 0)
    {
        const AnnotationRef ref(all[index].m_author, all[index].m_id);
        if (m_shiftDown)
        {
            std::vector<AnnotationRef>::iterator it = std::find(m_selection.begin(), m_selection.end(), ref);
            if (it != m_selection.end())
                m_selection.erase(it);
            else
                m_selection.push_back(ref);
            m_selectDrag = select_drag_none;
            return;
        }
        if (!containsRef(m_selection, ref))
        {
            m_selection.clear();
            m_selection.push_back(ref);
        }
        m_selectDrag = select_drag_move;
        m_moveWantDx = 0;
        m_moveWantDy = 0;
        m_moveSentDx = 0;
        m_moveSentDy = 0;
        m_lastMoveMs = nowMs;
        return;
    }
    //点在空白处：取消选择（按住 Shift 时保留），拖出选择框
    if (!m_shiftDown)
        m_selection.clear();
    m_selectDrag = select_drag_band;
    m_bandTo = pt;
}

void ShareAnnotationSession::selectMove(const AnnotationPoint& pt, int64_t nowMs)
{
    switch (m_selectDrag)
    {
    //移动：记下想要的总位移，到了间隔就发
    case select_drag_move:
        m_moveWantDx = static_cast<int>(pt.m_x) - m_dragOrigin.m_x;
        m_moveWantDy = static_cast<int>(pt.m_y) - m_dragOrigin.m_y;
        flushMove(nowMs, false);
        break;
    //缩放：按句柄算出新矩形，把原来的对象缩放进去，只改本地
    case select_drag_resize:
        {
            const AnnotRect to = AnnotResizeRect(m_dragFromRect, m_dragHandle, pt, m_shiftDown);
            const AnnotationStroke scaled = AnnotScaleObject(m_dragOriginal, m_dragFromRect, to);
            if (m_model.Apply(m_selfSeat, updateMessageFor(scaled), nowMs))
                m_dragChanged = true;
        }
        break;
    //拖一端：这一端跟着光标走，拖动中先脱开吸附，只改本地
    case select_drag_endpoint:
        {
            AnnotationStroke next = m_dragOriginal;
            if (m_dragHandle == annot_handle_start)
            {
                next.m_points.front() = pt;
                next.m_style.m_attachStart = AnnotationRef();
            }
            else
            {
                next.m_points.back() = pt;
                next.m_style.m_attachEnd = AnnotationRef();
            }
            if (m_model.Apply(m_selfSeat, updateMessageFor(next), nowMs))
                m_dragChanged = true;
        }
        break;
    //选择框
    case select_drag_band:
        m_bandTo = pt;
        break;
    //没有在拖
    default:
        break;
    }
}

void ShareAnnotationSession::selectUp(const AnnotationPoint& pt, int64_t nowMs)
{
    const int drag = m_selectDrag;
    switch (drag)
    {
    //移动：把剩下的位移发出去
    case select_drag_move:
        selectMove(pt, nowMs);
        flushMove(nowMs, true);
        break;
    //缩放：最后一次换算之后发出修改
    case select_drag_resize:
        selectMove(pt, nowMs);
        break;
    //拖一端：连接线的这一端落在可吸附的对象上就吸附过去
    case select_drag_endpoint:
        {
            selectMove(pt, nowMs);
            const AnnotationStroke* cur = m_model.Find(AnnotationRef(m_dragOriginal.m_author, m_dragOriginal.m_id));
            if (cur != NULL && cur->m_tool == annot_tool_connector)
            {
                const std::vector<AnnotationStroke> all = canvasStrokes();
                const int target = AnnotHitAttachable(all, pt, m_dragTolerance, m_aspect,
                                                      AnnotationRef(cur->m_author, cur->m_id));
                if (target >= 0)
                {
                    AnnotationStroke next = *cur;
                    const AnnotationRef ref(all[target].m_author, all[target].m_id);
                    if (m_dragHandle == annot_handle_start)
                        next.m_style.m_attachStart = ref;
                    else
                        next.m_style.m_attachEnd = ref;
                    if (m_model.Apply(m_selfSeat, updateMessageFor(next), nowMs))
                        m_dragChanged = true;
                }
            }
        }
        break;
    //选择框：完整落在框里的对象加进选择
    case select_drag_band:
        {
            m_bandTo = pt;
            const AnnotRect box = bandRect(m_dragOrigin, m_bandTo);
            const std::vector<AnnotationStroke> all = canvasStrokes();
            const std::vector<int> in = AnnotObjectsInRect(all, box, m_aspect);
            for (size_t i = 0; i < in.size(); ++i)
            {
                const AnnotationRef ref(all[in[i]].m_author, all[in[i]].m_id);
                if (!containsRef(m_selection, ref))
                    m_selection.push_back(ref);
            }
        }
        break;
    //没有在拖（Shift 点选）
    default:
        break;
    }
    finishSelectDrag(nowMs);
    m_drawing = false;
}

void ShareAnnotationSession::finishSelectDrag(int64_t nowMs)
{
    const int drag = m_selectDrag;
    m_selectDrag = select_drag_none;
    //移动：发出剩下的位移
    if (drag == select_drag_move)
    {
        m_selectDrag = select_drag_move;
        flushMove(nowMs, true);
        m_selectDrag = select_drag_none;
        return;
    }
    //缩放、拖一端：本地改过就把最终状态作为一条修改发出
    if ((drag == select_drag_resize || drag == select_drag_endpoint) && m_dragChanged)
    {
        m_dragChanged = false;
        const AnnotationStroke* cur = m_model.Find(AnnotationRef(m_dragOriginal.m_author, m_dragOriginal.m_id));
        if (cur != NULL)
            send(updateMessageFor(*cur));
    }
}

void ShareAnnotationSession::flushMove(int64_t nowMs, bool force)
{
    if (m_selectDrag != select_drag_move || m_selection.empty())
        return;
    const int dx = m_moveWantDx - m_moveSentDx;
    const int dy = m_moveWantDy - m_moveSentDy;
    if (dx == 0 && dy == 0)
        return;
    if (!force && nowMs - m_lastMoveMs < kAnnotMoveIntervalMs)
        return;
    //选中的对象多于一条操作的上限时分几条发，位移相同
    for (size_t first = 0; first < m_selection.size(); first += static_cast<size_t>(kAnnotMaxMoveTargets))
    {
        AnnotationMessage m;
        m.m_op = annot_op_move;
        m.m_dx = dx;
        m.m_dy = dy;
        const size_t last = (std::min)(m_selection.size(), first + static_cast<size_t>(kAnnotMaxMoveTargets));
        m.m_targets.assign(m_selection.begin() + first, m_selection.begin() + last);
        if (m_model.Apply(m_selfSeat, m, nowMs))
            send(m);
    }
    m_moveSentDx = m_moveWantDx;
    m_moveSentDy = m_moveWantDy;
    m_lastMoveMs = nowMs;
}

void ShareAnnotationSession::pruneSelection()
{
    for (size_t i = m_selection.size(); i > 0; --i)
    {
        const AnnotationStroke* s = m_model.Find(m_selection[i - 1]);
        if (s == NULL || !AnnotIsSelectable(*s))
            m_selection.erase(m_selection.begin() + (i - 1));
    }
}

AnnotationMessage ShareAnnotationSession::updateMessageFor(const AnnotationStroke& s)
{
    AnnotationMessage m;
    m.m_op = annot_op_update;
    m.m_author = s.m_author;
    m.m_id = s.m_id;
    m.m_tool = s.m_tool;
    m.m_color = s.m_color;
    m.m_width = s.m_width;
    m.m_points = s.m_points;
    m.m_text = s.m_text;
    m.m_style = s.m_style;
    return m;
}

bool ShareAnnotationSession::sendUpdate(const AnnotationStroke& s)
{
    const AnnotationMessage m = updateMessageFor(s);
    if (!m_model.Apply(m_selfSeat, m))
        return false;
    send(m);
    return true;
}

} // namespace screenshare
