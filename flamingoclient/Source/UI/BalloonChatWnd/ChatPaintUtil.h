/**
 *  ChatPaintUtil.h
 *
 *  聊天气泡与文件卡片共用的几个绘制工具函数：圆角矩形路径、GDI 画文字、量文字宽度。
 *  原先是 ChatBubble.cpp 的文件内函数；把文件卡片的绘制抽成共用组件（ChatFileCard）时，
 *  两边要画出逐像素相同的结果，故移到这里共用同一份实现。
 *
 *  放在子命名空间 balloonchatwnd::chatpaint 里：本目录不少文件各自有同名的文件内函数，
 *  用子命名空间避免名字混淆。调用方用 using 声明引入即可：
 *      using chatpaint::DrawGdiText;
 *      DrawGdiText(hdc, L"文字", rc, 14, false, RGB(0, 0, 0), DT_LEFT | DT_SINGLELINE);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL 与 <gdiplus.h>。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

namespace balloonchatwnd { namespace chatpaint {

//DrawGdiText 与 MeasureTextWidth 的 bold=true 所用字重。界面字体（微软雅黑）没有半粗字形，GDI 会
//人为加粗，每个字比常规字重宽约 1 像素；而 FW_BOLD 取的是自带的粗体字形，中文字宽与常规相同。
//两个函数必须用同一个字重，量出的宽度才与画出来的一致（2026-10-05 实测 11 像素「对方已接收」：
//常规 55、FW_SEMIBOLD 60、FW_BOLD 55）
const int kChatTextBoldWeight = FW_SEMIBOLD;

/**
 *  向 path 添加一个圆角矩形。圆角半径超过宽或高的一半时按一半处理。
 *  @param path   [出参] 要添加图形的路径
 *  @param x / y  左上角坐标（像素）
 *  @param w / h  宽与高（像素）
 *  @param radius 圆角半径（像素）
 */
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius);

/**
 *  往 path 里加一个四个角各自给半径的圆角矩形（聊天气泡靠头像那一角收小时用，docs/聊天气泡美化方案.md 第 1 节第 3 条）。
 *  每个角的半径都限制在宽、高各一半以内；半径为 0 的角画成直角。
 *  @param path        [入出参] 目标路径
 *  @param x / y       左上角
 *  @param w / h       宽与高（像素）
 *  @param rTopLeft    左上角半径（像素）
 *  @param rTopRight   右上角半径（像素）
 *  @param rBottomRight 右下角半径（像素）
 *  @param rBottomLeft 左下角半径（像素）
 */
void AddRoundRectCorners(Gdiplus::GraphicsPath& path,
                         Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL w, Gdiplus::REAL h,
                         Gdiplus::REAL rTopLeft, Gdiplus::REAL rTopRight,
                         Gdiplus::REAL rBottomRight, Gdiplus::REAL rBottomLeft);

/**
 *  用 GDI 在 rc 里画一段文字，字体族固定为微软雅黑，背景透明。
 *  @param hdc         目标设备上下文
 *  @param text        文字；为 NULL 或空串时不画
 *  @param rc          文字区域
 *  @param pixelHeight 字号（像素）；<=0 时不画
 *  @param bold        是否加粗（字重为 kChatTextBoldWeight）
 *  @param color       文字颜色
 *  @param dtFlags     DrawText 的格式标志
 *  @param italic / underline / strike 斜体、下划线、删除线，默认都关
 */
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags,
                 bool italic = false, bool underline = false, bool strike = false);

/**
 *  量一段单行文字的像素宽度（界面字体）。
 *  @param hdc         用于量宽的设备上下文
 *  @param text        文字；为 NULL 或空串时返回 0
 *  @param pixelHeight 字号（像素）
 *  @param bold        是否按加粗字重量宽，须与 DrawGdiText 绘制时的 bold 一致：加粗字形每个字都更宽，
 *                     按常规字重量出的宽度会偏小，紧随其后画的内容就会压到文字上
 *  @return 文字宽度（像素）
 */
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight, bool bold = false);

//「P2P」徽章与它前面那段文字之间的间距（像素）
const int kP2PBadgeGapPx = 6;
//「P2P」徽章文字的字号（像素，加粗）
const int kP2PBadgeFontPx = 10;

/**
 *  在一行里画一枚「P2P」徽章（2026-10-04 起，样式见 UI/BalloonMain/P2PBadge.h），在 rowTop ~ rowBottom 之间垂直居中。
 *  @param hdc       目标设备上下文
 *  @param left      徽章左边的 x
 *  @param rowTop    这一行的上边
 *  @param rowBottom 这一行的下边
 *  @param maxRight  徽章右边不能超过的 x；放不下时不画
 *  @return 画了返回徽章右边的 x；没画返回 left
 */
int DrawP2PBadgeInRow(HDC hdc, int left, int rowTop, int rowBottom, int maxRight);

//「机器人」徽章与它前面那段文字之间的间距（像素）
const int kBotBadgeGapPx = 4;
//「机器人」徽章文字的字号（像素）
const int kBotBadgeFontPx = 10;
//「机器人」徽章的底色（蓝）与文字色（白），docs/机器人方案.md §4.2
const COLORREF kBotBadgeBgColor = RGB(0x1E, 0x88, 0xE5);
const COLORREF kBotBadgeTextColor = RGB(0xFF, 0xFF, 0xFF);

/**
 *  在一行里画一枚文字徽章（两端为半圆的圆角矩形、底色上居中的文字），在 rowTop ~ rowBottom 之间垂直居中。
 *  群聊气泡在机器人发送者的名字后面用它画「机器人」，成员列表的机器人行也用它。
 *  @param hdc       目标设备上下文
 *  @param text      徽章文字；为 NULL 或空串时不画
 *  @param bgColor   底色
 *  @param textColor 文字色
 *  @param left      徽章左边的 x
 *  @param rowTop    这一行的上边
 *  @param rowBottom 这一行的下边
 *  @param maxRight  徽章右边不能超过的 x；放不下时不画
 *  @return 画了返回徽章右边的 x；没画返回 left
 */
int DrawTextBadgeInRow(HDC hdc, LPCWSTR text, COLORREF bgColor, COLORREF textColor, int left, int rowTop,
                       int rowBottom, int maxRight);

}} // namespace balloonchatwnd::chatpaint
