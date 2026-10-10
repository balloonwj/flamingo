/**
 *  SubtitleWindow.h
 *
 *  聊天字幕窗口：一个置顶、不进任务栏、不抢焦点、没有所有者窗口的弹出窗口，逐像素透明地显示一条
 *  字幕（外观见 SubtitleRenderer.h，交互见 SubtitleState.h）。实现 ISubtitleView，由 CSubtitleSession
 *  建立和销毁。
 *
 *  为什么不用 balloonui：balloonui 的窗口都是不透明的普通窗口、在 BeginPaint 里绘制，做不到逐像素透明、
 *  整窗点击穿透与描边大字。用户批准了本窗口自行绘制（docs/聊天字幕模式方案.md 第 3 节第 1 条），
 *  它只负责画字幕与工具按钮、处理拖动与锁定；设置菜单仍用 balloonui 的 DuiMenu。
 *
 *  显示方式：优先建成「不要重定向位图」的分层窗口，内容经 graphics::CCompositionLayer（DirectComposition）
 *  合成，这样「共享屏幕或录屏时隐藏字幕」打开时可以用 WDA_EXCLUDEFROMCAPTURE 排除在采集之外；系统不支持
 *  DirectComposition（Windows 7）或建不出设备时，退回 UpdateLayeredWindow，此时不能排除采集，设置菜单里
 *  这一项置灰。
 *
 *  鼠标：锁定时窗口带 WS_EX_TRANSPARENT，收不到鼠标消息，悬停由定时器查询光标位置判断；光标落在浮出的
 *  小锁按钮上时暂时去掉 WS_EX_TRANSPARENT，让那次点击落到按钮上。解锁时按住空白或文字处拖动，按住左右
 *  边缘调宽，拖动与调宽都限制在工作区内，结束后报告给会话存盘。工具按钮在松开鼠标后才执行，且经私有
 *  消息 kMsgSubtitleCommand 延后到当前鼠标消息处理完之后（弹出菜单、销毁窗口都不在鼠标消息的调用栈里）。
 *
 *  窗口私有消息：WM_APP + 0xE0（kMsgSubtitleCommand），已登记在开发约定文档的 UI 约定第 6 条。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "../../Common/Graphics/CompositionLayer.h"
#include "SubtitleLayout.h"
#include "SubtitleModel.h"
#include "SubtitleRenderer.h"
#include "SubtitleState.h"
#include "SubtitleTextFit.h"
#include "SubtitleView.h"

namespace subtitle {

//窗口私有消息：执行工具按钮命令（wParam 为按钮部位，取值见 SubtitlePart）。按钮在鼠标松开时投递给自己，
//等当前鼠标消息处理完再执行，弹出设置菜单、退出字幕模式时销毁窗口都不会发生在鼠标消息的调用栈里
const UINT kMsgSubtitleCommand = WM_APP + 0xE0;

//悬停检测与渐变的定时器：渐变或等待计时期间用短间隔，平时用长间隔只查询光标位置
const UINT_PTR kSubtitleTimerId = 1;
const UINT kSubtitleFastTickMs = 30;
const UINT kSubtitleSlowTickMs = 120;

//排除在屏幕采集之外的显示亲和性取值（Windows 10 2004 起；旧 SDK 不定义 WDA_EXCLUDEFROMCAPTURE）
const DWORD kDisplayAffinityExclude = 0x00000011;

/**
 *  CSubtitleWindow —— 一条字幕的窗口。只在界面线程上使用；不可复制。
 */
class CSubtitleWindow : public ISubtitleView
{
public:
    CSubtitleWindow();

    //析构时销毁窗口（若还在）
    virtual ~CSubtitleWindow();

    //---- 以下为 ISubtitleView 的实现 ----

    /**
     *  建出窗口：按保存的位置或默认位置放置，显示初始内容，不抢焦点。
     *    init：输入（见 SubtitleViewInit）。
     *  返回：成功返回 true；窗口类注册或窗口创建失败返回 false。
     */
    virtual bool Create(const SubtitleViewInit& init) override;

    //销毁窗口与显示层；可重复调用
    virtual void Destroy() override;

    /**
     *  换成新的内容：重新折行并重画。
     *    line：内容。
     */
    virtual void SetContent(const SubtitleLine& line) override;

    /**
     *  更换工具条上的会话名并重画。
     *    title：会话名。
     */
    virtual void SetTitle(const std::wstring& title) override;

    /**
     *  换成新的外观：按新字号与行数重算尺寸（底边不动）、重画，并重新设置是否排除在采集之外。
     *    ap：外观。
     */
    virtual void SetAppearance(const SubtitleAppearance& ap) override;

    //窗口矩形，屏幕坐标；窗口不存在时为空矩形
    virtual RECT GetRect() const override;

    //是否锁定
    virtual bool IsLocked() const override;

    //临时隐藏或恢复显示（客户端锁屏期间），见 ISubtitleView::SetHidden
    virtual void SetHidden(bool hidden) override;

    //---- 以下供诊断与单元测试使用 ----

    //窗口句柄；没有窗口时为 NULL
    HWND GetHwnd() const;

    //是否以 DirectComposition 方式显示（false 表示退回 UpdateLayeredWindow，或窗口不存在）
    bool IsCompositionMode() const;

    //「共享屏幕或录屏时隐藏字幕」能否生效（以 DirectComposition 方式显示时才能）
    bool IsCaptureExclusionSupported() const;

    /**
     *  以程序方式锁定或解锁（单元测试与键盘以外的入口用）；与用户点锁定按钮效果相同，但不通知会话。
     *    locked：新的锁定状态。
     */
    void SetLockedForTest(bool locked);

private:
    CSubtitleWindow(const CSubtitleWindow&);
    CSubtitleWindow& operator=(const CSubtitleWindow&);

    //窗口过程：从 GWLP_USERDATA 取出对象后转给 handleMessage
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    //注册窗口类（进程内一次）
    static bool registerClass();

    //处理一条窗口消息
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    /**
     *  建出原生窗口。
     *    composition：true 时带 WS_EX_NOREDIRECTIONBITMAP（DirectComposition 方式）。
     *    rc：         窗口矩形，屏幕坐标。
     *  返回：成功返回 true。
     */
    bool createNative(bool composition, const RECT& rc);

    //按当前矩形所在显示器的 DPI 与外观重算各部分尺寸、工具条与折行
    void relayout();

    //按当前状态绘制并提交给显示层
    void render();

    //定时器：查询光标、推进渐变、按需切换穿透与重画
    void onTimer();

    /**
     *  报告光标位置并推进状态；样子有变化时重画。
     *    forceRender：为 true 时无论样子有没有变化都重画。
     */
    void updateVisual(bool forceRender);

    //设置或去掉 WS_EX_TRANSPARENT（与当前相同时不动）
    void setClickThrough(bool on);

    //按渐变是否进行中调整定时器间隔
    void ensureTimerRate(bool fast);

    //鼠标按下、移动、松开
    void onMouseDown(POINT pt);
    void onMouseMove(POINT pt);
    void onMouseUp(POINT pt);

    //结束拖动或调宽：限制到工作区并报告会话
    void finishDrag();

    /**
     *  执行工具按钮命令（kMsgSubtitleCommand 的处理）。
     *    part：按钮部位，取值见 SubtitlePart。
     */
    void runCommand(int part);

    //弹出设置菜单，用户选了某项时把新外观报告给会话
    void showSettingsMenu();

    //按当前外观设置显示亲和性（是否排除在屏幕采集之外）
    void applyCaptureAffinity();

    //切换锁定状态（不通知会话），并按新状态调整穿透与重画
    void applyLocked(bool locked);

    //建立或更新工具按钮的悬停提示
    void updateTooltips();

    //字幕当前所在显示器的 DPI
    int currentDpi() const;

    /**
     *  取各显示器的工作区与主显示器的工作区。
     *    out：     [出参] 各显示器的工作区。
     *    primary： [出参] 主显示器的工作区。
     */
    static void collectWorkAreas(std::vector<RECT>& out, RECT& primary);

    /**
     *  光标位置（屏幕坐标）对应的部位。
     *    screenPt：光标的屏幕坐标。
     *  返回：取值见 SubtitlePart。
     */
    int hitTestScreen(POINT screenPt) const;

    HWND                       m_hwnd;            //字幕窗口；Create 后非 NULL，Destroy 后为 NULL
    HWND                       m_tooltip;         //工具按钮的悬停提示窗口；建不出来时为 NULL
    ISubtitleViewHost*         m_host;            //用户操作的接收方；不拥有
    graphics::CCompositionLayer m_composition;    //DirectComposition 显示层；退回 UpdateLayeredWindow 时未 Attach
    bool                       m_compositionMode; //是否以 DirectComposition 方式显示
    std::wstring               m_title;           //会话名
    SubtitleAppearance         m_appearance;      //外观
    SubtitleLine               m_line;            //当前内容
    CSubtitleState             m_state;           //交互状态
    SubtitleVisual             m_visual;          //最近一次呈现的样子
    SubtitleMetrics            m_metrics;         //按当前 DPI 与字号算出的尺寸
    SubtitleToolbar            m_toolbar;         //工具条各部分的位置
    FitResult                  m_fit;             //当前内容折行的结果
    int                        m_titleWidth;      //会话名文字的实际宽度，像素
    std::vector<uint8_t>       m_bits;            //绘制用的预乘透明度位图，大小随窗口变化
    bool                       m_clickThrough;    //窗口当前是否带 WS_EX_TRANSPARENT
    bool                       m_fastTimer;       //定时器当前是否为短间隔
    int                        m_hoverPart;       //光标悬停的部位，取值见 SubtitlePart
    int                        m_pressedPart;     //鼠标按下时所在的按钮部位；没有按在按钮上时为 kPartNone
    int                        m_dragMode;        //正在进行的拖动：0 无、kPartBody 移动、kPartLeftEdge / kPartRightEdge 调宽
    POINT                      m_dragStartCursor; //拖动开始时的光标屏幕坐标
    RECT                       m_dragStartRect;   //拖动开始时的窗口矩形
    bool                       m_tooltipsActive;  //悬停提示当前是否启用（按钮可点时才启用）
};

/**
 *  CSubtitleSession 的默认视图工厂：新建一个 CSubtitleWindow（还没有 Create）。
 *    ctx：未使用。
 *  返回：新对象，所有权归调用方。
 */
ISubtitleView* CreateSubtitleWindowView(void* ctx);

/**
 *  ⚙ 菜单关闭后，是否把前台交还给弹菜单前的那个窗口。纯函数。
 *
 *  弹菜单前字幕窗口要先 SetForegroundWindow 自己，菜单在点到外面时才能正常关闭；不交还的话，用户原来在用的
 *  程序会一直失去键盘焦点（用户 2026-10-05 确认要交还）。只有菜单关闭后前台仍是字幕自己（选了菜单项、按了 Esc、
 *  点在字幕上关闭菜单）或暂时没有前台窗口时才交还；用户点了别的窗口来关闭菜单时，前台已经是那个窗口，不再改动。
 *    before：      弹菜单前的前台窗口，可为 NULL。
 *    beforeAlive： before 此刻是否仍是有效窗口（调用方用 IsWindow 判断）。
 *    now：         菜单关闭后此刻的前台窗口，可为 NULL。
 *    self：        字幕窗口自己。
 *  返回：true 表示应当把前台交还给 before。
 */
bool ShouldRestoreForegroundAfterMenu(HWND before, bool beforeAlive, HWND now, HWND self);

} // namespace subtitle
