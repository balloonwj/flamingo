/**
 *  通话窗底部栏的网络信号图标，CallSignalArt.h
 *
 *  docs/音视频通话-网络状况标识方案.md 第 3 期：四根自左向右逐渐升高的竖条，按档位点亮其中几根并配色（CallNetQuality 的
 *  SignalLookForGrade）。分三层：
 *    - CallSignalBarRect / MakeCallSignalBitmap：纯几何与画位图。位图按控件的实际像素尺寸生成（高 DPI 下不经拉伸），
 *      直接写像素，不需要 GDI+。单元测试见 Source/Tests/CallNetQualityTests.cpp（标签 [callnet]）；
 *    - CallSignalArtCache：按「档位 + 像素尺寸」缓存位图，同一外观只画一次；
 *    - CallSignalIcon：显示图标的控件。balloonui 没有单独的图片控件，借用 DuiButton 的四态背景位图（四态贴同一张），
 *      只覆写 OnSetCursor 显示普通箭头光标：图标只用来看与悬停查看详情，点了没有动作，不应显示手形光标。
 *      不用禁用态的按钮，是因为禁用的控件不参与命中测试，悬停提示也就不会出现。
 *
 *  典型用法（通话窗每秒一次）：
 *      const RECT rc = m_signalIcon->GetRect();
 *      m_signalIcon->SetSignalBitmap(m_signalArt.Get(grade, rc.right - rc.left, rc.bottom - rc.top));
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <map>
#include <utility>

#include <windows.h>

#include "CallNetQuality.h"   //CallSignalLook、SignalLookForGrade
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace call {

//信号图标的设计尺寸（逻辑像素），控件按它占位，位图按控件排版后的实际像素尺寸生成
const int kCallSignalIconW = 20;
const int kCallSignalIconH = 16;

/**
 *  第 index 根竖条在 widthPx × heightPx 位图里的矩形。设计尺寸 20×16 下竖条宽 3、间距 2、最左一根距左边 1，
 *  高度依次为 5、8、11、14，底边距下边 1；其它尺寸按比例换算。
 *  @param index    竖条序号，0 ~ kSignalBarCount - 1，自左向右
 *  @param widthPx  位图宽（像素），须大于 0
 *  @param heightPx 位图高（像素），须大于 0
 *  @return 矩形（右、下边不含）；index 越界时返回全 0
 */
RECT CallSignalBarRect(int index, int widthPx, int heightPx);

/**
 *  画信号图标：32 位、自上而下、预乘透明度的 DIB，竖条以外完全透明；前 look.m_bars 根用点亮色，其余用暗色。
 *  @param look     外观（SignalLookForGrade 的结果）
 *  @param widthPx  宽（像素），须大于 0
 *  @param heightPx 高（像素），须大于 0
 *  @return 位图，所有权交给调用方（用完 DeleteObject）；尺寸非法或创建失败返回 NULL
 */
HBITMAP MakeCallSignalBitmap(const CallSignalLook& look, int widthPx, int heightPx);

/**
 *  按「档位 + 像素尺寸」缓存信号图标位图。DuiButton 不持有位图，缓存须比用到它的控件活得久，一般作为窗口的成员变量，
 *  声明在控件指针之后。线程：只在 UI 线程上使用。
 */
class CallSignalArtCache
{
public:
    CallSignalArtCache();

    //析构：释放缓存的全部位图。此时用到这些位图的控件须已销毁
    ~CallSignalArtCache();

    /**
     *  取某档位在某尺寸下的位图，第一次取时画好并缓存。
     *  @param grade    档位（可为 kCallGradeUnknown）
     *  @param widthPx  宽（像素）
     *  @param heightPx 高（像素）
     *  @return 位图（归本缓存所有，调用方不要释放）；尺寸非法或创建失败返回 NULL
     */
    HBITMAP Get(int grade, int widthPx, int heightPx);

    //已缓存的位图张数（单元测试用）
    size_t Count() const { return m_bitmaps.size(); }

private:
    CallSignalArtCache(const CallSignalArtCache&);
    CallSignalArtCache& operator=(const CallSignalArtCache&);

    //缓存的键：（档位，宽 × 高）。档位只有 6 种取值、尺寸随 DPI 变化，数量很少
    typedef std::pair<int, std::pair<int, int> > Key;

private:
    std::map<Key, HBITMAP> m_bitmaps;   //键 → 位图（归本对象所有，析构时释放）
};

/**
 *  显示信号图标的控件：DuiButton 的四态背景都贴同一张位图，不接收焦点、不显示文字、光标为普通箭头。
 *  悬停提示经 DuiToolTipMgr 登记（由使用方负责），点击时 DuiButton 照常发 DUIN_CLICK，使用方不处理即可。
 */
class CallSignalIcon : public balloonwjui::DuiButton
{
public:
    CallSignalIcon();

    /**
     *  换一张位图（四态相同）。位图相同时不重绘。
     *  @param bitmap 位图（借用，不拥有；须比本控件活得久，一般取自 CallSignalArtCache）。传 NULL 时 DuiButton 按
     *                默认的按钮样式绘制，所以使用方应在控件显示之前就贴好一张位图
     */
    void SetSignalBitmap(HBITMAP bitmap);

    //当前贴的位图（单元测试用）
    HBITMAP SignalBitmap() const { return m_bitmap; }

    //光标为普通箭头：图标只供查看，点了没有动作
    bool OnSetCursor(POINT pt) override;

private:
    HBITMAP m_bitmap;   //当前贴的位图（借用，不拥有）
};

} // namespace call
