/**
 *  NetServerCard.h
 *
 *  网络设置窗口里的一张服务器卡片 —— 对照 design/netsettings.png 顶部
 *  那三张可单选的服务器条目。卡片显示：图标、服务器名、"地址:端口"
 *  副标题、右侧连接状态。选中的卡片橙色描边 + 浅橙底。
 *
 *  继承 DuiButton 复用点击 / DUIN_CLICK 通知，只覆写 OnPaint 自绘。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloonnetsettings {

/**
 *  NetServerCard —— 一张可单选的服务器卡片（自绘）。
 */
class NetServerCard : public balloonwjui::DuiButton
{
public:
    NetServerCard();

    // 设置本卡代表哪一组服务器（取值见 NetServerKind）。会触发重绘。
    void SetServerKind(int kind);
    int  GetServerKind() const { return m_kind; }

    // 设置选中态（选中：橙色描边 + 浅橙底）。会触发重绘。
    void SetCardSelected(bool selected);
    bool IsCardSelected() const { return m_selected; }

    // 更新副标题显示的"地址:端口"。会触发重绘。
    //   host：服务器地址；port：端口。
    void SetEndpoint(LPCTSTR host, int port);

    // 设置连接状态（取值见 NetConnState，由窗口侧的后台探测结果驱动）。会触发重绘。
    void SetConnState(int state);

    // DuiControl 覆写：自绘卡片（图标 + 名称 + 副标题 + 状态）。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：普通箭头光标。
    bool OnSetCursor(POINT pt) override { return false; }

private:
    int     m_kind;        // 代表的服务器组，见 NetServerKind
    bool    m_selected;    // 是否选中
    CString m_host;        // 副标题用：服务器地址
    int     m_port;        // 副标题用：端口
    int     m_connState;   // 连接状态，见 NetConnState
};

} // namespace balloonnetsettings
