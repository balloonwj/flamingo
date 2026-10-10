/**
 *  屏幕共享标注工具条的下拉菜单模型，见 ShareAnnotationMenus.h。
 *
 *  菜单项 id 的编法：命令 × kIdPerCommand + 取值的序号（工具、档位、样式等取值本身；颜色为常用色的下标）。解码时反过来拆。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "ShareAnnotationMenus.h"

#include "ShareAnnotation.h"
#include "ShareAnnotationSession.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

const int kAnnotGroupOrder[kAnnotGroupCount] = { annot_group_select, annot_group_pen, annot_group_line, annot_group_arrow,
                                                annot_group_shape, annot_group_topology, annot_group_badge, annot_group_text,
                                                annot_group_eraser, annot_group_color, annot_group_width,
                                                annot_group_line_style, annot_group_fill };

const uint32_t kAnnotPalette[kAnnotPaletteSize] = { 0xE53935, 0xFB8C00, 0xFDD835, 0x43A047, 0x00ACC1, 0x1E88E5,
                                                    0x8E24AA, 0xD81B60, 0x6D4C41, 0x757575, 0x212121, 0xFFFFFF };

namespace {

//菜单项 id = 命令 × kIdPerCommand + 序号；序号不超过 kIdPerCommand - 1
const UINT kIdPerCommand = 1000;
//下拉按钮文字后的下拉符号
const wchar_t* const kDropMark = L" ▾";
//0x00RRGGBB 的有效位
const uint32_t kAnnotRgbMask = 0x00FFFFFF;
//一个颜色分量的位数与最大值，红色分量的移位
const int kAnnotChannelBits = 8;
const uint32_t kAnnotChannelMax = 0xFF;
const int kAnnotRedShift = 16;
//文字填充背景：当前颜色 1 份与白色 3 份混合（共 4 份）
const uint32_t kAnnotTextBgWhiteParts = 3;
const uint32_t kAnnotTextBgTotalParts = 4;

//一个带名字的取值
struct NamedValue
{
    int            m_value;   //取值
    const wchar_t* m_name;    //名字（TR_NOOP 标记，用时再翻译）
};

//画笔组
const NamedValue kPenTools[] = { { annot_tool_pencil, TR_NOOP(L"铅笔") }, { annot_tool_pen, TR_NOOP(L"钢笔") },
                                 { annot_tool_marker, TR_NOOP(L"马克笔") }, { annot_tool_highlighter, TR_NOOP(L"荧光笔") },
                                 { annot_tool_brush, TR_NOOP(L"毛笔") }, { annot_tool_laser, TR_NOOP(L"激光笔") } };
//线条组
const NamedValue kLineTools[] = { { annot_tool_line, TR_NOOP(L"直线") }, { annot_tool_polyline, TR_NOOP(L"折线") },
                                  { annot_tool_curve, TR_NOOP(L"曲线") } };
//形状组
const NamedValue kShapeTools[] = { { annot_tool_rect, TR_NOOP(L"矩形") }, { annot_tool_round_rect, TR_NOOP(L"圆角矩形") },
                                   { annot_tool_ellipse, TR_NOOP(L"椭圆") }, { annot_tool_triangle, TR_NOOP(L"三角形") },
                                   { annot_tool_diamond, TR_NOOP(L"菱形") }, { annot_tool_pentagon, TR_NOOP(L"五边形") },
                                   { annot_tool_hexagon, TR_NOOP(L"六边形") }, { annot_tool_star, TR_NOOP(L"五角星") },
                                   { annot_tool_parallelogram, TR_NOOP(L"平行四边形") },
                                   { annot_tool_bubble, TR_NOOP(L"对话气泡") }, { annot_tool_cloud, TR_NOOP(L"云形") } };
//橡皮组
const NamedValue kEraserTools[] = { { kAnnotEraser, TR_NOOP(L"整笔擦除") },
                                    { annot_tool_eraser_area, TR_NOOP(L"局部擦除") } };
//箭头走向
const NamedValue kRoutings[] = { { annot_route_straight, TR_NOOP(L"直线箭头") }, { annot_route_elbow, TR_NOOP(L"直角折线箭头") },
                                 { annot_route_arc, TR_NOOP(L"弧形箭头") } };
//两端样式
const NamedValue kCaps[] = { { annot_cap_none, TR_NOOP(L"无") }, { annot_cap_triangle, TR_NOOP(L"实心三角") },
                             { annot_cap_triangle_hollow, TR_NOOP(L"空心三角") }, { annot_cap_open, TR_NOOP(L"开口箭头") },
                             { annot_cap_swallowtail, TR_NOOP(L"燕尾") }, { annot_cap_circle, TR_NOOP(L"实心圆") },
                             { annot_cap_circle_hollow, TR_NOOP(L"空心圆") }, { annot_cap_diamond, TR_NOOP(L"菱形") },
                             { annot_cap_bar, TR_NOOP(L"竖线") } };
//块状箭头
const NamedValue kBlocks[] = { { annot_block_right, TR_NOOP(L"右箭头") }, { annot_block_double, TR_NOOP(L"左右箭头") },
                               { annot_block_up_down, TR_NOOP(L"上下箭头") }, { annot_block_bent, TR_NOOP(L"弯折箭头") },
                               { annot_block_u_turn, TR_NOOP(L"U 形掉头箭头") }, { annot_block_circular, TR_NOOP(L"环形箭头") } };
//数字标号样式与写法
const NamedValue kBadgeStyles[] = { { annot_badge_circle, TR_NOOP(L"实心圆") }, { annot_badge_circle_hollow, TR_NOOP(L"空心圆") },
                                    { annot_badge_square, TR_NOOP(L"方块") }, { annot_badge_pin, TR_NOOP(L"定位针") } };
const NamedValue kSequences[] = { { annot_seq_digit, TR_NOOP(L"1 2 3") }, { annot_seq_letter, TR_NOOP(L"A B C") },
                                  { annot_seq_chinese, TR_NOOP(L"一 二 三") } };
//文字背景
const NamedValue kTextBgs[] = { { annot_text_bg_none, TR_NOOP(L"无背景") }, { annot_text_bg_white, TR_NOOP(L"白底") },
                                { annot_text_bg_fill, TR_NOOP(L"填充色") } };
//常用色的名字（与 kAnnotPalette 一一对应）
const wchar_t* const kPaletteNames[kAnnotPaletteSize] = { TR_NOOP(L"红"), TR_NOOP(L"橙"), TR_NOOP(L"黄"), TR_NOOP(L"绿"),
                                                          TR_NOOP(L"青"), TR_NOOP(L"蓝"), TR_NOOP(L"紫"), TR_NOOP(L"粉"),
                                                          TR_NOOP(L"棕"), TR_NOOP(L"灰"), TR_NOOP(L"黑"), TR_NOOP(L"白") };
//粗细、线型、填充
const NamedValue kWidths[] = { { annot_width_thin, TR_NOOP(L"细") }, { annot_width_medium, TR_NOOP(L"中") },
                               { annot_width_thick, TR_NOOP(L"粗") } };
const NamedValue kLineStyles[] = { { annot_line_solid, TR_NOOP(L"实线") }, { annot_line_dash, TR_NOOP(L"虚线") },
                                   { annot_line_dot, TR_NOOP(L"点线") }, { annot_line_dash_dot, TR_NOOP(L"点划线") } };
const NamedValue kFills[] = { { annot_fill_none, TR_NOOP(L"空心") }, { annot_fill_solid, TR_NOOP(L"实心") },
                              { annot_fill_translucent, TR_NOOP(L"半透明") } };
//网络拓扑图元（第 20 期，与 AnnotationIcon 一一对应）
const NamedValue kIcons[] = { { annot_icon_computer, TR_NOOP(L"电脑") }, { annot_icon_laptop, TR_NOOP(L"笔记本") },
                              { annot_icon_server, TR_NOOP(L"服务器") }, { annot_icon_router, TR_NOOP(L"路由器") },
                              { annot_icon_switch, TR_NOOP(L"交换机") }, { annot_icon_firewall, TR_NOOP(L"防火墙") },
                              { annot_icon_ap, TR_NOOP(L"无线 AP") }, { annot_icon_internet, TR_NOOP(L"互联网") },
                              { annot_icon_database, TR_NOOP(L"数据库") }, { annot_icon_phone, TR_NOOP(L"手机") },
                              { annot_icon_printer, TR_NOOP(L"打印机") } };
//连接线走向（第 20 期）
const NamedValue kConnRoutings[] = { { annot_route_straight, TR_NOOP(L"直线") }, { annot_route_elbow, TR_NOOP(L"直角折线") },
                                     { annot_route_arc, TR_NOOP(L"弧线") } };
//组名（与 AnnotBarGroup 一一对应）
const wchar_t* const kGroupNames[kAnnotGroupCount] = { TR_NOOP(L"画笔"), TR_NOOP(L"线条"), TR_NOOP(L"箭头"), TR_NOOP(L"形状"),
                                                       TR_NOOP(L"标号"), TR_NOOP(L"文字"), TR_NOOP(L"橡皮"), TR_NOOP(L"颜色"),
                                                       TR_NOOP(L"粗细"), TR_NOOP(L"线型"), TR_NOOP(L"填充"), TR_NOOP(L"选择"),
                                                       TR_NOOP(L"拓扑") };

#define ANNOT_COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

UINT makeId(int command, int index)
{
    return static_cast<UINT>(command) * kIdPerCommand + static_cast<UINT>(index);
}

AnnotMenuItem makeItem(UINT id, const wchar_t* text, bool checked)
{
    AnnotMenuItem it;
    it.m_id = id;
    it.m_text = TR(text);
    it.m_checked = checked;
    return it;
}

//不可点的分组标题或提示
AnnotMenuItem makeLabel(const wchar_t* text)
{
    AnnotMenuItem it;
    it.m_text = TR(text);
    it.m_enabled = false;
    return it;
}

AnnotMenuItem makeSeparator()
{
    AnnotMenuItem it;
    it.m_separator = true;
    it.m_enabled = false;
    return it;
}

//把一组取值逐项加成菜单项：命令为 command，与 current 相等的打勾（onlyIfActive 为 false 时不打勾）
void appendValues(std::vector<AnnotMenuItem>& out, int command, const NamedValue* values, size_t count, int current,
                  bool onlyIfActive)
{
    for (size_t i = 0; i < count; ++i)
        out.push_back(makeItem(makeId(command, values[i].m_value), values[i].m_name,
                               onlyIfActive && values[i].m_value == current));
}

//子菜单项
AnnotMenuItem makeSubMenu(const wchar_t* text, const std::vector<AnnotMenuItem>& children)
{
    AnnotMenuItem it;
    it.m_text = TR(text);
    it.m_children = children;
    return it;
}

//在一组带名字的取值里找某个取值的名字；找不到返回 NULL
const wchar_t* findName(const NamedValue* values, size_t count, int value)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (values[i].m_value == value)
            return values[i].m_name;
    }
    return NULL;
}

//取值是否在一组带名字的取值里
bool hasValue(const NamedValue* values, size_t count, int value)
{
    return findName(values, count, value) != NULL;
}

} // namespace

AnnotBarState::AnnotBarState()
    : m_tool(annot_tool_pen)
    , m_color(kAnnotDefaultColor)
    , m_widthLevel(annot_width_medium)
    , m_lineStyle(annot_line_solid)
    , m_fill(annot_fill_none)
    , m_routing(annot_route_straight)
    , m_startCap(annot_cap_none)
    , m_endCap(annot_cap_triangle)
    , m_blockVariant(annot_block_right)
    , m_badgeStyle(annot_badge_circle)
    , m_badgeSequence(annot_seq_digit)
    , m_textBold(false)
    , m_textBg(annot_text_bg_none)
    , m_iconKind(annot_icon_computer)
    , m_connRouting(annot_route_straight)
    , m_lastDrawTool(annot_tool_pen)
{
}

AnnotMenuItem::AnnotMenuItem()
    : m_id(0)
    , m_checked(false)
    , m_enabled(true)
    , m_separator(false)
    , m_swatch(-1)
{
}

AnnotMenuChoice::AnnotMenuChoice()
    : m_command(0)
    , m_value(0)
{
}

std::vector<AnnotMenuItem> BuildAnnotGroupMenu(int group, const AnnotBarState& state)
{
    std::vector<AnnotMenuItem> out;
    switch (group)
    {
    //画笔
    case annot_group_pen:
        appendValues(out, annot_bar_tool, kPenTools, ANNOT_COUNT_OF(kPenTools), state.m_tool, true);
        break;
    //线条
    case annot_group_line:
        appendValues(out, annot_bar_tool, kLineTools, ANNOT_COUNT_OF(kLineTools), state.m_tool, true);
        break;
    //箭头：走向（选了即切到箭头工具；打勾的是箭头的当前走向），两端样式两个子菜单，块状箭头子菜单
    case annot_group_arrow:
        {
            appendValues(out, annot_bar_routing, kRoutings, ANNOT_COUNT_OF(kRoutings), state.m_routing, true);
            out.push_back(makeSeparator());
            std::vector<AnnotMenuItem> starts;
            appendValues(starts, annot_bar_start_cap, kCaps, ANNOT_COUNT_OF(kCaps), state.m_startCap, true);
            out.push_back(makeSubMenu(TR_NOOP(L"起点样式"), starts));
            std::vector<AnnotMenuItem> ends;
            appendValues(ends, annot_bar_end_cap, kCaps, ANNOT_COUNT_OF(kCaps), state.m_endCap, true);
            out.push_back(makeSubMenu(TR_NOOP(L"终点样式"), ends));
            out.push_back(makeSeparator());
            std::vector<AnnotMenuItem> blocks;
            appendValues(blocks, annot_bar_block, kBlocks, ANNOT_COUNT_OF(kBlocks), state.m_blockVariant, true);
            out.push_back(makeSubMenu(TR_NOOP(L"块状箭头"), blocks));
        }
        break;
    //形状：十一种，最后一行提示按住 Shift 画正形
    case annot_group_shape:
        appendValues(out, annot_bar_tool, kShapeTools, ANNOT_COUNT_OF(kShapeTools), state.m_tool, true);
        out.push_back(makeSeparator());
        out.push_back(makeLabel(TR_NOOP(L"按住 Shift 画正形")));
        break;
    //数字标号：样式、序号写法、从头编号
    case annot_group_badge:
        out.push_back(makeLabel(TR_NOOP(L"样式")));
        appendValues(out, annot_bar_badge_style, kBadgeStyles, ANNOT_COUNT_OF(kBadgeStyles), state.m_badgeStyle, true);
        out.push_back(makeSeparator());
        out.push_back(makeLabel(TR_NOOP(L"序号")));
        appendValues(out, annot_bar_badge_seq, kSequences, ANNOT_COUNT_OF(kSequences), state.m_badgeSequence, true);
        out.push_back(makeSeparator());
        out.push_back(makeItem(makeId(annot_bar_badge_reset, 0), TR_NOOP(L"从头编号"), false));
        break;
    //文字：加粗（选了切换），背景三种
    case annot_group_text:
        out.push_back(makeItem(makeId(annot_bar_text_bold, state.m_textBold ? 0 : 1), TR_NOOP(L"加粗"), state.m_textBold));
        out.push_back(makeSeparator());
        out.push_back(makeLabel(TR_NOOP(L"背景")));
        appendValues(out, annot_bar_text_bg, kTextBgs, ANNOT_COUNT_OF(kTextBgs), state.m_textBg, true);
        break;
    //橡皮
    case annot_group_eraser:
        appendValues(out, annot_bar_tool, kEraserTools, ANNOT_COUNT_OF(kEraserTools), state.m_tool, true);
        break;
    //颜色：常用色色块（当前色打勾），最后是自定义
    case annot_group_color:
        for (int i = 0; i < kAnnotPaletteSize; ++i)
        {
            AnnotMenuItem it = makeItem(makeId(annot_bar_color, i), kPaletteNames[i], kAnnotPalette[i] == state.m_color);
            it.m_swatch = static_cast<int>(kAnnotPalette[i]);
            out.push_back(it);
        }
        out.push_back(makeSeparator());
        out.push_back(makeItem(makeId(annot_bar_custom_color, 0), TR_NOOP(L"自定义颜色…"), false));
        break;
    //粗细
    case annot_group_width:
        appendValues(out, annot_bar_width, kWidths, ANNOT_COUNT_OF(kWidths), state.m_widthLevel, true);
        break;
    //线型
    case annot_group_line_style:
        appendValues(out, annot_bar_line_style, kLineStyles, ANNOT_COUNT_OF(kLineStyles), state.m_lineStyle, true);
        break;
    //填充
    case annot_group_fill:
        appendValues(out, annot_bar_fill, kFills, ANNOT_COUNT_OF(kFills), state.m_fill, true);
        break;
    //选择（第 20 期）：选择对象、删除选中的对象，及操作提示
    case annot_group_select:
        out.push_back(makeItem(makeId(annot_bar_tool, kAnnotSelect), TR_NOOP(L"选择对象"), state.m_tool == kAnnotSelect));
        out.push_back(makeItem(makeId(annot_bar_delete, 0), TR_NOOP(L"删除选中的对象"), false));
        out.push_back(makeSeparator());
        out.push_back(makeLabel(TR_NOOP(L"拖动对象可移动，拖动小方块可缩放")));
        out.push_back(makeLabel(TR_NOOP(L"双击文字、图元或连接线可修改文字")));
        out.push_back(makeLabel(TR_NOOP(L"按住 Shift 点选可选中多个")));
        break;
    //拓扑（第 20 期）：十一种图元（图元工具时当前图元打勾）、连接线与连接线走向
    case annot_group_topology:
        {
            appendValues(out, annot_bar_icon, kIcons, ANNOT_COUNT_OF(kIcons), state.m_iconKind,
                         state.m_tool == annot_tool_icon);
            out.push_back(makeSeparator());
            out.push_back(makeItem(makeId(annot_bar_tool, annot_tool_connector), TR_NOOP(L"连接线"),
                                   state.m_tool == annot_tool_connector));
            std::vector<AnnotMenuItem> routes;
            appendValues(routes, annot_bar_conn_routing, kConnRoutings, ANNOT_COUNT_OF(kConnRoutings), state.m_connRouting,
                         true);
            out.push_back(makeSubMenu(TR_NOOP(L"连接线走向"), routes));
            out.push_back(makeSeparator());
            out.push_back(makeLabel(TR_NOOP(L"连接线的两端拖到图元上会自动连上")));
        }
        break;
    //组取值非法：空菜单
    default:
        break;
    }
    return out;
}

bool DecodeAnnotMenuChoice(UINT id, AnnotMenuChoice& out)
{
    const int command = static_cast<int>(id / kIdPerCommand);
    const int index = static_cast<int>(id % kIdPerCommand);
    out.m_command = command;
    out.m_value = index;
    switch (command)
    {
    //工具：画笔、线条、形状、橡皮各组里的取值
    case annot_bar_tool:
        return hasValue(kPenTools, ANNOT_COUNT_OF(kPenTools), index) ||
               hasValue(kLineTools, ANNOT_COUNT_OF(kLineTools), index) ||
               hasValue(kShapeTools, ANNOT_COUNT_OF(kShapeTools), index) ||
               hasValue(kEraserTools, ANNOT_COUNT_OF(kEraserTools), index) || index == kAnnotSelect ||
               index == annot_tool_connector;
    //网络拓扑图元（第 20 期）
    case annot_bar_icon:
        return hasValue(kIcons, ANNOT_COUNT_OF(kIcons), index);
    //连接线走向（第 20 期）
    case annot_bar_conn_routing:
        return hasValue(kConnRoutings, ANNOT_COUNT_OF(kConnRoutings), index);
    //删除选中的对象（第 20 期）：没有取值
    case annot_bar_delete:
        return index == 0;
    //颜色：序号换成颜色值
    case annot_bar_color:
        if (index < 0 || index >= kAnnotPaletteSize)
            return false;
        out.m_value = static_cast<int>(kAnnotPalette[index]);
        return true;
    //自定义颜色、从头编号：没有取值
    case annot_bar_custom_color:
    case annot_bar_badge_reset:
        return index == 0;
    //粗细
    case annot_bar_width:
        return hasValue(kWidths, ANNOT_COUNT_OF(kWidths), index);
    //线型
    case annot_bar_line_style:
        return hasValue(kLineStyles, ANNOT_COUNT_OF(kLineStyles), index);
    //填充
    case annot_bar_fill:
        return hasValue(kFills, ANNOT_COUNT_OF(kFills), index);
    //箭头走向
    case annot_bar_routing:
        return hasValue(kRoutings, ANNOT_COUNT_OF(kRoutings), index);
    //箭头两端
    case annot_bar_start_cap:
    case annot_bar_end_cap:
        return hasValue(kCaps, ANNOT_COUNT_OF(kCaps), index);
    //块状箭头
    case annot_bar_block:
        return hasValue(kBlocks, ANNOT_COUNT_OF(kBlocks), index);
    //数字标号样式、写法
    case annot_bar_badge_style:
        return hasValue(kBadgeStyles, ANNOT_COUNT_OF(kBadgeStyles), index);
    case annot_bar_badge_seq:
        return hasValue(kSequences, ANNOT_COUNT_OF(kSequences), index);
    //文字加粗：0 或 1
    case annot_bar_text_bold:
        return index == 0 || index == 1;
    //文字背景
    case annot_bar_text_bg:
        return hasValue(kTextBgs, ANNOT_COUNT_OF(kTextBgs), index);
    //其余：不是菜单项
    default:
        return false;
    }
}

std::wstring AnnotGroupButtonText(int group, const AnnotBarState& state)
{
    if (group < 0 || group >= kAnnotGroupCount)
        return std::wstring();
    const wchar_t* name = NULL;
    if (AnnotToolGroup(state.m_tool) == group)
    {
        name = findName(kPenTools, ANNOT_COUNT_OF(kPenTools), state.m_tool);
        if (name == NULL)
            name = findName(kLineTools, ANNOT_COUNT_OF(kLineTools), state.m_tool);
        if (name == NULL)
            name = findName(kShapeTools, ANNOT_COUNT_OF(kShapeTools), state.m_tool);
        if (name == NULL)
            name = findName(kEraserTools, ANNOT_COUNT_OF(kEraserTools), state.m_tool);
        if (name == NULL && state.m_tool == annot_tool_block_arrow)
            name = TR_NOOP(L"块状箭头");
        //拓扑组（第 20 期）：图元写当前图元的名字，连接线写「连接线」
        if (name == NULL && state.m_tool == annot_tool_icon)
            name = findName(kIcons, ANNOT_COUNT_OF(kIcons), state.m_iconKind);
        if (name == NULL && state.m_tool == annot_tool_connector)
            name = TR_NOOP(L"连接线");
    }
    if (name == NULL)
        name = kGroupNames[group];
    return std::wstring(TR(name)) + kDropMark;
}

bool AnnotIsToolGroup(int group)
{
    switch (group)
    {
    //九个工具组：收进「绘图工具」菜单
    case annot_group_select:
    case annot_group_pen:
    case annot_group_line:
    case annot_group_arrow:
    case annot_group_shape:
    case annot_group_topology:
    case annot_group_badge:
    case annot_group_text:
    case annot_group_eraser:
        return true;
    //颜色、粗细、线型、填充四个样式组与非法取值
    default:
        return false;
    }
}

std::wstring AnnotGroupName(int group)
{
    if (group < 0 || group >= kAnnotGroupCount)
        return std::wstring();
    return TR(kGroupNames[group]);
}

std::vector<AnnotMenuItem> BuildAnnotToolsMenu(const AnnotBarState& state)
{
    std::vector<AnnotMenuItem> out;
    //按工具条原来的先后，每个工具组一个子菜单，内容与原来这一组的下拉菜单相同
    for (int i = 0; i < kAnnotGroupCount; ++i)
    {
        const int g = kAnnotGroupOrder[i];
        if (AnnotIsToolGroup(g))
            out.push_back(makeSubMenu(kGroupNames[g], BuildAnnotGroupMenu(g, state)));
    }
    return out;
}

std::wstring AnnotToolsButtonText(const AnnotBarState& state)
{
    //选择状态（2026-10-08）：「选择」有单独的按钮，「绘图工具」写最近一次用的画图工具
    if (state.m_tool == kAnnotSelect)
    {
        AnnotBarState shown = state;
        shown.m_tool = AnnotSelectButtonTarget(state);
        return AnnotToolsButtonText(shown);
    }
    const int g = AnnotToolGroup(state.m_tool);
    if (g < 0 || !AnnotIsToolGroup(g))
        return std::wstring(TR(L"绘图工具")) + kDropMark;
    return AnnotGroupButtonText(g, state);
}

int AnnotSelectButtonTarget(const AnnotBarState& state)
{
    if (state.m_tool != kAnnotSelect)
        return kAnnotSelect;
    //回到最近一次用的画图工具；取值不认识（不属于任何工具组）时按钢笔
    const int group = AnnotToolGroup(state.m_lastDrawTool);
    if (group < 0 || group == annot_group_select)
        return annot_tool_pen;
    return state.m_lastDrawTool;
}

int AnnotToolGroup(int tool)
{
    if (hasValue(kPenTools, ANNOT_COUNT_OF(kPenTools), tool))
        return annot_group_pen;
    if (hasValue(kLineTools, ANNOT_COUNT_OF(kLineTools), tool))
        return annot_group_line;
    if (tool == annot_tool_arrow || tool == annot_tool_block_arrow)
        return annot_group_arrow;
    if (hasValue(kShapeTools, ANNOT_COUNT_OF(kShapeTools), tool))
        return annot_group_shape;
    if (tool == annot_tool_badge)
        return annot_group_badge;
    if (tool == annot_tool_text)
        return annot_group_text;
    //第 20 期：选择工具、网络拓扑图元与连接线
    if (tool == kAnnotSelect)
        return annot_group_select;
    if (tool == annot_tool_icon || tool == annot_tool_connector)
        return annot_group_topology;
    if (hasValue(kEraserTools, ANNOT_COUNT_OF(kEraserTools), tool))
        return annot_group_eraser;
    return -1;
}

bool ApplyAnnotChoiceInSelectMode(const AnnotMenuChoice& choice, AnnotBarState& state, int& attribute, int& value)
{
    int attr = 0;
    switch (choice.m_command)
    {
    //颜色、粗细、线型、填充
    case annot_bar_color:
        attr = annot_restyle_color;
        break;
    case annot_bar_width:
        attr = annot_restyle_width;
        break;
    case annot_bar_line_style:
        attr = annot_restyle_line_style;
        break;
    case annot_bar_fill:
        attr = annot_restyle_fill;
        break;
    //两端样式
    case annot_bar_start_cap:
        attr = annot_restyle_start_cap;
        break;
    case annot_bar_end_cap:
        attr = annot_restyle_end_cap;
        break;
    //箭头走向与连接线走向都改选中对象的走向
    case annot_bar_routing:
    case annot_bar_conn_routing:
        attr = annot_restyle_routing;
        break;
    //文字加粗与背景
    case annot_bar_text_bold:
        attr = annot_restyle_text_bold;
        break;
    case annot_bar_text_bg:
        attr = annot_restyle_text_bg;
        break;
    //图元种类
    case annot_bar_icon:
        attr = annot_restyle_icon;
        break;
    //选工具、撤销、删除等：不是样式类命令
    default:
        return false;
    }
    //状态里对应的一项照常更新，工具保持为选择工具
    const int keepTool = state.m_tool;
    ApplyAnnotChoice(choice, state);
    state.m_tool = keepTool;
    attribute = attr;
    value = (choice.m_command == annot_bar_color) ? static_cast<int>(state.m_color) : choice.m_value;
    return true;
}

bool AnnotChoiceLeavesPointer(const AnnotMenuChoice& choice)
{
    switch (choice.m_command)
    {
    //选了工具，或选了会同时切到某个工具的样式：回到画图
    case annot_bar_tool:
    case annot_bar_routing:
    case annot_bar_start_cap:
    case annot_bar_end_cap:
    case annot_bar_block:
    case annot_bar_badge_style:
    case annot_bar_badge_seq:
    case annot_bar_text_bold:
    case annot_bar_text_bg:
    case annot_bar_icon:
    case annot_bar_conn_routing:
        return true;
    //颜色、粗细、线型、填充、撤销、清空、权限、保存等：不改变「鼠标」状态
    default:
        return false;
    }
}

namespace {

//把一个菜单选择套用到状态上（不含「最近一次用的画图工具」的记录，见 ApplyAnnotChoice）
bool applyChoiceToState(const AnnotMenuChoice& choice, AnnotBarState& state)
{
    const int v = choice.m_value;
    switch (choice.m_command)
    {
    //工具
    case annot_bar_tool:
        state.m_tool = v;
        return true;
    //颜色
    case annot_bar_color:
        state.m_color = static_cast<uint32_t>(v) & kAnnotRgbMask;
        return true;
    //粗细、线型、填充：工具不变
    case annot_bar_width:
        state.m_widthLevel = v;
        return true;
    case annot_bar_line_style:
        state.m_lineStyle = v;
        return true;
    case annot_bar_fill:
        state.m_fill = v;
        return true;
    //箭头走向与两端：切到箭头工具
    case annot_bar_routing:
        state.m_routing = v;
        state.m_tool = annot_tool_arrow;
        return true;
    case annot_bar_start_cap:
        state.m_startCap = v;
        state.m_tool = annot_tool_arrow;
        return true;
    case annot_bar_end_cap:
        state.m_endCap = v;
        state.m_tool = annot_tool_arrow;
        return true;
    //块状箭头：切到块状箭头工具
    case annot_bar_block:
        state.m_blockVariant = v;
        state.m_tool = annot_tool_block_arrow;
        return true;
    //数字标号的样式与序号写法：切到数字标号工具
    case annot_bar_badge_style:
        state.m_badgeStyle = v;
        state.m_tool = annot_tool_badge;
        return true;
    case annot_bar_badge_seq:
        state.m_badgeSequence = v;
        state.m_tool = annot_tool_badge;
        return true;
    //文字加粗（取值是切换后的值）与背景：切到文字工具
    case annot_bar_text_bold:
        state.m_textBold = (v != 0);
        state.m_tool = annot_tool_text;
        return true;
    case annot_bar_text_bg:
        state.m_textBg = v;
        state.m_tool = annot_tool_text;
        return true;
    //网络拓扑图元：切到图元工具（第 20 期）
    case annot_bar_icon:
        state.m_iconKind = v;
        state.m_tool = annot_tool_icon;
        return true;
    //连接线走向：切到连接线工具（第 20 期）
    case annot_bar_conn_routing:
        state.m_connRouting = v;
        state.m_tool = annot_tool_connector;
        return true;
    //撤销、清空、权限、退出、从头编号、自定义颜色（须先换成颜色命令）与未知命令：不改状态
    default:
        return false;
    }
}

} // namespace

bool ApplyAnnotChoice(const AnnotMenuChoice& choice, AnnotBarState& state)
{
    if (!applyChoiceToState(choice, state))
        return false;
    //记下最近一次用的画图工具（2026-10-08）：工具条「选择」按钮退出选择时回到它；选择工具本身不记
    if (state.m_tool != kAnnotSelect)
        state.m_lastDrawTool = state.m_tool;
    return true;
}

uint32_t AnnotTextBgColor(uint32_t color)
{
    //每个分量向白色（255）靠近四分之三
    uint32_t out = 0;
    for (int shift = 0; shift <= kAnnotRedShift; shift += kAnnotChannelBits)
    {
        const uint32_t c = (color >> shift) & kAnnotChannelMax;
        const uint32_t mixed = c + ((kAnnotChannelMax - c) * kAnnotTextBgWhiteParts) / kAnnotTextBgTotalParts;
        out |= mixed << shift;
    }
    return out;
}

void ApplyAnnotBarState(const AnnotBarState& state, ShareAnnotationSession& session)
{
    session.SetTool(state.m_tool);
    session.SetColor(state.m_color);
    session.SetWidthLevel(state.m_widthLevel);
    session.SetLineStyle(state.m_lineStyle);
    session.SetFill(state.m_fill);
    session.SetRouting(state.m_routing);
    session.SetArrowCaps(state.m_startCap, state.m_endCap);
    session.SetBlockVariant(state.m_blockVariant);
    session.SetBadgeStyle(state.m_badgeStyle, state.m_badgeSequence);
    session.SetTextStyle(state.m_textBold, state.m_textBg, AnnotTextBgColor(state.m_color));
    session.SetIconKind(state.m_iconKind);
    session.SetConnectorRouting(state.m_connRouting);
}

} // namespace screenshare
