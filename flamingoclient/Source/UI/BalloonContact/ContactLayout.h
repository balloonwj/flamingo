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

namespace ballooncontact {

/**
 *  联系人资料对话框客户区控件的数字 id —— 窗口代码按这些 id 取回控件。
 *  全部落在任务约定的 1000–1099 区间内。
 */
enum ContactCtrlId
{
    kIdAvatar         = 1000,   // 头部头像（DuiAvatar）
    kIdHeaderInfo     = 1001,   // 头部右侧多行资料块（ContactHeaderInfo 自绘）
    kIdDeptPath       = 1002,   // 部门路径整宽区块（ContactDeptPath 自绘，可折行）
    kIdCopyToast      = 1003,   // 复制成功后的轻量提示（DuiToast，浮在客户区顶部）

    kIdTagFlow        = 1010,   // 标签流式容器（ContactTagFlow，窗口往里塞胶囊）
    kIdAddTagButton   = 1011,   // 「+ 添加标签」虚线按钮（ContactAddTagButton 自绘）
    kIdTagBlock       = 1012,   // 标签区整块（小标题 + 滚动视口的 vbox）；高度随标签行数改写
    kIdTagScroll      = 1013,   // 标签流式容器外面的滚动视口（DuiScrollView），超过三行时出滚动条
    kIdTagLimitHint   = 1014,   // 标签数达上限时顶替「+ 添加标签」的说明文字（ContactTagLimitHint 自绘）

    kIdRemarkValue    = 1020,   // 「备注名」行右侧的当前备注文字（ContactValueText）
    kIdRemarkEditBtn  = 1021,   // 「备注名」行右侧的铅笔编辑按钮（ContactIconButton）
    kIdRemarkEdit     = 1022,   // 「备注名」行内编辑用文本框（DuiEdit，默认隐藏）
    kIdGroupCombo     = 1023,   // 「分组」行的下拉框（DuiComboBox）

    kIdSwitchMute     = 1030,   // 「消息免打扰」开关（DuiSwitch）
    // 1031 曾是「置顶聊天」开关 —— 见 ContactSwitchKind 的说明，本轮整项移除；
    // 编号留空不复用。
    // 1032 曾是「加入黑名单」开关。企业版取消好友关系后该行整行移除；
    // 编号留空不复用，避免与旧版本布局对不上号。
    kIdSwitchFavorite = 1033,   // 「添加到收藏」开关（DuiSwitch）
    kIdSwitchStarred  = 1034,   // 「特别关注」开关（DuiSwitch）

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
};

// =====================================================================
// 客户区尺寸常量 —— 窗口侧与布局侧共用同一份，避免两处各写一个 420
// =====================================================================

// 客户区宽度（逻辑像素）。照设计稿 dialogs.jsx 的 ContactInfoDialog width:420。
const int kContactBodyWidth = 420;

// 客户区左右内边距（逻辑像素，单边）。与内嵌 XML 中 contact-root 的
// padding="20,18,20,18" 的左右两个数一致。
const int kContactBodyPaddingH = 20;

// 部门路径整宽区块的可用宽度（逻辑像素）= 420 - 20 - 20 = 380。
// 部门路径独占一行整宽区域，不再与头像 / 职务共用一行，故可用宽度比
// 头部右侧文字块（302）宽得多，绝大多数部门路径一行就能放下。
const int kContactDeptPathAvailWidth = kContactBodyWidth - kContactBodyPaddingH * 2;

// 部门路径最多显示的行数。超出的部分由最后一行做中段省略。取 3 行的依据：
// 每行 380 像素、12 像素字号下约容 31 个汉字，3 行足以容下 90 个汉字以上的
// 部门路径；再多就属于异常数据，没必要为它把窗口撑高。
const int kContactDeptPathMaxLines = 3;

// 标签流式容器可用宽度的<u>估算值</u>（逻辑像素）= 420 - 20 - 20 = 380。标签区
// vbox 的 padding 只有上下两个方向（见内嵌 XML 的 padding="0,14,0,14"），故流式
// 容器的宽度就是客户区去掉左右内边距之后的整宽。
//
// 说它是估算值，是因为窗口实际的客户区比 kContactBodyWidth 宽出一圈缩放边框
// （实测 438 而非 420），真实可用宽度要大二十来个像素。SyncContactTagArea 数
// 行数时以滚动视口实际布局出来的宽度为准，量到的宽度不可信时才退回用本常量。
const int kContactTagFlowAvailWidth = kContactBodyWidth - kContactBodyPaddingH * 2;

// 客户区比 kContactBodyWidth 多出来的那一圈缩放边框的宽度余量（逻辑像素，左右
// 合计）。DuiFrameWindow 在 WM_NCCALCSIZE 里把整个窗口矩形当作客户区，于是客户区
// 比 ResizeClient 指定的宽度多出左右两条 resize 边：100% 缩放下实测多出 18 像素
// （客户区 438 而非 420）。这条边宽随系统 DPI 缩放，取 64 像素足以覆盖到 300% 缩放。
const int kContactFrameBorderAllowance = 64;

// 标签流式容器可用宽度的<u>可信上限</u>（逻辑像素）。SyncContactTagArea 优先采用
// 滚动视口实际布局出来的宽度，但只有不超过本上限的测量值才作数。
//
// 判据：本对话框是固定宽度窗口（建窗时 SetResizable(false)，客户区宽度恒按
// kContactBodyWidth 调整），标签区又总是占满客户区去掉左右内边距之后的整宽，因此
// 可用宽度在任何时刻都不可能超过"常量宽度 + 一圈缩放边框"。超过就只有一种解释：
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
 *  ContactBodyHeight(0, 1)（没有部门路径、标签只有一行）恒等于内嵌 XML 各区块
 *  高度的直接累加值。
 *
 *    deptLineCount：部门路径折行后的行数；<= 0 表示没有部门路径。
 *    tagRowCount：标签流式排布后的行数（一般传 SyncContactTagArea 的返回值）。
 *  返回：客户区高度。
 */
int ContactBodyHeight(int deptLineCount, int tagRowCount);

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
