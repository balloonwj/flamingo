/**
 *  文件消息的视觉共用件，ChatFileVisual.h
 *
 *  眼下只有一件事：按扩展名挑类型色块的颜色。聊天窗的文件气泡与「消息记录」窗列表行
 *  都要画这个色块，配色必须一致 —— 同一个 .xlsx 在两处显示成不同的绿，用户会以为是
 *  两种东西。故从 ChatBubble.cpp 里那个文件内局部函数抽出来共用。
 *
 *  纯逻辑，不碰窗口、不碰 GDI，可被 FlamingoUnit 测试工程直接调用。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（要用 CString）。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#pragma once

namespace balloonchatwnd {

/**
 *  按小写扩展名挑文件类型色块的底色。
 *
 *  已知的几类各给一个辨识度高的颜色，其余一律回落到中性灰 —— 与其为每种扩展名硬凑
 *  一个颜色，不如让"没见过的类型"长得一致，反倒不会误导。
 *
 *    type：小写扩展名（不含点），如 "xlsx"；空串或未知类型返回中性灰。
 *  @return 色块底色。
 */
COLORREF FileTypeColor(const CString& type);

} // namespace balloonchatwnd
