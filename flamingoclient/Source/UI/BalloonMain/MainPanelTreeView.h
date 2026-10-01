/**
 *  MainPanelTreeView.h
 *
 *  主面板三个 Tab 内容面板里用的 DuiTreeView 子类。
 *
 *  本子类只做绘制层面的扩展(右侧列文字 / 会话行装饰),滚轮不必自己管:
 *  单列 DuiTreeView::OnMouseWheel 会如实返回 false(见 DuiTreeView.cpp 里
 *  !IsMultiCol 的短路),DuiHost::DispatchMouseWheel 随即沿父链上冒,滚轮
 *  自然落到外层 MainPanelScrollView 上。2026-08-14 之前库里的滚轮不冒泡,
 *  本类曾覆写 OnMouseWheel 手工转发,库补上默认上冒后那段代码已删除。
 *
 *  本文件遵循 balloonui 头文件约定:include 它之前调用方须已引入
 *  ATL/WTL(Flamingo 侧由 stdafx.h 保证,FlamingoUnit 侧由其自定义 prolog
 *  保证)。
 *
 *  balloonwj@qq.com   2026-05-28
 */

#pragma once

#include <map>
#include <set>

#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTreeView.h"

namespace balloonmain {

/**
 *  MainPanelTreeView —— 主面板三个 Tab 树用的 DuiTreeView 子类，在基类之上
 *  做两件事：
 *
 *    1) 接管行右侧列的绘制 —— 基类把右侧文字强制按整行垂直居中，两行节点里它会落在
 *       主标签与副标签之间的中缝上，做不出"右侧文字和主标签同高"的效果。本子类不修改
 *       balloonui，而是子类化扩展：调用方用 SetItemRightTopText 交进来的文字，本类画在
 *       上半行（与主标签同一垂直带，用基类同款默认字体 / 灰色，自动对齐）。为不让左侧
 *       文字顶进右侧列，本类用等宽空格占位塞进基类 rightText（空格不可见，仅让基类据此
 *       裁剪左侧文字）。目前两处在用：会话树放"时间"，群树放"群成员数"。
 *    2) 会话行专有的装饰 —— 未读红点画在下半行（与预览同高，用 SetItemRightStacked
 *       连同时间一起交进来），以及置顶（整行覆淡暗色 + 左侧强调竖条）/ 免打扰（红点转灰
 *       并在其左侧加静音图标）标记。这些只有会话树会用；群树只走上排文字那一条路。
 *
 *  联系人树两样都不用，几个映射恒为空，OnPaint 走短路分支、零开销，其右侧文字仍由
 *  基类正常绘制。
 *
 *  除上述外的所有行为与裸 DuiTreeView 完全一致，节点 / 列 / 图标 / 排序 /
 *  编辑等 API 直接继承。
 */
class MainPanelTreeView : public balloonwjui::DuiTreeView
{
public:
    MainPanelTreeView();

    /**
     *  设置某节点"与主标签同高"的右侧文字。本类接管该节点右侧列的绘制，把这段文字
     *  画在行的上半区、右对齐，字体与颜色沿用基类右侧文字那一套（默认字体 + 灰色），
     *  从而与左侧主标签处在同一条水平线上。
     *
     *  之所以不直接用基类的 SetItemRightText：基类把右侧文字按<u>整行</u>垂直居中
     *  （见 DuiTreeView.cpp 单列分支），两行节点里它会落在主标签与副标签之间的中缝，
     *  看上去是"飘着"的。用例：群列表第一行右侧的成员数、会话列表第一行右侧的时间。
     *
     *    id    节点 id（AddRoot / AddChild 的返回值）。
     *    text  右侧文字；NULL 或空串表示不画，并释放该节点已占的右侧预留宽。
     *  内部会按文字宽度给基类 rightText 塞等宽空格占位以预留右侧列宽（使左侧主标签
     *  自动省略号截断、不顶到本文字底下），再 Invalidate 触发重绘。
     */
    void SetItemRightTopText(int id, LPCTSTR text);

    /**
     *  设置某会话节点右侧的"时间（上排）+ 未读红点（下排）"。是会话行专用的便捷方法：
     *  时间等价于 SetItemRightTopText（画在与姓名同高的上排），红点画在与预览同高的下排。
     *    id         节点 id（AddRoot / AddChild 的返回值）。
     *    timeText   右上排显示的时间文字（如 "14:32" / "昨天"）；空串表示不画时间。
     *    badgeCount 未读消息数；<= 0 表示该节点不画红点。
     *  内部会按时间 / 红点的宽度，给基类 rightText 塞等宽空格占位以预留右侧列宽，
     *  再 Invalidate 触发重绘。
     */
    void SetItemRightStacked(int id, LPCTSTR timeText, int badgeCount);

    /**
     *  设置某会话节点的"置顶 / 免打扰"标记。两个标记的呈现方式：
     *    · 置顶 —— 整行底色轻微加深（在基类画完行内容之后覆一层极低透明度的暗色，
     *              不影响文字可读性），与未置顶行区分开；
     *    · 免打扰 —— 行右下角在未读胶囊左侧画一个静音图标，同时未读胶囊由红转灰
     *              （"有新消息但不打扰"的通行画法）。
     *  两者可同时成立。免打扰会改变右下角占位宽度，故本方法内部会重算右侧列预留宽。
     *    id      节点 id（AddRoot / AddChild 的返回值）。
     *    pinned  该会话是否置顶。
     *    muted   该会话是否免打扰。
     */
    void SetItemConversationFlags(int id, bool pinned, bool muted);

    /**
     *  设置某节点的「特别关注」标记 —— 标记为真时在<u>主标签文字之后</u>画一个金色实心
     *  五角星，表示这个人上下线时会在桌面右下角弹提示。通讯录行与最近会话行共用。
     *
     *  为什么画在名字后面而不是行右端：会话行右端已经排了时间（右上）、未读胶囊与
     *  静音图标（右下）、在线状态徽标（右端垂直居中），再插一个星会叠在一起；而名字
     *  之后两个列表都是空的，落点还能保持一致。
     *
     *  星的横坐标由基类的 GetItemLabelTextRect 给出（文字实际画到哪儿结束，取决于
     *  缩进层级、图标尺寸、右侧占位与省略号截断，只有基类算得出）。调用方不必预留
     *  宽度：本类会把星的宽度计入该行的右侧占位，名字过长时基类先把名字省略号截断，
     *  星跟在截断处之后，不会压到右侧的时间 / 成员数 / 状态徽标。
     *
     *  注意：打了标记的行由本类接管基类 rightText（写入等宽空格占位），调用方不要再
     *  对这些行直接调用 SetItemRightText。
     *    id       节点 id（AddRoot / AddChild 的返回值）。
     *    starred  该联系人是否被特别关注。
     */
    void SetItemStarred(int id, bool starred);

    /**
     *  设置某节点是否画「我的收藏」徽章：在名字之后画一个实心书签（见 MainPanelIcons.h
     *  kIconBookmark），同时被特别关注时排在星标之后。部门树的联系人行、群列表的群行、
     *  会话列表的会话行共用；「我的收藏」分区里的行全是收藏，调用方不必再打这个标记。
     *
     *  位置规则与星标相同：横坐标取自 GetItemLabelTextRect，徽章宽度计入右侧占位，
     *  名字过长时先截断名字，徽章不会压到右侧内容。使用约束也与星标相同（本类接管
     *  该行的基类 rightText）。
     *    id        节点 id（AddRoot / AddChild 的返回值）。
     *    favorite  该联系人 / 群是否已收藏。
     */
    void SetItemFavorite(int id, bool favorite);

    // 某行当前是否画收藏徽章（SetItemFavorite 设过且为 true）。只读查询，供核对行标记
    // 是否与收藏模型一致（主面板基准测试用）。
    //   id：节点 id。返回：带徽章返回 true。
    bool IsItemFavorite(int id) const { return m_favoriteRows.count(id) != 0; }

    /**
     *  清空所有节点的行装饰接管记录（上排右侧文字 / 未读红点 / 置顶 / 免打扰 / 特别关注 /
     *  收藏 / 已写入的占位空格个数）。
     *  任何一棵树整表重建（Clear 后重填）之前都要调用 —— 基类 Clear 只清节点，
     *  不认识本子类的这几个映射。基类 Clear 不重置节点 id 计数（DuiTreeView.cpp 的
     *  AddRoot / AddChild 一律 m_nextId++），新节点不会继承旧节点的记录，但旧条目会
     *  一直留在映射里，且绘制时要逐条遍历。会话树与群树都要调。
     */
    void ClearAllRowDecorations();

    /**
     *  DuiControl 覆写:绘制。先调基类把整行其余内容（行底色 / 头像 / 标题 /
     *  预览）画完（会话行的基类右侧文字是占位空格、不可见），再依次叠加：置顶行
     *  底色、上排时间（与姓名同高）、下排静音图标与未读胶囊（与预览同高；免打扰
     *  时胶囊由红转灰）。
     *
     *  置顶底色之所以画在基类<u>之后</u>而非之前：基类对每一行都会先用行背景色
     *  FillSolidRect 铺一遍底（见 DuiTreeView::OnPaint 单列分支），画在前面会被
     *  它整片盖掉；故改为事后覆一层极低透明度的暗色。透明度取得很低，压在文字与
     *  头像上肉眼看不出发灰，只让整行读起来比旁边的行"重"一点。
     *    hdc      目标设备上下文。
     *    rcDirty  脏矩形（客户区坐标）；用于跳过不在脏区内的行，省去无谓绘制。
     */
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    /**
     *  所在窗口的 DPI 变化时由宿主调用。各行写进基类 rightText 的占位空格个数，是按
     *  旧 DPI 下的默认字体量出来的；字号变了以后要按新字体重新算一遍，否则右侧时间、
     *  角标的预留宽度与实际绘制宽度对不上。
     *    dpi  新的 DPI（重算时经 GetDefaultFont 间接使用，不直接读取）。
     */
    void OnDpiChanged(int dpi) override;

private:
    /**
     *  算出与脏区相交的可见行号范围 [outFirst, outLast]（行矩形与 DuiTreeView 单列绘制
     *  同口径）。没有相交的行时 outLast < outFirst。OnPaint 与 PaintNameMarks 只遍历这一段，
     *  几千行的列表每次绘制只看十几行。
     *    rcDirty：本次重画的区域（客户区坐标）。
     */
    void DirtyRowRange_(const RECT& rcDirty, int& outFirst, int& outLast) const;

    /**
     *  画名字之后的标记：先画「特别关注」星标，再画「我的收藏」书签徽章（两者都有时
     *  徽章紧跟在星之后）。由 OnPaint 在基类画完之后调用；两个集合都为空时立即返回，
     *  不产生任何开销。
     *
     *  横坐标取自基类的 GetItemLabelTextRect —— 名字画到哪儿结束只有基类算得出
     *  （缩进层级、图标尺寸、右侧占位、省略号截断都影响它）。取不到（节点不可见、
     *  标签为空、可用宽度被挤没）时该行不画，不做任何近似猜测。
     *    hdc      目标设备上下文。
     *    rcDirty  脏矩形（客户区坐标）；不与之相交的行跳过。
     */
    void PaintNameMarks(HDC hdc, const RECT& rcDirty);

    /**
     *  某节点名字之后的标记（星标、收藏徽章）连同间距一共占多宽，单位像素；没有标记时为 0。
     *    id  节点 id。
     */
    int NameMarksWidth_(int id) const;

    /**
     *  某节点右侧列（上排文字、下排未读胶囊与静音图标）要给行右端的状态徽标让出多宽，
     *  单位像素：有状态徽标时为"徽标宽 + 间距"，没有时为 0。基类把状态徽标贴在行右端，
     *  本类画的右侧列须整体让到它左边，否则会压在徽标上（回归用例见 MainPanelRowMarkTests 的 CRL1）。
     *    id  节点 id。
     */
    int StatusIconReserve_(int id) const;

    /**
     *  重算某节点右侧列的预留宽度并写回基类 rightText（等宽空格占位）。预留宽取
     *  "上排时间宽"与"下排（静音图标 + 未读胶囊）宽"的较大者，再加上名字之后的标记
     *  宽度，让基类把左侧标题 / 预览裁剪在右侧列左边、并给标记留出位置。时间 / 未读数 /
     *  免打扰 / 特别关注 / 收藏任一变化后都要调。
     *    id  节点 id。
     */
    void UpdateRightReserve_(int id);

    // 节点 id → 上排右侧文字（会话树放时间，群树放成员数）。联系人树恒空。整表重建前
    // 由 ClearAllRowDecorations 清空。键存在即由本类接管该行右侧绘制（基类 rightText
    // 里是等宽空格占位）。文字为空的节点不入表，好让不用这个能力的树保持短路。
    std::map<int, CString> m_rightTopText;

    // 节点 id → 未读角标数字。值 <= 0 或键不存在表示该行不画红点。仅会话树填充。
    std::map<int, int> m_badges;

    // 置顶的节点 id 集合。命中即给整行覆一层淡暗色并在左侧画橙色竖条。会话树与群树填充。
    std::set<int> m_pinnedRows;

    // 免打扰会话的节点 id 集合。命中即在右下角画静音图标、且未读胶囊改用灰底。
    // 仅会话树填充。
    std::set<int> m_mutedRows;

    // 被「特别关注」的节点 id 集合。通讯录行与最近会话行共用，为空时 OnPaint 的
    // 星标那一段整段跳过（群树、以及一个人都没关注时零开销）。
    std::set<int> m_starredRows;

    // 已收藏的节点 id 集合。部门树、群树、会话树共用；与 m_starredRows 同时为空时
    // OnPaint 的名字标记那一段整段跳过。整表重建前由 ClearAllRowDecorations 清空。
    std::set<int> m_favoriteRows;

    // 节点 id → 上次经 UpdateRightReserve_ 写进基类 rightText 的占位空格个数。算出来的
    // 个数与上次相同时不再写回：基类 SetItemRightText 要按 id 逐个扫描节点，会话树
    // 每个节点在填充时要算三四次占位，而结果几乎总是不变（2026-10-01 基准实测这部分
    // 约占一次会话列表重建的四成）。
    // 约定：设了上排文字 / 角标 / 标记的行，其基类 rightText 只由 UpdateRightReserve_
    // 写，调用方不要再直接 SetItemRightText，否则这里记的个数与实际内容不一致，下次
    // 个数相同时会跳过写回。整表重建前由 ClearAllRowDecorations 清空。
    std::map<int, int> m_rightSpacerLen;
};

} // namespace balloonmain
