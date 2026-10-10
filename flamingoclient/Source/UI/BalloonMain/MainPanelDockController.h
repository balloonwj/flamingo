/**
 *  主面板贴边自动隐藏控制器：把「贴边自动隐藏」的运行期逻辑集中在一个类里。
 *
 *  做四件事：
 *    · 用户拖动或拉伸主面板松开鼠标时，判断它是否靠在了可贴的屏幕边缘上，是则吸附到
 *      边缘、置顶，进入贴边状态；拖离边缘则退出贴边状态、取消置顶；
 *    · 贴边期间按固定间隔轮询鼠标、鼠标键、菜单与文字输入，交给 CPanelDockPolicy 判定，
 *      再按判定结果把窗口滑出屏幕（收起）或滑回原位（展开）；
 *    · 托盘、快捷键等外部入口唤出主面板时，若已收起则先展开；
 *    · 显示器布局或工作区变化时重新校验贴边位置，原来的边不能再贴时退出贴边状态。
 *
 *  判定规则本身不在本类里，而在 PanelDockGeometry.h 与 PanelDockPolicy.h 中（已有单元
 *  测试覆盖，见 Tests/PanelDockGeometryTests.cpp 与 Tests/PanelDockPolicyTests.cpp）。
 *  本类只负责采集输入、移动窗口与日志。
 *
 *  贴边期间窗口置顶：不置顶的话，别的窗口一最大化就会盖住屏幕边缘那条细条，鼠标碰
 *  不到，主面板就无法再展开。
 *
 *  典型用法：由宿主窗口按值持有，窗口创建之后调一次 Attach，并在自己的消息映射里加一行
 *  CHAIN_MSG_MAP_MEMBER（接管显示器变化消息）；WM_TIMER 由宿主转交，移动 / 缩放结束由
 *  宿主通知。
 *
 *      // 宿主窗口创建之后
 *      m_dockController.Attach(m_hWnd, m_lpFMGClient, this);
 *      // 宿主的 WM_EXITSIZEMOVE 处理里
 *      m_dockController.OnUserMoveSizeEnd();
 *      // 宿主的 WM_TIMER 处理里
 *      if (m_dockController.PumpTimer(nIDEvent))
 *          return;
 *
 *  balloonwj@qq.com   2026-10-02
 */

#pragma once

#include <vector>

#include "PanelDockGeometry.h"   // balloonmain::DockSnapResult / PanelDockEdge
#include "PanelDockPolicy.h"     // balloonmain::CPanelDockPolicy

class CFlamingoClient;

namespace balloonmain {

//贴边期间轮询鼠标与各项条件的定时器标识
const UINT_PTR kDockPollTimerId = 0xD0C1;
//收起 / 展开滑动动画的逐帧定时器标识
const UINT_PTR kDockSlideTimerId = 0xD0C2;

//轮询间隔，单位毫秒。鼠标碰到细条后最多等这么久才开始展开，取 100 毫秒用户感觉不到延迟
const UINT kDockPollIntervalMs = 100;
//滑动动画的总时长，单位毫秒
const DWORD kDockSlideDurationMs = 150;
//滑动动画的帧间隔，单位毫秒。系统定时器的实际精度约 15 毫秒，取 10 即按最快速度出帧
const UINT kDockSlideFrameMs = 10;

/**
 *  贴边控制器向宿主（主面板）打听的两件事。宿主实现本接口，控制器只借用、不持有。
 */
class IPanelDockHost
{
public:
    virtual ~IPanelDockHost() {}

    /**
     *  是否正在主面板上输入文字：搜索结果下拉开着、搜索框拥有焦点且内容不为空，或者个性
     *  签名编辑框开着。此时鼠标离开主面板也不收起，免得用户正打着字主面板就滑走了。
     */
    virtual bool IsDockEditingText() = 0;

    /**
     *  给定的顶层窗口是否是宿主自己弹出、但没有把宿主设为所有者的浮层（如搜索结果下拉）。
     *  鼠标停在这类浮层上时视为仍在主面板上。
     *    hTopLevel：鼠标下方窗口所属的顶层窗口，非 NULL。
     */
    virtual bool IsDockOwnPopup(HWND hTopLevel) = 0;
};

/**
 *  主面板贴边自动隐藏控制器。
 *
 *  生命周期与宿主窗口一致：Attach 之后才工作，Shutdown（或析构）时停掉定时器。
 *  不处于贴边状态时不运行任何定时器。
 */
class CMainPanelDockController
{
public:
    CMainPanelDockController();
    ~CMainPanelDockController();

    /**
     *  绑定宿主。须在宿主窗口创建之后调用一次。
     *    hHost    宿主窗口句柄，本类不拥有。既是被移动的窗口，也是定时器的宿主。
     *    pClient  网络客户端，本类不拥有。只读用户配置里的贴边自动隐藏开关；
     *             可为 NULL，此时按开关关闭处理。
     *    pHost    宿主接口，本类不拥有，不可为 NULL。
     */
    void Attach(HWND hHost, CFlamingoClient* pClient, IPanelDockHost* pHost);

    /**
     *  停掉全部定时器。宿主窗口销毁前调用；可重复调用。不移动窗口。
     */
    void Shutdown();

    /**
     *  把定时器消息交给本类处理。
     *    nIDEvent  WM_TIMER 带来的定时器标识。
     *  @return true 表示这个定时器归本类；false 表示不认识，宿主继续处理。
     */
    bool PumpTimer(UINT_PTR nIDEvent);

    /**
     *  用户开始拖动或拉伸宿主窗口（WM_ENTERSIZEMOVE）。停掉正在进行的滑动动画，
     *  免得动画与用户的拖动同时改窗口位置。
     */
    void OnUserMoveSizeStart();

    /**
     *  用户拖动或拉伸宿主窗口结束（WM_EXITSIZEMOVE）。开关开启且窗口靠在可贴的边缘上时
     *  吸附过去并进入贴边状态，否则退出贴边状态。可能移动窗口、改变置顶状态。
     *  宿主若要记录窗口位置，应在本方法返回之后再取窗口矩形（此时已吸附完毕）。
     */
    void OnUserMoveSizeEnd();

    /**
     *  登录后按保存的位置摆好窗口之后调用：开关开启且窗口靠在可贴的边缘上时直接进入贴边
     *  状态（展开），鼠标进入过主面板再离开后才会收起。
     */
    void DockIfAtEdge();

    /**
     *  托盘、快捷键等外部入口唤出宿主窗口时调用。已收起时把窗口移回吸附位置；
     *  展开状态下只是要求鼠标先进入过主面板才会再收起。未贴边时什么都不做。
     */
    void RevealForExternalShow();

    /**
     *  注销时调用：退出贴边状态，已收起时把窗口移回吸附位置，取消置顶。
     */
    void ResetForLogout();

    /**
     *  是否处于贴边且收起的状态。此时窗口仍是可见窗口（只是滑到了屏幕外），
     *  宿主判断「主面板是否在屏幕上」时要把这种情况算作不在。
     */
    bool IsHiddenAtEdge() const
    {
        return m_policy.IsHidden();
    }

    BEGIN_MSG_MAP_EX(CMainPanelDockController)
        //显示器增减、分辨率变化：重新校验贴边位置
        MESSAGE_HANDLER_EX(WM_DISPLAYCHANGE, OnDisplayChange)
        //任务栏位置或大小变化会改变工作区，同样要重新校验
        MESSAGE_HANDLER_EX(WM_SETTINGCHANGE, OnSettingChange)
    END_MSG_MAP()

private:
    //WM_DISPLAYCHANGE：重新校验贴边位置，消息照常交给宿主后续处理
    LRESULT OnDisplayChange(UINT uMsg, WPARAM wParam, LPARAM lParam);

    //WM_SETTINGCHANGE：工作区变化（SPI_SETWORKAREA）时重新校验贴边位置，消息照常交给宿主后续处理
    LRESULT OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam);

    //执行一次轮询：采集输入、交给状态机、按结果移动窗口
    void Poll();

    //采集一次状态机输入
    PanelDockInput BuildInput();

    //鼠标是否在宿主窗口、宿主所属的窗口或宿主的浮层上
    bool IsCursorOverPanel(POINT pt);

    //当前线程是否有弹出菜单开着（balloonui 菜单或系统原生菜单）
    static bool IsAnyMenuOpen();

    //读取贴边自动隐藏开关；没有网络客户端时按关闭处理
    bool IsFeatureEnabled() const;

    //收集当前所有显示器的矩形与工作区
    static std::vector<DockMonitor> CollectMonitors();

    //按宿主窗口当前的 DPI 把逻辑像素换算成物理像素
    int ScaleByDpi(int logicalPx) const;

    //进入贴边状态：记下吸附结果、把窗口移到吸附位置、置顶、启动轮询定时器。
    //  snap：DetectDockEdge 的结果，m_edge 不为 kPanelDockNone。
    //  pszTrigger：触发来源的英文短标签，只进日志
    void EnterDock(const DockSnapResult& snap, const char* pszTrigger);

    //退出贴边状态：停掉轮询与动画、按需移动窗口、取消置顶。
    //  pszTrigger：  触发来源的英文短标签，只进日志。
    //  pRestoreRect：退出后窗口应在的位置（只取左上角，不改尺寸）；为 NULL 时不移动窗口。
    //                用户把主面板拖离边缘时传 NULL（窗口已在用户放下的位置）；开关被关掉、
    //                注销时传吸附位置（窗口可能正收起在屏幕外，或正在滑动）。
    //                不能由本函数自己根据「是否已收起」决定：开关被关掉时状态机在 Tick 里
    //                已经先退出了贴边状态，此刻再读「是否已收起」得到的总是 false
    void LeaveDock(const char* pszTrigger, const RECT* pRestoreRect);

    //显示器或工作区变化后重新校验贴边位置
    void Revalidate(const char* pszTrigger);

    //把窗口滑到 target（只改位置，不改尺寸）。窗口不可见时直接移过去、不播放动画
    void SlideTo(const RECT& target);

    //立即把窗口移到 target 的位置，停掉正在进行的动画
    void MoveNow(const RECT& target);

    //推进一帧滑动动画
    void StepSlide();

    //停掉滑动动画定时器
    void StopSlide();

    //停掉轮询定时器
    void StopPoll();

    //当前状态下收起后的窗口矩形 / 细条矩形（按 m_snap 与 DPI 计算）
    RECT HiddenRect() const;
    RECT StripRect() const;

    //宿主窗口句柄，借用不拥有；被移动的窗口，也是两个定时器的宿主
    HWND m_hHost;
    //网络客户端，借用不拥有；只读用户配置里的贴边自动隐藏开关
    CFlamingoClient* m_lpFMGClient;
    //宿主接口，借用不拥有；询问是否正在输入文字与宿主自己的浮层
    IPanelDockHost* m_pHost;

    //收起 / 展开状态机
    CPanelDockPolicy m_policy;
    //最近一次吸附的结果：贴在哪条边、展开时窗口的位置、所在显示器的工作区。
    //只在贴边状态下有意义
    DockSnapResult m_snap;

    //轮询定时器是否在运行
    bool m_pollRunning;
    //滑动动画定时器是否在运行
    bool m_slideRunning;
    //滑动动画的起点与终点（窗口矩形，屏幕坐标、物理像素）
    RECT m_slideFrom;
    RECT m_slideTo;
    //滑动动画开始的时刻，GetTickCount() 值，毫秒
    DWORD m_slideStartTick;
};

} // namespace balloonmain
