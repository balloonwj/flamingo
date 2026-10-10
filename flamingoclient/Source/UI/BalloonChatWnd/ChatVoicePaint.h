/**
 *  ChatVoicePaint.h
 *
 *  语音条：聊天窗语音气泡里的内容（喇叭图标、波形、时长、未听红点）的尺寸常量与绘制（语音消息第 4 期）。
 *  聊天气泡（ChatBubble）与消息记录窗、合并记录查看窗里的语音条控件（ChatVoiceStrip）共用这一份，几处外观一致。
 *  语音条的底色由调用方画（气泡按收发方向取色），本文件只画底色上面的内容；发送中的圆环、发送失败的红色感叹号
 *  只有聊天窗有，留在 ChatBubble 里画。
 *
 *  绘制分两层，与气泡的绘制顺序对应：先用 GDI+ 画图形（图标、波形、红点），再用 GDI 画时长文字。
 *
 *  典型用法：
 *      RECT bar = { x, y, x + VoiceBubbleWidth(msg.voiceDuration), y + height };
 *      PaintVoiceBarShapes(g, bar, msg, true);   //true：未听时在语音条右侧画红点
 *      PaintVoiceBarText(hdc, bar, msg);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL；调用绘制函数的 .cpp 要自己包含 <gdiplus.h>。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <windows.h>
#include "ChatMockData.h"   // ChatMessage

//只用到 Graphics 的引用，前置声明即可，包含本文件的地方不必先包含 <gdiplus.h>
namespace Gdiplus {
class Graphics;
}

namespace balloonchatwnd {

// ---- 语音条的几何（像素）。宽度随时长变化，见 ChatVoiceBubble.h 的 VoiceBubbleWidth ----
const int kVoicePadH          = 12;   // 语音条左右内边距
const int kVoiceIconSize      = 18;   // 喇叭图标边长
const int kVoiceGap           = 8;    // 图标、波形、时长之间的间距
const int kVoiceDurationW     = 40;   // 时长文字区的宽度，容得下「1′05″」
const int kVoiceDurationPx    = 13;   // 时长文字的字号
const int kVoiceBarW          = 2;    // 波形每根柱子的宽度
const int kVoiceBarGap        = 2;    // 波形柱子之间的间距
const int kVoiceBarMaxH       = 18;   // 波形柱子的最大高度（音量 255）
const int kVoiceBarMinH       = 3;    // 波形柱子的最小高度（音量 0）
const int kVoiceLevelMax      = 255;  // 音量的上限
const int kVoiceDotSize       = 8;    // 未听红点的直径
const int kVoiceDotGap        = 6;    // 红点与语音条右边之间的水平间距

/**
 *  语音条里画得下几根波形柱：喇叭图标右边到时长文字左边之间的宽度，按每根 kVoiceBarW、间距 kVoiceBarGap 排。
 *  PaintVoiceBarShapes 按它画，单元测试按它检查短语音也有波形（语音消息第 6b 步）。
 *  @param barWidth 语音条宽度（像素），即 VoiceBubbleWidth 的结果
 *  @return 波形柱根数；放不下一根时为 0
 */
int VoiceWaveBarCount(int barWidth);

/**
 *  画语音条的图形部分：左侧喇叭图标（播放中按 voicePlayFrame 轮换 1～3 道声波，不在播放时画满 3 道）、
 *  中间的波形（从消息的波形值里按可用宽度等距取样）；drawDot 为 true 且是对方发来、还没听过的语音时，
 *  在语音条右侧外边画红点。
 *  @param g       目标 GDI+ 画布（调用方已设好抗锯齿）
 *  @param bar     语音条矩形（与气泡底色的矩形相同）
 *  @param m       语音消息（kind 为 kMsgVoice）
 *  @param drawDot 是否按已听状态画未听红点；合并记录查看窗里的语音没有已听状态，传 false
 */
void PaintVoiceBarShapes(Gdiplus::Graphics& g, const RECT& bar, const ChatMessage& m, bool drawDot);

/**
 *  画语音条的文字部分：右侧的时长（如「8″」「1′05″」）。
 *  @param hdc 目标设备上下文
 *  @param bar 语音条矩形，与 PaintVoiceBarShapes 收到的相同
 *  @param m   语音消息
 */
void PaintVoiceBarText(HDC hdc, const RECT& bar, const ChatMessage& m);

} // namespace balloonchatwnd
