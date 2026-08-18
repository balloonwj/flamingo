/**
 *  ContactIcons.h
 *
 *  联系人资料对话框用到的一组线性图标的 GDI+ 描边绘制 helper：
 *    · edit    铅笔 —— 「备注名」行右侧的编辑小按钮；
 *    · chat    聊天气泡 —— 底部「发消息」按钮；
 *    · phone / video / history 电话听筒 / 摄像机 / 时钟 —— 快捷操作行的前三个按钮；
 *    · locate  地图定位针 —— 快捷操作行「组织架构」按钮。
 *  图标形状取自 design 的 icons.jsx（统一 viewBox 24、圆头圆角描边），
 *  画法对照 BalloonChatWnd/ChatIcons.cpp 的同类实现。
 *
 *  其中 phone / video / history 三个图标 BalloonChatWnd/ChatIcons.h 里已有同款
 *  （kChatIconPhone / kChatIconVideo / kChatIconHistory），本模块直接转发过去，
 *  不再照着重画一份 —— 两处各画一份必然随时间走样。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入
 *  <windows.h> 与 <gdiplus.h>。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include <gdiplus.h>

namespace ballooncontact {

/**
 *  图标类型。逐值对应 design icons.jsx 里的同名图标。
 */
enum ContactIconKind
{
    kContactIconEdit    = 0,   // 铅笔 —— 「备注名」行的行内编辑按钮
    kContactIconChat    = 1,   // 聊天气泡 —— 底部「发消息」按钮
    kContactIconPhone   = 2,   // 电话听筒 —— 快捷操作行「语音通话」
    kContactIconVideo   = 3,   // 摄像机 —— 快捷操作行「视频通话」
    kContactIconHistory = 4,   // 时钟 —— 快捷操作行「历史消息」
    kContactIconLocate  = 5,   // 地图定位针 —— 快捷操作行「组织架构」
};

/**
 *  在指定区域内绘制一个图标。
 *
 *    g：          目标 Graphics（调用方应已开启抗锯齿）。
 *    kind：       图标类型，取值见 ContactIconKind；越界值不绘制。
 *    x / y：      绘制区域左上角（设备坐标）。
 *    size：       绘制区域边长（图标按 viewBox 24 等比缩放到此尺寸）。
 *    color：      图标颜色。
 *    strokeWidth：描边宽度，以 viewBox 24 为基准的单位（一般取 1.6 ~ 2.0）。
 */
void DrawContactIcon(Gdiplus::Graphics& g, int kind,
                     Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                     const Gdiplus::Color& color, Gdiplus::REAL strokeWidth);

} // namespace ballooncontact
