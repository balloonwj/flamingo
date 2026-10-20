/**
 *  ChatCardStyle.h
 *
 *  聊天消息列表里各类卡片共用的样式常量：颜色、尺寸、字号（docs/聊天卡片美化方案.md 第 3 节）。
 *  各类卡片（文件、文件夹、共享与通话、群公告、备忘、接龙、合并转发、邀请、Markdown、代码片段）都引用这一份，
 *  不再各写一套相近的色值；绘制函数在 ChatCardPaint，类型与状态的对应关系在 ChatCardModel。
 *
 *  只有常量，不依赖 GDI+；颜色一律写成 COLORREF，需要 Gdiplus::Color 的地方由绘制代码转换。
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 windows.h。
 *
 *  balloonwj@qq.com   2026-10-14
 */
#pragma once

namespace balloonchatwnd { namespace cardstyle {

// ---- 卡片底板（第 3.2 节）----

const int kCardWidth = 300;                               // 卡片固定宽（逻辑像素）；代码片段按内容自适应，不用它
const int kCardRadius = 10;                               // 底板圆角半径
const COLORREF kCardFill = RGB(0xFF, 0xFF, 0xFF);         // 底板底色：本人与对方的卡片都是白色
const COLORREF kCardBorder = RGB(0xE9, 0xE5, 0xE0);       // 1 像素暖灰边框
const COLORREF kCardShadow = RGB(0x46, 0x32, 0x1E);       // 投影颜色（暖灰），按 kCardShadowAlpha 的不透明度画
const int kCardShadowAlpha = 18;                          // 投影不透明度（0~255，约 7%）
const int kCardShadowOffsetY = 1;                         // 投影相对底板向下偏移的像素数
const int kCardPadTop = 14;                               // 底板上内边距
const int kCardPadH = 14;                                 // 底板左右内边距（每侧）
const int kCardPadBottom = 12;                            // 没有底部一行时的下内边距

// ---- 头部：图标块 + 标题 + 副标题（第 3.1、3.3 节）----

const int kTileSize = 36;                                 // 图标块边长
const int kTileRadius = 9;                                // 图标块圆角半径
const int kTileGlyphSize = 20;                            // 图标块内线条图标的绘制边长（ChatIcons 按 viewBox 24 缩放到此尺寸）
const float kTileGlyphStroke = 1.8f;                      // 图标块内线条图标的描边宽度（viewBox 24 单位）
const int kTileTextGap = 10;                              // 图标块与右侧标题之间的间距
const int kTitlePx = 14;                                  // 标题字号（半粗）
const int kTitleLineH = 20;                               // 标题行高
const COLORREF kTitleColor = RGB(0x1F, 0x23, 0x29);       // 标题文字色
const int kSubtitlePx = 12;                               // 副标题字号
const int kSubtitleLineH = 18;                            // 副标题行高
const COLORREF kSubtitleColor = RGB(0x86, 0x86, 0x8B);    // 副标题与信息行的文字色

// 已结束的屏幕共享、群通话：图标块改成灰色（第 3.1 节）
const COLORREF kTileEndedFill = RGB(0xF1, 0xEF, 0xEC);    // 灰色图标块的底色
const COLORREF kTileEndedGlyph = RGB(0x9A, 0x9A, 0x9F);   // 灰色图标块里的图标颜色

// ---- 说明块与信息行（第 3.3 节）----

const int kDescTopGap = 10;                               // 头部与说明块之间的间距
const int kDescRadius = 6;                                // 说明块圆角半径
const int kDescPadH = 10;                                 // 说明块左右内边距（每侧）
const int kDescPadV = 7;                                  // 说明块上下内边距（每侧）
const int kDescPx = 12;                                   // 说明块字号
const int kDescLineH = 18;                                // 说明块行高
const COLORREF kDescFill = RGB(0xF7, 0xF5, 0xF2);         // 说明块底色
const COLORREF kDescText = RGB(0x5E, 0x5E, 0x62);         // 说明块文字色
const COLORREF kDescStrongText = RGB(0x3E, 0x3E, 0x42);   // 说明块里需要加深的文字（合并记录预览里的发送者名）
const int kMetaTopGap = 10;                               // 上方内容与信息行（时长、人数）之间的间距
const int kMetaLineH = 20;                                // 信息行行高（与状态标签同高）
const int kMetaGap = 8;                                   // 信息行里状态标签与右侧文字之间的间距

// ---- 条目列表（接龙名单，docs/聊天卡片美化方案.md 5.3）----

const int kEntriesTopGap = 10;                            // 上方内容与条目列表之间的间距
const int kEntryLineH = 22;                               // 每条的行高
const int kEntryPx = 13;                                  // 条目文字字号
const int kEntryIndexW = 16;                              // 左侧序号列的宽度（序号右对齐）
const int kEntryIndexGap = 8;                             // 序号列与条目文字之间的间距
const COLORREF kEntryIndexColor = RGB(0xA3, 0xA1, 0xA6);  // 序号颜色
const COLORREF kEntryText = RGB(0x2A, 0x2A, 0x2A);        // 条目文字颜色
const COLORREF kEntryEmptyText = RGB(0x86, 0x86, 0x8B);   // 一条都没有时那行提示的颜色

// ---- 状态标签（第 3.4 节）----

const int kChipH = 20;                                    // 标签高
const int kChipRadius = 10;                               // 标签圆角半径（两端半圆）
const int kChipPx = 11;                                   // 标签字号
const int kChipPadH = 8;                                  // 标签左右内边距（每侧）
const int kChipDotSize = 6;                               // 标签文字前圆点的直径
const int kChipDotGap = 5;                                // 圆点与文字之间的间距
const COLORREF kChipLiveText = RGB(0x1F, 0x9D, 0x5A);     // 进行中：文字与圆点
const COLORREF kChipLiveFill = RGB(0xE7, 0xF6, 0xEE);     // 进行中：底色
const COLORREF kChipPendingText = RGB(0xE0, 0x63, 0x1F);  // 待处理、倒计时：文字与圆点
const COLORREF kChipPendingFill = RGB(0xFF, 0xF0, 0xE6);  // 待处理、倒计时：底色
const COLORREF kChipBadText = RGB(0xD9, 0x3C, 0x3C);      // 拒绝、重要、失败：文字与圆点
const COLORREF kChipBadFill = RGB(0xFD, 0xED, 0xED);      // 拒绝、重要、失败：底色
const COLORREF kChipEndedText = RGB(0x86, 0x86, 0x8B);    // 已结束：文字与圆点
const COLORREF kChipEndedFill = RGB(0xF1, 0xEF, 0xEC);    // 已结束：底色
const COLORREF kChipNeutralText = RGB(0x4A, 0x6C, 0xD8);  // 中性提示：文字与圆点
const COLORREF kChipNeutralFill = RGB(0xEC, 0xF0, 0xFD);  // 中性提示：底色

// ---- 按钮（第 3.5 节）----

const int kButtonH = 28;                                  // 胶囊按钮高
const int kButtonRadius = 14;                             // 胶囊按钮圆角半径（两端半圆）
const int kButtonPx = 13;                                 // 按钮字号
const int kButtonPadH = 14;                               // 主要、查看按钮的左右内边距（每侧）
const int kButtonGap = 14;                                // 相邻按钮之间的间距
const COLORREF kPrimaryFill = RGB(0xFF, 0x7A, 0x45);      // 主要按钮：底色（品牌橙）
const COLORREF kPrimaryHoverFill = RGB(0xF2, 0x6A, 0x33); // 主要按钮：悬停底色
const COLORREF kPrimaryText = RGB(0xFF, 0xFF, 0xFF);      // 主要按钮：文字（半粗）
const COLORREF kPrimaryDisabledFill = RGB(0xF1, 0xEF, 0xEC);   // 主要按钮置灰：底色
const COLORREF kPrimaryDisabledText = RGB(0xB4, 0xB1, 0xAC);   // 主要按钮置灰：文字
const COLORREF kViewBorder = RGB(0xFF, 0xC3, 0xA8);       // 查看按钮：边框
const COLORREF kViewText = RGB(0xF2, 0x6A, 0x33);         // 查看按钮：文字
const COLORREF kViewFill = RGB(0xFF, 0xFF, 0xFF);         // 查看按钮：底色
const COLORREF kViewHoverFill = RGB(0xFF, 0xF0, 0xE8);    // 查看按钮：悬停底色
const COLORREF kViewDisabledBorder = RGB(0xE9, 0xE5, 0xE0);    // 查看按钮置灰：边框
const COLORREF kViewDisabledText = RGB(0xB4, 0xB1, 0xAC);      // 查看按钮置灰：文字
const COLORREF kSecondaryText = RGB(0x5E, 0x5E, 0x62);    // 次要按钮：文字（没有边框与底色）
const COLORREF kSecondaryHoverText = RGB(0x1F, 0x23, 0x29);    // 次要按钮：悬停文字
const COLORREF kSecondaryDisabledText = RGB(0xC2, 0xBF, 0xBA); // 次要按钮置灰：文字

// ---- 底部一行（第 3.6 节）----

const int kFooterH = 40;                                  // 底部一行的高度（不含上方分隔线）
const int kFooterDividerH = 1;                            // 底部一行上方分隔线的厚度，画在底部一行上方紧邻的一行
const COLORREF kFooterDivider = RGB(0xF0, 0xEC, 0xE7);    // 底部一行上方的 1 像素分隔线
const int kFooterLabelIconSize = 13;                      // 左侧类型标签前的图标边长
const int kFooterLabelIconGap = 5;                        // 类型标签图标与文字之间的间距
const int kFooterLabelPx = 12;                            // 类型标签字号
const COLORREF kFooterLabelText = RGB(0x9A, 0x9A, 0x9F);  // 类型标签文字色
const int kFooterLabelButtonGap = 10;                     // 类型标签与最左边一个按钮之间至少留出的间距

// ---- 进度条（第 3.7 节）----

const int kProgressTopGap = 12;                           // 头部与进度条之间的间距
const int kProgressH = 4;                                 // 进度条厚
const int kProgressRadius = 2;                            // 进度条圆角半径
const COLORREF kProgressTrack = RGB(0xEE, 0xEA, 0xE5);    // 进度条底槽
const COLORREF kProgressFill = RGB(0xFF, 0x7A, 0x45);     // 进度条已完成部分（品牌橙；上传、下载、文件夹传输一律用它）
const int kProgressTextGap = 6;                           // 进度条与下方一行文字之间的间距
const int kProgressTextLineH = 18;                        // 进度条下方一行文字（已传 / 总大小、当前文件名、百分比）的行高

// ---- 语音（第 3.7 节）----

const COLORREF kUnreadDot = RGB(0xE5, 0x48, 0x4D);        // 语音消息未听红点

}} // namespace balloonchatwnd::cardstyle
