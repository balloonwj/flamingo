/**
 *  BalloonImageViewerWindow.h
 *
 *  聊天图片放大看图窗 —— 基于 balloonui DuiFrameWindow 的独立顶层窗口，对照
 *  设计稿 dd/图片放大.png。无边框、深色背景、可移动 / 可拖边缩放 / 可双击标题
 *  栏最大化；标题栏做成透明的深色拖动条（无 min/max/close 三按钮，靠 Esc 或
 *  任务栏 / Alt+F4 关闭）。客户区是一张自绘图片画布（ImageCanvas），支持缩放 /
 *  旋转 / 平移。
 *
 *  本轮（增量 2）只做窗口外壳 + 画布渲染与键鼠交互；底部悬浮工具栏与"另存为"
 *  留到增量 3，接入聊天气泡双击留到增量 4。调试期由命令行 --imageviewer
 *  命令行入口弹出（弹文件选择框挑一张本地图片自检）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../BalloonMain/TitleBarIcon.h"   // m_programIcons：标题栏与任务栏的程序图标

namespace balloonimageviewer {
class ImageCanvas;
}

/**
 *  看图窗的翻页回调（可选）：由打开看图窗的一方实现。看图窗里按 ← / → 时调用；没有设置时这两个键不做任何事。
 *  文件共享的图片预览用它翻到共享目录里的上一张 / 下一张（图片要先从分享者那里取来，取到后再经 OpenShared 换图）。
 */
class IImageViewerNavigator
{
public:
    virtual ~IImageViewerNavigator()
    {
    }

    /**
     *  看图窗里按了 ← 或 →。
     *  @param delta -1 上一张，1 下一张
     */
    virtual void OnImageViewerNavigate(int delta) = 0;
};

/**
 *  CBalloonImageViewerWindow
 *
 *  看图窗口。继承 balloonwjui::DuiFrameWindow，由它提供无边框自绘窗体与系统层
 *  的拖动 / 缩放 / 最大化；本类把标题栏配成透明深色、隐藏三按钮，客户区装一张
 *  ImageCanvas。
 */
class CBalloonImageViewerWindow : public balloonwjui::DuiFrameWindow
{
public:
    CBalloonImageViewerWindow();
    ~CBalloonImageViewerWindow();

    /**
     *  创建并显示看图窗口（调试入口用，非模态、无父窗口）。窗口显示后弹出
     *  文件选择框让开发者挑一张本地图片载入，便于核对渲染与交互。
     *    hMainDlg：主面板的窗口句柄；调试入口下本窗口是
     *              唯一可见窗口，关闭时据此请求退出程序。
     */
    void CreateAndShow(HWND hMainDlg);

    /**
     *  打开看图窗显示指定图片（增量 4 由聊天气泡双击调用）。窗口若未创建则先
     *  创建，已创建则复用并换图，随后置前显示。
     *    hOwner：拥有者窗口句柄（一般为聊天窗 / 主窗）；可为 NULL。
     *    imagePath：图片本地完整路径（UTF-16，非空）。
     */
    void ShowImage(HWND hOwner, LPCTSTR imagePath);

    /**
     *  设置翻页回调；传 NULL 取消。看图窗关闭时自动取消。
     *    navigator：翻页回调（不拥有，须在看图窗关闭或换成别的回调之前一直有效）。
     */
    void SetNavigator(IImageViewerNavigator* navigator);

    /**
     *  进程内共享看图窗入口：聊天气泡双击图片时调用。懒创建一个全局单例看图窗
     *  并显示指定图片（已打开则复用换图、置前）。非模态，关闭仅销毁看图窗本身、
     *  不影响应用。供单聊 / 群聊窗共用同一个看图窗实例。
     *    hOwner：拥有者窗口句柄（一般为发起的聊天窗 HWND，用于显示归属）；可为 NULL。
     *    imagePath：图片本地完整路径（UTF-16，非空）。
     */
    static void OpenShared(HWND hOwner, LPCTSTR imagePath);

    /**
     *  同 OpenShared，另外设置翻页回调（文件共享的图片预览用）。聊天图片经上面的 OpenShared 打开时回调被取消。
     *    navigator：翻页回调；可为 NULL（等同于 OpenShared）。
     */
    static void OpenSharedWithNavigator(HWND hOwner, LPCTSTR imagePath, IImageViewerNavigator* navigator);

    BEGIN_MSG_MAP(CBalloonImageViewerWindow)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDownMsg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    // 创建窗口、配置暗色外观、装入画布并显示。成功返回 true。
    bool BuildWindow(HWND hWndParent);

    // 构建客户区：一张铺满的 ImageCanvas。
    void InitUi();

    // WM_KEYDOWN：Esc 关闭窗口；其余键交回基类链。
    LRESULT OnKeyDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_CLOSE：调试入口下关闭即请求退出程序；正式使用下仅销毁本窗口。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    balloonimageviewer::ImageCanvas* m_pCanvas;   // 图片画布（裸指针，所有权在控件树）
    HWND     m_hMainDlg;                           // 调试入口下 主面板句柄；NULL=正式使用（关闭只销毁本窗）
    IImageViewerNavigator* m_navigator;            // 翻页回调（不拥有）；NULL 时 ← / → 不做任何事，关闭窗口时清空
    HBITMAP  m_hBgBitmap;                          // 深色背景位图（caller-owned，本类持有，析构释放）
    // 标题栏左侧与任务栏的程序图标（2026-10-07 起，开发约定文档 UI 约定第 10 条），随本对象释放，关窗再开时复用
    balloonmain::ProgramWindowIcons m_programIcons;
};
