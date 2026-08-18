/**
 *  AvatarGray.h
 *
 *  头像"离线置灰"要做的两件事：单个颜色的灰度换算（纯函数），以及整幅头像位图的
 *  灰度副本生成。之所以单独成文件而不是塞进 MainPanelHeader.cpp：置灰的<u>色彩换算
 *  规则</u>必须能被单元测试逐点断言（Rec.601 亮度、幂等性、黑白不变），而生成位图那
 *  半边要碰 GDI，两者混在绘制代码里就没法单独测了。
 *
 *  为什么不复用 CImageEx::GrayScale（ImageEx.cpp:171）：那个函数在 CImageEx 对象上
 *  <u>就地</u>改像素，而主面板 Header 的头像是一张 32bpp 预乘 alpha 的 HBITMAP，而且
 *  原图必须保留（恢复在线时要换回彩色），需要的是"生成一份灰度副本"而非"就地灰掉"。
 *
 *  典型用法（MainPanelHeader::OnPaint）：
 *      if (balloonmain::ShouldGrayAvatar(m_connUiState))
 *      {
 *          //字形头像：底色过 MakeGrayColor 再填色
 *          //真图头像：用 MakeGrayAvatarBitmap 生成的灰度副本去 AlphaBlend
 *      }
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include <windows.h>      // COLORREF / HBITMAP

namespace balloonmain {

/**
 *  把一个颜色换算成等亮度的灰色。
 *
 *    color： 源颜色（RGB）。
 *
 *  @return 灰度颜色，R / G / B 三个分量相等，取值为源色按 Rec.601 亮度公式
 *          （0.299R + 0.587G + 0.114B）算出的灰阶。
 *
 *  纯函数、无副作用。满足幂等：MakeGrayColor(MakeGrayColor(c)) == MakeGrayColor(c)；
 *  纯黑与纯白换算后不变。
 */
COLORREF MakeGrayColor(COLORREF color);

/**
 *  生成一张头像位图的灰度副本。
 *
 *    hbmSource： 源位图。须是 <u>32bpp、自上而下（top-down）、预乘 alpha 的 DIBSection</u>
 *                —— 主面板头像正是此格式，两个生成入口都以 biHeight 取负建位图
 *                （见 AvatarImageUtil.cpp:161）。传 NULL 直接返回 NULL。
 *                <u>本函数不获取源位图所有权</u>，也不修改源位图的任何像素。
 *
 *  @return 新建的 32bpp 预乘 alpha 位图，<u>所有权归调用方</u>，用完须 DeleteObject；
 *          源位图为 NULL、非 32bpp、或创建失败时返回 NULL —— 调用方须判空，
 *          取不到灰度副本时退回画原来的彩色图，不要画出空白。
 *
 *  说明：预乘 alpha 下三个颜色分量都已经乘过 alpha，而亮度公式是线性的，所以直接对
 *  预乘值求亮度再写回三个分量，结果仍是正确的预乘值，无需先反乘、算完再乘回去。
 *  alpha 分量原样保留，因此圆角外的透明区域不受影响。
 */
HBITMAP MakeGrayAvatarBitmap(HBITMAP hbmSource);

} // namespace balloonmain
