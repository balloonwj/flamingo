/**
 *  MemberPickerLayout.h
 *
 *  选人对话框（CCreateGroupDlg：新建群聊、邀请入群、选禁言成员、转让群主等共用）的控件树构建，以及邀请入群时
 *  「入群后可查看的聊天记录」选项组的取值规则（docs/邀请入群方案.md §3.2）。
 *
 *  控件树原先直接写在 CCreateGroupDlg::BuildUiOnce 里，2026-10-04 邀请入群第 2 期加选项组时抽到这里：
 *  对话框与布局单测调用同一个函数，单测测到的就是实际显示的那棵树（UI 约定第 7 条）。本文件不依赖
 *  对话框类，也不 include Flamingo 的 stdafx.h，与 FlamingoUnit.vcxproj 共用。
 *
 *  典型用法：
 *      balloonmain::MemberPickerLayoutSpec spec;
 *      spec.m_showInviteHistory = true;
 *      spec.m_confirmText = TR(L"邀请入群");
 *      balloonmain::MemberPickerLayoutRefs refs;
 *      std::unique_ptr<balloonwjui::DuiVBox> root = balloonmain::BuildMemberPickerLayout(spec, refs);
 *      ...
 *      int limit = 0;
 *      int err = balloonmain::ParseInviteHistoryLimit(true, balloonmain::InviteHistoryModeOf(refs.m_history),
 *                                                     refs.m_history.m_count->GetText(), limit);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <memory>

namespace balloonwjui {
class DuiVBox;
class DuiEdit;
class DuiButton;
class DuiLabel;
class DuiScrollView;
}

namespace balloonmain {

class MemberPickerTreeView;

// ---- 对话框尺寸（逻辑像素，DuiFrameWindow 的整窗尺寸，含标题栏，见 UI 约定第 7 条）--------------
// 初始尺寸：2026-10-02 修复 ResizeClient 多叠加系统边框的问题时由 760×560 加上修复前多叠加的 18×47（120 DPI 实测），
// 保持修复前的窗口大小。对话框可以缩放，最小尺寸由布局单测（InviteHistoryTests 的 IH4）核对排得下
const int kPickerDlgWidth     = 778;
const int kPickerDlgHeight    = 607;
const int kPickerDlgMinWidth  = 640;
const int kPickerDlgMinHeight = 460;
const int kPickerDlgTitleBarH = 36;   // 标题栏高，含在上面的整窗高度之内

// ---- 控件 id（对话框内唯一即可）----------------------------------------
const UINT kPickerIdSearch       = 1;   // 搜索联系人输入框
const UINT kPickerIdName         = 2;   // 群聊名称输入框（只有新建群聊有）
const UINT kPickerIdMemberScroll = 3;   // 左侧成员滚动区
const UINT kPickerIdChipScroll   = 4;   // 右侧已选成员滚动区
const UINT kPickerIdCount        = 5;   // 底部"已选 N 人"标签
const UINT kPickerIdCancel       = 6;   // 取消按钮
const UINT kPickerIdConfirm      = 7;   // 确认按钮（创建群聊 / 邀请入群等，文字按用途）
// 「入群后可查看的聊天记录」选项组（只有邀请入群有），从 20 起编，与上面一段以及成员行复选框（1000 起）、
// 已选成员移除按钮（2000 起）都不重叠
// 2026-10-04 用户反馈「同步全部」勾选框加「仅同步最近 N 条」的组合让人迷惑，改为三个单选项：全部 / 最近 N 条 / 不同步
const UINT kPickerIdHistoryTitle  = 20;  // 选项组标题
const UINT kPickerIdHistoryAll    = 21;  // 「全部聊天记录」单选按钮
const UINT kPickerIdHistoryCount  = 22;  // 「最近 N 条」的条数输入框
const UINT kPickerIdHistoryHint   = 23;  // 选项组下方的灰色说明 / 红色错误提示
const UINT kPickerIdHistoryRecent = 24;  // 「最近 N 条」单选按钮（与条数输入框排在同一行）
const UINT kPickerIdHistoryNone   = 25;  // 「不同步」单选按钮
// 「群类型」选项组（只有新建群聊、且帖子群功能可用时有，docs/帖子群方案.md 7.2），从 30 起编
const UINT kPickerIdGroupTypeTitle  = 30;  // 「群类型」标题
const UINT kPickerIdGroupTypeNormal = 31;  // 「普通群」单选按钮（默认选中）
const UINT kPickerIdGroupTypeThread = 32;  // 「帖子群」单选按钮
const UINT kPickerIdGroupTypeHint   = 33;  // 选项下方的灰色说明

// ---- 同步条数的取值（与服务端 kInviteHistoryLimitAll / kInviteHistoryLimitMax 一致）----
const int kInviteHistoryAll          = -1;     // 同步全部：请求里不带 historylimit
const int kInviteHistoryMax          = 10000;  // 「仅同步最近 N 条」的上限
const int kInviteHistoryDefaultCount = 100;    // 「最近 N 条」条数输入框的初值

// 选项组当前选中的是哪一项（三个单选项互斥），InviteHistoryModeOf 的返回值
enum InviteHistoryMode
{
    kInviteHistoryModeAll    = 0,   // 「全部聊天记录」：请求里不带 historylimit（默认选中）
    kInviteHistoryModeRecent = 1,   // 「最近 N 条」：按条数输入框取 1 ~ kInviteHistoryMax
    kInviteHistoryModeNone   = 2,   // 「不同步」：historylimit 为 0，新成员只能看到入群之后的消息
};

// 条数输入框里的内容不合法的几种情况，ParseInviteHistoryLimit 的返回值
enum InviteHistoryError
{
    kInviteHistoryOk        = 0,   // 合法，出参里是要发出的条数
    kInviteHistoryEmpty     = 1,   // 选了「最近 N 条」却没有填条数
    kInviteHistoryNotNumber = 2,   // 填的不是非负整数（粘贴进来的字母、负号等）
    kInviteHistoryTooLarge  = 3,   // 超过 kInviteHistoryMax
    kInviteHistoryZero      = 4,   // 选了「最近 N 条」却填了 0：不想同步应选「不同步」
};

/**
 *  InviteHistoryControls —— 「入群后可查看的聊天记录」选项组里对话框要读写的控件。指针都借用自控件树，
 *  树销毁后失效；选项组没有构建（非邀请入群）时全部为 NULL。
 */
struct InviteHistoryControls
{
    balloonwjui::DuiButton* m_all;     // 「全部聊天记录」单选按钮
    balloonwjui::DuiButton* m_recent;  // 「最近 N 条」单选按钮
    balloonwjui::DuiButton* m_none;    // 「不同步」单选按钮
    balloonwjui::DuiEdit*   m_count;   // 「最近 N 条」的条数输入框
    balloonwjui::DuiLabel*  m_hint;    // 选项组下方的说明 / 错误提示

    InviteHistoryControls()
        : m_all(NULL), m_recent(NULL), m_none(NULL), m_count(NULL), m_hint(NULL)
    {
    }

    // 三个单选按钮与条数输入框是否都已构建（说明标签不在此列）
    bool IsBuilt() const
    {
        return m_all != NULL && m_recent != NULL && m_none != NULL && m_count != NULL;
    }
};

/**
 *  MemberPickerLayoutSpec —— BuildMemberPickerLayout 的入参：本次按哪种用途搭界面。
 */
struct MemberPickerLayoutSpec
{
    bool    m_showNameInput;       // 是否显示群名输入框（只有新建群聊用）
    bool    m_showInviteHistory;   // 是否显示「入群后可查看的聊天记录」选项组（只有邀请入群用）
    bool    m_showGroupType;       // 是否显示「群类型：普通群 / 帖子群」选项组（新建群聊且帖子群功能可用时）
    bool    m_groupAllowsHistory;  // 群是否允许新成员查看入群前的历史；为 false 时选项组整组置灰并在下方说明原因
    bool    m_inviteConfirm;       // 本企业是否开启了「邀请入群需被邀请人同意」；为 true 时选项组下方说明对方同意后才入群
    CString m_confirmText;         // 底部确认按钮的文字（已按界面语言翻译）

    MemberPickerLayoutSpec()
        : m_showNameInput(false)
        , m_showInviteHistory(false)
        , m_showGroupType(false)
        , m_groupAllowsHistory(true)
        , m_inviteConfirm(false)
    {
    }
};

/**
 *  MemberPickerLayoutRefs —— BuildMemberPickerLayout 的出参：对话框后续要读写的控件。指针都借用自控件树，
 *  树销毁后失效；没有构建的控件为 NULL（如非新建群聊时的群名输入框、非邀请入群时的选项组）。
 */
struct MemberPickerLayoutRefs
{
    balloonwjui::DuiEdit*       m_searchInput;   // 搜索联系人输入框
    balloonwjui::DuiEdit*       m_nameInput;     // 群聊名称输入框
    balloonwjui::DuiScrollView* m_memberScroll;  // 左侧成员滚动区
    MemberPickerTreeView*       m_memberTree;    // 左侧候选树
    balloonwjui::DuiScrollView* m_chipScroll;    // 右侧已选成员滚动区
    balloonwjui::DuiLabel*      m_countLabel;    // 底部"已选 N 人"标签
    InviteHistoryControls       m_history;       // 「入群后可查看的聊天记录」选项组的控件；非邀请入群时全部为 NULL
    balloonwjui::DuiButton*     m_groupTypeNormal; // 「普通群」单选按钮；没有群类型选项组时为 NULL
    balloonwjui::DuiButton*     m_groupTypeThread; // 「帖子群」单选按钮；没有群类型选项组时为 NULL
    balloonwjui::DuiButton*     m_confirmButton; // 底部确认按钮（创建群聊 / 邀请入群等）

    MemberPickerLayoutRefs()
        : m_searchInput(NULL), m_nameInput(NULL), m_memberScroll(NULL), m_memberTree(NULL)
        , m_chipScroll(NULL), m_countLabel(NULL), m_groupTypeNormal(NULL), m_groupTypeThread(NULL)
        , m_confirmButton(NULL)
    {
    }
};

/**
 *  按用途搭出选人对话框的整棵控件树：上半区左列为搜索框与候选树，右列为（群名输入框）、「已选成员」标题、
 *  已选成员滚动区与（同步选项组）；下方为底部栏（已选 N 人、取消、确认）。
 *  @param spec 本次的用途与选项组状态
 *  @param refs [出参] 对话框后续要读写的控件（借用自返回的树）
 *  @return 控件树根，所有权交给调用方（通常转交 DuiFrameWindow::SetClientContent）
 */
std::unique_ptr<balloonwjui::DuiVBox> BuildMemberPickerLayout(const MemberPickerLayoutSpec& spec,
                                                              MemberPickerLayoutRefs& refs);

/**
 *  按当前选中的单选项刷新选项组里各控件的可用状态：群不允许看历史时三个单选项与条数输入框都不可操作；
 *  否则只有选中「最近 N 条」时条数输入框可编辑。
 *  @param controls           选项组的控件；没有构建时什么都不做
 *  @param groupAllowsHistory 群是否允许新成员查看入群前的历史
 */
void ApplyInviteHistoryEnableState(const InviteHistoryControls& controls, bool groupAllowsHistory);

/**
 *  选项组当前选中的是哪一项。三个单选按钮不在同一个父容器里（「最近 N 条」与条数输入框同排一行），
 *  balloonui 的单选按钮只在同一父容器内互斥，所以互斥由 SelectInviteHistoryMode 负责，本函数只读状态。
 *  @param controls 选项组的控件
 *  @return InviteHistoryMode 中的一个值；没有构建或一项都没选中时返回 kInviteHistoryModeAll
 */
int InviteHistoryModeOf(const InviteHistoryControls& controls);

/**
 *  选中某一项并取消另外两项（不触发控件的值变化通知），再按选中项刷新可用状态。对话框在用户点了任一单选按钮后调用，
 *  重置选项组时也用它选回「全部」。
 *  @param controls           选项组的控件；没有构建时什么都不做
 *  @param mode               要选中的项，取值见 InviteHistoryMode；认不出的取值按 kInviteHistoryModeAll 处理
 *  @param groupAllowsHistory 群是否允许新成员查看入群前的历史
 */
void SelectInviteHistoryMode(const InviteHistoryControls& controls, int mode, bool groupAllowsHistory);

/**
 *  「最近 N 条」单选按钮的宽度（逻辑像素）：单选圆点与左右留白，加上「最近」两字按控件默认字体量出的宽度。
 *  balloonui 的 DuiButton / DuiLabel 没有覆写 GetDesiredSize，布局里给它们 Hint().Auto() 得到的宽度是 0，
 *  按文字宽度占位的控件要先量好再用 Fixed（2026-10-04 实测：前缀文字因此整段不显示）。
 *  @return 宽度，单位逻辑像素
 */
int InviteRecentRadioWidth();

/**
 *  「群类型」选项组当前选中的群类型。
 *  @param normal 「普通群」单选按钮（借用），可为 NULL
 *  @param thread 「帖子群」单选按钮（借用），可为 NULL
 *  @return 选中「帖子群」时为 1（group_type_thread）；其余情况（包括没有构建选项组）为 0（普通群）
 */
int SelectedGroupType(const balloonwjui::DuiButton* normal, const balloonwjui::DuiButton* thread);

/**
 *  切换确认按钮的「正在提交」状态（2026-10-08 用户反馈建大群时界面像卡住了）：建群请求发出之后、应答回来之前，
 *  按钮变为不可点、文字为「正在创建…」，让人知道请求已经发出、正在等服务端；应答失败或重新打开对话框时恢复。
 *  @param confirm  确认按钮（借用），可为 NULL（界面还没建时什么也不做）
 *  @param busy     true 为进入等待状态，false 为恢复
 *  @param idleText 恢复时的按钮文字（按用途，如「创建群聊」）
 */
void SetPickerConfirmBusy(balloonwjui::DuiButton* confirm, bool busy, const CString& idleText);

/**
 *  由选项组的状态得出请求里的 historylimit（docs/邀请入群方案.md §3.2、§4.2）。纯函数。
 *  @param groupAllowsHistory 群是否允许新成员查看入群前的历史；为 false 时一律按全部（以群设置为准，请求里不带）
 *  @param mode               选中的单选项，取值见 InviteHistoryMode
 *  @param countText          条数输入框里的文字，只有选中「最近 N 条」时才看；首尾空白忽略
 *  @param outLimit           [出参] 合法时为要发出的条数：kInviteHistoryAll（全部）、0（不同步）或 1 ~ kInviteHistoryMax；
 *                            不合法时不修改
 *  @return InviteHistoryError 中的一个值；kInviteHistoryOk 表示合法
 */
int ParseInviteHistoryLimit(bool groupAllowsHistory, int mode, const CString& countText, int& outLimit);

/**
 *  选项组下方平时显示的灰色说明（已按界面语言翻译）：群不允许看历史时说明原因，开启了「需被邀请人同意」时
 *  说明对方同意后才入群，两者都有时分两行。条数不合法时这里临时换成红色的错误提示，改对后恢复成本函数的结果。
 *  @param groupAllowsHistory 群是否允许新成员查看入群前的历史
 *  @param inviteConfirm      是否开启了「邀请入群需被邀请人同意」
 *  @return 说明文字；两项都不需要说明时返回空串
 */
CString InviteHistoryHintText(bool groupAllowsHistory, bool inviteConfirm);

/**
 *  条数不合法时显示在选项组下方的提示文字（已按界面语言翻译）。
 *  @param error ParseInviteHistoryLimit 的返回值
 *  @return 提示文字；kInviteHistoryOk 返回空串
 */
CString InviteHistoryErrorText(int error);

/**
 *  邀请发出后按应答里的统计选择提示文字（docs/邀请入群方案.md §3.2）。纯函数。
 *  直接入群的人不需要提示（群里会出现入群提示），故不接收这一项；有发出卡片的提示对方同意后入群，
 *  有转为审批的提示等待审批，两者都有时分两行。
 *  @param invited 发出入群邀请卡片的人数
 *  @param pending 转为加群申请、等待审批的人数
 *  @return 提示文字（已按界面语言翻译）；不需要提示时返回空串
 */
CString InviteResultTipText(int invited, int pending);

} // namespace balloonmain
