/**
 *  屏幕共享标注的坐标换算与绘制，ShareAnnotationRender.h
 *
 *  观看窗的画面控件与发起人本机的标注层都用这里的函数把笔迹画到「共享内容在屏幕上占的矩形」里
 *  （docs/屏幕共享与录制方案.md 5.9）：归一化坐标与显示坐标互换、线宽与字号换算、箭头的两翼、用 GDI 画全部笔迹。
 *  换算函数是纯函数，单元测试见 Source/Tests/ShareAnnotationTests.cpp。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <vector>
#include <windows.h>

#include "ShareAnnotation.h"

namespace screenshare {

class ShareAnnotationSession;

/**
 *  显示坐标换成归一化坐标；矩形以外的点夹到边缘上。
 *  @param content 共享内容在屏幕（或控件）上占的矩形
 */
AnnotationPoint ScreenToAnnot(const RECT& content, POINT pt);

//归一化坐标换成显示坐标
POINT AnnotToScreen(const RECT& content, const AnnotationPoint& pt);

//显示像素换成归一化单位（按内容宽度），至少为 1
int AnnotTolerance(const RECT& content, int px);

//线宽值换成显示像素，至少为 1
int AnnotStrokePx(const RECT& content, int width);

/**
 *  线宽值换成绘制用的像素宽度，保留小数、至少 1 像素（2026-10-05：原先按整数截断，790 像素宽的画面上中档钢笔
 *  1.58 像素被截成 1 像素，线条显得细碎）。命中判定等仍用整数的 AnnotStrokePx。
 *  @param content 共享内容的显示矩形
 *  @param width   AnnotationStroke::m_width
 *  @return 像素宽度，不小于 1
 */
float AnnotStrokeWidthPx(const RECT& content, int width);

//字号值换成显示像素，至少为 kAnnotMinTextPx
int AnnotTextPx(const RECT& content, int size);

//文字的最小显示字号（像素）
const int kAnnotMinTextPx = 8;

/**
 *  箭头两翼的端点：从箭头尖 to 往回、与箭杆各成 30 度、长 headLen。
 *  @param left、right [出参] 两翼的端点
 */
void ComputeArrowHead(POINT from, POINT to, int headLen, POINT& left, POINT& right);

/**
 *  把笔迹画到 hdc 上。
 *  @param content                共享内容在 hdc 上占的矩形（笔迹只画在这个矩形里）
 *  @param strokes                全部笔迹，按先后
 *  @param pending                拖动中的图形预览；没有时为 NULL
 *  @param colorKeyTarget 目标是不是颜色键窗口（发起人的标注层）：是则荧光笔画成实色、文字不做抗锯齿（半透明与
 *                        抗锯齿的边缘像素会与键色混出杂色）；观看窗传 false，荧光笔半透明、文字抗锯齿
 */
void DrawAnnotations(HDC hdc, const RECT& content, const std::vector<AnnotationStroke>& strokes,
                     const AnnotationStroke* pending, bool colorKeyTarget);

/**
 *  同上，另带本机当前时刻（第 19 期）：激光笔按它淡出。上面那个版本按 GetTickCount64 取时刻。
 *  @param nowMs 本机当前时刻（毫秒）
 */
void DrawAnnotations(HDC hdc, const RECT& content, const std::vector<AnnotationStroke>& strokes,
                     const AnnotationStroke* pending, bool colorKeyTarget, int64_t nowMs);

/**
 *  把全部笔迹画成一张图层（第 19 期）：自上而下的 BGRA，预乘 alpha，没画到的像素全为 0。
 *  按添加先后画，局部橡皮把它之前画的像素擦成透明；激光笔按 nowMs 淡出；形状按样式填充；文字带加粗与背景。
 *  @param width、height 图层尺寸（像素），也是共享内容的显示尺寸（归一化坐标按它换算）
 *  @param strokes       全部笔迹，按先后
 *  @param pending       拖动中的预览；没有时为 NULL
 *  @param nowMs         本机当前时刻（毫秒）
 *  @param antialias     是否抗锯齿（颜色键窗口传 false，再经 AnnotDitherLayer 处理）
 *  @param bgra          [出参] 图层，大小为 width × height × 4
 */
void RenderAnnotationLayer(int width, int height, const std::vector<AnnotationStroke>& strokes,
                           const AnnotationStroke* pending, int64_t nowMs, bool antialias, std::vector<uint8_t>& bgra);

/**
 *  把图层里半透明的像素按 4×4 有序抖动换成全不透明或全透明（第 19 期）：颜色键窗口（发起人本机的标注层）不能显示半透明，
 *  荧光笔、半透明填充、淡出中的激光笔经抖动后按不透明度比例留下一部分像素，看上去仍是半透明的。
 *  全不透明的像素保持原色，全透明的保持为 0；留下的半透明像素换成去掉预乘之后的原色、alpha 为 255。
 *  @param bgra          图层（预乘 alpha），就地修改
 *  @param width、height 图层尺寸
 */
void AnnotDitherLayer(std::vector<uint8_t>& bgra, int width, int height);

//工具光标的图像（第 19 期）
struct AnnotCursorImage
{
    int                  m_size;   //边长（像素），图像为正方形
    int                  m_hotX;   //热点横坐标（像素）
    int                  m_hotY;   //热点纵坐标（像素）
    std::vector<uint8_t> m_bgra;   //自上而下的 BGRA，预乘 alpha

    AnnotCursorImage();
};

/**
 *  画当前工具的光标（第 33 条：光标变成当前工具的样子，笔尖带当前颜色）：画笔、铅笔、毛笔为斜放的笔，笔尖为当前颜色、
 *  热点在笔尖；荧光笔、马克笔为粗头笔；激光笔为一个发亮的圆点；直线、折线、曲线、箭头、形状、块状箭头为十字，中心是热点；
 *  文字为 I 形；整笔橡皮与局部橡皮为橡皮块；数字标号为带当前颜色的小圆。
 *  @param tool   工具：AnnotationTool 之一或 kAnnotEraser（会话类的整笔橡皮）
 *  @param color  当前颜色 0x00RRGGBB
 *  @param sizePx 边长（像素），不小于 16
 */
AnnotCursorImage RenderToolCursor(int tool, uint32_t color, int sizePx);

/**
 *  按当前工具与颜色建一个光标（第 21 期从屏幕共享控制器抽出，控制器与本机标注共用）：图像见 RenderToolCursor，边长随系统光标大小。
 *  @param tool  工具：AnnotationTool 之一或 kAnnotEraser
 *  @param color 当前颜色 0x00RRGGBB
 *  @return 新建的光标，调用方用 DestroyIcon 释放；建不出来时返回 NULL
 */
HCURSOR CreateAnnotationCursor(int tool, uint32_t color);

/**
 *  把别人的画笔位置画到图层上（第 33 条：其他参与绘制的人的画笔位置实时显示并标名字）：每人一个当前颜色的圆点，右下方一个
 *  同色底、白字的名字标签。在 RenderAnnotationLayer 之后调用，画在笔迹上面。
 *  @param width、height 图层尺寸
 *  @param pointers      别人的画笔位置
 *  @param names         与 pointers 一一对应的显示名；短于 pointers 时缺的名字不画标签
 *  @param bgra          [出入参] 图层（预乘 alpha）
 */
void RenderAnnotationPointers(int width, int height, const std::vector<AnnotationPointer>& pointers,
                              const std::vector<std::wstring>& names, std::vector<uint8_t>& bgra);

/**
 *  同 DrawAnnotations，另把别人的画笔位置画在笔迹上面（第 19 期）。
 */
void DrawAnnotationsWithPointers(HDC hdc, const RECT& content, const std::vector<AnnotationStroke>& strokes,
                                 const AnnotationStroke* pending, bool colorKeyTarget, int64_t nowMs,
                                 const std::vector<AnnotationPointer>& pointers, const std::vector<std::wstring>& names);

struct AnnotSelectionVisual;

/**
 *  把选择工具的提示画到图层上（第 20 期）：每个选中对象外接矩形外扩几个像素的蓝色虚线框；单选时的句柄（外接矩形的八个为
 *  白底蓝边的小方块，直线类两端的为小圆）；正在拖的选择框（浅蓝半透明填充加蓝边）。在 RenderAnnotationLayer 之后调用。
 *  @param width、height 图层尺寸（共享内容的显示尺寸）
 *  @param v             选择提示（归一化坐标）
 *  @param bgra          [出入参] 图层（预乘 alpha）
 */
void RenderAnnotationSelection(int width, int height, const AnnotSelectionVisual& v, std::vector<uint8_t>& bgra);

/**
 *  按可见区域画笔迹与选择提示（第 21 期，白板用）：图层只有可见区域那么大，笔迹按 content（世界在可见区域坐标里的矩形，可以比
 *  可见区域大、左上可以为负）换算，看不见的部分不画。
 *  @param viewW、viewH 可见区域（图层）的像素尺寸
 *  @param content      世界在可见区域坐标里的矩形
 *  @param strokes、pending、nowMs、antialias 同 RenderAnnotationLayer
 *  @param selection    选择提示；为 NULL 时不画
 *  @param bgra         [出参] 图层，大小为 viewW × viewH × 4，预乘 alpha
 */
void RenderAnnotationView(int viewW, int viewH, const RECT& content, const std::vector<AnnotationStroke>& strokes,
                          const AnnotationStroke* pending, int64_t nowMs, bool antialias,
                          const AnnotSelectionVisual* selection, std::vector<uint8_t>& bgra);

/**
 *  把 RenderAnnotationView 的结果合成到 hdc 的 view 矩形上（第 21 期，白板用；不是颜色键窗口）。
 *  @param view    可见区域在 hdc 上的矩形
 *  @param content 世界在 hdc 坐标里的矩形
 */
void DrawAnnotationsInView(HDC hdc, const RECT& view, const RECT& content, const std::vector<AnnotationStroke>& strokes,
                           const AnnotationStroke* pending, int64_t nowMs, const AnnotSelectionVisual* selection);

/**
 *  同 DrawAnnotationsWithPointers，另把选择工具的提示画在最上面（第 20 期）。
 *  @param selection 选择提示；为 NULL 时不画
 */
void DrawAnnotationsWithSelection(HDC hdc, const RECT& content, const std::vector<AnnotationStroke>& strokes,
                                  const AnnotationStroke* pending, bool colorKeyTarget, int64_t nowMs,
                                  const std::vector<AnnotationPointer>& pointers, const std::vector<std::wstring>& names,
                                  const AnnotSelectionVisual* selection);

/**
 *  画出标注会话里某块画面的内容（第 22 期 22e，发起人的标注层、观看窗的各块画面共用）：这块画面上的笔迹、别人的画笔位置
 *  与名字；本人当前画面就是它时，再画本人正在画的预览与选中框。
 *  @param hdc            目标设备
 *  @param content        画面在 hdc 上所占的矩形
 *  @param session        标注会话
 *  @param canvas         画面座位
 *  @param colorKeyTarget 目标是否为颜色键分层窗口（见 DrawAnnotations）
 *  @param nowMs          本机当前时刻（毫秒），激光笔据此淡出
 */
void DrawSessionCanvas(HDC hdc, const RECT& content, const ShareAnnotationSession& session, int canvas,
                       bool colorKeyTarget, int64_t nowMs);

/**
 *  按可见区域画出标注会话里某块画面的内容（第 23 期白板：世界可能比可见区域大很多，只按可见区域生成图层）。
 *  内容与 DrawSessionCanvas 相同：这块画面上的笔迹、别人的画笔位置与名字，本人当前画面是它时再画预览与选中框。
 *  @param hdc     目标设备
 *  @param view    可见区域在 hdc 上的矩形，只画这里面
 *  @param content 整个画面（白板的世界）在 hdc 上的矩形，可以比 view 大、左上可以为负
 *  @param session 标注会话
 *  @param canvas  画面座位
 *  @param nowMs   本机当前时刻（毫秒）
 */
void DrawSessionCanvasInView(HDC hdc, const RECT& view, const RECT& content, const ShareAnnotationSession& session,
                             int canvas, int64_t nowMs);

} // namespace screenshare
