/**
 *  SubtitleRenderer.h
 *
 *  字幕窗口的绘制：把排好的文字、工具条、底色画进一块预乘透明度的 32 位 BGRA 位图，交给
 *  DirectComposition（或 UpdateLayeredWindow）按逐像素透明度合成到桌面上。外观参照截图
 *  snapshots/t1.png、t2.png：
 *    · 文字用粗体，先画一层偏移的半透明阴影，再画深色描边，最后填色；说话人名字金黄、内容白色，
 *      等待提示与撤回提示浅灰。每行水平居中。压在任何背景上都看得清；
 *    · 解锁且悬停时，整窗画圆角半透明深色底，工具条里画会话名与四个白色线性图标；
 *    · 锁定时不画底色，小锁按钮浮出时只画它自己的圆形深色底与锁形图标；
 *    · 解锁时整窗铺一层透明度为 1/255 的底（肉眼看不见），使 UpdateLayeredWindow 方式下整个矩形
 *      都能收到鼠标消息，而不只是有文字的像素。
 *  文字的宽度由 CSubtitleTextMeasurer 测量，绘制与排版用同一种字体与同一种测量方式，两者结果一致。
 *
 *  本模块被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用（见 Tests/SubtitleTests.cpp 的 SUBR 组）。
 *  调用前进程须已初始化 GDI+。
 *
 *  典型用法：
 *      subtitle::CSubtitleTextMeasurer measurer(metrics.m_fontPx, true);
 *      params.m_text = subtitle::FitSubtitleText(prefix, content, textWidth, maxLines,
 *                                                 &subtitle::CSubtitleTextMeasurer::MeasureThunk, &measurer);
 *      subtitle::RenderSubtitle(params, bits);
 *
 *  balloonwj@qq.com   2026-10-05
 */

#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "SubtitleLayout.h"
#include "SubtitleModel.h"
#include "SubtitleState.h"
#include "SubtitleTextFit.h"

namespace Gdiplus {
class Bitmap;
class Graphics;
class Font;
class FontFamily;
}

namespace subtitle {

//说话人名字的颜色：金黄（与截图 t1 里已唱部分的颜色接近）
const COLORREF kSpeakerColor = RGB(255, 214, 74);
//内容的颜色：白
const COLORREF kContentColor = RGB(255, 255, 255);
//等待提示与撤回提示的颜色：浅灰
const COLORREF kHintColor = RGB(205, 205, 205);
//群聊字幕行首来源标注「【群名】」的颜色：浅蓝，与金黄的名字、白色的内容都分得开
const COLORREF kOriginColor = RGB(125, 190, 255);

//每像素字节数（BGRA）
const int kSubtitleBytesPerPixel = 4;

//绘制所需的全部输入
struct SubtitleRenderParams
{
    int             m_width;            //位图宽，物理像素，大于 0
    int             m_height;           //位图高，物理像素，大于 0
    SubtitleMetrics m_metrics;          //各部分尺寸
    SubtitleToolbar m_toolbar;          //工具条各部分的位置
    int             m_maxLines;         //最多行数（决定文字区高度）
    std::wstring    m_title;            //会话名；绘制时超出 m_toolbar.m_title 的部分截短并加省略号
    FitResult       m_text;             //排好的文字
    int             m_lineKind;         //内容种类，取值见 SubtitleLineKind，决定文字颜色
    bool            m_locked;           //是否锁定
    int             m_chromeAlpha;      //底色与工具按钮的不透明度，0~255
    int             m_lockButtonAlpha;  //锁定时小锁按钮的不透明度，0~255
    int             m_hoverPart;        //光标悬停的部位（取值见 SubtitlePart）；悬停的按钮画浅色底
    int             m_pressedPart;      //按下未松开的按钮（取值见 SubtitlePart），画得更亮；没有时为 kPartNone
    bool            m_hitTestable;      //是否给整窗铺一层透明度 1/255 的底（解锁时为 true）

    SubtitleRenderParams()
        : m_width(0)
        , m_height(0)
        , m_maxLines(kDefaultMaxLines)
        , m_lineKind(kLineWaiting)
        , m_locked(false)
        , m_chromeAlpha(0)
        , m_lockButtonAlpha(0)
        , m_hoverPart(kPartNone)
        , m_pressedPart(kPartNone)
        , m_hitTestable(false)
    {
    }
};

/**
 *  把字幕画进一块预乘透明度的 BGRA 位图。
 *    p：   绘制输入。
 *    bgra：[出参] 位图像素，自上而下，每行 m_width * 4 字节；函数内部按尺寸重新分配并清成全透明。
 *  返回：成功返回 true；尺寸无效或 GDI+ 绘制失败返回 false（此时 bgra 内容为全透明或未定义）。
 */
bool RenderSubtitle(const SubtitleRenderParams& p, std::vector<uint8_t>& bgra);

/**
 *  CSubtitleTextMeasurer —— 用 GDI+ 测量字幕文字的宽度。
 *
 *  字体取界面字体（i18n::UiFontName()），系统里没有这种字体时退回通用无衬线字体。测量方式与
 *  RenderSubtitle 绘制文字时完全相同（GenericTypographic 排版、计入行尾空格），所以 FitSubtitleText
 *  算出的每行宽度与画出来的一致。不可复制；只在界面线程上使用，调用前进程须已初始化 GDI+。
 */
class CSubtitleTextMeasurer
{
public:
    /**
     *  建立测量用的字体与绘图对象。
     *    fontPx：字号，物理像素，大于 0。
     *    bold：  是否粗体（字幕正文用粗体，会话名用常规）。
     */
    CSubtitleTextMeasurer(int fontPx, bool bold);
    ~CSubtitleTextMeasurer();

    /**
     *  测量一段文字的宽度。
     *    text：文字起点，不要求以 NUL 结尾；为 NULL 时返回 0。
     *    len： 长度（UTF-16 单元数）；不大于 0 时返回 0。
     *  返回：宽度，像素，向上取整。
     */
    int Measure(const wchar_t* text, int len) const;

    /**
     *  符合 MeasureTextFn 的转接函数：ctx 为 CSubtitleTextMeasurer 指针。
     */
    static int MeasureThunk(void* ctx, const wchar_t* text, int len);

private:
    CSubtitleTextMeasurer(const CSubtitleTextMeasurer&);
    CSubtitleTextMeasurer& operator=(const CSubtitleTextMeasurer&);

    Gdiplus::Bitmap*     m_bitmap;    //测量用的 1x1 位图，构造时创建、析构时释放
    Gdiplus::Graphics*   m_graphics;  //在 m_bitmap 上建立的绘图对象，用来调用 MeasureString
    Gdiplus::FontFamily* m_family;    //字体族；界面字体不可用时为通用无衬线字体的副本
    Gdiplus::Font*       m_font;      //测量用的字体（像素单位）
};

} // namespace subtitle
