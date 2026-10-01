/**
 *  GroupManagePaint.h
 *
 *  群管理对话框内部共用的绘制工具 —— GDI 文字、GDI+ 圆角矩形填充 / 描边，
 *  以及按 DPI 换算尺寸的函数。抽出来供本目录下的按钮、导航项、各页自绘小控件
 *  统一复用，避免每个文件各抄一份 DrawGdiText / AddRoundRect。
 *
 *  尺寸约定：本目录里所有字号与尺寸常量都按 96 DPI（100% 缩放）设计，使用前
 *  一律经 GmScale 换算成当前 DPI 下的物理像素，整个窗口因此随 DPI 等比放大。
 *  GmDrawText 与导航项建字体时自行换算字号，调用方传入的仍是 96 DPI 下的字号。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL，
 *  并且包含 <gdiplus.h>。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include <gdiplus.h>

namespace balloongroupmanage {

// 字重。映射到 GDI 的 FW_* 常量。
enum GmFontWeight
{
    kGmFontNormal   = 0,   // 常规（正文）
    kGmFontSemiBold = 1,   // 半粗（小标题 / 主按钮文字）
    kGmFontBold     = 2     // 粗体（页面大标题）
};

// 群管理窗的文字样式（字号表）。本目录所有自绘文字只能取自这张表 —— 各处
// 各写一个像素值，层级迟早会乱（曾出现说明文字 11px、下拉框里的字反而比行标题
// 大的情形）。取值来自对 design/群管理1/6/7/8.png 的实测，按窗口宽度比例
// 960/1125 折算到 96 DPI，见 Tests/GroupManageLayoutTests.cpp 的 TS 组。
enum GmTextStyle
{
    kGmTextPageTitle = 0,   // 页面大标题、顶栏群名：15px 粗体
    kGmTextCardTitle = 1,   // 卡片标题、成员名、公告条目标题、敏感词：14px 粗体
    kGmTextItemTitle = 2,   // 导航项、设置行标题、表单标签：14px 常规（导航选中项另行加粗）
    kGmTextBody      = 3,   // 页面副标题、卡片说明、公告正文、分组标题、普通按钮：13px 常规
    kGmTextCaption   = 4    // 设置行说明、顶栏角色与人数、导航分组标题、成员职务、时间、
                            // 小按钮、导航计数：12px 常规
};

// 一档文字样式的字号与字重。
struct GmTextSpec
{
    int m_px;       // 96 DPI 下的字号（像素），绘制时经 GmFontPixelHeight 换算
    int m_weight;   // 字重，取值见 GmFontWeight
};

// 查字号表。
//   style：取值见 GmTextStyle；越界时按 kGmTextBody 处理。
// @return 该档的字号与字重。纯函数，无副作用。
GmTextSpec GmTextStyleSpec(int style);

// 群管理窗输入框（含搜索框）在各状态下的描边色，取自 design/群管理4/5.png：常态很浅的
// 灰，悬停加深一档，获得焦点时用品牌橙；禁用时用常态色（底色由 DuiEdit 自己置灰）。
//   enabled：是否可用；focused：是否持有键盘焦点；hover：鼠标是否悬停其上。
// @return 描边色。焦点优先于悬停。纯函数，无副作用。
COLORREF GmInputBorderColor(bool enabled, bool focused, bool hover);

// 量一段单行文字按某档样式绘制时的宽度（当前 DPI 下的像素）。字体、字号换算与
// GmDrawText 完全相同，量出的宽度与实际绘制一致。
//   text： 文本；空指针或空串返回 0。
//   style：文字样式，取值见 GmTextStyle。
// @return 宽度（像素）。取不到屏幕 DC 或建字体失败时返回 0。
int GmMeasureTextWidth(LPCWSTR text, int style);

// 返回群管理窗当前使用的 DPI。默认取系统 DPI —— 与窗口里 balloonui 控件
// （输入框、下拉框、徽章等）建字体所用的 DPI 相同，两边的文字因此处在同一尺度上。
// 单元测试可用 GmSetDpiOverride 指定。
int GmDpi();

// 仅供单元测试：强制 GmDpi 返回 dpi，使排版结果不随跑测试的机器变化。
//   dpi：要使用的 DPI；传 0 或负数恢复为取系统 DPI。
// 副作用：修改进程级状态，测试结束时须传 0 恢复。
void GmSetDpiOverride(int dpi);

// 把按 96 DPI 设计的尺寸换算成当前 DPI 下的物理像素（四舍五入）。
//   logicalPx：96 DPI 下的像素数，可为 0 或负数（负数照比例换算）。
// @return 当前 DPI 下的像素数。
int GmScale(int logicalPx);

// 把按 96 DPI 设计的字号换算成当前 DPI 下建字体用的像素高度。
// GmDrawText 与导航项建字体都经它换算，调用方不要再自行换算。
//   logicalPx：96 DPI 下的字号（像素）。
// @return 当前 DPI 下的字号（像素），即 LOGFONT::lfHeight 的绝对值。
int GmFontPixelHeight(int logicalPx);

// 用 Microsoft YaHei 在指定矩形内绘制单行 / 多行文字（GDI）。
//   hdc：        目标 DC；
//   text：       文本（nullptr / 空串不绘制）；
//   rc：         绘制矩形；
//   pixelHeight：96 DPI 下的字号（像素，<= 0 不绘制），内部经 GmFontPixelHeight
//                换算成当前 DPI 下的字号；
//   weight：     字重，取值见 GmFontWeight；
//   color：      文字颜色；
//   dtFlags：    DrawText 对齐 / 换行标志（如 DT_LEFT | DT_VCENTER | DT_SINGLELINE）。
void GmDrawText(HDC hdc, LPCWSTR text, const RECT& rc,
                int pixelHeight, int weight, COLORREF color, UINT dtFlags);

// 向 GDI+ 路径追加一个圆角矩形（半径自动夹到不超过半宽 / 半高）。
//   path：           目标路径；
//   x / y / w / h：  矩形左上角与宽高；
//   radius：         圆角半径。
void GmAddRoundRect(Gdiplus::GraphicsPath& path,
                    Gdiplus::REAL x, Gdiplus::REAL y,
                    Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius);

// 在 hdc 上画一个抗锯齿圆角矩形（可选填充 + 可选描边）。
//   hdc：         目标 DC；
//   rc：          矩形（设备坐标）；
//   radius：      圆角半径（像素）；
//   fill：        填充色；CLR_INVALID 表示不填充；
//   border：      描边色；CLR_INVALID 表示不描边；
//   borderWidth： 描边宽度（像素）。
void GmFillRoundRect(HDC hdc, const RECT& rc, Gdiplus::REAL radius,
                     COLORREF fill, COLORREF border, Gdiplus::REAL borderWidth);

} // namespace balloongroupmanage
