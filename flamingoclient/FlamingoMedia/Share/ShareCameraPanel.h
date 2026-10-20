/**
 *  屏幕共享房间的摄像头小窗面板，ShareCameraPanel.h
 *
 *  把房间里开着摄像头的人（含本人）排成竖向的一列 16:9 小窗，每个小窗下方写名字；超出同屏上限（9 路）的人不解码，
 *  只在最下面写一行「另有 N 人开着摄像头」（docs/屏幕共享与录制方案.md 5.2 第 8 步、5.8）。观看窗放在画面右侧，
 *  发起人一侧放在一个浮动的小窗口里（CShareCameraWindow）。
 *  面板不持有画面数据：宿主定时调用 Refresh，面板经 IShareCameraFrames 取各路最近一帧，有新画面才重画。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *  小窗的排布（ComputeCameraTileRects）、排序（BuildCameraTiles）与发起人浮动窗口的位置（ComputeCameraWindowRect）
 *  都是纯函数，单元测试见 Source/Tests/ShareCameraPanelTests.cpp。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <windows.h>

namespace screenshare {

//一个摄像头小窗
struct ShareCameraTile
{
    int          m_seat;   //座位号
    std::wstring m_name;   //显示名（本人为「我」）
    bool         m_self;   //是否本人（画面取本人摄像头的预览）

    ShareCameraTile()
        : m_seat(0)
        , m_self(false)
    {
    }
};

//小窗画面的来源（由屏幕共享控制器实现）
class IShareCameraFrames
{
public:
    virtual ~IShareCameraFrames()
    {
    }

    /**
     *  取某一路摄像头最近一帧。
     *  @param seat   座位号
     *  @param self   是否本人（取本人摄像头的预览）
     *  @param bgra   [出参] 自上而下的 BGRA，行跨度 = 宽 × 4
     *  @param serial [出参] 帧序号
     *  @return 已有画面返回 true
     */
    virtual bool GetCameraFrame(int seat, bool self, std::vector<uint8_t>& bgra, int& width, int& height,
                                uint64_t& serial) = 0;
};

/**
 *  在区域里竖向排 count 个 16:9 小窗：宽度用满区域（减去左右边距），放不下时按高度均分、宽度随之缩小并水平居中。
 *  @param area   面板区域
 *  @param count  小窗个数
 *  @param bottomReserve 底部留给「另有 N 人」那一行的高度（没有那一行时传 0）
 *  @param out    [出参] 各小窗的矩形（先清空）；count 不大于 0 或区域太小时为空
 */
void ComputeCameraTileRects(const RECT& area, int count, int bottomReserve, std::vector<RECT>& out);

/**
 *  排出要显示的小窗：本人（摄像头开着时）在最前，其他人按座位号升序；最多 maxTiles 个，多出的只计人数。
 *  @param selfOn     本人摄像头是否开着
 *  @param selfSeat   本人座位号
 *  @param otherSeats 开着摄像头的其他成员的座位号
 *  @param names      座位号 → 显示名；查不到的显示「成员」
 *  @param maxTiles   同屏上限
 *  @param tiles      [出参] 要显示的小窗（先清空）
 *  @return 超出同屏上限、没有排进来的人数
 */
int BuildCameraTiles(bool selfOn, int selfSeat, const std::set<int>& otherSeats, const std::map<int, std::wstring>& names,
                     int maxTiles, std::vector<ShareCameraTile>& tiles);

/**
 *  发起人摄像头浮动窗口的位置：贴在工作区右边缘、竖向居中；高度 = 标题栏 + 各小窗 + 「另有 N 人」那一行，
 *  超出工作区时限制在工作区内（四边各留边距）。
 *  @param work          被共享内容所在显示器的工作区
 *  @param tileCount     小窗个数
 *  @param overflowCount 超出同屏上限的人数
 *  @return 窗口矩形（屏幕坐标）
 */
RECT ComputeCameraWindowRect(const RECT& work, int tileCount, int overflowCount);

//摄像头小窗收起后小把手的宽与按钮行高（像素，2026-10-12）；把手的标题条取 balloonui 标题栏的最小高度 18
const int kCameraHandleW = 72;
const int kCameraHandleTitleH = 18;
const int kCameraHandleContentH = 36;

/**
 *  摄像头小窗收起后小把手的位置（2026-10-12）：贴着展开时窗口的右边缘与顶边，宽 kCameraHandleW、
 *  高 kCameraHandleTitleH + kCameraHandleContentH。
 *  @param expanded 展开时的窗口矩形（ComputeCameraWindowRect 的结果，屏幕坐标）
 *  @return 小把手的窗口矩形（屏幕坐标）
 */
RECT ComputeCameraHandleRect(const RECT& expanded);

} // namespace screenshare

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace screenshare {

//摄像头小窗面板
class CShareCameraPanel : public balloonwjui::DuiControl
{
public:
    CShareCameraPanel();

    //画面来源（不拥有；为 NULL 时只画名字）
    void SetSource(IShareCameraFrames* source);

    /**
     *  设置要显示的小窗与超出上限的人数。
     *  @param tiles         小窗（最多 9 个，多出的忽略）
     *  @param overflowCount 超出上限、没有显示画面的人数
     */
    void SetTiles(const std::vector<ShareCameraTile>& tiles, int overflowCount);

    //有没有要显示的内容（宿主据此决定面板显隐）
    bool HasContent() const;

    //取各路最新画面，有变化时重画（宿主定时调用）
    void Refresh();

    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    //一个小窗的画面缓存
    struct TileFrame
    {
        std::vector<uint8_t> m_bgra;     //画面
        int                  m_width;    //宽
        int                  m_height;   //高
        uint64_t             m_serial;   //帧序号

        TileFrame();
    };

    IShareCameraFrames*          m_source;          //画面来源（不拥有）
    std::vector<ShareCameraTile> m_tiles;           //小窗
    std::vector<TileFrame>       m_frames;          //与 m_tiles 一一对应的画面缓存
    int                          m_overflowCount;   //超出上限的人数
};

} // namespace screenshare
