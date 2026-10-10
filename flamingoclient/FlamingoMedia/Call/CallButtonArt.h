/**
 *  通话窗圆形图标按钮的外观与位图，CallButtonArt.h
 *
 *  2026-10-05 起通话窗（呼叫中 / 接通态）与来电弹窗的按钮由文字按钮改为圆形图标按钮。按钮本身仍是 balloonui 的
 *  DuiButton，不子类化、不改 balloonui：本文件用 GDI+ 预先画好常态 / 悬停 / 按下 / 禁用四张 32 位预乘透明度的圆形位图，
 *  经 DuiButton::SetBgBitmap 贴上去，按钮文字清空，原来的文字改作悬停提示（DuiToolTipMgr）。
 *
 *  分三层：
 *    - LookForAction / TooltipForAction：纯函数，由按钮种类（CallActionId）、激活态、是否视频态算出图标、斜杠、配色、
 *      直径与提示文字。不碰 GDI，单元测试直接调用（Source/Tests/CallButtonArtTests.cpp，标签 [callbtn]）。
 *    - MakeCallButtonBitmap：按外观与交互状态画一张位图（GDI+ 由它按需初始化）。
 *    - CallButtonArtCache：按外观缓存四态位图并把它们装到按钮上。DuiButton 不持有位图，缓存须比用到它的按钮活得久，
 *      一般作为窗口的成员变量，声明在全部控件指针之后。
 *
 *  图标取自 ChatIcons（麦克风、喇叭、摄像机、听筒、共享屏幕），「已关闭」的斜杠由本文件另画，不改 ChatIcons。
 *
 *  典型用法：
 *      m_buttonArt.Apply(button, call::LookForAction(call::kActionMute, muted, video),
 *                        call::TooltipForAction(call::kActionMute, muted, video));
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <map>
#include <string>

#include <windows.h>

#include "CallActionBarModel.h"   //CallActionId

namespace balloonwjui {
class DuiButton;
}

namespace call {

//普通圆形按钮的直径（像素）：麦克风、音量、摄像头、切换媒体、共享屏幕，以及来电弹窗的拒绝、接听
const int kCallRoundBtnPx = 48;
//挂断与取消的直径（像素）：比普通按钮大一圈，确立主次
const int kCallRoundBtnLargePx = 56;
//底部栏相邻两个圆形按钮之间的间距（像素）
const int kCallRoundBtnGap = 12;

//按钮配色
enum CallButtonTone
{
    call_tone_normal = 0,   //常态：浅灰底、深灰图标（麦克风开着、摄像头开着、切换媒体、音量、未在共享）
    call_tone_active = 1,   //激活态：深灰底、白色图标（麦克风已关、摄像头已关、正在共享），让用户一眼看出当前不是常态
    call_tone_danger = 2,   //危险操作：红底、白色图标（挂断、取消、拒绝）
    call_tone_accept = 3,   //接听：绿底、白色图标
};

//按钮的交互状态，对应 DuiButton::SetBgBitmap 的四个槽位
enum CallButtonState
{
    call_btn_normal   = 0,   //常态
    call_btn_hover    = 1,   //鼠标悬停
    call_btn_pressed  = 2,   //鼠标按下未松开
    call_btn_disabled = 3,   //禁用
    call_btn_state_count = 4,   //状态个数（数组长度用，不是一种状态）
};

//一个圆形图标按钮的外观
struct CallButtonLook
{
    int  m_icon;         //图标，取 balloonchatwnd::ChatIconKind；-1 表示不画图标（未知的按钮种类）
    bool m_slash;        //是否在图标上画一道斜杠（「已关闭」）
    bool m_hangupTilt;   //听筒是否旋转成水平放下的姿态（挂断 / 取消 / 拒绝的通用画法）
    int  m_tone;         //配色，取 CallButtonTone
    int  m_diameter;     //直径（像素）

    CallButtonLook();
};

/**
 *  由按钮种类与状态算出外观。
 *  @param actionId 按钮种类，取 CallActionId
 *  @param active   激活态，含义随按钮而定：静音键为「麦克风已关」，摄像头键为「摄像头已关」，共享键为「正在共享」，
 *                  音量键为「音量已调到 0」；其余按钮忽略
 *  @param video    当前是否为视频通话：只影响切换媒体键（语音态画摄像机表示「切到视频」，视频态画听筒表示「切回语音」）
 *  @return 外观；未知的按钮种类返回常态配色、不画图标的普通按钮
 */
CallButtonLook LookForAction(int actionId, bool active, bool video);

/**
 *  按钮的悬停提示文字（已经过 TR 翻译）。参数含义同 LookForAction。
 *  @return 提示文字；未知的按钮种类返回空串
 */
std::wstring TooltipForAction(int actionId, bool active, bool video);

/**
 *  打开共享电脑声音失败时，通话窗顶部提示条上的文字（2026-10-06，已经过 TR 翻译）。
 *  @param result CallMediaSession::SetSystemAudioEnabled 的返回值，取 CallSystemAudioResult
 *  @return 提示文字；成功（call_sysaudio_ok）或未知取值时返回空串
 */
std::wstring SystemAudioFailureText(int result);

/**
 *  对方开始或停止共享电脑声音时，通话窗顶部提示条上的文字（2026-10-06，已经过 TR 翻译）。
 *  @param sharing true 为对方开始共享，false 为对方停止共享
 */
std::wstring PeerSystemAudioText(bool sharing);

/**
 *  画一张圆形图标按钮位图：32 位、自上而下、预乘透明度，圆外完全透明。GDI+ 由本函数按需初始化。
 *  @param look  外观
 *  @param state 交互状态，取 CallButtonState
 *  @return 位图，所有权交给调用方（用完 DeleteObject）；创建失败返回 NULL
 */
HBITMAP MakeCallButtonBitmap(const CallButtonLook& look, int state);

/**
 *  按外观缓存四态位图，并把位图与提示文字装到 DuiButton 上。同一外观只画一次，之后复用。
 *  线程：只在 UI 线程上使用。
 */
class CallButtonArtCache
{
public:
    CallButtonArtCache();

    //析构：释放缓存的全部位图。此时用到这些位图的按钮须已销毁
    ~CallButtonArtCache();

    /**
     *  把外观对应的四态位图装到按钮上，清空按钮文字，登记悬停提示，并让按钮不再接收焦点（否则点击后会在圆形按钮外
     *  画方形虚线焦点框）。
     *  @param button  目标按钮（借用，不拥有）；为 NULL 时什么也不做
     *  @param look    外观
     *  @param tooltip 悬停提示文字；为空串时撤掉提示
     *  @return 位图都已就绪并装上时返回 true；位图创建失败时返回 false，按钮保持原样（仍显示原文字，不至于变成空白）
     */
    bool Apply(balloonwjui::DuiButton* button, const CallButtonLook& look, const std::wstring& tooltip);

    //已缓存的外观个数（单元测试用）
    size_t LookCount() const { return m_bitmaps.size(); }

private:
    CallButtonArtCache(const CallButtonArtCache&);
    CallButtonArtCache& operator=(const CallButtonArtCache&);

    //一种外观的四态位图
    struct StateBitmaps
    {
        HBITMAP m_states[call_btn_state_count];   //按 CallButtonState 下标存放，归本缓存所有

        StateBitmaps();
    };

    //把外观编成缓存的键
    static int KeyOf(const CallButtonLook& look);

private:
    std::map<int, StateBitmaps> m_bitmaps;   //外观键 → 四态位图（位图归本对象所有，析构时释放）
};

} // namespace call
