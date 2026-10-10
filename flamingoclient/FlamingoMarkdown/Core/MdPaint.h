/**
 *  Markdown 文档的绘制：把排版结果画到宿主给的 HDC 上。文字用 GDI（与聊天气泡一致，ClearType 效果相同），
 *  底色、圆点、任务框、「复制」按钮等形状用 GDI+ 抗锯齿绘制。
 *
 *  绘制顺序：块底色（代码块、表头）→ 行内代码底色 → 选区 → 线条、项目符号、任务框 → 文字 → 「复制」按钮。
 *  不画文档背景（由宿主画气泡）。调用前后 HDC 的状态（选中的对象、文字颜色、背景模式、对齐方式）不变。
 *
 *  GDI+ 在第一次绘制时启动（引用计数，与宿主各自启动互不影响），之后一直保持到进程退出。
 *
 *  典型用法：
 *
 *      MdPaintParams params;
 *      params.m_selBegin = 3;
 *      params.m_selEnd = 10;
 *      PaintMdLayout(layout, style, hdc, left, top, params);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>

#include "MdLayout.h"

namespace flamingomd {

/**
 *  绘制参数（DLL 内部使用，由导出函数从 MdPaintState 转换而来）。
 */
struct MdPaintParams
{
    int  m_selBegin;          //选区起点（已保证不大于终点）；没有选区时与终点相等
    int  m_selEnd;            //选区终点
    int  m_hoverLink;         //鼠标悬停的链接序号；-1 表示没有
    int  m_hoverCodeBlock;    //鼠标所在的代码块序号；-1 表示没有
    int  m_hoverCopyButton;   //鼠标悬停在其「复制」按钮上的代码块序号；-1 表示没有
    bool m_hasClip;           //是否裁剪
    RECT m_clip;              //裁剪矩形（HDC 坐标）

    MdPaintParams()
        : m_selBegin(0), m_selEnd(0), m_hoverLink(-1), m_hoverCodeBlock(-1), m_hoverCopyButton(-1), m_hasClip(false)
    {
        m_clip.left = 0;
        m_clip.top = 0;
        m_clip.right = 0;
        m_clip.bottom = 0;
    }
};

/**
 *  绘制已排版的文档。
 *  @param layout 排版结果
 *  @param style  排版时使用的样式（取颜色）
 *  @param hdc    目标设备上下文
 *  @param x      文档左上角在 hdc 中的横坐标
 *  @param y      文档左上角在 hdc 中的纵坐标
 *  @param params 绘制参数
 */
void PaintMdLayout(const MdLayoutResult& layout, const MdResolvedStyle& style, HDC hdc, int x, int y,
                   const MdPaintParams& params);

/**
 *  启动 GDI+（只有第一次调用生效）。m_init 时调用；绘制时也会自动调用。
 *  @return GDI+ 可用时返回 true
 */
bool EnsureMdGdiplus();

} // namespace flamingomd
