/**
 *  标注工具条的效果预览图标，ShareAnnotationMenuIcons.h
 *
 *  2026-10-05 联调反馈「各个绘图工具要有图标标识，不然有些无法想象出来绘制效果」：工具条的分组按钮与下拉菜单的每一项都配一个
 *  小图标。图标不是另画的示意图，而是按这一项的工具与样式构造一笔样例笔迹，交给标注的绘制代码（RenderAnnotationLayer）画出来，
 *  所以马克笔是一道粗的方头笔迹、荧光笔是半透明的宽笔迹、虚线就是虚线、某种箭头端点就是那种端点，与实际画出来的一致。
 *  少数不是笔迹的项（整笔擦除、选择对象、删除选中的对象、自定义颜色、从头编号、加粗）用 GDI+ 画一个示意图形。
 *
 *  菜单项的 id 已经编码了「命令 + 取值」（见 ShareAnnotationMenus.h 的 DecodeAnnotMenuChoice），图标按解出来的命令与取值决定，
 *  菜单模型不需要另加字段。DuiMenu 的勾选与图标只能二选一，所以「当前选中」画进图标里：图标底下垫一块浅蓝圆角底并描主色边。
 *
 *  典型用法：
 *      AnnotMenuChoice c;
 *      if (AnnotMenuItemIconChoice(item, c))
 *          RenderAnnotChoiceIcon(c, state, item.m_checked, kAnnotMenuIconSize, bits, pitch);
 *
 *  本文件只有纯函数（绘制用 GDI+，不涉及窗口），单元测试见 Source/Tests/ShareMenuIconTests.cpp。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include "ShareAnnotationMenus.h"

namespace screenshare {

//菜单图标的边长（像素）：与 balloonui DuiMenu 绘制图标的尺寸一致
const int kAnnotMenuIconSize = 16;
//工具条分组按钮前面图标的边长（像素）
const int kAnnotButtonIconSize = 18;

/**
 *  某条命令有没有效果预览图标。
 *  @param choice 命令与取值
 *  @return 有图标返回 true；撤销、清空、权限、退出、保存等工具条按钮命令与非法命令返回 false
 */
bool AnnotChoiceHasIcon(const AnnotMenuChoice& choice);

/**
 *  画一条命令的效果预览图标。
 *  @param choice  命令与取值
 *  @param state   工具条当前状态（样例笔迹的颜色、走向、两端样式等取自这里）
 *  @param checked 是否当前选中（画浅蓝底与主色边）
 *  @param size    图标边长，像素，须大于 0
 *  @param bgra    [出参] 预乘透明度的 BGRA 像素，自上而下，调用方分配 size 行、每行 stride 字节；不拥有
 *  @param stride  每行字节数，不小于 size * 4
 *  @return 画了图标返回 true；命令没有图标（AnnotChoiceHasIcon 为 false）或参数无效时返回 false，bgra 不变
 */
bool RenderAnnotChoiceIcon(const AnnotMenuChoice& choice, const AnnotBarState& state, bool checked, int size,
                           uint8_t* bgra, int stride);

/**
 *  菜单项用哪条命令的图标：普通项为它自己的命令；子菜单的父项（起点样式、终点样式、块状箭头、连接线走向）为子菜单里
 *  打勾的那一项，没有打勾的取第一项；分隔线、不可点的标题与提示没有图标。
 *  @param item 菜单项（BuildAnnotGroupMenu 给出）
 *  @param out  [出参] 命令与取值
 *  @return 有图标返回 true
 */
bool AnnotMenuItemIconChoice(const AnnotMenuItem& item, AnnotMenuChoice& out);

/**
 *  工具条分组按钮前面的图标用哪条命令：当前工具在这一组时为当前工具（如马克笔），否则为这一组的代表项
 *  （画笔组为钢笔、线条组为直线、形状组为矩形、橡皮组为整笔擦除）；箭头、拓扑、标号、粗细、线型、填充按当前样式。
 *  @param group 取值见 AnnotBarGroup
 *  @param state 工具条当前状态
 *  @param out   [出参] 命令与取值
 *  @return 有图标返回 true；颜色组（按钮前面是色块）与非法组返回 false
 */
bool AnnotGroupButtonChoice(int group, const AnnotBarState& state, AnnotMenuChoice& out);

/**
 *  工具条「收起」按钮的图形：向左的双箭头（深灰），预乘透明度。
 *  @param size   边长，像素，须大于 0
 *  @param bgra   [出参] 像素，自上而下，调用方分配 size 行、每行 stride 字节；不拥有
 *  @param stride 每行字节数，不小于 size * 4
 *  @return 参数有效时返回 true
 */
bool RenderAnnotCollapseGlyph(int size, uint8_t* bgra, int stride);

} // namespace screenshare
