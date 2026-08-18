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
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

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

    // DuiControl 覆写：绘制头像 + 名称 + 副行（+ 状态点 / 时间 / 角标 / 定位按钮）。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：跟踪鼠标是否落在「定位」图标上，仅在该状态翻转时重绘。
    // 鼠标在行内移动是高频事件，每次都重绘会让整个宿主不停刷新。
    bool OnMouseMove(POINT pt, UINT mkFlags) override;

    // DuiButton 覆写：记下本次按下的落点在不在「定位」图标内，供 OnLButtonUp 判定。
    // 只判松开点是不够的：在行的空白处按下、拖到图标上再松开，本该是一次普通的行
    // 点击（打开会话），只判松开点会把它当成定位。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

    // DuiButton 覆写：按下与松开的落点<u>都</u>在「定位」图标内时，发
    // DUIN_LISTITEM_LOCATE 并拦截基类的 DUIN_CLICK；否则行为与基类完全一致。
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
    // 算出「定位」小按钮的图标矩形（宿主客户区坐标，与 m_rcItem 同一坐标系）。
    // 绘制、鼠标命中判定都取这一份，免得两处各算一遍慢慢对不上。本行高度非正时
    // 返回全 0 的空矩形，调用方须自行判空。
    RECT LocateIconRect() const;

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
    CString  m_rightText;     // 右上角文字（时间），空 = 不显示
    bool     m_showStatus;    // 是否显示头像状态点
    int      m_status;        // 在线状态（MockStatus）
    int      m_badgeCount;    // 未读角标数字，0 = 不显示
    COLORREF m_hoverFill;     // 悬停底色（默认 design --hover 浅灰）
    // 本行是否提供悬停态的「定位」小按钮。false（默认）时该按钮既不绘制也不参与
    // 鼠标命中判定，本行的行为与改造前完全一致。
    bool     m_showLocate;
    // 鼠标此刻是否正落在「定位」图标矩形内。仅在 m_showLocate 为 true 时有意义，
    // 用于决定图标画常态色还是加深色 + 浅色圆底；鼠标离开本行时复位为 false。
    bool     m_locateHot;
    // 本次左键<u>按下</u>时落点在不在「定位」图标内。OnLButtonDown 写入、
    // OnLButtonUp 判完即复位；鼠标离开本行时也复位，免得一次没收到松开事件的
    // 按下（例如按住拖出窗口）把状态一直留着，影响下一次点击的判定。
    bool     m_locatePressed;
};

} // namespace balloonmain
