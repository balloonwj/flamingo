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
 *  留到增量 3，接入聊天气泡双击留到增量 4。调试期由 CMainDlg 的 --imageviewer
 *  命令行入口弹出（弹文件选择框挑一张本地图片自检）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#pragma once

#include "../../balloonui/balloonui/Controls/Window/DuiFrameWindow.h"

namespace balloonimageviewer {
class ImageCanvas;
}

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
     *    hMainDlg：CMainDlg 的窗口句柄（隐藏的消息中枢）；调试入口下本窗口是
     *              唯一可见窗口，关闭时据此通知 CMainDlg 退出程序。
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
     *  进程内共享看图窗入口：聊天气泡双击图片时调用。懒创建一个全局单例看图窗
     *  并显示指定图片（已打开则复用换图、置前）。非模态，关闭仅销毁看图窗本身、
     *  不影响应用。供单聊 / 群聊窗共用同一个看图窗实例。
     *    hOwner：拥有者窗口句柄（一般为发起的聊天窗 HWND，用于显示归属）；可为 NULL。
     *    imagePath：图片本地完整路径（UTF-16，非空）。
     */
    static void OpenShared(HWND hOwner, LPCTSTR imagePath);

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

    // WM_CLOSE：调试入口下关闭即退出程序，转交 CMainDlg；正式使用下仅销毁本窗口。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    balloonimageviewer::ImageCanvas* m_pCanvas;   // 图片画布（裸指针，所有权在控件树）
    HWND     m_hMainDlg;                           // 调试入口下 CMainDlg 句柄；NULL=正式使用（关闭只销毁本窗）
    HBITMAP  m_hBgBitmap;                          // 深色背景位图（caller-owned，本类持有，析构释放）
};
