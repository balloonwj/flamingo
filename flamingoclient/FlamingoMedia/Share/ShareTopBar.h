/**
 *  顶部浮动条的公共基类，ShareTopBar.h
 *
 *  屏幕共享主持人的控制条（CShareControlBar）、多人共享里共享者的控制条（CShareSharerBar）与观看端的浮动工具条
 *  （CViewerFloatBar，远程桌面被控端的顶部条 CRdControlledBar 也派生自它）是同一种外观：一条浮在摆放区域顶部居中的深色窄条，
 *  上面是 18 像素高的标题条，下面是一排 30×30 的方形图标按钮（ShareBarButtonArt），最右端是「隐藏」与「收起 / 固定」。
 *  2026-10-14 之前三者各写一份建窗、摆位与收起逻辑，现在提到本类（docs/远程桌面控制权限与观看工具条方案.md 8.2 第 6 条）：
 *    - 建窗：标题条高、去掉标题栏按钮、不可缩放、无边框、深色底与透明标题条；窗口样式与所有者由子类给出；
 *    - 摆位：在「摆放区域」顶部居中，宽度不超过摆放区域（放不下时由子类左侧的状态文字让出宽度）；
 *    - 收起与隐藏（TopBarCollapse）：收起成顶边正中的小把手、隐藏后鼠标推到「感应区域」顶边时临时出现，
 *      定时检查鼠标位置，「隐藏」「收起 / 固定」两个按钮的外观与点击。
 *  各条自己的按钮、状态文字、菜单与通知方式留在子类。
 *
 *  子类的用法：
 *      构造时传入自己的「隐藏」「收起 / 固定」控件 id；
 *      建窗：CreateTopBar(owner, title, style, exStyle)；
 *      建控件树：row = MakeTopBarRow(); ...子类的按钮...; AddTopBarOwnButtons(row); SetTopBarWidth(整条宽);
 *               SetClientContent(MakeTopBarRoot(row)); ApplyOwnButtonLooks();
 *      摆位：PlaceTopBar(摆放区域, 感应区域)；拖动时只挪窗口：MoveTopBar(...)；
 *      点击通知里先调 HandleTopBarClick(ctrlId)，返回 true 时就是这两个按钮、已处理；
 *      消息映射以 CHAIN_MSG_MAP(CShareTopBar) 结尾（本类处理收起检查的定时器）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-14
 */
#pragma once

#include <stdint.h>
#include <memory>
#include <string>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "ShareTopBarCollapse.h"   // TopBarCollapse：收起 / 隐藏
#include "ShareBarButtonArt.h"     // ShareBarButtonArtCache、kShareTopBar* 尺寸与颜色

namespace balloonwjui
{
    class DuiButton;
}

namespace screenshare {

//顶部浮动条的公共基类
class CShareTopBar : public balloonwjui::DuiFrameWindow
{
public:
    //收起 / 隐藏的当前状态，取值见 TopBarState（单元测试用）
    int CollapseState() const;

    //展开时的整条宽（像素，单元测试用）
    int BarWidthForTest() const;

    /**
     *  按给定的时刻与光标位置做一次收起 / 隐藏的周期检查（定时器传 GetTickCount64 与 GetCursorPos；单元测试直接传，
     *  不移动真实鼠标）。状态变了就重新摆放窗口。
     *  @param nowMs  当前时刻（毫秒）
     *  @param cursor 光标位置（屏幕坐标）
     */
    void TickCollapseAt(int64_t nowMs, POINT cursor);

    BEGIN_MSG_MAP(CShareTopBar)
        MESSAGE_HANDLER(WM_TIMER, OnTopBarTimer)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

protected:
    /**
     *  @param hideId   本条「隐藏」按钮的控件 id（避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3 与子类自己的 id）
     *  @param toggleId 本条「收起 / 固定」按钮的控件 id
     */
    CShareTopBar(UINT hideId, UINT toggleId);
    virtual ~CShareTopBar();

    /**
     *  建窗（已有窗口时什么也不做）：标题条高、去掉标题栏按钮、不可缩放、无边框、深色底与透明标题条。
     *  @param owner   所有者窗口；为 NULL 表示没有所有者（不拥有）
     *  @param title   标题（已翻译）
     *  @param style   窗口样式（如 WS_POPUP）
     *  @param exStyle 扩展样式（如 WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW）
     *  @return 窗口存在（新建成功或本来就有）返回 true
     */
    bool CreateTopBar(HWND owner, const wchar_t* title, DWORD style, DWORD exStyle);

    /**
     *  摆放时是否放到最上层（置顶）。置顶的条每次摆放都放回 HWND_TOPMOST；不置顶的条不改 Z 序
     *  （有所有者的条本来就在所有者之上）。只记下，不立即改窗口。
     */
    void SetTopBarTopmost(bool topmost);
    bool IsTopBarTopmost() const;

    /**
     *  标题条的文字：展开（含临时展开）时写 expanded，收起成小把手时写 kTopBarHandlePrefix + handle。
     *  窗口已建好时立即按当前状态刷新标题。
     *  @param expanded 展开时的标题（已翻译）
     *  @param handle   收起成小把手时的标题（已翻译），前面另加向下的箭头
     */
    void SetTopBarTitles(const std::wstring& expanded, const std::wstring& handle);

    //设置展开时的整条宽（像素），子类建控件树时按自己的按钮算出；下次摆放时生效
    void SetTopBarWidth(int width);

    /**
     *  在摆放区域顶部居中摆放（宽度不超过摆放区域），并按当前收起状态刷新标题、窗口、「收起 / 固定」按钮与定时器。
     *  @param placeArea 摆放区域（屏幕坐标）：如显示器工作区、观看窗的画面区域
     *  @param edgeArea  隐藏后的感应区域（屏幕坐标）：鼠标推到它的顶边时临时出现，如整块显示器、观看窗的画面区域
     */
    void PlaceTopBar(const RECT& placeArea, const RECT& edgeArea);

    /**
     *  只按新的摆放区域挪窗口（拖动、缩放观看窗时每秒可达上百次）：标题、按钮外观与定时器都不变。
     *  @param placeArea 摆放区域（屏幕坐标）
     *  @param edgeArea  感应区域（屏幕坐标）
     */
    void MoveTopBar(const RECT& placeArea, const RECT& edgeArea);

    //按当前收起状态摆放窗口，并刷新标题、「收起 / 固定」按钮与定时器（收起状态变了之后调用）
    void ApplyCollapse();

    //回到一直展开（每次重新显示时调用）；不摆放窗口
    void ResetCollapse();

    /**
     *  处理「隐藏」「收起 / 固定」的点击（子类在点击通知里先调用）。
     *  @param ctrlId 被点击的控件 id
     *  @return 是这两个按钮之一、已处理返回 true；否则返回 false，由子类继续处理
     */
    bool HandleTopBarClick(UINT ctrlId);

    //建一条内容横条：左右内边距与按钮间距按 kShareTopBar* 常量
    std::unique_ptr<balloonwjui::DuiHBox> MakeTopBarRow() const;

    //把内容横条装进深色底的根节点，交给 SetClientContent
    std::unique_ptr<balloonwjui::DuiVBox> MakeTopBarRoot(std::unique_ptr<balloonwjui::DuiHBox> row) const;

    /**
     *  在横条里加一个 kShareBarBtnPx 见方、上下留白的按钮（外观由调用方用 Art() 装上）。
     *  @param row 横条（不拥有）
     *  @param id  控件 id
     *  @return 按钮（控件树持有，仅引用）
     */
    balloonwjui::DuiButton* AddTopBarIconButton(balloonwjui::DuiHBox* row, UINT id);

    //在横条最右端加「隐藏」「收起 / 固定」两个按钮（「隐藏」在左）
    void AddTopBarOwnButtons(balloonwjui::DuiHBox* row);

    //给「隐藏」「收起 / 固定」装上外观与悬停提示（建好控件树后调用）
    void ApplyOwnButtonLooks();

    //「隐藏」「收起 / 固定」两个按钮在整条宽里占的宽度（含它们之前的一个间距），子类算整条宽时加上
    static int OwnButtonsWidth();

    //停掉收起检查的定时器、回到一直展开、忘掉控件树里的按钮（子类隐藏或销毁窗口之前调用）
    void StopTopBar();

    //按钮四态位图的缓存（子类给自己的按钮装外观用）
    ShareBarButtonArtCache& Art();

    //展开时的窗口矩形（屏幕坐标），最近一次摆放时算出
    const RECT& ExpandedRect() const;

private:
    CShareTopBar(const CShareTopBar&);
    CShareTopBar& operator=(const CShareTopBar&);

    //收起检查的定时器：每 kTopBarTickMs 检查一次鼠标；其余定时器交给子类与基类
    LRESULT OnTopBarTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //按摆放区域与整条宽算出展开时的窗口矩形，记下感应区域
    void computeExpandedRect(const RECT& placeArea, const RECT& edgeArea);
    //按收起状态只摆放窗口：展开为整条、收起为小把手、隐藏时隐去窗口
    void applyWindowRect();
    //按收起状态设置「收起 / 固定」按钮的图标与提示
    void applyToggleLook();

    UINT                    m_hideId;        //「隐藏」按钮的控件 id
    UINT                    m_toggleId;      //「收起 / 固定」按钮的控件 id
    bool                    m_topmost;       //摆放时是否放到最上层（见 SetTopBarTopmost）
    int                     m_barWidth;      //展开时的整条宽（像素）
    RECT                    m_expandedRect;  //展开时的窗口矩形（屏幕坐标）
    RECT                    m_edgeArea;      //隐藏后的感应区域（屏幕坐标）
    std::wstring            m_expandedTitle; //展开时的标题
    std::wstring            m_handleTitle;   //收起成小把手时的标题（不含箭头）
    TopBarCollapse          m_collapse;      //收起 / 隐藏状态
    ShareBarButtonArtCache  m_art;           //按钮四态位图的缓存（须比按钮活得久，作为成员持有）
    HBITMAP                 m_bgBitmap;      //纯色背景图（本类生成、析构释放）
    balloonwjui::DuiButton* m_hideBtn;       //「隐藏」按钮（控件树持有，本指针仅引用）
    balloonwjui::DuiButton* m_toggleBtn;     //「收起 / 固定」按钮（控件树持有，本指针仅引用）
};

} // namespace screenshare
