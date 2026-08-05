/**
 *  CaptureCanvas.h
 *
 *  截图浮层的画布控件（balloonui DuiControl 子类，自绘）。铺满整个全屏遮罩窗口，
 *  负责：①把冻结的整桌面位图作背景画出来；②在选区<u>之外</u>蒙一层半透明暗色；
 *  ③橡皮筋框选（按下拖拽抬起，反向拖拽自动归一）；④画选区边框 + 实时尺寸读数；
 *  ⑤选区成形后在其旁弹出操作工具栏（重选 / 复制 / 保存 / 发送 / 取消，复用 balloonui
 *  的 DuiButton），工具栏作为本画布的子控件。
 *
 *  纯几何运算（归一 / 夹取 / 最小尺寸 / 工具栏落点）一律走 ScreenshotGeometry，
 *  本控件只管绘制与交互。选区坐标即"宿主客户区坐标"，而遮罩窗口创建在虚拟桌面原点，
 *  故选区坐标同时也是冻结位图的像素坐标，裁剪时可直接使用。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#pragma once

#include "../../balloonui/balloonui/DuiControl.h"
#include "../../balloonui/balloonui/Controls/Input/DuiEditHost.h"  // 文字标注复用的多行编辑框
#include "AnnotationModel.h"      // 标注数据模型（撤销重做栈 / 工具状态 / 箭头几何）
#include "ScreenshotGeometry.h"   // SelHitZone（选区移动 / 缩放命中区）
#include "ScreenshotToolbar.h"    // ITipSink（按钮悬停提示接收方接口）

namespace balloonscreenshot {

// 操作工具栏各按钮的控件 id。遮罩窗口（ScreenCaptureOverlay）在 WM_DUI_NOTIFY 里
// 按这些 id 区分被点的按钮。本浮层是 DuiHost（无标题栏），不受 DuiFrameWindow 标题栏
// 占用 1/2/3 的限制，但仍从 ≥10 起编以求稳妥。
enum
{
    kIdToolReselect = 10,   // 重选：清掉当前选区、回到可重新框选状态
    kIdToolCopy     = 11,   // 复制：把选区图复制到剪贴板
    kIdToolSave     = 12,   // 保存：把选区图存成 PNG 文件
    kIdToolStage    = 13,   // 发送到对话框：把选区图内联插入聊天输入框（不立即发）
    kIdToolSend     = 14,   // 发送：把选区图作为图片消息立即发到当前会话
    kIdToolCancel   = 15,   // 取消：放弃截图、关闭浮层
};

// 标注工具栏各控件的 id（第二期）。同样由遮罩窗口在 WM_DUI_NOTIFY 里收到后转交本画布
// HandleToolbarNotify 处理。颜色 swatch 占 kIdAnnColor0 起的连续 9 个。
enum
{
    kIdAnnSelect    = 19,   // 工具：选择 / 移动（回到默认模式，可拖动 / 缩放选区）
    kIdAnnRect      = 20,   // 工具：矩形
    kIdAnnEllipse   = 21,   // 工具：椭圆
    kIdAnnArrow     = 22,   // 工具：箭头
    kIdAnnPen       = 23,   // 工具：画笔
    kIdAnnUndo      = 24,   // 撤销
    kIdAnnRedo      = 25,   // 重做
    kIdAnnMosaic    = 26,   // 工具：马赛克（矩形框）
    kIdAnnMosaicBrush = 27, // 工具：马赛克（画笔涂抹）
    kIdAnnText      = 28,   // 工具：文字（第二期 b，按钮下一轮接入）

    kIdAnnColor0    = 30,   // 颜色 swatch 起始 id（kIdAnnColor0 .. kIdAnnColor0+8）

    kIdAnnWidthThin  = 40,  // 线宽：细
    kIdAnnWidthMed   = 41,  // 线宽：中
    kIdAnnWidthThick = 42,  // 线宽：粗
};

// 标注调色板的颜色个数。
const int kAnnColorCount = 9;

// 画布双击确认的通知 ctrlId：有可用选区时在选区内双击，画布以本 id 发 DUIN_CLICK，
// 遮罩窗口收到后等同于点了「发送到对话框」。用一个不与工具栏冲突的值。
const UINT kIdCanvasDblConfirm = 200;

// 截图画布控件。除作 balloonui 自绘控件外，还实现 ITipSink：工具栏按钮悬停时
// 回调本画布，由本画布在按钮正下方自绘提示框（不动用全局 DuiToolTipMgr）。
class CaptureCanvas : public balloonwjui::DuiControl, public ITipSink
{
public:
    CaptureCanvas();
    ~CaptureCanvas() override;

    // ---- ITipSink 实现 ----
    // 在 anchorRect（某按钮的客户区矩形）正下方显示提示文字，越界时夹回窗口内。
    void    ShowTip(const RECT& anchorRect, LPCTSTR text) override;
    // 隐藏提示。
    void    HideTip() override;

    // 设置冻结的整桌面背景位图（截图发起瞬间抓取的全虚拟桌面）。所有权<u>不</u>转移，
    // 由遮罩窗口持有；本控件只借来做背景与裁剪源。须在加入控件树后、显示前调一次。
    //   hbmp：冻结桌面位图（DDB），与遮罩窗口客户区等大。
    void    SetFrozenBitmap(HBITMAP hbmp);

    // 当前是否有"可用选区"（已框选且宽高达到最小阈值）。
    bool    HasUsableSelection() const { return m_hasSel; }

    // 取当前选区矩形（宿主客户区坐标 = 冻结位图像素坐标）。仅 HasUsableSelection()
    // 为 true 时有意义。
    RECT    GetSelectionRect() const { return m_sel; }

    // 清除选区、隐藏工具栏、清空标注，回到"等待重新框选"状态（对应工具栏「重选」）。
    void    ClearSelection();

    // ---- 文字标注编辑（供遮罩窗口在提交动作 / Esc 时驱动）----
    // 当前是否正在编辑一个文字框。
    bool    IsTextEditing() const { return m_textEditing; }
    // 提交当前文字框：取出文字，非空则落成一个文字标注，并移除编辑框。无编辑时为 no-op。
    void    CommitTextEdit();
    // 取消当前文字框：丢弃文字、移除编辑框。无编辑时为 no-op。
    void    CancelTextEdit();

    // 把文字编辑框的高度长到刚好容下当前内容（按 EDIT 实际行数 × 行高），高度上限为
    // 选区底。每次文字变化（遮罩窗口收到 DUIN_VALUECHANGED）时调，使所有文字可见、
    // 且不溢出（从而不出现竖直滚动条）。无编辑时为 no-op。
    void    GrowTextEditToFit();

    // 遮罩窗口把"非操作栏"的工具栏通知（标注工具 / 颜色 / 线宽 / 撤销重做）转交本方法。
    //   ctrlId：被点控件的 id。
    //   返回：true 表示已识别并处理（更新状态 + 重绘）；false 表示不认识该 id。
    bool    HandleToolbarNotify(UINT ctrlId);

    // 把已提交的标注烤进裁剪结果位图（供遮罩窗口 CropSelection 用）。标注以宿主客户区
    // 坐标记录，这里按选区左上角平移到裁剪图本地坐标后绘制。
    //   dstDc：裁剪图的 DC（已选入目标位图）；
    //   sel：  选区矩形（客户区坐标）。
    void    BakeAnnotationsInto(HDC dstDc, const RECT& sel);

    // ---- DuiControl 覆写 ----
    void    OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool    OnLButtonDown(POINT pt, UINT mkFlags) override;
    bool    OnMouseMove (POINT pt, UINT mkFlags) override;
    bool    OnLButtonUp (POINT pt, UINT mkFlags) override;
    bool    OnLButtonDblClk(POINT pt, UINT mkFlags) override;
    bool    OnSetCursor (POINT pt) override;
    void    Layout(const RECT& rcAvail) override;

private:
    // 创建操作工具栏（5 个 balloonui 按钮）作为本画布子控件，初始隐藏。
    void    BuildToolbar();
    // 按当前选区把工具栏摆到选区旁（ScreenshotGeometry::PlaceActionBar）并显示。
    void    PositionAndShowToolbar();
    // 画选区外的半透明暗色蒙版（选区内保持原桌面亮度）。
    void    PaintDimMask(HDC hdc, const RECT& client);
    // 画选区边框 + 尺寸读数气泡。
    void    PaintSelectionDecor(HDC hdc);
    // 画悬停提示框（深色圆底 + 白字，落在 m_tipAnchor 正下方，水平居中、夹进窗口）。
    void    PaintTooltip(HDC hdc);

    // ---- 标注（第二期）----
    // 创建标注工具栏（工具 / 颜色 / 线宽 / 撤销重做）作为子控件，初始隐藏。
    void    BuildAnnotationToolbar();
    // 画所有已提交标注（withDraft=true 时连同正在拖的草稿一起），平移 (dx,dy) 后绘制。
    void    PaintAnnotations(HDC hdc, int dx, int dy, bool withDraft);
    // 画单个标注（坐标已加偏移 (dx,dy)）。
    void    DrawOneAnnotation(HDC hdc, const Annotation& a, int dx, int dy);

    // ---- 马赛克渲染（采样冻结桌面位图 m_frozen；调用时 m_frozen 须为空闲未占用）----
    // 生成 frozenRect 区域的"块状化"全分辨率位图（先 HALFTONE 缩小再最近邻放大），
    // 与 frozenRect 等大；调用方负责 DeleteObject。失败返回 NULL。
    //   refDc：     兼容 DC 来源（一般即目标 hdc）；
    //   frozenRect：冻结位图内的源矩形（客户区坐标 = 冻结像素坐标）；
    //   block：     马赛克块边长（像素）。
    HBITMAP BuildMosaicBitmap(HDC refDc, const RECT& frozenRect, int block);
    // 矩形马赛克：把 frozenRect 区域打码后画到 hdc 的 frozenRect+(dx,dy) 处。
    void    DrawMosaicRect(HDC hdc, const RECT& frozenRect, int dx, int dy, int block);
    // 画笔马赛克：沿 a.points 路径逐点盖块状马赛克方块（块取自路径包围盒的马赛克位图）。
    void    DrawMosaicBrush(HDC hdc, const Annotation& a, int dx, int dy);

    // ---- 文字标注 ----
    // 在 pt 处开一个多行文字编辑框（复用 balloonui DuiEditHost，挂为本画布子控件）。
    void    BeginTextEdit(POINT pt);
    // 画一条已提交的文字标注（GDI DrawText，微软雅黑，按 start→end.x 的宽度自动换行）。
    void    DrawTextAnnotation(HDC hdc, const Annotation& a, int dx, int dy);
    // 刷新工具 / 颜色 / 线宽按钮的"选中"视觉（高亮当前选中项）。
    void    RefreshToolbarActiveState();
    // 把点夹进当前选区（标注不画到选区外）。
    POINT   ClampToSelection(POINT pt) const;
    // 当前是否处于"画标注"模式（有可用选区且选了某个标注工具）。
    bool    InAnnotationMode() const;

private:
    // 冻结桌面位图（借用，不持有 / 不删）。注意：<u>不</u>把它长期 SelectObject 进任何 DC——
    // 否则遮罩窗口裁剪选区时 SelectObject(m_frozen) 会因"同一位图不能同时选进两个 DC"
    // 而失败、裁出黑块。绘制时临时选进一个 DC、画完即释放。
    HBITMAP m_frozen;
    HBITMAP m_dimDib;            // 1x1 黑色位图，AlphaBlend 蒙暗用（本控件创建 / 销毁）

    POINT   m_anchor;            // 拖拽锚点（按下点，客户区坐标）
    RECT    m_sel;               // 当前选区（归一 + 夹取后，客户区坐标）
    bool    m_dragging;          // 是否正在拖拽框选
    bool    m_hasSel;            // 是否已有可用选区（宽高达阈值）

    balloonwjui::DuiControl* m_toolbar;   // 操作工具栏容器（子控件，所有权在控件树）
    SIZE    m_barSize;           // 操作工具栏整体尺寸（像素），供落点计算

    // ---- 标注（第二期）----
    AnnotationStore          m_annotations;  // 已提交标注 + 撤销/重做栈
    AnnotationToolState      m_toolState;    // 当前工具 / 颜色 / 线宽
    Annotation               m_draft;        // 正在拖拽绘制的草稿标注
    bool                     m_drawingAnn;   // 是否正在拖拽画标注
    balloonwjui::DuiControl* m_annToolbar;   // 标注工具栏容器（子控件，所有权在控件树）
    SIZE                     m_annBarSize;   // 标注工具栏整体尺寸（像素）

    // ---- 选区移动 / 缩放（默认模式：未选标注工具时，选区内拖动=移动、拖边/角=缩放）----
    bool       m_movingSel;    // 是否正在移动 / 缩放选区
    SelHitZone m_dragZone;     // 当前拖拽命中区（kSelInside=移动；边 / 角=缩放）
    POINT      m_lastDragPt;   // 上次拖拽点（增量计算位移）

    // ---- 悬停提示（自绘，显示在按钮正下方；ITisSink 回调驱动）----
    bool    m_tipVisible;      // 当前是否有提示要画
    RECT    m_tipAnchor;       // 触发提示的按钮矩形（客户区坐标），提示画在其正下方
    CString m_tipText;         // 提示文字

    // ---- 文字标注编辑态 ----
    bool                  m_textEditing;  // 是否正在编辑文字框
    balloonwjui::DuiEditHost* m_textEdit; // 编辑框（加入控件树后由树持有，裸指针访问；非编辑时 NULL）
    RECT                  m_textBox;      // 编辑框矩形（客户区坐标）；左上=文字锚点、right=换行宽度边界
    COLORREF              m_textColor;    // 本次文字颜色（开编辑时取当前调色板色）
    int                   m_textFontPx;   // 本次文字字号（像素高，开编辑时由线宽档位换算）
};

} // namespace balloonscreenshot
