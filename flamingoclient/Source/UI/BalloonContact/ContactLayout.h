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
#include <vector>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "ContactModel.h"          // ContactProfileCell：「个人资料」区块的格子数据

namespace balloonwjui {
class DuiAvatar;
class DuiLabel;
}

namespace ballooncontact {

class ContactModel;

// 资料窗头部头像（字形头像）在彩色状态下的底色，与 BuildContactBody 内嵌 XML 里 avatar 的
// fallback-bg-color 相同；离线时换成它的等亮度灰色（见 ApplyContactAvatar）。
const COLORREF kContactAvatarBgColor = RGB(59, 130, 246);

/**
 *  联系人资料对话框客户区控件的数字 id —— 窗口代码按这些 id 取回控件。
 *  全部落在任务约定的 1000–1099 区间内。
 */
enum ContactCtrlId
{
    kIdAvatar         = 1000,   // 头部头像（DuiAvatar 的子类，看本人时可点击打开修改头像对话框，2026-10-07）
    kIdHeaderInfo     = 1001,   // 头部右侧多行资料块（ContactHeaderInfo 自绘）
    kIdDeptPath       = 1002,   // 部门路径整宽区块（ContactDeptPath 自绘，可折行）
    kIdCopyToast      = 1003,   // 复制成功后的轻量提示（DuiToast，浮在客户区顶部）
    kIdProfileBlock   = 1004,   // 「个人资料」区块（vbox）；没有可显示的字段时不可见、高度为 0
    kIdProfileSeparator = 1005, // 「个人资料」区块下方的分隔线，随区块一起显隐
    kIdProfileToggle  = 1006,   // 「个人资料」小标题右侧的「展开全部 / 收起」文字链接（DuiLabel）

    kIdTagFlow        = 1010,   // 标签流式容器（ContactTagFlow，窗口往里塞胶囊）
    kIdAddTagButton   = 1011,   // 「+ 添加标签」虚线按钮（ContactAddTagButton 自绘）
    kIdTagBlock       = 1012,   // 标签区整块（小标题 + 滚动视口的 vbox）；高度随标签行数改写
    kIdTagScroll      = 1013,   // 标签流式容器外面的滚动视口（DuiScrollView），超过三行时出滚动条
    kIdTagLimitHint   = 1014,   // 标签数达上限时顶替「+ 添加标签」的说明文字（ContactTagLimitHint 自绘）
    // 下面四个是看本人时整块隐藏的区块（2026-10-04，见 ApplyContactSelfView）
    kIdTagSeparator    = 1015,  // 标签区下方的分隔线
    kIdRemarkSection   = 1016,  // 备注名 + 分组两行所在的 vbox
    kIdRemarkSeparator = 1017,  // 备注名 / 分组区下方的分隔线
    kIdNotifySection   = 1018,  // 消息提醒 + 添加到收藏两行所在的 vbox

    kIdRemarkValue    = 1020,   // 「备注名」行右侧的当前备注文字（ContactValueText）
    kIdRemarkEditBtn  = 1021,   // 「备注名」行右侧的铅笔编辑按钮（ContactIconButton）
    kIdRemarkEdit     = 1022,   // 「备注名」行内编辑用文本框（DuiEdit，默认隐藏）
    kIdGroupCombo     = 1023,   // 「分组」行的下拉框（DuiComboBox）

    // 1030 曾是「消息免打扰」开关，1034 曾是「特别关注」开关。2026-10-03 两者合并成
    // 「消息提醒」下拉框 kIdNotifyCombo（五种提醒模式中单聊可用的四种），编号留空不复用。
    // 1031 曾是「置顶聊天」开关 —— 见 ContactSwitchKind 的说明，本轮整项移除；
    // 编号留空不复用。
    // 1032 曾是「加入黑名单」开关。企业版取消好友关系后该行整行移除；
    // 编号留空不复用，避免与旧版本布局对不上号。
    kIdSwitchFavorite = 1033,   // 「添加到收藏」开关（DuiSwitch）
    kIdNotifyCombo    = 1035,   // 「消息提醒」行的下拉框（DuiComboBox）；第 i 项即 NotifyModeMenu.h 的 GetNotifyModeChoices(false) 第 i 个模式

    kIdBtnClose       = 1040,   // 底部「关闭」按钮（ContactActionButton，ghost）
    kIdBtnSend        = 1041,   // 底部「发消息」按钮（ContactActionButton，primary）
    kIdBtnVoice       = 1042,   // 快捷操作行「语音通话」按钮（ContactActionButton，ghost）
    kIdBtnVideo       = 1043,   // 快捷操作行「视频通话」按钮（ContactActionButton，ghost）
    kIdBtnHistory     = 1044,   // 快捷操作行「历史消息」按钮（ContactActionButton，ghost）
    kIdBtnLocate      = 1045,   // 快捷操作行「组织架构」按钮（ContactActionButton，ghost）
    kIdQuickRow       = 1046,   // 快捷操作行容器（hbox，四个按钮等宽平分）

    // 标签胶囊的 ctrlId 基址 —— 第 i 枚胶囊取 kIdTagChipBase + i。
    // 留出 [1050, 1099] 共 50 个号，足够任何合理数量的标签。
    kIdTagChipBase    = 1050,

    // 「个人资料」区块的格子较多，单独占用 1100–1199 一段（2026-10-04）：第 s 个格子槽位的各控件取
    // ContactProfileCtrlId(s, 部件)，第 r 行的横向容器取 kIdProfileRowBase + r。
    kIdProfileCellBase = 1100,   // 格子槽位控件 id 的起点（九个槽位占 1100–1189）
    kIdProfileRowBase  = 1190,   // 行容器 id 的起点；r = 0 是昵称独占的一行，r = 1 是个性签名独占的一行
};

// 「个人资料」区块的格子槽位数：0 号槽位放昵称、1 号槽位放个性签名（各独占一行），2～8 号槽位按两列排
// 其余七个字段（2026-10-07 加昵称之前是 0 号签名、1～7 号两列，共 8 个槽位）。
const int kContactProfileSlotCount = 9;

// 两列格子的第一个槽位。
const int kContactProfileFirstGridSlot = 2;

// 「个人资料」区块的行容器数：昵称一行、个性签名一行，加上七个字段按两列排的四行。
const int kContactProfileRowSlots = 6;

/**
 *  「个人资料」区块第 row 行容纳的槽位区间：0 行是昵称槽位，1 行是签名槽位，此后每行两个两列槽位。
 *    row：      行号，取值 [0, kContactProfileRowSlots)。
 *    firstSlot：[出参] 这一行的第一个槽位。
 *    lastSlot： [出参] 这一行的最后一个槽位（含）；已夹在 kContactProfileSlotCount 之内。
 *  纯函数。
 */
inline void ContactProfileRowSlotRange(int row, int& firstSlot, int& lastSlot)
{
    if (row < kContactProfileFirstGridSlot)
    {
        firstSlot = row;
        lastSlot = row;
        return;
    }
    const int kColumns = 2;
    firstSlot = kContactProfileFirstGridSlot + (row - kContactProfileFirstGridSlot) * kColumns;
    lastSlot = firstSlot + kColumns - 1;
    if (lastSlot >= kContactProfileSlotCount)
    {
        lastSlot = kContactProfileSlotCount - 1;
    }
}

// 每个格子槽位占用的控件 id 个数。
const int kIdProfileCellStride = 10;

// 格子槽位里的各个部件，值是相对该槽位起始 id 的偏移。
enum ContactProfileCellPart
{
    kProfilePartCell   = 0,   // 格子本身（vbox）；槽位空着时不可见
    kProfilePartLabel  = 1,   // 字段名（ContactText）
    kProfilePartPencil = 2,   // 编辑按钮（ContactIconButton）；只有看本人、字段可改时可见
    kProfilePartValue  = 3,   // 值（ContactText）；本人没填的字段显示弱色的「未填写」
    kProfilePartEdit   = 4,   // 就地编辑用的输入框（DuiEdit）；不在编辑时不可见
    kProfilePartCombo  = 5,   // 就地编辑性别用的下拉框（DuiComboBox，保密 / 男 / 女）；不在编辑时不可见
    kProfilePartVisibility = 6,   // 公开范围开关（DuiLabel 链接，「仅自己可见 ▾」）；只有看本人时的手机号、座机两格可见（2026-10-06）
};

/**
 *  「个人资料」区块里某个格子槽位的某个部件的控件 id。
 *    slot：槽位，取值 [0, kContactProfileSlotCount)。
 *    part：部件，取值见 ContactProfileCellPart。
 *  @return 控件 id。纯函数。
 */
inline UINT ContactProfileCtrlId(int slot, int part)
{
    return (UINT)(kIdProfileCellBase + slot * kIdProfileCellStride + part);
}

// 看别人时「个人资料」区块默认显示的行数；多出来的行收起，点「展开全部」再显示（2026-10-04 用户确定：
// 资料填得全的人全部展开时窗口约 950 像素高，小屏幕放不下）。
const int kContactProfileCollapsedRows = 2;

// =====================================================================
// 客户区尺寸常量 —— 窗口侧与布局侧共用同一份，避免两处各写一个 420
// =====================================================================

// 客户区宽度（逻辑像素）。原先照设计稿 dialogs.jsx 的 ContactInfoDialog width:420；2026-10-04 为
// 「个人资料」区块的两列排版加宽到 600（docs/联系人资料字段扩充方案.md 4.2 节）。
const int kContactBodyWidth = 600;

// 标题栏高度（逻辑像素，照设计稿 Modal header 44）。窗口高度 = 客户区内容高度
// （ContactBodyHeight）+ 本值。
const int kContactTitleBarHeight = 44;

// 客户区左右内边距（逻辑像素，单边）。与内嵌 XML 中 contact-root 的
// padding="20,18,20,18" 的左右两个数一致。
const int kContactBodyPaddingH = 20;

// 部门路径整宽区块的可用宽度（逻辑像素）= 600 - 20 - 20 = 560。
// 部门路径独占一行整宽区域，不再与头像 / 职务共用一行，故可用宽度比
// 头部右侧文字块（302）宽得多，绝大多数部门路径一行就能放下。
const int kContactDeptPathAvailWidth = kContactBodyWidth - kContactBodyPaddingH * 2;

// 部门路径最多显示的行数。超出的部分由最后一行做中段省略。取 3 行的依据：
// 每行 380 像素、12 像素字号下约容 31 个汉字，3 行足以容下 90 个汉字以上的
// 部门路径；再多就属于异常数据，没必要为它把窗口撑高。
const int kContactDeptPathMaxLines = 3;

// 标签流式容器可用宽度的<u>估算值</u>（逻辑像素）= 600 - 20 - 20 = 560。标签区
// vbox 的 padding 只有上下两个方向（见内嵌 XML 的 padding="0,14,0,14"），故流式
// 容器的宽度就是客户区去掉左右内边距之后的整宽。
//
// 说它是估算值，是因为 SyncContactTagArea 数行数时以滚动视口实际布局出来的宽度为准，
// 量到的宽度不可信时才退回用本常量。2026-10-02 之前 ResizeClient 会多叠加一圈系统
// 边框，客户区比 kContactBodyWidth 宽（实测 438 而非 420）；此后客户区宽度
// 就是 kContactBodyWidth，本常量与实测值一致。
const int kContactTagFlowAvailWidth = kContactBodyWidth - kContactBodyPaddingH * 2;

// 判断测量宽度是否可信时，在 kContactBodyWidth 之上允许的余量（逻辑像素，左右合计）。
// 2026-10-02 之前 ResizeClient 按窗口样式多叠加一圈系统边框，客户区比指定宽度多出
// 十几到几十像素（实测 438 而非 420，随 DPI 变化），本余量是为它留的；此后
// 客户区宽度恰好等于 kContactBodyWidth，余量只作为容差保留，不影响判定结果。
const int kContactFrameBorderAllowance = 64;

// 标签流式容器可用宽度的<u>可信上限</u>（逻辑像素）。SyncContactTagArea 优先采用
// 滚动视口实际布局出来的宽度，但只有不超过本上限的测量值才作数。
//
// 判据：本对话框是固定宽度窗口（建窗时 SetResizable(false)，客户区宽度恒按
// kContactBodyWidth 调整），标签区又总是占满客户区去掉左右内边距之后的整宽，因此
// 可用宽度在任何时刻都不可能超过"常量宽度 + 余量"。超过就只有一种解释：
// 控件此刻量到的不是本对话框的宽度。
//
// 之所以要有这条判据：窗口先以 CWindow::rcDefault 建出来、随后才由 ApplyBodyHeight
// 调 ResizeClient 调整到真实尺寸，界面第一次构建时控件树仍停在窗口的默认尺寸上，
// 滚动视口量到的宽度实测为 1382 像素。按这个宽度数行数会把十来个标签算成一行，
// 滚动视口的内容总高度也只有一行，第二行往后的标签既显示不出来、也滚不到。原先的
// 兜底判据是"量到的宽度 <= 0"，挡不住这种非零的错误值。
const int kContactTagFlowMaxTrustedWidth =
    kContactTagFlowAvailWidth + kContactFrameBorderAllowance;

// 标签区最多把窗口撑高到几行。再多的标签留给滚动视口滚着看 —— 标签本来是
// "一眼扫过去"的辅助信息，为它把整个对话框拉得比屏幕还高并不合算。
const int kContactTagMaxRows = 3;

// 快捷操作行（语音 / 视频 / 历史 / 定位四个按钮）的行高（逻辑像素），与内嵌 XML 中
// 该 hbox 的 fixedHeight 一致。
const int kContactQuickRowHeight = 36;

// 快捷操作行与上方部门路径、下方分隔线之间的间距（逻辑像素，单侧），与内嵌 XML 中
// 它前后各一个 spacer control 的 fixedHeight 一致。
const int kContactQuickRowGap = 14;

/**
 *  把部门路径按层级切成一个个段名。
 *
 *  纯函数，不碰任何控件与 GDI，便于单元测试。以 '/' 为分隔字符切分 ——
 *  路径拼接侧（CBalloonMainPanel::ComputeContactDeptInfo）用的分隔串是
 *  " / "，其中唯一的非空白字符正是 '/'。每段去掉首尾空白；切出来为空的
 *  段直接丢弃，因此连续分隔符不会产生空段。
 *
 *    deptPath：完整部门路径，如「产品部 / 设计组」；nullptr 或空串视为无部门。
 *    outSegments：出参，按从根到叶的顺序存放各级部门名；函数内先清空。
 */
void SplitDeptPathSegments(LPCTSTR deptPath, std::vector<CString>& outSegments);

/**
 *  把已量好宽度的各级部门名贪心装箱成若干行。
 *
 *  纯整数运算、不依赖 GDI，便于单元测试。规则：同一行内相邻两段之间要
 *  插一个分隔符，故行宽 = 各段宽之和 + 分隔符宽 ×(本行段数 - 1)；行宽不
 *  超过 availWidth 就继续往本行放，超了就换行。两种兜底：行首那一段无论
 *  多宽都先放进本行（单段自身超宽由绘制层按字符再切）；行数已达上限时，
 *  剩余各段全部并入最后一行（由绘制层做中段省略）。
 *
 *    segWidths：各级部门名的像素宽度，顺序与段序一致；空表示无部门。
 *    sepWidth：分隔符（" / "）的像素宽度。
 *    availWidth：单行可用宽度（像素）。<= 0 时每行只放得下行首一段。
 *    maxLines：最多行数；小于 1 按 1 处理。
 *    outLineEndSeg：出参，第 k 项是第 k 行的结束段下标（不含），即第 k 行
 *                   覆盖 [前一项, 本项) 这些段；函数内先清空。
 *  返回：行数（= outLineEndSeg.size()）；segWidths 为空时返回 0。
 */
int PackDeptPathLines(const std::vector<int>& segWidths, int sepWidth,
                      int availWidth, int maxLines,
                      std::vector<int>& outLineEndSeg);

/**
 *  部门路径区块按行数算出的高度（逻辑像素）。
 *
 *  纯函数。0 行（联系人没有部门）返回 0 —— 此时区块整个不占位，窗口高度
 *  与加这块之前完全一致。
 *
 *    lineCount：折行后的行数；<= 0 一律按 0 行处理。
 *  返回：区块高度（含区块与头部之间的间距）。
 */
int ContactDeptBlockHeight(int lineCount);

/**
 *  标签区整块（小标题 + 流式胶囊容器）按标签行数算出的高度（逻辑像素）。
 *
 *  纯函数。1 行时返回的正是内嵌 XML 里写死的那个高度，故只有一行标签的
 *  联系人界面与加入本函数之前逐像素一致；每多一行标签，本块增高"行高 + 行距"。
 *  行数被夹在 [1, kContactTagMaxRows] 内 —— 一个标签都没有时标签区仍要留出
 *  一行的位置给「+ 添加标签」按钮，超过上限的部分交给滚动视口。
 *
 *    tagRowCount：标签流式排布后的行数。
 *  返回：标签区整块的高度。
 */
int ContactTagBlockHeight(int tagRowCount);

/**
 *  客户区总高度（逻辑像素）—— 窗口按它 SetMinSize / ResizeClient。
 *
 *  纯函数。部门路径区块与标签区都是高度可变的，两者必须由这一个函数一起算出
 *  来：各算各的、各自 ResizeClient 的话，后调的那一次会把先调的那一次算进去的
 *  增量抹掉，表现为改了部门路径高度就丢、或者加了标签窗口不长高。
 *
 *  ContactBodyHeight(0, 1)（没有部门路径、标签只有一行、个人资料区块不显示）恒等于内嵌 XML
 *  各区块高度的直接累加值。
 *
 *    deptLineCount：部门路径折行后的行数；<= 0 表示没有部门路径。
 *    tagRowCount：标签流式排布后的行数（一般传 SyncContactTagArea 的返回值）。
 *    profileRowCount：「个人资料」区块显示的行数（ConfigureContactProfile 的返回值）；0 表示不显示。
 *    selfView：是否是看本人的视图（ApplyContactSelfView 隐藏的区块不计入高度，tagRowCount 随之不起作用）。
 *  返回：客户区高度。
 */
int ContactBodyHeight(int deptLineCount, int tagRowCount, int profileRowCount = 0, bool selfView = false);

/**
 *  切换「看本人」的视图：看本人时隐藏语音通话 / 视频通话两个快捷按钮、底部「发消息」按钮，以及标签区、
 *  备注名与分组、消息提醒与添加到收藏三个区块（连同它们下方的分隔线）；看别人时全部显示。
 *  「历史消息」「组织架构」两个快捷按钮与「关闭」按钮始终显示。
 *    root：客户区根控件；nullptr 忽略。
 *    self：true 表示看的是本人。
 *  副作用：显隐变化后调用方须重排（RelayoutContactBody），窗口高度按 ContactBodyHeight(..., self) 重算。
 */
void ApplyContactSelfView(balloonwjui::DuiControl* root, bool self);

/**
 *  「个人资料」区块（含它下方的分隔线）按显示的行数算出的高度（逻辑像素）。
 *
 *  纯函数。0 行返回 0 —— 此时区块与分隔线都不可见，窗口高度与加这块之前完全一致。
 *    rowCount：显示的行数；<= 0 一律按 0 行处理。
 *  返回：区块高度。
 */
int ContactProfileBlockHeight(int rowCount);

/**
 *  「个人资料」格子里值一行的高度（像素）在指定 DPI 下应取多少：显示值的文字、就地编辑输入框、性别下拉框三者同高，
 *  取设计高度 22 与「默认字体一行的高度 + 输入框上下边框 + 上下各 2 像素余量」中的较大者，保证输入框里的单行文字能
 *  垂直居中、不被裁掉（2026-10-07，用户在 125% 缩放的显示器上看到昵称等输入框里的文字下半截被裁掉）。
 *    dpi：显示器 DPI（96 为 100% 缩放），按 balloonui 在该 DPI 下的默认字体计算。
 *  返回：高度，不小于 22。只读字体度量，无副作用。
 */
int ContactProfileValueHeightForDpi(int dpi);

/**
 *  「个人资料」格子里值一行的实际高度：按本机所有显示器中最高的 DPI 计算（见 i18n::MaxMonitorDpi），
 *  窗口放在哪块显示器上都放得下。建控件树与算区块高度时都用它。
 *  返回：高度（像素）。
 */
int ContactProfileValueHeight();

/**
 *  「个人资料」一行格子的高度：字段名一行、间距与值一行（ContactProfileValueHeight）之和。
 *  返回：高度（像素）。
 */
int ContactProfileRowHeight();

/**
 *  「个人资料」区块实际显示几行：看别人、行数多于 kContactProfileCollapsedRows 且没有展开时只显示
 *  前 kContactProfileCollapsedRows 行，其余情况全部显示。纯函数。
 *    totalRows：格子排出来的总行数（ContactProfileRowCount）。
 *    collapsible：是否允许收起；看本人时为 false（始终全部展开）。
 *    expanded：用户是否已点了「展开全部」。
 *  返回：显示的行数。
 */
int ContactProfileShownRows(int totalRows, bool collapsible, bool expanded);

/**
 *  把格子数据灌进「个人资料」区块：昵称进 0 号槽位、个性签名进 1 号槽位，其余按顺序进 2～8 号槽位
 *  （没填的字段已不在 cells 里，后面的格子自然前移；看别人时没有昵称，0 号槽位所在的行不显示）；按收起规则决定显示几行，「展开全部 / 收起」链接只在可收起且行数
 *  多于默认行数时出现；把区块的真实高度写回它在父容器里的布局提示。
 *
 *    root：客户区根控件；nullptr 时返回 0。
 *    cells：BuildContactProfileCells 的结果。
 *    collapsible / expanded：含义同 ContactProfileShownRows。
 *    outSlotFields：出参，长度 kContactProfileSlotCount，第 s 项是 s 号槽位当前显示的字段
 *                   （ContactProfileField），槽位空着或所在行被收起时为 -1。
 *  返回：显示的行数（0 表示区块不显示）。调用方据此算窗口高度（ContactBodyHeight）。
 *  副作用：显隐变化后调用方须重排（RelayoutContactBody），本函数不重排。
 */
int ConfigureContactProfile(balloonwjui::DuiControl* root, const std::vector<ContactProfileCell>& cells,
                            bool collapsible, bool expanded, std::vector<int>& outSlotFields);

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
 *  新建一行"标签已达上限"的说明文字控件，已设好文字与 kIdTagLimitHint id。
 *
 *  标签数达到上限时，窗口用它<u>顶替</u>末尾的「+ 添加标签」按钮 —— 按钮还在
 *  却一按就弹"加不了了"是很差的体验，不如直接把原因写在原位置上。删掉一枚
 *  标签后窗口会重建标签区，按钮自然恢复。
 *
 *  返回：构造好的文字控件（所有权转移给调用方）。
 */
std::unique_ptr<balloonwjui::DuiControl> CreateTagLimitHint();

/**
 *  重算标签区的行数，并把结果同步到滚动视口与标签区高度提示上。
 *
 *  窗口每次增删标签、重建标签区之后调用一次。函数内部做三件事：
 *    · 按可用宽度数出标签流式排布需要多少行（超过三行时会出现滚动条，滚动条
 *      要占掉一点宽度，故此时按缩窄后的宽度重数一遍）；
 *    · 把流式容器的完整高度告诉滚动视口，使滚动范围正确；
 *    · 把标签区整块的高度提示改写成"可见行数"对应的高度。
 *
 *    root：客户区根控件；nullptr 时直接返回 0。
 *  返回：可见行数（已夹在 [1, kContactTagMaxRows] 内）。调用方据此算窗口高度
 *        （ContactBodyHeight）。
 */
int SyncContactTagArea(balloonwjui::DuiControl* root);

/**
 *  弹一条轻量提示（DuiToast），复用客户区里那一个提示控件。
 *
 *  标签重复、标签超长一类"没法照做"的情况用它给用户一句话交代。提示条的渐入
 *  渐出由 DuiAnimMgr 自带的脉冲定时器推进（见 balloonui/DuiAnimation.cpp 的
 *  StartPulse），调用方与宿主窗口都不必额外做什么。
 *
 *    root：客户区根控件；nullptr 忽略。
 *    text：提示文字；nullptr 或空串忽略。
 */
void ShowContactTipToast(balloonwjui::DuiControl* root, LPCTSTR text);

/**
 *  按 ctrlId 配置头部多行资料块（ContactHeaderInfo）的四行内容。
 *
 *  布局自用控件 ContactHeaderInfo 是文件局部类型，窗口拿不到其声明 ——
 *  本自由函数代窗口完成配置，按 id 在子树里定位该控件。
 *
 *    root：客户区根控件；nullptr 忽略。
 *    primaryName：第 1 行主名（备注名优先，否则真名）；点击该行姓名时复制它。
 *    parenText：    第 1 行主名后紧跟的括号内容（当前放的是备注名，形如
 *                   「（产品组同事）」），无则传空。
 *    title：第 2 行职务，无则传空（该行不画）。部门路径不再放这一行 ——
 *           它已独立成整宽区块，见 ConfigureContactDeptPath。
 *    accountRegion：第 3 行「账号 XXX · 地区」（展示文本）。
 *    signature：第 4 行个性签名，无则传空（该行不画）。
 *    account：裸账号（不含"账号 "前缀与地区）；点击账号行时复制它到剪贴板,无则传空。
 */
void ConfigureContactHeader(balloonwjui::DuiControl* root,
                            LPCTSTR primaryName, LPCTSTR parenText,
                            LPCTSTR title, LPCTSTR accountRegion,
                            LPCTSTR signature, LPCTSTR account);

/**
 *  按模型配置头部的头像控件：占位字母、右下角状态点，以及底色 —— 模型标记为灰度时
 *  （对方离线，2026-10-03 起）底色换成等亮度的灰色，否则用原色。模型带头像来源（AvatarSource().m_valid，
 *  2026-10-07）时占位字形与底色取自来源，否则取姓名首字与固定底色 kContactAvatarBgColor。
 *  头像位图（真实头像）由窗口另行生成并经 DuiAvatar::SetBitmap 挂上，本函数不碰位图。
 *    avatar：头像控件（kIdAvatar），借用；为 NULL 时什么都不做。
 *    model： 资料模型，只读。
 *  副作用：改动控件的字母、状态点与底色，控件自行请求重绘。
 */
void ApplyContactAvatar(balloonwjui::DuiAvatar* avatar, const ContactModel& model);

/**
 *  设置头部头像是否可点击（2026-10-07：看本人时可点击，打开修改头像对话框）。可点击时鼠标悬停显示手形
 *  光标与提示文字，松开左键时发出 DUIN_CLICK（ctrlId 为 kIdAvatar）；不可点击时既不变光标也不发点击通知。
 *    avatar：  头像控件（BuildContactBody 建出的 kIdAvatar），借用；为 NULL 或不是本布局建出的头像时什么都不做。
 *    clickable：是否可点击。
 *    tooltip：  可点击时的悬停提示文字（如「修改头像」）；NULL 或空串表示不提示。不可点击时忽略。
 */
void SetContactAvatarClickable(balloonwjui::DuiAvatar* avatar, bool clickable, LPCTSTR tooltip);

/**
 *  头部头像当前是否可点击（见 SetContactAvatarClickable）。
 *    avatar：头像控件，借用；为 NULL 或不是本布局建出的头像时返回 false。
 *  返回：可点击返回 true。
 */
bool IsContactAvatarClickable(const balloonwjui::DuiAvatar* avatar);

/**
 *  按 ctrlId 配置部门路径整宽区块（ContactDeptPath），并把算出来的真实
 *  高度写回它在父容器里的布局提示。
 *
 *  区块在内嵌 XML 里以 fixedHeight="0" 出场，真实高度只有拿到部门路径、
 *  按可用宽度折完行才知道，故由本函数在运行期改写。路径为空时区块被置为
 *  不可见且高度归 0，界面上不留任何痕迹。
 *
 *    root：客户区根控件；nullptr 忽略。
 *    deptPath：完整部门路径（如「产品部 / 设计组」）；nullptr 或空串表示
 *              该联系人不属于任何部门。
 *  返回：折行后的行数（0 表示没有部门路径）。调用方据此算窗口高度
 *        （ContactBodyHeight）。
 */
int ConfigureContactDeptPath(balloonwjui::DuiControl* root, LPCTSTR deptPath);

/**
 *  切换「个人资料」区块里某个格子的就地编辑态。进入编辑：隐藏值与编辑按钮，性别显示下拉框并选中
 *  当前性别，其余字段显示输入框并填入 text；退出编辑：隐藏输入框与下拉框，恢复值与编辑按钮。
 *    root：客户区根控件；nullptr 忽略。
 *    slot：槽位，取值 [0, kContactProfileSlotCount)。
 *    field：该槽位显示的字段（ContactProfileField）。
 *    editing：true 进入编辑，false 退出编辑。
 *    text：进入编辑时输入框的初始文字（性别不用）。
 *    gender：进入编辑时性别下拉框要选中的性别编码（只有性别用）。
 *  副作用：显隐变化后调用方须重排（RelayoutContactBody）并把焦点交给输入框。
 */
void SetContactProfileSlotEditing(balloonwjui::DuiControl* root, int slot, int field, bool editing,
                                  LPCTSTR text, int gender);

/**
 *  公开范围开关（「仅自己可见 ▾ / 对同事可见 ▾」链接）要占的宽度：按开关当前的字体量出两种文字中
 *  较宽的一种，再加一点留白，切换取值时宽度不变、旁边的控件不跟着移动。联系人资料窗看本人时使用
 *  （2026-10-07 之前本人资料对话框也用它，该对话框已删除）。
 *    link：开关控件；nullptr 返回 0。
 *  返回：宽度（像素）；取不到设备上下文时返回按中文设计的保守宽度。
 */
int ProfileVisibilityLinkWidth(const balloonwjui::DuiLabel* link);

/**
 *  在公开范围开关的下方弹出两项菜单（「仅自己可见」「对同事可见」，当前项打勾），同步等待选择。
 *  联系人资料窗看本人时使用。必须在鼠标事件处理完之后调用（由窗口投给自己的消息里调用），
 *  否则模态菜单会打乱鼠标捕获，菜单关闭后窗口里的点击失灵。
 *    owner：菜单的属主窗口。
 *    anchorScreen：开关的屏幕矩形；菜单左上角落在它左下方，超出工作区时由 DuiMenu 自己翻转。
 *    current：当前取值，取值见 net/Protocol.h 的 enum profile_visibility。
 *  返回：DuiMenu::TrackPopup 的返回值，取值见 profilefields::VisibilityMenuId；取消时为 0。
 */
UINT TrackProfileVisibilityMenu(HWND owner, const RECT& anchorScreen, int current);

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
 *  切换任何控件的显隐之后也要调用本函数（DuiControl::SetVisible 不触发重排）。
 *  本函数对每一层可见的容器都按其当前矩形强制重排，而不只是根控件这一层，
 *  因此嵌套在深处的控件（如「备注名」行内的文本框）同样能拿到正确的矩形。
 *
 *    root：客户区根控件（SetClientContent 时传入的那个）；nullptr 忽略。
 */
void RelayoutContactBody(balloonwjui::DuiControl* root);

} // namespace ballooncontact
