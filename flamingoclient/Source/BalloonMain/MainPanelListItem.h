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

    // DuiControl 覆写：绘制头像 + 名称 + 副行（+ 状态点 / 时间 / 角标）。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：鼠标进入 / 离开时，除基类维护 hover 态外主动重绘本行。
    // 基类 OnMouseEnter/OnMouseLeave 只改 m_bHover、<u>不</u>触发重绘——在频繁重绘的
    // 宿主里 hover 底色会被别的重绘"蹭"出来，但在相对静止的宿主（如搜索结果浮窗）
    // 里就一直不刷新，故在此补一次 Invalidate 保证 hover 底色即时显隐。
    bool OnMouseEnter() override;
    bool OnMouseLeave() override;

    // DuiControl 覆写：列表行用普通箭头光标，不用 DuiButton 默认手型。
    bool OnSetCursor(POINT pt) override { return false; }

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
};

} // namespace balloonmain
