/**
 *  flamingoAdmin —— RootContainer 根容器。
 *
 *  历史的 ui::Toast 类已删除, admin 改用 balloonwjui::DuiToast(顶层浮出 +
 *  渐入渐出 + 自驱定时); RootContainer 持有一个 DuiToast 实例作 toast 层。
 *
 *  关键设计——页面常驻、切换只切显隐:登录界面与主界面外壳一经加入便常驻,
 *  切换时只改可见性、<u>绝不销毁</u>。原因:销毁一个「宿主仍在跟踪(hover /
 *  焦点 / 鼠标按下)的控件」会留下悬空指针, 宿主下次鼠标事件即访问已释放内
 *  存而崩溃 —— 而触发登录 / 登出的按钮恰恰位于被切走的页面内。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"
#include "Controls/Basic/DuiToast.h"
#include <atlstr.h>
#include <memory>
#include <vector>

namespace flamingoadmin { namespace ui {

// ===========================================================================
// RootContainer —— 客户区根容器。
//
// 自底向上叠三层：
//   · 页面层 —— 若干常驻「页面」，任一时刻只显示一个（其余隐藏但不销毁）；
//   · 弹窗层 —— 一个常驻「弹窗」，按需显隐，显示时压在当前页面之上；
//   · Toast 层 —— 始终置顶的轻量提示(balloonwjui::DuiToast)。
//
// 绘制顺序：当前页面 → 弹窗（若可见）→ Toast。命中测试：弹窗可见时只下发
// 给弹窗，否则下发给当前页面（Toast 非交互、点击穿透）。
//
// 三层都「常驻、切显隐、绝不销毁」，从根本上杜绝销毁宿主跟踪中的控件树
// 导致的悬空指针崩溃。
// ===========================================================================
class RootContainer : public balloonwjui::DuiControl
{
public:
    // 设置 Toast 层。构造 RootContainer 后调用一次。
    void SetToast(std::unique_ptr<balloonwjui::DuiToast> toast);

    // 加入一个页面。页面加入时即隐藏；之后用 ShowPage 切换显示。
    // 页面一经加入便常驻，不会被销毁。
    void AddPage(std::unique_ptr<balloonwjui::DuiControl> page);

    // 显示 page、隐藏其它所有已加入的页面。
    //   page：须是此前经 AddPage 加入过的页面。
    void ShowPage(balloonwjui::DuiControl* page);

    // 加入一个弹窗。弹窗加入时即隐藏；之后用 ShowModal 显示。可加入多个
    // （新增企业弹窗、组织节点弹窗等），任一时刻只显示其中一个。
    void AddModal(std::unique_ptr<balloonwjui::DuiControl> modal);

    // 显示指定弹窗（须是此前 AddModal 加入过的），并隐藏其它弹窗。
    void ShowModal(balloonwjui::DuiControl* modal);

    // 隐藏当前弹窗。
    void HideModal();

    // 弹窗层当前是否有弹窗可见。
    bool IsModalVisible() const;

    // 当前可见的弹窗；没有弹窗可见时返回 nullptr。借用，所有权属本容器。
    balloonwjui::DuiControl* GetVisibleModal() const
    {
        return IsModalVisible() ? m_activeModal : nullptr;
    }

    // 取 Toast 层指针（借用，所有权属本容器）。
    balloonwjui::DuiToast* GetToast() const { return m_toast; }

    // 显示层变化回调：ShowPage / ShowModal / HideModal 改完显隐之后调用。
    //   user：SetLayerChangedCallback 时传入的指针。
    typedef void (*LayerChangedFn)(void* user);

    // 注册显示层变化回调。FlamingoLink.dll 的「在线用户」页是原生子窗口，会画在叠层弹窗
    // 之上，控制台据此在页面切换、弹窗开关时重新决定它显示与否（2026-10-04）。
    //   fn：回调，为空表示不通知。user：原样传回，所有权归调用方。
    void SetLayerChangedCallback(LayerChangedFn fn, void* user);

    void                     Layout(const RECT& rcAvail) override;
    void                     OnPaint(HDC hdc, const RECT& rcDirty) override;
    balloonwjui::DuiControl*  HitTest(POINT ptHostClient) override;

private:
    balloonwjui::DuiToast*                m_toast       = nullptr;  // 借用（在 m_children 中）
    std::vector<balloonwjui::DuiControl*> m_pages;                  // 借用（均在 m_children 中）
    balloonwjui::DuiControl*              m_current     = nullptr;  // 当前显示的页面
    std::vector<balloonwjui::DuiControl*> m_modals;                 // 借用（均在 m_children 中）
    balloonwjui::DuiControl*              m_activeModal = nullptr;  // 当前显示的弹窗
    LayerChangedFn                        m_layerChanged = nullptr; // 显示层变化回调，可为空
    void*                                 m_layerChangedUser = nullptr; // 回调的用户数据，所有权归调用方

    // 调用显示层变化回调（未注册时什么都不做）
    void FireLayerChanged();
};

}} // namespace flamingoadmin::ui
