/**
 *  MainPanelListItem.h
 *
 *  主面板列表里的一行（联系人 / 群 / 会话通用）。对照 design 05/06 与
 *  main-panel.jsx 的 ContactListView / GroupsList / RecentList 行：彩色
 *  字母头像 + 名称 + 副行文字，可选头像状态点、右上角时间、未读角标。
 *
 *  继承 DuiButton 复用悬停 / 点击 / 通知逻辑（点击打开会话留待 P2），
 *  只覆写 OnPaint。各字段经 setter 配置。
 *
 *  另可经 SetLocateButtonVisible 打开行右侧的悬停态「定位」小按钮（搜索结果的
 *  联系人行用）：鼠标移到行上时右侧出现一枚地图定位针图标，点它发出
 *  DUIN_LISTITEM_LOCATE 而<u>不</u>发基类的 DUIN_CLICK，宿主据此把通讯录树滚动
 *  定位到这个人，而不是打开会话。
 *
 *  更一般地，SetHoverActions 可以给行右侧配一组悬停态小按钮（定位、打开文件、打开所在
 *  文件夹），从左到右排开，每个按钮点击时发各自的通知码，同样不发 DUIN_CLICK。搜索结果
 *  的聊天文件行用「打开文件」「打开所在文件夹」两个。按钮显示时，第一行右侧的时间文字
 *  隐藏、名称与副行的右界让到按钮左边，免得叠在一起。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

#include <vector>

namespace balloonmain {

/**
 *  MainPanelListItem —— 主面板通用列表行。
 */
class MainPanelListItem : public balloonwjui::DuiButton
{
public:
    // 行内头像的边长与圆角半径（逻辑像素）。公开出来是给 SetOwnedAvatarBitmap
    // 的调用方按同一尺寸生成真图位图用 —— 两处各写一个数字迟早会漂移。
    static const int kAvatarSizePx   = 36;
    static const int kAvatarRadiusPx = 8;

    // 本控件的自定义通知码。balloonui 的自定义码是按控件各自从 DUIN_CUSTOM 起
    // 编号的，不同控件的第一个自定义码数值必然相同，故这里另开取值段（+70 起），
    // 避开 DuiTreeView 占用的 +1..+11、TagChip 的 +50/+51、MemberPickerTreeView
    // 的 +40/+41、DuiRichEdit 的 +60。即便如此，宿主分发时仍须连触发控件一起判
    // （见 CSearchDropdown::OnChildNotify 的说明），另开取值段只是缓解。
    enum
    {
        // 悬停态的「定位」小按钮被点击 —— 请求把通讯录树定位到本行代表的联系人。
        DUIN_LISTITEM_LOCATE = balloonwjui::DUIN_CUSTOM + 70,
        // 悬停态的「打开文件」小按钮被点击 —— 请求用系统关联的程序打开本行代表的本机文件。
        DUIN_LISTITEM_OPEN_FILE = balloonwjui::DUIN_CUSTOM + 71,
        // 悬停态的「打开所在文件夹」小按钮被点击 —— 请求在资源管理器里打开本行代表的文件
        // 所在的文件夹（文件夹传输记录则打开那个文件夹本身）。
        DUIN_LISTITEM_OPEN_FOLDER = balloonwjui::DUIN_CUSTOM + 72,
    };

    // 行右侧悬停态小按钮的种类，SetHoverActions 的参数取这些值。
    enum HoverAction
    {
        kHoverActionLocate     = 0,   // 地图定位针，点击发 DUIN_LISTITEM_LOCATE（搜索结果的联系人行）
        kHoverActionOpenFile   = 1,   // 方框加右上箭头，点击发 DUIN_LISTITEM_OPEN_FILE（本机有文件的聊天文件行）
        kHoverActionOpenFolder = 2,   // 文件夹轮廓，点击发 DUIN_LISTITEM_OPEN_FOLDER（本机有文件或文件夹的行）
    };

    MainPanelListItem();
    ~MainPanelListItem();

    // 设置头像：text 为头像内显示的字 / emoji，color 为头像底色。
    void SetAvatar(LPCTSTR text, COLORREF color);

    /**
     *  设置真实头像位图（自定义图头像），<b>本控件接管所有权</b>（析构或再次
     *  设置时 DeleteObject）。设了它就画真图、不画底色方块与字形；传 NULL 清空、
     *  回到 SetAvatar 那套字形渲染。
     *
     *  取"接管所有权"而不是借用：本控件的宿主（搜索结果浮窗等）每次刷新都整批
     *  重建行控件，位图跟着控件一起生灭最省心，调用方不必再维护一份位图表。
     *
     *    hbm：32bpp 预乘 alpha 位图（一般由 balloonmain::MakeRoundedAvatarFromFile
     *         按行内头像边长生成）；传 NULL 表示没有真图。
     */
    void SetOwnedAvatarBitmap(HBITMAP hbm);

    // 设置主行名称。
    void SetName(LPCTSTR name);

    // 设置名称内的"命中高亮"字符区间（搜索结果黄底高亮用）：start 为起始字符
    // 下标，len 为字符数；len<=0 表示清除高亮。下标越界会被安全裁剪。
    void SetNameHighlight(int start, int len);

    // 设置副行文字（职位 / 群人数 / 末条消息等）。
    void SetSubText(LPCTSTR sub);

    // 设置副行内的"命中高亮"字符区间，口径同 SetNameHighlight：start 为起始字符下标，
    // len 为字符数；len<=0 表示清除高亮，下标越界会被安全裁剪。搜索下拉「消息」标签
    // 用它标出命中的那句话里的关键字。
    void SetSubHighlight(int start, int len);

    // 设置本行是否被键盘选中（搜索下拉里用上下方向键移到的那一行）。选中时画与悬停相同的
    // 底色（SetHoverFill 设的颜色）。on：true 选中，false 取消。
    void SetKeySelected(bool on);

    // 设置头像状态点：show 为是否显示，status 取值见 MockData 的 MockStatus。
    void SetStatus(bool show, int status);

    // 设置第一行右侧的辅助文字（会话行的时间 / 群行的成员数）。空串表示不显示。
    // 本行有副行时，这段文字与名称同高（两者同处第一行），名称的右界会自动为它让位，
    // 长名称在它左边就打省略号；本行没有副行时，沿用贴着行右上角的位置。
    void SetRightText(LPCTSTR text);

    // 设置未读角标数字。0 表示不显示。
    void SetBadgeCount(int count);

    // 设置悬停底色 —— 鼠标移到本行上时填充的背景色。默认 RGB(0xF4,0xF3,0xF0)
    // （design --hover，浅灰，与主面板列表一致）。搜索结果等需要更明显悬停反馈
    // 的场景可改设更深的色。
    void SetHoverFill(COLORREF c);

    /**
     *  设置本行是否提供悬停态的「定位」小按钮。默认 false —— 会话列表、群列表、
     *  通讯录树用的行都不需要它，只有搜索结果里的联系人行才打开。
     *
     *  打开后：鼠标移到本行上时，行右侧的内边距处画一枚地图定位针图标；鼠标再移到
     *  图标上会多垫一层浅色圆底、图标本身也加深一档；此时按下并松开左键发出
     *  DUIN_LISTITEM_LOCATE，<u>不</u>发基类 DuiButton 的 DUIN_CLICK（否则宿主会
     *  既定位又打开会话）。鼠标不在本行上时该图标完全不绘制、也不占位。
     *
     *    show：true 表示鼠标移到本行上时，行右侧显示定位图标。
     */
    void SetLocateButtonVisible(bool show);

    /**
     *  设置行右侧的一组悬停态小按钮（SetLocateButtonVisible 是只有「定位」一个按钮时的写法）。
     *  按钮从左到右排开，最右一个贴着行右内距；鼠标移到本行上（或本行被键盘选中）时才绘制。
     *  按下与松开都落在同一个按钮上时发该按钮的通知码，不发 DUIN_CLICK。按钮显示期间，第一行
     *  右侧的时间文字隐藏，名称与副行的右界让到按钮左边。
     *    actions：按钮种类，取 HoverAction 的值，从左到右；空表示不要按钮。未知取值忽略。
     */
    void SetHoverActions(const std::vector<int>& actions);

    /**
     *  第 index 个悬停按钮的图标矩形（宿主客户区坐标，与 GetRect 同一坐标系）。绘制与命中判定
     *  都用这一份。index 越界、或本行尚未布局时返回全 0 的空矩形。
     */
    RECT HoverActionRect(size_t index) const;

    /**
     *  pt 落在哪个悬停按钮上：返回按钮下标（SetHoverActions 传入的顺序），不在任何按钮上返回 -1。
     *  不看鼠标是否悬停在本行上，只按矩形判定。pt 为宿主客户区坐标。
     */
    int HitTestHoverAction(POINT pt) const;

    // DuiControl 覆写：绘制头像 + 名称 + 副行（+ 状态点 / 时间 / 角标 / 悬停按钮）。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：跟踪鼠标落在哪个悬停按钮上，仅在该状态变化时重绘。
    // 鼠标在行内移动是高频事件，每次都重绘会让整个宿主不停刷新。
    bool OnMouseMove(POINT pt, UINT mkFlags) override;

    // DuiButton 覆写：记下本次按下的落点在哪个悬停按钮上，供 OnLButtonUp 判定。
    // 只判松开点是不够的：在行的空白处按下、拖到图标上再松开，本该是一次普通的行
    // 点击（打开会话），只判松开点会把它当成定位。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

    // DuiButton 覆写：按下与松开的落点<u>都</u>在同一个悬停按钮上时，发该按钮的通知码
    // 并拦截基类的 DUIN_CLICK；否则行为与基类完全一致。
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：鼠标进入 / 离开时，除基类维护 hover 态外主动重绘本行。
    // 基类 OnMouseEnter/OnMouseLeave 只改 m_bHover、<u>不</u>触发重绘——在频繁重绘的
    // 宿主里 hover 底色会被别的重绘"蹭"出来，但在相对静止的宿主（如搜索结果浮窗）
    // 里就一直不刷新，故在此补一次 Invalidate 保证 hover 底色即时显隐。
    bool OnMouseEnter() override;
    bool OnMouseLeave() override;

    // DuiControl 覆写：列表行用普通箭头光标，不用 DuiButton 默认手型。
    bool OnSetCursor(POINT pt) override { return false; }

private:
    // 悬停按钮此刻是否应当显示：配了按钮，且鼠标悬停在本行上或本行被键盘选中。
    bool HoverActionsShown() const;

private:
    CString  m_avatarText;    // 头像内的字 / emoji
    COLORREF m_avatarColor;   // 头像底色
    // 真实头像位图（自定义图头像）；NULL = 没有真图，按字形 + 底色渲染。
    // 本控件持有，析构 / 重设时 DeleteObject。
    HBITMAP  m_avatarBitmap;
    CString  m_name;          // 主行名称
    int      m_nameHiStart;   // 名称高亮起始字符下标，-1 表示无高亮
    int      m_nameHiLen;     // 名称高亮字符数
    CString  m_subText;       // 副行文字
    int      m_subHiStart;    // 副行高亮起始字符下标，-1 表示无高亮
    int      m_subHiLen;      // 副行高亮字符数
    bool     m_keySelected;   // 是否被键盘选中（搜索下拉的上下方向键）；行随刷新重建，状态随之重置
    CString  m_rightText;     // 右上角文字（时间），空 = 不显示
    bool     m_showStatus;    // 是否显示头像状态点
    int      m_status;        // 在线状态（MockStatus）
    int      m_badgeCount;    // 未读角标数字，0 = 不显示
    COLORREF m_hoverFill;     // 悬停底色（默认 design --hover 浅灰）
    // 行右侧的悬停按钮（HoverAction 取值，从左到右）。为空（默认）时既不绘制也不参与鼠标
    // 命中判定，本行的行为与没有按钮时完全一致。
    std::vector<int> m_hoverActions;
    // 鼠标此刻落在第几个悬停按钮上（m_hoverActions 下标），不在任何按钮上为 -1。用于决定
    // 图标画常态色还是加深色 + 浅色圆底；鼠标离开本行时复位为 -1。
    int      m_hotAction;
    // 本次左键<u>按下</u>时落在第几个悬停按钮上，-1 表示不在按钮上。OnLButtonDown 写入、
    // OnLButtonUp 判完即复位；鼠标离开本行时也复位，免得一次没收到松开事件的按下（例如
    // 按住拖出窗口）把状态一直留着，影响下一次点击的判定。
    int      m_pressedAction;
};

} // namespace balloonmain
