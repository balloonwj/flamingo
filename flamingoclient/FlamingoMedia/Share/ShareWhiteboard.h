/**
 *  白板的视图（第 21 期，docs/屏幕共享与录制方案.md 第 2 节第 39、41 条），ShareWhiteboard.h
 *
 *  白板是一块可以向右、向下拖动扩展的画布。为了让笔迹的归一化坐标在扩展前后不变，白板把一块固定大小的「世界」当作标注的共享内容：
 *  世界是白板打开时可见区域的 kWhiteboardWorldScale 倍（横纵各），笔迹按世界归一化；画布是世界里从左上角起有背景、可以画的那一块，
 *  起初与可见区域一样大，拖它的右边、下边可以扩展（不小于可见区域、不超过世界）；可见区域在画布里滚动。世界比可见区域大，
 *  线宽与字号按 kWhiteboardWidthScale 缩小（见 ShareAnnotationSession::SetWidthScale），显示上与屏幕上的标注一样粗细。
 *  本类只有纯逻辑（像素坐标的换算），不碰窗口，单元测试见 Source/Tests/ShareRecordPenTests.cpp（SSRP3、SSRP4）。
 *
 *  典型用法（白板画布控件里）：
 *      m_view.Init(clientW, clientH);
 *      DrawAnnotationsInView(hdc, viewRect, offset(m_view.ContentRect()), ...);
 *      session.PointerDown(m_view.ViewToAnnot(x, y), ...);
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <vector>
#include <windows.h>

#include "ShareAnnotation.h"

namespace screenshare {

//白板背景
enum WhiteboardBackground
{
    wb_bg_plain = 0,   //纯白
    wb_bg_grid  = 1,   //网格
    wb_bg_lines = 2,   //横线
};

//背景的种类数
const int kWhiteboardBackgroundCount = 3;

//画布的边（拖动扩展用）
enum WhiteboardEdge
{
    wb_edge_none   = 0,   //没有命中
    wb_edge_right  = 1,   //右边：向右扩展
    wb_edge_bottom = 2,   //下边：向下扩展
    wb_edge_corner = 3,   //右下角：同时向右、向下扩展
};

//世界是白板打开时可见区域的多少倍（横纵各）
const int kWhiteboardWorldScale = 2;
//白板上线宽与字号的缩放：世界是可见区域的两倍，线宽减半后显示上与屏幕标注一致
const double kWhiteboardWidthScale = 0.5;
//网格的间距、横线的间距（像素）
const int kWhiteboardGridPx = 32;
const int kWhiteboardLinePx = 40;
//拖动画布边的命中宽度（像素，边线两侧各算这么宽）
const int kWhiteboardEdgeHitPx = 6;
//白板的颜色（白板窗口、观看窗里的白板画面与导出的图片共用）：画布、网格线、横线、画布以外
const COLORREF kWhiteboardCanvasColor = RGB(255, 255, 255);
const COLORREF kWhiteboardGridColor = RGB(228, 233, 241);
const COLORREF kWhiteboardLineColor = RGB(204, 216, 232);
const COLORREF kWhiteboardOutsideColor = RGB(226, 229, 234);
//共享白板（第 23 期）的世界宽高比 16:9：房间里每个人的世界形状一致，笔迹的归一化坐标才对得上
const int kWhiteboardSharedAspectW = 16;
const int kWhiteboardSharedAspectH = 9;
//共享白板背景线间距的参照：世界宽为这么多像素时网格间距为 kWhiteboardGridPx、横线间距为 kWhiteboardLinePx，
//其余宽度按比例（背景线贴着世界，每个人看到的背景与笔迹的相对位置一致）
const int kWhiteboardSharedRefWorldW = 3840;
//共享白板的缩放范围与滚轮缩放一格的倍数
const double kWhiteboardMinZoom = 0.25;
const double kWhiteboardMaxZoom = 4.0;
const double kWhiteboardZoomStep = 1.25;

//白板的视图：世界、画布、滚动与坐标换算
class ShareWhiteboardView
{
public:
    ShareWhiteboardView();

    /**
     *  初始化：世界为可见区域的 kWhiteboardWorldScale 倍，画布与可见区域一样大，不滚动。
     *  @param viewW、viewH 可见区域的像素尺寸（白板画布控件的大小），不小于 1
     */
    void Init(int viewW, int viewH);

    /**
     *  初始化为共享白板（第 23 期）：世界宽为可见区域宽的 kWhiteboardWorldScale 倍、宽高比固定为 16:9，画布就是整个世界（不再拖动扩展），
     *  不缩放、不滚动；背景线按世界的大小缩放。
     *  @param viewW、viewH 可见区域的像素尺寸，不小于 1
     */
    void InitShared(int viewW, int viewH);

    //是否为共享白板（由 InitShared 初始化）
    bool IsShared() const;

    //当前缩放倍数：共享白板为 kWhiteboardMinZoom ~ kWhiteboardMaxZoom，本机白板恒为 1
    double Zoom() const;

    /**
     *  缩放（共享白板）：世界与画布按倍数缩放，可见区域里 (anchorX, anchorY) 下面的世界位置保持不动；倍数限制在缩放范围以内。
     *  @return 缩放倍数有变化返回 true；本机白板什么也不做、返回 false
     */
    bool SetZoom(double zoom, int anchorX, int anchorY);

    /**
     *  可见区域在世界里的位置（第 23 期，「跟随创建者」用），按世界归一化到 0 ~ kAnnotCoordMax。
     *  @param left、top [出参] 可见区域左上角
     *  @param width     [出参] 可见区域的宽（世界比可见区域窄时为 kAnnotCoordMax）
     */
    void VisibleRegion(int& left, int& top, int& width) const;

    /**
     *  显示世界里的某块区域（「跟随创建者」）：缩放到可见区域的宽正好是 width（限制在缩放范围以内），再把左上角滚到 (left, top)。
     *  width 不大于 0 时什么也不做。只用于共享白板。
     */
    void ShowRegion(int left, int top, int width);

    //世界、画布、可见区域的像素尺寸
    int WorldWidth() const;
    int WorldHeight() const;
    int CanvasWidth() const;
    int CanvasHeight() const;
    int ViewWidth() const;
    int ViewHeight() const;

    //滚动位置：可见区域左上角在世界里的像素坐标
    int ScrollX() const;
    int ScrollY() const;

    //世界的宽高比（宽 ÷ 高），交给标注会话（文字近似尺寸、连接线裁边按它换算）
    double Aspect() const;

    /**
     *  可见区域的大小变了（窗口改变大小）：画布至少与可见区域一样大（不超过世界），滚动限制在画布以内。
     */
    void SetViewSize(int w, int h);

    /**
     *  滚动（像素），限制在画布以内。
     *  @return 滚动位置有变化返回 true
     */
    bool ScrollBy(int dx, int dy);

    /**
     *  可见区域里的一点是否落在画布的右边、下边或右下角上（两侧各 kWhiteboardEdgeHitPx 像素）。
     *  @return 取值见 WhiteboardEdge
     */
    int HitEdge(int viewX, int viewY) const;

    /**
     *  把画布的边拖到可见区域里的某一点：画布的右边（或下边）移到这一点在世界里的横（或纵）坐标，不小于可见区域、不超过世界。
     *  @param edge 取值见 WhiteboardEdge
     */
    void DragEdge(int edge, int viewX, int viewY);

    //世界在可见区域坐标里的矩形（左上为负的滚动位置），即交给标注绘制的「共享内容矩形」
    RECT ContentRect() const;

    //画布在可见区域坐标里的矩形
    RECT CanvasRect() const;

    //可见区域里的一点换成按世界归一化的坐标（限制在世界以内）
    AnnotationPoint ViewToAnnot(int x, int y) const;

    /**
     *  背景线的位置（可见区域坐标），只给画布以内、可见区域以内的，不含画布的边。
     *  @param background 取值见 WhiteboardBackground：网格给竖线与横线，横线只给横线，纯白都没有
     *  @param xs         [出参] 竖线的横坐标
     *  @param ys         [出参] 横线的纵坐标
     */
    void BackgroundLines(int background, std::vector<int>& xs, std::vector<int>& ys) const;

private:
    //把滚动位置限制在画布以内
    void clampScroll();
    //背景线的间距（像素）：共享白板按世界的大小缩放（不小于 4 像素），本机白板就是 basePx
    int lineSpacing(int basePx) const;

    int m_worldW;    //世界宽（像素）
    int m_worldH;    //世界高（像素）
    int m_canvasW;   //画布宽（像素）
    int m_canvasH;   //画布高（像素）
    int m_viewW;     //可见区域宽（像素）
    int m_viewH;     //可见区域高（像素）
    int m_scrollX;   //可见区域左上角在世界里的横坐标（像素）
    int m_scrollY;   //可见区域左上角在世界里的纵坐标（像素）
    bool   m_shared;      //是否为共享白板（第 23 期）：画布即世界、宽高比 16:9、可缩放
    int    m_baseWorldW;  //共享白板不缩放时的世界宽（像素）：可见区域宽的 kWhiteboardWorldScale 倍
    int    m_baseWorldH;  //共享白板不缩放时的世界高（像素）：按 16:9 由宽算出
    double m_zoom;        //缩放倍数；本机白板恒为 1
};

} // namespace screenshare
