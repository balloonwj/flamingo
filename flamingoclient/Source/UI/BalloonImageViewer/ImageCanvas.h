/**
 *  ImageCanvas.h
 *
 *  看图窗的图片画布 —— 自绘的 balloonui DuiControl 子类。在深色背景上等比
 *  绘制一张图片，支持缩放（滚轮 / 按钮 / 双击适应↔100%）、顺时针旋转 90°、
 *  按住拖动平移。所有缩放 / 旋转 / 平移的数值运算都委托给 ImageViewerGeometry
 *  里的纯函数（已有单测覆盖），本控件只负责把结果落到 GDI+ 绘制与鼠标交互。
 *
 *  控件本身不拥有 HWND（balloonui DUI 节点的不变量），所有坐标默认是宿主
 *  客户区坐标；内部把它换算成"画布局部坐标"（画布左上角为原点）再交给几何
 *  纯函数。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../BalloonChatWnd/ChatClipboardContent.h"   // ChatClipSegment（复制图片时写进剪贴板的聊天内容片段）

#include <vector>

// 只用到 Gdiplus::Bitmap 的指针，前置声明以免在头文件里拉入沉重的 <gdiplus.h>。
namespace Gdiplus { class Bitmap; }
// 只用到 DuiToast 的指针（复制成功的轻提示）。
namespace balloonwjui { class DuiToast; }

namespace balloonimageviewer {

class ImageViewerToolbar;

/**
 *  算出看图窗「复制图片」要写进剪贴板的聊天内容片段。纯函数（只查询文件是否存在）。
 *
 *  复制与聊天气泡右键「复制」走同一条路：把图片按本地文件路径写成一段图片片段，聊天
 *  输入框粘贴时据此把它插成一张可发送的图片（见 ChatInputRichEdit::TryPasteChatContent）。
 *    sourcePath：看图窗当前图片的源文件路径。
 *    hasImage：  画布上是否已成功载入图片（解码失败时为 false）。
 *  返回：恰好一段图片片段；画布上没有图、路径为空、或源文件已经不存在时返回空序列
 *        —— 不能把一个指向不存在文件的路径写进剪贴板，那样粘贴后发送会失败。
 */
std::vector<balloonchatwnd::ChatClipSegment> BuildImageViewerCopySegments(const CString& sourcePath,
                                                                          bool hasImage);

// 看图画布控件。
class ImageCanvas : public balloonwjui::DuiControl
{
public:
    // 单击了图片以外的空白处时的回调（只看不存模式的大图浮层据此关闭）。ctx 为 SetBlankClickHandler 传入的上下文
    typedef void (*BlankClickFn)(void* ctx);

    // 构造。
    //   viewOnly：「只看不存」模式（密聊图片的大图浮层，docs/密聊方案.md 4.10）：工具栏只有缩放、1:1、适应窗口与
    //             旋转，没有复制、另存与识别文字；图片不可平移时按住拖动不移动宿主窗口。构造后不变。
    explicit ImageCanvas(bool viewOnly = false);
    ~ImageCanvas() override;

    // 从本地文件加载图片，并重置为"适应窗口"居中显示。解码失败则清空当前图
    //（画布只剩深色背景）。
    //   path：图片本地完整路径（UTF-16，非空）。
    //   返回：解码成功返回 true，失败返回 false。
    bool LoadImageFromFile(LPCTSTR path);

    // 从内存里的图片文件字节（PNG / JPEG 等）加载图片，并重置为"适应窗口"居中显示。解码后复制成一张独立的位图，
    // 不保留对 bytes 的引用，调用方随后可以清零自己的缓冲区。没有源文件路径，所以另存、复制图片、识别文字都不可用。
    //   bytes：整张图片文件的字节；为空时清空当前图并返回 false。
    //   返回：解码成功返回 true。
    bool LoadImageFromMemory(const std::vector<unsigned char>& bytes);

    // 登记单击空白处的回调：按下后没有拖动就松开、且按下点不在图片上时调用（工具栏上的点击由工具栏自己处理，不会走到这里）。
    //   fn：回调，传 NULL 取消；ctx：原样传回的上下文（借用，不拥有）。
    void SetBlankClickHandler(BlankClickFn fn, void* ctx);

    // 当前是否已成功载入一张图片。
    bool HasImage() const;

    // ---- 看图动作（增量 3 由底部工具栏按钮调用）----

    // 放大 / 缩小一档（以画布中心为锚点）。
    void ZoomIn();
    void ZoomOut();
    // 切到"适应窗口"（等比放进画布、只缩不放），并居中。
    void ZoomToFit();
    // 切到 100%（原始像素 1:1），以画布中心为锚点。
    void ZoomToActual();
    // 顺时针 / 逆时针旋转 90°，旋转后按新朝向重新适应窗口。
    void RotateClockwise();
    void RotateCounterClockwise();

    // 另存为：弹保存对话框，把当前图片的源文件另存到所选路径（按原文件字节拷贝，
    // 无损、保持原格式）。无图或无源路径时静默返回。由底部工具栏「另存为」调用。
    void SaveImageAs();

    // 当前图片可复制的内容（见 BuildImageViewerCopySegments）；无图或源文件已不存在时为空。
    std::vector<balloonchatwnd::ChatClipSegment> BuildCopySegments() const;

    // 复制图片：把当前图片写进剪贴板（Flamingo 聊天内容格式 + 位图 + 纯文字「[图片]」），
    // 之后可以直接粘进聊天输入框发送，也可以粘到画图等程序。由底部工具栏「复制图片」调用。
    // 副作用：清空并改写系统剪贴板；成功时在看图窗顶部弹「已复制」轻提示，没有可复制的
    // 内容或剪贴板被占用时弹失败提示。
    void CopyImage();

    // 图片文字识别：按当前旋转方向识别源文件里的文字，打开识别结果窗口（经 ocr::LaunchOcrForFile，
    // 系统不支持或没有识别语言时弹提示）。无图或无源路径时静默返回。由底部工具栏「图片文字识别」调用，
    // 该按钮只在图片文字识别模块可用时出现（docs/图片文字识别方案.md 第 8.5 节）。
    void RecognizeText();

    // 当前缩放比的百分数读数（形如 "96%"），供工具栏显示。
    CString GetZoomReadout() const;

    // ---- DuiControl 覆写：绘制与鼠标交互 ----
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    bool OnMouseMove(POINT pt, UINT mkFlags) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;
    void Layout(const RECT& rcAvail) override;

private:
    // 画布可视区尺寸（= 控件矩形尺寸）。
    SIZE ViewportSize() const;
    // 当前图片在当前旋转下、未缩放的"显示尺寸"（90/270 时宽高对调）。
    SIZE DisplaySize() const;
    // 当前缩放比缩放后的图片在屏矩形尺寸（= DisplaySize × m_scale，四舍五入）。
    SIZE ScaledBoxSize() const;
    // 把宿主客户区坐标点换算成画布局部坐标（画布左上角为原点）。
    POINT ToLocal(POINT ptHost) const;
    // 把 m_origin 经 ClampImageOrigin 夹取 / 居中（小图锁定居中）。
    void ClampOrigin();
    // 以锚点 anchorLocal（画布局部坐标）把缩放比设为 newScale、修正落点后刷新。
    void ApplyZoomAtAnchor(double newScale, POINT anchorLocal);
    // 图片是否在任一维大于视口（即可平移）。无图时为 false。
    bool IsPannable() const;

    // 宿主客户区坐标点是否落在当前显示的图片上（按缩放、平移后的在屏矩形算）。无图时为 false。
    bool IsPointOnImage(POINT ptHost) const;

    // 把底部悬浮工具栏摆到画布底部中央（在 Layout 里调用）。
    void PositionToolbar();

    // 把这次按住左键的拖动交给系统原生的窗口移动（等同拖动标题栏：贴边分屏、最大化时
    // 拖动先还原，都与标题栏一致）。先释放本控件的鼠标捕获，再向宿主窗口发
    // WM_NCLBUTTONDOWN + HTCAPTION；该消息在移动结束（松开左键）之后才返回。
    //   ptHost：当前鼠标位置（宿主客户区坐标）。开始移动前先把窗口挪过"按下点到当前点"
    //           这段距离，按下处始终在光标下方。
    void StartWindowMove(POINT ptHost);

    // 按给定增量旋转（deltaDegreesCW 为顺时针度数，可负），归一到 [0,360)，
    // 再按新朝向重新适应窗口并刷新。RotateClockwise/CounterClockwise 的共用实现。
    void ApplyRotation(int deltaDegreesCW);

private:
    Gdiplus::Bitmap* m_image;       // 解码后的图片位图（本类持有，析构 / 重载时释放）；NULL = 无图
    int      m_imgW;                // 原始（未旋转）图片像素宽
    int      m_imgH;                // 原始（未旋转）图片像素高
    int      m_rotation;            // 旋转角，取 0 / 90 / 180 / 270（顺时针累加并归一）
    double   m_scale;               // 当前缩放比（1.0 = 100%）
    POINT    m_origin;              // 缩放后图片在屏矩形左上角（画布局部坐标），平移即改它
    bool     m_fitOnLayout;         // 是否处于"适应窗口"态：为真时窗口尺寸变化会跟随重新适应
    bool     m_dragging;            // 是否正在按住左键拖动平移
    POINT    m_dragLastPt;          // 上一次拖动鼠标点（宿主客户区坐标），用于算平移增量
    bool     m_pressed;             // 左键是否按在画布上、尚未松开且还没开始移动窗口。图片不可平移时，
                                    // 移出系统拖动阈值后据此开始移动窗口；松开或开始移动窗口时清除
    POINT    m_pressPt;             // 左键按下时的位置（宿主客户区坐标），判断是否移出拖动阈值用
    CString  m_sourcePath;          // 当前图片的源文件路径（另存为、复制图片用）；空表示无图
    ImageViewerToolbar* m_pToolbar; // 底部悬浮工具栏（子控件，所有权在控件树）
    balloonwjui::DuiToast* m_pToast; // 顶部轻提示（复制成功后显示「已复制」；子控件，所有权在控件树）
    bool     m_viewOnly;            // 是否「只看不存」模式（构造时决定，之后不变），含义见构造函数
    BlankClickFn m_blankClickFn;    // 单击空白处的回调；NULL 表示没有登记
    void*    m_blankClickCtx;       // 上面回调的上下文（借用，不拥有）
};

} // namespace balloonimageviewer
