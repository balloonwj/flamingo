/**
 *  锁屏界面上按钮栏、密码框与确认框的布局和命中测试（纯几何，可单测）；以及设置窗口「预览」时画面上方那行
 *  退出提示的位置（2026-10-07）。
 *
 *  坐标是锁屏窗口的客户区坐标（物理像素，锁屏窗口按 Per-Monitor-V2 创建，一个窗口盖住一块显示器）。
 *  尺寸按样稿（tmp/lockscreen/lock_preview.html）的逻辑像素乘以缩放比例 scale（= 显示器 DPI / 96）。
 *
 *  典型用法（锁屏线程，每帧或尺寸变化时）：
 *      int contentW[3] = { 解锁、重启、关机三个按钮的「图标 + 间距 + 文字」宽度 };
 *      LockOverlayLayout L = ComputeOverlayLayout(w, h, scale, contentW);
 *      LockOverlayHit hit = HitTestOverlay(L, state.Phase(), pt);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>

#include "LockUiState.h"

namespace flamingolock {

/**
 *  一块显示器上锁屏界面各部件的矩形（客户区坐标，物理像素）。
 */
struct LockOverlayLayout
{
    RECT m_unlockButton;      // 「解锁」按钮
    RECT m_restartButton;     // 「重启」按钮
    RECT m_shutdownButton;    // 「关机」按钮
    RECT m_passwordBox;       // 密码输入框
    RECT m_passwordGo;        // 密码框右边的圆形箭头按钮
    RECT m_passwordMessage;   // 密码框下方的提示文字（「密码不正确」、冷却倒计时）
    RECT m_confirmPanel;      // 确认框整体
    RECT m_confirmTitle;      // 确认框标题「确定要关机吗？」
    RECT m_confirmText;       // 确认框说明文字
    RECT m_confirmCancel;     // 确认框「取消」按钮
    RECT m_confirmOk;         // 确认框「关机 / 重启」按钮
    float m_scale;            // 计算时用的缩放比例，绘制圆角、字号时沿用
};

/**
 *  命中测试的结果。
 */
enum LockOverlayHit
{
    kHitNothing = 0,          // 没点中任何部件（只有动画时一律如此）
    kHitUnlock,               // 「解锁」
    kHitRestart,              // 「重启」
    kHitShutdown,             // 「关机」
    kHitPasswordBox,          // 密码输入框
    kHitPasswordGo,           // 密码框右边的箭头
    kHitConfirmCancel,        // 确认框「取消」
    kHitConfirmOk,            // 确认框「关机 / 重启」
    kHitConfirmPanel,         // 确认框的其它地方（吸收点击，不做任何事）
};

const int kOverlayButtonCount = 3;   // 按钮栏的按钮数：解锁、重启、关机

/**
 *  计算一块显示器上锁屏界面的布局：按钮栏在底部（距底边 9% 高度）水平居中；密码框在按钮栏上方；
 *  确认框在屏幕正中。
 *  @param width         客户区宽（物理像素）
 *  @param height        客户区高（物理像素）
 *  @param scale         缩放比例（DPI / 96），不大于 0 时按 1 处理
 *  @param contentWidths 三个按钮的内容宽度（图标 + 间距 + 文字，物理像素），依次为解锁、重启、关机；
 *                       可为 NULL，表示都按最小宽度
 *  @return 布局
 */
LockOverlayLayout ComputeOverlayLayout(int width, int height, float scale, const int* contentWidths);

/**
 *  命中测试。只测当前阶段可见的部件：只有动画时一律没点中；按钮阶段测三个按钮；密码阶段再加密码框与箭头；
 *  确认框阶段只测确认框（点在框外也算没点中）。
 *  @param layout 布局
 *  @param phase  当前阶段
 *  @param pt     点（客户区坐标）
 *  @return 命中的部件
 */
LockOverlayHit HitTestOverlay(const LockOverlayLayout& layout, LockUiPhase phase, POINT pt);

/**
 *  预览时画面上方那行提示（「按 Esc 或单击退出预览」）的胶囊矩形：水平居中，顶边距屏幕顶边 6% 高度；
 *  宽度为文字宽度加两侧留白，超出屏幕时截到距左右边缘各 16 逻辑像素。2026-10-07 设置窗口「预览」加入。
 *  @param width     客户区宽（物理像素）
 *  @param height    客户区高（物理像素）
 *  @param scale     缩放比例（DPI / 96），不大于 0 时按 1 处理
 *  @param textWidth 提示文字的宽度（物理像素），负数按 0 处理
 *  @return 胶囊矩形（客户区坐标）
 */
RECT ComputePreviewHintRect(int width, int height, float scale, int textWidth);

} // namespace flamingolock
