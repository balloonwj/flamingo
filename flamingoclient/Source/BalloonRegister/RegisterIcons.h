/**
 *  RegisterIcons.h
 *
 *  注册对话框用到的一组线性图标的 GDI+ 描边绘制 helper。图标形状取自
 *  design 的 icons.jsx（统一 viewBox 24、圆头圆角描边）。注册表单的
 *  输入框左侧图标、步骤圆点里的对勾、返回按钮的箭头都从这里取。
 *
 *  与 BalloonChatWnd/ChatIcons 同思路、各自独立 —— 注册窗只需要少数
 *  几个图标，单列一份避免跨模块耦合。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 <windows.h>。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include <gdiplus.h>

namespace balloonregister {

/**
 *  注册窗用到的图标类型。逐值对应 design icons.jsx 里的同名图标。
 */
enum RegIconKind
{
    kRegIconUser    = 0,   // 人像 —— 昵称输入框
    kRegIconQrcode  = 1,   // 二维码 —— 自定义账号输入框
    kRegIconLock    = 2,   // 锁 —— 密码 / 确认密码输入框
    kRegIconPhone   = 3,   // 电话听筒 —— 手机号输入框
    kRegIconCheck   = 4,   // 对勾 —— 步骤完成标记 / 注册成功页
    kRegIconChevron = 5,   // 右尖括号 —— 返回按钮（绘制时旋转 180° 成左箭头）
    kRegIconCompany = 6,   // 楼宇 —— 公司名称输入框
};

/**
 *  在指定区域内绘制一个图标。
 *
 *    g：          目标 Graphics（调用方应已开启抗锯齿）。
 *    kind：       图标类型，取值见 RegIconKind；越界值不绘制。
 *    x / y：      绘制区域左上角（设备坐标）。
 *    size：       绘制区域边长（图标按 viewBox 24 等比缩放到此尺寸）。
 *    color：      图标颜色。
 *    strokeWidth：描边宽度，以 viewBox 24 为基准的单位（设计稿用 1.6，
 *                 步骤圆点里的对勾用 3.0 加粗）。
 */
void DrawRegIcon(Gdiplus::Graphics& g, int kind,
                 Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                 const Gdiplus::Color& color, Gdiplus::REAL strokeWidth);

} // namespace balloonregister
