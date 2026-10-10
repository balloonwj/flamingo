/**
 *  ChatIconButton.h
 *
 *  单聊窗用的透明底图标按钮 —— 头部的语音/视频/记录/更多，输入工具栏的
 *  表情/图片/文件/截图/语音/更多，都用它。继承 DuiButton 复用点击 /
 *  通知逻辑，只覆写 OnPaint。
 *
 *  2026-10-03 起可以在右上角显示一个数字角标（balloonui 的 DuiBadge，作为子控件），
 *  头部的文件共享按钮用它显示本会话里可用共享的个数：SetBadgeCount(n)，n 为 0 时不显示。
 *
 *  2026-10-05 起有高亮状态（SetActive）与随状态切换的悬停提示（SetToggleTips），头部「字幕模式」按钮用它们
 *  表示本会话的字幕开着：图标变蓝，提示为「字幕已开启」，关着时为「字幕已关闭」。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloonwjui {
class DuiBadge;
}

namespace balloonchatwnd {

/**
 *  ChatIconButton —— 透明底图标按钮，悬停时显示浅灰圆角底；SetActive(true) 后高亮（图标画成蓝色，不加底色）。
 *  需要时可以用 SetToggleTips 给开、关两种状态各设一条悬停提示，SetActive 切换状态时自动换成对应的那条。
 */
class ChatIconButton : public balloonwjui::DuiButton
{
public:
    ChatIconButton();
    ~ChatIconButton();   // 注销本控件的 tooltip 注册

    // 设置图标类型，取值见 ChatIcons.h 的 ChatIconKind。
    void SetIconKind(int kind);

    // 当前的图标类型（ChatIconKind）。
    int GetIconKind() const { return m_iconKind; }

    // DuiControl 覆写：绘制图标（+ 悬停底）。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：用普通箭头光标，不用 DuiButton 默认手型。
    // 鼠标移入、移出时的重画由基类 DuiButton 负责，本类不再覆写 OnMouseEnter / OnMouseLeave。
    bool OnSetCursor(POINT pt) override { return false; }

    /**
     *  设置右上角角标上的数字（头部文件共享按钮显示本会话可用共享的个数）。
     *  @param count 个数；小于等于 0 时不显示角标，超过 99 时显示「99+」
     */
    void SetBadgeCount(int count);

    // 角标上当前的数字；不显示角标时为 0
    int GetBadgeCount() const { return m_badgeCount; }

    // 角标的定位框（与控件矩形同一坐标系，单元测试用）；还没有角标时为空矩形
    RECT GetBadgeRect() const;

    /**
     *  设置按钮是否高亮：高亮时图标画成蓝色（不加底色，悬停时与普通按钮一样显示浅灰底），用来表示
     *  这个按钮代表的功能正开着（头部「字幕模式」按钮在本会话的字幕开着时高亮，docs/聊天字幕模式方案.md
     *  第 1 节第 11 条）。设置过 SetToggleTips 时，同时把悬停提示换成对应状态的那条。
     *  @param active true 高亮，false 恢复普通样式；与当前状态相同时不重画、不重新登记提示
     */
    void SetActive(bool active);

    // 按钮当前是否高亮
    bool IsActive() const { return m_active; }

    /**
     *  给开、关两种状态各设一条悬停提示（如「字幕已开启」/「字幕已关闭」），并立即按当前状态登记到
     *  DuiToolTipMgr；之后每次 SetActive 改变状态时自动换成对应的那条。不调用本函数时，SetActive
     *  不动悬停提示，调用方可以照旧自己登记。
     *  @param activeTip   高亮（功能开着）时的提示文字，复制保存，调用方不必保留
     *  @param inactiveTip 普通（功能关着）时的提示文字，复制保存，调用方不必保留
     */
    void SetToggleTips(LPCTSTR activeTip, LPCTSTR inactiveTip);

    /**
     *  当前状态对应的那条悬停提示（SetToggleTips 设置的两条之一，高亮时取 activeTip，否则取 inactiveTip）。
     *  @return 提示文字；没有调用过 SetToggleTips 时为空串
     */
    CString GetToggleTip() const;

    // DuiControl 覆写：按钮排版后把角标放到右上角
    void Layout(const RECT& rcAvail) override;

private:
    // 按当前按钮矩形摆放角标
    void PlaceBadge();

    // 设置过两态提示时，把当前状态对应的那条登记到 DuiToolTipMgr（覆盖原来登记的文字）
    void ApplyToggleTip();

    int m_iconKind;   // 图标类型（ChatIconKind）
    balloonwjui::DuiBadge* m_badge;   // 右上角的角标（子控件，归本按钮所有）；第一次显示数字时创建，此前为 NULL
    int m_badgeCount;                 // 角标上的数字；不显示角标时为 0
    bool m_active;                    // 是否高亮（SetActive 设置）；高亮时图标画成蓝色，不加底色
    bool m_hasToggleTips;             // 是否调用过 SetToggleTips；为 false 时 SetActive 不动悬停提示
    CString m_activeTip;              // 高亮时的悬停提示（SetToggleTips 设置，生命周期同按钮）
    CString m_inactiveTip;            // 普通状态的悬停提示（SetToggleTips 设置，生命周期同按钮）
};

} // namespace balloonchatwnd
