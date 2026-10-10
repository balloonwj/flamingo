/**
 *  锁屏模块的 Direct2D / DirectWrite 公共辅助：颜色与矩形换算、按候选列表挑选系统里存在的字体、
 *  创建文字格式、在矩形里画一行文字。锁屏画面全部用 Direct2D 1.0 绘制（Windows 7 起提供），
 *  只链接系统导入库 d2d1.lib、dwrite.lib，不引入新库（docs/锁屏方案.md 第 2 节「绘制方式」）。
 *
 *  坐标约定：渲染目标的 DPI 设为 96，于是 1 个 DIP 等于 1 个物理像素；界面尺寸由调用方乘以缩放比例。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <atlbase.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>

namespace flamingolock {

/**
 *  由 0xRRGGBB 与不透明度拼出 Direct2D 颜色。
 *  @param rgb   颜色，0xRRGGBB
 *  @param alpha 不透明度 0~1
 *  @return 颜色
 */
D2D1_COLOR_F ColorRgb(uint32_t rgb, float alpha = 1.0f);

/**
 *  两个颜色按 t 插值（含不透明度）。
 *  @param a 起点颜色
 *  @param b 终点颜色
 *  @param t 0~1，超出时截断
 *  @return 插值结果
 */
D2D1_COLOR_F MixColor(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t);

/**
 *  RECT（整数像素）换成 D2D1_RECT_F。
 */
D2D1_RECT_F ToRectF(const RECT& rc);

/**
 *  从候选字体名里挑第一个在系统字体集中存在的；都不存在时返回最后一个候选。
 *  @param dwrite     DirectWrite 工厂，不可为 NULL
 *  @param candidates 候选字体名数组
 *  @param count      候选个数（至少 1）
 *  @return 选中的字体名
 */
std::wstring PickFontFamily(IDWriteFactory* dwrite, const wchar_t* const* candidates, int count);

/**
 *  创建一个文字格式（不自动换行）。
 *  @param dwrite DirectWrite 工厂
 *  @param family 字体名
 *  @param size   字号（DIP，即物理像素）
 *  @param weight 字重
 *  @param hAlign 水平对齐
 *  @param vAlign 段落（竖直）对齐
 *  @return 文字格式；失败返回空指针
 */
CComPtr<IDWriteTextFormat> MakeTextFormat(IDWriteFactory* dwrite, const std::wstring& family, float size,
                                          DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT hAlign,
                                          DWRITE_PARAGRAPH_ALIGNMENT vAlign);

/**
 *  量出一行文字的宽度（DIP）。
 *  @param dwrite DirectWrite 工厂
 *  @param format 文字格式
 *  @param text   文字
 *  @return 宽度；失败返回 0
 */
float MeasureTextWidth(IDWriteFactory* dwrite, IDWriteTextFormat* format, const std::wstring& text);

/**
 *  在矩形里画一行文字，对齐方式取文字格式自身的设置。
 *  @param rt     渲染目标
 *  @param format 文字格式
 *  @param text   文字
 *  @param rect   矩形
 *  @param brush  画刷
 */
void DrawTextIn(ID2D1RenderTarget* rt, IDWriteTextFormat* format, const std::wstring& text,
                const D2D1_RECT_F& rect, ID2D1Brush* brush);

/**
 *  取当前时刻的「时:分」与「月日 星期」两段文字（界面语言为中文时为中文写法，其它语言经 TR 翻译）。
 *  @param timeText 接收形如 "23:09" 的时间
 *  @param dateText 接收形如 "10月5日  星期一" 的日期
 *  @param wallMs   当前时刻（自 1970-01-01 起的毫秒，本地时区由函数换算）；为 0 时取系统当前时间
 */
void FormatClockTexts(std::wstring& timeText, std::wstring& dateText, uint64_t wallMs);

/**
 *  把「自 1970-01-01 起的毫秒数（UTC）」换成本地时间。
 *  @param wallMs 毫秒数；为 0 时取系统当前时间
 *  @return 本地时间
 */
SYSTEMTIME LocalTimeFromWallMs(uint64_t wallMs);

/**
 *  取当前时刻（自 1970-01-01 起的毫秒数，UTC）。
 */
uint64_t NowWallMs();

} // namespace flamingolock
