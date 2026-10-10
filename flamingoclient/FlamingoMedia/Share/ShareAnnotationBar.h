/**
 *  屏幕共享的标注工具条，ShareAnnotationBar.h
 *
 *  进入标注模式后浮在共享内容（发起人）或观看窗（观看者）底部居中的一条置顶工具条（docs/屏幕共享与录制方案.md 5.9）。
 *  第 19 期起改为一排分组下拉按钮（第 33～38 条），第 20 期加入「选择」与「拓扑」两组（第 37 条），从左到右按 kAnnotGroupOrder：
 *  选择、画笔、线条、箭头、形状、拓扑、标号、文字、橡皮 | 颜色、粗细、线型、填充 | 撤销 |（发起人另有）清空、禁止 / 允许他人标注 |
 *  退出标注。选择工具下有选中对象时，颜色、粗细等样式改到选中的对象上（由控制器处理）。每组点开是一个 DuiMenu，菜单内容、打勾与命令的对应关系由
 *  纯逻辑的 ShareAnnotationMenus 给出；当前工具所在的组按钮显示为主色并写当前工具的名字，颜色按钮前面是当前颜色的色块。
 *  选中菜单项向通知窗口（屏幕共享控制器）PostMessage kMsgShareAnnotCommand（wParam 为 AnnotBarCommand，lParam 为取值），
 *  由控制器统一处理；工具条同时把选择记进自己的状态，界面立即跟着变。「自定义颜色…」由工具条弹出系统取色框。
 *  观看者失去标注权限时工具与颜色等按钮置灰。发起人一侧的工具条排除在屏幕采集之外。
 *  2026-10-05 起选择、画笔、线条、箭头、形状、拓扑、标号、文字、橡皮九个工具组收进一个「绘图工具 ▾」按钮（显示当前工具的
 *  图标与名字），点开是每组一个子菜单的菜单；颜色、粗细、线型、填充仍各占一个按钮。
 *  2026-10-05 起每个分组按钮前面有效果预览图标、下拉菜单每一项也有（ShareAnnotationMenuIcons）；整条放不下所在显示器的
 *  工作区时分组按钮只显示图标与下拉符号。按钮行最左边的「收起」把工具条缩成只有「展开工具条」「退出标注」的一小条，
 *  标注模式不变；收起与否在本对象存续期间保留。
 *  工具条不设所有者窗口：balloonui 把按钮通知发给 GetParent 返回的窗口，弹出窗口设了所有者就会发到所有者那里。
 *  2026-10-07：最左边加「鼠标」按钮（annot_bar_pointer）：进入「鼠标」状态后控制器撤掉输入层，点击穿透到桌面程序，工具条与
 *  已画的标注保留；再选任意工具回到画图。「收起」改成与顶部共享控制条一样的小把手：收起后只剩显示当前工具图标的小把手，
 *  鼠标移上去临时展开，移开约 2 秒后再收起，临时展开时「收起」按钮变成「固定」（TopBarCollapse）。
 *  2026-10-07 晚（方案第 14.2 节）：「收起」右边加「隐藏」：工具条与小把手都不显示，鼠标推到工具条原来所在的屏幕边缘
 *  （多半是底边）时临时出现，离开约 2 秒后再隐藏，临时出现时点「固定」恢复常显。隐藏只在这一次标注期间有效：退出标注后
 *  再进入，工具条重新显示（收起状态照旧保留）。共享与录屏画笔两种场合都适用。
 *
 *  形态与控制条（CShareControlBar）同款：基于 balloonui DuiFrameWindow 的 WS_POPUP 无边框小窗，置顶、不抢焦点、
 *  不进任务栏，按钮全部是 DuiButton。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <vector>
#include "ShareAnnotationMenus.h"
#include "ShareBarPopupGuard.h"   // CShareBarPopupGuard：菜单或取色框开着时被隐藏的处理（2026-10-07）
#include "ShareTopBarCollapse.h"  // TopBarCollapse：收起成小把手、悬停临时展开（2026-10-07）
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace balloonwjui
{
    class DuiButton;
}

namespace screenshare {

//标注工具条
class CShareAnnotationBar : public balloonwjui::DuiFrameWindow
{
public:
    //按钮命令：随 kMsgShareAnnotCommand 的 wParam 发给控制器（取值见 ShareAnnotationMenus.h 的 AnnotBarCommand；
    //这几个名字是第 6 期起就有的，保留给控制器沿用）
    enum Command
    {
        kCmdTool       = annot_bar_tool,         //选了工具：lParam 为 AnnotationTool 之一或 kAnnotEraser
        kCmdColor      = annot_bar_color,        //选了颜色：lParam 为 0x00RRGGBB
        kCmdUndo       = annot_bar_undo,         //撤销本人最近的一笔
        kCmdClear      = annot_bar_clear,        //清空所有人的笔迹（只有发起人的工具条有）
        kCmdPermission = annot_bar_permission,   //发起人切换他人的标注权限：lParam 为 1 允许、0 禁止
        kCmdExit       = annot_bar_exit,         //退出标注模式
        kCmdSave       = annot_bar_save,         //保存为图片（第 23 期）
    };

    CShareAnnotationBar();
    ~CShareAnnotationBar();

    /**
     *  显示在 anchor 下方居中；下方放不下时放进 anchor 底部，并限制在 anchor 所在显示器的工作区内。重复调用会重建内容
     *  并重新摆位。
     *  @param anchor    参照矩形（屏幕坐标）：发起人为共享内容的矩形，观看者为观看窗的矩形
     *  @param notifyWnd 通知窗口，不拥有
     *  @param hostMode  是否发起人（多出清空与权限按钮，且排除在屏幕采集之外）
     */
    void Show(const RECT& anchor, HWND notifyWnd, bool hostMode);

    //是否以主持人模式显示（2026-10-08）：主持人模式的工具条浮在被共享的内容上，按「允许截图」开关决定能否被采集
    bool IsHostMode() const;

    //隐藏并销毁；可重复调用
    void Hide();

    //按新的参照矩形重新摆位（共享的窗口移动了、观看窗改变了大小）
    void MoveTo(const RECT& anchor);

    //刷新当前工具与颜色的高亮（控制器在工具或颜色变了之后调用）
    void SetTool(int tool);
    void SetColor(uint32_t color);

    /**
     *  整体同步当前状态（第 19 期）：工具、颜色、粗细、线型、填充、箭头、块状箭头、标号、文字样式。按钮文字与菜单打勾随之变化。
     *  @param state 控制器记着的当前状态
     */
    void SetState(const AnnotBarState& state);

    //发起人的权限按钮：他人当前能否标注（能则按钮显示「禁止他人标注」）
    void SetViewersAllowed(bool allowed);

    //观看者能否标注：不能时工具、颜色、样式、撤销按钮置灰
    void SetDrawEnabled(bool enabled);

    /**
     *  本机模式（第 21 期，录屏画笔与白板用）：只有本人在画，发起人模式下也不显示「禁止 / 允许他人标注」。在 Show 之前调用。
     *  @param local 是否本机模式
     */
    void SetLocalMode(bool local);

    /**
     *  收起或展开工具条（2026-10-05；2026-10-07 起收起后是贴在原位置的小把手，显示当前工具的图标，鼠标移上去临时展开）。
     *  展开为一直展开；已显示时立即重建并重新摆位。
     *  @param collapsed 是否收起
     */
    void SetCollapsed(bool collapsed);

    //当前是否收起成小把手（临时展开时为 false）
    bool IsCollapsed() const;

    //当前的收起状态，取值见 TopBarState（单元测试用）
    int CollapseState() const;

    /**
     *  收起与临时展开状态下的周期检查（定时器调用；单元测试直接传入时刻与鼠标是否在工具条上）。
     *  @param nowMs  当前时刻（毫秒）
     *  @param inside 鼠标此刻是否在工具条（或小把手）上
     */
    void TickCollapse(int64_t nowMs, bool inside);

    /**
     *  按给定的时刻与光标位置做一次收起 / 隐藏的周期检查（2026-10-07；定时器传 GetTickCount64 与 GetCursorPos，单元测试
     *  直接传，不移动真实鼠标）。除「在不在工具条上」之外还检查光标是否在隐藏后的屏幕边缘感应区里。
     *  @param nowMs  当前时刻（毫秒）
     *  @param cursor 光标位置（屏幕坐标）
     */
    void TickCollapseAt(int64_t nowMs, POINT cursor);

    /**
     *  「鼠标」状态（2026-10-07）：控制器进入或退出后同步给工具条，「鼠标」按钮为主色、「绘图工具」按钮不再高亮。
     *  @param on 是否处于「鼠标」状态
     */
    void SetPointerMode(bool on);

    //是否处于「鼠标」状态
    bool IsPointerMode() const;

    //对外可见的控件 id（单元测试按它找按钮）：分组按钮从 kIdGroupBase 起按 AnnotBarGroup 编
    enum
    {
        kIdGroupBase = 20,   //第一个分组按钮（画笔）
        kIdCollapse  = 66,   //收起工具条（「鼠标」右边的小按钮）；临时展开时为「固定工具条」
        kIdExpand    = 67,   //小把手上的当前工具图标（收起后），点一下展开并固定
        kIdTools     = 68,   //「绘图工具 ▾」（2026-10-05 起九个工具组收进它的菜单）
        kIdPointer   = 69,   //「鼠标」（2026-10-07，整条最左边）：不拦截鼠标，工具条与标注保留
        kIdHide      = 70,   //「隐藏」（2026-10-07 晚，「收起」右边）：工具条与小把手都不显示，鼠标到屏幕边缘时重新出现
        kIdSelect    = 71,   //「选择」（2026-10-08，「绘图工具」左边）：进入选择工具，再点一下回到上一个画图工具
    };

    //投给自己的「收起 / 展开」（2026-10-05）：wParam 为 1 收起、0 展开、2 隐藏（2026-10-07 晚）。等按钮的点击通知处理完
    //再重建界面，避免在点击通知里销毁按钮本身（窗口私有消息，登记见开发约定文档「UI 约定」第 6 条）
    static const UINT kMsgCollapseToggle = WM_APP + 0xD0;
    //kMsgCollapseToggle 的 wParam 取值
    static const WPARAM kCollapseParamExpand = 0;    //点了小把手：展开并固定
    static const WPARAM kCollapseParamToggle = 1;    //点了「收起 / 固定」
    static const WPARAM kCollapseParamHide   = 2;    //点了「隐藏」

    //投给自己的「弹出下拉菜单」（2026-10-07）：wParam 为分组按钮的 AnnotBarGroup 取值，或 kMenuParamTools 表示「绘图工具」。
    //等按钮的鼠标事件处理完再弹，菜单的嵌套消息循环不再套在按钮的点击通知里；菜单或取色框开着时被要求隐藏，先关掉它们、
    //只隐藏窗口，返回后再销毁（窗口私有消息，登记见开发约定文档「UI 约定」第 6 条）
    static const UINT kMsgShowMenu = WM_APP + 0xD3;
    //kMsgShowMenu 的 wParam：弹「绘图工具」菜单（分组取值都小于 kAnnotGroupCount，不会与它相同）
    static const WPARAM kMenuParamTools = 0xFF;

    //菜单或取色框是否正在弹出（单元测试用）
    bool IsPopupActive() const;

    BEGIN_MSG_MAP(CShareAnnotationBar)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kMsgCollapseToggle, OnCollapseToggle)
        MESSAGE_HANDLER(kMsgShowMenu, OnShowMenu)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //其余按钮的控件 id（避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3）
    enum
    {
        kIdUndo       = 60,   //撤销
        kIdClear      = 61,   //清空
        kIdPermission = 62,   //禁止 / 允许他人标注
        kIdExit       = 63,   //退出标注
        kIdSave       = 64,   //保存为图片（第 23 期），本机模式没有
    };

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //kMsgCollapseToggle：wParam 为 1 时是点了「收起 / 固定」，为 0 时是点了小把手（展开并固定）
    LRESULT OnCollapseToggle(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //收起与临时展开状态下检查鼠标位置（kTopBarTimerId）；其余定时器交给基类
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //按 m_collapse 的状态重建界面（小把手或整条）并摆放，开关检查鼠标的定时器
    void applyCollapse();
    //小把手的窗口矩形：在整条展开时的矩形底边正中
    RECT handleRect() const;
    //kMsgShowMenu：弹出分组或「绘图工具」菜单；期间被要求隐藏时，返回后在这里销毁窗口
    LRESULT OnShowMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //取色框的钩子：WM_INITDIALOG 时把对话框窗口登记到 m_popup，被要求隐藏时据此取消取色框
    static UINT_PTR CALLBACK colorDialogHook(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam);
    //参照矩形所在显示器的工作区（屏幕坐标）
    static RECT monitorWorkArea(const RECT& anchor);
    void EnsureWindow();
    void BuildUi();
    //收起后的小把手：只有一个显示当前工具图标的按钮
    void buildCollapsedUi();
    void refresh();
    int barWidth() const;
    //按当前工具与样式重建各分组按钮与「展开工具条」按钮前面的图标
    void refreshGroupIcons();
    //某个分组按钮的宽度：按这一组按钮上可能出现的最长文字，非简体中文界面下按译文加宽
    int groupWidth(int group) const;
    //弹出某一组的下拉菜单，选中后处理
    void showGroupMenu(int group);
    //弹出「绘图工具」菜单（九个工具组各一个子菜单），选中后处理
    void showToolsMenu();
    //「绘图工具」按钮的宽度：按九个工具组的按钮可能出现的最长文字，加图标
    int toolsWidth() const;
    /**
     *  处理一个选中的菜单命令：自定义颜色先弹取色框；其余把选择记进本地状态，再发给控制器。
     *  @param choice 解码后的命令
     */
    void applyChoice(const AnnotMenuChoice& choice);
    //按当前颜色重建颜色按钮前面的色块
    void refreshColorSwatch();

    HWND                                 m_notifyWnd;      //通知窗口（不拥有）
    bool                                 m_hostMode;       //是否发起人
    AnnotBarState                        m_state;          //当前状态（工具、颜色与各项样式）
    bool                                 m_viewersAllowed; //他人能否标注（发起人的权限按钮）
    bool                                 m_drawEnabled;    //本人能否标注
    bool                                 m_localMode;      //本机模式（第 21 期）：不显示权限按钮
    std::vector<balloonwjui::DuiButton*> m_groupButtons;   //分组按钮，下标为 AnnotBarGroup 的取值（控件树持有，本指针仅引用）
    balloonwjui::DuiButton*              m_undoButton;     //撤销按钮（同上）
    balloonwjui::DuiButton*              m_permButton;     //权限按钮（同上）；观看者没有
    HBITMAP                              m_swatch;         //颜色按钮前面的当前颜色色块（本类生成，颜色变了重建，析构释放）
    HBITMAP                              m_bgBitmap;       //纯色背景图（本类生成、析构释放）
    std::vector<HBITMAP>                 m_groupIcons;     //各分组按钮前面的效果图标，下标为 AnnotBarGroup 的取值；本类生成，
                                                           //状态变了重建、析构释放；没有图标的组为 NULL
    HBITMAP                              m_expandIcon;     //当前工具的图标，「绘图工具」与「展开工具条」两个按钮共用；本类生成，
                                                           //状态变了重建、析构释放
    balloonwjui::DuiButton*              m_toolsButton;    //「绘图工具 ▾」按钮（控件树持有，本指针仅引用）；收起时为 NULL
    HBITMAP                              m_collapseIcon;   //「收起」按钮的双左箭头图标，本类生成、析构释放
    balloonwjui::DuiButton*              m_expandButton;   //小把手上的当前工具图标按钮（控件树持有，本指针仅引用）；展开时为 NULL
    TopBarCollapse                       m_collapse;       //收起 / 临时展开 / 一直展开（2026-10-07），在本对象存续期间保留
    bool                                 m_handleUi;       //当前建的界面是不是小把手
    RECT                                 m_expandedRect;   //整条展开时的窗口矩形（屏幕坐标），小把手贴在它的底边正中
    bool                                 m_pointerMode;    //是否处于「鼠标」状态（2026-10-07）
    balloonwjui::DuiButton*              m_pointerButton;  //「鼠标」按钮（控件树持有，本指针仅引用）；收起时为 NULL
    balloonwjui::DuiButton*              m_collapseButton; //「收起 / 固定」按钮（同上）
    balloonwjui::DuiButton*              m_hideButton;     //「隐藏」按钮（同上，2026-10-07 晚）
    HBITMAP                              m_hideIcon;       //「隐藏」按钮的眼睛加斜线图标（深色），本类生成、析构释放
    balloonwjui::DuiButton*              m_selectButton;   //「选择」按钮（2026-10-08，控件树持有，本指针仅引用）；收起时为 NULL
    HBITMAP                              m_selectIcon;     //「选择」按钮的图标（与菜单里「选择对象」的图标相同），本类生成、析构释放
    HBITMAP                              m_toolsIcon;      //选择状态下「绘图工具」按钮的图标（最近一次用的画图工具），本类生成、
                                                           //状态变了重建、析构释放；不在选择状态时为 NULL（「绘图工具」用 m_expandIcon）
    RECT                                 m_monitorRect;    //参照矩形所在显示器的整块矩形（屏幕坐标），MoveTo 时记下；
                                                           //隐藏后的屏幕边缘感应区据此计算
    HBITMAP                              m_pointerIcon;    //「鼠标」按钮的箭头图标（深色，常态），本类生成、析构释放
    HBITMAP                              m_pointerIconOn;  //「鼠标」按钮的箭头图标（白色，「鼠标」状态下的主色按钮用），同上
    HBITMAP                              m_pinIcon;        //临时展开时「固定工具条」的图钉图标，本类生成、析构释放
    bool                                 m_compact;        //整条放不下工作区时为 true：分组按钮只显示图标与下拉符号
    RECT                                 m_anchor;         //参照矩形
    //系统取色框的「自定义颜色」一栏固定 16 格（CHOOSECOLOR::lpCustColors 的约定）
    static const int kCustomColorSlots = 16;
    COLORREF                             m_customColors[kCustomColorSlots];   //取色框的「自定义颜色」一栏（随工具条对象保留）
    CShareBarPopupGuard                  m_popup;          //下拉菜单与取色框的弹出状态与推迟销毁（2026-10-07）
};

} // namespace screenshare
