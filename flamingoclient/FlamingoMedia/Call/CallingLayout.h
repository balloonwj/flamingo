/**
 *  一对一通话窗「呼叫中」态的控件树与通话窗共用的尺寸，CallingLayout.h
 *
 *  2026-10-05 用户要求：主叫方发起通话后直接打开接通后的那个通话窗（当时为 720×569，2026-10-06 加「共享电脑声音」按钮后
 *  默认宽度改为 776；标题栏同样有图钉、最小化、最大化、关闭），
 *  只是显示状态不同。所以呼叫中态与接通态用同一副骨架：上方是语音态那种画面区（画对方头像，下面一行
 *  「正在呼叫，等待对方接听…」），下方是同样高度的底部栏，只在右侧放一个「取消」。视频通话呼叫时同样画对方头像，
 *  接通后才打开摄像头。对方接听后窗口原地换成接通态的控件树，尺寸与位置都不变。
 *
 *  画面区的头像与状态文字由通话窗（CBalloonCallDlg::ApplyCanvasPlaceholders）贴上，本文件只负责搭控件树。
 *  布局单测见 Source/Tests/CallUxTests.cpp 的 CLY1（按默认尺寸排得下、画面区与接通后的语音态位置一致、最大化后
 *  「取消」仍贴右下）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法：
 *      call::CallingBody body = call::BuildCallingBody(kIdCanvasBig, kIdBtnRight, m_buttonArt);
 *      m_bigCanvas = body.m_canvas;
 *      SetClientContent(std::move(body.m_root));
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <memory>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace call {

class CallVideoCanvas;
class CallButtonArtCache;

//---- 一对一通话窗的尺寸（逻辑像素），呼叫中态与接通态共用 ----
//标题栏高（来电态另用 flamingomedia::kIncomingTitleBarH）
const int kCallDlgTitleBarH = 44;
//默认整窗宽：2026-10-06 由 720 改为 776，视频态底部栏多了「共享电脑声音」按钮后最宽需要 776（见 BalloonCallDlg.cpp 的
//kVideoMinClientW），用户选定把默认宽度一并加大，接通时窗口不会被最小宽度撑大
const int kCallDlgWidth = 776;
//默认画面区高（原按 720×405≈16:9 取值，宽度加到 776 后高度不变）；整窗默认高 = 画面区 + 底部栏 + 标题栏
const int kCallCanvasH = 405;
//底部栏高（视频态放小画面 160×90 与几个按钮）
const int kCallBottomBarH = 120;
//视频态底部栏通话时长标签的高度（原为底部栏文字按钮的高度；2026-10-05 起按钮改为圆形图标按钮，尺寸见 CallButtonArt.h）
const int kCallBtnH = 32;
//底部栏左右内边距（按钮与窗口边缘之间的留白）
const int kCallSideMargin = 24;
//语音态与呼叫中态的画面区与窗口左、上、右边缘之间的留白（底边紧接底部栏）
const int kCallArtMargin = 12;

//BuildCallingBody 的结果：控件树的根与画面区控件（借用，归控件树所有）
struct CallingBody
{
    std::unique_ptr<balloonwjui::DuiControl> m_root;   //客户区内容的根，交给 SetClientContent
    CallVideoCanvas* m_canvas;                         //画面区（语音态画法）；通话窗往上贴对方头像与状态文字

    CallingBody();
};

/**
 *  搭「呼叫中」态的客户区控件树：画面区（语音态画法，不响应点击）占满底部栏以上的全部高度、四周留白与接通后的
 *  语音态相同；底部栏高 kCallBottomBarH，只在右侧放一个「取消」按钮。2026-10-05 起「取消」是红色圆形图标按钮
 *  （直径 kCallRoundBtnLargePx，与接通态的挂断同一位置、同一外观），文字改作悬停提示。
 *  @param canvasId 画面区的 ctrlId
 *  @param cancelId 「取消」按钮的 ctrlId（通话窗按它发取消信令并关窗）
 *  @param art      图标按钮的位图缓存（借用；须比返回的控件树活得久，一般是通话窗的成员）
 *  @return 控件树与画面区控件的指针
 */
CallingBody BuildCallingBody(UINT canvasId, UINT cancelId, CallButtonArtCache& art);

} // namespace call
