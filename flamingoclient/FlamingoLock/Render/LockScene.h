/**
 *  锁屏动画场景的接口。每块显示器的锁屏窗口各持有一个场景对象，按自己的尺寸排版、独立播放。
 *
 *  生命周期（都在锁屏线程上）：
 *      ILockScene* s = CreateLockScene(kAnimHonor);
 *      s->Init(env, width, height);           // 窗口尺寸确定后，或尺寸变化后
 *      每帧：s->Update(dt, wallMs); s->Draw(rt);
 *      渲染目标重建时：s->DiscardDeviceResources();   // 画刷、离屏位图等与渲染目标绑定的资源
 *
 *  场景只负责动画本身；按钮栏、密码框、确认框与左下角的时间由 CLockOverlayRenderer 叠画在上面。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include "LockD2D.h"
#include "../Core/LockAnimationChoice.h"

namespace flamingolock {

/**
 *  场景运行需要的公共对象，由锁屏会话创建并在会话期间保持有效。
 */
struct SceneEnv
{
    ID2D1Factory*   m_d2d;      // Direct2D 工厂（借用），用于创建路径几何
    IDWriteFactory* m_dwrite;   // DirectWrite 工厂（借用），用于创建文字格式
    float           m_scale;    // 缩放比例（显示器 DPI / 96）
};

/**
 *  一种锁屏动画。
 */
class ILockScene
{
public:
    virtual ~ILockScene()
    {
    }

    /**
     *  按画面尺寸初始化（或重新初始化）场景状态。与渲染目标无关的资源（文字格式、路径几何）在这里创建。
     *  @param env    公共对象（借用，须在场景销毁前一直有效）
     *  @param width  画面宽（物理像素）
     *  @param height 画面高（物理像素）
     */
    virtual void Init(const SceneEnv& env, float width, float height) = 0;

    /**
     *  渲染目标即将重建：释放画刷、离屏位图等与旧渲染目标绑定的资源，下次 Draw 时重新创建。
     */
    virtual void DiscardDeviceResources() = 0;

    /**
     *  推进动画。
     *  @param dt     距上一帧的秒数（调用方已截断到不超过 0.1 秒）
     *  @param wallMs 当前时刻（自 1970-01-01 起的毫秒，UTC），时钟类场景据此显示时间
     */
    virtual void Update(float dt, uint64_t wallMs) = 0;

    /**
     *  画一帧（调用方已 BeginDraw，并会 EndDraw）。
     *  @param rt 渲染目标
     */
    virtual void Draw(ID2D1RenderTarget* rt) = 0;

    /**
     *  鼠标在画面上移动（粒子时钟用来推开粒子）。默认不处理。
     *  @param x 横坐标（物理像素）；鼠标离开画面时为负数
     *  @param y 纵坐标（物理像素）
     */
    virtual void OnPointer(float x, float y)
    {
        (void)x;
        (void)y;
    }

    /**
     *  是否在左下角叠画时间与日期（场景自己显示时间、或画面要求简洁时返回 false）。
     */
    virtual bool ShowsCornerClock() const = 0;
};

/**
 *  创建一种动画的场景对象。
 *  @param anim 动画
 *  @return 新建的场景（调用方持有，用 delete 释放）；不认识的动画返回八荣八耻
 */
ILockScene* CreateLockScene(LockAnimation anim);

} // namespace flamingolock
