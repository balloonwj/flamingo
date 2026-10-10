/**
 *  登录窗与注册窗之间「翻转」过渡的每一帧怎么画（纯计算，可单测，不建窗口）。
 *
 *  翻转是绕竖直中线转半圈：前一半把起始窗口的画面水平压扁到 0，后一半把目标窗口的画面从 0 展开到原宽。
 *  转动途中远离观看者的那条竖边略短（梯形，轻微透视），画面略微压暗；画面矩形（水平中心、上边缘、宽、高）
 *  在起始窗口与目标窗口之间平滑插值，所以 565 高的登录窗翻过去时会渐渐变成 642 高的注册窗。
 *
 *  坐标一律相对「过渡画布」的左上角：画布是起始窗口与目标窗口两个矩形的并集，过渡窗口就盖在它上面。
 *  像素格式与 UpdateLayeredWindow 一致：32 位预乘 BGRA（按 uint32_t 读为 0xAARRGGBB），逐行紧排、自上而下。
 *
 *  典型用法（见 WindowFlipTransition.cpp）：
 *      float t = FlipProgress(elapsedMs, kFlipDurationMs);
 *      FlipFrame f = ComputeFlipFrame(fromRect, toRect, t, kFlipForward);
 *      RenderFlipFrame(f.m_showTarget ? toImage : fromImage, f, canvas);
 *
 *  翻转过渡的设计说明写在本文件与 WindowFlipTransition.h 里，汇总与验证记录见 docs/登录注册窗口翻转切换.md。
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <vector>

namespace balloonauth {

const uint32_t kFlipDurationMs = 350;      // 整个翻转的时长（毫秒）：前一半压扁起始画面，后一半展开目标画面
const float kFlipPerspective = 0.12f;      // 转到 90 度时远端竖边比近端短多少（占画面高度的比例），形成梯形
const float kFlipMaxShade = 0.35f;         // 转到 90 度时画面压暗的比例（0 不压暗，1 全黑）

/**
 *  一块 32 位预乘 BGRA 像素。
 */
struct FlipBitmap
{
    FlipBitmap()
        : m_width(0)
        , m_height(0)
    {
    }

    int                   m_width;    // 宽（像素）
    int                   m_height;   // 高（像素）
    std::vector<uint32_t> m_pixels;   // 逐行紧排、自上而下，共 m_width * m_height 个；0 表示全透明
};

/**
 *  翻转方向：决定哪一侧先「远去」（竖边变短）。两个方向互为镜像，打开注册窗与返回登录窗各用一个，
 *  看起来像同一张卡片翻过去又翻回来。
 */
enum FlipDirection
{
    kFlipForward = 0,    // 登录窗 → 注册窗：前半程右竖边远去，后半程左竖边远去
    kFlipBackward,       // 注册窗 → 登录窗：前半程左竖边远去，后半程右竖边远去
};

/**
 *  一帧的几何：画面被画进一个左右两条竖边平行的梯形。坐标相对过渡画布左上角（像素，浮点）。
 */
struct FlipFrame
{
    bool  m_showTarget;    // true 画目标窗口的画面（后半程），false 画起始窗口的画面（前半程）
    float m_left;          // 梯形左竖边的横坐标
    float m_right;         // 梯形右竖边的横坐标；不大于 m_left 时本帧什么都不画（正好转到侧面）
    float m_leftTop;       // 左竖边上端的纵坐标
    float m_leftBottom;    // 左竖边下端的纵坐标
    float m_rightTop;      // 右竖边上端的纵坐标
    float m_rightBottom;   // 右竖边下端的纵坐标
    float m_shade;         // 压暗比例 0~1
};

/**
 *  缓入缓出曲线（正弦形）：单调不减，FlipEase(0) = 0，FlipEase(0.5) = 0.5，FlipEase(1) = 1，关于中点对称。
 *  @param t 线性进度，超出 [0, 1] 时先截到这个范围
 *  @return 缓动后的进度 0~1
 */
float FlipEase(float t);

/**
 *  按已经过去的时间算线性进度。
 *  @param elapsedMs  从翻转开始已经过去的毫秒数
 *  @param durationMs 翻转总时长（毫秒）；为 0 时直接返回 1
 *  @return 0~1，过了总时长一律为 1
 */
float FlipProgress(uint64_t elapsedMs, uint32_t durationMs);

/**
 *  过渡画布：起始与目标两个窗口矩形的并集（屏幕坐标）。
 *  @param from 起始窗口矩形
 *  @param to   目标窗口矩形
 *  @return 并集矩形
 */
RECT FlipCanvasRect(const RECT& from, const RECT& to);

/**
 *  算一帧的几何。转角 = π × FlipEase(t)；画面矩形在 from 与 to 之间按同一缓动进度插值（水平中心、上边缘、宽、高）；
 *  水平方向按 |cos 转角| 压扁（以画面矩形的竖直中线为轴）；远端竖边上下各缩进 高 × kFlipPerspective × sin 转角 / 2；
 *  压暗 kFlipMaxShade × sin 转角。缓动进度不小于 0.5 时画目标画面。
 *  t 为 0 时恰为 from 矩形、不压扁不压暗；t 为 1 时恰为 to 矩形。
 *  @param from 起始窗口矩形（屏幕坐标）
 *  @param to   目标窗口矩形（屏幕坐标）
 *  @param t    线性进度 0~1（超出时截断）
 *  @param dir  翻转方向
 *  @return 本帧几何（相对 FlipCanvasRect(from, to) 的左上角）
 */
FlipFrame ComputeFlipFrame(const RECT& from, const RECT& to, float t, FlipDirection dir);

/**
 *  目标窗口的摆放：上边缘与锚点窗口对齐、水平居中；超出工作区时平移回工作区之内（先保证右、下边不出界，
 *  再保证左、上边不出界——窗口比工作区还大时以左上角为准）。注册窗打开时据此摆在登录窗的位置上。
 *  @param anchor   锚点窗口矩形（屏幕坐标，登录窗）
 *  @param width    目标窗口宽
 *  @param height   目标窗口高
 *  @param workArea 锚点所在显示器的工作区（屏幕坐标）
 *  @return 目标窗口矩形
 */
RECT AlignTopCenter(const RECT& anchor, int width, int height, const RECT& workArea);

/**
 *  把一张画面按本帧几何画进画布：画布先整块清成透明，再按列把画面缩放进梯形（最近邻取样），梯形边缘的像素按覆盖
 *  面积给出半透明，整块按 m_shade 压暗。
 *  @param image  起始或目标窗口的画面（预乘 BGRA）；为空时画布保持全透明
 *  @param frame  本帧几何
 *  @param canvas 画布，调用方事先设好 m_width / m_height；m_pixels 由本函数按尺寸重新填写
 */
void RenderFlipFrame(const FlipBitmap& image, const FlipFrame& frame, FlipBitmap& canvas);

} // namespace balloonauth
