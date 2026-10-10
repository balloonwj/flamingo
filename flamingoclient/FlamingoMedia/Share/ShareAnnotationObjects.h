/**
 *  屏幕共享标注的对象几何（第 20 期，docs/屏幕共享与录制方案.md 第 2 节第 37 条），ShareAnnotationObjects.h
 *
 *  标注升级为可修改的对象之后，选择工具、橡皮、连接线都要知道「一个对象占哪块地方」。本文件只有纯函数，全部在归一化坐标里算
 *  （0 ~ kAnnotCoordMax），不依赖窗口：
 *    - 对象的外接矩形：两点与多点的对象按点的范围；文字按近似的字宽与行高；数字标号按半径；连接线按实际的两端；
 *    - 连接线实际的两端：吸附的一端从被吸附对象外接矩形的中心出发，裁到这个矩形的边上；
 *    - 命中：点选时封闭形状、图元、文字、标号点在里面就算，线条要落在容差以内；框选取完整落在框里的对象；
 *    - 单选时的句柄（直线、箭头、连接线为两端，其余为外接矩形的四角与四边中点）、按句柄拖动后的新矩形、把对象缩放到新矩形。
 *  文字的宽高与字体有关，这里按「ASCII 字符半个字宽、其余一个字宽」近似，只用于选择与命中，不用于绘制。
 *  归一化坐标的横纵单位不等长，涉及长宽的换算都要传共享内容的宽高比（宽 ÷ 高）。
 *  单元测试见 Source/Tests/ShareObjectTests.cpp。
 *
 *  典型用法：
 *      int index = AnnotHitObject(model.Strokes(), pt, tolerance, aspect, true);
 *      if (index >= 0) AnnotRect box = AnnotObjectBounds(model.Strokes()[index], model.Strokes(), aspect);
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "ShareAnnotation.h"

namespace screenshare {

//归一化坐标里的矩形，左上含、右下含
struct AnnotRect
{
    int m_left;     //左
    int m_top;      //上
    int m_right;    //右
    int m_bottom;   //下

    //空矩形（左上大于右下）
    AnnotRect();
    AnnotRect(int left, int top, int right, int bottom);

    //是否空矩形
    bool IsEmpty() const;
    //点是否在矩形里（含边）
    bool Contains(int x, int y) const;
    //另一个矩形是否完整落在本矩形里
    bool ContainsRect(const AnnotRect& r) const;
    int Width() const;
    int Height() const;
    //中心
    AnnotationPoint Center() const;
};

//单选时的句柄
enum AnnotHandle
{
    annot_handle_none  = -1,   //没有命中句柄
    annot_handle_nw    = 0,    //左上角
    annot_handle_n     = 1,    //上边中点
    annot_handle_ne    = 2,    //右上角
    annot_handle_e     = 3,    //右边中点
    annot_handle_se    = 4,    //右下角
    annot_handle_s     = 5,    //下边中点
    annot_handle_sw    = 6,    //左下角
    annot_handle_w     = 7,    //左边中点
    annot_handle_start = 8,    //直线、箭头、连接线的起点
    annot_handle_end   = 9,    //直线、箭头、连接线的终点
};

//外接矩形的句柄个数
const int kAnnotBoxHandleCount = 8;
//缩放后对象的最小边长（归一化单位）
const int kAnnotMinObjectSize = 200;
//文字近似：行高是字号的多少倍、ASCII 字符宽是字号的多少倍
const double kAnnotApproxLineHeight = 1.3;
const double kAnnotApproxAsciiWidth = 0.55;
//数字标号的半径是字号的多少倍（与绘制一致）
const double kAnnotBadgeRadiusRatio = 0.8;
//连接线裁到被吸附对象边上之后再留的间隙（归一化单位，按内容高度计）
const int kAnnotConnectorGap = 120;

/**
 *  文字在归一化坐标里的近似宽高。
 *  @param utf8     文字（UTF-8），可有换行
 *  @param fontSize 字号值（AnnotationStroke::m_width，换算见 kAnnotTextScale）
 *  @param aspect   共享内容的宽高比（宽 ÷ 高），不大于 0 时按 1
 *  @param w、h     [出参] 宽、高（归一化单位）
 */
void AnnotApproxTextSize(const std::string& utf8, int fontSize, double aspect, int& w, int& h);

/**
 *  连接线实际的两端：吸附的一端从被吸附对象外接矩形的中心出发、沿连线裁到矩形边外 kAnnotConnectorGap；被吸附对象不存在时用
 *  连接线自己记的点。不是连接线时返回它的首尾两点。
 *  @param c       连接线
 *  @param all     全部对象（找被吸附的对象）
 *  @param aspect  宽高比
 *  @param from、to [出参] 起点、终点
 */
void AnnotConnectorEnds(const AnnotationStroke& c, const std::vector<AnnotationStroke>& all, double aspect,
                        AnnotationPoint& from, AnnotationPoint& to);

/**
 *  对象的外接矩形（几何范围，不含线宽）。
 *  @param s      对象
 *  @param all    全部对象（连接线要按被吸附对象算两端）
 *  @param aspect 宽高比
 *  @return 外接矩形；没有点时为空矩形
 */
AnnotRect AnnotObjectBounds(const AnnotationStroke& s, const std::vector<AnnotationStroke>& all, double aspect);

//能否被选中：画完了的、不是激光笔与局部橡皮
bool AnnotIsSelectable(const AnnotationStroke& s);

//是否封闭形状：矩形、椭圆、各种多边形、对话气泡、云形、块状箭头
bool AnnotIsClosedShape(int tool);

/**
 *  点是否落在一个对象上（不看能否被选中）。图元、文字、标号按外接矩形；封闭形状 insideCounts 时点在轮廓里面也算；
 *  其余看点到线的距离是否不超过容差加半个线宽。
 *  @param s            对象
 *  @param all          全部对象（连接线按被吸附对象算两端）
 *  @param pt、tolerance、aspect、insideCounts 同 AnnotHitObject
 */
bool AnnotHitTest(const AnnotationStroke& s, const std::vector<AnnotationStroke>& all, const AnnotationPoint& pt,
                  int tolerance, double aspect, bool insideCounts);

//连接线能否吸附到它上面：图元、封闭形状（含块状箭头）、文字、数字标号
bool AnnotIsAttachable(const AnnotationStroke& s);

//是否「两端可拖」的对象：直线、箭头、连接线（两个点）
bool AnnotIsEndpointObject(const AnnotationStroke& s);

/**
 *  点中的最上面一个可选对象。
 *  @param all          全部对象，后添加的在上面
 *  @param pt           点
 *  @param tolerance    容差（归一化单位）
 *  @param aspect       宽高比
 *  @param insideCounts 封闭形状点在里面是否算命中（选择工具传 true；橡皮传 false，只认边线）。图元、文字、标号点在里面总算命中
 *  @return 下标；没有命中返回 -1
 */
int AnnotHitObject(const std::vector<AnnotationStroke>& all, const AnnotationPoint& pt, int tolerance, double aspect,
                   bool insideCounts);

/**
 *  点所在的最上面一个可吸附对象（连接线的端点落在哪里）。
 *  @param exclude 不算的对象（连接线自己）
 *  @return 下标；没有返回 -1
 */
int AnnotHitAttachable(const std::vector<AnnotationStroke>& all, const AnnotationPoint& pt, int tolerance, double aspect,
                       const AnnotationRef& exclude);

/**
 *  完整落在框里的可选对象（框选）。
 *  @return 下标，按先后
 */
std::vector<int> AnnotObjectsInRect(const std::vector<AnnotationStroke>& all, const AnnotRect& box, double aspect);

/**
 *  单选时的句柄位置：两端可拖的对象为起点、终点两个（连接线按实际两端）；其余为外接矩形的八个，顺序同 AnnotHandle。
 */
std::vector<AnnotationPoint> AnnotHandlePositions(const AnnotationStroke& s, const std::vector<AnnotationStroke>& all,
                                                  double aspect);

/**
 *  点中了哪个句柄。
 *  @param handles   AnnotHandlePositions 的结果
 *  @param endpoints 是否两端句柄（两个时为起点、终点）
 *  @return AnnotHandle 之一
 */
int AnnotHitHandle(const std::vector<AnnotationPoint>& handles, bool endpoints, const AnnotationPoint& pt, int tolerance);

/**
 *  按某个外接矩形句柄拖到 pt 之后的新矩形：拖动的边跟着点走，对边不动；不会翻转，边长不小于 kAnnotMinObjectSize。
 *  @param keepRatio 是否保持宽高比例（只对四个角有效）
 */
AnnotRect AnnotResizeRect(const AnnotRect& r, int handle, const AnnotationPoint& pt, bool keepRatio);

/**
 *  把对象从一个矩形缩放到另一个矩形：各点按比例换算；文字的左上角换算、字号按高度比例缩放；数字标号的中心换算、大小按
 *  两个方向里较小的比例缩放。线宽不变。
 *  @return 缩放后的对象（作者、编号、种类不变）
 */
AnnotationStroke AnnotScaleObject(const AnnotationStroke& s, const AnnotRect& from, const AnnotRect& to);

} // namespace screenshare
