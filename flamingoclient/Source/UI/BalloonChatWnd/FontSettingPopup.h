/**
 *  FontSettingPopup.h
 *
 *  聊天输入区"字体设置"浮层 —— 单聊 / 群聊 toolbar 的「AF」按钮点开后浮在按钮
 *  上方。对照 design/聊天字体设置1.png、2.png：样式（粗体 / 斜体 / 下划线 /
 *  删除线 + 清除）、字号（步进 + 12/14/16/20 快选）、颜色（一排色块）、实时预览。
 *  字体族固定微软雅黑（本轮不含字体族选择）。聊天窗打开时底部另有「使用默认字体」
 *  按钮（本会话设过自定义字体时才可点）；系统设置页编辑全局默认字体时没有这个按钮。
 *
 *  仿 EmojiPickerPopup 继承 DuiPopupHost：用 balloonui 现成布局/按钮搭面板，
 *  每次设置变化即把完整字体（net::TextFont）经回调交回宿主聊天窗，由宿主据此
 *  更新"当前字体"（影响后续发送 + 输入框所见即所得）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiPopupHost.h"
#include "../../net/MessageProtocolClient.h"   // net::TextFont

// 与 CEmojiPickerPopup 等 C 前缀窗口类一致，置于全局命名空间（便于聊天窗作为
// 成员无限定引用）。

/**
 *  CFontSettingPopup —— 字体设置浮层。
 */
class CFontSettingPopup : public balloonwjui::DuiPopupHost
{
public:
    // 字体设置变化回调。font 为面板当前完整设置（m_valid 恒为 true），宿主据此
    // 更新"当前字体"。userdata 为 SetChangeCallback 传入的上下文（一般为聊天窗）。
    typedef void (*FontChangeCallback)(void* userdata, const net::TextFont& font);

    // 「使用默认字体」回调：用户点了面板底部的该按钮，宿主据此删除当前会话的自定义
    // 字体、改用全局默认。userdata 为 SetUseDefaultCallback 传入的上下文。
    typedef void (*UseDefaultCallback)(void* userdata);

    // 「使用默认字体」按钮的控件 id。公开出来供单元测试经 HandleClick 模拟点击。
    static const UINT kIdUseDefault = 9310;

    CFontSettingPopup();
    ~CFontSettingPopup();

    // 设置"字体变化"回调。
    //   cb：回调函数指针；nullptr 取消。
    //   userdata：回调上下文（一般为聊天窗 this）。
    void SetChangeCallback(FontChangeCallback cb, void* userdata);

    // 设置「使用默认字体」回调。装了回调，面板底部才有「使用默认字体」按钮（聊天窗
    // 用）；不装则没有这个按钮（系统设置页编辑全局默认时用）。须在首次 OpenAt 之前
    // 调用 —— 面板控件树只在首次打开时构建一次。
    //   cb：回调函数指针；nullptr 取消。
    //   userdata：回调上下文（一般为聊天窗 this）。
    void SetUseDefaultCallback(UseDefaultCallback cb, void* userdata);

    // 设置「使用默认字体」按钮是否可点：当前会话设过自定义字体时可点，否则不可点
    // （已经在用默认字体，点了也没有意义）。
    //   enabled：是否可点。
    void SetUseDefaultEnabled(bool enabled);

    // 「使用默认字体」按钮当前是否可点。
    bool IsUseDefaultEnabled() const { return m_useDefaultEnabled; }

    // 面板上有没有「使用默认字体」按钮（即是否装了对应回调）。
    bool HasUseDefaultButton() const { return m_useDefaultCb != NULL; }

    // 处理面板上某个控件被点击：更新字体设置并回调宿主，或触发「使用默认字体」。
    // 子控件的点击通知经此处理；单元测试也直接调用它模拟点击。
    //   ctrlId：被点击控件的 id。
    // 返回：该点击被面板处理了返回 true；id 不属于面板、或「使用默认字体」不可点 /
    //       没有该按钮时返回 false。
    bool HandleClick(UINT ctrlId);

    // 用宿主当前字体初始化面板（重开时回显当前选择）。可在 OpenAt 前调；
    // m_valid==false 时按内置默认（14px、黑、无样式）。
    //   font：宿主当前字体设置。
    void SetFont(const net::TextFont& font);
    const net::TextFont& GetFont() const { return m_font; }

    // 在 anchor 屏幕矩形上方打开面板（toolbar 在窗口底部，优先朝上弹）。
    //   anchorScreen：锚矩形（一般取「AF」按钮的屏幕矩形）。
    //   owner：owner 窗口句柄（聊天窗）；目前未使用，浮层显示时不设所有者，原因见 .cpp。
    void OpenAt(const RECT& anchorScreen, HWND owner);

    BEGIN_MSG_MAP(CFontSettingPopup)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnChildNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiPopupHost)
    END_MSG_MAP()

private:
    // 子控件 DUI 通知：某设置控件被点 → 据 ctrlId 更新 m_font、刷新激活态与
    // 预览、回调宿主（面板保持打开，便于连续微调）。
    LRESULT OnChildNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 首次打开时构建一次面板控件树。
    void BuildContentOnce();

    // 按 m_font 刷新各控件激活态（B/I/U/S、字号快选、色块选中）与字号数值、预览。
    void RefreshControls();

    // 把 m_font 经回调交回宿主。
    void NotifyChange();

private:
    FontChangeCallback m_cb;     // 设置变化回调
    void*              m_ud;     // 回调上下文
    UseDefaultCallback m_useDefaultCb;       // 「使用默认字体」回调；NULL 表示面板上没有该按钮
    void*              m_useDefaultUd;       // 「使用默认字体」回调上下文
    bool               m_useDefaultEnabled;  // 「使用默认字体」按钮是否可点
    bool               m_built;  // 控件树是否已构建
    net::TextFont      m_font;   // 面板当前字体设置（m_valid 恒为 true）

    // 需在 RefreshControls 里更新的控件裸指针（所有权在控件树）。
    balloonwjui::DuiControl* m_pRoot;        // 内容根（FindCtrlById 入口）
    balloonwjui::DuiControl* m_pSizeValue;   // 字号数值标签（显示当前字号）
    balloonwjui::DuiControl* m_pPreview;     // 预览控件
    balloonwjui::DuiControl* m_pUseDefault;  // 「使用默认字体」按钮；没有该按钮时为 NULL
};
