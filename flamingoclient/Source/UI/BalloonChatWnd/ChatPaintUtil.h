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
 *  用 GDI 在 rc 里画一段文字，字体族固定为微软雅黑，背景透明。
 *  @param hdc         目标设备上下文
 *  @param text        文字；为 NULL 或空串时不画
 *  @param rc          文字区域
 *  @param pixelHeight 字号（像素）；<=0 时不画
 *  @param bold        是否加粗（FW_SEMIBOLD）
 *  @param color       文字颜色
 *  @param dtFlags     DrawText 的格式标志
 *  @param italic / underline / strike 斜体、下划线、删除线，默认都关
 */
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags,
                 bool italic = false, bool underline = false, bool strike = false);

/**
 *  量一段单行文字的像素宽度（微软雅黑、不加粗）。
 *  @param hdc         用于量宽的设备上下文
 *  @param text        文字；为 NULL 或空串时返回 0
 *  @param pixelHeight 字号（像素）
 *  @return 文字宽度（像素）
 */
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight);

}} // namespace balloonchatwnd::chatpaint
