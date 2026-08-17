/**
 *  flamingoAdmin —— 网络设置界面 NetworkSettingsScreen。
 *
 *  从登录界面的「网络设置」链接进入。让管理员配置控制台要连接的服务器
 *  地址（主机 + 端口 + 是否 TLS），保存后写入 Util/AppConfig（落盘到
 *  flamingoAdmin.ini）。本界面是 RootContainer 里与登录页 / 外壳页同级的
 *  常驻页面，开 / 关只切显隐。
 *
 *  自绘居中卡片：返回链接 + 标题 + 主机 / 端口输入框 + TLS 开关 +
 *  状态行 + 测试连接 / 保存并返回 按钮。主机 / 端口是 DuiEdit 真
 *  输入框；TLS 开关为自绘可点区。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include "DuiControl.h"

#include <vector>

namespace balloonwjui { class DuiEdit; }

namespace flamingoadmin {

// 网络设置界面。
class NetworkSettingsScreen : public balloonwjui::DuiControl
{
public:
    NetworkSettingsScreen();

    // 从 AppConfig 把当前服务器配置载入表单、清空状态行。
    // 每次显示本页前由 AdminFrame 调用。
    void LoadFromConfig();

    // ---- 表单字段读取（供 AdminFrame 保存时校验 / 取值）----
    CString GetHost() const;       // 已去首尾空白的主机文字
    CString GetPortText() const;   // 端口输入框原始文字
    bool    GetUseTls() const { return m_useTls; }

    // 设置状态行文字与颜色（测试连接结果 / 校验错误提示）。
    void SetStatus(LPCTSTR text, COLORREF color);

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

private:
    RECT     m_panelRect{};        // 居中卡片矩形
    RECT     m_tlsToggleRect{};    // TLS 开关命中矩形（OnPaint 时算出）
    bool     m_useTls = false;     // TLS 开关状态
    CString  m_status;             // 状态行文字（空 = 不显示）
    COLORREF m_statusColor = 0;    // 状态行文字颜色

    balloonwjui::DuiEdit* m_hostEdit = nullptr;
    balloonwjui::DuiEdit* m_portEdit = nullptr;
};

} // namespace flamingoadmin
