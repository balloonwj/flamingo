/**
 *  屏幕共享标注工具条的下拉菜单模型（第 19 期，docs/屏幕共享与录制方案.md 第 2 节第 33～38 条），ShareAnnotationMenus.h
 *
 *  工具条按组排成一行下拉按钮：画笔、线条、箭头、形状、标号、文字、橡皮 | 颜色、粗细、线型、填充 | 撤销、清空、权限、退出。
 *  本文件只装配数据（每组有哪些项、哪项打勾、按钮上写什么、菜单项 id 对应哪条命令），不碰窗口，单元测试见
 *  Source/Tests/ShareDrawTests.cpp（SSDW18、SSDW19）。工具条（CShareAnnotationBar）把模型逐项交给 DuiMenu 弹出，
 *  用户选中后按 DecodeAnnotMenuChoice 换成命令发给控制器。
 *
 *  典型用法：
 *      std::vector<AnnotMenuItem> items = BuildAnnotGroupMenu(annot_group_arrow, state);
 *      AnnotMenuChoice choice;
 *      if (DecodeAnnotMenuChoice(chosenId, choice)) 发 kMsgShareAnnotCommand(choice.m_command, choice.m_value);
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <windows.h>   // UINT

namespace screenshare {

//工具条发给控制器的命令（kMsgShareAnnotCommand 的 wParam），lParam 为取值
enum AnnotBarCommand
{
    annot_bar_tool        = 1,    //选了工具：取值为 AnnotationTool 之一或 kAnnotEraser
    annot_bar_color       = 2,    //选了颜色：取值为 0x00RRGGBB
    annot_bar_undo        = 3,    //撤销本人最近的一笔
    annot_bar_clear       = 4,    //清空所有人的笔迹（发起人）
    annot_bar_permission  = 5,    //发起人切换他人的标注权限：1 允许、0 禁止
    annot_bar_exit        = 6,    //退出标注模式
    annot_bar_width       = 7,    //粗细档位（第 19 期）：取值见 AnnotationWidthLevel
    annot_bar_line_style  = 8,    //线型（第 19 期）：取值见 AnnotationLineStyle
    annot_bar_fill        = 9,    //填充（第 19 期）：取值见 AnnotationFill
    annot_bar_routing     = 10,   //箭头走向（第 19 期）：取值见 AnnotationRouting，同时切到箭头工具
    annot_bar_start_cap   = 11,   //箭头起点样式（第 19 期）：取值见 AnnotationCap，同时切到箭头工具
    annot_bar_end_cap     = 12,   //箭头终点样式（第 19 期）：取值见 AnnotationCap，同时切到箭头工具
    annot_bar_block       = 13,   //块状箭头样式（第 19 期）：取值见 AnnotationBlockArrow，同时切到块状箭头工具
    annot_bar_badge_style = 14,   //数字标号样式（第 19 期）：取值见 AnnotationBadgeStyle，同时切到数字标号工具
    annot_bar_badge_seq   = 15,   //数字标号的序号写法（第 19 期）：取值见 AnnotationSequence，同时切到数字标号工具
    annot_bar_badge_reset = 16,   //数字标号从头编号（第 19 期）
    annot_bar_text_bold   = 17,   //文字加粗（第 19 期）：1 加粗、0 不加粗，同时切到文字工具
    annot_bar_text_bg     = 18,   //文字背景（第 19 期）：取值见 AnnotationTextBg，同时切到文字工具
    annot_bar_custom_color = 19,  //自定义颜色（第 19 期）：工具条弹出取色框，选好后改发 annot_bar_color
    annot_bar_icon        = 20,   //网络拓扑图元（第 20 期）：取值见 AnnotationIcon，同时切到图元工具
    annot_bar_conn_routing = 21,  //连接线走向（第 20 期）：取值见 AnnotationRouting，同时切到连接线工具
    annot_bar_delete      = 22,   //删除选中的对象（第 20 期）
    annot_bar_save        = 23,   //保存为图片（第 23 期）：白板导出整张画布，共享画面导出当前画面加笔迹
    annot_bar_pointer     = 24,   //「鼠标」（2026-10-07）：1 进入「鼠标」状态（不拦截鼠标，工具条与标注保留）、0 回到画图
};

//工具条上的下拉组
enum AnnotBarGroup
{
    annot_group_pen        = 0,   //画笔：铅笔、钢笔、马克笔、荧光笔、毛笔、激光笔
    annot_group_line       = 1,   //线条：直线、折线、曲线
    annot_group_arrow      = 2,   //箭头：走向、两端样式、块状箭头
    annot_group_shape      = 3,   //形状：十一种
    annot_group_badge      = 4,   //数字标号：样式、序号写法、从头编号
    annot_group_text       = 5,   //文字：加粗、背景
    annot_group_eraser     = 6,   //橡皮：整笔擦除、局部擦除
    annot_group_color      = 7,   //颜色：12 种常用色与自定义
    annot_group_width      = 8,   //粗细：细、中、粗
    annot_group_line_style = 9,   //线型：实线、虚线、点线、点划线
    annot_group_fill       = 10,  //填充：空心、实心、半透明
    annot_group_select     = 11,  //选择（第 20 期）：选择对象、删除选中的对象，及操作提示
    annot_group_topology   = 12,  //拓扑（第 20 期）：十一种网络拓扑图元、连接线与连接线走向
};

//下拉组的个数
const int kAnnotGroupCount = 13;

//工具条上各组从左到右的顺序（第 20 期起不再按 AnnotBarGroup 的取值排）：选择、画笔、线条、箭头、形状、拓扑、标号、文字、橡皮 |
//颜色、粗细、线型、填充
extern const int kAnnotGroupOrder[kAnnotGroupCount];
//常用色的个数（第 38 条）
const int kAnnotPaletteSize = 12;

//常用色 0x00RRGGBB：红、橙、黄、绿、青、蓝、紫、粉、棕、灰、黑、白
extern const uint32_t kAnnotPalette[kAnnotPaletteSize];

//工具条的当前状态（菜单据此打勾、按钮据此写字）
struct AnnotBarState
{
    int      m_tool;           //当前工具
    uint32_t m_color;          //当前颜色
    int      m_widthLevel;     //粗细档位
    int      m_lineStyle;      //线型
    int      m_fill;           //填充
    int      m_routing;        //箭头走向
    int      m_startCap;       //箭头起点样式
    int      m_endCap;         //箭头终点样式
    int      m_blockVariant;   //块状箭头样式
    int      m_badgeStyle;     //数字标号样式
    int      m_badgeSequence;  //数字标号的序号写法
    bool     m_textBold;       //文字是否加粗
    int      m_textBg;         //文字背景
    int      m_iconKind;       //网络拓扑图元的种类（第 20 期）
    int      m_connRouting;    //连接线的走向（第 20 期）
    int      m_lastDrawTool;   //最近一次用的画图工具（不含选择工具，2026-10-08）：工具条「选择」按钮退出选择时回到它；默认钢笔

    //默认：钢笔、红色、中档、实线、空心、直线走向、终点实心三角、右箭头、实心圆标号、阿拉伯数字、不加粗、无背景、电脑图元、
    //直线连接线
    AnnotBarState();
};

//下拉菜单的一项
struct AnnotMenuItem
{
    UINT                       m_id;         //菜单项 id；分隔线与分组标题为 0
    std::wstring               m_text;       //文字（已翻译）
    bool                       m_checked;    //是否打勾
    bool                       m_enabled;    //是否可点（分组标题、提示行不可点）
    bool                       m_separator;  //是否分隔线
    int                        m_swatch;     //颜色项的颜色 0x00RRGGBB；不是颜色项为 -1
    std::vector<AnnotMenuItem> m_children;   //子菜单；为空表示普通项

    AnnotMenuItem();
};

/**
 *  某一组的下拉菜单。
 *  @param group 取值见 AnnotBarGroup
 *  @param state 当前状态
 *  @return 菜单项，按显示顺序；组取值非法时为空
 */
std::vector<AnnotMenuItem> BuildAnnotGroupMenu(int group, const AnnotBarState& state);

//菜单选中项换成的命令
struct AnnotMenuChoice
{
    int m_command;   //命令，取值见 AnnotBarCommand
    int m_value;     //取值（工具、颜色、档位等）

    AnnotMenuChoice();
};

/**
 *  把菜单项 id 换成命令。
 *  @param id  BuildAnnotGroupMenu 给出的某个菜单项 id
 *  @param out [出参] 命令
 *  @return id 认识返回 true
 */
bool DecodeAnnotMenuChoice(UINT id, AnnotMenuChoice& out);

/**
 *  下拉按钮上的文字：当前工具在这一组时写当前工具的名字（如「马克笔 ▾」），否则写组名（如「画笔 ▾」）；
 *  颜色、粗细、线型、填充写组名。
 */
std::wstring AnnotGroupButtonText(int group, const AnnotBarState& state);

/**
 *  是否为工具组（选择、画笔、线条、箭头、形状、拓扑、标号、文字、橡皮）。2026-10-05 起这九组不再各占一个按钮，
 *  收进工具条上唯一的「绘图工具」按钮的菜单里；颜色、粗细、线型、填充四个样式组仍各占一个按钮。
 *  @param group 取值见 AnnotBarGroup
 *  @return 是工具组返回 true
 */
bool AnnotIsToolGroup(int group);

/**
 *  组名（已翻译，如「画笔」「形状」），不带下拉符号。
 *  @param group 取值见 AnnotBarGroup；非法时返回空串
 */
std::wstring AnnotGroupName(int group);

/**
 *  「绘图工具」菜单（2026-10-05）：九个工具组按 kAnnotGroupOrder 的先后各为一个子菜单，子菜单标题为组名，
 *  子菜单的内容与 BuildAnnotGroupMenu 给出的这一组的菜单相同。
 *  @param state 当前状态
 *  @return 九个子菜单项
 */
std::vector<AnnotMenuItem> BuildAnnotToolsMenu(const AnnotBarState& state);

/**
 *  「绘图工具」按钮上的文字：当前工具所在组的按钮写法（如「马克笔 ▾」）；当前工具不属于任何工具组时为「绘图工具 ▾」。
 *  @param state 当前状态
 */
std::wstring AnnotToolsButtonText(const AnnotBarState& state);

/**
 *  点工具条上「选择」按钮（2026-10-08）后要切到的工具：当前不是选择工具时进入选择；当前是选择工具时回到最近一次用的
 *  画图工具（m_lastDrawTool，取值不认识时为钢笔）。
 *  @param state 当前状态
 *  @return 工具（AnnotationTool 之一、kAnnotEraser 或 kAnnotSelect）
 */
int AnnotSelectButtonTarget(const AnnotBarState& state);

/**
 *  某个工具属于哪一组。
 *  @return 组；不属于任何工具组时返回 -1
 */
int AnnotToolGroup(int tool);

/**
 *  把一条命令记进工具条状态（工具条与控制器共用，两边记法一致）。箭头走向与两端、块状箭头、标号、文字样式类命令同时切到
 *  对应的工具。
 *  @param choice 命令（自定义颜色须先换成 annot_bar_color）
 *  @param state  [入出参] 状态
 *  @return 是改状态的命令返回 true；撤销、清空、权限、退出、从头编号、自定义颜色等不改状态的命令返回 false，state 不变
 */
bool ApplyAnnotChoice(const AnnotMenuChoice& choice, AnnotBarState& state);

/**
 *  文字「填充色」背景用的颜色：当前颜色与白色按 1:3 混合的浅色，深色文字压在上面仍然看得清。
 *  @param color 当前颜色 0x00RRGGBB
 *  @return 浅色 0x00RRGGBB
 */
uint32_t AnnotTextBgColor(uint32_t color);

/**
 *  这条命令是否让标注从「鼠标」状态回到画图（2026-10-07）：选了工具，或选了会同时切到某个工具的样式（箭头走向与两端、块状箭头、
 *  标号、文字样式、图元、连接线走向）时回到画图；颜色、粗细、线型、填充、撤销等不改变「鼠标」状态。
 *  @param choice 命令
 */
bool AnnotChoiceLeavesPointer(const AnnotMenuChoice& choice);

class ShareAnnotationSession;

/**
 *  把工具条状态整体交给标注会话：工具、颜色、粗细、线型、填充、箭头走向与两端、块状箭头、标号样式、文字样式。
 *  @param state   工具条状态
 *  @param session [入出参] 标注会话（不拥有）
 */
void ApplyAnnotBarState(const AnnotBarState& state, ShareAnnotationSession& session);

/**
 *  选择工具下、有选中对象时的一条命令（第 20 期）：颜色、粗细、线型、填充、两端样式、走向、文字样式、图元种类这些「样式类」命令
 *  改到选中的对象上，工具条状态里对应的一项随之更新，但工具保持为选择工具（不像平时那样切到箭头、文字等工具）。
 *  @param choice    命令
 *  @param state     [入出参] 工具条状态（工具应为 kAnnotSelect）
 *  @param attribute [出参] 交给 ShareAnnotationSession::RestyleSelection 的样式项，取值见 AnnotRestyle
 *  @param value     [出参] 新值
 *  @return 是样式类命令返回 true；其余（选工具、撤销、删除等）返回 false，state 不变
 */
bool ApplyAnnotChoiceInSelectMode(const AnnotMenuChoice& choice, AnnotBarState& state, int& attribute, int& value);

} // namespace screenshare
