/**
 *  BadgeRender.h
 *
 *  会话列表"未读数字红点"（角标）的纯几何 / 文案计算。把"画在哪、画多宽、
 *  显示什么文字"这三件可预判的事抽成纯函数，供 MainPanelTreeView::OnPaint
 *  在会话行右下角叠绘红点时调用；同时让 FlamingoUnit 测试工程能脱离窗口 /
 *  GDI 直接断言（对照 PopupMenuPlacement 的纯函数 + 单测组织方式）。
 *
 *  真正的抗锯齿填充与文字光栅化在 MainPanelTreeView.cpp 里用 GDI+ / GDI
 *  完成，本模块只做坐标与字符串运算、不碰任何绘制 / 窗口 API。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#pragma once

#include <windows.h>      // RECT
#include <atlstr.h>       // CString

namespace balloonmain {

/**
 *  把未读消息数格式化成角标显示文字。
 *    count   未读消息数。
 *  返回：
 *    · count <= 0       → 空串（调用方据此不画角标）；
 *    · 1 .. 99          → 十进制数字本身（如 "7"）；
 *    · > 99             → "99+"（不在小红点里塞 3 位以上数字）。
 */
CString FormatBadgeText(int count);

/**
 *  按角标文字字符数算出红点外形的宽度（逻辑像素）。
 *
 *  单字符时为正圆（宽 = 直径）；多字符时横向拉成胶囊（药丸）形，每多一个
 *  字符在直径基础上加宽 extraPerChar，使数字横向容得下。
 *
 *    charCount     角标文字字符数（一般取 FormatBadgeText 结果的长度）。
 *    diameter      红点高度，即圆 / 胶囊的直径（逻辑像素）。
 *    extraPerChar  每超出 1 个字符增加的宽度（逻辑像素）。
 *  返回：红点外形宽度（逻辑像素）；charCount <= 1 时即为 diameter。
 */
int ComputeBadgeWidth(int charCount, int diameter, int extraPerChar);

/**
 *  算红点矩形 —— 右边缘对齐 rightEdge、并以 centerY 为垂直中心，宽 badgeWidth、
 *  高 diameter。用于把红点对到会话行"消息预览"那一行的高度（与左侧预览同高），
 *  时间则单独画在上一行（与姓名同高）。
 *
 *    rightEdge   红点右边缘的 x 坐标（客户区坐标，一般 = 行右边 - 右内距）。
 *    badgeWidth  红点外形宽度（来自 ComputeBadgeWidth）。
 *    diameter    红点高度（圆 / 胶囊直径）。
 *    centerY     红点的垂直中心 y 坐标（客户区坐标，取预览行的垂直中心）。
 *  返回：红点矩形（客户区坐标）。
 */
RECT ComputeBadgeRect(int rightEdge, int badgeWidth, int diameter, int centerY);

/**
 *  算置顶会话行左侧那根"强调竖条"的矩形 —— 贴着行的左边缘、在行内垂直居中，
 *  宽 barWidth、高为行高的 heightPercent%。
 *
 *  竖条是置顶行的主要视觉标记（另配一层很淡的行底色）。之所以贴左边缘而不是像
 *  未读角标那样放右侧：右下角已经排了静音图标与未读胶囊，右上角是时间，只有左
 *  边缘这一条是空的，而且竖条压在行的内容左侧空白上，不占内容宽度、不会把头像
 *  和文字往右挤。
 *
 *    rowRect        整行的矩形（客户区坐标）。
 *    barWidth       竖条宽度（逻辑像素）；<= 0 时返回空矩形（调用方据此不画）。
 *    heightPercent  竖条高度占行高的百分比，取值 1..100，超出范围按边界值截取；
 *                   取 100 即与行等高。
 *  返回：竖条矩形（客户区坐标）；行高 <= 0 或 barWidth <= 0 时为空矩形（左右 /
 *        上下均相等），调用方可用 right <= left 判空。
 */
RECT ComputePinnedAccentRect(const RECT& rowRect, int barWidth, int heightPercent);

} // namespace balloonmain
