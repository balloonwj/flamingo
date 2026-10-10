/**
 *  字幕的绘制（docs/语音字幕方案.md 第 7、8、9 节），CaptionRenderer.h
 *
 *  把若干条字幕画进一块透明的 BGRA 图层（预乘透明度，自上而下）。编辑器的视频预览、硬字幕导出、观看窗的叠加
 *  都调用这一份，保证所见即所得。画法：
 *    - 每条字幕先加「说话人：」前缀（样式里关闭时不加），按图层宽度折行（Model/CaptionLayout），底部居中；
 *    - 文字用 GraphicsPath::AddString 转成轮廓，先描边、再填色（DrawString 做不到描边），前缀用黄色；
 *    - 中间结果（正在说的这句）用浅灰色；可选半透明底框；
 *    - 给出了要避开的区域（画中画）且它与字幕所在的高度重叠时，字幕改在它旁边较宽的一侧居中。
 *  尺寸按 CaptionStyle 的定义（以 1080 高的画面为基准）随 refHeight 等比缩放。
 *
 *  字体固定用「微软雅黑」（不存在时依次退到黑体、微软正黑体、系统默认无衬线字体）：字幕是视频内容，不随界面语言变化；
 *  而且 GDI+ 的 AddString 不做字体回退，用界面字体（英文界面为 Segoe UI）画中文会变成方框。
 *
 *  调用前进程须已初始化 GDI+（模块 Init 时调用 GdiplusStartup）。可在任意线程调用（导出线程也用）。
 *
 *  典型用法：
 *      std::vector<caption::CaptionPaintLine> lines(1);
 *      lines[0].m_text = L"...";
 *      caption::CaptionPaintParams p;
 *      p.m_width = 1920; p.m_height = 200; p.m_refHeight = 1080;
 *      caption::CaptionRenderer::Paint(lines, p, pixels, NULL);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "../Model/CaptionDraft.h"

namespace caption {

//一条要画的字幕
struct CaptionPaintLine
{
    CaptionPaintLine();

    std::wstring m_speaker;   //说话人；为空或样式关闭说话人时不加前缀
    std::wstring m_text;      //文字
    bool         m_partial;   //是否为正在说的中间结果（浅灰色）
};

//绘制参数
struct CaptionPaintParams
{
    CaptionPaintParams();

    CaptionStyle m_style;       //外观
    int          m_width;       //图层宽（像素）
    int          m_height;      //图层高（像素）；字幕从底部往上排，底边离图层底边 m_style.m_bottomMarginPx（已缩放）
    int          m_refHeight;   //视频画面的高（像素），样式尺寸按它相对 1080 缩放；图层就是整幅画面时等于 m_height
    bool         m_hasAvoid;    //是否有要避开的区域
    RECT         m_avoid;       //要避开的区域（图层坐标，像素）
};

/**
 *  字幕绘制。全部为静态方法。
 */
class CaptionRenderer
{
public:
    /**
     *  画字幕。图层原有内容保留（调用方通常先清成全透明）。
     *  @param lines  要画的字幕，从上到下
     *  @param p      绘制参数
     *  @param bgra   [入参出参] 图层像素，m_width × m_height 个，预乘透明度，自上而下
     *  @param bounds [出参，可为 NULL] 实际画到的区域（图层坐标）；什么都没画时为空矩形
     *  @return 参数不对时返回 false
     */
    static bool Paint(const std::vector<CaptionPaintLine>& lines, const CaptionPaintParams& p, uint32_t* bgra,
                      RECT* bounds);

    //样式尺寸按画面高度缩放后的像素值（至少 1）
    static int Scale(int px, int refHeight);
};

} // namespace caption
