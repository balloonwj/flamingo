/**
 *  ChatIcons.h
 *
 *  单聊窗用到的一组线性图标的 GDI+ 描边绘制 helper。图标形状取自 design
 *  的 icons.jsx（统一 viewBox 24、圆头圆角描边）。头部按钮、输入工具栏、
 *  气泡回执等处都从这里取图标。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 <windows.h>。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include <gdiplus.h>

namespace balloonchatwnd {

/**
 *  图标类型。逐值对应 design icons.jsx 里的同名图标。
 */
enum ChatIconKind
{
    kChatIconPhone   = 0,   // 电话听筒 —— 语音通话
    kChatIconVideo   = 1,   // 摄像机 —— 视频通话
    kChatIconHistory = 2,   // 时钟 —— 消息记录
    kChatIconMore    = 3,   // 三点 —— 更多 / 详情
    kChatIconEmoji   = 4,   // 笑脸 —— 表情
    kChatIconImage   = 5,   // 图片
    kChatIconFile    = 6,   // 文件
    kChatIconCut     = 7,   // 剪刀 —— 截图
    kChatIconMic     = 8,   // 麦克风 —— 语音
    kChatIconPlus    = 9,   // 加号 —— 更多
    kChatIconCheck   = 10,  // 对勾 —— 已读回执
    kChatIconSend    = 11,  // 纸飞机 —— 发送
    kChatIconSettings = 12, // 齿轮 —— 群管理
    kChatIconShake   = 13,  // 抖动的小窗 + 两侧运动线 —— 窗口抖动
    kChatIconFont    = 14,  // 大写字母 A —— 字体设置
    kChatIconRemoteDesktop = 15,  // 显示器 —— 远程桌面
    kChatIconPin     = 16,  // 图钉 —— 被 Pin 住的消息（画在气泡右上角外侧）
    kChatIconFolder  = 17,  // 文件夹 + 向上箭头 —— 发送文件夹
};

/**
 *  在指定区域内绘制一个图标。
 *
 *    g：          目标 Graphics（调用方应已开启抗锯齿）。
 *    kind：       图标类型，取值见 ChatIconKind；越界值不绘制。
 *    x / y：      绘制区域左上角（设备坐标）。
 *    size：       绘制区域边长（图标按 viewBox 24 等比缩放到此尺寸）。
 *    color：      图标颜色。
 *    strokeWidth：描边宽度，以 viewBox 24 为基准的单位（一般取 2.0）。
 */
void DrawChatIcon(Gdiplus::Graphics& g, int kind,
                  Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                  const Gdiplus::Color& color, Gdiplus::REAL strokeWidth);

} // namespace balloonchatwnd
