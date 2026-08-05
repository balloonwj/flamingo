/**
 *  BalloonMainLayout.h
 *
 *  登录后主面板的 balloonui 客户区控件树构造种子。把"主面板长什么样"
 *  从对话框类里抽出成一个可独立编译、可被单元测试直接调用的纯函数模块
 *  —— CBalloonMainPanel 与 FlamingoUnit 测试工程共用本文件。
 *
 *  设计稿：design/pc-snapshots/05-main-contacts.png、06-main-groups.png。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前，调用方必须已引入
 *  ATL/WTL（Flamingo 侧由 stdafx.h 保证，FlamingoUnit 侧由其 stdafx.h
 *  保证）。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include <memory>
#include <string>
#include "../../balloonui/balloonui/DuiControl.h"

namespace balloonmain {

/**
 *  CtrlId —— 主面板里所有"需要被代码引用"的控件的数字 id。
 *
 *  与 BalloonChat / BalloonAuth 同约定：XML 里写 id="200"，对话框侧用
 *  DuiControl::FindCtrlById(id) 取回控件指针。集中成 enum 避免 XML
 *  字符串与 C++ 代码各写一份魔数、对不上。
 *
 *  纯装饰、不需代码引用的控件（底部 spacer）在 XML 里不写 id，GetCtrlId()
 *  返回 0 —— 0 视为"未命名"，不属于本 enum。
 */
enum CtrlId : UINT
{
    // ---- 顶部 Banner ----
    kIdHeader        = 240,   // 渐变 Banner（MainPanelHeader）

    // ---- 搜索 ----
    kIdSearchBox     = 200,   // 搜索框（DuiEditHost）

    // ---- 三个 Tab 按钮 ----
    kIdTabMessages   = 210,   // “消息 / 会话”Tab
    kIdTabContacts   = 211,   // “联系人”Tab
    kIdTabGroups     = 212,   // “群”Tab

    // ---- 三个 Tab 内容面板 ----
    kIdPaneMessages  = 220,   // 会话列表面板容器
    kIdPaneContacts  = 221,   // 联系人列表面板容器
    kIdPaneGroups    = 222,   // 群列表面板容器

    // ---- Tab 内容面板里的 DuiTreeView(PR9 开始替换原手写 listitem) ----
    kIdContactsTree  = 213,   // 联系人页:DuiTreeView,两根("我的好友"/"服务号")
    kIdGroupsTree    = 214,   // 群页:DuiTreeView,单根("我的群聊 N")
    kIdMessagesTree  = 215,   // 消息页:DuiTreeView,扁平(每会话一根,无层级)
    // 216 曾是联系人页顶部"通讯录 · N 人"标题的 id。该标题行已去掉——人数改为跟在
    // 树里公司根节点的名字后面(见 BuildContactsListXml 注释),故此 id 一并撤销;
    // 若日后再加联系人页标题,另取新值,不要复用 216,免得与旧版本布局混淆。

    // ---- 底部栏 ----
    kIdBottomMenu    = 230,   // 底部左下角汉堡菜单按钮
    kIdBottomAction  = 231,   // 底部主操作按钮（联系人页 +查找 / 群页 +建群）
    kIdBottomBar     = 232,   // 底部栏本身（承载上面两个按钮的 hbox）。取它的矩形是为了
                              // 把"按住底部栏空白处拖动窗口"的可拖区框出来，见 PanelDragZone.h
};

/**
 *  构造主面板的客户区控件树（不含窗口边框 —— 边框由 DuiFrameWindow
 *  自身负责）。三个 Tab 内容面板默认装入 mock 数据列表；生产场景由
 *  调用方在 root 就绪后用真实数据替换(具体 API 见 BalloonMainPanel
 *  的 RefreshXxx 方法)。
 *
 *  返回：客户区根控件（一个 vbox），调用方拿到所有权；解析失败返回
 *        nullptr（内嵌 XML 为编译期常量、形态固定，理论上不会失败）。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildMainPanelBody();

} // namespace balloonmain
