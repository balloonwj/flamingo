/**
 *  MainPanelRowMarkTests.cpp
 *
 *  主面板列表行标记的渲染测试：
 *    GPR 组 —— 「群」Tab 里置顶的群画出与会话 Tab 相同的置顶样式（整行淡灰底 + 左侧
 *              橙色竖条），且不影响第一行右侧的成员数。
 *    FAV 组 —— 收藏的联系人 / 群在名字后面画出收藏徽章（书签图标）。
 *    RRV 组 —— 右侧占位空格"个数不变就不写回基类"之后，占位内容仍与一次设全的结果一致。
 *    SUBB 组 —— 桌面字幕开着的会话在名字后面画出蓝色字幕徽章（选中行改白色），标签 [subtitle][rowmark]。
 *    MU-C3 —— 会话被手动标为未读时的小红点（docs/会话标为未读方案.md），标签 [markunread][rowmark]。
 *    CRL 组 —— 会话行的排布（docs/消息提醒模式与消息盒子方案.md 第 13 节，2026-10-07 重排）：所有行的时间右对齐
 *              在同一列；星标、收藏、字幕、通话中、仅通知都画在第二行右侧；名字只按时间截断；摘要按剩余宽度截断、
 *              不压到徽章；宽度不够时按顺序省略徽章。标签 [convrow][rowmark]。
 *    PRC 组 —— 头像右下角的在线状态角标：离开、忙碌画在头像右下角，在线、离线不画；选中行的描边改用选中底色；
 *              通讯录行不再有行末状态徽标。标签 [presencecorner][rowmark]。
 *
 *  标记本身只是一个 bool，能出问题的全在绘制，故与 StarredRowBadgeTests.cpp 同一做法：
 *  把树画到白底离屏位图上，按行分区数指定颜色的像素。MainPanelTreeView 的行矩形是
 *  m_rcItem.top + 行号 × 行高，位置确定，可以直接按行号切分区域。
 *
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>
#include <vector>

#include <gdiplus.h>   // 效果图用例把画布存成 PNG

#include "../UI/BalloonMain/MainPanelTreeView.h"
#include "../UI/BalloonMain/MainPanelIcons.h"   // NMI 组直接画提醒模式徽章
#include "../net/Protocol.h"                    // enum conversation_notify_mode
#include "../UI/BalloonMain/NotifyModePolicy.h"  // ShowsUnreadDot：MU-C3 的行装饰判定
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"   // RRV 组量占位宽度用的默认字体

using namespace balloonmain;

namespace {

// 置顶行左侧竖条的颜色，与 MainPanelTreeView.cpp 的 kPinnedAccentColor 同值。
const COLORREF kPinnedAccent = RGB(0xFF, 0x7A, 0x45);

// 测试用树的尺寸与行高（逻辑像素）。宽度按主面板群列表的实际宽度取。
const int kTreeW = 320;
const int kTreeH = 200;
const int kRowH  = 40;

// 判定"文字像素"的亮度上限：白底与置顶行的淡灰底都比它亮，灰色文字比它暗。
const int kTextLumaMax = 0xB0;

// 右侧成员数所在的区域宽度：从行右边缘往左这么宽的一条。
const int kRightTextBandW = 60;

// 左侧竖条所在的区域宽度：从行左边缘往右这么宽的一条（竖条宽 3，留些余量）。
const int kLeftAccentBandW = 8;

/**
 *  一组命中像素覆盖的范围；一个像素都没命中时 m_empty 为 true。
 */
struct PixelBox
{
    bool m_empty;   // 一个像素都没命中
    RECT m_rc;      // 覆盖全部命中像素的最小矩形（右、下边界为开区间）；m_empty 为 true 时无意义

    PixelBox()
        : m_empty(true)
    {
        ::SetRectEmpty(&m_rc);
    }

    // 把 (x, y) 这个像素并入范围。
    void Add(int x, int y)
    {
        if (m_empty)
        {
            m_rc.left   = x;
            m_rc.top    = y;
            m_rc.right  = x + 1;
            m_rc.bottom = y + 1;
            m_empty = false;
            return;
        }
        if (x < m_rc.left)
            m_rc.left = x;
        if (y < m_rc.top)
            m_rc.top = y;
        if (x + 1 > m_rc.right)
            m_rc.right = x + 1;
        if (y + 1 > m_rc.bottom)
            m_rc.bottom = y + 1;
    }
};

/**
 *  一张白底离屏画布：构造时建 DIB，Paint 把树画上去，之后可按区域数像素。
 */
class OffscreenCanvas
{
public:
    OffscreenCanvas(int w, int h)
        : m_w(w)
        , m_h(h)
        , m_dc(NULL)
        , m_bmp(NULL)
        , m_bits(NULL)
    {
        HDC screenDC = ::GetDC(NULL);
        m_dc = ::CreateCompatibleDC(screenDC);
        ::ReleaseDC(NULL, screenDC);

        BITMAPINFO bi;
        ::ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = w;
        bi.bmiHeader.biHeight      = -h;   // 负高 = 自上而下
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = NULL;
        m_bmp = ::CreateDIBSection(m_dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        m_bits = (const unsigned int*)bits;
        if (m_bmp != NULL)
        {
            ::SelectObject(m_dc, m_bmp);
        }
    }

    ~OffscreenCanvas()
    {
        if (m_bmp != NULL)
        {
            ::DeleteObject(m_bmp);
        }
        if (m_dc != NULL)
        {
            ::DeleteDC(m_dc);
        }
    }

    // 画布是否建成。
    bool Ok() const
    {
        return m_dc != NULL && m_bmp != NULL && m_bits != NULL;
    }

    // 先铺白底，再把树画上去。
    void Paint(MainPanelTreeView& tree)
    {
        RECT all = { 0, 0, m_w, m_h };
        FillWhite(all);
        tree.SetRect(all);
        tree.OnPaint(m_dc, all);
    }

    // 把 rc 铺成白色。
    void FillWhite(const RECT& rc)
    {
        HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
        ::FillRect(m_dc, &rc, white);
        ::DeleteObject(white);
    }

    // 把树画到画布的 rc 区域里（不先铺底，调用方自己决定底色）。
    void PaintAt(MainPanelTreeView& tree, const RECT& rc)
    {
        tree.SetRect(rc);
        tree.OnPaint(m_dc, rc);
    }

    // 模拟滚动视口里的局部重画：树的矩形是 treeRect（可以比画布高、可以从负坐标开始，
    // 与主面板滚动视图里列表的摆法一致），只重画 dirty 这一块。dirty 先铺白底。
    void PaintRegion(MainPanelTreeView& tree, const RECT& treeRect, const RECT& dirty)
    {
        FillWhite(dirty);
        tree.SetRect(treeRect);
        tree.OnPaint(m_dc, dirty);
    }

    // 在 (x, y) 处用默认 GUI 字体写一行说明文字。
    void DrawCaption(int x, int y, LPCTSTR text)
    {
        HGDIOBJ oldFont = ::SelectObject(m_dc, ::GetStockObject(DEFAULT_GUI_FONT));
        ::SetBkMode(m_dc, TRANSPARENT);
        ::SetTextColor(m_dc, RGB(0x66, 0x66, 0x66));
        ::TextOut(m_dc, x, y, text, (int)_tcslen(text));
        ::SelectObject(m_dc, oldFont);
    }

    // 把整张画布存成 PNG。成功返回 true。
    bool SavePng(LPCWSTR path) const
    {
        Gdiplus::Bitmap bmp(m_bmp, NULL);
        CLSID pngClsid;
        if (!FindPngEncoder(pngClsid))
        {
            return false;
        }
        return bmp.Save(path, &pngClsid, NULL) == Gdiplus::Ok;
    }

    // 数 rc 范围内颜色恰为 want 的像素。
    int CountColor(const RECT& rc, COLORREF want) const
    {
        const unsigned int target = ((unsigned int)GetRValue(want) << 16)
                                  | ((unsigned int)GetGValue(want) << 8)
                                  |  (unsigned int)GetBValue(want);
        int count = 0;
        for (int y = rc.top; y < rc.bottom; ++y)
        {
            for (int x = rc.left; x < rc.right; ++x)
            {
                if ((m_bits[y * m_w + x] & 0x00FFFFFFu) == target)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    // 数 rc 范围内三个通道都不超过 lumaMax 的像素（即足够暗、可视为文字的像素）。
    int CountDark(const RECT& rc, int lumaMax) const
    {
        return DarkBoundingBoxAndCount(rc, lumaMax, NULL);
    }

    // rc 范围内颜色恰为 want 的像素覆盖的范围。
    PixelBox BoundingBox(const RECT& rc, COLORREF want) const
    {
        const unsigned int target = ((unsigned int)GetRValue(want) << 16)
                                  | ((unsigned int)GetGValue(want) << 8)
                                  |  (unsigned int)GetBValue(want);
        PixelBox box;
        for (int y = rc.top; y < rc.bottom; ++y)
        {
            for (int x = rc.left; x < rc.right; ++x)
            {
                if ((m_bits[y * m_w + x] & 0x00FFFFFFu) == target)
                {
                    box.Add(x, y);
                }
            }
        }
        return box;
    }

    // rc 范围内三个通道都与 want 相差不超过 tolerance 的像素覆盖的范围（线条图标抗锯齿后
    // 恰为目标色的像素很少，按容差找）。
    PixelBox NearColorBoundingBox(const RECT& rc, COLORREF want, int tolerance) const
    {
        PixelBox box;
        for (int y = rc.top; y < rc.bottom; ++y)
        {
            for (int x = rc.left; x < rc.right; ++x)
            {
                if (IsNear(m_bits[y * m_w + x], want, tolerance))
                {
                    box.Add(x, y);
                }
            }
        }
        return box;
    }

    // rc 范围内三个通道都与 want 相差不超过 tolerance 的像素个数。
    int CountNearColor(const RECT& rc, COLORREF want, int tolerance) const
    {
        int count = 0;
        for (int y = rc.top; y < rc.bottom; ++y)
        {
            for (int x = rc.left; x < rc.right; ++x)
            {
                if (IsNear(m_bits[y * m_w + x], want, tolerance))
                {
                    ++count;
                }
            }
        }
        return count;
    }

    // rc 范围内足够暗（可视为文字）的像素覆盖的范围。
    PixelBox DarkBoundingBox(const RECT& rc, int lumaMax) const
    {
        PixelBox box;
        DarkBoundingBoxAndCount(rc, lumaMax, &box);
        return box;
    }

    // rc 范围内"深灰"像素覆盖的范围：三个通道都不超过 lumaMax，且三者最大差不超过
    // maxSpread。用来只认灰色文字，把同样偏暗的彩色图形（如在线状态的绿）排除在外。
    PixelBox GrayTextBoundingBox(const RECT& rc, int lumaMax, int maxSpread) const
    {
        PixelBox box;
        for (int y = rc.top; y < rc.bottom; ++y)
        {
            for (int x = rc.left; x < rc.right; ++x)
            {
                const unsigned int p = m_bits[y * m_w + x];
                const int r = (int)((p >> 16) & 0xFF);
                const int g = (int)((p >> 8) & 0xFF);
                const int b = (int)(p & 0xFF);
                const int hi = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
                const int lo = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
                if (hi <= lumaMax && hi - lo <= maxSpread)
                {
                    box.Add(x, y);
                }
            }
        }
        return box;
    }

    // rc 范围内不是白底的像素（任一通道低于 whiteMin）覆盖的范围。
    PixelBox NonWhiteBoundingBox(const RECT& rc, int whiteMin) const
    {
        PixelBox box;
        for (int y = rc.top; y < rc.bottom; ++y)
        {
            for (int x = rc.left; x < rc.right; ++x)
            {
                const unsigned int p = m_bits[y * m_w + x];
                const int r = (int)((p >> 16) & 0xFF);
                const int g = (int)((p >> 8) & 0xFF);
                const int b = (int)(p & 0xFF);
                if (r < whiteMin || g < whiteMin || b < whiteMin)
                {
                    box.Add(x, y);
                }
            }
        }
        return box;
    }

private:
    // 像素 p（0x00RRGGBB）三个通道是否都与 want 相差不超过 tolerance。
    static bool IsNear(unsigned int p, COLORREF want, int tolerance)
    {
        const int r = (int)((p >> 16) & 0xFF);
        const int g = (int)((p >> 8) & 0xFF);
        const int b = (int)(p & 0xFF);
        return abs(r - (int)GetRValue(want)) <= tolerance
            && abs(g - (int)GetGValue(want)) <= tolerance
            && abs(b - (int)GetBValue(want)) <= tolerance;
    }

    // 在 GDI+ 已注册的图像编码器里找 PNG 编码器的 CLSID。
    static bool FindPngEncoder(CLSID& out)
    {
        UINT count = 0;
        UINT bytes = 0;
        if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
        {
            return false;
        }
        std::vector<BYTE> buf(bytes);
        Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)&buf[0];
        if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
        {
            return false;
        }
        for (UINT i = 0; i < count; ++i)
        {
            if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
            {
                out = codecs[i].Clsid;
                return true;
            }
        }
        return false;
    }

    // 数 rc 范围内三个通道都不超过 lumaMax 的像素；box 非空时顺带求出它们覆盖的范围。
    int DarkBoundingBoxAndCount(const RECT& rc, int lumaMax, PixelBox* box) const
    {
        int count = 0;
        for (int y = rc.top; y < rc.bottom; ++y)
        {
            for (int x = rc.left; x < rc.right; ++x)
            {
                const unsigned int p = m_bits[y * m_w + x];
                const int r = (int)((p >> 16) & 0xFF);
                const int g = (int)((p >> 8) & 0xFF);
                const int b = (int)(p & 0xFF);
                if (r <= lumaMax && g <= lumaMax && b <= lumaMax)
                {
                    ++count;
                    if (box != NULL)
                    {
                        box->Add(x, y);
                    }
                }
            }
        }
        return count;
    }

private:
    int                 m_w;      // 画布宽，像素
    int                 m_h;      // 画布高，像素
    HDC                 m_dc;     // 内存 DC，析构时释放
    HBITMAP             m_bmp;    // 32bpp DIB，选入 m_dc，析构时释放
    const unsigned int* m_bits;   // DIB 像素首地址，归 m_bmp 所有
};

// 在作用域内启动 GDI+，离开时关闭。标记是用 GDI+ 画的；产品里客户端启动时就初始化了
// GDI+，测试进程却没有 —— 只有带子节点的树画展开箭头时才会顺带初始化，NMK 组的树都是
// 扁平的，不自己启动的话一个标记都画不出来（2026-10-01 排查 NMK1/NMK2 时查明）。
class NmkScopedGdiplus
{
public:
    // 启动 GDI+；失败时 Ok() 返回 false。
    NmkScopedGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    // 关闭 GDI+。
    ~NmkScopedGdiplus()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    // GDI+ 是否启动成功。
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，关闭时要用
    bool      m_ok;      // 是否启动成功
};

// 第 row 行（从 0 起，含分组标题行）的整行矩形。
RECT RowRect(int row)
{
    RECT rc = { 0, row * kRowH, kTreeW, (row + 1) * kRowH };
    return rc;
}

// 第 row 行左侧竖条所在的窄条。
RECT LeftBand(int row)
{
    RECT rc = RowRect(row);
    rc.right = rc.left + kLeftAccentBandW;
    return rc;
}

// 第 row 行上半行右侧、成员数所在的区域。
RECT RightTopBand(int row)
{
    RECT rc = RowRect(row);
    rc.left   = rc.right - kRightTextBandW;
    rc.bottom = rc.top + kRowH / 2;
    return rc;
}

// 造一棵群树：分组标题一行，下面两个群，行版式与主面板群 Tab 相同
//（群名 + 群号副标签 + 第一行右侧成员数）。返回两个群节点的 id。
void BuildGroupTree(MainPanelTreeView& tree, int& groupA, int& groupB)
{
    tree.SetRowHeight(kRowH);
    const int root = tree.AddRoot(_T("我的群聊 · 2"));
    tree.SetExpanded(root, true);

    groupA = tree.AddChild(root, _T("产品设计组"));
    tree.SetItemSubLabel(groupA, _T("群号 268435458"));
    tree.SetItemRightTopText(groupA, _T("5人"));

    groupB = tree.AddChild(root, _T("研发周会"));
    tree.SetItemSubLabel(groupB, _T("群号 268435459"));
    tree.SetItemRightTopText(groupB, _T("12人"));
}

} // anonymous namespace

TEST_CASE("GPR1 群树置顶行画出左侧橙色竖条，成员数照常显示", "[grouppin][rowmark]")
{
    MainPanelTreeView tree;
    int groupA = 0;
    int groupB = 0;
    BuildGroupTree(tree, groupA, groupB);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());

    //没置顶时哪一行都没有竖条
    canvas.Paint(tree);
    CHECK(canvas.CountColor(LeftBand(1), kPinnedAccent) == 0);
    CHECK(canvas.CountColor(LeftBand(2), kPinnedAccent) == 0);

    //置顶第一个群（第 1 行；第 0 行是分组标题）
    tree.SetItemConversationFlags(groupA, true, notify_mode_allow);
    canvas.Paint(tree);
    const int accentA = canvas.CountColor(LeftBand(1), kPinnedAccent);
    INFO("置顶行竖条像素数 = " << accentA);
    CHECK(accentA > 0);
    CHECK(canvas.CountColor(LeftBand(2), kPinnedAccent) == 0);
    CHECK(canvas.CountColor(LeftBand(0), kPinnedAccent) == 0);

    //两行右上角的成员数都照常画出来
    const int textA = canvas.CountDark(RightTopBand(1), kTextLumaMax);
    const int textB = canvas.CountDark(RightTopBand(2), kTextLumaMax);
    INFO("成员数文字像素：置顶行 = " << textA << "，普通行 = " << textB);
    CHECK(textA > 0);
    CHECK(textB > 0);

    //取消置顶后竖条消失
    tree.SetItemConversationFlags(groupA, false, notify_mode_allow);
    canvas.Paint(tree);
    CHECK(canvas.CountColor(LeftBand(1), kPinnedAccent) == 0);
}

// =====================================================================
// FAV 组：收藏徽章
// =====================================================================

namespace {

// 收藏徽章（书签图标）的颜色，与 MainPanelTreeView.cpp 的 kFavoriteIconColor 同值。
const COLORREF kFavoriteRose = RGB(0xEC, 0x48, 0x99);

// 特别关注星标的颜色，与 MainPanelTreeView.cpp 的 kStarIconColor 同值。
const COLORREF kStarGold = RGB(0xF5, 0xA6, 0x23);

// 会话行行高（逻辑像素），与主面板会话 Tab 相同。
const int kConvRowH = 48;

// 判定"名字文字像素"的亮度上限：名字是近黑色 (30,30,30)，时间与摘要是浅灰 (140,140,140)，取两者之间，
// 只认名字。
const int kNameLumaMax = 0x70;

// 未读胶囊的红色，与 MainPanelTreeView.cpp 的 kBadgeRed 同值。
const COLORREF kUnreadRed = RGB(0xFF, 0x4D, 0x4F);

// 判定"灰色文字像素"时三个通道之间允许的最大差值。灰色文字三通道几乎相等；彩色的标记（绿、金、玫红）
// 三通道相差一百多，远超此值，不会被当成文字。
const int kGraySpreadMax = 0x30;

// 第二行胶囊（高 16、与未读胶囊同一条中线，中线在行中线下 6 像素）的上沿比行中线高出的像素数。
const int kSecondLineCapsuleRise = 2;

// 造一张 32bpp 的纯色方形位图，调用方负责 DeleteObject。
HBITMAP MakeSolidBitmap(int size, COLORREF color)
{
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = size;
    bi.bmiHeader.biHeight      = -size;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HDC screen = ::GetDC(NULL);
    HBITMAP bmp = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ::ReleaseDC(NULL, screen);
    if (bmp == NULL || bits == NULL)
    {
        return NULL;
    }
    // premultiplied alpha：alpha 取满，三通道直接写颜色即可。
    const unsigned int px = 0xFF000000u
                          | ((unsigned int)GetRValue(color) << 16)
                          | ((unsigned int)GetGValue(color) << 8)
                          |  (unsigned int)GetBValue(color);
    unsigned int* p = (unsigned int*)bits;
    for (int i = 0; i < size * size; ++i)
    {
        p[i] = px;
    }
    return bmp;
}

} // anonymous namespace

TEST_CASE("FAV1 联系人行：收藏后名字后面画出书签徽章，取消后消失", "[favorite][rowmark]")
{
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    const int root = tree.AddRoot(_T("设计部"));
    tree.SetExpanded(root, true);
    const int node = tree.AddChild(root, _T("莫梅"));
    tree.SetItemSubLabel(node, _T("资深设计师"));

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());

    canvas.Paint(tree);
    CHECK(canvas.CountColor(RowRect(1), kFavoriteRose) == 0);

    tree.SetItemFavorite(node, true);
    canvas.Paint(tree);
    const int lit = canvas.CountColor(RowRect(1), kFavoriteRose);
    INFO("收藏后的徽章像素数 = " << lit);
    CHECK(lit > 0);
    //分组标题行不受影响
    CHECK(canvas.CountColor(RowRect(0), kFavoriteRose) == 0);

    tree.SetItemFavorite(node, false);
    canvas.Paint(tree);
    CHECK(canvas.CountColor(RowRect(1), kFavoriteRose) == 0);
}

TEST_CASE("FAV2 同时特别关注与收藏：星标在前、徽章在后，互不重叠", "[favorite][rowmark]")
{
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    const int root = tree.AddRoot(_T("设计部"));
    tree.SetExpanded(root, true);
    const int node = tree.AddChild(root, _T("莫梅"));
    tree.SetItemStarred(node, true);
    tree.SetItemFavorite(node, true);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);

    const PixelBox star = canvas.BoundingBox(RowRect(1), kStarGold);
    const PixelBox fav  = canvas.BoundingBox(RowRect(1), kFavoriteRose);
    REQUIRE_FALSE(star.m_empty);
    REQUIRE_FALSE(fav.m_empty);
    INFO("星标右边界 = " << star.m_rc.right << "，徽章左边界 = " << fav.m_rc.left);
    CHECK(fav.m_rc.left >= star.m_rc.right);
}

TEST_CASE("FAV3 会话行名字超长时：星标与书签画在第二行，不压到未读胶囊，名字不为它们让位", "[favorite][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    tree.SetRowHeight(kConvRowH);
    const int node = tree.AddRoot(_T("莫梅、江怡、黄涛波、秦明芳、向瑶娜、罗俊辉、赵卓轩、韩雪飞、韩娟丹"));
    tree.SetItemSummary(node, _T("晚上一起吃饭吗"));
    tree.SetItemRightStacked(node, _T("09:12"), 3);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    const RECT row = { 0, 0, kTreeW, kConvRowH };
    const int mid = kConvRowH / 2;

    //先不打标记画一遍，量出名字被截断到哪儿
    canvas.Paint(tree);
    RECT topHalf = row;
    topHalf.bottom = mid;
    const PixelBox nameBefore = canvas.DarkBoundingBox(topHalf, kNameLumaMax);
    REQUIRE_FALSE(nameBefore.m_empty);

    //打上两个标记：都画在第二行（中线以下），书签在星标右边，都在未读胶囊左边
    tree.SetItemStarred(node, true);
    tree.SetItemFavorite(node, true);
    canvas.Paint(tree);
    const PixelBox star   = canvas.BoundingBox(row, kStarGold);
    const PixelBox fav    = canvas.BoundingBox(row, kFavoriteRose);
    const PixelBox unread = canvas.BoundingBox(row, kUnreadRed);
    REQUIRE_FALSE(star.m_empty);
    REQUIRE_FALSE(fav.m_empty);
    REQUIRE_FALSE(unread.m_empty);
    INFO("星标 y[" << star.m_rc.top << "," << star.m_rc.bottom << ")，书签 x[" << fav.m_rc.left << ","
         << fav.m_rc.right << ")，未读胶囊左边界 = " << unread.m_rc.left);
    CHECK(star.m_rc.top >= mid);
    CHECK(fav.m_rc.top >= mid);
    CHECK(fav.m_rc.left >= star.m_rc.right);
    CHECK(fav.m_rc.right <= unread.m_rc.left);

    //名字只按时间截断：打不打标记，名字画到的位置都一样
    const PixelBox nameAfter = canvas.DarkBoundingBox(topHalf, kNameLumaMax);
    REQUIRE_FALSE(nameAfter.m_empty);
    CHECK(nameAfter.m_rc.right == nameBefore.m_rc.right);
}

TEST_CASE("FAV4 多行时只有收藏的那些行有徽章", "[favorite][rowmark]")
{
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    const int root = tree.AddRoot(_T("我的群聊 · 3"));
    tree.SetExpanded(root, true);
    const int a = tree.AddChild(root, _T("产品设计组"));
    const int b = tree.AddChild(root, _T("研发周会"));
    const int c = tree.AddChild(root, _T("午饭小分队"));
    tree.SetItemRightTopText(a, _T("5人"));
    tree.SetItemRightTopText(b, _T("12人"));
    tree.SetItemRightTopText(c, _T("3人"));
    (void)b;

    tree.SetItemFavorite(a, true);
    tree.SetItemFavorite(c, true);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);
    CHECK(canvas.CountColor(RowRect(1), kFavoriteRose) > 0);
    CHECK(canvas.CountColor(RowRect(2), kFavoriteRose) == 0);
    CHECK(canvas.CountColor(RowRect(3), kFavoriteRose) > 0);

    //清空全部行装饰时收藏标记一并清掉
    tree.ClearAllRowDecorations();
    canvas.Paint(tree);
    CHECK(canvas.CountColor(RowRect(1), kFavoriteRose) == 0);
    CHECK(canvas.CountColor(RowRect(3), kFavoriteRose) == 0);
}

// =====================================================================
// ICB 组：「通话中」徽章（docs/音视频通话优化方案.md 第 3 项）
// =====================================================================

namespace {

// 「通话中」胶囊的底色，与 MainPanelTreeView.cpp 的 kInCallFill 同值。
const COLORREF kInCallGreen = RGB(0x1F, 0xA8, 0x5C);
// 胶囊里图标与文字的颜色（白）。
const COLORREF kInCallWhite = RGB(0xFF, 0xFF, 0xFF);
// 媒体类型：1 语音、2 视频（与 IMediaHost::OnCallPresence 一致）。
const int kInCallVoice = 1;
const int kInCallVideo = 2;

} // anonymous namespace

TEST_CASE("ICB1 联系人行：通话中时名字后面画出绿色胶囊，结束后消失", "[incall][rowmark]")
{
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    const int root = tree.AddRoot(_T("设计部"));
    tree.SetExpanded(root, true);
    const int node = tree.AddChild(root, _T("莫梅"));

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);
    CHECK(canvas.CountColor(RowRect(1), kInCallGreen) == 0);
    CHECK(tree.GetItemInCallMedia(node) == 0);

    tree.SetItemInCall(node, kInCallVoice);
    CHECK(tree.GetItemInCallMedia(node) == kInCallVoice);
    canvas.Paint(tree);
    const int lit = canvas.CountColor(RowRect(1), kInCallGreen);
    INFO("通话中胶囊的绿色像素数 = " << lit);
    CHECK(lit > 0);
    //胶囊里有白色的图标与文字
    const PixelBox capsule = canvas.BoundingBox(RowRect(1), kInCallGreen);
    REQUIRE_FALSE(capsule.m_empty);
    CHECK(canvas.CountColor(capsule.m_rc, kInCallWhite) > 0);
    //分组标题行不受影响
    CHECK(canvas.CountColor(RowRect(0), kInCallGreen) == 0);

    tree.SetItemInCall(node, 0);
    CHECK(tree.GetItemInCallMedia(node) == 0);
    canvas.Paint(tree);
    CHECK(canvas.CountColor(RowRect(1), kInCallGreen) == 0);
}

TEST_CASE("ICB2 同时特别关注、收藏与通话中：星标、书签、胶囊依次排开，互不重叠", "[incall][rowmark]")
{
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    const int root = tree.AddRoot(_T("设计部"));
    tree.SetExpanded(root, true);
    const int node = tree.AddChild(root, _T("莫梅"));
    tree.SetItemStarred(node, true);
    tree.SetItemFavorite(node, true);
    tree.SetItemInCall(node, kInCallVideo);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);

    const PixelBox star = canvas.BoundingBox(RowRect(1), kStarGold);
    const PixelBox fav = canvas.BoundingBox(RowRect(1), kFavoriteRose);
    const PixelBox call = canvas.BoundingBox(RowRect(1), kInCallGreen);
    REQUIRE_FALSE(star.m_empty);
    REQUIRE_FALSE(fav.m_empty);
    REQUIRE_FALSE(call.m_empty);
    INFO("书签右边界 = " << fav.m_rc.right << "，胶囊左边界 = " << call.m_rc.left);
    CHECK(fav.m_rc.left >= star.m_rc.right);
    CHECK(call.m_rc.left >= fav.m_rc.right);
    //胶囊不超出树的右边
    CHECK(call.m_rc.right <= kTreeW);
}

TEST_CASE("ICB3 多行时只有通话中的行有胶囊；清空行装饰后一并去掉", "[incall][rowmark]")
{
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    const int root = tree.AddRoot(_T("我的群聊 · 3"));
    tree.SetExpanded(root, true);
    const int a = tree.AddChild(root, _T("产品设计组"));
    const int b = tree.AddChild(root, _T("研发周会"));
    const int c = tree.AddChild(root, _T("午饭小分队"));
    tree.SetItemRightTopText(a, _T("5人"));
    tree.SetItemRightTopText(b, _T("12人"));
    tree.SetItemRightTopText(c, _T("3人"));
    tree.SetItemInCall(b, kInCallVoice);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);
    CHECK(canvas.CountColor(RowRect(1), kInCallGreen) == 0);
    CHECK(canvas.CountColor(RowRect(2), kInCallGreen) > 0);
    CHECK(canvas.CountColor(RowRect(3), kInCallGreen) == 0);

    //语音改视频：仍是一个胶囊（图标换成摄像机），状态随之更新
    tree.SetItemInCall(b, kInCallVideo);
    CHECK(tree.GetItemInCallMedia(b) == kInCallVideo);
    canvas.Paint(tree);
    CHECK(canvas.CountColor(RowRect(2), kInCallGreen) > 0);

    tree.ClearAllRowDecorations();
    CHECK(tree.GetItemInCallMedia(b) == 0);
    canvas.Paint(tree);
    CHECK(canvas.CountColor(RowRect(2), kInCallGreen) == 0);
}

TEST_CASE("ICB4 会话行：通话中胶囊画在第二行，不与时间、未读胶囊重叠", "[incall][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    tree.SetRowHeight(kConvRowH);
    const int node = tree.AddRoot(_T("产品设计组、研发周会、午饭小分队、周末羽毛球、读书会、年会筹备组、新人培训"));
    tree.SetItemSummary(node, _T("[群语音通话]"));
    tree.SetItemRightStacked(node, _T("01:07"), 4);
    tree.SetItemInCall(node, kInCallVoice);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    const RECT row = { 0, 0, kTreeW, kConvRowH };
    const int mid = kConvRowH / 2;
    canvas.Paint(tree);

    RECT topRight = row;
    topRight.left   = kTreeW - kRightTextBandW;
    topRight.bottom = mid;
    const PixelBox timeText = canvas.GrayTextBoundingBox(topRight, kTextLumaMax, kGraySpreadMax);
    const PixelBox call     = canvas.BoundingBox(row, kInCallGreen);
    const PixelBox unread   = canvas.BoundingBox(row, kUnreadRed);
    REQUIRE_FALSE(timeText.m_empty);
    REQUIRE_FALSE(call.m_empty);
    REQUIRE_FALSE(unread.m_empty);
    INFO("胶囊 x[" << call.m_rc.left << "," << call.m_rc.right << ") y[" << call.m_rc.top << ","
         << call.m_rc.bottom << ")，时间 y 下边界 = " << timeText.m_rc.bottom << "，未读胶囊左边界 = "
         << unread.m_rc.left);
    //胶囊高 16、与未读胶囊同一条中线，上沿最多比行中线高 2 像素，在时间文字下面
    CHECK(call.m_rc.top >= mid - kSecondLineCapsuleRise);
    CHECK(call.m_rc.top >= timeText.m_rc.bottom);
    CHECK(call.m_rc.right <= unread.m_rc.left);
}

// =====================================================================
// 效果图（默认不运行）
// =====================================================================
//
// 用与界面完全相同的绘制代码，把群 Tab、会话 Tab（另加一棵窄的会话树，看徽章按顺序省略）、联系人 Tab 各几行
// 典型内容画到一张 PNG 上，供人工核对置顶样式、行标记与头像在线状态角标。标签带 [.]，默认不运行；输出路径
// 取自环境变量 FLAMINGO_SNAPSHOT_PATH，没设时直接跳过。用法：
//     set FLAMINGO_SNAPSHOT_PATH=E:\flamingo\snapshots\row_marks.png
//     FlamingoUnit.exe "[snapshot]"

namespace {

// 效果图里每棵树的宽度（逻辑像素），与主面板列表宽度相当；窄会话树的宽度，用来看徽章按顺序省略。
const int kSnapTreeW = 320;
const int kSnapNarrowTreeW = 230;

// 效果图的外边距与每棵树上方说明文字所占的高度（逻辑像素）。
const int kSnapMargin   = 16;
const int kSnapCaptionH = 22;

// 各棵树的高度：群树 5 行 × 40，会话树 5 行 × 48，窄会话树 2 行 × 48，联系人树 5 行 × 40。
const int kSnapGroupTreeH   = 5 * kRowH;
const int kSnapConvTreeH    = 5 * kConvRowH;
const int kSnapNarrowTreeH  = 2 * kConvRowH;
const int kSnapContactTreeH = 5 * kRowH;

// 头像边长（逻辑像素），与主面板群树 / 会话树一致。
const int kSnapAvatarSmall = 32;
const int kSnapAvatarLarge = 36;

// 充当头像的几种底色。
const COLORREF kSnapAvatarColors[] =
{
    RGB(0x5B, 0x8F, 0xF9), RGB(0x61, 0xDD, 0xAA), RGB(0xF6, 0xBD, 0x16), RGB(0xE8, 0x68, 0x4A),
};
const int kSnapAvatarColorCount = sizeof(kSnapAvatarColors) / sizeof(kSnapAvatarColors[0]);

// 效果图里几行的未读数。
const int kSnapUnreadSmall = 2;
const int kSnapUnreadMid   = 5;
const int kSnapUnreadBig   = 12;

// 把一棵树设成会话 Tab 的样子：行高 48、头像 36 的圆形。
void SetupSnapConvTree(MainPanelTreeView& tree)
{
    tree.SetRowHeight(kConvRowH);
    tree.SetIconSize(kSnapAvatarLarge);
    tree.SetIconCornerRadius(kSnapAvatarLarge / 2);
    tree.SetIconUsesAlpha(true);
}

} // anonymous namespace

TEST_CASE("SNAP 行标记效果图：置顶群、会话行第二行标记与头像在线状态角标", "[.][snapshot][rowmark]")
{
    TCHAR path[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariable(_T("FLAMINGO_SNAPSHOT_PATH"), path, MAX_PATH) == 0)
    {
        SUCCEED("FLAMINGO_SNAPSHOT_PATH not set, snapshot skipped");
        return;
    }
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());

    std::vector<HBITMAP> bitmaps;
    for (int i = 0; i < kSnapAvatarColorCount; ++i)
    {
        bitmaps.push_back(MakeSolidBitmap(kSnapAvatarLarge, kSnapAvatarColors[i]));
    }

    // ---- 群 Tab：两个置顶群排在最前，其中一个同时收藏；长自动群名收藏后名字被省略 ----
    MainPanelTreeView groups;
    groups.SetRowHeight(kRowH);
    groups.SetIconSize(kSnapAvatarSmall);
    groups.SetIconCornerRadius(kSnapAvatarSmall / 2);
    groups.SetIconUsesAlpha(true);
    const int gRoot = groups.AddRoot(_T("我的群聊 · 4"));
    groups.SetExpanded(gRoot, true);
    groups.SetItemSelectable(gRoot, false);
    const TCHAR* groupNames[] =
    {
        _T("产品设计组"), _T("研发周会"),
        _T("莫梅、江怡、黄涛波、秦明芳等"), _T("午饭小分队"),
    };
    const TCHAR* groupCounts[] = { _T("5人"), _T("12人"), _T("6人"), _T("3人") };
    const bool groupPinned[]   = { true, true, false, false };
    const bool groupFavorite[] = { true, false, true, false };
    for (int i = 0; i < 4; ++i)
    {
        const int id = groups.AddChild(gRoot, groupNames[i]);
        CString no;
        no.Format(_T("群号 26843545%d"), 6 + i);
        groups.SetItemSubLabel(id, no);
        groups.SetItemRightTopText(id, groupCounts[i]);
        groups.SetItemIcon(id, bitmaps[i % kSnapAvatarColorCount]);
        groups.SetItemConversationFlags(id, groupPinned[i], notify_mode_allow);
        groups.SetItemFavorite(id, groupFavorite[i]);
    }

    // ---- 会话 Tab：置顶 + 特别关注 + 收藏 + 离开；收藏的群（仅通知、视频通话中、不提醒）；忙碌 + 字幕 + 选中；
    //      消息盒子；离线（头像灰度）----
    MainPanelTreeView convs;
    SetupSnapConvTree(convs);
    const int c1 = convs.AddRoot(_T("莫梅"));
    convs.SetItemSummary(c1, _T("晚上一起吃饭吗"));
    convs.SetItemRightStacked(c1, _T("09:12"), kSnapUnreadSmall);
    convs.SetItemConversationFlags(c1, true, notify_mode_starred);
    convs.SetItemStarred(c1, true);
    convs.SetItemFavorite(c1, true);
    convs.SetItemPresence(c1, presence_away);
    convs.SetItemIcon(c1, bitmaps[0]);
    const int c2 = convs.AddRoot(_T("产品设计组"));
    convs.SetItemSummary(c2, _T("江怡：设计稿已经更新，大家看一下有没有问题"));
    convs.SetItemRightStacked(c2, _T("昨天"), kSnapUnreadMid);
    convs.SetItemConversationFlags(c2, false, notify_mode_silent);
    convs.SetItemFavorite(c2, true);
    convs.SetItemInCall(c2, kInCallVideo);
    convs.SetItemNotifyOnly(c2, true);
    convs.SetItemIcon(c2, bitmaps[1]);
    const int c3 = convs.AddRoot(_T("黄涛波"));
    convs.SetItemSummary(c3, _T("好的，我下午三点之前把会议纪要整理好发到群里"));
    convs.SetItemRightStacked(c3, _T("周一"), 0);
    convs.SetItemUnreadDot(c3, true);
    convs.SetItemSubtitle(c3, true);
    convs.SetItemPresence(c3, presence_busy);
    convs.SetItemIcon(c3, bitmaps[2]);
    convs.SetCurSel(c3, false);
    const int c4 = convs.AddRoot(_T("消息盒子"));
    convs.SetItemSummary(c4, _T("研发周会：明天的会改到下午两点"));
    convs.SetItemRightStacked(c4, _T("周一"), kSnapUnreadBig);
    convs.SetItemQuietUnread(c4, true);
    convs.SetItemIcon(c4, bitmaps[3]);
    const int c5 = convs.AddRoot(_T("秦明芳"));
    convs.SetItemSummary(c5, _T("收到"));
    convs.SetItemRightStacked(c5, _T("09/28"), 0);
    convs.SetItemPresence(c5, presence_offline);
    convs.SetItemIcon(c5, bitmaps[0]);
    convs.SetItemIconGrayed(c5, true);

    // ---- 窄会话树：宽度不够时按「收藏 → 字幕 → 仅通知 → 星标」省略，通话中与未读数保留 ----
    MainPanelTreeView narrow;
    SetupSnapConvTree(narrow);
    const int n1 = narrow.AddRoot(_T("莫梅"));
    narrow.SetItemSummary(n1, _T("晚上一起吃饭吗"));
    narrow.SetItemRightStacked(n1, _T("09:12"), kSnapUnreadSmall);
    narrow.SetItemStarred(n1, true);
    narrow.SetItemFavorite(n1, true);
    narrow.SetItemSubtitle(n1, true);
    narrow.SetItemInCall(n1, kInCallVoice);
    narrow.SetItemIcon(n1, bitmaps[0]);
    const int n2 = narrow.AddRoot(_T("产品设计组"));
    narrow.SetItemSummary(n2, _T("江怡：设计稿已经更新"));
    narrow.SetItemRightStacked(n2, _T("昨天"), kSnapUnreadMid);
    narrow.SetItemConversationFlags(n2, false, notify_mode_silent);
    narrow.SetItemStarred(n2, true);
    narrow.SetItemFavorite(n2, true);
    narrow.SetItemNotifyOnly(n2, true);
    narrow.SetItemIcon(n2, bitmaps[1]);

    // ---- 联系人 Tab（部门树）：特别关注 + 收藏 + 离开、收藏 + 忙碌、在线、离线（头像灰度）----
    MainPanelTreeView contacts;
    contacts.SetRowHeight(kRowH);
    contacts.SetIconSize(kSnapAvatarSmall);
    contacts.SetIconCornerRadius(kSnapAvatarSmall / 2);
    contacts.SetIconUsesAlpha(true);
    const int dRoot = contacts.AddRoot(_T("设计部 · 4"));
    contacts.SetExpanded(dRoot, true);
    contacts.SetItemSelectable(dRoot, false);
    const TCHAR* people[] = { _T("莫梅"), _T("江怡"), _T("黄涛波"), _T("秦明芳") };
    const bool personStar[] = { true, false, false, false };
    const bool personFav[]  = { true, true, false, false };
    const int personPresence[] = { presence_away, presence_busy, presence_online, presence_offline };
    for (int i = 0; i < 4; ++i)
    {
        const int id = contacts.AddChild(dRoot, people[i]);
        contacts.SetItemSubLabel(id, _T("资深设计师"));
        contacts.SetItemPresence(id, personPresence[i]);
        contacts.SetItemIconGrayed(id, personPresence[i] == presence_offline);
        contacts.SetItemIcon(id, bitmaps[(i + 1) % kSnapAvatarColorCount]);
        contacts.SetItemStarred(id, personStar[i]);
        contacts.SetItemFavorite(id, personFav[i]);
    }

    // ---- 排版：四棵树自上而下，每棵上方一行说明 ----
    const int canvasW = kSnapTreeW + kSnapMargin * 2;
    const int canvasH = kSnapMargin * 5 + kSnapCaptionH * 4
                      + kSnapGroupTreeH + kSnapConvTreeH + kSnapNarrowTreeH + kSnapContactTreeH;
    OffscreenCanvas canvas(canvasW, canvasH);
    REQUIRE(canvas.Ok());
    RECT all = { 0, 0, canvasW, canvasH };
    canvas.FillWhite(all);

    int y = kSnapMargin;
    canvas.DrawCaption(kSnapMargin, y, _T("群 Tab"));
    y += kSnapCaptionH;
    RECT rcGroups = { kSnapMargin, y, kSnapMargin + kSnapTreeW, y + kSnapGroupTreeH };
    canvas.PaintAt(groups, rcGroups);
    y = rcGroups.bottom + kSnapMargin;

    canvas.DrawCaption(kSnapMargin, y, _T("会话 Tab"));
    y += kSnapCaptionH;
    RECT rcConvs = { kSnapMargin, y, kSnapMargin + kSnapTreeW, y + kSnapConvTreeH };
    canvas.PaintAt(convs, rcConvs);
    y = rcConvs.bottom + kSnapMargin;

    canvas.DrawCaption(kSnapMargin, y, _T("会话 Tab（窄，徽章按顺序省略）"));
    y += kSnapCaptionH;
    RECT rcNarrow = { kSnapMargin, y, kSnapMargin + kSnapNarrowTreeW, y + kSnapNarrowTreeH };
    canvas.PaintAt(narrow, rcNarrow);
    y = rcNarrow.bottom + kSnapMargin;

    canvas.DrawCaption(kSnapMargin, y, _T("联系人 Tab（部门树）"));
    y += kSnapCaptionH;
    RECT rcContacts = { kSnapMargin, y, kSnapMargin + kSnapTreeW, y + kSnapContactTreeH };
    canvas.PaintAt(contacts, rcContacts);

    CHECK(canvas.SavePng(path));

    for (size_t i = 0; i < bitmaps.size(); ++i)
    {
        ::DeleteObject(bitmaps[i]);
    }
}

// =====================================================================
// RRV —— 右侧占位（UpdateRightReserve_）：占位空格个数与上次相同时不再写回基类
//
// 2026-10-01 为提速加了"个数不变就跳过 SetItemRightText"。能出错的地方有两处：跳过时
// 基类里的内容本应变化却没变（占位偏窄，左侧文字顶到右侧列下面）；整表重建后记录没清，
// 新写的内容被误判为"和上次一样"而跳过。本组用例只看基类 rightText 的内容，不看绘制。
// =====================================================================

namespace {

// 会话行的时间文字与未读数，RRV 各用例共用。
const TCHAR* const kRrvTime   = _T("23:56");
const int          kRrvUnread = 5;

// 用基类右侧文字的默认字体量一段文字的像素宽，作为"占位是否够宽"的参照。
int MeasureWithDefaultFont(LPCTSTR text)
{
    HDC dc = ::GetDC(NULL);
    HGDIOBJ old = ::SelectObject(dc, balloonwjui::DuiResMgr::Inst().GetDefaultFont());
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32(dc, text, (int)_tcslen(text), &sz);
    ::SelectObject(dc, old);
    ::ReleaseDC(NULL, dc);
    return sz.cx;
}

// 占位字符串是否全由空格组成。
bool IsAllSpaces(const CString& s)
{
    for (int i = 0; i < s.GetLength(); ++i)
    {
        if (s[i] != _T(' '))
        {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("RRV1 会话行先设时间与未读、再设置顶:占位与一次设全的结果一致", "[rowmark][reserve]")
{
    // a：填充时的实际顺序（先时间与未读，再置顶与免打扰）—— 第二次算出的个数与第一次相同，走跳过分支。
    MainPanelTreeView treeA;
    const int a = treeA.AddRoot(_T("张伟"));
    treeA.SetItemRightStacked(a, kRrvTime, kRrvUnread);
    treeA.SetItemConversationFlags(a, true, notify_mode_allow);

    // b：反过来的顺序，最后一次调用一定按全部状态算出占位并写入，作为参照。
    MainPanelTreeView treeB;
    const int b = treeB.AddRoot(_T("张伟"));
    treeB.SetItemConversationFlags(b, true, notify_mode_allow);
    treeB.SetItemRightStacked(b, kRrvTime, kRrvUnread);

    const CString textA = treeA.GetItemRightText(a);
    const CString textB = treeB.GetItemRightText(b);
    CHECK(textA == textB);
    REQUIRE_FALSE(textA.IsEmpty());
    CHECK(IsAllSpaces(textA));

    // 占位至少要盖住上排的时间文字，否则左侧的名字会顶到时间下面。
    CHECK(MeasureWithDefaultFont(textA) >= MeasureWithDefaultFont(kRrvTime));
}

TEST_CASE("RRV2 提醒模式从允许提醒改成接收不提醒:占位变宽并写回", "[rowmark][reserve]")
{
    MainPanelTreeView tree;
    const int node = tree.AddRoot(_T("李娜"));
    tree.SetItemRightStacked(node, kRrvTime, kRrvUnread);
    tree.SetItemConversationFlags(node, false, notify_mode_allow);
    const CString before = tree.GetItemRightText(node);

    tree.SetItemConversationFlags(node, false, notify_mode_silent);
    const CString after = tree.GetItemRightText(node);

    // 接收不提醒在未读胶囊左边多一个徽章，下排变宽；这里时间比胶囊窄，占位由下排决定。
    CHECK(after.GetLength() > before.GetLength());

    // 与一开始就是接收不提醒的行结果一致。
    MainPanelTreeView fresh;
    const int f = fresh.AddRoot(_T("李娜"));
    fresh.SetItemConversationFlags(f, false, notify_mode_silent);
    fresh.SetItemRightStacked(f, kRrvTime, kRrvUnread);
    CHECK(after == fresh.GetItemRightText(f));
}

TEST_CASE("RRV3 ClearAllRowDecorations 之后重设同样的内容:照常写回,不被误跳过", "[rowmark][reserve]")
{
    MainPanelTreeView tree;
    const int node = tree.AddRoot(_T("王平"));
    tree.SetItemRightStacked(node, kRrvTime, kRrvUnread);
    const CString expected = tree.GetItemRightText(node);
    REQUIRE_FALSE(expected.IsEmpty());

    // 模拟整表重建：装饰记录清空，基类里的占位也回到空（新节点的初始状态）。
    tree.ClearAllRowDecorations();
    tree.SetItemRightText(node, _T(""));

    // 再设同样的内容：记录已清，必须重新写回；若没清，会因"个数与上次相同"被跳过而留空。
    tree.SetItemRightStacked(node, kRrvTime, kRrvUnread);
    CHECK(tree.GetItemRightText(node) == expected);
}

// =====================================================================
// NMK —— 名字后的标记只为需要重画的行计算（2026-10-01）
//
// 改动前 PaintNameMarks 每次绘制都把树里所有带标记的行过一遍，逐行算名字的文字区域
// （其中要在全部可见行里逐个找这一行），5000 人的通讯录每画一次要几十到一百毫秒。
// 改为只看需要重画的那几行之后，画出来的结果必须与原来完全一样：需要重画的行里该有的
// 标记一个不少，不需要重画的行一个不画。
// =====================================================================

namespace {

// NMK 组用的大树：行数与带标记的行的分布。每 12 行一个收藏，其中每 24 行的那个同时带星标。
const int kNmkRows        = 3000;
const int kNmkMarkEvery   = 12;
const int kNmkMarkOffset  = 4;
const int kNmkStarEvery   = 24;

// 建一棵 kNmkRows 行的扁平树，按上面的规律打标记。返回各行的节点 id。
std::vector<int> BuildMarkedTree(MainPanelTreeView& tree)
{
    tree.SetRowHeight(kRowH);
    std::vector<int> ids;
    for (int i = 0; i < kNmkRows; ++i)
    {
        CString label;
        label.Format(_T("成员 %d"), i);
        const int id = tree.AddRoot(label);
        ids.push_back(id);
        if (i % kNmkMarkEvery == kNmkMarkOffset)
        {
            tree.SetItemFavorite(id, true);
            if (i % kNmkStarEvery == kNmkMarkOffset)
            {
                tree.SetItemStarred(id, true);
            }
        }
    }
    return ids;
}

// 树向上滚过 firstRow 行时，它在画布坐标里的矩形（与滚动视图的摆法一致）。
RECT ScrolledTreeRect(int firstRow)
{
    RECT rc;
    rc.left   = 0;
    rc.right  = kTreeW;
    rc.top    = -firstRow * kRowH;
    rc.bottom = rc.top + kNmkRows * kRowH;
    return rc;
}

} // namespace

TEST_CASE("NMK1 3000 行的树滚到第 100 行,只重画视口:视口里带标记的行画出标记,其余不画", "[rowmark][namemarks]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    BuildMarkedTree(tree);
    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());

    const int firstRow = 100;   // 100 % 12 == 4：视口第一行带收藏，且 100 % 24 == 4 也带星标
    const RECT viewport = { 0, 0, kTreeW, kTreeH };
    canvas.PaintRegion(tree, ScrolledTreeRect(firstRow), viewport);

    const int visibleRows = kTreeH / kRowH;
    for (int r = 0; r < visibleRows; ++r)
    {
        const int i = firstRow + r;
        const bool favorite = (i % kNmkMarkEvery == kNmkMarkOffset);
        const bool starred  = favorite && (i % kNmkStarEvery == kNmkMarkOffset);
        INFO("row " << i);
        CHECK((canvas.CountColor(RowRect(r), kFavoriteRose) > 0) == favorite);
        CHECK((canvas.CountColor(RowRect(r), kStarGold) > 0) == starred);
    }
}

TEST_CASE("NMK2 带标记的行只有一部分落在重画区域里也照样画出,完全在外的不画", "[rowmark][namemarks]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    std::vector<int> ids;
    for (int i = 0; i < 5; ++i)
    {
        CString label;
        label.Format(_T("成员 %d"), i);
        ids.push_back(tree.AddRoot(label));
    }
    tree.SetItemFavorite(ids[0], true);   // 跨上边界
    tree.SetItemFavorite(ids[3], true);   // 跨下边界
    tree.SetItemFavorite(ids[4], true);   // 完全在重画区域之外

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    const RECT all = { 0, 0, kTreeW, kTreeH };
    canvas.FillWhite(all);
    // 重画区域从第 0 行中线到第 3 行中线：第 0、3 行各有一半在里面
    const RECT dirty = { 0, kRowH / 2, kTreeW, 3 * kRowH + kRowH / 2 };
    canvas.PaintRegion(tree, all, dirty);

    CHECK(canvas.CountColor(RowRect(0), kFavoriteRose) > 0);
    CHECK(canvas.CountColor(RowRect(3), kFavoriteRose) > 0);
    CHECK(canvas.CountColor(RowRect(4), kFavoriteRose) == 0);
}

// NMK3 的平均耗时上限（毫秒）：3000 行、250 个带标记的行，重画一个视口大小的区域。
// 2026-10-01 在 Debug|Win32 下实测：改动前（遍历全部可见行、逐个带标记的行算名字区域）
// 27.4 毫秒，改动后（只看与脏区相交的行）0.49 毫秒。上限取在两者之间、离改动后足够远，
// 机器负载的波动不至于跨过它。
const double kNmkPaintLimitMs = 5.0;

TEST_CASE("NMK3 3000 行与 250 个带标记的行,重画一个视口在限定时间内完成", "[rowmark][namemarks]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    const int kRounds = 20;
    MainPanelTreeView tree;
    BuildMarkedTree(tree);
    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    const RECT viewport = { 0, 0, kTreeW, kTreeH };

    LARGE_INTEGER freq;
    LARGE_INTEGER t0;
    LARGE_INTEGER t1;
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&t0);
    for (int k = 0; k < kRounds; ++k)
    {
        canvas.PaintRegion(tree, ScrolledTreeRect(1000 + k), viewport);
    }
    ::QueryPerformanceCounter(&t1);
    const double avgMs = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart / kRounds;
    INFO("average paint of one viewport: " << avgMs << " ms");
    CHECK(avgMs <= kNmkPaintLimitMs);
}

// =====================================================================
// NMR / NMI —— 消息提醒模式的行徽章（docs/消息提醒模式与消息盒子方案.md §4.3）
//
// 接收不提醒、收进消息盒子、屏蔽三种模式在行右下角画徽章（铃铛加斜线 / 抽屉柜 / 禁止符），
// 徽章宽度要计入右侧占位；允许提醒与特别关注不画（特别关注是名字后面的星标）。
// =====================================================================

namespace {

// NMR 组的会话行用的时间文字与未读数
const TCHAR* const kNmrTime   = _T("10:21");
const int          kNmrUnread = 9;

// NMI 组把图标画进的位图边长（像素），与 MainPanelIcons 的 viewBox 相同，一个单位一个像素
const int kNmiIconPx = 24;

// NMI 组判定"画出了东西"的不透明像素下限：12 像素以上的描边图形在 24 像素画布上远不止这么多
const int kNmiMinInkPixels = 40;

// NMI 组判定两个图标"不一样"的最少差异像素数
const int kNmiMinDiffPixels = 20;

// 透明度超过这个值的像素算作"有墨"
const BYTE kNmiInkAlpha = 64;

// 建一棵只有一行会话的树：时间、未读、提醒模式一次设好。返回该行占位的空格个数。
int ConvRowReserve(int notifyMode, int unread)
{
    MainPanelTreeView tree;
    const int node = tree.AddRoot(_T("午餐搭子 478"));
    tree.SetItemRightStacked(node, kNmrTime, unread);
    tree.SetItemConversationFlags(node, false, notifyMode);
    return tree.GetItemRightText(node).GetLength();
}

// 把一个提醒模式徽章画进 kNmiIconPx 见方的透明位图，返回每个像素"是否有墨"
std::vector<bool> RenderIconInk(int kind)
{
    Gdiplus::Bitmap bmp(kNmiIconPx, kNmiIconPx, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        const Gdiplus::REAL kStroke = 2.0f;
        DrawLineIcon(g, kind, 0.0f, 0.0f, (Gdiplus::REAL)kNmiIconPx, Gdiplus::Color(255, 0, 0, 0), kStroke);
    }
    std::vector<bool> ink;
    for (int y = 0; y < kNmiIconPx; ++y)
    {
        for (int x = 0; x < kNmiIconPx; ++x)
        {
            Gdiplus::Color c;
            bmp.GetPixel(x, y, &c);
            ink.push_back(c.GetA() > kNmiInkAlpha);
        }
    }
    return ink;
}

int CountInk(const std::vector<bool>& ink)
{
    int n = 0;
    for (size_t i = 0; i < ink.size(); ++i)
    {
        if (ink[i])
            ++n;
    }
    return n;
}

int CountDiff(const std::vector<bool>& a, const std::vector<bool>& b)
{
    int n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        if (a[i] != b[i])
            ++n;
    }
    return n;
}

} // namespace

TEST_CASE("NMR1 接收不提醒、收进消息盒子、屏蔽三种模式的会话行多出徽章占位，允许提醒与特别关注不多", "[notifymode][rowmark]")
{
    const int allow   = ConvRowReserve(notify_mode_allow, kNmrUnread);
    const int starred = ConvRowReserve(notify_mode_starred, kNmrUnread);
    CHECK(starred == allow);
    CHECK(ConvRowReserve(notify_mode_silent, kNmrUnread) > allow);
    CHECK(ConvRowReserve(notify_mode_msgbox, kNmrUnread) > allow);
    CHECK(ConvRowReserve(notify_mode_block, kNmrUnread) > allow);

    //行上记下的模式：只记会画徽章的三种
    MainPanelTreeView tree;
    const int node = tree.AddRoot(_T("发布协调"));
    tree.SetItemConversationFlags(node, false, notify_mode_msgbox);
    CHECK(tree.GetItemNotifyMode(node) == notify_mode_msgbox);
    tree.SetItemConversationFlags(node, false, notify_mode_starred);
    CHECK(tree.GetItemNotifyMode(node) == notify_mode_allow);
}

TEST_CASE("NMR2 屏蔽的会话行不带未读数时只留徽章的占位，比带未读的接收不提醒窄", "[notifymode][rowmark]")
{
    //主面板对屏蔽的会话传未读数 0（ShowsUnreadBadge），这里核对占位随之只算徽章
    const int blockNoBadge  = ConvRowReserve(notify_mode_block, 0);
    const int allowNoBadge  = ConvRowReserve(notify_mode_allow, 0);
    const int silentWithBadge = ConvRowReserve(notify_mode_silent, kNmrUnread);
    CHECK(blockNoBadge >= allowNoBadge);
    CHECK(silentWithBadge > blockNoBadge);
}

TEST_CASE("NMR3 联系人行设了模式也留出徽章占位，改回允许提醒后恢复", "[notifymode][rowmark]")
{
    //联系人树的行没有时间与未读，只有名字、签名与在线状态徽标
    MainPanelTreeView tree;
    const int node = tree.AddRoot(_T("江屿白"));
    tree.SetItemSubLabel(node, _T("设计组长"));
    const int before = tree.GetItemRightText(node).GetLength();

    tree.SetItemNotifyMode(node, notify_mode_silent);
    CHECK(tree.GetItemNotifyMode(node) == notify_mode_silent);
    const CString withBadge = tree.GetItemRightText(node);
    CHECK(withBadge.GetLength() > before);
    CHECK(IsAllSpaces(withBadge));

    //特别关注不画徽章，星标另由 SetItemStarred 管
    tree.SetItemNotifyMode(node, notify_mode_starred);
    CHECK(tree.GetItemNotifyMode(node) == notify_mode_allow);
    CHECK(tree.GetItemRightText(node).GetLength() == before);
}

TEST_CASE("NMR4 模式 0 → 1 → 0：占位变宽后恢复原样", "[notifymode][rowmark]")
{
    MainPanelTreeView tree;
    const int node = tree.AddRoot(_T("需求评审 117"));
    tree.SetItemRightStacked(node, kNmrTime, kNmrUnread);
    tree.SetItemConversationFlags(node, false, notify_mode_allow);
    const CString before = tree.GetItemRightText(node);

    tree.SetItemNotifyMode(node, notify_mode_silent);
    CHECK(tree.GetItemRightText(node).GetLength() > before.GetLength());

    tree.SetItemNotifyMode(node, notify_mode_allow);
    CHECK(tree.GetItemRightText(node) == before);
}

TEST_CASE("NMI1 三个提醒模式徽章都画得出来，彼此不同，也与喇叭加叉不同", "[notifymode][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());

    const std::vector<bool> bellSlash = RenderIconInk(kIconBellSlash);
    const std::vector<bool> drawer    = RenderIconInk(kIconDrawer);
    const std::vector<bool> block     = RenderIconInk(kIconBlock);
    const std::vector<bool> mute      = RenderIconInk(kIconMute);

    CHECK(CountInk(bellSlash) >= kNmiMinInkPixels);
    CHECK(CountInk(drawer) >= kNmiMinInkPixels);
    CHECK(CountInk(block) >= kNmiMinInkPixels);

    CHECK(CountDiff(bellSlash, drawer) >= kNmiMinDiffPixels);
    CHECK(CountDiff(bellSlash, block) >= kNmiMinDiffPixels);
    CHECK(CountDiff(drawer, block) >= kNmiMinDiffPixels);
    CHECK(CountDiff(bellSlash, mute) >= kNmiMinDiffPixels);
}

// =====================================================================
// SUBB 组：「字幕模式」徽章（docs/聊天字幕模式方案.md 第 1 节第 13 条）
// =====================================================================

namespace {

// 字幕徽章的蓝色，与 MainPanelTreeView.cpp 的 kSubtitleIconColor 同值。
const COLORREF kSubtitleBlue = RGB(0x3B, 0x82, 0xF6);
// 找蓝色线条图标时的颜色容差：抗锯齿后线条中心接近目标色，边缘与白底混合的像素不算。
// 白底上的其它标记（金色星、玫红书签、绿色状态与胶囊、深灰文字）都远在这个范围之外。
const int kSubtitleBlueTolerance = 48;
// 选中行上判定「没有画成蓝色」时的容差：选中底色品牌蓝 #2D6CDF 与徽章蓝的差值大于它。
const int kSubtitleStrictTolerance = 6;
// 选中行上的白色（徽章改画白色）：细线条抗锯齿后与蓝底混合，中心像素也只有约 (213, 226, 248)，
// 故按「三个通道都不低于 0xA0」数亮像素；选中底色品牌蓝 #2D6CDF 的红通道只有 0x2D，不会被算进来。
const COLORREF kSelectedWhite = RGB(0xFF, 0xFF, 0xFF);
const int kSelectedWhiteTolerance = 0x5F;
// 名字文字右边与搜索区域之间留的像素：最后一个字的抗锯齿边缘可能略超出文字矩形
const int kLabelEdgeMargin = 2;
// 字幕徽章的边长（像素），与 MainPanelTreeView.cpp 的 kSubtitleIconSize 同值。
const int kSubtitleBadgePx = 12;

// 某行名字文字右边的那一段（须在画过一次之后调用，行矩形才确定）。字幕徽章画在名字之后，只在这一段里
// 找蓝色：ClearType 文字边缘有偏蓝的彩色像素，会落进容差范围（2026-10-05 实测，名字「莫梅」所在的一行
// 不设徽章也有 7 个）。取不到名字矩形时返回整行。
RECT RightOfLabel(const MainPanelTreeView& tree, int node, const RECT& row)
{
    RECT region = row;
    RECT label;
    if (tree.GetItemLabelTextRect(node, label))
    {
        region.left = label.right + kLabelEdgeMargin;
    }
    return region;
}

} // anonymous namespace

TEST_CASE("SUBB1 联系人行：字幕开着时名字后面画出蓝色字幕徽章，关掉后消失", "[subtitle][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    const int root = tree.AddRoot(_T("设计部"));
    tree.SetExpanded(root, true);
    const int node = tree.AddChild(root, _T("莫梅"));

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);
    const RECT nodeRegion = RightOfLabel(tree, node, RowRect(1));
    const RECT rootRegion = RightOfLabel(tree, root, RowRect(0));
    CHECK(canvas.CountNearColor(nodeRegion, kSubtitleBlue, kSubtitleBlueTolerance) == 0);
    CHECK_FALSE(tree.IsItemSubtitle(node));

    tree.SetItemSubtitle(node, true);
    CHECK(tree.IsItemSubtitle(node));
    canvas.Paint(tree);
    const int lit = canvas.CountNearColor(nodeRegion, kSubtitleBlue, kSubtitleBlueTolerance);
    INFO("字幕徽章的蓝色像素数 = " << lit);
    CHECK(lit > 0);
    //分组标题行不受影响
    CHECK(canvas.CountNearColor(rootRegion, kSubtitleBlue, kSubtitleBlueTolerance) == 0);

    tree.SetItemSubtitle(node, false);
    CHECK_FALSE(tree.IsItemSubtitle(node));
    canvas.Paint(tree);
    CHECK(canvas.CountNearColor(nodeRegion, kSubtitleBlue, kSubtitleBlueTolerance) == 0);

    //清空全部行装饰时一并清掉
    tree.SetItemSubtitle(node, true);
    tree.ClearAllRowDecorations();
    CHECK_FALSE(tree.IsItemSubtitle(node));
}

TEST_CASE("SUBB2 会话行：字幕徽章画在第二行未读胶囊左边，选中行上改画白色", "[subtitle][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    tree.SetRowHeight(kConvRowH);
    const int node = tree.AddRoot(_T("莫梅、江怡、黄涛波、秦明芳、向瑶娜、罗俊辉、赵卓轩、韩雪飞、韩娟丹"));
    //摘要很短，落在行左侧；字幕徽章在右侧徽章区，搜索时避开摘要文字边缘的彩色像素
    tree.SetItemSummary(node, _T("好"));
    tree.SetItemRightStacked(node, _T("09:12"), 3);
    tree.SetItemSubtitle(node, true);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    const RECT row = { 0, 0, kTreeW, kConvRowH };
    const int mid = kConvRowH / 2;
    canvas.Paint(tree);
    RECT markZone = row;
    markZone.left = kTreeW / 2;
    markZone.top  = mid;
    const PixelBox badge  = canvas.NearColorBoundingBox(markZone, kSubtitleBlue, kSubtitleBlueTolerance);
    const PixelBox unread = canvas.BoundingBox(row, kUnreadRed);
    REQUIRE_FALSE(badge.m_empty);
    REQUIRE_FALSE(unread.m_empty);
    INFO("徽章 x[" << badge.m_rc.left << "," << badge.m_rc.right << ") y[" << badge.m_rc.top << ","
         << badge.m_rc.bottom << ")，未读胶囊左边界 = " << unread.m_rc.left);
    CHECK(badge.m_rc.bottom - badge.m_rc.top <= kSubtitleBadgePx);
    CHECK(badge.m_rc.right <= unread.m_rc.left);

    //选中行：同一位置不再有蓝色，改为白色
    tree.SetCurSel(node, false);
    canvas.Paint(tree);
    const int blue  = canvas.CountNearColor(badge.m_rc, kSubtitleBlue, kSubtitleStrictTolerance);
    const int white = canvas.CountNearColor(badge.m_rc, kSelectedWhite, kSelectedWhiteTolerance);
    INFO("选中后徽章区域：蓝 " << blue << "，白 " << white);
    CHECK(blue == 0);
    CHECK(white > 0);
}

TEST_CASE("SUBB3 与收藏书签、通话中胶囊同时存在：书签、字幕徽章、胶囊依次排开", "[subtitle][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    const int root = tree.AddRoot(_T("我的群聊 · 1"));
    tree.SetExpanded(root, true);
    const int node = tree.AddChild(root, _T("产品设计组"));
    tree.SetItemFavorite(node, true);
    tree.SetItemSubtitle(node, true);
    tree.SetItemInCall(node, kInCallVoice);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);
    const PixelBox fav = canvas.BoundingBox(RowRect(1), kFavoriteRose);
    const PixelBox sub = canvas.NearColorBoundingBox(RightOfLabel(tree, node, RowRect(1)), kSubtitleBlue,
                                                     kSubtitleBlueTolerance);
    const PixelBox call = canvas.BoundingBox(RowRect(1), kInCallGreen);
    REQUIRE_FALSE(fav.m_empty);
    REQUIRE_FALSE(sub.m_empty);
    REQUIRE_FALSE(call.m_empty);
    INFO("书签右边界 = " << fav.m_rc.right << "，字幕徽章 " << sub.m_rc.left << "~" << sub.m_rc.right
         << "，胶囊左边界 = " << call.m_rc.left);
    CHECK(sub.m_rc.left >= fav.m_rc.right);
    CHECK(call.m_rc.left >= sub.m_rc.right);
}

TEST_CASE("SUBB4 字幕徽章的宽度计入右侧占位：开着时占位变宽，关掉后恢复", "[subtitle][rowmark][reserve]")
{
    MainPanelTreeView tree;
    const int node = tree.AddRoot(_T("张伟"));
    tree.SetItemRightStacked(node, kRrvTime, kRrvUnread);
    const CString before = tree.GetItemRightText(node);
    REQUIRE_FALSE(before.IsEmpty());

    tree.SetItemSubtitle(node, true);
    const CString with = tree.GetItemRightText(node);
    CHECK(IsAllSpaces(with));
    CHECK(MeasureWithDefaultFont(with) > MeasureWithDefaultFont(before));

    tree.SetItemSubtitle(node, false);
    CHECK(tree.GetItemRightText(node) == before);
}

TEST_CASE("SUBB5 选中行上字幕徽章改画白色，不画蓝色", "[subtitle][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    const int root = tree.AddRoot(_T("设计部"));
    tree.SetExpanded(root, true);
    const int node = tree.AddChild(root, _T("莫梅"));
    tree.SetItemSubtitle(node, true);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    //先在未选中时找到徽章所在的位置
    canvas.Paint(tree);
    const PixelBox badge = canvas.NearColorBoundingBox(RightOfLabel(tree, node, RowRect(1)), kSubtitleBlue,
                                                       kSubtitleBlueTolerance);
    REQUIRE_FALSE(badge.m_empty);

    tree.SetCurSel(node, false);
    canvas.Paint(tree);
    const int blue = canvas.CountNearColor(badge.m_rc, kSubtitleBlue, kSubtitleStrictTolerance);
    const int white = canvas.CountNearColor(badge.m_rc, kSelectedWhite, kSelectedWhiteTolerance);
    INFO("选中后徽章区域：蓝 " << blue << "，白 " << white);
    CHECK(blue == 0);
    CHECK(white > 0);
}

// =====================================================================
// MU-C3：会话被手动标为未读时的小红点（docs/会话标为未读方案.md）
// =====================================================================

namespace {

// 不提醒的模式下胶囊与小红点的灰色，与 MainPanelTreeView.cpp 的 kBadgeMutedGray 同值。
const COLORREF kMarkDotGray = RGB(0x9A, 0xA0, 0xA6);
// 小红点实心部分的最大宽度：直径 10，抗锯齿的边缘不是纯色，实心部分只会更窄。
const int kUnreadDotMaxSolidPx = 10;
// 一位数未读胶囊实心部分的最小宽度：直径 16，去掉两侧抗锯齿边缘后不少于 13。
const int kUnreadCapsuleMinSolidPx = 13;
// 会话行右上角的时间文字，只用来让行右侧有内容。
const TCHAR* const kUnreadTimeText = _T("14:32");
// 有未读数时的未读数。
const int kUnreadCount = 3;

} // anonymous namespace

TEST_CASE("MU-C3a 标为未读且未读数为 0 时画小红点，有未读数时只画数字胶囊", "[markunread][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    const int node = tree.AddRoot(_T("莫梅"));
    tree.SetItemRightStacked(node, kUnreadTimeText, 0);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);
    CHECK(canvas.CountColor(RowRect(0), kUnreadRed) == 0);

    //标为未读：出现一个不带数字的小圆点
    tree.SetItemUnreadDot(node, true);
    CHECK(tree.IsItemUnreadDot(node));
    canvas.Paint(tree);
    const PixelBox dot = canvas.BoundingBox(RowRect(0), kUnreadRed);
    REQUIRE_FALSE(dot.m_empty);
    const int dotW = dot.m_rc.right - dot.m_rc.left;
    INFO("小红点实心宽度 = " << dotW);
    CHECK(dotW <= kUnreadDotMaxSolidPx);
    //没有数字：圆点中心 3×3 全是红色（数字胶囊的中心是白色的数字）
    const int cx = (dot.m_rc.left + dot.m_rc.right) / 2;
    const int cy = (dot.m_rc.top + dot.m_rc.bottom) / 2;
    const RECT core = { cx - 1, cy - 1, cx + 2, cy + 2 };
    const int kCorePixels = 9;
    CHECK(canvas.CountColor(core, kUnreadRed) == kCorePixels);

    //同时有未读数：只画数字胶囊（比小红点宽，里面有白色数字）
    tree.SetItemRightStacked(node, kUnreadTimeText, kUnreadCount);
    canvas.Paint(tree);
    const PixelBox capsule = canvas.BoundingBox(RowRect(0), kUnreadRed);
    REQUIRE_FALSE(capsule.m_empty);
    CHECK(capsule.m_rc.right - capsule.m_rc.left >= kUnreadCapsuleMinSolidPx);

    //清除标记、未读数也清零：什么都不画
    tree.SetItemUnreadDot(node, false);
    tree.SetItemRightStacked(node, kUnreadTimeText, 0);
    canvas.Paint(tree);
    CHECK(canvas.CountColor(RowRect(0), kUnreadRed) == 0);
}

TEST_CASE("MU-C3b 不提醒的模式下小红点变灰", "[markunread][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    const int node = tree.AddRoot(_T("项目群"));
    tree.SetItemRightStacked(node, kUnreadTimeText, 0);
    tree.SetItemConversationFlags(node, false, notify_mode_silent);
    tree.SetItemUnreadDot(node, true);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);
    CHECK(canvas.CountColor(RowRect(0), kUnreadRed) == 0);
    CHECK(canvas.CountColor(RowRect(0), kMarkDotGray) > 0);
}

TEST_CASE("MU-C3c 行装饰判定：标记且没有未读数才画，屏蔽的会话不画", "[markunread][rowmark]")
{
    CHECK(balloonmain::ShowsUnreadDot(0, true, notify_mode_allow));
    CHECK_FALSE(balloonmain::ShowsUnreadDot(kUnreadCount, true, notify_mode_allow));
    CHECK_FALSE(balloonmain::ShowsUnreadDot(0, false, notify_mode_allow));
    //灰色的几种模式照样画（颜色由行的提醒模式决定）
    CHECK(balloonmain::ShowsUnreadDot(0, true, notify_mode_silent));
    CHECK(balloonmain::ShowsUnreadDot(0, true, notify_mode_mention_follow));
    //屏蔽的会话不画未读
    CHECK_FALSE(balloonmain::ShowsUnreadDot(0, true, notify_mode_block));
}

// =====================================================================
// CRL 组：会话行的排布（docs/消息提醒模式与消息盒子方案.md 第 13 节）
// =====================================================================
//
// 2026-10-07 起：在线状态改画在头像右下角，单聊行右端不再有状态徽标，所有行的时间与未读数贴同一条右边线
// （此前单聊行要给状态徽标让位，时间比群行靠左 18 像素）；星标、收藏、字幕、通话中、仅通知都画在第二行右侧，
// 名字只按时间截断，摘要按剩余宽度截断，宽度不够时按「收藏 → 字幕 → 仅通知 → 星标」省略徽章。

namespace {

// 会话行头像边长（逻辑像素），与主面板会话 Tab 相同。
const int kConvAvatarPx = 36;

// 判定"非白像素"的亮度下限：三个通道都不低于它的算白底。
const int kNearWhite = 0xF8;

// 充当头像的纯色：绿色，与白色描边、选中底色、角标的橙与红都拉得开。
const COLORREF kAvatarGreen = RGB(0x00, 0xA0, 0x00);

// 树控件右内边距（逻辑像素），与 DuiTreeView 的私有常量 kRightPad 同值：时间与未读胶囊的右边线。
const int kRowRightPad = 8;

// 「仅通知」标签的底色，与 MainPanelTreeView.cpp 的 kNotifyOnlyFill 同值。
const COLORREF kNotifyOnlyFillGray = RGB(0xEC, 0xEC, 0xEC);

// 不提醒的模式下未读胶囊的灰色，与 MainPanelTreeView.cpp 的 kBadgeMutedGray 同值。
const COLORREF kMutedCapsuleGray = RGB(0x9A, 0xA0, 0xA6);

// 各用例共用的时间文字（同一段文字在各行宽度相同，右边界可以直接比较）。
const TCHAR* const kCrlTime = _T("09:12");

// 超长的名字与摘要。
const TCHAR* const kCrlLongName = _T("莫梅、江怡、黄涛波、秦明芳、向瑶娜、罗俊辉、赵卓轩、韩雪飞、韩娟丹");
const TCHAR* const kCrlLongSummary = _T("好的，我下午三点之前把会议纪要整理好发到群里，大家记得看一下有没有遗漏");

// CRL3 / CRL5 的未读数。
const int kCrlUnread = 3;
const int kCrlUnreadBig = 99;

// CRL5 扫描的树宽度范围与步长（像素）：从主面板常见宽度一直收窄到可省略的徽章全部省略。
const int kCrlWidestTree = 320;
const int kCrlNarrowestTree = 140;
const int kCrlWidthStep = 10;

// 第二行搜索字幕徽章、「仅通知」标签时避开摘要文字的左边界（像素）：摘要只有一个字，落在这条线左边。
const int kCrlMarkSearchLeft = 40;

// CRL5 判定"画出了某个标记"的最少像素数：12 像素的图标、16 像素高的胶囊内部远不止这么多，
// 其它图形抗锯齿边缘恰好同色的零星像素远少于这些。
const int kCrlMinIconPixels = 8;
const int kCrlMinCapsulePixels = 40;
// 字幕徽章是细线条图标（线宽约 1.3 像素），抗锯齿后接近目标蓝色的像素很少，有一个就算画出；搜索区域已避开
// 摘要文字（ClearType 彩边会落进容差），第二行里其余的图形都不是蓝色。
const int kCrlMinLinePixels = 1;

// 把一棵树设成会话 Tab 的样子：行高 48、头像 36 的圆形。
void SetupConvTree(MainPanelTreeView& tree)
{
    tree.SetRowHeight(kConvRowH);
    tree.SetIconSize(kConvAvatarPx);
    tree.SetIconCornerRadius(kConvAvatarPx / 2);
    tree.SetIconUsesAlpha(true);
}

// 会话树第 row 行的整行矩形。
RECT ConvRowRect(int row, int treeW)
{
    RECT rc = { 0, row * kConvRowH, treeW, (row + 1) * kConvRowH };
    return rc;
}

// 会话树第 row 行上半行右侧、时间所在的区域。下沿让开第二行胶囊探进上半行的那 2 像素。
RECT ConvTimeBand(int row)
{
    RECT rc = ConvRowRect(row, kTreeW);
    rc.left   = rc.right - kRightTextBandW;
    rc.bottom = rc.top + kConvRowH / 2 - kSecondLineCapsuleRise;
    return rc;
}

// 会话树第 row 行的第二行（下半行）中 x 不小于 left 的部分。
RECT ConvSecondLine(int row, int left, int treeW)
{
    RECT rc = ConvRowRect(row, treeW);
    rc.left = left;
    rc.top  = rc.top + kConvRowH / 2;
    return rc;
}

// 会话树第 row 行的第一行（上半行），下沿让开第二行胶囊探进来的部分。
RECT ConvFirstLine(int row, int treeW)
{
    RECT rc = ConvRowRect(row, treeW);
    rc.bottom = rc.top + kConvRowH / 2 - kSecondLineCapsuleRise;
    return rc;
}

// 给一行打上全部五种标记、接收不提醒模式与未读数。
void MarkAll(MainPanelTreeView& tree, int node, int unread)
{
    tree.SetItemRightStacked(node, kCrlTime, unread);
    tree.SetItemConversationFlags(node, false, notify_mode_silent);
    tree.SetItemStarred(node, true);
    tree.SetItemFavorite(node, true);
    tree.SetItemSubtitle(node, true);
    tree.SetItemInCall(node, kInCallVoice);
    tree.SetItemNotifyOnly(node, true);
}

} // anonymous namespace

TEST_CASE("CRL1 会话列表所有行的时间右对齐在同一列，未读胶囊右边界也相同", "[convrow][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    HBITMAP avatar = MakeSolidBitmap(kConvAvatarPx, kAvatarGreen);
    REQUIRE(avatar != NULL);

    MainPanelTreeView tree;
    SetupConvTree(tree);
    //单聊行：离开状态、置顶、带未读（真实单聊行的形态）
    const int single = tree.AddRoot(_T("莫梅"));
    tree.SetItemIcon(single, avatar);
    tree.SetItemSummary(single, _T("晚上一起吃饭吗"));
    tree.SetItemRightStacked(single, kCrlTime, kCrlUnread);
    tree.SetItemPresence(single, presence_away);
    //群行：仅通知、接收不提醒，没有未读
    const int group = tree.AddRoot(_T("产品设计组"));
    tree.SetItemIcon(group, avatar);
    tree.SetItemSummary(group, _T("江怡：设计稿已经更新"));
    tree.SetItemRightStacked(group, kCrlTime, 0);
    tree.SetItemConversationFlags(group, false, notify_mode_silent);
    tree.SetItemNotifyOnly(group, true);
    //消息盒子行：灰色未读胶囊
    const int box = tree.AddRoot(_T("消息盒子"));
    tree.SetItemIcon(box, avatar);
    tree.SetItemSummary(box, _T("研发周会：明天的会改到下午"));
    tree.SetItemRightStacked(box, kCrlTime, kCrlUnread);
    tree.SetItemQuietUnread(box, true);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);

    PixelBox timeBox[3];
    for (int r = 0; r < 3; ++r)
    {
        timeBox[r] = canvas.GrayTextBoundingBox(ConvTimeBand(r), kTextLumaMax, kGraySpreadMax);
        INFO("row " << r);
        REQUIRE_FALSE(timeBox[r].m_empty);
    }
    INFO("时间右边界：单聊 " << timeBox[0].m_rc.right << "，群 " << timeBox[1].m_rc.right << "，消息盒子 "
         << timeBox[2].m_rc.right);
    CHECK(timeBox[0].m_rc.right == timeBox[1].m_rc.right);
    CHECK(timeBox[1].m_rc.right == timeBox[2].m_rc.right);
    //贴着行右端的内边距（文字墨迹可能比排版边界少一两个像素）
    CHECK(timeBox[0].m_rc.right <= kTreeW - kRowRightPad);
    CHECK(timeBox[0].m_rc.right >= kTreeW - kRowRightPad - kSecondLineCapsuleRise);

    //单聊行的红色胶囊与消息盒子行的灰色胶囊右边界相同
    const PixelBox red  = canvas.BoundingBox(ConvRowRect(0, kTreeW), kUnreadRed);
    const PixelBox gray = canvas.BoundingBox(ConvRowRect(2, kTreeW), kMutedCapsuleGray);
    REQUIRE_FALSE(red.m_empty);
    REQUIRE_FALSE(gray.m_empty);
    CHECK(red.m_rc.right == gray.m_rc.right);

    //置顶后左侧竖条照常，时间位置不变（置顶的覆色会把文字边缘的浅色像素压暗，右边界允许差 1 像素）
    tree.SetItemConversationFlags(single, true, notify_mode_allow);
    canvas.Paint(tree);
    RECT accentBand = ConvRowRect(0, kTreeW);
    accentBand.right = kLeftAccentBandW;
    CHECK(canvas.CountColor(accentBand, kPinnedAccent) > 0);
    const PixelBox pinnedTime = canvas.GrayTextBoundingBox(ConvTimeBand(0), kTextLumaMax, kGraySpreadMax);
    REQUIRE_FALSE(pinnedTime.m_empty);
    CHECK(abs(pinnedTime.m_rc.right - timeBox[1].m_rc.right) <= 1);

    ::DeleteObject(avatar);
}

TEST_CASE("CRL2 会话行的五种标记都画在第二行，依次排在提醒模式图标与未读胶囊左边", "[convrow][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    SetupConvTree(tree);
    const int node = tree.AddRoot(_T("莫梅"));
    tree.SetItemSummary(node, _T("好"));
    MarkAll(tree, node, kCrlUnread);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);

    const RECT row = ConvRowRect(0, kTreeW);
    const int mid = kConvRowH / 2;
    const PixelBox star   = canvas.BoundingBox(row, kStarGold);
    const PixelBox fav    = canvas.BoundingBox(row, kFavoriteRose);
    const PixelBox sub    = canvas.NearColorBoundingBox(ConvSecondLine(0, kCrlMarkSearchLeft, kTreeW), kSubtitleBlue,
                                                        kSubtitleBlueTolerance);
    const PixelBox call   = canvas.BoundingBox(row, kInCallGreen);
    const PixelBox notice = canvas.BoundingBox(ConvSecondLine(0, kCrlMarkSearchLeft, kTreeW), kNotifyOnlyFillGray);
    const PixelBox unread = canvas.BoundingBox(row, kMutedCapsuleGray);
    REQUIRE_FALSE(star.m_empty);
    REQUIRE_FALSE(fav.m_empty);
    REQUIRE_FALSE(sub.m_empty);
    REQUIRE_FALSE(call.m_empty);
    REQUIRE_FALSE(notice.m_empty);
    REQUIRE_FALSE(unread.m_empty);
    INFO("星 " << star.m_rc.left << "~" << star.m_rc.right << "，书签 " << fav.m_rc.left << "~" << fav.m_rc.right
         << "，字幕 " << sub.m_rc.left << "~" << sub.m_rc.right << "，通话中 " << call.m_rc.left << "~"
         << call.m_rc.right << "，仅通知 " << notice.m_rc.left << "~" << notice.m_rc.right << "，未读 "
         << unread.m_rc.left << "~" << unread.m_rc.right);

    //都在第二行：图标的上沿不高于行中线，胶囊的上沿最多高出 2 像素
    CHECK(star.m_rc.top >= mid);
    CHECK(fav.m_rc.top >= mid);
    CHECK(sub.m_rc.top >= mid);
    CHECK(call.m_rc.top >= mid - kSecondLineCapsuleRise);
    CHECK(notice.m_rc.top >= mid - kSecondLineCapsuleRise);
    //第一行（名字所在）没有任何标记的颜色
    const RECT firstLine = ConvFirstLine(0, kTreeW);
    CHECK(canvas.CountColor(firstLine, kStarGold) == 0);
    CHECK(canvas.CountColor(firstLine, kFavoriteRose) == 0);
    CHECK(canvas.CountColor(firstLine, kInCallGreen) == 0);

    //从左往右：星、书签、字幕、通话中、仅通知，再往右是（灰色）未读胶囊
    CHECK(star.m_rc.right <= fav.m_rc.left);
    CHECK(fav.m_rc.right <= sub.m_rc.left);
    CHECK(sub.m_rc.right <= call.m_rc.left);
    CHECK(call.m_rc.right <= notice.m_rc.left);
    CHECK(notice.m_rc.right <= unread.m_rc.left);
    CHECK(unread.m_rc.right <= kTreeW - kRowRightPad);
}

TEST_CASE("CRL3 会话行的名字只按时间截断：打不打标记，名字画到的位置都一样", "[convrow][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    SetupConvTree(tree);
    const int plain = tree.AddRoot(kCrlLongName);
    tree.SetItemSummary(plain, _T("好"));
    tree.SetItemRightStacked(plain, kCrlTime, 0);
    const int marked = tree.AddRoot(kCrlLongName);
    tree.SetItemSummary(marked, _T("好"));
    MarkAll(tree, marked, kCrlUnreadBig);
    //第三行名字很短、时间相同：在这一行量时间的位置（长名字的行里，时间那一条也有名字的深灰像素）
    const int shortName = tree.AddRoot(_T("莫梅"));
    tree.SetItemSummary(shortName, _T("好"));
    tree.SetItemRightStacked(shortName, kCrlTime, 0);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);

    const PixelBox namePlain  = canvas.DarkBoundingBox(ConvFirstLine(0, kTreeW), kNameLumaMax);
    const PixelBox nameMarked = canvas.DarkBoundingBox(ConvFirstLine(1, kTreeW), kNameLumaMax);
    const PixelBox time       = canvas.GrayTextBoundingBox(ConvTimeBand(2), kTextLumaMax, kGraySpreadMax);
    REQUIRE_FALSE(namePlain.m_empty);
    REQUIRE_FALSE(nameMarked.m_empty);
    REQUIRE_FALSE(time.m_empty);
    INFO("名字右边界：无标记 " << namePlain.m_rc.right << "，有标记 " << nameMarked.m_rc.right << "，时间左边界 "
         << time.m_rc.left);
    CHECK(nameMarked.m_rc.right == namePlain.m_rc.right);
    CHECK(nameMarked.m_rc.right < time.m_rc.left);
}

TEST_CASE("CRL4 会话行的摘要按剩余宽度截断并补省略号，不压到徽章", "[convrow][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    SetupConvTree(tree);
    const int node = tree.AddRoot(_T("莫梅"));
    tree.SetItemSummary(node, kCrlLongSummary);
    tree.SetItemRightStacked(node, kCrlTime, kCrlUnread);
    tree.SetItemStarred(node, true);
    tree.SetItemFavorite(node, true);
    tree.SetItemInCall(node, kInCallVideo);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);

    //原文保留，交给基类画的是截断后的文字
    CHECK(tree.IsItemConversationLayout(node));
    CHECK(tree.GetItemSummary(node) == kCrlLongSummary);
    const CString drawn = tree.GetItemSubLabel(node);
    INFO("画出的摘要：" << CT2A(drawn, CP_UTF8).m_psz);
    CHECK(drawn != kCrlLongSummary);
    CHECK(drawn.Right(3) == _T("..."));

    //摘要文字在星标左边，不重叠
    const PixelBox star = canvas.BoundingBox(ConvRowRect(0, kTreeW), kStarGold);
    const PixelBox summary = canvas.GrayTextBoundingBox(ConvSecondLine(0, 0, kTreeW), kTextLumaMax, kGraySpreadMax);
    REQUIRE_FALSE(star.m_empty);
    REQUIRE_FALSE(summary.m_empty);
    INFO("摘要右边界 = " << summary.m_rc.right << "，星标左边界 = " << star.m_rc.left);
    CHECK(summary.m_rc.right < star.m_rc.left);

    //清空行装饰后不再是会话行版式
    tree.ClearAllRowDecorations();
    CHECK_FALSE(tree.IsItemConversationLayout(node));
    CHECK(tree.GetItemSummary(node).IsEmpty());
}

TEST_CASE("CRL5 树逐步变窄：按收藏、字幕、仅通知、星标的顺序省略，通话中与未读数始终保留", "[convrow][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelTreeView tree;
    SetupConvTree(tree);
    const int node = tree.AddRoot(_T("莫梅"));
    tree.SetItemSummary(node, _T("好"));
    MarkAll(tree, node, kCrlUnread);

    bool sawAll = false;
    bool sawNone = false;
    for (int w = kCrlWidestTree; w >= kCrlNarrowestTree; w -= kCrlWidthStep)
    {
        OffscreenCanvas canvas(w, kConvRowH);
        REQUIRE(canvas.Ok());
        canvas.Paint(tree);
        const RECT row = ConvRowRect(0, w);
        const RECT markLine = ConvSecondLine(0, kCrlMarkSearchLeft, w);
        const bool star   = canvas.CountColor(row, kStarGold) >= kCrlMinIconPixels;
        const bool fav    = canvas.CountColor(row, kFavoriteRose) >= kCrlMinIconPixels;
        const bool sub    = canvas.CountNearColor(markLine, kSubtitleBlue, kSubtitleBlueTolerance) >= kCrlMinLinePixels;
        const bool notice = canvas.CountColor(markLine, kNotifyOnlyFillGray) >= kCrlMinCapsulePixels;
        const bool call   = canvas.CountColor(row, kInCallGreen) >= kCrlMinCapsulePixels;
        const bool unread = canvas.CountColor(row, kMutedCapsuleGray) >= kCrlMinIconPixels;
        INFO("width = " << w << "：星 " << star << "，书签 " << fav << "，字幕 " << sub << "，仅通知 " << notice
             << "，通话中 " << call << "，未读 " << unread);
        CHECK(call);
        CHECK(unread);
        //先省略的在场，后省略的一定也在场
        if (fav)
        {
            CHECK(sub);
        }
        if (sub)
        {
            CHECK(notice);
        }
        if (notice)
        {
            CHECK(star);
        }
        if (star && fav && sub && notice)
        {
            sawAll = true;
        }
        if (!star && !fav && !sub && !notice)
        {
            sawNone = true;
        }
    }
    CHECK(sawAll);
    CHECK(sawNone);
}

// =====================================================================
// PRC 组：头像右下角的在线状态角标（2026-10-07 取代行右端的状态徽标）
// =====================================================================

namespace {

// 角标的两种颜色，与 MainPanelStatusMenu.cpp 的 kDotAway / kDotBusy 同值；在线的绿 kDotOnline 用来断言"不画"。
const COLORREF kAwayOrange  = RGB(0xF5, 0x9E, 0x0B);
const COLORREF kBusyRed     = RGB(0xDC, 0x26, 0x26);
const COLORREF kOnlineGreen = RGB(0x16, 0xA3, 0x4A);
// 找角标颜色的容差：圆底内部就是这个颜色，抗锯齿边缘的像素不算。
const int kCornerColorTolerance = 24;

// 描边颜色：普通行白色，选中行为选中底色品牌蓝（与 MainPanelTreeView.cpp 同值）。
const COLORREF kRingWhite = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kRingSelected = RGB(0x2D, 0x6C, 0xDF);
// 判定描边颜色的容差：描边像素可能与头像或圆底有少量混色。
const int kRingTolerance = 60;

// 与 MainPanelTreeView.cpp 相同的几何：基类头像与名字之间的间距、角标圆心离头像右下角的距离、
// 描边环上取样点离圆心的距离（圆底半径 5，描边外沿 7，取 6）。
const int kAvatarTextGap = 6;
const int kCornerInset = 5;
const int kRingSampleDist = 6;
// 角标圆心位置允许的偏差（像素）：圆底内部像素的范围中心与理论圆心之间的误差。
const int kCornerCenterSlack = 2;

// 会话树第 row 行头像的右边界与下边界：右边界 = 名字起点 - 间距，头像在 48 高的行里垂直居中。
void ConvAvatarEdges(const MainPanelTreeView& tree, int node, int row, int& outRight, int& outBottom)
{
    RECT label;
    outRight = 0;
    if (tree.GetItemLabelTextRect(node, label))
    {
        outRight = label.left - kAvatarTextGap;
    }
    outBottom = row * kConvRowH + (kConvRowH - kConvAvatarPx) / 2 + kConvAvatarPx;
}

// 在 (x, y) 这一个像素上，颜色是否与 want 相近。
bool PixelNear(const OffscreenCanvas& canvas, int x, int y, COLORREF want, int tolerance)
{
    RECT one = { x, y, x + 1, y + 1 };
    return canvas.CountNearColor(one, want, tolerance) == 1;
}

// 描边环上四个取样点里与 want 相近的个数。取样点在圆心左侧与上方（压在头像上，与描边颜色、头像颜色都不同）。
int RingSamplesNear(const OffscreenCanvas& canvas, int cx, int cy, COLORREF want)
{
    int hits = 0;
    if (PixelNear(canvas, cx - kRingSampleDist, cy, want, kRingTolerance))
        ++hits;
    if (PixelNear(canvas, cx - kRingSampleDist, cy - 1, want, kRingTolerance))
        ++hits;
    if (PixelNear(canvas, cx, cy - kRingSampleDist, want, kRingTolerance))
        ++hits;
    if (PixelNear(canvas, cx - 1, cy - kRingSampleDist, want, kRingTolerance))
        ++hits;
    return hits;
}

// 一棵五行的会话树：离开、忙碌、在线、离线（头像灰度）、没有登记在线状态。返回各行节点 id。
std::vector<int> BuildPresenceTree(MainPanelTreeView& tree, HBITMAP avatar)
{
    SetupConvTree(tree);
    const TCHAR* names[] = { _T("莫梅"), _T("江怡"), _T("黄涛波"), _T("秦明芳"), _T("向瑶娜") };
    const int presences[] = { presence_away, presence_busy, presence_online, presence_offline,
                              MainPanelTreeView::kNoPresence };
    std::vector<int> ids;
    for (int i = 0; i < 5; ++i)
    {
        const int id = tree.AddRoot(names[i]);
        tree.SetItemIcon(id, avatar);
        tree.SetItemSummary(id, _T("收到"));
        tree.SetItemRightStacked(id, kCrlTime, 0);
        tree.SetItemPresence(id, presences[i]);
        tree.SetItemIconGrayed(id, presences[i] == presence_offline);
        ids.push_back(id);
    }
    return ids;
}

} // anonymous namespace

TEST_CASE("PRC1 离开、忙碌的角标画在头像右下角，在线与离线不画角标", "[presencecorner][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    HBITMAP avatar = MakeSolidBitmap(kConvAvatarPx, kAvatarGreen);
    REQUIRE(avatar != NULL);
    MainPanelTreeView tree;
    const std::vector<int> ids = BuildPresenceTree(tree, avatar);
    CHECK(tree.GetItemPresence(ids[0]) == presence_away);
    CHECK(tree.GetItemPresence(ids[4]) == MainPanelTreeView::kNoPresence);

    const int rows = 5;
    OffscreenCanvas canvas(kTreeW, rows * kConvRowH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);

    const COLORREF cornerColor[] = { kAwayOrange, kBusyRed };
    for (int r = 0; r < 2; ++r)
    {
        int iconRight = 0;
        int iconBottom = 0;
        ConvAvatarEdges(tree, ids[r], r, iconRight, iconBottom);
        REQUIRE(iconRight > 0);
        const PixelBox disc = canvas.NearColorBoundingBox(ConvRowRect(r, kTreeW), cornerColor[r],
                                                          kCornerColorTolerance);
        REQUIRE_FALSE(disc.m_empty);
        const int cx = (disc.m_rc.left + disc.m_rc.right) / 2;
        const int cy = (disc.m_rc.top + disc.m_rc.bottom) / 2;
        INFO("row " << r << "：圆底 x[" << disc.m_rc.left << "," << disc.m_rc.right << ") y[" << disc.m_rc.top << ","
             << disc.m_rc.bottom << ")，头像右边界 " << iconRight << "，下边界 " << iconBottom);
        CHECK(abs(cx - (iconRight - kCornerInset)) <= kCornerCenterSlack);
        CHECK(abs(cy - (iconBottom - kCornerInset)) <= kCornerCenterSlack);
        //不压到名字（名字起点 = 头像右边界 + 间距）
        CHECK(disc.m_rc.right <= iconRight + kAvatarTextGap);
    }

    //在线、离线、没有登记的行：没有任何角标颜色
    for (int r = 2; r < rows; ++r)
    {
        INFO("row " << r);
        const RECT row = ConvRowRect(r, kTreeW);
        CHECK(canvas.CountNearColor(row, kAwayOrange, kCornerColorTolerance) == 0);
        CHECK(canvas.CountNearColor(row, kBusyRed, kCornerColorTolerance) == 0);
        CHECK(canvas.CountNearColor(row, kOnlineGreen, kCornerColorTolerance) == 0);
    }
    //离线行的头像是灰的，在线行的头像是彩色的
    CHECK(canvas.CountColor(ConvRowRect(2, kTreeW), kAvatarGreen) > 0);
    CHECK(canvas.CountColor(ConvRowRect(3, kTreeW), kAvatarGreen) == 0);

    //角标不占文字宽度：有角标与没有登记的行，名字起点相同
    RECT labelAway;
    RECT labelNone;
    REQUIRE(tree.GetItemLabelTextRect(ids[0], labelAway));
    REQUIRE(tree.GetItemLabelTextRect(ids[4], labelNone));
    CHECK(labelAway.left == labelNone.left);

    ::DeleteObject(avatar);
}

TEST_CASE("PRC2 角标的描边：普通行白色，选中行改用选中底色", "[presencecorner][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    HBITMAP avatar = MakeSolidBitmap(kConvAvatarPx, kAvatarGreen);
    REQUIRE(avatar != NULL);
    MainPanelTreeView tree;
    const std::vector<int> ids = BuildPresenceTree(tree, avatar);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);
    int iconRight = 0;
    int iconBottom = 0;
    ConvAvatarEdges(tree, ids[0], 0, iconRight, iconBottom);
    REQUIRE(iconRight > 0);
    const int cx = iconRight - kCornerInset;
    const int cy = iconBottom - kCornerInset;
    const int kMinHits = 3;
    const int whiteHits = RingSamplesNear(canvas, cx, cy, kRingWhite);
    INFO("普通行描边取样：白 " << whiteHits);
    CHECK(whiteHits >= kMinHits);

    tree.SetCurSel(ids[0], false);
    canvas.Paint(tree);
    const int blueHits = RingSamplesNear(canvas, cx, cy, kRingSelected);
    const int whiteAfter = RingSamplesNear(canvas, cx, cy, kRingWhite);
    INFO("选中行描边取样：蓝 " << blueHits << "，白 " << whiteAfter);
    CHECK(blueHits >= kMinHits);
    CHECK(whiteAfter == 0);

    ::DeleteObject(avatar);
}

TEST_CASE("PRC3 通讯录行：在线状态画成头像角标，行右端什么都不画", "[presencecorner][rowmark]")
{
    NmkScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    const int kContactAvatarPx = 32;
    HBITMAP avatar = MakeSolidBitmap(kContactAvatarPx, kAvatarGreen);
    REQUIRE(avatar != NULL);

    MainPanelTreeView tree;
    tree.SetRowHeight(kRowH);
    tree.SetIconSize(kContactAvatarPx);
    tree.SetIconCornerRadius(kContactAvatarPx / 2);
    tree.SetIconUsesAlpha(true);
    const int root = tree.AddRoot(_T("设计部"));
    tree.SetExpanded(root, true);
    const int away = tree.AddChild(root, _T("莫梅"));
    const int online = tree.AddChild(root, _T("江怡"));
    tree.SetItemSubLabel(away, _T("资深设计师"));
    tree.SetItemSubLabel(online, _T("设计组长"));
    tree.SetItemIcon(away, avatar);
    tree.SetItemIcon(online, avatar);
    tree.SetItemPresence(away, presence_away);
    tree.SetItemPresence(online, presence_online);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    canvas.Paint(tree);

    //离开的人头像上有橙色角标，在线的人没有
    CHECK(canvas.CountNearColor(RowRect(1), kAwayOrange, kCornerColorTolerance) > 0);
    CHECK(canvas.CountNearColor(RowRect(2), kAwayOrange, kCornerColorTolerance) == 0);
    CHECK(canvas.CountNearColor(RowRect(2), kOnlineGreen, kCornerColorTolerance) == 0);

    //行右端一条什么都没有画（2026-10-07 之前这里是在线状态徽标）
    const int kRightStripW = 24;
    for (int r = 1; r <= 2; ++r)
    {
        RECT strip = RowRect(r);
        strip.left = strip.right - kRightStripW;
        INFO("row " << r);
        CHECK(canvas.NonWhiteBoundingBox(strip, kNearWhite).m_empty);
    }
    CHECK(tree.GetItemStatusIcon(away) == NULL);
    CHECK(tree.GetItemStatusIcon(online) == NULL);

    ::DeleteObject(avatar);
}

namespace {

// PRC4 要检查的主面板源文件（相对 flamingoclient\Source）：给树挂在线状态标记的代码都在这几个文件里。
const char* const kPresenceSourceFiles[] =
{
    "UI\\BalloonMain\\BalloonMainPanel.cpp",
    "UI\\BalloonMain\\PresenceTreeMarks.cpp",
};

// 从测试程序所在目录往上找客户端的 Source 目录时，最多回溯的层数。
const int kMaxSourceWalkDepth = 12;

// 找到 flamingoclient\Source 目录（以其中存在 Flamingo.vcxproj 为准），返回带结尾反斜杠的路径；找不到返回空串。
std::string LocateClientSourceDir()
{
    char module[MAX_PATH] = { 0 };
    if (::GetModuleFileNameA(NULL, module, MAX_PATH) == 0)
    {
        return std::string();
    }
    std::string dir(module);
    for (int depth = 0; depth < kMaxSourceWalkDepth; ++depth)
    {
        const size_t slash = dir.find_last_of("\\/");
        if (slash == std::string::npos)
        {
            break;
        }
        dir = dir.substr(0, slash);
        const std::string candidate = dir + "\\Source\\";
        if (::GetFileAttributesA((candidate + "Flamingo.vcxproj").c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            return candidate;
        }
    }
    return std::string();
}

// 读出整个文件的内容；读不到返回 false。
bool ReadWholeFile(const std::string& path, std::string& out)
{
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == NULL)
    {
        return false;
    }
    out.clear();
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
    {
        out.append(buf, n);
    }
    fclose(fp);
    return true;
}

} // anonymous namespace

TEST_CASE("PRC4 主面板源码不再给树挂行末状态徽标", "[presencecorner][rowmark]")
{
    const std::string sourceDir = LocateClientSourceDir();
    REQUIRE_FALSE(sourceDir.empty());
    const size_t fileCount = sizeof(kPresenceSourceFiles) / sizeof(kPresenceSourceFiles[0]);
    for (size_t i = 0; i < fileCount; ++i)
    {
        std::string text;
        INFO("file " << kPresenceSourceFiles[i]);
        REQUIRE(ReadWholeFile(sourceDir + kPresenceSourceFiles[i], text));
        //调用写法只有这两种（经指针、经对象）；注释里提到这个接口名不受影响
        CHECK(text.find("->SetItemStatusIcon(") == std::string::npos);
        CHECK(text.find(".SetItemStatusIcon(") == std::string::npos);
    }
}
