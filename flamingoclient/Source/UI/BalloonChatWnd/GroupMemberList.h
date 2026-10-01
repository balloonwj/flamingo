/**
 *  GroupMemberList.h
 *
 *  群聊窗右侧边栏的成员列表容器 —— 把若干 GroupMemberItem 按固定行高
 *  自上而下堆叠。子控件超出本控件矩形的部分由 OnPaint 裁剪。
 *
 *  成员多于边栏可视行数时可以滚动（2026-10-01 起）：滚轮每格滚 kGroupMemberWheelRows 行；
 *  右侧画与聊天消息区同一种细滚动条（ChatScrollThumb.h），可按住滑块拖动。重新填充成员时
 *  保持原滚动位置，超出新范围时收回到新的底部。
 *
 *  可按关键词过滤（2026-10-01 起）：宿主群聊窗把侧栏搜索框的文字交给
 *  SetFilterKeyword，没命中的成员行隐藏（不销毁，头像与在线状态照常更新），命中的行从顶部起
 *  连续排列；一个都没命中时在列表区域画一行「没有找到匹配的成员」。成员列表重建后宿主调
 *  ReapplyFilter 按原关键词重新过滤。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonchatwnd {

// 成员行固定行高（逻辑像素）。
const int kGroupMemberRowHeight = 38;

// 滚轮一格（WHEEL_DELTA）滚动的行数。
const int kGroupMemberWheelRows = 3;

/**
 *  判断成员名字是否命中侧栏搜索的关键词。规则与群管理窗成员页相同（底层都是
 *  contactsearch::Match）：名字子串、拼音首字母、全拼，英文不区分大小写。
 *    name：   成员显示的名字（有群名片时为群名片）。
 *    keyword：关键词；先去掉首尾空白，去掉后为空时视为全部命中。
 *  @return 命中返回 true。无副作用。
 */
bool GroupMemberNameMatches(const CString& name, const CString& keyword);

/**
 *  GroupMemberList —— 成员列表容器（固定行高堆叠，内容超出时可滚动）。
 */
class GroupMemberList : public balloonwjui::DuiControl
{
public:
    GroupMemberList();

    // DuiControl 覆写：把子控件按固定行高自上而下堆叠，整体上移当前滚动量；先按新的
    // 可视高度与行数把滚动位置收回合法范围（重新填充成员后据此保持原位置）。
    void Layout(const RECT& rcAvail) override;

    // DuiControl 覆写：绘制各成员行（裁剪到本控件矩形内），内容超出时再画滚动条滑块。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：落点在滚动条命中带里时由本控件接下（用于拖动），否则照常交给成员行。
    balloonwjui::DuiControl* HitTest(POINT ptHostClient) override;

    // DuiControl 覆写：滚轮每格滚 kGroupMemberWheelRows 行。内容没有超出时不处理、返回 false，
    // 交给外层。
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

    // DuiControl 覆写：在滚动条命中带里按下开始拖动（点在轨道空白处时滑块中心先跳到点击处）。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：拖动中按指针位置滚动。
    bool OnMouseMove(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：结束拖动。
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

    // 当前滚动位置（像素，0 表示在最顶部）。
    int ScrollY() const { return m_scrollY; }

    /**
     *  滚动到指定位置：先收进合法范围，与当前位置不同时重新排版并重绘。
     *    scrollY：目标滚动位置（像素）。
     */
    void SetScrollY(int scrollY);

    // 可见成员行的总高度（像素）= 可见行数 × kGroupMemberRowHeight。过滤隐藏的行不计入。
    int ContentHeight() const;

    /**
     *  按关键词过滤成员行：命中的行显示、没命中的行隐藏（GroupMemberNameMatches），随后
     *  强制重新排版并重绘。关键词（去掉首尾空白后）与上一次不同时滚动位置回到顶部。
     *    keyword：侧栏搜索框的文字；空串或只有空白表示不过滤、全部显示。
     */
    void SetFilterKeyword(const CString& keyword);

    /**
     *  按当前关键词重新过滤一遍，不改变滚动位置（超出新范围时照常收回）。成员列表重建
     *  （摘掉旧行、挂上新行）之后由宿主调用，否则新挂上的行不论是否命中都会显示。
     */
    void ReapplyFilter();

    // 当前的过滤关键词（已去掉首尾空白）；空串表示不过滤。
    const CString& FilterKeyword() const { return m_filterKeyword; }

    // 当前可见（未被过滤隐藏）的成员行数。
    int VisibleRowCount() const;

    // 有过滤关键词、且一行都没命中时返回 true；界面据此画「没有找到匹配的成员」。
    // 没有关键词时恒为 false（成员尚未到齐、侧栏为空时不显示这行提示）。
    bool HasNoMatches() const;

    /**
     *  设置右侧让位宽度：成员行与滚动条滑块都收在本控件矩形右缘往里这么多的范围内。
     *  侧栏贴着窗口右缘，DuiFrameWindow 把客户区最外侧几个像素判成缩放边框，滑块若画在那里，
     *  按下鼠标只会拖动窗口边框、抓不住滑块（2026-10-01 实机发现）。由宿主群聊窗按
     *  balloonmain::ComputeResizeBorderInsetPx 算好后设入。
     *    px：让位宽度（物理像素），负值按 0 处理。
     *  值变化时重新排版并重绘。
     */
    void SetRightInset(int px);

    /**
     *  把登录者本人在本群的新角色同步给每一个成员行（GroupMemberItem::SetMyNetRole）。
     *  本人角色变化后由宿主群聊窗调用：各行据此决定右键菜单里有没有「设为 / 撤销管理员」，
     *  不同步的话要等下一次重填成员列表才会生效。
     *    netRole：本人角色，取值见 grouprole::NetGroupRole，负值表示未知。
     *  不是 GroupMemberItem 的子控件跳过。不重绘：角色只影响右键菜单，界面上看不出来。
     */
    void SetMyNetRoleForAll(int netRole);

    /**
     *  更新某个成员行的在线状态（GroupMemberItem::SetPresence），头像角上的徽标随之重画。
     *    userid：  成员 userid。
     *    presence：在线状态，取值见 GroupMemberItem::SetPresence。
     *  @return 找到该成员的行返回 true；侧栏里没有这个成员时什么都不做，返回 false。
     */
    bool SetMemberPresence(int64_t userid, int presence);

private:
    // 当前滚动条滑块的矩形（宿主客户区坐标）；内容没有超出时为空矩形。
    RECT ThumbRect() const;

    // 成员行与滑块所在的范围：本控件矩形去掉右侧让位宽度（宿主客户区坐标）。
    RECT ContentRect() const;

    int  m_scrollY;            // 当前滚动位置（像素），恒在 [0, 内容高 - 可视高] 内
    bool m_thumbDragging;      // 是否正按住滑块拖动
    int  m_thumbDragOffsetY;   // 拖动时按下点相对滑块上沿的偏移（像素），拖动中保持不变，滑块不跳
    int  m_rightInset;         // 右侧让位宽度（物理像素），给窗口缩放边框让出位置，见 SetRightInset
    CString m_filterKeyword;   // 过滤关键词（已去掉首尾空白），空串表示不过滤；见 SetFilterKeyword
};

} // namespace balloonchatwnd
