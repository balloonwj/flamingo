/**
 *  ContactLayout.h
 *
 *  联系人资料对话框客户区控件树的构造接口。布局以内嵌 XML 为唯一真值
 *  源，交给 balloonui 的 DuiXmlBuilder 解析。窗口代码按下面的
 *  ContactCtrlId 数字 id 取回需要操作的控件。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include <memory>
#include "../../balloonui/balloonui/DuiControl.h"

namespace ballooncontact {

/**
 *  联系人资料对话框客户区控件的数字 id —— 窗口代码按这些 id 取回控件。
 *  全部落在任务约定的 1000–1099 区间内。
 */
enum ContactCtrlId
{
    kIdAvatar         = 1000,   // 头部头像（DuiAvatar）
    kIdHeaderInfo     = 1001,   // 头部右侧多行资料块（ContactHeaderInfo 自绘）

    kIdTagFlow        = 1010,   // 标签流式容器（ContactTagFlow，窗口往里塞胶囊）
    kIdAddTagButton   = 1011,   // 「+ 添加标签」虚线按钮（ContactAddTagButton 自绘）

    kIdRemarkValue    = 1020,   // 「备注名」行右侧的当前备注文字（ContactValueText）
    kIdRemarkEditBtn  = 1021,   // 「备注名」行右侧的铅笔编辑按钮（ContactIconButton）
    kIdRemarkEdit     = 1022,   // 「备注名」行内编辑用文本框（DuiEditHost，默认隐藏）
    kIdGroupCombo     = 1023,   // 「分组」行的下拉框（DuiComboBox）

    kIdSwitchMute     = 1030,   // 「消息免打扰」开关（DuiSwitch）
    kIdSwitchPinned   = 1031,   // 「置顶聊天」开关（DuiSwitch）
    kIdSwitchBlocked  = 1032,   // 「加入黑名单」开关（DuiSwitch）

    kIdBtnClose       = 1040,   // 底部「关闭」按钮（ContactActionButton，ghost）
    kIdBtnSend        = 1041,   // 底部「发消息」按钮（ContactActionButton，primary）

    // 标签胶囊的 ctrlId 基址 —— 第 i 枚胶囊取 kIdTagChipBase + i。
    // 留出 [1050, 1099] 共 50 个号，足够任何合理数量的标签。
    kIdTagChipBase    = 1050,
};

/**
 *  构造联系人资料对话框的客户区控件树。
 *
 *  返回：客户区根控件（白底纵向布局容器）；解析失败返回 nullptr。
 *  调用方取得后用 SetClientContent 安装；标签胶囊不在此构造，由窗口
 *  在拿到 ContactModel 后动态填入 kIdTagFlow 容器。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildContactBody();

/**
 *  新建一枚标签胶囊控件，已设好文字与 ctrlId。
 *
 *  本函数把 TagChip 的构造收拢在 Layout 文件内，让窗口侧不必直接依赖
 *  TagChip 头 —— 与"布局相关的控件由 Layout 负责"的分层一致。
 *
 *    text：胶囊上的标签文字；nullptr 视为空。
 *    ctrlId：要赋给胶囊的数字 id（窗口按它定位被删除的标签）。
 *  返回：构造好的胶囊控件（所有权转移给调用方）。
 */
std::unique_ptr<balloonwjui::DuiControl> CreateTagChip(LPCTSTR text, UINT ctrlId);

/**
 *  新建一个「+ 添加标签」虚线按钮控件，已设好文字与 kIdAddTagButton id。
 *
 *  与 CreateTagChip 一样把布局自用控件的构造收拢在 Layout 文件内。窗口
 *  在重建标签区时把它追加到标签流式容器末尾。
 *
 *  返回：构造好的按钮控件（所有权转移给调用方）。
 */
std::unique_ptr<balloonwjui::DuiControl> CreateAddTagButton();

/**
 *  按 ctrlId 配置头部多行资料块（ContactHeaderInfo）的四行内容。
 *
 *  布局自用控件 ContactHeaderInfo 是文件局部类型，窗口拿不到其声明 ——
 *  本自由函数代窗口完成配置，按 id 在子树里定位该控件。
 *
 *    root：客户区根控件；nullptr 忽略。
 *    primaryName：第 1 行主名（备注名优先，否则真名）。
 *    realNameParen：第 1 行紧跟的括号真名（如「(江屿白)」），无则传空。
 *    titleDept：第 2 行「职务 · 部门」，无则传空（该行不画）。
 *    accountRegion：第 3 行「账号 XXX · 地区」（展示文本）。
 *    signature：第 4 行个性签名，无则传空（该行不画）。
 *    account：裸账号（不含"账号 "前缀与地区）；点击账号行时复制它到剪贴板,无则传空。
 */
void ConfigureContactHeader(balloonwjui::DuiControl* root,
                            LPCTSTR primaryName, LPCTSTR realNameParen,
                            LPCTSTR titleDept, LPCTSTR accountRegion,
                            LPCTSTR signature, LPCTSTR account);

/**
 *  按 ctrlId 修改一个单行文本控件（ContactText）的文本内容。
 *
 *  用于刷新「备注名」行右侧当前值显示。ContactText 是文件局部类型，
 *  本自由函数代窗口完成文本设置。
 *
 *    root：客户区根控件；nullptr 忽略。
 *    ctrlId：目标 ContactText 控件的 id（如 kIdRemarkValue）。
 *    text：新文本内容；nullptr 视为空。
 */
void SetContactText(balloonwjui::DuiControl* root, UINT ctrlId,
                    LPCTSTR text);

/**
 *  对客户区控件树重新跑一次布局，使新塞入 / 删除的标签胶囊立即生效。
 *
 *  标签流式容器（ContactTagFlow）会按可用宽度换行排布子胶囊；窗口在
 *  增删标签后调用本函数，传入客户区根控件即可触发整树重排。
 *
 *    root：客户区根控件（SetClientContent 时传入的那个）；nullptr 忽略。
 */
void RelayoutContactBody(balloonwjui::DuiControl* root);

} // namespace ballooncontact
