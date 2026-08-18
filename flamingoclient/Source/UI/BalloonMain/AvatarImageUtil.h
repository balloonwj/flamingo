/**
 *  AvatarImageUtil.h
 *
 *  本地上传头像的图片处理工具（第二期）：把用户选的任意图片（JPG/PNG/GIF）
 *  居中裁成 1:1 方形、缩放到目标边长，产出 (a) 可上传的文件、(b) 可显示的
 *  圆角方形位图。供「修改头像」对话框的本地上传页与主面板 header 真图渲染共用。
 *
 *  裁剪几何 ComputeCenterSquareCrop 是 header-only 纯函数（只做整型运算、不碰
 *  GDI+ / 文件），便于 FlamingoUnit(Catch2) 直接断言；其余 GDI+ / 文件 IO 函数
 *  在 .cpp 实现。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#pragma once

#include <windows.h>

namespace balloonmain {

/**
 *  计算「居中最大正方形」裁剪区域：从 srcW×srcH 里取边长 min(srcW,srcH) 的居中
 *  正方形。纯整型运算，无副作用，可单测。
 *  @param srcW    源宽（像素，>0）
 *  @param srcH    源高（像素，>0）
 *  @param outX    [出参] 裁剪区左上角 x
 *  @param outY    [出参] 裁剪区左上角 y
 *  @param outSide [出参] 裁剪区边长
 *  @return srcW>0 且 srcH>0 返回 true；否则 false（出参不变）
 */
inline bool ComputeCenterSquareCrop(int srcW, int srcH,
                                    int& outX, int& outY, int& outSide)
{
    if (srcW <= 0 || srcH <= 0)
    {
        return false;
    }
    int side = (srcW < srcH) ? srcW : srcH;
    outSide = side;
    outX = (srcW - side) / 2;
    outY = (srcH - side) / 2;
    return true;
}

/**
 *  把源图片文件居中裁成 1:1、缩放到 targetPx 边长，存成 PNG 到 outPath。
 *  供上传：ImageTransfer 随后读取 outPath 上传到 imgserver。
 *  @param srcPath  源图片完整路径（宽字符；JPG/PNG/GIF 等 GDI+ 可解码格式）
 *  @param targetPx 目标方形边长（像素，>0；建议 256）
 *  @param outPath  输出 PNG 完整路径（宽字符；其父目录须已存在）
 *  @return 成功 true；源不可解码 / 保存失败返回 false
 */
bool ProcessAvatarSourceFile(LPCWSTR srcPath, int targetPx, LPCWSTR outPath);

/**
 *  从图片文件生成一张 sizePx 圆角方形、32bpp 预乘 alpha 的显示位图（居中裁方 +
 *  缩放 + 圆角，角外透明）。供对话框预览与主面板 header 真图渲染（配 AlphaBlend）。
 *  @param path     图片完整路径（宽字符）
 *  @param sizePx   位图边长（像素，>0）
 *  @param radiusPx 圆角半径（像素，内部夹到 [0,sizePx/2]）
 *  @return 新分配的 HBITMAP，所有权归调用方（用完 ::DeleteObject）；失败返回 NULL
 */
HBITMAP MakeRoundedAvatarFromFile(LPCWSTR path, int sizePx, int radiusPx);

} // namespace balloonmain
