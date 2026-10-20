/**
 *  共享控制条与录制条的图标按钮，ShareBarButtonArt.h
 *
 *  2026-10-07 起屏幕共享控制条（CShareControlBar）与录制条（CRecordBar）统一成同一套样式（docs/屏幕共享与录制方案.md
 *  第 13 节）：左侧是状态区（状态点或闪烁的录制红点 + 文字），右侧是一排方形图标按钮，原来按钮上的文字改作悬停提示。
 *  按钮仍是 balloonui 的 DuiButton，不子类化、不改 balloonui，做法与通话窗的 CallButtonArt 相同：本文件用 GDI+ 预先画好
 *  常态 / 悬停 / 按下 / 禁用四张 32 位预乘透明度的圆角方形位图，经 DuiButton::SetBgBitmap 贴上去，清空按钮文字，原文字
 *  登记为悬停提示（DuiToolTipMgr）。开关类按钮（麦克风、电脑声音、字幕、摄像头、标注、录制）打开时为高亮底色（录制为红底），
 *  麦克风、电脑声音、字幕、摄像头关着时图标上加一道斜杠。
 *
 *  分三层：
 *    - ShareBarLookFor / ShareBarTooltipFor / ShareRecordDotState：纯函数，单元测试直接调用
 *      （Source/Tests/ShareBarArtTests.cpp，标签 [sharebarart]）；
 *    - MakeShareBarButtonBitmap：按外观与交互状态画一张位图；
 *    - ShareBarButtonArtCache：按外观缓存四态位图并装到按钮上。DuiButton 不持有位图，缓存须比用到它的按钮活得久，
 *      一般作为窗口的成员变量。
 *  图标取自 ChatIcons（麦克风、电脑声音、字幕、摄像机、三个点、共享屏幕）与 MainPanelIcons（人形），其余（暂停、继续、
 *  停止、录制、画笔、鼠标、收起、固定，以及 2026-10-12 为浮动观看工具条新增的白板、共享说明、皇冠、全屏、退出全屏、
 *  离开）由本文件按同样的线性风格画。申请类按钮在等待对方同意期间为橙底（share_bar_tone_pending）。
 *
 *  典型用法：
 *      m_art.Apply(button, ShareBarLookFor(share_bar_mic, micOn), ShareBarTooltipFor(share_bar_mic, micOn));
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <windows.h>

namespace balloonwjui
{
    class DuiButton;
}

namespace screenshare {

//方形图标按钮的边长（像素）：控制条内容横条高 46，上下各留 8
const int kShareBarBtnPx = 30;
//相邻两个图标按钮之间的间距（像素）
const int kShareBarBtnGap = 6;
//红色文字按钮「收回控制」的最小宽（像素），译文较长时按文字加宽；主持人控制条与共享者控制条共用（第 18 期）
const int kShareRevokeBtnMinW = 88;
//录制红点闪烁的半周期（毫秒）：亮 500 毫秒、暗 500 毫秒
const int kShareRecordBlinkMs = 500;

//顶部浮动条（主持人控制条、共享者控制条、观看端浮动条，见 ShareTopBar.h）共用的尺寸与颜色（2026-10-14 起集中在这里）：
//标题条高、内容横条高（像素）
const int kShareTopBarTitleH = 18;
const int kShareTopBarContentH = 46;
//内容横条的左右内边距（像素）
const int kShareTopBarRowPadLeft = 14;
const int kShareTopBarRowPadRight = 10;
//按钮上下留白（像素），让 kShareBarBtnPx 的按钮在内容横条里上下居中
const int kShareTopBarBtnVMargin = (kShareTopBarContentH - kShareBarBtnPx) / 2;
//工具条顶边距摆放区域顶边的距离（像素）
const int kShareTopBarTopMarginPx = 8;
//状态点（文字「●」）所占的宽（像素）
const int kShareTopBarDotW = 14;
//条的底色与文字色
const COLORREF kShareTopBarBgColor = RGB(44, 48, 56);
const COLORREF kShareTopBarTextColor = RGB(255, 255, 255);

//按钮的种类（决定图标与提示文字）
enum ShareBarAction
{
    share_bar_pause = 0,          //暂停 / 继续（on 表示已暂停，此时图标为「继续」）
    share_bar_stop_record = 1,    //录制条的「停止」：红底方块
    share_bar_stop_share = 2,     //共享控制条的「结束共享」：红底方块
    share_bar_mic = 3,            //麦克风开关（on 表示打开）；共享控制条与录制条的提示文字不同，见 ShareBarTooltipFor
    share_bar_system_audio = 4,   //电脑声音开关（on 表示打开）
    share_bar_caption = 5,        //字幕开关（on 表示打开）
    share_bar_camera = 6,         //摄像头开关（on 表示打开）
    share_bar_annotate = 7,       //标注（on 表示在标注模式）；录制条上是「画笔」
    share_bar_record = 8,         //录制这场共享（on 表示在录）
    share_bar_mouse = 9,          //鼠标效果（点开菜单，图标右下角带下拉三角）
    share_bar_more = 10,          //更多（点开菜单）
    share_bar_switch = 11,        //切换共享内容
    share_bar_viewers = 12,       //观看者名单（只画人形，人数由按钮文字给出）
    share_bar_collapse = 13,      //收起 / 固定（on 表示已收起或临时展开，此时图标为「固定」）
    share_bar_hide = 14,          //隐藏工具条（2026-10-07，方案第 14.2 节）：工具条与把手都不显示，鼠标到屏幕边缘时重新出现
    //以下为浮动观看工具条（CViewerFloatBar，2026-10-12）新增；三态按钮用 ShareBarLookForTriState 取外观
    share_bar_control = 15,       //申请控制（三态：未申请 / 申请中 / 控制中），屏幕共享观看者与远程桌面控制端都用
    share_bar_share_mine = 16,    //共享我的屏幕（三态：未申请 / 申请中 / 共享中），屏幕共享观看者用
    share_bar_board = 17,         //新建白板（三态：未申请 / 申请中 / 白板开着），屏幕共享观看者用
    share_bar_info = 18,          //共享说明（点开说明浮窗），共享有描述时才有
    share_bar_host = 19,          //主持人菜单（点开菜单，带下拉三角），经移交成为主持人时才有
    share_bar_fullscreen = 20,    //全屏 / 退出全屏（on 表示正在全屏，此时图标为「退出全屏」）
    share_bar_leave = 21,         //离开共享 / 断开远程桌面：红底
    share_bar_action_count = 22,  //种类个数（不是一种按钮）
};

//三态按钮的状态（ShareBarLookForTriState 的 state）：申请类按钮在发出申请、等对方处理期间显示橙底
enum ShareBarTriState
{
    share_bar_tri_off = 0,       //没有申请：常态灰底
    share_bar_tri_pending = 1,   //已申请、等待对方同意：橙底
    share_bar_tri_on = 2,        //已生效（在控制、在共享、白板开着）：蓝底
};

//按钮配色
enum ShareBarTone
{
    share_bar_tone_normal = 0,   //常态：比条底色略亮的灰底、浅色图标
    share_bar_tone_active = 1,   //打开的开关：蓝底、白色图标
    share_bar_tone_danger = 2,   //结束、停止、正在录制：红底、白色图标
    share_bar_tone_pending = 3,  //已申请、等待对方同意（2026-10-12）：橙底、白色图标
};

//按钮的交互状态，对应 DuiButton::SetBgBitmap 的四个槽位
enum ShareBarButtonState
{
    share_bar_btn_normal   = 0,   //常态
    share_bar_btn_hover    = 1,   //鼠标悬停
    share_bar_btn_pressed  = 2,   //鼠标按下未松开
    share_bar_btn_disabled = 3,   //禁用
    share_bar_btn_state_count = 4,   //状态个数（数组长度用，不是一种状态）
};

//图标（ShareBarButtonLook::m_icon）
enum ShareBarIcon
{
    share_bar_icon_none = 0,      //不画图标（观看者按钮的底图，人形另作前置图标）
    share_bar_icon_pause = 1,     //两道竖杠
    share_bar_icon_play = 2,      //向右的实心三角（继续）
    share_bar_icon_stop = 3,      //实心圆角方块
    share_bar_icon_mic = 4,       //麦克风（ChatIcons）
    share_bar_icon_speaker = 5,   //显示器里一个音符（ChatIcons 的电脑声音）
    share_bar_icon_caption = 6,   //字幕框（ChatIcons）
    share_bar_icon_camera = 7,    //摄像机（ChatIcons）
    share_bar_icon_pen = 8,       //斜放的铅笔
    share_bar_icon_record = 9,    //圆环 + 中间实心圆点
    share_bar_icon_mouse = 10,    //鼠标箭头
    share_bar_icon_more = 11,     //三个点（ChatIcons）
    share_bar_icon_switch = 12,   //显示器 + 向上箭头（ChatIcons 的共享屏幕）
    share_bar_icon_people = 13,   //人形（MainPanelIcons）
    share_bar_icon_collapse = 14, //向上的尖括号（收起）
    share_bar_icon_pin = 15,      //向下的尖括号（固定，展开后不再收起）
    share_bar_icon_pushpin = 16,  //图钉（ChatIcons；标注工具条临时展开时的「固定工具条」）
    share_bar_icon_hide = 17,     //眼睛轮廓加一道斜线（隐藏工具条，2026-10-07）
    share_bar_icon_board = 18,    //一块白板加一支斜放的笔（新建白板，2026-10-12）
    share_bar_icon_info = 19,     //圆圈里一个 i（共享说明）
    share_bar_icon_crown = 20,    //皇冠（主持人菜单）
    share_bar_icon_fullscreen = 21,      //四角向外的折线（全屏）
    share_bar_icon_exit_fullscreen = 22, //四角向内的折线（退出全屏）
    share_bar_icon_leave = 23,    //半开的门框加一支向右的箭头（离开 / 断开）
};

//一个图标按钮的外观
struct ShareBarButtonLook
{
    int  m_icon;      //图标，取值见 ShareBarIcon
    bool m_slash;     //是否在图标上画一道斜杠（开关关着）
    bool m_dropMark;  //是否在右下角画下拉小三角（点开是菜单）
    int  m_tone;      //配色，取值见 ShareBarTone

    ShareBarButtonLook();
};

/**
 *  由按钮种类与状态算出外观（纯函数）。
 *  @param action 按钮种类，取值见 ShareBarAction
 *  @param on     开关状态，含义见 ShareBarAction 各值的注释；不是开关的按钮忽略
 *  @return 外观；未知种类返回常态、不画图标
 */
ShareBarButtonLook ShareBarLookFor(int action, bool on);

/**
 *  三态按钮（申请控制、共享我的屏幕、新建白板）按状态算出外观（纯函数，2026-10-12）：图标不变，常态灰底、申请中橙底、
 *  已生效蓝底。其余种类的申请中按关着处理，已生效等同 ShareBarLookFor(action, true)。
 *  @param action 按钮种类，取值见 ShareBarAction
 *  @param state  取值见 ShareBarTriState；越界按 share_bar_tri_off 处理
 *  @return 外观
 */
ShareBarButtonLook ShareBarLookForTriState(int action, int state);

/**
 *  按钮的悬停提示文字（已经过 TR 翻译），就是改版前按钮上的文字（纯函数）。
 *  @param action    按钮种类
 *  @param on        开关状态
 *  @param recordBar 是否是录制条（录制条与共享控制条上同一种开关的原文字不同，如「麦克风：开」与「关闭麦克风」）
 *  @return 提示文字；未知种类返回空串
 */
std::wstring ShareBarTooltipFor(int action, bool on, bool recordBar);

/**
 *  浮动观看工具条（CViewerFloatBar）上按钮的默认悬停提示，按三态取（纯函数，2026-10-14）。三态按钮在申请中时提示撤回：
 *  申请控制为「取消申请」、共享我的屏幕为「取消共享申请」、新建白板为「取消白板申请」；其余情况等同
 *  ShareBarTooltipFor(action, state == share_bar_tri_on, false)。屏幕共享观看窗与远程桌面观看窗的按钮共用这一份文字。
 *  @param action 按钮种类，取值见 ShareBarAction
 *  @param state  取值见 ShareBarTriState
 *  @return 提示文字（已经过 TR 翻译）；未知种类返回空串
 */
std::wstring ShareBarTooltipForTriState(int action, int state);

//录制红点（闪烁徽章）的状态
enum ShareRecordDotState
{
    share_record_dot_hidden = 0,   //没有在录：不显示
    share_record_dot_bright = 1,   //在录、闪烁的亮相：红色
    share_record_dot_dim    = 2,   //在录、闪烁的暗相：暗红色
    share_record_dot_paused = 3,   //暂停中：灰色、不闪
};

/**
 *  录制红点在某一时刻的状态（纯函数）：在录时每 kShareRecordBlinkMs 毫秒亮暗交替，暂停时灰色不闪。
 *  @param recording 是否在录
 *  @param paused    是否暂停
 *  @param nowMs     当前时刻（毫秒，任意起点，如 GetTickCount64）
 *  @return 取值见 ShareRecordDotState
 */
int ShareRecordDotStateAt(bool recording, bool paused, int64_t nowMs);

/**
 *  录制红点各状态的文字颜色（纯函数）。
 *  @param state 取值见 ShareRecordDotState
 *  @param barBg 条的底色：隐藏时返回它，红点与底色融为一体
 */
COLORREF ShareRecordDotColor(int state, COLORREF barBg);

/**
 *  画一张方形图标按钮位图：32 位、自上而下、预乘透明度，圆角外完全透明。GDI+ 由本函数按需初始化。
 *  @param look  外观
 *  @param state 交互状态，取值见 ShareBarButtonState
 *  @param size  边长（像素）
 *  @return 位图，所有权交给调用方（用完 DeleteObject）；创建失败返回 NULL
 */
HBITMAP MakeShareBarButtonBitmap(const ShareBarButtonLook& look, int state, int size);

/**
 *  画一张单独的图标位图（透明底，预乘透明度），用作按钮的前置图标（观看者按钮的人形）。
 *  @param icon  图标，取值见 ShareBarIcon
 *  @param size  边长（像素）
 *  @param color 图标颜色
 *  @return 位图，所有权交给调用方；创建失败返回 NULL
 */
HBITMAP MakeShareBarIconBitmap(int icon, int size, COLORREF color);

/**
 *  画一张不透明的纯色小位图（2026-10-12），用作工具条窗口的背景（DuiFrameWindow::SetBgImage 九宫格拉伸铺满）。
 *  共享控制条、共享者控制条、标注工具条与浮动观看工具条都用这一个（2026-10-14 起，原来各写一份同样的函数）；
 *  录制条另有一份可指定边长的同类函数，它的小把手红点图标要画在任意边长的底图上。
 *  @param color 颜色
 *  @return 位图，所有权交给调用方（用完 DeleteObject）；创建失败返回 NULL
 */
HBITMAP MakeShareBarBackgroundBitmap(COLORREF color);

/**
 *  按外观缓存四态位图，并把位图与提示文字装到 DuiButton 上。同一外观只画一次，之后复用。只在 UI 线程上使用。
 */
class ShareBarButtonArtCache
{
public:
    ShareBarButtonArtCache();

    //析构：释放缓存的全部位图。此时用到这些位图的按钮须已销毁
    ~ShareBarButtonArtCache();

    /**
     *  把外观对应的四态位图装到按钮上，清空按钮文字（keepText 为 true 时保留），登记悬停提示，让按钮不接收焦点。
     *  @param button   目标按钮（借用，不拥有）；为 NULL 时什么也不做
     *  @param look     外观
     *  @param tooltip  悬停提示文字；为空串时撤掉提示
     *  @param keepText 是否保留按钮文字（观看者按钮在底图上写人数）
     *  @return 位图都已就绪并装上时返回 true；位图创建失败时返回 false，按钮保持原样（仍显示原文字，不至于变成空白）
     */
    bool Apply(balloonwjui::DuiButton* button, const ShareBarButtonLook& look, const std::wstring& tooltip,
               bool keepText = false);

    //已缓存的外观个数（单元测试用）
    size_t LookCount() const;

private:
    ShareBarButtonArtCache(const ShareBarButtonArtCache&);
    ShareBarButtonArtCache& operator=(const ShareBarButtonArtCache&);

    //一种外观的四态位图
    struct StateBitmaps
    {
        HBITMAP m_states[share_bar_btn_state_count];   //按 ShareBarButtonState 下标存放，归本缓存所有

        StateBitmaps();
    };

    //把外观编成缓存的键
    static int keyOf(const ShareBarButtonLook& look);

    std::map<int, StateBitmaps> m_bitmaps;   //外观键 → 四态位图（位图归本对象所有，析构时释放）
};

} // namespace screenshare
