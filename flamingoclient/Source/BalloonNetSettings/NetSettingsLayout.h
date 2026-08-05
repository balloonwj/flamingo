/**
 *  NetSettingsLayout.h
 *
 *  网络设置窗口客户区控件树的构造接口。布局以内嵌 XML 为唯一真值源，
 *  交给 balloonui 的 DuiXmlBuilder 解析。窗口代码按下面的 NetCtrlId
 *  数字 id 取回需要操作的控件。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include <memory>
#include "../../balloonui/balloonui/DuiControl.h"

namespace balloonnetsettings {

/**
 *  网络设置窗口客户区控件的数字 id —— 窗口代码按这些 id 取回控件。
 */
enum NetCtrlId
{
    kIdCardChat   = 700,   // 聊天服务器卡片（NetServerCard）
    kIdCardImage  = 701,   // 图片服务器卡片（NetServerCard）
    kIdCardFile   = 702,   // 文件服务器卡片（NetServerCard）

    kIdEditHost   = 710,   // "服务器地址"输入框（DuiEditHost）
    kIdEditPort   = 711,   // "端口"输入框（DuiEditHost）

    kIdBtnTest    = 720,   // "测试连接"按钮（NetActionButton）
    kIdBtnSave    = 721,   // "保存"按钮（NetActionButton）
};

/**
 *  构造网络设置窗口的客户区控件树。
 *  返回：客户区根控件（白底纵向布局容器）；解析失败返回 nullptr。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildNetSettingsBody();

} // namespace balloonnetsettings
