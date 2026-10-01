/**
 *  MainPanelRowMarkTests.cpp
 *
 *  主面板列表行标记的渲染测试：
 *    GPR 组 —— 「群」Tab 里置顶的群画出与会话 Tab 相同的置顶样式（整行淡灰底 + 左侧
 *              橙色竖条），且不影响第一行右侧的成员数。
 *    FAV 组 —— 收藏的联系人 / 群在名字后面画出收藏徽章（书签图标）。
 *    RRV 组 —— 右侧占位空格"个数不变就不写回基类"之后，占位内容仍与一次设全的结果一致。
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

#include <vector>

#include <gdiplus.h>   // 效果图用例把画布存成 PNG

#include "../UI/BalloonMain/MainPanelTreeView.h"
#include "../UI/BalloonMain/PresenceBadge.h"   // CRL 组用真实的在线状态徽标位图
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
    tree.SetItemConversationFlags(groupA, true, false);
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
    tree.SetItemConversationFlags(groupA, false, false);
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

// 行右端状态图标的颜色：造一张纯色位图充当，只关心它占多宽、画在哪儿。
const COLORREF kStatusGreen = RGB(0x12, 0xB7, 0x6A);

// 状态图标边长（逻辑像素），与主面板在线状态徽标的尺寸相当。
const int kStatusIconSize = 12;

// 会话行行高（逻辑像素），与主面板会话 Tab 相同。
const int kConvRowH = 48;

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

TEST_CASE("FAV3 会话行名字超长时名字被省略，徽章照样画出且不压右侧内容", "[favorite][rowmark]")
{
    HBITMAP status = MakeSolidBitmap(kStatusIconSize, kStatusGreen);
    REQUIRE(status != NULL);

    MainPanelTreeView tree;
    tree.SetRowHeight(kConvRowH);
    const int node = tree.AddRoot(_T("莫梅"));
    tree.SetItemSubLabel(node, _T("晚上一起吃饭吗"));
    tree.SetItemRightStacked(node, _T("09:12"), 3);
    tree.SetItemStatusIcon(node, status);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    const RECT row = { 0, 0, kTreeW, kConvRowH };

    //先用短名字、不打标记画一遍，量出右侧时间文字与状态图标的左边界。名字短，
    //右上角那一条里的深色像素就只有时间文字；它们的位置与名字长短无关
    canvas.Paint(tree);
    RECT topRight = row;
    topRight.left   = kTreeW - kRightTextBandW;
    topRight.bottom = kConvRowH / 2;
    const PixelBox timeText = canvas.DarkBoundingBox(topRight, kTextLumaMax);
    const PixelBox statusBox = canvas.BoundingBox(row, kStatusGreen);
    REQUIRE_FALSE(timeText.m_empty);
    REQUIRE_FALSE(statusBox.m_empty);

    //换成超长名字并打上两个标记：名字被省略给它们让位，两个标记都画得出来
    tree.SetItemLabel(node,
        _T("莫梅、江怡、黄涛波、秦明芳、向瑶娜、罗俊辉、赵卓轩、韩雪飞、韩娟丹"));
    tree.SetItemStarred(node, true);
    tree.SetItemFavorite(node, true);
    canvas.Paint(tree);
    const PixelBox star = canvas.BoundingBox(row, kStarGold);
    const PixelBox fav  = canvas.BoundingBox(row, kFavoriteRose);
    REQUIRE_FALSE(star.m_empty);
    REQUIRE_FALSE(fav.m_empty);
    INFO("徽章右边界 = " << fav.m_rc.right << "，时间左边界 = " << timeText.m_rc.left
         << "，状态图标左边界 = " << statusBox.m_rc.left);
    CHECK(fav.m_rc.right <= timeText.m_rc.left);
    CHECK(fav.m_rc.right <= statusBox.m_rc.left);

    ::DeleteObject(status);
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
// 效果图（默认不运行）
// =====================================================================
//
// 用与界面完全相同的绘制代码，把群 Tab、会话 Tab、联系人 Tab 各几行典型内容画到一张
// PNG 上，供人工核对置顶样式与收藏徽章。标签带 [.]，默认不运行；输出路径取自环境变量
// FLAMINGO_SNAPSHOT_PATH，没设时直接跳过。用法：
//     set FLAMINGO_SNAPSHOT_PATH=E:\flamingo\snapshots\row_marks.png
//     FlamingoUnit.exe "[snapshot]"

namespace {

// 效果图里每棵树的宽度（逻辑像素），与主面板列表宽度相当。
const int kSnapTreeW = 320;

// 效果图的外边距与每棵树上方说明文字所占的高度（逻辑像素）。
const int kSnapMargin   = 16;
const int kSnapCaptionH = 22;

// 三棵树各自的高度：群树 5 行 × 40，会话树 3 行 × 48，联系人树 4 行 × 40。
const int kSnapGroupTreeH   = 5 * kRowH;
const int kSnapConvTreeH    = 3 * kConvRowH;
const int kSnapContactTreeH = 4 * kRowH;

// 头像边长（逻辑像素），与主面板群树 / 会话树一致。
const int kSnapAvatarSmall = 32;
const int kSnapAvatarLarge = 36;

// 效果图里单聊行与联系人行用的在线状态：在线（presence_type 的 1），用真实的状态徽标位图。
const int kSnapPresenceOnline = 1;

// 充当头像的几种底色。
const COLORREF kSnapAvatarColors[] =
{
    RGB(0x5B, 0x8F, 0xF9), RGB(0x61, 0xDD, 0xAA), RGB(0xF6, 0xBD, 0x16), RGB(0xE8, 0x68, 0x4A),
};
const int kSnapAvatarColorCount = sizeof(kSnapAvatarColors) / sizeof(kSnapAvatarColors[0]);

} // anonymous namespace

TEST_CASE("SNAP 行标记效果图：置顶群与收藏徽章", "[.][snapshot][rowmark]")
{
    TCHAR path[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariable(_T("FLAMINGO_SNAPSHOT_PATH"), path, MAX_PATH) == 0)
    {
        SUCCEED("FLAMINGO_SNAPSHOT_PATH not set, snapshot skipped");
        return;
    }

    std::vector<HBITMAP> bitmaps;
    for (int i = 0; i < kSnapAvatarColorCount; ++i)
    {
        bitmaps.push_back(MakeSolidBitmap(kSnapAvatarLarge, kSnapAvatarColors[i]));
    }
    HBITMAP status = CreatePresenceBadgeBitmap(kSnapPresenceOnline, kStatusIconSize);

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
        groups.SetItemConversationFlags(id, groupPinned[i], false);
        groups.SetItemFavorite(id, groupFavorite[i]);
    }

    // ---- 会话 Tab：置顶 + 特别关注 + 收藏的单聊、收藏的群聊、普通单聊 ----
    MainPanelTreeView convs;
    convs.SetRowHeight(kConvRowH);
    convs.SetIconSize(kSnapAvatarLarge);
    convs.SetIconCornerRadius(kSnapAvatarLarge / 2);
    convs.SetIconUsesAlpha(true);
    const int c1 = convs.AddRoot(_T("莫梅"));
    convs.SetItemSubLabel(c1, _T("晚上一起吃饭吗"));
    convs.SetItemRightStacked(c1, _T("09:12"), 2);
    convs.SetItemConversationFlags(c1, true, false);
    convs.SetItemStatusIcon(c1, status);
    convs.SetItemStarred(c1, true);
    convs.SetItemFavorite(c1, true);
    convs.SetItemIcon(c1, bitmaps[0]);
    const int c2 = convs.AddRoot(_T("产品设计组"));
    convs.SetItemSubLabel(c2, _T("江怡：设计稿已经更新"));
    convs.SetItemRightStacked(c2, _T("昨天"), 0);
    convs.SetItemFavorite(c2, true);
    convs.SetItemIcon(c2, bitmaps[1]);
    const int c3 = convs.AddRoot(_T("黄涛波"));
    convs.SetItemSubLabel(c3, _T("好的"));
    convs.SetItemRightStacked(c3, _T("周一"), 0);
    convs.SetItemStatusIcon(c3, status);
    convs.SetItemIcon(c3, bitmaps[2]);

    // ---- 联系人 Tab（部门树）：特别关注 + 收藏、仅收藏、普通 ----
    MainPanelTreeView contacts;
    contacts.SetRowHeight(kRowH);
    contacts.SetIconSize(kSnapAvatarSmall);
    contacts.SetIconCornerRadius(kSnapAvatarSmall / 2);
    contacts.SetIconUsesAlpha(true);
    const int dRoot = contacts.AddRoot(_T("设计部 · 3"));
    contacts.SetExpanded(dRoot, true);
    contacts.SetItemSelectable(dRoot, false);
    const TCHAR* people[] = { _T("莫梅"), _T("江怡"), _T("黄涛波") };
    const bool personStar[] = { true, false, false };
    const bool personFav[]  = { true, true, false };
    for (int i = 0; i < 3; ++i)
    {
        const int id = contacts.AddChild(dRoot, people[i]);
        contacts.SetItemSubLabel(id, _T("资深设计师"));
        contacts.SetItemStatusIcon(id, status);
        contacts.SetItemIcon(id, bitmaps[(i + 1) % kSnapAvatarColorCount]);
        contacts.SetItemStarred(id, personStar[i]);
        contacts.SetItemFavorite(id, personFav[i]);
    }

    // ---- 排版：三棵树自上而下，每棵上方一行说明 ----
    const int canvasW = kSnapTreeW + kSnapMargin * 2;
    const int canvasH = kSnapMargin * 4 + kSnapCaptionH * 3
                      + kSnapGroupTreeH + kSnapConvTreeH + kSnapContactTreeH;
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

    canvas.DrawCaption(kSnapMargin, y, _T("联系人 Tab（部门树）"));
    y += kSnapCaptionH;
    RECT rcContacts = { kSnapMargin, y, kSnapMargin + kSnapTreeW, y + kSnapContactTreeH };
    canvas.PaintAt(contacts, rcContacts);

    CHECK(canvas.SavePng(path));

    for (size_t i = 0; i < bitmaps.size(); ++i)
    {
        ::DeleteObject(bitmaps[i]);
    }
    ::DeleteObject(status);
}

// =====================================================================
// CRL 组：会话行右端的排布
// =====================================================================
//
// 会话行（单聊）右端同时有三样东西：基类画的在线状态徽标（贴行右端、整行垂直居中），
// 以及 MainPanelTreeView 自己画的上排时间与下排未读胶囊（右边缘都在 m_rcItem.right -
// kBadgeRightPad）。后两者不给状态徽标让位时就会叠在一起，本组用例量出三者各自占的
// 范围并断言互不相交。

namespace {

// 在线状态取值：在线（presence_type 的 1）。
const int kPresenceOnline = 1;

// 状态徽标边长，与 BalloonMainPanel.cpp 的 kPresenceBadgeSizePx 一致。
const int kRealBadgeSizePx = 12;

// 未读胶囊的红色，与 MainPanelTreeView.cpp 的 kBadgeRed 同值。
const COLORREF kUnreadRed = RGB(0xFF, 0x4D, 0x4F);

// 判定"非白像素"的亮度下限：三个通道都不低于它的算白底。
const int kNearWhite = 0xF8;

// 判定"灰色文字像素"时三个通道之间允许的最大差值。灰色文字三通道几乎相等；在线状态
// 的绿 #16A34A 三通道相差一百多，远超此值，不会被当成文字。
const int kGraySpreadMax = 0x30;

// 两个像素范围是否相交。
bool BoxesIntersect(const PixelBox& a, const PixelBox& b)
{
    if (a.m_empty || b.m_empty)
    {
        return false;
    }
    RECT out;
    return ::IntersectRect(&out, &a.m_rc, &b.m_rc) != FALSE;
}

} // anonymous namespace

// 回归用例，钉住 2026-09-30 修复的单聊会话行右端重叠问题：修复前未读胶囊与状态徽标重叠约
// 11×7 像素、时间文字下沿与徽标相交 1 像素。
TEST_CASE("CRL1 会话行右端：在线状态徽标与时间、未读胶囊互不重叠", "[convrow]")
{
    HBITMAP badge = CreatePresenceBadgeBitmap(kPresenceOnline, kRealBadgeSizePx);
    REQUIRE(badge != NULL);

    OffscreenCanvas canvas(kTreeW, kTreeH);
    REQUIRE(canvas.Ok());
    const RECT row = { 0, 0, kTreeW, kConvRowH };
    RECT rightStrip = row;
    rightStrip.left = kTreeW - kRightTextBandW;

    //一、只有状态徽标：量出它占的范围（右端那一条里的非白像素）
    PixelBox badgeBox;
    {
        MainPanelTreeView tree;
        tree.SetRowHeight(kConvRowH);
        const int node = tree.AddRoot(_T("莫梅"));
        tree.SetItemSubLabel(node, _T("晚上一起吃饭吗"));
        tree.SetItemStatusIcon(node, badge);
        canvas.Paint(tree);
        badgeBox = canvas.NonWhiteBoundingBox(rightStrip, kNearWhite);
    }

    //二、三样同时在场（真实单聊行的形态）：量出时间文字与未读胶囊各自占的范围。
    //状态徽标是彩色的，既不算灰色文字像素，也不是未读胶囊的红，不会混进这两个范围；
    //它自己的位置不受右侧列排布影响，沿用第一步量到的
    PixelBox timeBox;
    PixelBox unreadBox;
    {
        MainPanelTreeView tree;
        tree.SetRowHeight(kConvRowH);
        const int node = tree.AddRoot(_T("莫梅"));
        tree.SetItemSubLabel(node, _T("晚上一起吃饭吗"));
        tree.SetItemRightStacked(node, _T("09:12"), 2);
        tree.SetItemStatusIcon(node, badge);
        canvas.Paint(tree);
        RECT topRight = rightStrip;
        topRight.bottom = kConvRowH / 2;
        timeBox   = canvas.GrayTextBoundingBox(topRight, kTextLumaMax, kGraySpreadMax);
        unreadBox = canvas.BoundingBox(rightStrip, kUnreadRed);
    }

    REQUIRE_FALSE(badgeBox.m_empty);
    REQUIRE_FALSE(timeBox.m_empty);
    REQUIRE_FALSE(unreadBox.m_empty);
    INFO("状态徽标 x[" << badgeBox.m_rc.left << "," << badgeBox.m_rc.right << ") y["
         << badgeBox.m_rc.top << "," << badgeBox.m_rc.bottom << ")；时间 x["
         << timeBox.m_rc.left << "," << timeBox.m_rc.right << ") y["
         << timeBox.m_rc.top << "," << timeBox.m_rc.bottom << ")；未读胶囊 x["
         << unreadBox.m_rc.left << "," << unreadBox.m_rc.right << ") y["
         << unreadBox.m_rc.top << "," << unreadBox.m_rc.bottom << ")");
    CHECK_FALSE(BoxesIntersect(badgeBox, timeBox));
    CHECK_FALSE(BoxesIntersect(badgeBox, unreadBox));

    ::DeleteObject(badge);
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
    treeA.SetItemConversationFlags(a, true, false);

    // b：反过来的顺序，最后一次调用一定按全部状态算出占位并写入，作为参照。
    MainPanelTreeView treeB;
    const int b = treeB.AddRoot(_T("张伟"));
    treeB.SetItemConversationFlags(b, true, false);
    treeB.SetItemRightStacked(b, kRrvTime, kRrvUnread);

    const CString textA = treeA.GetItemRightText(a);
    const CString textB = treeB.GetItemRightText(b);
    CHECK(textA == textB);
    REQUIRE_FALSE(textA.IsEmpty());
    CHECK(IsAllSpaces(textA));

    // 占位至少要盖住上排的时间文字，否则左侧的名字会顶到时间下面。
    CHECK(MeasureWithDefaultFont(textA) >= MeasureWithDefaultFont(kRrvTime));
}

TEST_CASE("RRV2 免打扰从关变开:占位变宽并写回", "[rowmark][reserve]")
{
    MainPanelTreeView tree;
    const int node = tree.AddRoot(_T("李娜"));
    tree.SetItemRightStacked(node, kRrvTime, kRrvUnread);
    tree.SetItemConversationFlags(node, false, false);
    const CString before = tree.GetItemRightText(node);

    tree.SetItemConversationFlags(node, false, true);
    const CString after = tree.GetItemRightText(node);

    // 免打扰在未读胶囊左边多一个静音图标，下排变宽；这里时间比胶囊窄，占位由下排决定。
    CHECK(after.GetLength() > before.GetLength());

    // 与一开始就带免打扰的行结果一致。
    MainPanelTreeView fresh;
    const int f = fresh.AddRoot(_T("李娜"));
    fresh.SetItemConversationFlags(f, false, true);
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

