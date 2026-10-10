/**
 *  屏幕共享与录屏的鼠标效果（第 16 期），说明见 ShareMouseEffects.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "ShareMouseEffects.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>   // memcpy
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

namespace {

//五种预设色，下标为 share_mouse_color
const COLORREF kMouseColors[kShareMouseColorCount] =
{
    RGB(255, 200, 0),    //黄色
    RGB(230, 40, 40),    //红色
    RGB(40, 110, 230),   //蓝色
    RGB(40, 180, 80),    //绿色
    RGB(150, 70, 200),   //紫色
};

//五种预设色的名字，下标同上
const wchar_t* const kMouseColorNames[kShareMouseColorCount] =
{
    TR_NOOP(L"黄色"),
    TR_NOOP(L"红色"),
    TR_NOOP(L"蓝色"),
    TR_NOOP(L"绿色"),
    TR_NOOP(L"紫色"),
};

//菜单项 id 相对 idBase 的偏移：三个开关，以及两组颜色的起点
const UINT kMenuHalo = 0;
const UINT kMenuClick = 1;
const UINT kMenuTrail = 2;
const UINT kMenuHaloColorBase = 10;
const UINT kMenuTrailColorBase = 20;

//每像素字节数（BGRA）与分量的最大值
const int kBytesPerPixel = 4;
const int kChannelMax = 255;
//抗锯齿：边界两侧各半个像素内按距离渐变
const double kHalfPixel = 0.5;
//包围盒在半径之外多留的像素，覆盖抗锯齿的那半个像素
const int kBoxMargin = 1;

//颜色菜单项的图标（第 16 期补充），按 16 像素的设计给出、其它尺寸等比缩放：设计边长、四周透明边、
//细边的亮度（占原色的百分比）、白色对勾的三个点与线宽
const int kSwatchDesignSize = 16;
const int kSwatchInset = 2;
const int kSwatchBorderShade = 60;
const int kPercent = 100;
const int kSwatchCheckAx = 5;
const int kSwatchCheckAy = 8;
const int kSwatchCheckBx = 7;
const int kSwatchCheckBy = 10;
const int kSwatchCheckCx = 11;
const int kSwatchCheckCy = 5;
const int kSwatchCheckWidth = 2;

//设置文本里的键
const char kKeyHalo[] = "halo";
const char kKeyClick[] = "click";
const char kKeyTrail[] = "trail";
const char kKeyHaloColor[] = "halocolor";
const char kKeyTrailColor[] = "trailcolor";
//设置文本的格式化缓冲区长度（字节）
const size_t kSettingsBufBytes = 128;

//颜色取值是否在范围内，不在时按黄色
int normalizeColor(int color)
{
    return (color >= 0 && color < kShareMouseColorCount) ? color : share_mouse_color_yellow;
}

/**
 *  按不透明度把颜色混进一个 BGRA 像素。
 *  @param p     像素的第一个字节（B）
 *  @param color 要混进的颜色
 *  @param alpha 不透明度（0～255），已乘上覆盖率
 */
void blendPixel(uint8_t* p, COLORREF color, int alpha)
{
    if (alpha <= 0)
        return;
    if (alpha > kChannelMax)
        alpha = kChannelMax;
    const int keep = kChannelMax - alpha;
    p[0] = static_cast<uint8_t>((p[0] * keep + GetBValue(color) * alpha) / kChannelMax);
    p[1] = static_cast<uint8_t>((p[1] * keep + GetGValue(color) * alpha) / kChannelMax);
    p[2] = static_cast<uint8_t>((p[2] * keep + GetRValue(color) * alpha) / kChannelMax);
}

/**
 *  到边界的覆盖率：距离在边界内半个像素以内为 1，边界外半个像素以外为 0，中间线性过渡。
 *  @param distance 像素中心到图形中心（或线段）的距离
 *  @param edge     边界（半径或半线宽）
 */
double coverage(double distance, double edge)
{
    if (distance <= edge - kHalfPixel)
        return 1.0;
    if (distance >= edge + kHalfPixel)
        return 0.0;
    return edge + kHalfPixel - distance;
}

//把整数夹在 [lo, hi] 里
int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

//实心圆：只遍历圆的包围盒
void fillDisc(uint8_t* bgra, int width, int height, int stride, double cx, double cy, double radius, COLORREF color,
              int alpha)
{
    const int x0 = clampInt(static_cast<int>(floor(cx - radius)) - kBoxMargin, 0, width - 1);
    const int x1 = clampInt(static_cast<int>(ceil(cx + radius)) + kBoxMargin, 0, width - 1);
    const int y0 = clampInt(static_cast<int>(floor(cy - radius)) - kBoxMargin, 0, height - 1);
    const int y1 = clampInt(static_cast<int>(ceil(cy + radius)) + kBoxMargin, 0, height - 1);
    for (int y = y0; y <= y1; ++y)
    {
        uint8_t* row = bgra + static_cast<size_t>(y) * stride;
        for (int x = x0; x <= x1; ++x)
        {
            const double d = sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            const double cov = coverage(d, radius);
            if (cov > 0.0)
                blendPixel(row + static_cast<size_t>(x) * kBytesPerPixel, color, static_cast<int>(alpha * cov));
        }
    }
}

//圆环：外边界的覆盖率减去内边界的覆盖率
void strokeRing(uint8_t* bgra, int width, int height, int stride, double cx, double cy, double radius,
                double ringWidth, COLORREF color, int alpha)
{
    const double outer = radius + ringWidth / 2;
    const double inner = radius - ringWidth / 2;
    const int x0 = clampInt(static_cast<int>(floor(cx - outer)) - kBoxMargin, 0, width - 1);
    const int x1 = clampInt(static_cast<int>(ceil(cx + outer)) + kBoxMargin, 0, width - 1);
    const int y0 = clampInt(static_cast<int>(floor(cy - outer)) - kBoxMargin, 0, height - 1);
    const int y1 = clampInt(static_cast<int>(ceil(cy + outer)) + kBoxMargin, 0, height - 1);
    for (int y = y0; y <= y1; ++y)
    {
        uint8_t* row = bgra + static_cast<size_t>(y) * stride;
        for (int x = x0; x <= x1; ++x)
        {
            const double d = sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            double cov = coverage(d, outer) - coverage(d, inner);
            if (cov > 0.0)
                blendPixel(row + static_cast<size_t>(x) * kBytesPerPixel, color, static_cast<int>(alpha * cov));
        }
    }
}

//像素到线段的距离
double distanceToSegment(double px, double py, double ax, double ay, double bx, double by)
{
    const double dx = bx - ax;
    const double dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = 0.0;
    if (len2 > 0.0)
    {
        t = ((px - ax) * dx + (py - ay) * dy) / len2;
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    }
    const double cx = ax + t * dx - px;
    const double cy = ay + t * dy - py;
    return sqrt(cx * cx + cy * cy);
}

//圆头粗线段
void strokeSegment(uint8_t* bgra, int width, int height, int stride, POINT a, POINT b, double lineWidth,
                   COLORREF color, int alpha)
{
    const double half = lineWidth / 2;
    const int x0 = clampInt(static_cast<int>(floor((a.x < b.x ? a.x : b.x) - half)) - kBoxMargin, 0, width - 1);
    const int x1 = clampInt(static_cast<int>(ceil((a.x > b.x ? a.x : b.x) + half)) + kBoxMargin, 0, width - 1);
    const int y0 = clampInt(static_cast<int>(floor((a.y < b.y ? a.y : b.y) - half)) - kBoxMargin, 0, height - 1);
    const int y1 = clampInt(static_cast<int>(ceil((a.y > b.y ? a.y : b.y) + half)) + kBoxMargin, 0, height - 1);
    for (int y = y0; y <= y1; ++y)
    {
        uint8_t* row = bgra + static_cast<size_t>(y) * stride;
        for (int x = x0; x <= x1; ++x)
        {
            const double d = distanceToSegment(x, y, a.x, a.y, b.x, b.y);
            const double cov = coverage(d, half);
            if (cov > 0.0)
                blendPixel(row + static_cast<size_t>(x) * kBytesPerPixel, color, static_cast<int>(alpha * cov));
        }
    }
}

/**
 *  从设置文本里读一个整数项。
 *  @param text 设置文本
 *  @param key  键
 *  @param out  [出参] 取值（返回 true 时有效）
 *  @return 找到了这一项且取值是整数时返回 true
 */
bool readSettingInt(const std::string& text, const char* key, int& out)
{
    const std::string prefix = std::string(key) + "=";
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t end = text.find(';', pos);
        if (end == std::string::npos)
            end = text.size();
        const std::string item = text.substr(pos, end - pos);
        if (item.compare(0, prefix.size(), prefix) == 0 && item.size() > prefix.size())
        {
            const std::string value = item.substr(prefix.size());
            char* stop = NULL;
            const long v = strtol(value.c_str(), &stop, 10);
            if (stop != NULL && *stop == '\0')
            {
                out = static_cast<int>(v);
                return true;
            }
            return false;
        }
        pos = end + 1;
    }
    return false;
}

//读一个 0 / 1 开关项；缺失或取值不认识时为 defaultValue
bool readSettingBool(const std::string& text, const char* key, bool defaultValue)
{
    int v = 0;
    if (!readSettingInt(text, key, v) || (v != 0 && v != 1))
        return defaultValue;
    return v == 1;
}

//读一个颜色项；缺失或超出范围时为 defaultValue
int readSettingColor(const std::string& text, const char* key, int defaultValue)
{
    int v = 0;
    if (!readSettingInt(text, key, v) || v < 0 || v >= kShareMouseColorCount)
        return defaultValue;
    return v;
}

} // namespace

ShareMouseEffectSettings::ShareMouseEffectSettings()
    : m_halo(true)
    , m_click(false)
    , m_trail(false)
    , m_haloColor(share_mouse_color_yellow)
    , m_trailColor(share_mouse_color_red)
{
}

COLORREF ShareMouseColorRgb(int color)
{
    return kMouseColors[normalizeColor(color)];
}

std::wstring ShareMouseColorName(int color)
{
    return TR(kMouseColorNames[normalizeColor(color)]);
}

std::string SerializeMouseEffectSettings(const ShareMouseEffectSettings& settings)
{
    char buf[kSettingsBufBytes] = { 0 };
    _snprintf_s(buf, kSettingsBufBytes, _TRUNCATE, "%s=%d;%s=%d;%s=%d;%s=%d;%s=%d", kKeyHalo, settings.m_halo ? 1 : 0,
                kKeyClick, settings.m_click ? 1 : 0, kKeyTrail, settings.m_trail ? 1 : 0, kKeyHaloColor,
                normalizeColor(settings.m_haloColor), kKeyTrailColor, normalizeColor(settings.m_trailColor));
    return std::string(buf);
}

ShareMouseEffectSettings ParseMouseEffectSettings(const std::string& text)
{
    const ShareMouseEffectSettings def;
    ShareMouseEffectSettings out;
    out.m_halo = readSettingBool(text, kKeyHalo, def.m_halo);
    out.m_click = readSettingBool(text, kKeyClick, def.m_click);
    out.m_trail = readSettingBool(text, kKeyTrail, def.m_trail);
    out.m_haloColor = readSettingColor(text, kKeyHaloColor, def.m_haloColor);
    out.m_trailColor = readSettingColor(text, kKeyTrailColor, def.m_trailColor);
    return out;
}

bool MapScreenToFrame(POINT screenPt, const RECT& sourceRect, int frameW, int frameH, POINT& out)
{
    const int srcW = sourceRect.right - sourceRect.left;
    const int srcH = sourceRect.bottom - sourceRect.top;
    if (srcW <= 0 || srcH <= 0 || frameW <= 0 || frameH <= 0)
        return false;
    if (screenPt.x < sourceRect.left || screenPt.x >= sourceRect.right || screenPt.y < sourceRect.top ||
        screenPt.y >= sourceRect.bottom)
        return false;
    //画面与采集矩形大小不同（缩放后的画面）时按比例换算
    out.x = static_cast<LONG>(static_cast<int64_t>(screenPt.x - sourceRect.left) * frameW / srcW);
    out.y = static_cast<LONG>(static_cast<int64_t>(screenPt.y - sourceRect.top) * frameH / srcH);
    return true;
}

ShareMouseMenuItem::ShareMouseMenuItem()
    : m_id(0)
    , m_checked(false)
    , m_swatchColor(-1)
{
}

std::vector<ShareMouseMenuItem> BuildMouseEffectMenu(const ShareMouseEffectSettings& settings, UINT idBase)
{
    std::vector<ShareMouseMenuItem> menu;
    ShareMouseMenuItem halo;
    halo.m_id = idBase + kMenuHalo;
    halo.m_text = TR(L"鼠标光圈");
    halo.m_checked = settings.m_halo;
    menu.push_back(halo);
    ShareMouseMenuItem click;
    click.m_id = idBase + kMenuClick;
    click.m_text = TR(L"点击效果");
    click.m_checked = settings.m_click;
    menu.push_back(click);
    ShareMouseMenuItem trail;
    trail.m_id = idBase + kMenuTrail;
    trail.m_text = TR(L"鼠标轨迹");
    trail.m_checked = settings.m_trail;
    menu.push_back(trail);

    //两个颜色子菜单：当前选中的颜色打勾
    ShareMouseMenuItem haloColors;
    haloColors.m_text = TR(L"光圈颜色");
    ShareMouseMenuItem trailColors;
    trailColors.m_text = TR(L"轨迹颜色");
    for (int c = 0; c < kShareMouseColorCount; ++c)
    {
        ShareMouseMenuItem item;
        item.m_text = ShareMouseColorName(c);
        item.m_swatchColor = c;
        item.m_id = idBase + kMenuHaloColorBase + static_cast<UINT>(c);
        item.m_checked = (normalizeColor(settings.m_haloColor) == c);
        haloColors.m_children.push_back(item);
        item.m_id = idBase + kMenuTrailColorBase + static_cast<UINT>(c);
        item.m_checked = (normalizeColor(settings.m_trailColor) == c);
        trailColors.m_children.push_back(item);
    }
    menu.push_back(haloColors);
    menu.push_back(trailColors);
    return menu;
}

void RenderMouseColorSwatch(uint8_t* bgra, int size, int stride, COLORREF color, bool checked)
{
    if (bgra == NULL || size <= 0 || stride < size * kBytesPerPixel)
        return;
    //整张清成全透明（预乘 alpha 下透明像素的颜色分量也为 0）
    for (int y = 0; y < size; ++y)
        memset(bgra + static_cast<size_t>(y) * stride, 0, static_cast<size_t>(size) * kBytesPerPixel);
    //方块：四周留 kSwatchInset 个像素的透明边（按 16 像素的设计等比缩放），最外一圈是同色加深的细边
    const int inset = size * kSwatchInset / kSwatchDesignSize;
    const int last = size - 1 - inset;
    if (last < inset)
        return;
    const COLORREF border = RGB(GetRValue(color) * kSwatchBorderShade / kPercent,
                                GetGValue(color) * kSwatchBorderShade / kPercent,
                                GetBValue(color) * kSwatchBorderShade / kPercent);
    for (int y = inset; y <= last; ++y)
    {
        uint8_t* row = bgra + static_cast<size_t>(y) * stride;
        for (int x = inset; x <= last; ++x)
        {
            const bool edge = (x == inset || x == last || y == inset || y == last);
            const COLORREF c = edge ? border : color;
            uint8_t* p = row + static_cast<size_t>(x) * kBytesPerPixel;
            p[0] = GetBValue(c);
            p[1] = GetGValue(c);
            p[2] = GetRValue(c);
            p[3] = static_cast<uint8_t>(kChannelMax);
        }
    }
    //当前颜色：方块里画白色对勾（两段圆头粗线，按 16 像素的设计等比缩放）
    if (checked)
    {
        POINT a;
        a.x = kSwatchCheckAx * size / kSwatchDesignSize;
        a.y = kSwatchCheckAy * size / kSwatchDesignSize;
        POINT b;
        b.x = kSwatchCheckBx * size / kSwatchDesignSize;
        b.y = kSwatchCheckBy * size / kSwatchDesignSize;
        POINT c;
        c.x = kSwatchCheckCx * size / kSwatchDesignSize;
        c.y = kSwatchCheckCy * size / kSwatchDesignSize;
        const double width = static_cast<double>(kSwatchCheckWidth) * size / kSwatchDesignSize;
        strokeSegment(bgra, size, size, stride, a, b, width, RGB(kChannelMax, kChannelMax, kChannelMax), kChannelMax);
        strokeSegment(bgra, size, size, stride, b, c, width, RGB(kChannelMax, kChannelMax, kChannelMax), kChannelMax);
    }
}

bool ApplyMouseEffectMenuCommand(ShareMouseEffectSettings& settings, UINT id, UINT idBase)
{
    if (id < idBase)
        return false;
    const UINT offset = id - idBase;
    if (offset == kMenuHalo)
    {
        settings.m_halo = !settings.m_halo;
        return true;
    }
    if (offset == kMenuClick)
    {
        settings.m_click = !settings.m_click;
        return true;
    }
    if (offset == kMenuTrail)
    {
        settings.m_trail = !settings.m_trail;
        return true;
    }
    if (offset >= kMenuHaloColorBase && offset < kMenuHaloColorBase + kShareMouseColorCount)
    {
        settings.m_haloColor = static_cast<int>(offset - kMenuHaloColorBase);
        return true;
    }
    if (offset >= kMenuTrailColorBase && offset < kMenuTrailColorBase + kShareMouseColorCount)
    {
        settings.m_trailColor = static_cast<int>(offset - kMenuTrailColorBase);
        return true;
    }
    return false;
}

ShareMouseEffects::ShareMouseEffects()
    : m_inside(false)
    , m_leftDown(false)
    , m_rightDown(false)
    , m_hasLastTrailPt(false)
{
    m_cursor.x = 0;
    m_cursor.y = 0;
    m_lastTrailPt.x = 0;
    m_lastTrailPt.y = 0;
}

void ShareMouseEffects::SetSettings(const ShareMouseEffectSettings& settings)
{
    m_settings = settings;
    m_settings.m_haloColor = normalizeColor(settings.m_haloColor);
    m_settings.m_trailColor = normalizeColor(settings.m_trailColor);
    //关掉的效果随之清空，重新打开时不会画出关掉之前的残留
    if (!m_settings.m_trail)
    {
        m_trail.clear();
        m_hasLastTrailPt = false;
    }
    if (!m_settings.m_click)
        m_clicks.clear();
}

const ShareMouseEffectSettings& ShareMouseEffects::Settings() const
{
    return m_settings;
}

void ShareMouseEffects::Update(POINT framePt, bool inside, bool leftDown, bool rightDown, int64_t nowMs)
{
    //按下的那一刻：上一次没按、这一次按着。按键状态不论效果开没开都记下，免得打开时把一直按着误当成刚按下
    const bool leftPressed = leftDown && !m_leftDown;
    const bool rightPressed = rightDown && !m_rightDown;
    m_leftDown = leftDown;
    m_rightDown = rightDown;
    m_inside = inside;
    if (inside)
        m_cursor = framePt;

    //丢掉已经消失的轨迹点与点击圈
    while (!m_trail.empty() && nowMs - m_trail.front().m_time > kShareMouseTrailLifeMs)
        m_trail.pop_front();
    for (std::vector<ClickRing>::iterator it = m_clicks.begin(); it != m_clicks.end();)
    {
        if (nowMs - it->m_time >= kShareMouseClickLifeMs)
            it = m_clicks.erase(it);
        else
            ++it;
    }
    if (!inside)
    {
        //离开画面：回来后的第一下照常记点
        m_hasLastTrailPt = false;
        return;
    }

    //光标没动时不重复记点：停下之后旧点按时淡出，不会一直需要出帧。与「上次记点时的位置」比较而不是与轨迹队列的
    //最后一个点比较：队列里的点全部过期之后，静止的光标不能再被当成新点记一次
    if (m_settings.m_trail &&
        (!m_hasLastTrailPt || m_lastTrailPt.x != framePt.x || m_lastTrailPt.y != framePt.y))
    {
        TrailPoint p;
        p.m_pt = framePt;
        p.m_time = nowMs;
        m_trail.push_back(p);
        m_lastTrailPt = framePt;
        m_hasLastTrailPt = true;
    }
    if (m_settings.m_click && (leftPressed || rightPressed))
    {
        ClickRing ring;
        ring.m_pt = framePt;
        ring.m_time = nowMs;
        ring.m_color = leftPressed ? kShareMouseClickLeftColor : kShareMouseClickRightColor;
        m_clicks.push_back(ring);
        //左右键同时按下：再加一个右键的圈
        if (leftPressed && rightPressed)
        {
            ring.m_color = kShareMouseClickRightColor;
            m_clicks.push_back(ring);
        }
    }
}

void ShareMouseEffects::Render(uint8_t* bgra, int width, int height, int stride, int64_t nowMs) const
{
    if (bgra == NULL || width <= 0 || height <= 0 || stride < width * kBytesPerPixel)
        return;

    //轨迹：相邻两点连成一段，按较新那一点的年龄决定粗细与浓淡，两端都还没消失的段才画
    if (m_settings.m_trail)
    {
        const COLORREF color = ShareMouseColorRgb(m_settings.m_trailColor);
        for (size_t i = 1; i < m_trail.size(); ++i)
        {
            const int64_t olderAge = nowMs - m_trail[i - 1].m_time;
            const int64_t age = nowMs - m_trail[i].m_time;
            if (olderAge > kShareMouseTrailLifeMs || age > kShareMouseTrailLifeMs)
                continue;
            const double fresh = 1.0 - static_cast<double>(age < 0 ? 0 : age) / kShareMouseTrailLifeMs;
            const double lineWidth =
                kShareMouseTrailMinWidth + (kShareMouseTrailMaxWidth - kShareMouseTrailMinWidth) * fresh;
            strokeSegment(bgra, width, height, stride, m_trail[i - 1].m_pt, m_trail[i].m_pt, lineWidth, color,
                          static_cast<int>(kShareMouseTrailAlpha * fresh));
        }
    }

    //光圈：只在光标在画面里时画
    if (m_settings.m_halo && m_inside)
    {
        fillDisc(bgra, width, height, stride, m_cursor.x, m_cursor.y, kShareMouseHaloRadius,
                 ShareMouseColorRgb(m_settings.m_haloColor), kShareMouseHaloAlpha);
    }

    //点击圈：随时间变大、变淡
    if (m_settings.m_click)
    {
        for (size_t i = 0; i < m_clicks.size(); ++i)
        {
            const int64_t age = nowMs - m_clicks[i].m_time;
            if (age < 0 || age >= kShareMouseClickLifeMs)
                continue;
            const double progress = static_cast<double>(age) / kShareMouseClickLifeMs;
            const double radius =
                kShareMouseClickStartRadius + (kShareMouseClickEndRadius - kShareMouseClickStartRadius) * progress;
            strokeRing(bgra, width, height, stride, m_clicks[i].m_pt.x, m_clicks[i].m_pt.y, radius,
                       kShareMouseClickRingWidth, m_clicks[i].m_color,
                       static_cast<int>(kShareMouseClickAlpha * (1.0 - progress)));
        }
    }
}

bool ShareMouseEffects::IsAnimating(int64_t nowMs) const
{
    return TrailPointCount(nowMs) > 0 || ActiveClickCount(nowMs) > 0;
}

size_t ShareMouseEffects::TrailPointCount(int64_t nowMs) const
{
    size_t count = 0;
    for (size_t i = 0; i < m_trail.size(); ++i)
    {
        if (nowMs - m_trail[i].m_time <= kShareMouseTrailLifeMs)
            ++count;
    }
    return count;
}

size_t ShareMouseEffects::ActiveClickCount(int64_t nowMs) const
{
    size_t count = 0;
    for (size_t i = 0; i < m_clicks.size(); ++i)
    {
        const int64_t age = nowMs - m_clicks[i].m_time;
        if (age >= 0 && age < kShareMouseClickLifeMs)
            ++count;
    }
    return count;
}

void ShareMouseEffects::Reset()
{
    m_trail.clear();
    m_clicks.clear();
    m_inside = false;
    m_leftDown = false;
    m_rightDown = false;
    m_hasLastTrailPt = false;
}


void LogicalMouseButtons(bool physicalLeft, bool physicalRight, bool swapped, bool& left, bool& right)
{
    left = swapped ? physicalRight : physicalLeft;
    right = swapped ? physicalLeft : physicalRight;
}

ShareMouseFxStage::ShareMouseFxStage()
    : m_settingsChanged(true)
    , m_inside(false)
    , m_animating(false)
    , m_dirty(false)
    , m_encodedValid(false)
    , m_encodedInside(false)
    , m_encodedAnimating(false)
{
    m_framePt.x = 0;
    m_framePt.y = 0;
    m_encodedPt.x = 0;
    m_encodedPt.y = 0;
}

void ShareMouseFxStage::SetSettings(const ShareMouseEffectSettings& settings)
{
    std::lock_guard<std::mutex> guard(m_settingsMutex);
    m_settings = settings;
    m_settingsChanged = true;
}

ShareMouseEffectSettings ShareMouseFxStage::Settings() const
{
    std::lock_guard<std::mutex> guard(m_settingsMutex);
    return m_settings;
}

bool ShareMouseFxStage::Tick(POINT screenPt, bool leftDown, bool rightDown, const RECT& sourceRect, int frameW,
                             int frameH, int64_t nowMs)
{
    //取走新设置：效果状态随之更新，并要求出一帧（打开的效果画上去、关掉的效果擦掉）
    {
        std::lock_guard<std::mutex> guard(m_settingsMutex);
        if (m_settingsChanged)
        {
            m_effects.SetSettings(m_settings);
            m_settingsChanged = false;
            m_dirty = true;
        }
    }
    POINT framePt;
    framePt.x = 0;
    framePt.y = 0;
    m_inside = MapScreenToFrame(screenPt, sourceRect, frameW, frameH, framePt);
    if (m_inside)
        m_framePt = framePt;
    m_effects.Update(framePt, m_inside, leftDown, rightDown, nowMs);
    m_animating = m_effects.IsAnimating(nowMs);

    if (m_dirty)
        return true;
    //轨迹或点击圈还在淡出；或者上次编码时还在淡出、现在淡完了（再出一帧擦掉残影）
    if (m_animating || (m_encodedValid && m_encodedAnimating))
        return true;
    //光圈跟着光标：位置或「在不在画面里」变了才出帧
    if (!m_effects.Settings().m_halo)
        return false;
    if (!m_encodedValid)
        return m_inside;
    if (m_inside != m_encodedInside)
        return true;
    return m_inside && (m_framePt.x != m_encodedPt.x || m_framePt.y != m_encodedPt.y);
}

const uint8_t* ShareMouseFxStage::Compose(const uint8_t* raw, int width, int height, int stride, int64_t nowMs,
                                          int& outStride)
{
    outStride = stride;
    //没有可见的效果（光圈关着或光标在画面外，且没有轨迹与点击圈）：原样编码原始画面，省一次拷贝
    const bool haloVisible = m_effects.Settings().m_halo && m_inside;
    if (raw == NULL || width <= 0 || height <= 0 || (!haloVisible && !m_effects.IsAnimating(nowMs)))
        return raw;
    const int packed = width * kBytesPerPixel;
    m_buffer.resize(static_cast<size_t>(packed) * height);
    for (int y = 0; y < height; ++y)
        memcpy(&m_buffer[static_cast<size_t>(packed) * y], raw + static_cast<size_t>(stride) * y, packed);
    m_effects.Render(&m_buffer[0], width, height, packed, nowMs);
    outStride = packed;
    return &m_buffer[0];
}

void ShareMouseFxStage::OnEncoded()
{
    m_encodedValid = true;
    m_encodedInside = m_inside;
    m_encodedPt = m_framePt;
    m_encodedAnimating = m_animating;
    m_dirty = false;
}

void ShareMouseFxStage::Reset()
{
    m_effects.Reset();
    m_inside = false;
    m_animating = false;
    m_encodedValid = false;
    m_encodedInside = false;
    m_encodedAnimating = false;
    m_buffer.clear();
}

} // namespace screenshare
