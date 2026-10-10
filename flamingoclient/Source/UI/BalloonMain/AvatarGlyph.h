/**
 *  AvatarGlyph.h
 *
 *  在字形头像（姓名首字或两个字母）上把字画在正中。2026-10-03 起所有头像画字都走这里。
 *
 *  为什么不用「居中对齐的格式 + DrawString / DrawText」：两者居中的都是文字排版框而不是字本身。
 *  GDI+ 的 DrawString 在排版框两侧留的空白不对称，垂直方向又以整行行高居中，微软雅黑的字因此普遍
 *  偏左 1~1.5 像素、偏上 0.5~2 像素；GDI 的 DrawText 水平基本准确，垂直方向按字符单元格高度居中，
 *  偏低 0.5~1.5 像素（2026-10-03 实测，测量脚本见 tmp/aa/glyph_probe*.py）。本文件改为取字形的实际
 *  笔画外框，按外框居中，误差在半个像素以内。
 *
 *  提供两种画法，与原先各处的画法一一对应：
 *    · FillCenteredGlyph —— GDI+，把字转成轮廓路径后抗锯齿填充。用于画在透明位图上的头像
 *      （MakeMonogramAvatar 等），这类位图上 GDI 的 ClearType 文字无法正确混合透明通道。
 *    · DrawCenteredGlyphGdi —— GDI，用 GetGlyphOutlineW 取字形黑框算出落点后 TextOutW。用于直接
 *      画在不透明底色上的头像（聊天气泡、群成员行等），保留 GDI 文字的清晰度。
 *
 *  典型用法：
 *      // GDI+：透明位图上的圆形头像
 *      balloonmain::FillCenteredGlyph(g, text, len, fontFamily, Gdiplus::FontStyleRegular,
 *                                     fontPx, Gdiplus::RectF(0, 0, size, size), textBrush);
 *      // GDI：不透明底色上的圆角头像
 *      balloonmain::DrawCenteredGlyphGdi(hdc, avatarText, avatarRect, 16, true, RGB(255, 255, 255));
 *
 *  本文件遵循本目录头文件约定：包含它之前调用方须已引入 <windows.h>；GDI+ 版的参数类型只做前置
 *  声明，调用方使用它时须已包含 <gdiplus.h>。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#pragma once

#include <windows.h>

namespace Gdiplus {
class Graphics;
class FontFamily;
class Brush;
class RectF;
class GraphicsPath;
}

namespace balloonmain {

/**
 *  按字形外框把文字居中：给出文字在「原点在左上角、以 TA_TOP 方式书写」时笔画外框的位置，算出
 *  书写原点，使外框中心落在 box 的中心。纯函数，供 DrawCenteredGlyphGdi 使用并单独测试。
 *
 *    box：       要居中到的方框（设备坐标）。
 *    inkFromOrg：以书写原点为 (0, 0) 时笔画外框的位置，right / bottom 不含（与 RECT 惯例相同）。
 *  @return 书写原点（设备坐标）。外框与方框的宽或高之差为奇数时，多出的那一个像素放在右侧 / 下侧。
 */
POINT ComputeCenteredTextOrigin(const RECT& box, const RECT& inkFromOrg);

/**
 *  GDI+ 画法：把 text 的前 len 个 UTF-16 单元转成轮廓路径，平移到使路径外框中心与 box 中心重合，
 *  再按 g 当前的抗锯齿设置填充。字为空、len<=0 或路径为空（如空格）时什么都不画。
 *
 *    g：         目标画布；调用方事先设好平滑模式与像素偏移模式（MakeMonogramAvatar 用抗锯齿 +
 *                PixelOffsetModeHalf）。
 *    text、len： 要画的文字与 UTF-16 单元个数（一般取前 1~2 个）。
 *    family：    字体族（借用）。
 *    fontStyle： Gdiplus::FontStyle 取值（常规 / 粗体等）。
 *    emSizePx：  字号（像素，em 高度）。
 *    box：       要居中到的方框（画布坐标）。
 *    brush：     填充画刷（借用）。
 *  副作用：在 g 上绘制。
 */
void FillCenteredGlyph(Gdiplus::Graphics& g, LPCWSTR text, int len,
                       const Gdiplus::FontFamily& family, int fontStyle, float emSizePx,
                       const Gdiplus::RectF& box, const Gdiplus::Brush& brush);

/**
 *  GDI 画法：用微软雅黑在 box 正中画 text，字体参数与原先各处头像画字的 DrawGdiText 相同
 *  （OUT_TT_PRECIS、CLEARTYPE_QUALITY；加粗时为 FW_SEMIBOLD）。按 GetGlyphOutlineW 给出的字形
 *  黑框居中后 TextOutW；有字形取不到度量（例如 UTF-16 代理对）时退回 DrawText 的居中方式，
 *  保证至少能画出字来。
 *
 *    hdc：        目标设备上下文；本函数会临时选入字体、设置透明背景与文字颜色，返回前全部恢复。
 *    text：       要画的文字（以 0 结尾）；为空时什么都不画。
 *    box：        要居中到的方框（设备坐标）。
 *    pixelHeight：字号（像素，em 高度，即 CreateFontW 的 -height）；<=0 时什么都不画。
 *    bold：       是否加粗。
 *    color：      文字颜色。
 */
void DrawCenteredGlyphGdi(HDC hdc, LPCWSTR text, const RECT& box, int pixelHeight, bool bold, COLORREF color);

/**
 *  按像素格对齐地填充头像底色（以及头像外圈）：临时把 g 切到 PixelOffsetModeHalf 再 FillPath，
 *  填完恢复原来的设置。
 *
 *  GDI+ 默认的像素偏移模式把整数坐标当作像素中心，按 (x, y, w, h) 填出来的形状实际比名义方框整体
 *  往右下偏半个像素。头像字（DrawCenteredGlyphGdi 按名义方框居中）与照片头像（按名义方框 1:1 贴图）
 *  都对齐名义方框，底色不对齐时字看上去偏左上半个像素（2026-10-03 实测，聊天气泡与选人行）。
 *
 *    g：    目标画布。
 *    brush：填充画刷（借用）。
 *    path： 头像底色或外圈的路径（借用），按名义方框坐标构造。
 *  副作用：在 g 上绘制；g 的像素偏移模式在返回前恢复。
 */
void FillAvatarBackground(Gdiplus::Graphics& g, const Gdiplus::Brush& brush, const Gdiplus::GraphicsPath& path);

} // namespace balloonmain
