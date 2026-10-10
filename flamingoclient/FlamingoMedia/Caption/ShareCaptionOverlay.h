/**
 *  屏幕共享实时字幕的浮层窗口（docs/语音字幕方案.md 第 9 节「显示」），ShareCaptionOverlay.h
 *
 *  一个鼠标穿透、不抢焦点、不进任务栏的分层窗口（WS_EX_LAYERED | WS_EX_TRANSPARENT），盖在显示区域的底部：
 *    - 观看者：以观看窗为所有者窗口，盖在观看窗的画面上，不置顶（切到别的程序时随观看窗一起被盖住）；
 *    - 共享者本人：置顶，盖在被共享的显示器或窗口上，并用 SetWindowDisplayAffinity 排除在屏幕采集之外（与录制条的做法相同），
 *      观看者看到的画面里没有这个浮层，自己却看得到；
 *    - 录屏（RecordCaptionLive）：与共享者本人相同，盖在录制区域上并排除在采集之外，不会被录进视频。
 *  要求排除在采集之外而系统做不到时（SetWindowDisplayAffinity 失败）浮层不显示，免得字幕被采集进画面。
 *  字幕画法用字幕模块的 RenderOverlay（与观看端、录像里同一份绘制代码：白字黑边、说话人黄色、中间结果浅灰），
 *  画好的预乘透明度 BGRA 经 UpdateLayeredWindow 交给系统合成。不用 balloonui：分层窗口只显示一张图，没有控件。
 *
 *  只在 UI 线程上使用。
 *
 *  典型用法：
 *      m_overlay.Update(area, true, NULL, lines, module);
 *      m_overlay.Hide();
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "ShareCaptionBoard.h"
#include "../../FlamingoCaption/CaptionApi.h"

namespace screenshare {

/**
 *  CShareCaptionOverlay —— 字幕浮层窗口。
 */
class CShareCaptionOverlay
{
public:
    CShareCaptionOverlay();
    ~CShareCaptionOverlay();

    CShareCaptionOverlay(const CShareCaptionOverlay&) = delete;
    CShareCaptionOverlay& operator=(const CShareCaptionOverlay&) = delete;

    /**
     *  按新的区域与字幕重画并显示；没有字幕时隐藏。
     *  @param area               显示区域（屏幕坐标）：浮层贴在它的底部、与它同宽
     *  @param excludeFromCapture 是否排除在屏幕采集之外（共享者本人为 true）
     *  @param owner              所有者窗口（观看窗）：浮层不置顶、始终在它之上；为 NULL 时浮层置顶。与上次不同时重建浮层窗口
     *  @param lines              要显示的字幕（从上到下）
     *  @param module             字幕模块（画字幕用，不拥有）；为 NULL 时隐藏
     */
    void Update(const RECT& area, bool excludeFromCapture, HWND owner, const std::vector<ShareCaptionLine>& lines,
                caption::ICaptionModule* module);

    //隐藏（不销毁窗口）
    void Hide();

    //销毁窗口；可重复调用
    void Destroy();

    //窗口是否正在显示
    bool IsShown() const;

    //浮层窗口的句柄（单元测试检查窗口样式、所有者与是否排除采集用）；还没建或已销毁时为 NULL
    HWND WindowHandle() const;

private:
    /**
     *  按所有者窗口与是否排除采集建浮层窗口（已建且两者都相同时直接返回）。先建普通窗口、按需排除在采集之外，再改成分层窗口
     *  （分层窗口上设置排除会失败，见实现里的说明）。
     *  @return 窗口可用返回 true；要求排除却排除不了时不建窗口、返回 false
     */
    bool ensureWindow(HWND owner, bool excludeFromCapture);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND                  m_hwnd;        //浮层窗口；还没建时为 NULL
    bool                  m_excluded;    //当前浮层窗口建立时是否排除在采集之外（与之不同时重建窗口）
    HWND                  m_owner;       //浮层窗口建立时的所有者窗口（不拥有）；为 NULL 表示置顶的无所有者窗口
    bool                  m_excludeFailLogged;   //排除在采集之外失败的日志是否已记过（只记一次）
    std::vector<uint32_t> m_pixels;      //上一次画的像素（预乘透明度 BGRA）
};

} // namespace screenshare
