/**
 *  ProfileLayout.h
 *
 *  个人信息对话框客户区控件树的构造接口。布局以内嵌 XML 为唯一真值
 *  源，交给 balloonui 的 DuiXmlBuilder 解析。窗口代码按下面的 ProfileCtrlId
 *  数字 id 取回需要操作的控件。
 *
 *  对话框有"查看 / 编辑"两种模式：控件树一次性把两套控件全建出来，
 *  窗口按当前模式 SetVisible 显隐对应控件 —— 见 BalloonProfileWindow。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include <memory>
#include "../../balloonui/balloonui/DuiControl.h"

namespace balloonprofile {

/**
 *  个人信息对话框客户区控件的数字 id —— 窗口代码按这些 id 取回控件。
 *  全部落在 [900, 999] 区间，与本子项目其它窗口的 id 段不冲突。
 */
enum ProfileCtrlId
{
    // ---- 头部区 -------------------------------------------------------
    kIdAvatar       = 900,   // 头像（带铅笔角标的自绘控件 ProfileAvatar）

    // ---- 状态胶囊（四个，下标对应 ProfileStatus）---------------------
    kIdPillOnline    = 910,  // "在线"状态胶囊（ProfileStatusPill）
    kIdPillAway      = 911,  // "离开"状态胶囊（ProfileStatusPill）
    kIdPillBusy      = 912,  // "勿扰"状态胶囊（ProfileStatusPill）
    kIdPillInvisible = 913,  // "隐身"状态胶囊（ProfileStatusPill）

    // ---- 字段区：查看态的只读值文本（ProfileText）---------------------
    kIdViewNickname  = 920,  // 昵称 —— 查看态只读文本
    kIdViewSignature = 921,  // 个性签名 —— 查看态只读文本
    kIdViewGender    = 922,  // 性别 —— 查看态只读文本
    kIdViewBirthday  = 923,  // 生日 —— 查看态只读文本
    kIdViewRegion    = 924,  // 所在地 —— 查看态只读文本
    kIdViewEmail     = 925,  // 邮箱 —— 查看态只读文本

    // ---- 字段区：编辑态的可编辑控件 ----------------------------------
    kIdEditNickname  = 930,  // 昵称 —— 编辑态文本框（DuiEditHost）
    kIdEditSignature = 931,  // 个性签名 —— 编辑态文本框（DuiEditHost）
    kIdEditGender    = 932,  // 性别 —— 编辑态下拉框（DuiComboBox）
    kIdEditBirthday  = 933,  // 生日 —— 编辑态文本框（DuiEditHost）
    kIdEditRegion    = 934,  // 所在地 —— 编辑态文本框（DuiEditHost）
    kIdEditEmail     = 935,  // 邮箱 —— 编辑态文本框（DuiEditHost）

    // ---- 底部按钮区 ---------------------------------------------------
    //   查看态控件树含"关闭 + 编辑资料"；编辑态含"取消 + 保存"。
    kIdBtnClose     = 960,   // "关闭"按钮（ghost，仅查看态存在）
    kIdBtnEdit      = 961,   // "编辑资料"按钮（primary，仅查看态存在）
    kIdBtnCancel    = 962,   // "取消"按钮（ghost，仅编辑态存在）
    kIdBtnSave      = 963,   // "保存"按钮（primary，仅编辑态存在）
};

/**
 *  构造个人信息对话框的客户区控件树。
 *  对话框两态布局高度不同（编辑态字段行更高、底部按钮组不同），故按
 *  模式分别构造 —— 窗口在"查看 ⇄ 编辑"切换时整体重建客户区。
 *    editMode：true = 构造编辑态控件树（字段为文本框 / 下拉框，底部为
 *              取消 / 保存）；false = 构造查看态控件树（字段为只读文本，
 *              底部为关闭 / 编辑资料）。
 *  返回：客户区根控件（白底纵向布局容器）；解析失败返回 nullptr。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildProfileBody(bool editMode);

// 个人信息对话框客户区在某模式下的总高度（逻辑像素，不含标题栏）。
// 窗口创建 / 切换模式时据此 ResizeClient。
//   editMode：true = 编辑态高度；false = 查看态高度。
int ProfileBodyHeight(bool editMode);

// =====================================================================
// 布局自用控件的访问 helper —— ProfileText / ProfileStatusPill /
// ProfileAvatar 是 ProfileLayout.cpp 文件局部类型，窗口侧拿不到其声明，
// 故由本组函数代为按 ctrl-id 操作（在 .cpp 内 dynamic_cast 落地）。
// 文本框（DuiEditHost）/ 下拉框（DuiComboBox）是 balloonui 公共类型，
// 窗口可直接 dynamic_cast，不经这里。
// =====================================================================

// 设置查看态某只读值文本控件（ProfileText）的文本内容。
//   root：客户区根控件；ctrlId：目标控件 id（kIdView* 之一）；
//   text：新文本（nullptr 视为空）。控件不存在 / 类型不符时静默忽略。
void SetProfileViewText(balloonwjui::DuiControl* root, UINT ctrlId,
                        LPCTSTR text);

// 设置某状态胶囊（ProfileStatusPill）的选中态。
//   root：客户区根控件；ctrlId：目标胶囊 id（kIdPill* 之一）；
//   selected：是否选中。控件不存在 / 类型不符时静默忽略。
void SetProfilePillSelected(balloonwjui::DuiControl* root, UINT ctrlId,
                            bool selected);

// 读取某状态胶囊代表的在线状态（取值见 ProfileModel 的 ProfileStatus）。
//   root：客户区根控件；ctrlId：目标胶囊 id。
//   返回：胶囊的状态值；控件不存在 / 类型不符时返回 -1。
int GetProfilePillStatus(balloonwjui::DuiControl* root, UINT ctrlId);

// 设置头像（ProfileAvatar）的字母占位。
//   root：客户区根控件；ctrlId：头像 id（kIdAvatar）；
//   letter：字母占位（一般姓名首字，nullptr 视为空）。
void SetProfileAvatarLetter(balloonwjui::DuiControl* root, UINT ctrlId,
                            LPCTSTR letter);

} // namespace balloonprofile
