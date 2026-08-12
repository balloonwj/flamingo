/**
 *  MainPanelTreeView.h
 *
 *  主面板三个 Tab 内容面板里用的 DuiTreeView 子类。
 *
 *  存在唯一理由 —— 滚轮上抛:
 *    · 单列 DuiTreeView::OnMouseWheel 直接 return false(详见
 *      DuiTreeView.cpp 中的 OnMouseWheel 实现:!IsMultiCol 时 return);
 *    · DuiHost::OnMouseWheel 只把滚轮投给"最深命中控件",且<u>不冒泡</u>
 *      (见 DuiHost.cpp 的 OnMouseWheel 注释);
 *    · 主面板里 treeview 铺满了 scrollview 视口,滚轮总落在 treeview
 *      身上,于是滚轮永远到不了外层 MainPanelScrollView,overlay 滚动条
 *      没机会显示,treeview 也滚不动。
 *  本子类在 OnMouseWheel 中:先让基类尝试消化(多列模式自带滚动逻辑);
 *  基类返 false(单列模式)时,沿父链找最近的 DuiScrollView 子类把滚轮
 *  转发过去(复用 MainPanelScrollView.cpp 里的 ForwardWheelToScrollView)。
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

#include "../../balloonui/balloonui/Controls/List/DuiTreeView.h"

namespace balloonmain {

/**
 *  MainPanelTreeView —— 主面板三个 Tab 树用的 DuiTreeView 子类，在基类之上
 *  做两件事：
 *
 *    1) 滚轮上抛（原始职责，见上方文件头说明）；
 *    2) 接管行右侧列的绘制 —— 基类把右侧文字强制按整行垂直居中，两行节点里它会落在
 *       主标签与副标签之间的中缝上，做不出"右侧文字和主标签同高"的效果。本子类不修改
 *       balloonui，而是子类化扩展：调用方用 SetItemRightTopText 交进来的文字，本类画在
 *       上半行（与主标签同一垂直带，用基类同款默认字体 / 灰色，自动对齐）。为不让左侧
 *       文字顶进右侧列，本类用等宽空格占位塞进基类 rightText（空格不可见，仅让基类据此
 *       裁剪左侧文字）。目前两处在用：会话树放"时间"，群树放"群成员数"。
 *    3) 会话行专有的装饰 —— 未读红点画在下半行（与预览同高，用 SetItemRightStacked
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
     *  缩进层级、图标尺寸、右侧占位与省略号截断，只有基类算得出）。因此本标记<u>不需要</u>
     *  调用方预留宽度：名字过长时基类会先把名字省略号截断，星跟在截断处之后。
     *    id       节点 id（AddRoot / AddChild 的返回值）。
     *    starred  该联系人是否被特别关注。
     */
    void SetItemStarred(int id, bool starred);

    /**
     *  清空所有节点的行装饰接管记录（上排右侧文字 / 未读红点 / 置顶 / 免打扰 / 特别关注）。
     *  任何一棵树整表重建（Clear 后重填）之前都要调用 —— 基类 Clear 只清节点，
     *  不认识本子类的这几个映射，而节点 id 会被复用，不清的话新节点会莫名其妙
     *  继承上一批节点的角标与标记。会话树与群树都要调。
     */
    void ClearAllRowDecorations();

    /**
     *  DuiControl 覆写:滚轮事件处理。
     *    pt       命中点屏幕→客户区坐标(由 DuiHost 路由时已转换)。
     *    zDelta   滚轮增量,正值表示远离用户(上滚),负值近用户(下滚);
     *             一格为 WHEEL_DELTA(120)。
     *    mkFlags  鼠标按键 / 修饰键状态位(MK_SHIFT / MK_CONTROL 等)。
     *  返回:true 表示已消化(多列模式由基类内部水平/垂直滚动消化;单列
     *       模式由祖先 scrollview 消化),false 表示父链上没有 scrollview
     *       可承接(此情况下整个事件不会再有谁处理)。
     */
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

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

private:
    /**
     *  画所有被「特别关注」的行的星标（在各自主标签文字之后）。由 OnPaint 在基类
     *  画完之后调用；m_starredRows 为空时立即返回，不产生任何开销。
     *
     *  横坐标取自基类的 GetItemLabelTextRect —— 名字画到哪儿结束只有基类算得出
     *  （缩进层级、图标尺寸、右侧占位、省略号截断都影响它）。取不到（节点不可见、
     *  标签为空、可用宽度被挤没）时该行不画，不做任何近似猜测。
     *    hdc      目标设备上下文。
     *    rcDirty  脏矩形（客户区坐标）；不与之相交的行跳过。
     */
    void PaintStarredMarks(HDC hdc, const RECT& rcDirty);

    /**
     *  重算某节点右侧列的预留宽度并写回基类 rightText（等宽空格占位）。预留宽取
     *  "上排时间宽"与"下排（静音图标 + 未读胶囊）宽"的较大者，让基类把左侧标题 /
     *  预览裁剪在右侧列左边、不顶进来。时间 / 未读数 / 免打扰任一变化后都要调。
     *    id  节点 id。
     */
    void UpdateRightReserve_(int id);

    // 节点 id → 上排右侧文字（会话树放时间，群树放成员数）。联系人树恒空。整表重建前
    // 由 ClearAllRowDecorations 清空。键存在即由本类接管该行右侧绘制（基类 rightText
    // 里是等宽空格占位）。文字为空的节点不入表，好让不用这个能力的树保持短路。
    std::map<int, CString> m_rightTopText;

    // 节点 id → 未读角标数字。值 <= 0 或键不存在表示该行不画红点。仅会话树填充。
    std::map<int, int> m_badges;

    // 置顶会话的节点 id 集合。命中即给整行覆一层淡暗色。仅会话树填充。
    std::set<int> m_pinnedRows;

    // 免打扰会话的节点 id 集合。命中即在右下角画静音图标、且未读胶囊改用灰底。
    // 仅会话树填充。
    std::set<int> m_mutedRows;

    // 被「特别关注」的节点 id 集合。通讯录行与最近会话行共用，为空时 OnPaint 的
    // 星标那一段整段跳过（群树、以及一个人都没关注时零开销）。
    std::set<int> m_starredRows;
};

} // namespace balloonmain
