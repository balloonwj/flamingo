/**
 *  ContactModel.h
 *
 *  联系人资料对话框的数据模型 —— 持有被查看联系人的姓名 / 备注名 /
 *  账号 / 地区 / 部门 / 职务 / 个性签名 / 在线状态，以及对话框内可编辑
 *  的标签列表、分组、三个开关（消息免打扰 / 置顶聊天 / 加入黑名单）。
 *
 *  纯数据结构，不涉及任何控件 / 网络 / 持久化。当前以 mock 数据
 *  （「江屿白」那一组）初始化，真实数据接入待后续。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （需要 CString）。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include <vector>

namespace ballooncontact {

/**
 *  联系人在线状态。逐值对应设计稿 components.jsx 的 STATUS_LABEL：
 *  决定头像右下角状态点的颜色。
 */
enum ContactStatus
{
    kContactStatusOnline    = 0,   // 在线：状态点绿
    kContactStatusAway      = 1,   // 离开：状态点橙（本对话框 mock 取此值）
    kContactStatusBusy      = 2,   // 勿扰：状态点红
    kContactStatusInvisible = 3,   // 隐身：状态点灰
    kContactStatusOffline   = 4,   // 离线：不画状态点
};

/**
 *  对话框内的三个布尔开关。枚举值同时用作 ContactModel 内部开关
 *  数组的下标。
 */
enum ContactSwitchKind
{
    kContactSwitchMute      = 0,   // 消息免打扰：开则该联系人消息不提醒
    kContactSwitchPinned    = 1,   // 置顶聊天：开则会话置于消息列表顶部
    kContactSwitchBlocked   = 2,   // 加入黑名单：开则屏蔽该联系人
};

// 对话框内开关的总数。
const int kContactSwitchCount = 3;

/**
 *  ContactModel —— 联系人资料对话框的数据模型。
 *
 *  分两部分：
 *    · 只读资料区 —— 姓名 / 备注名 / 账号 / 地区 / 部门 / 职务 / 签名 /
 *      状态。由窗口在打开时一次性灌入（当前为 mock）。
 *    · 可编辑区 —— 标签列表、分组、三个开关。用户在对话框里增删标签 /
 *      切换分组 / 拨动开关时，窗口同步调用本模型的写接口。
 */
class ContactModel
{
public:
    // 构造：填入「江屿白」那一组 mock 数据（见各字段默认值说明）。
    ContactModel();

    // ---- 真实数据注入（替换 mock）----

    // 一次性灌入只读资料区（姓名 / 账号 / 地区 / 职务 / 部门路径 / 签名 / 状态）。
    // 备注名单独由 SetRemark 设；标签、分组另由 ClearTags / SetGroups 设。
    //   name      真名（昵称）
    //   account   账号
    //   region    所在地区，可空
    //   title     职务，可空
    //   dept      部门路径（如「产品部 / 设计组」），可空
    //   signature 个性签名，可空
    //   status    在线状态，取值见 ContactStatus
    void SetBasicInfo(const CString& name, const CString& account, const CString& region,
                      const CString& title, const CString& dept, const CString& signature,
                      int status);

    // 清空标签列表（真实数据暂无标签时调用，去掉 mock 标签）。
    void ClearTags();

    // 用给定分组列表替换 mock 分组，并选中 selected 项（越界则选 0）。
    //   groups   分组名列表（如仅一项部门路径）。
    //   selected 选中下标。
    void SetGroups(const std::vector<CString>& groups, int selected);

    // ---- 只读资料 ----

    // 真名（设计稿 contact.name），如「江屿白」。
    const CString& Name() const;

    // 备注名（设计稿 contact.remark），如「设计组组长」；可为空。
    const CString& Remark() const;

    // 设置备注名。供「备注名」行内编辑提交时调用。
    //   remark：新的备注名，允许为空字符串（表示清除备注）。
    void SetRemark(LPCTSTR remark);

    // 账号（设计稿 contact.account），如「10923847」。
    const CString& Account() const;

    // 所在地区（设计稿 contact.region），如「北京」。
    const CString& Region() const;

    // 职务（设计稿 contact.title），如「设计组组长」；可为空。
    const CString& Title() const;

    // 部门路径（设计稿 contact.dept），如「产品部 / 设计组」；可为空。
    const CString& Dept() const;

    // 个性签名（设计稿 contact.signature），如「在出差路上…」；可为空。
    const CString& Signature() const;

    // 在线状态，取值见 ContactStatus。
    int Status() const;

    // 头像占位字母（真名首字），如「江」。
    CString AvatarLetter() const;

    // ---- 标签 ----

    // 当前标签数。
    int TagCount() const;

    // 读取第 index 个标签文本。index 越界返回空 CString。
    //   index：取值 [0, TagCount())。
    CString Tag(int index) const;

    // 在末尾追加一个标签。空白文本（去首尾空格后为空）忽略，返回 false。
    //   tag：标签文本。
    //   返回：true 表示已追加；false 表示文本为空被忽略。
    bool AddTag(LPCTSTR tag);

    // 删除第 index 个标签。index 越界则忽略。
    //   index：取值 [0, TagCount())。
    void RemoveTag(int index);

    // ---- 分组 ----

    // 可选分组的总数。
    int GroupCount() const;

    // 读取第 index 个分组名。index 越界返回空 CString。
    //   index：取值 [0, GroupCount())。
    CString Group(int index) const;

    // 当前选中的分组下标，取值 [0, GroupCount())。
    int SelectedGroup() const;

    // 设置当前选中的分组。index 越界则忽略。
    //   index：取值 [0, GroupCount())。
    void SetSelectedGroup(int index);

    // ---- 开关 ----

    // 读取某个开关的状态。kind 越界返回 false。
    //   kind：开关类别，取值见 ContactSwitchKind。
    bool Switch(int kind) const;

    // 设置某个开关的状态。kind 越界则忽略。
    //   kind：开关类别，取值见 ContactSwitchKind；on：true 开 / false 关。
    void SetSwitch(int kind, bool on);

private:
    // 判定 kind 是否是合法的开关下标。
    static bool IsValidSwitch(int kind);

private:
    CString m_name;        // 真名（contact.name）
    CString m_remark;      // 备注名（contact.remark），可空
    CString m_account;     // 账号（contact.account）
    CString m_region;      // 所在地区（contact.region）
    CString m_title;       // 职务（contact.title），可空
    CString m_dept;        // 部门路径（contact.dept），可空
    CString m_signature;   // 个性签名（contact.signature），可空
    int     m_status;      // 在线状态，见 ContactStatus

    std::vector<CString> m_tags;    // 标签列表，用户可增删
    std::vector<CString> m_groups;  // 可选分组名（mock 固定列表）
    int     m_selectedGroup;        // 当前选中的分组下标

    bool    m_switches[kContactSwitchCount];   // 三个开关状态，下标见 ContactSwitchKind
};

} // namespace ballooncontact
