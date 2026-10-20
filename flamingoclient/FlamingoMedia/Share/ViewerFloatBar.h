/**
 *  观看端的浮动工具条，ViewerFloatBar.h
 *
 *  屏幕共享观看者与远程桌面控制端共用（docs/远程桌面控制权限与观看工具条方案.md 第 4.3 节，2026-10-12）；远程桌面被控端的
 *  顶部条（CRdControlledBar）也派生自本类，它没有所有者，用 SetTopmost 置顶，并用 SetLinkBadgeVisible 显示「P2P」徽章。原来观看窗的
 *  按钮放在窗口标题栏上（窗口模式）或借用远程桌面的文字按钮条（全屏），改为一条与共享者控制条（CShareControlBar）同样式的
 *  浮动工具条：深色底、30×30 方形图标按钮（ShareBarButtonArt）、悬停提示、开关打开时蓝底、申请中橙底，左侧可放一段
 *  状态文字，最右端是「隐藏」「收起 / 固定」（TopBarCollapse）。2026-10-14 起建窗、摆位与收起 / 隐藏由公共基类 CShareTopBar 负责，
 *  本类只管按钮清单、状态文字、「P2P」徽章、排除采集与置顶，以及下面几项与窗口样式有关的处理。
 *
 *  - 位置：浮在参照矩形的顶部居中。窗口模式下参照矩形是观看窗的画面区域，全屏时是显示器矩形；观看窗移动、改变大小或
 *    切换全屏时，由观看窗调 MoveTo 交来新的参照矩形。
 *  - 窗口关系：以观看窗为所有者，始终在观看窗之上、随它最小化，不进任务栏、不抢焦点（键盘焦点留在观看窗，远程桌面的
 *    按键转发不受影响）。窗口是有所有者的 WS_POPUP 窗口：2026-10-14 起 balloonui 只把 WS_CHILD 宿主的控件通知发给父窗口，
 *    按钮通知发回本窗口（此前会发给所有者，本类曾为此建成重叠窗口再去掉标题栏，并自己处理 WM_WINDOWPOSCHANGING 与
 *    WM_NCACTIVATE，见 docs/远程桌面控制权限与观看工具条方案.md 8n）。
 *  - 按钮：由使用方用 SetButtons 给出（编号、种类、状态、提示、可选文字）；点击时向通知窗口 PostMessage(通知消息, 编号, 0)。
 *    按钮集合（编号与有无文字）变了时整条重建并重新摆位，只是状态、提示、文字变了时原地刷新。工具条自己不弹菜单：菜单由
 *    使用方弹出，用 GetButtonScreenRect 取按钮的屏幕矩形贴在它下方。
 *
 *  典型用法（观看窗里）：
 *      m_bar.SetCaptionText(TR(L"屏幕共享"));
 *      m_bar.SetBarButtons(buttons);
 *      m_bar.Show(m_hWnd, m_hWnd, WM_XXX_BAR_CMD, pictureScreenRect);
 *      ...WM_MOVE / WM_SIZE / 切换全屏后：m_bar.MoveTo(新的参照矩形);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-12
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "ShareTopBar.h"           // CShareTopBar：顶部浮动条的公共基类（建窗、摆位、收起 / 隐藏，2026-10-14）

namespace balloonwjui
{
    class DuiLabel;
    class DuiButton;
    class DuiHBox;
}

namespace screenshare {

//使用方按钮编号的起点：小于它的编号留给工具条自己的按钮（隐藏、收起）与 DuiFrameWindow 标题栏保留的 1 / 2 / 3
const UINT kViewerBarUserIdFirst = 100;

//浮动观看工具条上一个由使用方给出的按钮
struct ViewerBarButtonSpec
{
    UINT         m_id;       //按钮编号：点击时作为通知消息的 wParam 回传；取 kViewerBarUserIdFirst 及以上，同一条上不重复
    int          m_action;   //按钮种类，取值见 ShareBarAction，决定图标
    int          m_state;    //按钮状态，取值见 ShareBarTriState；普通开关只用 share_bar_tri_off / share_bar_tri_on
    std::wstring m_tooltip;  //悬停提示（已翻译）；为空时取 ShareBarTooltipForTriState(m_action, m_state)，只有文字与默认不同时才给
    std::wstring m_text;     //按钮文字（已翻译，如「3 人观看 ▾」）；为空时是边长 kShareBarBtnPx 的方形图标按钮。
                             //种类为 share_bar_viewers 时文字前另画人形图标

    ViewerBarButtonSpec();
    ViewerBarButtonSpec(UINT id, int action, int state, const std::wstring& tooltip);
};

/**
 *  观看窗交给浮动工具条的参照矩形（Show / MoveTo 的 anchor），屏幕共享观看窗与远程桌面观看窗共用。
 *  @param viewer 观看窗（借用）
 *  @return 屏幕坐标：全屏时为观看窗所在显示器的矩形，窗口模式时为标题栏以下的画面区域；窗口不存在时为空矩形
 */
RECT ComputeViewerBarAnchor(const balloonwjui::DuiFrameWindow& viewer);

//观看端的浮动工具条
class CViewerFloatBar : public CShareTopBar
{
public:
    //工具条自己的按钮编号（对外可见，单元测试按它找按钮）
    enum
    {
        kIdHide   = 10,   //隐藏工具条：工具条与把手都不显示，鼠标推到参照矩形顶边时临时出现
        kIdToggle = 11,   //收起 / 固定：收起后只剩顶边正中的小把手
    };

    //工具条顶边距参照矩形顶边的距离，单位像素
    static const int kTopMarginPx = kShareTopBarTopMarginPx;

    CViewerFloatBar();
    ~CViewerFloatBar();

    /**
     *  显示工具条（第一次调用时建窗；已显示时按新的参照矩形重新摆位）。每次调用都从「一直展开」开始。
     *  @param owner     所有者窗口（观看窗）：工具条始终在它之上、随它最小化；不拥有
     *  @param notifyWnd 按钮点击时 PostMessage(notifyMsg, 按钮编号, 0) 的窗口；不拥有
     *  @param notifyMsg 通知消息号
     *  @param anchor    参照矩形（屏幕坐标）：窗口模式为观看窗的画面区域，全屏时为显示器矩形
     */
    void Show(HWND owner, HWND notifyWnd, UINT notifyMsg, const RECT& anchor);

    /**
     *  按新的参照矩形重新摆位（观看窗移动、改变大小、切换全屏之后调用）。收起、隐藏状态保持不变。
     *  @param anchor 参照矩形（屏幕坐标）
     */
    void MoveTo(const RECT& anchor);

    //隐藏并销毁工具条窗口；可重复调用。按钮清单、状态文字保留，下次 Show 时沿用
    void Hide();

    /**
     *  标题条文字（如「屏幕共享」「远程桌面」）：展开时写在 18 像素高的标题条上，收起成小把手时前面加「▼ 」。
     *  @param text 已翻译的文字
     */
    void SetCaptionText(const std::wstring& text);

    /**
     *  左侧的状态文字（如远程桌面的「仅查看」「控制中」）；为空时不显示状态区。有无状态文字变了时重建并重新摆位。
     *  @param text 已翻译的文字
     */
    void SetStatusText(const std::wstring& text);

    /**
     *  是否把工具条排除在屏幕采集之外（2026-10-12）：观看窗排除时工具条跟着排除，否则本人共享整块屏幕时工具条会单独出现在
     *  共享画面里。窗口还没建时记下，建好后生效。
     *  @param excluded true 排除，false 允许采集
     */
    void SetCaptureExcluded(bool excluded);

    /**
     *  是否置顶（2026-10-12，远程桌面被控端的顶部条用）：没有所有者的工具条要置顶才不会被其他窗口挡住。
     *  窗口还没建时记下，建好后生效；已建好时立即改变 Z 序。
     *  @param topmost true 置顶，false 不置顶（默认，有所有者的工具条本来就在所有者之上）
     */
    void SetTopmost(bool topmost);

    /**
     *  状态文字后面的「P2P」徽章（2026-10-12，远程桌面被控端的顶部条用）：画面经直连链路传输时显示。
     *  有无徽章变了时重建并重新摆位。
     *  @param visible true 显示，false 不显示（默认）
     */
    void SetLinkBadgeVisible(bool visible);

    //当前是否显示「P2P」徽章（单元测试用）
    bool IsLinkBadgeVisible() const;

    /**
     *  设置按钮。编号序列或有无文字变了时整条重建并重新摆位；只有状态、提示、文字变了时原地刷新。
     *  不要在本工具条按钮的点击通知里同步调用（通知是 PostMessage 投给使用方的，正常使用不会出现这种情况）。
     *  @param buttons 按钮清单，按从左到右的顺序
     */
    void SetBarButtons(const std::vector<ViewerBarButtonSpec>& buttons);

    /**
     *  取某个按钮在屏幕上的矩形，菜单、浮窗据此贴在按钮下方弹出。
     *  @param id  按钮编号
     *  @param out [出参] 屏幕坐标
     *  @return 工具条可见且有这个按钮时返回 true
     */
    bool GetButtonScreenRect(UINT id, RECT& out) const;

    //状态文字当前的内容（单元测试用）；没有状态区时为空
    std::wstring StatusTextForTest() const;

    BEGIN_MSG_MAP(CViewerFloatBar)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
        CHAIN_MSG_MAP(CShareTopBar)
    END_MSG_MAP()

private:
    CViewerFloatBar(const CViewerFloatBar&);
    CViewerFloatBar& operator=(const CViewerFloatBar&);

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //WM_MOUSEACTIVATE：返回 MA_NOACTIVATE，点击工具条不激活它。默认处理在客户区按下时返回 MA_ACTIVATE，尽管窗口带
    //WS_EX_NOACTIVATE，按下鼠标后工具条仍成为前台窗口，观看窗随之失去键盘焦点（远程桌面控制中按键不再转发给对方）；
    //远程桌面被控端的顶部条没有所有者，点它也会把被控端用户正在用的窗口切到后台（2026-10-13 可见桌面实机测试发现）
    LRESULT OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //建窗（已有窗口时直接返回）
    void ensureWindow();
    //按当前的按钮清单与状态文字重建控件树，并算出整条宽
    void buildUi();
    //把按钮清单的外观、提示、文字装到已建好的按钮上
    void applyButtons();
    //某个按钮的宽（像素）：方形图标按钮为 kShareBarBtnPx，带文字的按文字算
    int buttonWidth(const ViewerBarButtonSpec& spec) const;
    //按当前的参照矩形摆放（参照矩形既是摆放区域，也是隐藏后的感应区域），并刷新收起状态
    void place();
    //两份按钮清单的编号序列与有无文字是否相同（相同则只需原地刷新）
    static bool sameLayout(const std::vector<ViewerBarButtonSpec>& a, const std::vector<ViewerBarButtonSpec>& b);

    HWND                             m_owner;          //所有者窗口（观看窗，借用）
    HWND                             m_notifyWnd;      //按钮点击的通知窗口（借用）
    UINT                             m_notifyMsg;      //通知消息号
    RECT                             m_anchor;         //参照矩形（屏幕坐标），Show / MoveTo 时更新
    std::wstring                     m_caption;        //标题条文字（已翻译）
    std::wstring                     m_status;         //状态文字（已翻译）；为空表示没有状态区
    std::vector<ViewerBarButtonSpec> m_buttons;        //使用方给出的按钮清单（从左到右）
    bool                             m_captureExcluded;//是否排除在屏幕采集之外（2026-10-12，见 SetCaptureExcluded）；窗口重建时沿用
    bool                             m_badgeVisible;   //是否显示「P2P」徽章（2026-10-12，见 SetLinkBadgeVisible）
    HBITMAP                          m_peopleIcon;     //「N 人观看」按钮前的人形图标（本类生成、析构释放）
    balloonwjui::DuiLabel*           m_statusLabel;    //状态文字（控件树持有，本指针仅引用）；没有状态区时为 NULL
};

} // namespace screenshare
