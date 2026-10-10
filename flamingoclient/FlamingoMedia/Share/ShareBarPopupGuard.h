/**
 *  浮动工具条弹出菜单期间的关闭与推迟销毁，ShareBarPopupGuard.h
 *
 *  共享控制条、录制条、标注工具条都是不抢焦点（WS_EX_NOACTIVATE）的置顶小窗，按钮点开的下拉菜单用 balloonui 的 DuiMenu。
 *  DuiMenu::TrackPopup 是同步的：它在调用方的消息处理函数里跑一个嵌套消息循环，菜单弹出窗口没有所有者，只在失去焦点或
 *  失去激活时关闭。2026-10-07 用户实测的两个问题都由此而来：
 *    - 菜单开着时点了条上的「结束共享」：点不抢焦点的条不会让菜单失去焦点，菜单一直留在屏幕上；
 *    - 控制器在菜单的嵌套循环里销毁了条：ATL 要等这个窗口最外层的消息处理返回才清 m_hWnd，于是 m_hWnd 留着已失效的
 *      句柄，下一次显示时 IsWindow() 为假、再 Create 触发 atlwin.h 的断言 m_hWnd == 0。
 *  本类记下条当前弹出的菜单（含子菜单）或取色框，提供：
 *    - Close：关掉正在弹出的菜单（先关子菜单、再关根菜单，避免子菜单在它的 DuiMenu 对象释放之后才处理 WM_CLOSE）或取消
 *      取色框；
 *    - 推迟销毁的标记：条在菜单弹出期间被要求隐藏时只隐藏窗口、记下标记，等 TrackPopup 返回之后再销毁；在那之前若又要显示，
 *      直接复用这个窗口并清掉标记。
 *  使用约定（三个条相同）：按钮点击通知里不直接弹菜单，而是 PostMessage 给自己，等鼠标事件处理完再在那条消息里弹；
 *  TrackPopup 返回后若条已被隐藏或通知目标为空，丢弃这次选择。
 *
 *  典型用法：
 *      m_popup.BeginMenu(&menu, subMenus);
 *      const UINT chosen = menu.TrackPopup(x, y, m_hWnd);
 *      m_popup.End();
 *      if (m_popup.TakePendingDestroy()) { DestroyWindow(); return 0; }
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <memory>
#include <vector>
#include <windows.h>

namespace balloonwjui
{
    class DuiMenu;
}

namespace screenshare {

/**
 *  条在被要求隐藏时该怎么做（纯函数，便于单元测试）。
 *  @param popupActive 条上的菜单或取色框是否正在弹出（TrackPopup / ChooseColor 还没返回）
 *  @return true 表示只隐藏窗口、推迟到弹出结束后再销毁；false 表示可以立即销毁
 */
bool ShareBarShouldDeferDestroy(bool popupActive);

/**
 *  菜单返回后这次选择还要不要交给控制器（纯函数）。
 *  @param chosen          菜单返回的命令 id；0 表示没有选
 *  @param barHidden       菜单弹出期间条是否已被隐藏（推迟销毁中）
 *  @param notifyWndValid  通知窗口是否仍然有效
 *  @return 三项都满足（选了、条没被隐藏、通知窗口有效）时返回 true
 */
bool ShareBarShouldDeliverChoice(UINT chosen, bool barHidden, bool notifyWndValid);

//浮动工具条弹出菜单或取色框期间的状态（每个条一份，UI 线程上使用）
class CShareBarPopupGuard
{
public:
    CShareBarPopupGuard();

    /**
     *  开始跟踪一个菜单。根菜单与子菜单都由调用方持有，须保活到 End 之后。
     *  @param root     根菜单
     *  @param subMenus 根菜单下（含多级）的全部子菜单
     */
    void BeginMenu(balloonwjui::DuiMenu* root, const std::vector<std::unique_ptr<balloonwjui::DuiMenu> >& subMenus);

    //开始一个模态的取色框（ChooseColor）；对话框窗口建好后由钩子经 SetDialogWindow 登记
    void BeginDialog();

    /**
     *  登记正在弹出的模态对话框窗口（取色框的钩子在 WM_INITDIALOG 里调用），Close 时向它发「取消」。
     *  @param dialog 对话框窗口；不拥有
     */
    void SetDialogWindow(HWND dialog);

    //菜单或对话框已返回：清掉记下的菜单与对话框
    void End();

    //菜单或对话框是否正在弹出
    bool IsActive() const;

    /**
     *  关掉正在弹出的菜单或取消对话框；没有在弹出时什么也不做。只是投递关闭请求，菜单在随后的消息循环里关闭、
     *  TrackPopup 随之返回 0。
     */
    void Close();

    //记下「推迟销毁」：条在菜单弹出期间被要求隐藏；同时记下这次弹出被打断（WasInterrupted）
    void SetPendingDestroy();

    /**
     *  这次弹出期间条是否被隐藏过（BeginMenu / BeginDialog 时清零）。被隐藏过的话，即使随后又复用窗口重新显示，
     *  这次菜单的选择也属于上一场共享或录制，应当丢弃。
     */
    bool WasInterrupted() const;

    //是否有推迟的销毁
    bool IsPendingDestroy() const;

    /**
     *  取走推迟销毁的标记（取后清除）。
     *  @return 之前有推迟的销毁返回 true
     */
    bool TakePendingDestroy();

private:
    balloonwjui::DuiMenu*              m_root;            //正在弹出的根菜单（调用方持有）；没有在弹菜单时为 NULL
    std::vector<balloonwjui::DuiMenu*> m_subMenus;        //根菜单下的子菜单（调用方持有），按添加顺序
    bool                               m_dialogActive;    //取色框是否正在弹出
    HWND                               m_dialog;          //取色框窗口（钩子登记，不拥有）；还没建好或没有时为 NULL
    bool                               m_pendingDestroy;  //弹出期间被要求隐藏，等弹出结束后再销毁窗口
    bool                               m_interrupted;     //本次弹出期间条被隐藏过，弹出结束后丢弃选择
};

} // namespace screenshare
