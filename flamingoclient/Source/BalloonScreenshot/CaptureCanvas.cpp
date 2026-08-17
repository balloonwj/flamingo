/**
 *  CaptureCanvas.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-06-04
 */
#include "stdafx.h"
#include "CaptureCanvas.h"
#include "ScreenshotGeometry.h"
#include "ScreenshotToolbar.h"   // 深色风格工具栏控件（DarkBar / IconButton / ColorDot / WidthDot）
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // DUIN_CLICK（双击确认通知）
#include "../../../third-party/balloonui/balloonui/DuiHost.h"     // GetHost()->m_hWnd（文字标注结束后把键盘焦点交回宿主窗口）

#include <gdiplus.h>             // 标注用 GDI+ 抗锯齿绘制

// AlphaBlend 在 msimg32 里。
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace balloonscreenshot {

namespace {

// ---- 操作工具栏（深色条）布局常量 ----
const int kActItemH    = 30;   // 操作项高
const int kActPad      = 8;    // 条内边距
const int kActGap      = 6;    // 项间距
const int kActGroupGap = 14;   // 「发送到对话框」前的分组间距
const int kActNormalW  = 74;   // 2 字操作项宽（含小图标 + 文字）
const int kActStageW   = 132;  // 「发送到对话框」宽（含小图标 + 文字）
const int kBarGap      = 8;    // 工具栏与选区 / 两条之间间距

// 操作项描述（顺序即从左到右；stage 为橙色主按钮、其前留分组间距）。
struct ActionItem
{
    UINT           id;
    const wchar_t* label;
    ToolIconKind   icon;
    const wchar_t* tip;
    int            width;
    bool           primary;
    bool           groupGapBefore;
};
const ActionItem kActionItems[] =
{
    { kIdToolReselect, L"重选",         kIconReselect, L"重新框选",          kActNormalW, false, false },
    { kIdToolCopy,     L"复制",         kIconCopy,     L"复制到剪贴板",      kActNormalW, false, false },
    { kIdToolSave,     L"保存",         kIconSave,     L"保存为图片",        kActNormalW, false, false },
    { kIdToolStage,    L"发送到对话框", kIconStage,    L"把截图插入聊天输入框", kActStageW, true,  true  },
    { kIdToolSend,     L"发送",         kIconSend,     L"立即发送到当前会话", kActNormalW, false, false },
    { kIdToolCancel,   L"取消",         kIconCancel,   L"取消截图（Esc）",   kActNormalW, false, false },
};
const int kActionCount = (int)(sizeof(kActionItems) / sizeof(kActionItems[0]));

// ---- 标注工具栏（深色条）布局常量 ----
const int kAnnItemH    = 28;   // 标注项高
const int kAnnPad      = 8;    // 条内边距
const int kAnnToolW    = 30;   // 工具图标按钮宽
const int kAnnDotW     = 18;   // 色点直径
const int kAnnWidthW   = 30;   // 线宽样条宽
const int kAnnUndoW    = 30;   // 撤销 / 重做宽
const int kAnnIntraGap = 5;    // 组内间距
const int kAnnGroupGap = 12;   // 组间间距
const int kAnnToolCount  = 8;  // 工具个数（选择 / 矩形 / 椭圆 / 箭头 / 画笔 / 马赛克框 / 马赛克刷 / 文字）
const int kAnnWidthCount = 3;  // 线宽档位个数

// 调色板 9 色（设计稿顺序：红橙黄绿青蓝紫黑白）。
const COLORREF kPalette[kAnnColorCount] =
{
    RGB(0xFF, 0x4D, 0x4F), RGB(0xFF, 0x8C, 0x00), RGB(0xFF, 0xD7, 0x00),
    RGB(0x2B, 0xB6, 0x73), RGB(0x00, 0xBC, 0xD4), RGB(0x2B, 0x6F, 0xFF),
    RGB(0x8B, 0x5C, 0xF6), RGB(0x00, 0x00, 0x00), RGB(0xFF, 0xFF, 0xFF),
};

// 三档线宽（细 / 中 / 粗，像素）及对应控件 id。
const int  kWidthThin  = 2;
const int  kWidthMed   = 4;
const int  kWidthThick = 6;
const int  kAnnWidths[kAnnWidthCount]   = { kWidthThin, kWidthMed, kWidthThick };
const UINT kAnnWidthIds[kAnnWidthCount] = { kIdAnnWidthThin, kIdAnnWidthMed, kIdAnnWidthThick };
const wchar_t* const kAnnWidthTips[kAnnWidthCount] = { L"细线", L"中等线", L"粗线" };

// 工具的图标种类 / id / tooltip（顺序一致）。第一个是"选择 / 移动"，回到默认模式。
const ToolIconKind   kAnnToolIcons[kAnnToolCount] =
    { kIconCursor, kIconRect, kIconEllipse, kIconArrow, kIconPen, kIconMosaic, kIconMosaicBrush, kIconText };
const UINT           kAnnToolIds[kAnnToolCount]   =
    { kIdAnnSelect, kIdAnnRect, kIdAnnEllipse, kIdAnnArrow, kIdAnnPen, kIdAnnMosaic, kIdAnnMosaicBrush, kIdAnnText };
const wchar_t* const kAnnToolTips[kAnnToolCount]  =
    { L"选择 / 移动（拖动或缩放选区）", L"矩形", L"椭圆", L"箭头", L"画笔",
      L"马赛克（拖框打码）", L"马赛克（涂抹打码）", L"文字（点击输入）" };

// 文字编辑框（DuiEditHost）的控件 id —— 取一个不与工具栏 / 颜色 / 线宽冲突的值；
// 其失焦 / 值变通知（非 DUIN_CLICK）会冒到遮罩窗口但被忽略，互不干扰。
const UINT kIdTextEdit    = 60;
const int  kTextInnerPad  = 4;    // 文字框初始一行高的内边距估算（像素，随后被 GrowTextEditToFit 校正）
const int  kTextMinWidth  = 80;   // 文字框最小换行宽度（像素）
const int  kTextEdgeGap   = 6;    // 文字框右 / 下与选区边的留白（像素）
const int  kTextEditVChrome = 6;  // 文字框竖直方向"框比文字多出来"的量（上下边框 + 少量留白）

// 选区 / 蒙版 / 边框 常量。
const int      kMinSelW     = 4;
const int      kMinSelH     = 4;
const BYTE     kDimAlpha    = 120;
const COLORREF kBorderColor = RGB(0x2B, 0x6F, 0xFF);
const int      kBorderWidth = 2;

// 选区移动 / 缩放：边 / 角命中容差，与手柄方块大小。
const int kHandleTol  = 8;    // 鼠标距边 / 角 ≤ 此值算命中缩放
const int kHandleSize = 8;    // 手柄方块边长（像素）
const int kMoveMinW   = 12;   // 缩放时选区最小宽（比框选阈值大些，便于操作）
const int kMoveMinH   = 12;   // 缩放时选区最小高

// 工具 id ↔ 标注类型 的互转。
AnnotationTool ToolForId(UINT id)
{
    switch (id)
    {
    case kIdAnnRect:        return kAnnRect;
    case kIdAnnEllipse:     return kAnnEllipse;
    case kIdAnnArrow:       return kAnnArrow;
    case kIdAnnPen:         return kAnnPen;
    case kIdAnnMosaic:      return kAnnMosaic;
    case kIdAnnMosaicBrush: return kAnnMosaicBrush;
    case kIdAnnText:        return kAnnText;
    default:               return kAnnNone;
    }
}

UINT IdForTool(AnnotationTool t)
{
    switch (t)
    {
    case kAnnRect:        return kIdAnnRect;
    case kAnnEllipse:     return kIdAnnEllipse;
    case kAnnArrow:       return kIdAnnArrow;
    case kAnnPen:         return kIdAnnPen;
    case kAnnMosaic:      return kIdAnnMosaic;
    case kAnnMosaicBrush: return kIdAnnMosaicBrush;
    case kAnnText:        return kIdAnnText;
    default:             return 0;
    }
}

// 是否"路径型"工具（鼠标拖拽时按经过点采样成自由路径）：画笔、画笔马赛克。
bool IsPathTool(AnnotationTool t)
{
    return t == kAnnPen || t == kAnnMosaicBrush;
}

} // anonymous namespace

CaptureCanvas::CaptureCanvas()
    : m_frozen(NULL)
    , m_dimDib(NULL)
    , m_dragging(false)
    , m_hasSel(false)
    , m_toolbar(NULL)
    , m_drawingAnn(false)
    , m_annToolbar(NULL)
    , m_movingSel(false)
    , m_dragZone(kSelOutside)
    , m_tipVisible(false)
    , m_textEditing(false)
    , m_textEdit(NULL)
    , m_textColor(RGB(0xFF, 0x4D, 0x4F))
    , m_textFontPx(16)
{
    m_anchor.x = 0;
    m_anchor.y = 0;
    m_lastDragPt.x = 0;
    m_lastDragPt.y = 0;
    ::SetRectEmpty(&m_sel);
    ::SetRectEmpty(&m_tipAnchor);
    ::SetRectEmpty(&m_textBox);
    m_barSize.cx = 0;
    m_barSize.cy = 0;
    m_annBarSize.cx = 0;
    m_annBarSize.cy = 0;
    BuildToolbar();            // 计算并设 m_barSize
    BuildAnnotationToolbar();  // 计算并设 m_annBarSize

    // 让画布自身带一个 ctrlId：选区内双击时本控件以此 id 发 DUIN_CLICK，
    // 遮罩窗口据此把"画布双击确认"与各工具栏按钮区分开。
    SetCtrlId(kIdCanvasDblConfirm);
}

CaptureCanvas::~CaptureCanvas()
{
    if (m_dimDib != NULL)
    {
        ::DeleteObject(m_dimDib);
        m_dimDib = NULL;
    }
    // m_frozen 为借用，不在此释放；m_toolbar 及其按钮随控件树析构。
}

void CaptureCanvas::SetFrozenBitmap(HBITMAP hbmp)
{
    // 只记下借用的位图句柄，<u>不</u>把它常驻选进任何 DC（否则裁剪选区时
    // SelectObject(m_frozen) 会因"同一位图不能同时选进两个 DC"失败、裁出黑块）。
    // 绘制时（OnPaint）临时选进一个 DC、画完即释放。
    m_frozen = hbmp;
}

void CaptureCanvas::BuildToolbar()
{
    // 操作栏：深色圆角条 + 文字图标按钮（发送到对话框为橙色主按钮）。
    std::unique_ptr<DarkBar> bar(new DarkBar());
    m_toolbar = bar.get();
    m_toolbar->SetVisible(false);   // 选区成形前不显示
    for (int i = 0; i < kActionCount; ++i)
    {
        std::unique_ptr<IconButton> b(new IconButton());
        b->SetCtrlId(kActionItems[i].id);
        b->SetLabel(kActionItems[i].label);
        b->SetIcon(kActionItems[i].icon);
        if (kActionItems[i].primary)
        {
            b->SetPrimary(true);
        }
        b->SetTip(kActionItems[i].tip);   // 悬停提示由本画布画在按钮正下方
        b->SetTipSink(this);
        m_toolbar->AddChild(std::move(b));
    }
    AddChild(std::move(bar));

    // 整体尺寸：各项宽度 + 间距（stage 前为分组间距）。
    int sum = 0;
    for (int i = 0; i < kActionCount; ++i)
    {
        if (i > 0)
        {
            sum += kActionItems[i].groupGapBefore ? kActGroupGap : kActGap;
        }
        sum += kActionItems[i].width;
    }
    m_barSize.cx = kActPad * 2 + sum;
    m_barSize.cy = kActPad * 2 + kActItemH;
}

void CaptureCanvas::BuildAnnotationToolbar()
{
    // 标注栏：深色圆角条 + 图标工具按钮 | 圆形色点 | 红色线宽样条 | 撤销重做。
    std::unique_ptr<DarkBar> bar(new DarkBar());
    m_annToolbar = bar.get();
    m_annToolbar->SetVisible(false);

    // 工具：选择 / 矩形 / 椭圆 / 箭头 / 画笔（图形图标）。
    for (int i = 0; i < kAnnToolCount; ++i)
    {
        std::unique_ptr<IconButton> b(new IconButton());
        b->SetCtrlId(kAnnToolIds[i]);
        b->SetIcon(kAnnToolIcons[i]);
        b->SetTip(kAnnToolTips[i]);
        b->SetTipSink(this);
        m_annToolbar->AddChild(std::move(b));
    }
    // 颜色：圆形色点（色块本身即提示，不另挂 tooltip）。
    for (int i = 0; i < kAnnColorCount; ++i)
    {
        std::unique_ptr<ColorDot> d(new ColorDot());
        d->SetCtrlId(kIdAnnColor0 + i);
        d->SetColor(kPalette[i]);
        m_annToolbar->AddChild(std::move(d));
    }
    // 线宽：红色样条。
    for (int i = 0; i < kAnnWidthCount; ++i)
    {
        std::unique_ptr<WidthDot> w(new WidthDot());
        w->SetCtrlId(kAnnWidthIds[i]);
        w->SetWidth(kAnnWidths[i]);
        w->SetTip(kAnnWidthTips[i]);
        w->SetTipSink(this);
        m_annToolbar->AddChild(std::move(w));
    }
    // 撤销 / 重做：曲线箭头图标。
    {
        std::unique_ptr<IconButton> u(new IconButton());
        u->SetCtrlId(kIdAnnUndo);
        u->SetIcon(kIconUndo);
        u->SetTip(L"撤销");
        u->SetTipSink(this);
        m_annToolbar->AddChild(std::move(u));
        std::unique_ptr<IconButton> r(new IconButton());
        r->SetCtrlId(kIdAnnRedo);
        r->SetIcon(kIconRedo);
        r->SetTip(L"重做");
        r->SetTipSink(this);
        m_annToolbar->AddChild(std::move(r));
    }
    AddChild(std::move(bar));

    // 整体尺寸（四组：工具 | 颜色 | 线宽 | 撤销重做，组间留 kAnnGroupGap）。
    int toolsW = kAnnToolCount * kAnnToolW + (kAnnToolCount - 1) * kAnnIntraGap;
    int dotsW  = kAnnColorCount * kAnnDotW + (kAnnColorCount - 1) * kAnnIntraGap;
    int widsW  = kAnnWidthCount * kAnnWidthW + (kAnnWidthCount - 1) * kAnnIntraGap;
    int undoW  = 2 * kAnnUndoW + kAnnIntraGap;
    m_annBarSize.cx = kAnnPad * 2 + toolsW + kAnnGroupGap + dotsW + kAnnGroupGap
                      + widsW + kAnnGroupGap + undoW;
    m_annBarSize.cy = kAnnPad * 2 + kAnnItemH;
}

void CaptureCanvas::ClearSelection()
{
    // 重选 = 放弃当前所有内容：若正在输入文字，先丢弃该文字框。
    if (m_textEditing)
    {
        CancelTextEdit();
    }
    m_hasSel = false;
    m_dragging = false;
    m_drawingAnn = false;
    m_movingSel = false;
    ::SetRectEmpty(&m_sel);
    // 重选 = 全新一张：清掉已画的标注，工具退回"未选"（拖拽=重新框选）。
    m_annotations.Clear();
    m_toolState.SetTool(kAnnNone);
    if (m_toolbar != NULL)
    {
        m_toolbar->SetVisible(false);
    }
    if (m_annToolbar != NULL)
    {
        m_annToolbar->SetVisible(false);
    }
    Invalidate();
}

void CaptureCanvas::BeginTextEdit(POINT pt)
{
    if (GetHost() == NULL)
    {
        return;   // 未挂宿主，输入框既拿不到键盘焦点也无从绘制
    }
    // 字号 / 颜色取当前工具状态。
    m_textFontPx = TextFontSizeForWidth(m_toolState.LineWidth());
    m_textColor  = m_toolState.Color();

    // 编辑框矩形：左上=锚点，右界贴选区右（留白）。换行宽度不够则把锚点左移补足，
    // 仍受选区左边约束。
    int right = m_sel.right - kTextEdgeGap;
    if (right - pt.x < kTextMinWidth)
    {
        pt.x = right - kTextMinWidth;
        if (pt.x < m_sel.left + kTextEdgeGap)
        {
            pt.x = m_sel.left + kTextEdgeGap;
        }
    }
    int lineH  = m_textFontPx + kTextInnerPad * 2 + 2;   // 一行高（含上下内边距 + 边框）
    int top    = pt.y;
    int bottom = top + lineH;
    if (bottom > m_sel.bottom - kTextEdgeGap)
    {
        // 锚点太靠下：整体上移以容下一行，仍受选区上边约束。
        top = m_sel.bottom - kTextEdgeGap - lineH;
        if (top < m_sel.top + kTextEdgeGap)
        {
            top = m_sel.top + kTextEdgeGap;
        }
        bottom = top + lineH;
    }
    m_textBox.left   = pt.x;
    m_textBox.top    = top;
    m_textBox.right  = right;
    m_textBox.bottom = bottom;

    // 建一个多行 DuiEditHost 作子控件（复用 balloonui，IME / 光标 / 编辑都现成）。
    std::unique_ptr<balloonwjui::DuiEditHost> edit(new balloonwjui::DuiEditHost());
    edit->SetCtrlId(kIdTextEdit);
    edit->SetMultiLine(true);          // 多行
    edit->SetWordWrap(true);           // 按宽度自动换行
    edit->SetVerticalCenter(false);    // 多行从顶部排
    // 不显示竖直滚动条：本场景靠"自动长高"让内容尽量装得下，画面上不该出现滚动条。
    // 关掉的只是那根可拖动的滑块，内容仍可用滚轮与方向键滚动，所以文字长到顶住选区
    // 底边之后仍然看得到后面的内容。
    edit->SetVScrollPolicy(balloonwjui::DuiRichEdit::kScrollBarNever);
    edit->SetCtlFont(L"Microsoft YaHei", m_textFontPx, false, false, false, false);
    edit->SetTextColor(m_textColor);
    edit->SetBgColor(RGB(0xFF, 0xFF, 0xFF));
    m_textEdit = edit.get();
    AddChild(std::move(edit));         // AddChild 内部自动 AttachToHost

    m_textEdit->SetRect(m_textBox);    // 摆放到算好的矩形上
    m_textEdit->SetText(_T(""));
    m_textEdit->SetVisible(true);
    m_textEdit->SetFocus();

    m_textEditing = true;
    GrowTextEditToFit();   // 按实际字体行高把初始高度校正成"刚好一行"
    Invalidate();
}

void CaptureCanvas::GrowTextEditToFit()
{
    if (!m_textEditing || m_textEdit == NULL)
    {
        return;
    }
    // 输入框的显示行数（自动换行折出来的行也计入）。空文本为 1。
    int lineCount = m_textEdit->LineCount();
    if (lineCount < 1)
    {
        lineCount = 1;
    }
    // 行高由控件按自己当前的默认字体量出，与它内部排版用的是同一个口径。
    int lineH = m_textEdit->GetLineHeight();
    if (lineH <= 0)
    {
        lineH = m_textFontPx;
    }

    // 所需框高 = 内容高 + 竖直框架量；上限为选区底（极长文字才会触顶，那时退回滚动）。
    int needH = lineCount * lineH + kTextEditVChrome;
    int maxBottom = m_sel.bottom - kTextEdgeGap;
    int newBottom = m_textBox.top + needH;
    if (newBottom > maxBottom)
    {
        newBottom = maxBottom;
    }
    int minBottom = m_textBox.top + lineH + kTextEditVChrome;   // 至少一行高
    if (newBottom < minBottom)
    {
        newBottom = minBottom;
    }
    if (newBottom != m_textBox.bottom)
    {
        m_textBox.bottom = newBottom;
        m_textEdit->SetRect(m_textBox);
        Invalidate();
    }
}

void CaptureCanvas::CommitTextEdit()
{
    if (!m_textEditing)
    {
        return;
    }
    CString text;
    if (m_textEdit != NULL)
    {
        text = m_textEdit->GetText();
        balloonwjui::DuiEditHost* dead = m_textEdit;
        m_textEdit = NULL;
        RemoveChild(dead);   // 连同其 EDIT 子窗一并销毁
    }
    m_textEditing = false;

    // 去掉尾部空白；仍有可见内容才落成文字标注。
    text.TrimRight();
    if (!text.IsEmpty())
    {
        Annotation a;
        a.tool      = kAnnText;
        a.color     = m_textColor;
        a.lineWidth = m_toolState.LineWidth();
        a.start.x   = m_textBox.left;
        a.start.y   = m_textBox.top;
        a.end.x     = m_textBox.right;   // 换行宽度右界
        a.end.y     = m_sel.bottom;      // 高度不限：绘制按内容自适应，再被选区裁剪
        a.text      = text;
        m_annotations.Add(a);
    }
    // 焦点交回宿主窗口，保证 Esc 等键随后由浮层处理。
    if (GetHost() != NULL)
    {
        ::SetFocus(GetHost()->m_hWnd);
    }
    Invalidate();
}

void CaptureCanvas::CancelTextEdit()
{
    if (!m_textEditing)
    {
        return;
    }
    if (m_textEdit != NULL)
    {
        balloonwjui::DuiEditHost* dead = m_textEdit;
        m_textEdit = NULL;
        RemoveChild(dead);
    }
    m_textEditing = false;
    if (GetHost() != NULL)
    {
        ::SetFocus(GetHost()->m_hWnd);
    }
    Invalidate();
}

void CaptureCanvas::PositionAndShowToolbar()
{
    if (m_toolbar == NULL || m_annToolbar == NULL)
    {
        return;
    }

    // 两条工具栏竖直堆叠：标注栏在上、操作栏在下，整体作为一块放在选区旁。
    SIZE combined;
    combined.cx = (m_annBarSize.cx > m_barSize.cx) ? m_annBarSize.cx : m_barSize.cx;
    combined.cy = m_annBarSize.cy + kBarGap + m_barSize.cy;
    POINT origin = PlaceActionBar(m_sel, combined, m_rcItem, kBarGap);

    // ---- 标注栏（上）----
    RECT annRc = { origin.x, origin.y, origin.x + m_annBarSize.cx, origin.y + m_annBarSize.cy };
    m_annToolbar->SetRect(annRc);
    {
        const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = m_annToolbar->Children();
        int x    = origin.x + kAnnPad;
        int yBtn = origin.y + kAnnPad;                                  // 按钮 / 样条顶（高 kAnnItemH）
        int yDot = origin.y + kAnnPad + (kAnnItemH - kAnnDotW) / 2;     // 色点顶（组内竖直居中）
        const int swFirst = kAnnToolCount;                             // 颜色组起始下标
        const int swEnd   = kAnnToolCount + kAnnColorCount;            // 颜色组结束下标
        const int wFirst  = swEnd;                                     // 线宽组起始下标
        const int wEnd    = swEnd + kAnnWidthCount;                    // 线宽组结束下标
        const int urFirst = wEnd;                                      // 撤销重做组起始下标
        for (size_t i = 0; i < kids.size(); ++i)
        {
            int ii = (int)i;
            if (ii > 0)
            {
                if (ii == swFirst || ii == wFirst || ii == urFirst)
                {
                    x += kAnnGroupGap;   // 组间间距
                }
                else
                {
                    x += kAnnIntraGap;   // 组内间距
                }
            }
            RECT rc;
            if (ii < swFirst)
            {
                rc.left = x; rc.top = yBtn; rc.right = x + kAnnToolW; rc.bottom = yBtn + kAnnItemH;
                x += kAnnToolW;
            }
            else if (ii < swEnd)
            {
                rc.left = x; rc.top = yDot; rc.right = x + kAnnDotW; rc.bottom = yDot + kAnnDotW;
                x += kAnnDotW;
            }
            else if (ii < wEnd)
            {
                rc.left = x; rc.top = yBtn; rc.right = x + kAnnWidthW; rc.bottom = yBtn + kAnnItemH;
                x += kAnnWidthW;
            }
            else
            {
                rc.left = x; rc.top = yBtn; rc.right = x + kAnnUndoW; rc.bottom = yBtn + kAnnItemH;
                x += kAnnUndoW;
            }
            kids[i]->SetRect(rc);
        }
    }

    // ---- 操作栏（下）----
    int actY = origin.y + m_annBarSize.cy + kBarGap;
    RECT actRc = { origin.x, actY, origin.x + m_barSize.cx, actY + m_barSize.cy };
    m_toolbar->SetRect(actRc);
    {
        const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = m_toolbar->Children();
        int x = origin.x + kActPad;
        int y = actY + kActPad;
        for (size_t i = 0; i < kids.size() && (int)i < kActionCount; ++i)
        {
            if (i > 0)
            {
                x += kActionItems[i].groupGapBefore ? kActGroupGap : kActGap;
            }
            RECT rc = { x, y, x + kActionItems[i].width, y + kActItemH };
            kids[i]->SetRect(rc);
            x += kActionItems[i].width;
        }
    }

    m_annToolbar->SetVisible(true);
    m_toolbar->SetVisible(true);
    RefreshToolbarActiveState();
    Invalidate();
}

void CaptureCanvas::Layout(const RECT& /*rcAvail*/)
{
    // m_rcItem 已被调用方 SetRect 设为全客户区。本控件铺满整窗、不让子控件自动填充；
    // 工具栏按当前选区另行摆放（选区存在时保持其落点正确）。
    if (m_hasSel)
    {
        PositionAndShowToolbar();
    }
}

void CaptureCanvas::PaintDimMask(HDC hdc, const RECT& client)
{
    if (m_dimDib == NULL)
    {
        // 惰性建 1x1 黑色位图作蒙版源。
        m_dimDib = ::CreateCompatibleBitmap(hdc, 1, 1);
        HDC tmp = ::CreateCompatibleDC(hdc);
        HBITMAP old = (HBITMAP)::SelectObject(tmp, m_dimDib);
        RECT one = { 0, 0, 1, 1 };
        ::FillRect(tmp, &one, (HBRUSH)::GetStockObject(BLACK_BRUSH));
        ::SelectObject(tmp, old);
        ::DeleteDC(tmp);
    }

    HDC src = ::CreateCompatibleDC(hdc);
    HBITMAP old = (HBITMAP)::SelectObject(src, m_dimDib);
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = kDimAlpha;
    bf.AlphaFormat = 0;   // 只用 SourceConstantAlpha，忽略源逐像素 alpha

    // 选区有效时只蒙选区之外的四块；否则整屏蒙暗（提示用户去框选）。
    if (m_dragging || m_hasSel)
    {
        RECT bands[4];
        // 上
        bands[0].left = client.left;   bands[0].top = client.top;    bands[0].right = client.right; bands[0].bottom = m_sel.top;
        // 下
        bands[1].left = client.left;   bands[1].top = m_sel.bottom;  bands[1].right = client.right; bands[1].bottom = client.bottom;
        // 左（选区上下之间）
        bands[2].left = client.left;   bands[2].top = m_sel.top;     bands[2].right = m_sel.left;   bands[2].bottom = m_sel.bottom;
        // 右
        bands[3].left = m_sel.right;   bands[3].top = m_sel.top;     bands[3].right = client.right; bands[3].bottom = m_sel.bottom;
        for (int i = 0; i < 4; ++i)
        {
            int w = bands[i].right - bands[i].left;
            int h = bands[i].bottom - bands[i].top;
            if (w > 0 && h > 0)
            {
                ::AlphaBlend(hdc, bands[i].left, bands[i].top, w, h, src, 0, 0, 1, 1, bf);
            }
        }
    }
    else
    {
        ::AlphaBlend(hdc, client.left, client.top,
                     client.right - client.left, client.bottom - client.top,
                     src, 0, 0, 1, 1, bf);
    }

    ::SelectObject(src, old);
    ::DeleteDC(src);
}

void CaptureCanvas::PaintSelectionDecor(HDC hdc)
{
    if (!(m_dragging || m_hasSel))
    {
        return;
    }
    // 选区边框。
    HPEN pen = ::CreatePen(PS_SOLID, kBorderWidth, kBorderColor);
    HPEN oldPen = (HPEN)::SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
    ::Rectangle(hdc, m_sel.left, m_sel.top, m_sel.right, m_sel.bottom);
    ::SelectObject(hdc, oldBrush);
    ::SelectObject(hdc, oldPen);
    ::DeleteObject(pen);

    // 尺寸读数气泡（深色底 + 白字），放选区左上角上方；上方放不下则放进选区内顶部。
    CString readout = FormatSizeReadout(m_sel.right - m_sel.left, m_sel.bottom - m_sel.top);
    HFONT font = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
    HFONT oldFont = (HFONT)::SelectObject(hdc, font);

    RECT measure = { 0, 0, 0, 0 };
    ::DrawText(hdc, readout, -1, &measure, DT_CALCRECT | DT_SINGLELINE);
    int textW = measure.right - measure.left;
    int textH = measure.bottom - measure.top;
    const int padX = 6;
    const int padY = 3;
    int boxW = textW + padX * 2;
    int boxH = textH + padY * 2;

    RECT box;
    box.left = m_sel.left;
    box.top  = m_sel.top - boxH - 4;
    if (box.top < 0)
    {
        box.top = m_sel.top + 4;   // 上方放不下，落进选区内顶部
    }
    box.right  = box.left + boxW;
    box.bottom = box.top + boxH;

    HBRUSH dark = ::CreateSolidBrush(RGB(0x20, 0x20, 0x20));
    ::FillRect(hdc, &box, dark);
    ::DeleteObject(dark);

    int oldMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor = ::SetTextColor(hdc, RGB(0xFF, 0xFF, 0xFF));
    RECT textRc = { box.left + padX, box.top + padY, box.right - padX, box.bottom - padY };
    ::DrawText(hdc, readout, -1, &textRc, DT_SINGLELINE | DT_LEFT | DT_TOP);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldMode);
    ::SelectObject(hdc, oldFont);

    // 8 个缩放手柄（白方块 + 品牌色边）。仅默认模式、稳定选区时画——标注模式不画，
    // 避免误以为可缩放；框选拖拽中也不画。
    if (m_hasSel && !m_dragging && !InAnnotationMode())
    {
        int cxm = (m_sel.left + m_sel.right) / 2;
        int cym = (m_sel.top + m_sel.bottom) / 2;
        POINT hs[8] =
        {
            { m_sel.left, m_sel.top }, { cxm, m_sel.top }, { m_sel.right, m_sel.top },
            { m_sel.left, cym },                           { m_sel.right, cym },
            { m_sel.left, m_sel.bottom }, { cxm, m_sel.bottom }, { m_sel.right, m_sel.bottom },
        };
        int half = kHandleSize / 2;
        HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
        HBRUSH brand = ::CreateSolidBrush(kBorderColor);
        for (int i = 0; i < 8; ++i)
        {
            RECT h = { hs[i].x - half, hs[i].y - half, hs[i].x + half + 1, hs[i].y + half + 1 };
            ::FillRect(hdc, &h, white);
            ::FrameRect(hdc, &h, brand);
        }
        ::DeleteObject(white);
        ::DeleteObject(brand);
    }
}

void CaptureCanvas::ShowTip(const RECT& anchorRect, LPCTSTR text)
{
    m_tipAnchor = anchorRect;
    m_tipText   = (text != NULL) ? text : _T("");
    m_tipVisible = !m_tipText.IsEmpty();
    Invalidate();
}

void CaptureCanvas::HideTip()
{
    if (m_tipVisible)
    {
        m_tipVisible = false;
        Invalidate();
    }
}

void CaptureCanvas::PaintTooltip(HDC hdc)
{
    if (m_tipText.IsEmpty())
    {
        return;
    }

    // ---- 提示框视觉常量 ----
    const int      kTipGap    = 6;    // 提示框与按钮的竖直间距（像素）
    const int      kTipPadX   = 8;    // 文字左右内边距（像素）
    const int      kTipPadY   = 4;    // 文字上下内边距（像素）
    const int      kTipRadius = 5;    // 圆角半径（像素）
    const BYTE     kTipAlpha  = 235;  // 底色不透明度（0~255，略透以柔和）
    const COLORREF kTipBg     = RGB(0x20, 0x20, 0x20);   // 深色底
    const COLORREF kTipFg     = RGB(0xFF, 0xFF, 0xFF);   // 白字

    // tooltip 文字用微软雅黑：复制系统 GUI 字体的尺寸 / 字重，仅把字体名改成微软雅黑，
    // 这样字号与工具栏文字一致、只换了字形（CLEARTYPE 平滑）。
    LOGFONT lf;
    ::ZeroMemory(&lf, sizeof(lf));
    ::GetObject((HFONT)::GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
    ::wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Microsoft YaHei");
    lf.lfQuality = CLEARTYPE_QUALITY;
    HFONT font = ::CreateFontIndirect(&lf);
    HFONT oldFont = (HFONT)::SelectObject(hdc, font);

    RECT measure = { 0, 0, 0, 0 };
    ::DrawText(hdc, m_tipText, -1, &measure, DT_CALCRECT | DT_SINGLELINE);
    int boxW = (measure.right - measure.left) + kTipPadX * 2;
    int boxH = (measure.bottom - measure.top) + kTipPadY * 2;

    // 水平居中对齐按钮，竖直落在按钮正下方。
    int cx = (m_tipAnchor.left + m_tipAnchor.right) / 2;
    RECT box;
    box.left   = cx - boxW / 2;
    box.top    = m_tipAnchor.bottom + kTipGap;
    box.right  = box.left + boxW;
    box.bottom = box.top + boxH;

    // 夹进画布：左右越界则整体平移；下方越界（按钮贴屏幕底）则翻到按钮上方。
    if (box.right > m_rcItem.right)
    {
        int d = box.right - m_rcItem.right;
        box.left  -= d;
        box.right -= d;
    }
    if (box.left < m_rcItem.left)
    {
        int d = m_rcItem.left - box.left;
        box.left  += d;
        box.right += d;
    }
    if (box.bottom > m_rcItem.bottom)
    {
        box.top    = m_tipAnchor.top - kTipGap - boxH;
        box.bottom = box.top + boxH;
    }

    // 深色圆角底（GDI+ 抗锯齿，圆角画法与工具栏一致）。
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::REAL x = (Gdiplus::REAL)box.left;
        Gdiplus::REAL y = (Gdiplus::REAL)box.top;
        Gdiplus::REAL w = (Gdiplus::REAL)(box.right - box.left);
        Gdiplus::REAL h = (Gdiplus::REAL)(box.bottom - box.top);
        Gdiplus::REAL d = (Gdiplus::REAL)(kTipRadius * 2);
        Gdiplus::GraphicsPath path;
        path.AddArc(x, y, d, d, 180.0f, 90.0f);
        path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
        path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
        path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();
        Gdiplus::SolidBrush br(Gdiplus::Color(kTipAlpha,
                              GetRValue(kTipBg), GetGValue(kTipBg), GetBValue(kTipBg)));
        g.FillPath(&br, &path);
    }

    // 白字（居中）。
    int oldMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor = ::SetTextColor(hdc, kTipFg);
    ::DrawText(hdc, m_tipText, -1, &box, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldMode);
    ::SelectObject(hdc, oldFont);
    ::DeleteObject(font);
}

void CaptureCanvas::OnPaint(HDC hdc, const RECT& rcDirty)
{
    RECT client = m_rcItem;

    // ① 冻结桌面背景。临时把 m_frozen 选进一个内存 DC 画、画完即释放，避免长期占用
    // 该位图（裁剪选区时还要 SelectObject 它）。
    if (m_frozen != NULL)
    {
        HDC mem = ::CreateCompatibleDC(hdc);
        HBITMAP old = (HBITMAP)::SelectObject(mem, m_frozen);
        ::BitBlt(hdc, client.left, client.top,
                 client.right - client.left, client.bottom - client.top,
                 mem, 0, 0, SRCCOPY);
        ::SelectObject(mem, old);
        ::DeleteDC(mem);
    }
    // ② 选区外暗化蒙版。
    PaintDimMask(hdc, client);
    // ③ 选区边框 + 尺寸读数。
    PaintSelectionDecor(hdc);
    // ④ 标注（已提交 + 正在拖的草稿），裁剪在选区内，不画到选区外。
    if (m_hasSel)
    {
        int saved = ::SaveDC(hdc);
        ::IntersectClipRect(hdc, m_sel.left, m_sel.top, m_sel.right, m_sel.bottom);
        PaintAnnotations(hdc, 0, 0, true);
        ::RestoreDC(hdc, saved);
    }
    // ⑤ 画子控件（工具栏）—— 必须显式调基类，否则子控件不绘制。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
    // ⑥ 悬停提示画在最上层（按钮正下方），盖住工具栏。
    if (m_tipVisible)
    {
        PaintTooltip(hdc);
    }
}

bool CaptureCanvas::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    // 命中落在工具栏控件 / 文字编辑框上时不会进到这里（HitTest 优先返回控件）。

    // 文字工具：落点开 / 提交文字框。点已有编辑框之外 = 提交当前框（不在同一击里新建，
    // 需再点一次才开新框，行为可预期）；无编辑框时在选区内开一个新框。
    if (m_hasSel && m_toolState.Tool() == kAnnText)
    {
        if (m_textEditing)
        {
            CommitTextEdit();
            return true;
        }
        if (::PtInRect(&m_sel, pt))
        {
            BeginTextEdit(pt);
        }
        return true;
    }

    if (InAnnotationMode())
    {
        // 选了标注工具：在选区内按下 = 开画一个标注；选区外按下忽略（不重选，免丢标注）。
        if (!::PtInRect(&m_sel, pt))
        {
            return true;
        }
        m_drawingAnn = true;
        m_draft = Annotation();
        m_draft.tool      = m_toolState.Tool();
        m_draft.color     = m_toolState.Color();
        m_draft.lineWidth = m_toolState.LineWidth();
        POINT c = ClampToSelection(pt);
        m_draft.start = c;
        m_draft.end   = c;
        if (IsPathTool(m_draft.tool))
        {
            m_draft.points.push_back(c);
        }
        Invalidate();
        return true;
    }

    // 默认模式（未选标注工具）：已有选区时，按命中区决定移动 / 缩放 / 新框选。
    if (m_hasSel)
    {
        SelHitZone zone = HitTestSelection(m_sel, pt, kHandleTol);
        if (zone != kSelOutside)
        {
            // 选区内 → 移动；边 / 角 → 缩放。拖拽期间先隐藏工具栏，松开再重摆。
            m_movingSel = true;
            m_dragZone = zone;
            m_lastDragPt = pt;
            if (m_toolbar != NULL)
            {
                m_toolbar->SetVisible(false);
            }
            if (m_annToolbar != NULL)
            {
                m_annToolbar->SetVisible(false);
            }
            Invalidate();
            return true;
        }
        // zone == kSelOutside → 落到下方"新框选"。
    }

    // 未选标注工具且点在选区外（或还没选区）：开始一次新的框选（清掉旧选区与旧标注）。
    m_dragging = true;
    m_hasSel = false;
    m_annotations.Clear();
    m_anchor = pt;
    m_sel.left = pt.x;
    m_sel.top = pt.y;
    m_sel.right = pt.x;
    m_sel.bottom = pt.y;
    if (m_toolbar != NULL)
    {
        m_toolbar->SetVisible(false);
    }
    if (m_annToolbar != NULL)
    {
        m_annToolbar->SetVisible(false);
    }
    Invalidate();
    return true;
}

bool CaptureCanvas::OnMouseMove(POINT pt, UINT /*mkFlags*/)
{
    if (m_drawingAnn)
    {
        POINT c = ClampToSelection(pt);
        m_draft.end = c;
        if (IsPathTool(m_draft.tool))
        {
            m_draft.points.push_back(c);
        }
        Invalidate();
        return true;
    }
    if (m_movingSel)
    {
        int dx = pt.x - m_lastDragPt.x;
        int dy = pt.y - m_lastDragPt.y;
        if (m_dragZone == kSelInside)
        {
            // 移动：整体平移并夹边；标注按"实际位移"同步平移（跟选区一起移）。
            RECT moved = MoveSelectionClamped(m_sel, dx, dy, m_rcItem);
            m_annotations.Translate(moved.left - m_sel.left, moved.top - m_sel.top);
            m_sel = moved;
        }
        else
        {
            // 缩放：按命中边 / 角调整，夹进窗口、不小于最小尺寸。标注不动（绝对位置，
            // 超出选区的部分绘制时被裁掉）。
            m_sel = ResizeSelection(m_sel, m_dragZone, dx, dy, m_rcItem, kMoveMinW, kMoveMinH);
        }
        m_lastDragPt = pt;
        Invalidate();
        return true;
    }
    if (m_dragging)
    {
        RECT r = NormalizeDragRect(m_anchor, pt);
        m_sel = ClampRectToBounds(r, m_rcItem);
        Invalidate();
        return true;
    }
    return false;
}

bool CaptureCanvas::OnLButtonUp(POINT /*pt*/, UINT /*mkFlags*/)
{
    if (m_drawingAnn)
    {
        m_drawingAnn = false;
        // 提交非退化标注：形状起止不同 / 画笔有 ≥2 点。
        bool ok = false;
        if (IsPathTool(m_draft.tool))
        {
            ok = (m_draft.points.size() >= 2);
        }
        else
        {
            ok = (m_draft.start.x != m_draft.end.x || m_draft.start.y != m_draft.end.y);
        }
        if (ok)
        {
            m_annotations.Add(m_draft);
        }
        Invalidate();
        return true;
    }
    if (m_movingSel)
    {
        // 移动 / 缩放结束：重新摆放并显示工具栏（跟随选区新位置 / 尺寸）。
        m_movingSel = false;
        PositionAndShowToolbar();
        Invalidate();
        return true;
    }
    if (m_dragging)
    {
        m_dragging = false;
        if (IsSelectionUsable(m_sel, kMinSelW, kMinSelH))
        {
            m_hasSel = true;
            PositionAndShowToolbar();
        }
        else
        {
            m_hasSel = false;
            if (m_toolbar != NULL)
            {
                m_toolbar->SetVisible(false);
            }
            if (m_annToolbar != NULL)
            {
                m_annToolbar->SetVisible(false);
            }
        }
        Invalidate();
        return true;
    }
    return false;
}

bool CaptureCanvas::OnLButtonDblClk(POINT pt, UINT /*mkFlags*/)
{
    // 有稳定可用选区、且双击点落在选区内（非框选 / 移动缩放 / 画标注进行中）→
    // 等同点了「发送到对话框」。本画布带 ctrlId(kIdCanvasDblConfirm)，以它发 DUIN_CLICK，
    // 遮罩窗口收到后触发 stage 动作（把截图插入聊天输入框）。
    if (m_hasSel && !m_dragging && !m_movingSel && !m_drawingAnn && ::PtInRect(&m_sel, pt))
    {
        NotifyParent(balloonwjui::DUIN_CLICK);
        return true;
    }
    return false;
}

bool CaptureCanvas::OnSetCursor(POINT pt)
{
    // 文字工具：用 I 形光标，提示"点击输入文字"。
    if (m_hasSel && m_toolState.Tool() == kAnnText)
    {
        ::SetCursor(::LoadCursor(NULL, IDC_IBEAM));
        return true;
    }

    // 默认模式（未选标注工具、有选区）：按鼠标所在命中区给移动 / 缩放光标；
    // 正在拖动时固定用拖拽区的光标。其余情况（框选 / 画标注）用十字。
    SelHitZone zone = kSelOutside;
    if (m_movingSel)
    {
        zone = m_dragZone;
    }
    else if (m_hasSel && !InAnnotationMode() && !m_drawingAnn)
    {
        zone = HitTestSelection(m_sel, pt, kHandleTol);
    }

    LPCTSTR cur = IDC_CROSS;
    switch (zone)
    {
    case kSelInside:
        cur = IDC_SIZEALL;
        break;
    case kSelLeft:
    case kSelRight:
        cur = IDC_SIZEWE;
        break;
    case kSelTop:
    case kSelBottom:
        cur = IDC_SIZENS;
        break;
    case kSelTL:
    case kSelBR:
        cur = IDC_SIZENWSE;
        break;
    case kSelTR:
    case kSelBL:
        cur = IDC_SIZENESW;
        break;
    default:
        cur = IDC_CROSS;
        break;
    }
    ::SetCursor(::LoadCursor(NULL, cur));
    return true;
}

bool CaptureCanvas::InAnnotationMode() const
{
    return m_hasSel && m_toolState.Tool() != kAnnNone;
}

POINT CaptureCanvas::ClampToSelection(POINT pt) const
{
    POINT p = pt;
    if (p.x < m_sel.left)   p.x = m_sel.left;
    if (p.x > m_sel.right)  p.x = m_sel.right;
    if (p.y < m_sel.top)    p.y = m_sel.top;
    if (p.y > m_sel.bottom) p.y = m_sel.bottom;
    return p;
}

bool CaptureCanvas::HandleToolbarNotify(UINT ctrlId)
{
    // 任何工具栏交互（切工具 / 改色 / 改线宽 / 撤销重做）都意味着当前文字框结束，先提交。
    if (m_textEditing)
    {
        CommitTextEdit();
    }

    // 「选择 / 移动」工具：回到默认模式（无标注工具，可拖动 / 缩放选区、光标恢复）。
    if (ctrlId == (UINT)kIdAnnSelect)
    {
        m_toolState.SetTool(kAnnNone);
        RefreshToolbarActiveState();
        Invalidate();
        return true;
    }
    // 工具：再点已选工具 = 取消（回到默认模式）。
    AnnotationTool t = ToolForId(ctrlId);
    if (t != kAnnNone)
    {
        m_toolState.SetTool(m_toolState.Tool() == t ? kAnnNone : t);
        RefreshToolbarActiveState();
        Invalidate();
        return true;
    }
    // 颜色。
    if (ctrlId >= (UINT)kIdAnnColor0 && ctrlId < (UINT)(kIdAnnColor0 + kAnnColorCount))
    {
        m_toolState.SetColor(kPalette[ctrlId - (UINT)kIdAnnColor0]);
        RefreshToolbarActiveState();
        return true;
    }
    // 线宽。
    if (ctrlId == (UINT)kIdAnnWidthThin)
    {
        m_toolState.SetLineWidth(kWidthThin);
        RefreshToolbarActiveState();
        return true;
    }
    if (ctrlId == (UINT)kIdAnnWidthMed)
    {
        m_toolState.SetLineWidth(kWidthMed);
        RefreshToolbarActiveState();
        return true;
    }
    if (ctrlId == (UINT)kIdAnnWidthThick)
    {
        m_toolState.SetLineWidth(kWidthThick);
        RefreshToolbarActiveState();
        return true;
    }
    // 撤销 / 重做。
    if (ctrlId == (UINT)kIdAnnUndo)
    {
        m_annotations.Undo();
        Invalidate();
        return true;
    }
    if (ctrlId == (UINT)kIdAnnRedo)
    {
        m_annotations.Redo();
        Invalidate();
        return true;
    }
    return false;
}

void CaptureCanvas::RefreshToolbarActiveState()
{
    if (m_annToolbar == NULL)
    {
        return;
    }
    UINT activeToolId = IdForTool(m_toolState.Tool());

    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = m_annToolbar->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        balloonwjui::DuiControl* c = kids[i].get();
        ColorDot* dot = dynamic_cast<ColorDot*>(c);
        if (dot != NULL)
        {
            dot->SetActive(dot->GetColor() == m_toolState.Color());
            continue;
        }
        WidthDot* wd = dynamic_cast<WidthDot*>(c);
        if (wd != NULL)
        {
            wd->SetActive(wd->GetWidth() == m_toolState.LineWidth());
            continue;
        }
        // 工具图标按钮：选中态 = 与当前工具匹配；「选择 / 移动」在默认模式(kAnnNone)下高亮；
        // 撤销 / 重做按钮永不匹配，保持未选。
        IconButton* b = dynamic_cast<IconButton*>(c);
        if (b != NULL)
        {
            UINT id = b->GetCtrlId();
            bool active = (id == activeToolId)
                          || (id == (UINT)kIdAnnSelect && m_toolState.Tool() == kAnnNone);
            b->SetActive(active);
        }
    }
}

void CaptureCanvas::PaintAnnotations(HDC hdc, int dx, int dy, bool withDraft)
{
    const std::vector<Annotation>& items = m_annotations.Items();
    for (size_t i = 0; i < items.size(); ++i)
    {
        DrawOneAnnotation(hdc, items[i], dx, dy);
    }
    if (withDraft && m_drawingAnn)
    {
        DrawOneAnnotation(hdc, m_draft, dx, dy);
    }
}

void CaptureCanvas::DrawOneAnnotation(HDC hdc, const Annotation& a, int dx, int dy)
{
    // 马赛克用原生 GDI 位图缩放实现，单独走 —— 不与 GDI+ 在同一 hdc 上交错（避免
    // GDI+ 缓存 DC 状态导致原生 BitBlt 不生效）。
    if (a.tool == kAnnMosaic)
    {
        // 归一矩形（用原始坐标算源区域，再整体偏移 dx/dy 绘制）。
        int l = (a.start.x < a.end.x) ? a.start.x : a.end.x;
        int t = (a.start.y < a.end.y) ? a.start.y : a.end.y;
        int r = (a.start.x < a.end.x) ? a.end.x : a.start.x;
        int b = (a.start.y < a.end.y) ? a.end.y : a.start.y;
        RECT fr = { l, t, r, b };
        DrawMosaicRect(hdc, fr, dx, dy, MosaicBlockForWidth(a.lineWidth));
        return;
    }
    if (a.tool == kAnnMosaicBrush)
    {
        DrawMosaicBrush(hdc, a, dx, dy);
        return;
    }
    if (a.tool == kAnnText)
    {
        DrawTextAnnotation(hdc, a, dx, dy);
        return;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(a.color), GetGValue(a.color), GetBValue(a.color)),
                     (Gdiplus::REAL)a.lineWidth);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    POINT s = { a.start.x + dx, a.start.y + dy };
    POINT e = { a.end.x + dx, a.end.y + dy };
    switch (a.tool)
    {
    case kAnnRect:
    {
        int l = (s.x < e.x) ? s.x : e.x;
        int t = (s.y < e.y) ? s.y : e.y;
        int r = (s.x < e.x) ? e.x : s.x;
        int b = (s.y < e.y) ? e.y : s.y;
        g.DrawRectangle(&pen, (Gdiplus::REAL)l, (Gdiplus::REAL)t,
                        (Gdiplus::REAL)(r - l), (Gdiplus::REAL)(b - t));
        break;
    }
    case kAnnEllipse:
    {
        int l = (s.x < e.x) ? s.x : e.x;
        int t = (s.y < e.y) ? s.y : e.y;
        int r = (s.x < e.x) ? e.x : s.x;
        int b = (s.y < e.y) ? e.y : s.y;
        g.DrawEllipse(&pen, (Gdiplus::REAL)l, (Gdiplus::REAL)t,
                      (Gdiplus::REAL)(r - l), (Gdiplus::REAL)(b - t));
        break;
    }
    case kAnnArrow:
    {
        g.DrawLine(&pen, (Gdiplus::REAL)s.x, (Gdiplus::REAL)s.y,
                   (Gdiplus::REAL)e.x, (Gdiplus::REAL)e.y);
        POINT w1, w2;
        int headLen = a.lineWidth * 4 + 8;
        ComputeArrowHead(a.start, a.end, headLen, w1, w2);   // 用原始坐标算，再加偏移
        g.DrawLine(&pen, (Gdiplus::REAL)e.x, (Gdiplus::REAL)e.y,
                   (Gdiplus::REAL)(w1.x + dx), (Gdiplus::REAL)(w1.y + dy));
        g.DrawLine(&pen, (Gdiplus::REAL)e.x, (Gdiplus::REAL)e.y,
                   (Gdiplus::REAL)(w2.x + dx), (Gdiplus::REAL)(w2.y + dy));
        break;
    }
    case kAnnPen:
        if (a.points.size() >= 2)
        {
            std::vector<Gdiplus::PointF> pts(a.points.size());
            for (size_t i = 0; i < a.points.size(); ++i)
            {
                pts[i] = Gdiplus::PointF((Gdiplus::REAL)(a.points[i].x + dx),
                                         (Gdiplus::REAL)(a.points[i].y + dy));
            }
            g.DrawLines(&pen, &pts[0], (int)pts.size());
        }
        break;
    default:
        break;
    }
}

HBITMAP CaptureCanvas::BuildMosaicBitmap(HDC refDc, const RECT& frozenRect, int block)
{
    if (m_frozen == NULL)
    {
        return NULL;
    }
    int w = frozenRect.right - frozenRect.left;
    int h = frozenRect.bottom - frozenRect.top;
    if (w <= 0 || h <= 0)
    {
        return NULL;
    }
    int bw = MosaicDownscaleExtent(w, block);   // 降采样后的块数（每维 >=1）
    int bh = MosaicDownscaleExtent(h, block);

    HDC srcDc   = ::CreateCompatibleDC(refDc);
    HDC smallDc = ::CreateCompatibleDC(refDc);
    HDC outDc   = ::CreateCompatibleDC(refDc);
    HBITMAP smallBmp = ::CreateCompatibleBitmap(refDc, bw, bh);
    HBITMAP outBmp   = ::CreateCompatibleBitmap(refDc, w, h);

    HBITMAP oldSrc   = (HBITMAP)::SelectObject(srcDc, m_frozen);
    HBITMAP oldSmall = (HBITMAP)::SelectObject(smallDc, smallBmp);
    HBITMAP oldOut   = (HBITMAP)::SelectObject(outDc, outBmp);

    // ① 缩小：HALFTONE 让每个目标像素取源块的平均色。
    ::SetStretchBltMode(smallDc, HALFTONE);
    ::SetBrushOrgEx(smallDc, 0, 0, NULL);
    ::StretchBlt(smallDc, 0, 0, bw, bh,
                 srcDc, frozenRect.left, frozenRect.top, w, h, SRCCOPY);
    // ② 放大：COLORONCOLOR（最近邻），呈方块马赛克。
    ::SetStretchBltMode(outDc, COLORONCOLOR);
    ::StretchBlt(outDc, 0, 0, w, h, smallDc, 0, 0, bw, bh, SRCCOPY);

    ::SelectObject(srcDc, oldSrc);
    ::SelectObject(smallDc, oldSmall);
    ::SelectObject(outDc, oldOut);
    ::DeleteObject(smallBmp);
    ::DeleteDC(srcDc);
    ::DeleteDC(smallDc);
    ::DeleteDC(outDc);
    return outBmp;
}

void CaptureCanvas::DrawMosaicRect(HDC hdc, const RECT& frozenRect, int dx, int dy, int block)
{
    int w = frozenRect.right - frozenRect.left;
    int h = frozenRect.bottom - frozenRect.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }
    HBITMAP mosaic = BuildMosaicBitmap(hdc, frozenRect, block);
    if (mosaic == NULL)
    {
        return;
    }
    HDC mdc = ::CreateCompatibleDC(hdc);
    HBITMAP old = (HBITMAP)::SelectObject(mdc, mosaic);
    ::BitBlt(hdc, frozenRect.left + dx, frozenRect.top + dy, w, h, mdc, 0, 0, SRCCOPY);
    ::SelectObject(mdc, old);
    ::DeleteDC(mdc);
    ::DeleteObject(mosaic);
}

void CaptureCanvas::DrawMosaicBrush(HDC hdc, const Annotation& a, int dx, int dy)
{
    if (a.points.empty())
    {
        return;
    }
    int block = MosaicBlockForWidth(a.lineWidth);
    int diameter = block * 2;   // 笔触直径约 2 个块宽
    const int kMinBrushDiameter = 8;
    if (diameter < kMinBrushDiameter)
    {
        diameter = kMinBrushDiameter;
    }
    int radius = diameter / 2;

    // 路径包围盒（含笔触半径）。
    int minX = a.points[0].x;
    int maxX = a.points[0].x;
    int minY = a.points[0].y;
    int maxY = a.points[0].y;
    for (size_t i = 1; i < a.points.size(); ++i)
    {
        if (a.points[i].x < minX) minX = a.points[i].x;
        if (a.points[i].x > maxX) maxX = a.points[i].x;
        if (a.points[i].y < minY) minY = a.points[i].y;
        if (a.points[i].y > maxY) maxY = a.points[i].y;
    }
    minX -= radius;
    minY -= radius;
    maxX += radius;
    maxY += radius;

    RECT bbox = { minX, minY, maxX, maxY };
    // 夹进画布（= 冻结位图范围），避免源越界读到黑边。
    if (bbox.left < m_rcItem.left)     bbox.left = m_rcItem.left;
    if (bbox.top < m_rcItem.top)       bbox.top = m_rcItem.top;
    if (bbox.right > m_rcItem.right)   bbox.right = m_rcItem.right;
    if (bbox.bottom > m_rcItem.bottom) bbox.bottom = m_rcItem.bottom;
    // 块对齐：把左上角向下取整到 block 整数倍，使块网格稳定、不随笔画"游动"。
    bbox.left -= (bbox.left % block);
    bbox.top  -= (bbox.top % block);

    int w = bbox.right - bbox.left;
    int h = bbox.bottom - bbox.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    HBITMAP mosaic = BuildMosaicBitmap(hdc, bbox, block);
    if (mosaic == NULL)
    {
        return;
    }
    HDC mdc = ::CreateCompatibleDC(hdc);
    HBITMAP old = (HBITMAP)::SelectObject(mdc, mosaic);

    // 沿路径逐点把马赛克方块盖上去（局部坐标 = 点 - bbox 左上）。
    for (size_t i = 0; i < a.points.size(); ++i)
    {
        int sx = (a.points[i].x - bbox.left) - radius;
        int sy = (a.points[i].y - bbox.top) - radius;
        int sw = diameter;
        int sh = diameter;
        if (sx < 0)
        {
            sw += sx;
            sx = 0;
        }
        if (sy < 0)
        {
            sh += sy;
            sy = 0;
        }
        if (sx + sw > w) sw = w - sx;
        if (sy + sh > h) sh = h - sy;
        if (sw <= 0 || sh <= 0)
        {
            continue;
        }
        ::BitBlt(hdc, bbox.left + dx + sx, bbox.top + dy + sy, sw, sh, mdc, sx, sy, SRCCOPY);
    }

    ::SelectObject(mdc, old);
    ::DeleteDC(mdc);
    ::DeleteObject(mosaic);
}

void CaptureCanvas::DrawTextAnnotation(HDC hdc, const Annotation& a, int dx, int dy)
{
    if (a.text.IsEmpty())
    {
        return;
    }
    int fontPx = TextFontSizeForWidth(a.lineWidth);
    LOGFONT lf;
    ::ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight       = -fontPx;
    lf.lfWeight       = FW_NORMAL;
    lf.lfCharSet      = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfQuality      = CLEARTYPE_QUALITY;
    ::wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Microsoft YaHei");
    HFONT font = ::CreateFontIndirect(&lf);
    HFONT oldFont = (HFONT)::SelectObject(hdc, font);

    RECT rc;
    rc.left   = a.start.x + dx;
    rc.top    = a.start.y + dy;
    rc.right  = a.end.x + dx;       // 换行宽度右界
    rc.bottom = a.end.y + dy;

    int oldMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor = ::SetTextColor(hdc, a.color);
    ::DrawText(hdc, a.text, -1, &rc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldMode);
    ::SelectObject(hdc, oldFont);
    ::DeleteObject(font);
}

void CaptureCanvas::BakeAnnotationsInto(HDC dstDc, const RECT& sel)
{
    // 标注以客户区坐标记录；裁剪图的本地坐标 = 客户区坐标 - 选区左上角。
    int saved = ::SaveDC(dstDc);
    ::IntersectClipRect(dstDc, 0, 0, sel.right - sel.left, sel.bottom - sel.top);
    PaintAnnotations(dstDc, -sel.left, -sel.top, false);
    ::RestoreDC(dstDc, saved);
}

} // namespace balloonscreenshot
