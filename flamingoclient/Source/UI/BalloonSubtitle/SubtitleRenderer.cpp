/**
 *  SubtitleRenderer.cpp
 *
 *  字幕窗口绘制的实现，说明见 SubtitleRenderer.h。
 *
 *  文字用 GraphicsPath::AddString 转成轮廓再画，这样才能先描一圈深色边、再填色（DrawString 做不到描边）。
 *  整张图直接画进 PARGB 格式的位图（像素缓冲区就是调用方的 vector），得到的就是 DirectComposition 与
 *  UpdateLayeredWindow 要的预乘透明度像素。文字渲染不用 ClearType：ClearType 的彩色边缘在透明背景上会画出
 *  错误的透明度。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#include "stdafx.h"

#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include <math.h>
#include <algorithm>
#include <memory>

#include "SubtitleRenderer.h"

#include "../../Common/I18n/I18n.h"          // UiFontName：字幕与会话名用界面字体
#include "../BalloonChatWnd/ChatIcons.h"     // DrawChatIcon：工具按钮的线性图标

#pragma comment(lib, "gdiplus.lib")

namespace subtitle {

namespace {

//百分比的分母
const int kPercent = 100;
//描边可见宽度占字号的百分比（画笔宽度是它的两倍，一半压在字形里），最少 kMinOutlinePx 像素
const int kOutlinePercent = 8;
const int kMinOutlinePx = 2;
//描边颜色的不透明度（黑色）
const BYTE kOutlineAlpha = 215;
//阴影向右下偏移占字号的百分比，最少 1 像素；阴影颜色的不透明度（黑色）
const int kShadowPercent = 6;
const BYTE kShadowAlpha = 110;
//底色：深灰，完全显示时的不透明度
const BYTE kChromeR = 20;
const BYTE kChromeG = 22;
const BYTE kChromeB = 28;
const BYTE kChromeMaxAlpha = 150;
//悬停、按下的按钮底：白色，完全显示时的不透明度
const BYTE kHoverAlpha = 45;
const BYTE kPressedAlpha = 85;
//工具按钮图标与会话名：白色，完全显示时的不透明度
const BYTE kIconAlpha = 255;
const BYTE kTitleAlpha = 220;
//小锁按钮的圆形底：与底色同色，完全显示时的不透明度
const BYTE kLockBgAlpha = 175;
//图标占按钮边长的百分比；图标描边宽度（以 viewBox 24 为基准）。聊天窗头部图标用 2.0，这里的按钮更小、
//又压在深色半透明底上，2.0 换算后只有约 1.4 像素，斜线经抗锯齿后整条发灰，所以加粗到 2.4
const int kIconPercent = 64;
const Gdiplus::REAL kIconStroke = 2.4f;
//解锁时整窗铺的那层底的不透明度：1/255，肉眼看不见，只为让整窗都能收到鼠标消息
const BYTE kHitTestAlpha = 1;
//最大不透明度
const int kOpaque = 255;

//按 0~255 的比例缩放一个不透明度
BYTE ScaleAlpha(BYTE alpha, int factor)
{
    if (factor <= 0)
        return 0;
    if (factor >= kOpaque)
        return alpha;
    return (BYTE)((int)alpha * factor / kOpaque);
}

Gdiplus::Color ToColor(COLORREF rgb, BYTE alpha)
{
    return Gdiplus::Color(alpha, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb));
}

//测量与绘制共用的排版格式：GenericTypographic（不加额外的左右留白）并计入行尾空格
class TypographicFormat
{
public:
    TypographicFormat()
        : m_format(Gdiplus::StringFormat::GenericTypographic())
    {
        m_format.SetFormatFlags(m_format.GetFormatFlags() | Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
    }
    const Gdiplus::StringFormat* Get() const
    {
        return &m_format;
    }

private:
    Gdiplus::StringFormat m_format;   //GenericTypographic 的副本，加上计入行尾空格
};

//建立字体族：界面字体不可用时用通用无衬线字体
Gdiplus::FontFamily* CreateFamily()
{
    Gdiplus::FontFamily* family = new Gdiplus::FontFamily(i18n::UiFontName());
    if (family->GetLastStatus() != Gdiplus::Ok || !family->IsAvailable())
    {
        delete family;
        family = Gdiplus::FontFamily::GenericSansSerif()->Clone();
    }
    return family;
}

//字体族支持粗体时用粗体，否则用常规
int TextStyle(const Gdiplus::FontFamily& family, bool bold)
{
    if (bold && family.IsStyleAvailable(Gdiplus::FontStyleBold))
        return Gdiplus::FontStyleBold;
    return Gdiplus::FontStyleRegular;
}

//圆角矩形路径
void AddRoundRect(Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL w, Gdiplus::REAL h,
                  Gdiplus::REAL r)
{
    const Gdiplus::REAL d = r * 2.0f;
    if (r <= 0.0f || d > w || d > h)
    {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

/**
 *  画一段带阴影与描边的文字。
 *    g / family / style / emPx：绘图对象、字体族、字形、字号（像素）。
 *    text：  文字。
 *    x / y： 排版框左上角。
 *    fill：  填充色。
 *    outline：描边可见宽度，像素。
 *    shadow：阴影偏移，像素。
 *    format：排版格式（与测量时相同的 GenericTypographic）。
 */
void DrawOutlinedText(Gdiplus::Graphics& g, const Gdiplus::FontFamily& family, int style, Gdiplus::REAL emPx,
                      const std::wstring& text, Gdiplus::REAL x, Gdiplus::REAL y, const Gdiplus::Color& fill,
                      Gdiplus::REAL outline, Gdiplus::REAL shadow, const Gdiplus::StringFormat* format)
{
    if (text.empty())
        return;
    Gdiplus::GraphicsPath path;
    path.AddString(text.c_str(), (INT)text.size(), &family, style, emPx, Gdiplus::PointF(x, y), format);

    //阴影：把轮廓平移后填充，再描一圈同色的边，阴影与描边一样粗
    std::unique_ptr<Gdiplus::GraphicsPath> shadowPath(path.Clone());
    Gdiplus::Matrix shift;
    shift.Translate(shadow, shadow);
    shadowPath->Transform(&shift);
    const Gdiplus::Color shadowColor(kShadowAlpha, 0, 0, 0);
    Gdiplus::SolidBrush shadowBrush(shadowColor);
    Gdiplus::Pen shadowPen(shadowColor, outline * 2.0f);
    shadowPen.SetLineJoin(Gdiplus::LineJoinRound);
    g.FillPath(&shadowBrush, shadowPath.get());
    g.DrawPath(&shadowPen, shadowPath.get());

    //描边：画笔一半压在字形里，再填色时盖住，看得见的只有外侧那一半
    Gdiplus::Pen pen(Gdiplus::Color(kOutlineAlpha, 0, 0, 0), outline * 2.0f);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    g.DrawPath(&pen, &path);
    Gdiplus::SolidBrush brush(fill);
    g.FillPath(&brush, &path);
}

//工具条上一个按钮对应的图标与部位
struct ToolbarButton
{
    const RECT* m_rect;  //按钮位置
    int         m_part;  //部位，取值见 SubtitlePart
    int         m_icon;  //图标，取值见 balloonchatwnd::ChatIconKind
};

//画一个工具按钮：悬停或按下时画浅色圆底，再画白色图标
void DrawToolbarButton(Gdiplus::Graphics& g, const ToolbarButton& b, int chromeAlpha, int hoverPart, int pressedPart)
{
    const Gdiplus::REAL x = (Gdiplus::REAL)b.m_rect->left;
    const Gdiplus::REAL y = (Gdiplus::REAL)b.m_rect->top;
    const Gdiplus::REAL size = (Gdiplus::REAL)(b.m_rect->right - b.m_rect->left);
    if (b.m_part == pressedPart || b.m_part == hoverPart)
    {
        const BYTE a = ScaleAlpha((b.m_part == pressedPart) ? kPressedAlpha : kHoverAlpha, chromeAlpha);
        Gdiplus::SolidBrush bg(Gdiplus::Color(a, 255, 255, 255));
        g.FillEllipse(&bg, x, y, size, size);
    }
    const Gdiplus::REAL iconSize = size * (Gdiplus::REAL)kIconPercent / (Gdiplus::REAL)kPercent;
    const Gdiplus::REAL offset = (size - iconSize) / 2.0f;
    balloonchatwnd::DrawChatIcon(g, b.m_icon, x + offset, y + offset, iconSize,
                                 Gdiplus::Color(ScaleAlpha(kIconAlpha, chromeAlpha), 255, 255, 255), kIconStroke);
}

} // anonymous namespace

bool RenderSubtitle(const SubtitleRenderParams& p, std::vector<uint8_t>& bgra)
{
    if (p.m_width <= 0 || p.m_height <= 0)
    {
        bgra.clear();
        return false;
    }
    const size_t stride = (size_t)p.m_width * kSubtitleBytesPerPixel;
    bgra.assign(stride * p.m_height, 0);

    Gdiplus::Bitmap bmp(p.m_width, p.m_height, (INT)stride, PixelFormat32bppPARGB, &bgra[0]);
    if (bmp.GetLastStatus() != Gdiplus::Ok)
        return false;
    {
        Gdiplus::Graphics g(&bmp);
        if (g.GetLastStatus() != Gdiplus::Ok)
            return false;
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        //像素 i 覆盖 [i, i+1)：按钮的圆形底不会漏到按钮矩形外面一列
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        //解锁时整窗铺一层几乎看不见的底，使整个矩形都能收到鼠标消息
        if (p.m_hitTestable)
            g.Clear(Gdiplus::Color(kHitTestAlpha, 0, 0, 0));

        const SubtitleMetrics& m = p.m_metrics;
        std::unique_ptr<Gdiplus::FontFamily> family(CreateFamily());
        const TypographicFormat typo;

        //底色与工具条（解锁且悬停时）
        if (!p.m_locked && p.m_chromeAlpha > 0)
        {
            Gdiplus::GraphicsPath bgPath;
            AddRoundRect(bgPath, 0.0f, 0.0f, (Gdiplus::REAL)p.m_width, (Gdiplus::REAL)p.m_height,
                         (Gdiplus::REAL)m.m_cornerRadius);
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(ScaleAlpha(kChromeMaxAlpha, p.m_chromeAlpha), kChromeR, kChromeG,
                                                       kChromeB));
            g.FillPath(&bgBrush, &bgPath);

            //会话名：常规字体，超出位置时截短并加省略号，竖直居中
            const int titleW = p.m_toolbar.m_title.right - p.m_toolbar.m_title.left;
            if (titleW > 0 && !p.m_title.empty())
            {
                Gdiplus::Font titleFont(family.get(), (Gdiplus::REAL)m.m_titleFontPx,
                                        TextStyle(*family, false), Gdiplus::UnitPixel);
                //与测量会话名宽度时同一种排版格式（GenericTypographic），否则默认格式额外加的左右留白会让
                //刚好量出来的宽度装不下、被截成省略号
                Gdiplus::StringFormat fmt(typo.Get());
                fmt.SetFormatFlags(fmt.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap);
                fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                fmt.SetAlignment(Gdiplus::StringAlignmentNear);
                fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                const Gdiplus::RectF box((Gdiplus::REAL)p.m_toolbar.m_title.left, (Gdiplus::REAL)p.m_toolbar.m_title.top,
                                         (Gdiplus::REAL)titleW,
                                         (Gdiplus::REAL)(p.m_toolbar.m_title.bottom - p.m_toolbar.m_title.top));
                Gdiplus::SolidBrush titleBrush(Gdiplus::Color(ScaleAlpha(kTitleAlpha, p.m_chromeAlpha), 255, 255, 255));
                g.DrawString(p.m_title.c_str(), (INT)p.m_title.size(), &titleFont, box, &fmt, &titleBrush);
            }

            const ToolbarButton buttons[] = {
                { &p.m_toolbar.m_settings, kPartSettings, balloonchatwnd::kChatIconSettings },
                { &p.m_toolbar.m_lock, kPartLock, balloonchatwnd::kChatIconLock },
                { &p.m_toolbar.m_back, kPartBack, balloonchatwnd::kChatIconBackToChat },
                { &p.m_toolbar.m_close, kPartClose, balloonchatwnd::kChatIconClose },
            };
            for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
                DrawToolbarButton(g, buttons[i], p.m_chromeAlpha, p.m_hoverPart, p.m_pressedPart);
        }

        //锁定后浮出的小锁按钮：圆形深色底 + 打开的挂锁（点它解锁）
        if (p.m_locked && p.m_lockButtonAlpha > 0)
        {
            const RECT& rc = p.m_toolbar.m_lock;
            const Gdiplus::REAL size = (Gdiplus::REAL)(rc.right - rc.left);
            Gdiplus::SolidBrush lockBg(Gdiplus::Color(ScaleAlpha(kLockBgAlpha, p.m_lockButtonAlpha), kChromeR, kChromeG,
                                                      kChromeB));
            g.FillEllipse(&lockBg, (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top, size, size);
            const ToolbarButton lockButton = { &p.m_toolbar.m_lock, kPartLock, balloonchatwnd::kChatIconUnlock };
            DrawToolbarButton(g, lockButton, p.m_lockButtonAlpha, kPartNone, kPartNone);
        }

        //文字：每行水平居中，说话人前缀与内容分别着色
        const RECT text = SubtitleTextRect(m, p.m_width, p.m_maxLines);
        const int textW = text.right - text.left;
        const int style = TextStyle(*family, true);
        const Gdiplus::REAL emPx = (Gdiplus::REAL)m.m_fontPx;
        Gdiplus::Font font(family.get(), emPx, style, Gdiplus::UnitPixel);
        const UINT16 emHeight = family->GetEmHeight(style);
        const Gdiplus::REAL fontHeight = (emHeight > 0)
            ? emPx * (Gdiplus::REAL)family->GetLineSpacing(style) / (Gdiplus::REAL)emHeight
            : emPx;
        const Gdiplus::REAL outline = (Gdiplus::REAL)(std::max)(kMinOutlinePx, m.m_fontPx * kOutlinePercent / kPercent);
        const Gdiplus::REAL shadow = (Gdiplus::REAL)(std::max)(1, m.m_fontPx * kShadowPercent / kPercent);
        const bool message = (p.m_lineKind == kLineMessage);
        const Gdiplus::Color originColor = ToColor(kOriginColor, (BYTE)kOpaque);
        const Gdiplus::Color speakerColor = ToColor(kSpeakerColor, (BYTE)kOpaque);
        const Gdiplus::Color contentColor = ToColor(message ? kContentColor : kHintColor, (BYTE)kOpaque);
        const size_t lineCount = (std::min)(p.m_text.m_lines.size(), (size_t)p.m_maxLines);
        for (size_t i = 0; i < lineCount; ++i)
        {
            const FitLine& line = p.m_text.m_lines[i];
            if (line.m_text.empty())
                continue;
            Gdiplus::RectF bounds;
            g.MeasureString(line.m_text.c_str(), (INT)line.m_text.size(), &font, Gdiplus::PointF(0.0f, 0.0f), typo.Get(),
                            &bounds);
            Gdiplus::REAL x = (Gdiplus::REAL)text.left + ((Gdiplus::REAL)textW - bounds.Width) / 2.0f;
            if (x < (Gdiplus::REAL)text.left)
                x = (Gdiplus::REAL)text.left;
            const Gdiplus::REAL y = (Gdiplus::REAL)text.top + (Gdiplus::REAL)(i * m.m_lineHeight) +
                                    ((Gdiplus::REAL)m.m_lineHeight - fontHeight) / 2.0f;
            //一行最多分三段着色：来源标注（浅蓝）、说话人前缀（金黄）、内容（白色或浅灰）
            const size_t originLen = (std::min)((size_t)(line.m_originLen > 0 ? line.m_originLen : 0),
                                                line.m_text.size());
            const size_t speakerLen = (std::min)((size_t)(line.m_speakerLen > 0 ? line.m_speakerLen : 0),
                                                 line.m_text.size() - originLen);
            const std::wstring segments[] = { line.m_text.substr(0, originLen),
                                              line.m_text.substr(originLen, speakerLen) };
            const Gdiplus::Color* segmentColors[] = { &originColor, &speakerColor };
            for (size_t s = 0; s < sizeof(segments) / sizeof(segments[0]); ++s)
            {
                if (segments[s].empty())
                    continue;
                DrawOutlinedText(g, *family, style, emPx, segments[s], x, y, *segmentColors[s], outline, shadow,
                                 typo.Get());
                Gdiplus::RectF segmentBounds;
                g.MeasureString(segments[s].c_str(), (INT)segments[s].size(), &font, Gdiplus::PointF(0.0f, 0.0f),
                                typo.Get(), &segmentBounds);
                x += segmentBounds.Width;
            }
            DrawOutlinedText(g, *family, style, emPx, line.m_text.substr(originLen + speakerLen), x, y, contentColor,
                             outline, shadow, typo.Get());
        }
        g.Flush(Gdiplus::FlushIntentionSync);
    }
    return true;
}

CSubtitleTextMeasurer::CSubtitleTextMeasurer(int fontPx, bool bold)
    : m_bitmap(NULL)
    , m_graphics(NULL)
    , m_family(NULL)
    , m_font(NULL)
{
    m_bitmap = new Gdiplus::Bitmap(1, 1, PixelFormat32bppPARGB);
    m_graphics = new Gdiplus::Graphics(m_bitmap);
    m_graphics->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    m_family = CreateFamily();
    m_font = new Gdiplus::Font(m_family, (Gdiplus::REAL)((fontPx > 0) ? fontPx : 1), TextStyle(*m_family, bold),
                               Gdiplus::UnitPixel);
}

CSubtitleTextMeasurer::~CSubtitleTextMeasurer()
{
    delete m_font;
    delete m_family;
    delete m_graphics;
    delete m_bitmap;
}

int CSubtitleTextMeasurer::Measure(const wchar_t* text, int len) const
{
    if (text == NULL || len <= 0)
        return 0;
    const TypographicFormat typo;
    Gdiplus::RectF bounds;
    if (m_graphics->MeasureString(text, len, m_font, Gdiplus::PointF(0.0f, 0.0f), typo.Get(), &bounds) != Gdiplus::Ok)
        return 0;
    return (int)ceil(bounds.Width);
}

int CSubtitleTextMeasurer::MeasureThunk(void* ctx, const wchar_t* text, int len)
{
    if (ctx == NULL)
        return 0;
    return static_cast<const CSubtitleTextMeasurer*>(ctx)->Measure(text, len);
}

} // namespace subtitle
