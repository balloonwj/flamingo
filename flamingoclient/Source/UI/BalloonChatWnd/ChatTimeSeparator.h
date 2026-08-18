/**
 *  聊天消息「时间分隔行」的判定与文案，ChatTimeSeparator.h
 *
 *  消息流里不给每条消息都标时间，而是在两条消息间隔较久时插入一行居中的灰色小字
 *  （2026-08-12 与用户确认的形态）。这么做的理由：每条都标会让满屏都是时间，而且短
 *  消息（比如只回一个"好"字）的气泡会被时间撑宽，很别扭。
 *
 *  本文件只管两件纯逻辑的事 —— <u>要不要插</u>、<u>插的话写什么</u>，不碰控件、不读
 *  时钟（当前时刻由调用方传入），因此可以完全用单元测试覆盖（见
 *  flamingoclient/Source/Tests/ChatTimeSeparatorTests.cpp，Suite CTS）。真正把分隔行
 *  插进列表、测量与绘制，由消息列表那边负责。
 *
 *  <u>往上翻历史消息时也要重算</u>：新插入的历史消息与原有首条之间同样可能需要一行
 *  分隔。只在追加新消息时判定的话，翻上去会看到时间行断档。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <atlstr.h>       // CString

namespace balloonchatwnd {

/**
 *  插入分隔行的时间间隔阈值（毫秒）。
 *
 *  取 5 分钟：连续对话时（间隔通常几秒到几十秒）一行都不插，界面干净；聊着聊着停下
 *  再回来，就会有一行标出"这是隔了一会儿之后的消息"。这个量级与国内常见 IM 一致。
 */
const long long kChatTimeSeparatorThresholdMs = 5LL * 60LL * 1000LL;

/**
 *  判定两条相邻消息之间要不要插一行时间分隔。
 *
 *    prevMs：    上一条消息的时间（Unix 毫秒）。<u>为 0 表示没有上一条</u>（本条是
 *                消息流的第一条），此时一律要插 —— 开头总该有个时间。
 *    curMs：     本条消息的时间（Unix 毫秒）。为 0 或负数视为时间未知，不插。
 *    thresholdMs：间隔阈值，一般传 kChatTimeSeparatorThresholdMs。
 *
 *  @return 要插返回 true。判定用 >= ：正好满阈值就算"隔了一会儿"。
 *          两条消息时间倒挂（本条早于上一条，服务器时间回拨或乱序时可能出现）也返回
 *          true —— 时间倒着走本身就该让用户看见，闷着不显示只会更费解。
 */
bool ShouldInsertTimeSeparator(long long prevMs, long long curMs, long long thresholdMs);

/**
 *  把消息时间格式化成分隔行上显示的文案。
 *
 *  分档（与公告列表的口径一致，避免同一个时间在两处显示成不同样子）：
 *    · 今天    —— "HH:MM"
 *    · 昨天    —— "昨天 HH:MM"
 *    · 今年更早 —— "MM-DD HH:MM"
 *    · 往年    —— "YYYY-MM-DD HH:MM"（跨年之后光看月日分不清是哪一年）
 *
 *    timestampMs：消息时间（Unix 毫秒）；<= 0 或无法转换时返回空串。
 *    nowMs：      当前时刻（Unix 毫秒），由调用方传入而非本函数去读时钟 —— 这样
 *                 "今天 / 昨天"的判定可以被测试固定住。
 *
 *  @return 展示文案（UTF-16）；无法格式化时返回空串，调用方据此不插这一行。
 */
CString FormatChatSeparatorTime(long long timestampMs, long long nowMs);

} // namespace balloonchatwnd
