/**
 *  聊天气泡美化的样式常量（docs/聊天气泡美化方案.md 第 2 节）：同一人连续消息的合组、气泡尖角、对方气泡底色、正文行高、
 *  提示行胶囊、图片与引用块、多选时的行底色、@ 高亮与发送失败原因的颜色。聊天窗的气泡（ChatBubble）、聊天记录管理器的
 *  语音条（ChatVoiceStrip）与测试共用这一份，不再各写一套相近的色值。
 *
 *  只有常量与一个纯函数，不依赖窗口；包含它之前调用方须已引入 <windows.h>（要用 COLORREF / RGB）。典型用法：
 *      const int lineH = bubblestyle::TextLineHeight(14);   // 22
 *
 *  balloonwj@qq.com   2026-10-14
 */
#pragma once

namespace balloonchatwnd { namespace bubblestyle {

// ---- 同一人连续消息合组（第 1 条） ----

// 两条相邻消息能合成一组的最大时间间隔（毫秒）：3 分钟。正好 3 分钟仍合组
const long long kGroupMaxGapMs = 3LL * 60LL * 1000LL;
// 与同组相邻消息之间那一侧的行外边距（像素）。上下两行各留 2，两条气泡之间合起来是 4；不在组内时仍是各 7（合起来 14）
const int kGroupedHalfGap = 2;

// ---- 气泡尖角（第 3 条） ----

// 每组第一条气泡靠头像那一角（对方为左上角、本人为右上角）的圆角半径（像素）；其余各角仍是 12
const int kAvatarCornerRadius = 4;

// ---- 对方气泡底色（第 2 条） ----

// 对方文字、语音气泡的底色 #F0EEEA；聊天记录管理器里的语音条同用这个颜色
const COLORREF kPeerBubbleFill = RGB(0xF0, 0xEE, 0xEA);

// ---- 正文行高（第 4 条） ----

// 正文行高与字号之比的分子、分母：行高 = 字号 × 11 / 7（四舍五入），14 像素正文为 22
const int kLineHeightNum = 11;
const int kLineHeightDen = 7;
// 文字气泡的上下内边距（像素，每侧）；单行气泡高 22 + 8 × 2 = 38
const int kTextPadV = 8;

/**
 *  正文没有表情的行的行高。
 *  @param fontPx 正文字号（像素），取值 1 及以上；小于 1 时按 1 处理
 *  @return 行高（像素），字号 × 11 / 7 四舍五入；14 像素为 22
 */
inline int TextLineHeight(int fontPx)
{
    if (fontPx < 1)
    {
        fontPx = 1;
    }
    return (fontPx * kLineHeightNum + kLineHeightDen / 2) / kLineHeightDen;
}

// ---- 提示行胶囊（第 5 条）：时间分隔、系统提示、撤回提示、接龙提示 ----

// 胶囊底色 #F0EEEA 与文字色 #8F8B85
const COLORREF kTipPillFill = RGB(0xF0, 0xEE, 0xEA);
const COLORREF kTipPillText = RGB(0x8F, 0x8B, 0x85);
// 胶囊高与左右内边距（像素，每侧）；圆角为高的一半
const int kTipPillH = 20;
const int kTipPillPadH = 10;
// 胶囊离行左右两边的最小距离（像素）：文字再长，胶囊也不超出这个范围，放不下的文字末尾加省略号
const int kTipPillSideMargin = 48;
// 撤回提示里「编辑」的颜色（品牌橙 #F26A33，原为蓝色 #2F6FD0）
const COLORREF kRecallEditColor = RGB(0xF2, 0x6A, 0x33);

// ---- 图片（第 6 条） ----

// 图片的圆角半径（像素）
const int kImageRadius = 8;
// 图片 1 像素细边框的颜色（黑色）与不透明度（0~255，约 6%）
const COLORREF kImageBorder = RGB(0x00, 0x00, 0x00);
const int kImageBorderAlpha = 15;

// ---- 引用块（第 7 条） ----

// 引用块白底的不透明度（0~255，60%），铺在气泡底色之上
const int kQuoteFillAlpha = 153;
// 引用块白底在竖线一侧与另一侧的圆角半径（像素）
const int kQuoteFillRadiusBar = 2;
const int kQuoteFillRadius = 6;
// 引用块白底右侧留给文字的内边距（像素）：被引用内容过长时省略号不贴着白底边缘
const int kQuoteFillPadR = 6;
// 左侧竖线的颜色与宽度（像素）
const COLORREF kQuoteBar = RGB(0xCB, 0xC6, 0xBF);
const int kQuoteBarW = 2;
// 竖线与文字之间的间距（像素）：竖线收窄 1 像素，间距加 1，文字位置不变
const int kQuoteBarGap = 8;
// 人名（半粗）与被引用内容的颜色
const COLORREF kQuoteName = RGB(0x5E, 0x5E, 0x62);
const COLORREF kQuoteText = RGB(0x8A, 0x8A, 0x8A);

// ---- 多选（第 8 条） ----

// 多选模式下被勾选的消息整行的底色
const COLORREF kSelectedRowFill = RGB(0xFF, 0xF3, 0xEC);

// ---- @ 高亮（第 9 条） ----

// 对方气泡里「@我」的文字色与底色块
const COLORREF kAtMeFg = RGB(0xD9, 0x54, 0x1C);
const COLORREF kAtMeBg = RGB(0xFF, 0xE0, 0xCF);
// 「@别人」的文字色与底色块（两种气泡共用）
const COLORREF kAtOtherFg = RGB(0x3B, 0x6F, 0xD6);
const COLORREF kAtOtherBg = RGB(0xE6, 0xEE, 0xFC);
// 底色块的圆角半径（像素，原为 3）
const int kAtRadius = 4;

// ---- 发送失败（第 10 条） ----

// 发送失败原因的文字色
const COLORREF kFailReasonText = RGB(0xD9, 0x3C, 0x3C);

}} // namespace balloonchatwnd::bubblestyle
