/**
 *  EmojiPickerPopup.h
 *
 *  单聊窗"表情"按钮点开后的 Unicode emoji 选择面板。子类化 balloonui
 *  DuiPopupHost（无边框 WS_POPUP 浮层，自带 ESC / 失焦自动关闭），内部用
 *  balloonui 现成控件（DuiVBox / DuiHBox / DuiButton）搭一个 emoji 网格。
 *  点中某 emoji 经回调把该表情的 UTF-16 串交回单聊窗，由单聊窗插入输入框、
 *  随文本作为普通文本消息发送（不新增协议）。
 *
 *  说明：emoji 由 balloonui 控件经 GDI 绘制，受 GDI 限制呈现为单色字形
 *  （非彩色）；如需彩色 emoji 需另走 DirectWrite 渲染路径，超出本轮范围。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#pragma once

#include "../../balloonui/balloonui/Controls/Feedback/DuiPopupHost.h"

#include <string>
#include <map>

/**
 *  CEmojiPickerPopup —— 单聊输入区上方的 emoji 选择浮层。
 */
class CEmojiPickerPopup : public balloonwjui::DuiPopupHost
{
public:
    // 选中某 emoji 时的回调；emoji 为该表情的 UTF-16 串（可能是代理对，含多个
    // wchar），由调用方插入输入框。userdata 为 SetPickCallback 传入的上下文。
    typedef void (*PickCallback)(void* userdata, const wchar_t* emoji);

    CEmojiPickerPopup();
    ~CEmojiPickerPopup();

    /**
     *  设置"选中 emoji"回调。
     *    cb：回调函数指针；nullptr 取消。
     *    userdata：回调上下文（一般为单聊窗 this）。
     */
    void SetPickCallback(PickCallback cb, void* userdata);

    /**
     *  在 anchor 屏幕矩形上方打开 emoji 面板（工具栏在窗口底部，故优先朝上）。
     *    anchorScreen：锚矩形（一般取表情按钮的屏幕矩形）。
     *    owner：owner 窗口句柄（单聊窗）；仅用于生命周期归属，通知路由见 .cpp。
     */
    void OpenAt(const RECT& anchorScreen, HWND owner);

    BEGIN_MSG_MAP(CEmojiPickerPopup)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnChildNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiPopupHost)
    END_MSG_MAP()

private:
    // 子控件 DUI 通知：某 emoji 单元格被点击 → 回调该 emoji（面板保持打开，
    // 便于连续选多个；关闭交给 ESC / 失焦）。
    LRESULT OnChildNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 首次打开时构建一次 emoji 网格控件树。
    void BuildContentOnce();

private:
    PickCallback m_pickCb;   // 选中回调
    void*        m_pickUd;   // 回调上下文
    bool         m_built;    // 控件树是否已构建

    // "单元格控件 id → emoji 串" 映射：点击某格时据其 ctrlId 取回 emoji。
    std::map<UINT, std::wstring> m_cellEmoji;
};
