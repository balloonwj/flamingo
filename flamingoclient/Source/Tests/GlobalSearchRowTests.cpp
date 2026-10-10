/**
 *  主面板全局搜索结果行绘制的单元测试（Suite GSR），GlobalSearchRowTests.cpp
 *
 *  把控件画到离屏画布上，按像素颜色核对新加的绘制效果：
 *    GSR1  MainPanelListItem::SetSubHighlight：副行关键字下画出搜索命中黄底，只落在副行那一带；
 *          不设高亮时整行没有黄底；
 *    GSR2  GroupFileItem::SetHighlighted：被定位的群文件行画出淡黄底，取消后没有；
 *    GSR3  SearchFilterTab::GetDesiredSize：期望宽度随计数位数变宽，不小于最小宽度；
 *    GSR4  MainPanelListItem::SetHoverActions：按钮从右往左排、命中判定落在各自的图标上；按钮
 *          显示时画出图标并隐藏第一行右侧的时间文字；SetLocateButtonVisible 与只配一个定位按钮
 *          相同；
 *    GSSNAP（隐藏，标签 [.][snapshot][globalsearchsnap]）：用与搜索下拉相同的结果行、筛选标签
 *          控件，把「消息」「文件」「全部」三个标签的典型内容画成一张 PNG 供人工核对。先设
 *          环境变量 FLAMINGO_SNAPSHOT_PATH 为输出路径，再跑 FlamingoUnit.exe "[globalsearchsnap]"。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <gdiplus.h>
#include <string>
#include <vector>

#include "../UI/BalloonMain/MainPanelListItem.h"
#include "../UI/BalloonChatWnd/GroupFileItem.h"
#include "../UI/BalloonSearch/SearchFilterTab.h"
#include "../UI/BalloonSearch/GlobalSearchText.h"
#include "../Common/Graphics/ShellTypeIcon.h"

namespace {

// 搜索命中黄底（与 MainPanelListItem.cpp 的 kHighlightFill 一致）
const COLORREF kHitYellow = RGB(0xFF, 0xF1, 0x76);
// 群文件定位淡黄底（与 GroupFileItem.cpp 的 kLocateFill 一致）
const COLORREF kLocateYellow = RGB(0xFF, 0xF4, 0xC2);

// 结果行的尺寸（与 SearchDropdown.cpp 的 kResultRowHeight、主面板默认宽度 338 一致）
const int kRowW = 338;
const int kRowH = 54;
// 结果行里名称与副行两行文字的总高（与 MainPanelListItem::OnPaint 的 blockH 一致）
const int kTextBlockH = 33;
// 副行相对文字块顶部的偏移（与 OnPaint 里 subRc 的 blockTop + 17 一致）
const int kSubOffset = 17;
// 群文件行的尺寸（边栏宽 240，行高 kGroupFileRowHeight）
const int kFileRowW = 240;

// 进程内启动一次 GDI+（结果行与群文件行都用 GDI+ 画形状）。
class ScopedGdiplusForSearch
{
public:
    ScopedGdiplusForSearch()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    ~ScopedGdiplusForSearch()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌
    bool      m_ok;      // 是否启动成功
};

// 在 GDI+ 已注册的图像编码器里找 PNG 编码器的 CLSID。
bool FindPngEncoderForSearch(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
    {
        return false;
    }
    std::vector<BYTE> buf(bytes);
    Gdiplus::ImageCodecInfo* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    if (Gdiplus::GetImageEncoders(count, bytes, info) != Gdiplus::Ok)
    {
        return false;
    }
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(info[i].MimeType, L"image/png") == 0)
        {
            out = info[i].Clsid;
            return true;
        }
    }
    return false;
}

/**
 *  一张白底离屏画布：32 位自上而下 DIB，可按区域数某种颜色的像素，可存成 PNG。
 */
class SearchCanvas
{
public:
    SearchCanvas(int w, int h)
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
        RECT all = { 0, 0, w, h };
        Fill(all, RGB(0xFF, 0xFF, 0xFF));
    }

    ~SearchCanvas()
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

    // 绘制用的设备上下文。
    HDC Dc() const
    {
        return m_dc;
    }

    // 用纯色填充 rc。
    void Fill(const RECT& rc, COLORREF color)
    {
        HBRUSH brush = ::CreateSolidBrush(color);
        ::FillRect(m_dc, &rc, brush);
        ::DeleteObject(brush);
    }

    // 在 rc 里用默认 GUI 字体写一行灰色文字（效果图里的分组标题与说明）。
    void DrawCaption(const RECT& rc, LPCTSTR text)
    {
        HGDIOBJ oldFont = ::SelectObject(m_dc, ::GetStockObject(DEFAULT_GUI_FONT));
        ::SetBkMode(m_dc, TRANSPARENT);
        ::SetTextColor(m_dc, RGB(0x9A, 0x9A, 0x9A));
        RECT r = rc;
        ::DrawText(m_dc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        ::SelectObject(m_dc, oldFont);
    }

    // 数 rc 范围内颜色恰为 want 的像素。
    int CountColor(const RECT& rc, COLORREF want) const
    {
        const unsigned int target = ((unsigned int)GetRValue(want) << 16)
                                  | ((unsigned int)GetGValue(want) << 8)
                                  |  (unsigned int)GetBValue(want);
        int count = 0;
        for (int y = rc.top; y < rc.bottom && y < m_h; ++y)
        {
            for (int x = rc.left; x < rc.right && x < m_w; ++x)
            {
                if ((m_bits[y * m_w + x] & 0x00FFFFFFu) == target)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    // 把整张画布存成 PNG。成功返回 true。
    bool SavePng(LPCWSTR path) const
    {
        Gdiplus::Bitmap bmp(m_bmp, NULL);
        CLSID pngClsid;
        if (!FindPngEncoderForSearch(pngClsid))
        {
            return false;
        }
        return bmp.Save(path, &pngClsid, NULL) == Gdiplus::Ok;
    }

private:
    int                 m_w;     // 画布宽（像素）
    int                 m_h;     // 画布高（像素）
    HDC                 m_dc;    // 选入了 DIB 的内存 DC，析构时删除
    HBITMAP             m_bmp;   // 32 位 DIB，析构时删除
    const unsigned int* m_bits;  // DIB 像素首地址（随 m_bmp 释放）
};

// 把一个控件画到画布的 rc 区域。
void PaintControlAt(SearchCanvas& canvas, balloonwjui::DuiControl& ctrl, const RECT& rc)
{
    ctrl.SetRect(rc);
    ctrl.OnPaint(canvas.Dc(), rc);
}

// 名称文字带的范围（行内顶部到副行之前），用于确认副行高亮没有画到名称上。
RECT NameBand(const RECT& row)
{
    const int blockTop = row.top + (kRowH - kTextBlockH) / 2;
    RECT rc = { row.left, blockTop, row.right, blockTop + kSubOffset - 1 };
    return rc;
}

// 副行文字带的范围。
RECT SubBand(const RECT& row)
{
    const int blockTop = row.top + (kRowH - kTextBlockH) / 2;
    RECT rc = { row.left, blockTop + kSubOffset, row.right, blockTop + kTextBlockH };
    return rc;
}

} // namespace

TEST_CASE("GSR1 副行关键字画出搜索命中黄底，只落在副行那一带", "[globalsearch]")
{
    ScopedGdiplusForSearch gdiplus;
    REQUIRE(gdiplus.Ok());

    SearchCanvas canvas(kRowW, kRowH * 2);
    REQUIRE(canvas.Ok());

    //第一行：副行「明天交周报」里标出「周报」
    balloonmain::MainPanelListItem hit;
    hit.SetAvatar(_T("产"), RGB(0x4A, 0x90, 0xE2));
    hit.SetName(_T("产品设计组"));
    hit.SetSubText(_T("明天交周报"));
    hit.SetSubHighlight(3, 2);
    RECT row1 = { 0, 0, kRowW, kRowH };
    PaintControlAt(canvas, hit, row1);

    //第二行：同样的内容，不设高亮
    balloonmain::MainPanelListItem plain;
    plain.SetAvatar(_T("产"), RGB(0x4A, 0x90, 0xE2));
    plain.SetName(_T("产品设计组"));
    plain.SetSubText(_T("明天交周报"));
    RECT row2 = { 0, kRowH, kRowW, kRowH * 2 };
    PaintControlAt(canvas, plain, row2);

    CHECK(canvas.CountColor(SubBand(row1), kHitYellow) > 0);
    CHECK(canvas.CountColor(NameBand(row1), kHitYellow) == 0);
    CHECK(canvas.CountColor(row2, kHitYellow) == 0);

    //清除高亮（len<=0）后重画，黄底消失
    hit.SetSubHighlight(0, 0);
    canvas.Fill(row1, RGB(0xFF, 0xFF, 0xFF));
    PaintControlAt(canvas, hit, row1);
    CHECK(canvas.CountColor(row1, kHitYellow) == 0);
}

TEST_CASE("GSR2 被定位的群文件行画出淡黄底，取消后没有", "[globalsearch]")
{
    ScopedGdiplusForSearch gdiplus;
    REQUIRE(gdiplus.Ok());

    SearchCanvas canvas(kFileRowW, balloonchatwnd::kGroupFileRowHeight);
    REQUIRE(canvas.Ok());
    RECT row = { 0, 0, kFileRowW, balloonchatwnd::kGroupFileRowHeight };

    balloonchatwnd::GroupFileItem item;
    item.SetFile(_T("合同.pdf"), _T("张三 · 今天 10:20 · 300 KB"), _T("pdf"));
    item.SetHighlighted(true);
    PaintControlAt(canvas, item, row);
    CHECK(canvas.CountColor(row, kLocateYellow) > 0);

    item.SetHighlighted(false);
    canvas.Fill(row, RGB(0xFF, 0xFF, 0xFF));
    PaintControlAt(canvas, item, row);
    CHECK(canvas.CountColor(row, kLocateYellow) == 0);
}

TEST_CASE("GSR3 筛选标签的期望宽度随计数位数变宽", "[globalsearch]")
{
    //最小宽度（与 SearchFilterTab.cpp 的 kTabMinWidth 一致）
    const int kMinWidth = 40;

    balloonsearch::SearchFilterTab tab;
    tab.SetLabel(_T("全部"));
    const int bare = tab.GetDesiredSize().cx;
    CHECK(bare >= kMinWidth);

    tab.SetCount(true, 5);
    const int oneDigit = tab.GetDesiredSize().cx;
    tab.SetCount(true, 135);
    const int threeDigits = tab.GetDesiredSize().cx;
    CHECK(oneDigit >= bare);
    CHECK(threeDigits > oneDigit);

    //隐藏计数后回到不带数字的宽度
    tab.SetCount(false, 135);
    CHECK(tab.GetDesiredSize().cx == bare);

    //标签越长越宽
    balloonsearch::SearchFilterTab longer;
    longer.SetLabel(_T("联系人"));
    longer.SetCount(true, 135);
    CHECK(longer.GetDesiredSize().cx > threeDigits);
}

TEST_CASE("GSR4 结果行悬停按钮的位置、命中判定与绘制", "[globalsearch]")
{
    ScopedGdiplusForSearch gdiplus;
    REQUIRE(gdiplus.Ok());

    //行右内距与图标边长（与 MainPanelListItem.cpp 的 kRowRightPad、kActionIconSize 一致）
    const int kRowRightPad = 10;
    const int kIconSize    = 18;
    //悬停底色（键盘选中时与悬停相同）
    const COLORREF kFill = RGB(0xE9, 0xEC, 0xF1);

    SearchCanvas canvas(kRowW, kRowH * 2);
    REQUIRE(canvas.Ok());

    //第一行：没有按钮，右上角显示时间；第二行：同样内容，配「打开文件」「打开所在文件夹」
    balloonmain::MainPanelListItem plain;
    balloonmain::MainPanelListItem withActions;
    balloonmain::MainPanelListItem* rows[2] = { &plain, &withActions };
    for (int i = 0; i < 2; ++i)
    {
        rows[i]->SetAvatar(_T("文"), RGB(0x4A, 0x90, 0xE2));
        rows[i]->SetName(_T("季度报告.docx"));
        rows[i]->SetSubText(_T("产品设计组 · 1.2 MB"));
        rows[i]->SetRightText(_T("10:30"));
        rows[i]->SetHoverFill(kFill);
        //键盘选中时与鼠标悬停画法相同，离屏绘制不需要真的移动鼠标
        rows[i]->SetKeySelected(true);
    }
    std::vector<int> actions;
    actions.push_back(balloonmain::MainPanelListItem::kHoverActionOpenFile);
    actions.push_back(balloonmain::MainPanelListItem::kHoverActionOpenFolder);
    withActions.SetHoverActions(actions);

    RECT row1 = { 0, 0, kRowW, kRowH };
    RECT row2 = { 0, kRowH, kRowW, kRowH * 2 };
    PaintControlAt(canvas, plain, row1);
    PaintControlAt(canvas, withActions, row2);

    //位置：最后一个按钮贴着行右内距，前一个在它左边，都垂直居中
    const RECT rcOpen   = withActions.HoverActionRect(0);
    const RECT rcFolder = withActions.HoverActionRect(1);
    CHECK(rcFolder.right == row2.right - kRowRightPad);
    CHECK(rcFolder.right - rcFolder.left == kIconSize);
    CHECK(rcOpen.right < rcFolder.left);
    CHECK(rcOpen.top == row2.top + (kRowH - kIconSize) / 2);
    //越界下标与没有按钮的行返回空矩形
    const RECT none = withActions.HoverActionRect(2);
    CHECK(none.right == none.left);
    const RECT plainRc = plain.HoverActionRect(0);
    CHECK(plainRc.right == plainRc.left);

    //命中判定：各自图标的中心落在各自的下标上，行左侧不在任何按钮上
    POINT openCenter   = { (rcOpen.left + rcOpen.right) / 2, (rcOpen.top + rcOpen.bottom) / 2 };
    POINT folderCenter = { (rcFolder.left + rcFolder.right) / 2, (rcFolder.top + rcFolder.bottom) / 2 };
    POINT leftSide     = { row2.left + kRowRightPad, (row2.top + row2.bottom) / 2 };
    CHECK(withActions.HitTestHoverAction(openCenter) == 0);
    CHECK(withActions.HitTestHoverAction(folderCenter) == 1);
    CHECK(withActions.HitTestHoverAction(leftSide) == -1);
    CHECK(plain.HitTestHoverAction(folderCenter) == -1);

    //绘制：两个图标里都有笔画（不全是悬停底色）
    const int openArea   = (rcOpen.right - rcOpen.left) * (rcOpen.bottom - rcOpen.top);
    const int folderArea = (rcFolder.right - rcFolder.left) * (rcFolder.bottom - rcFolder.top);
    CHECK(canvas.CountColor(rcOpen, kFill) < openArea);
    CHECK(canvas.CountColor(rcFolder, kFill) < folderArea);

    //时间文字：第一行画了（图标上方那一带不全是底色），第二行隐藏（同一带全是底色）
    const int bandTop = (kRowH - kTextBlockH) / 2;
    RECT timeBand1 = { rcOpen.left, row1.top + bandTop, row1.right - kRowRightPad, row1.top + rcOpen.top - row2.top };
    RECT timeBand2 = { rcOpen.left, row2.top + bandTop, row2.right - kRowRightPad, rcOpen.top };
    const int bandArea = (timeBand2.right - timeBand2.left) * (timeBand2.bottom - timeBand2.top);
    REQUIRE(bandArea > 0);
    CHECK(canvas.CountColor(timeBand1, kFill) < bandArea);
    CHECK(canvas.CountColor(timeBand2, kFill) == bandArea);

    //SetLocateButtonVisible 等同于只配一个定位按钮；关掉后没有按钮
    balloonmain::MainPanelListItem contact;
    RECT row3 = { 0, 0, kRowW, kRowH };
    contact.SetRect(row3);
    contact.SetLocateButtonVisible(true);
    const RECT rcLocate = contact.HoverActionRect(0);
    CHECK(rcLocate.right == row3.right - kRowRightPad);
    CHECK(contact.HoverActionRect(1).right == contact.HoverActionRect(1).left);
    contact.SetLocateButtonVisible(false);
    CHECK(contact.HoverActionRect(0).right == contact.HoverActionRect(0).left);

    //未知取值被忽略
    std::vector<int> bogus;
    bogus.push_back(99);
    contact.SetHoverActions(bogus);
    CHECK(contact.HoverActionRect(0).right == contact.HoverActionRect(0).left);
}

namespace {

// 效果图的版式（像素）
const int kSnapMargin      = 16;   // 画布四周与三栏之间的留白
const int kSnapCaptionH    = 22;   // 每栏上方说明文字的高度
const int kSnapTabRowH     = 38;   // 筛选标签栏高（与 SearchDropdown.cpp 的 kTabRowHeight 一致）
const int kSnapTabPadLeft  = 10;   // 标签栏左内距（与 kTabBarPadLeft 一致）
const int kSnapTabGap      = 4;    // 标签间距（与 kTabBarGap 一致）
const int kSnapSectionH    = 24;   // 分组标题高（与 kSectionHeaderHeight 一致）
const int kSnapSmallRowH   = 32;   // 提示文字 / 「查看更多」行高（与 kSectionHintHeight 一致）
const int kSnapSectionPadX = 8;    // 分组标题与提示文字的左内距
const int kSnapPanelH      = 520;  // 每栏画布高度
// 效果图里文件结果行的系统图标边长（与结果行头像同尺寸）
const int kSnapIconPx = balloonmain::MainPanelListItem::kAvatarSizePx;
// 「查看更多」文字颜色（DuiButton Text 变体的品牌色，效果图里用文字示意）
const COLORREF kSnapMoreColor = RGB(0xFF, 0x5A, 0x44);

// 一栏效果图的绘制位置。
struct SnapCursor
{
    int m_left;   // 这一栏的左边 x
    int m_y;      // 下一个元素的顶部 y
};

// 画一排筛选标签，active 为选中的那个下标；counts 为负表示不显示数字。
void SnapTabs(SearchCanvas& canvas, SnapCursor& cur, int active, const int counts[5])
{
    const TCHAR* labels[5] = { _T("全部"), _T("联系人"), _T("群组"), _T("消息"), _T("文件") };
    int x = cur.m_left + kSnapTabPadLeft;
    for (int i = 0; i < 5; ++i)
    {
        balloonsearch::SearchFilterTab tab;
        tab.SetLabel(labels[i]);
        tab.SetCount(counts[i] >= 0, counts[i] >= 0 ? counts[i] : 0);
        tab.SetActive(i == active);
        const int w = tab.GetDesiredSize().cx;
        RECT rc = { x, cur.m_y, x + w, cur.m_y + kSnapTabRowH };
        PaintControlAt(canvas, tab, rc);
        x += w + kSnapTabGap;
    }
    cur.m_y += kSnapTabRowH;
}

// 画一个分组标题。
void SnapSection(SearchCanvas& canvas, SnapCursor& cur, LPCTSTR text)
{
    RECT rc = { cur.m_left + kSnapSectionPadX, cur.m_y, cur.m_left + kRowW, cur.m_y + kSnapSectionH };
    canvas.DrawCaption(rc, text);
    cur.m_y += kSnapSectionH;
}

// 画一行矮行文字（提示或「查看更多」）。
void SnapSmallRow(SearchCanvas& canvas, SnapCursor& cur, LPCTSTR text, COLORREF color, UINT align)
{
    HGDIOBJ oldFont = ::SelectObject(canvas.Dc(), ::GetStockObject(DEFAULT_GUI_FONT));
    ::SetBkMode(canvas.Dc(), TRANSPARENT);
    ::SetTextColor(canvas.Dc(), color);
    RECT rc = { cur.m_left + kSnapSectionPadX, cur.m_y, cur.m_left + kRowW, cur.m_y + kSnapSmallRowH };
    ::DrawText(canvas.Dc(), text, -1, &rc, align | DT_VCENTER | DT_SINGLELINE);
    ::SelectObject(canvas.Dc(), oldFont);
    cur.m_y += kSnapSmallRowH + 1;
}

// 画一行「消息」结果：会话名、时间、副行（命中一条时显示片段并标黄）。
void SnapMessageRow(SearchCanvas& canvas, SnapCursor& cur, LPCTSTR name, COLORREF color,
                    int hitCount, const std::wstring& text, const std::wstring& keyword, LPCTSTR timeText)
{
    const balloonsearch::HighlightedText sub = balloonsearch::BuildMessageHitSubText(hitCount, text, keyword);
    balloonmain::MainPanelListItem item;
    CString glyph(name);
    item.SetAvatar(glyph.Left(1), color);
    item.SetName(name);
    item.SetRightText(timeText);
    item.SetSubText(sub.m_text.c_str());
    item.SetSubHighlight(sub.m_hiStart, sub.m_hiLen);
    RECT rc = { cur.m_left, cur.m_y, cur.m_left + kRowW, cur.m_y + kRowH };
    PaintControlAt(canvas, item, rc);
    cur.m_y += kRowH + 1;
}

// 画一行「文件」结果：系统图标、文件名（标黄）、来源与大小、时间。
void SnapFileRow(SearchCanvas& canvas, SnapCursor& cur, int kind, int iconKind, const std::wstring& name,
                 const std::wstring& source, const std::wstring& size, const std::wstring& keyword,
                 LPCTSTR timeText)
{
    const balloonsearch::FileHitTexts texts =
        balloonsearch::BuildFileHitTexts(kind, name, source, size, keyword);
    balloonmain::MainPanelListItem item;
    HBITMAP icon = shellicon::CreateItemIconBitmap(iconKind, name, kSnapIconPx);
    if (icon != NULL)
    {
        item.SetOwnedAvatarBitmap(icon);
    }
    else
    {
        item.SetAvatar(_T("文"), RGB(0xB8, 0xB5, 0xB1));
    }
    item.SetName(texts.m_name.m_text.c_str());
    item.SetNameHighlight(texts.m_name.m_hiStart, texts.m_name.m_hiLen);
    item.SetRightText(timeText);
    item.SetSubText(texts.m_sub.c_str());
    RECT rc = { cur.m_left, cur.m_y, cur.m_left + kRowW, cur.m_y + kRowH };
    PaintControlAt(canvas, item, rc);
    cur.m_y += kRowH + 1;
}

// 外壳图标查询要求调用线程初始化过 COM；测试线程自己初始化一次，析构时配平。
class ScopedComForSnap
{
public:
    ScopedComForSnap()
        : m_hr(::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))
    {
    }

    ~ScopedComForSnap()
    {
        if (SUCCEEDED(m_hr))
        {
            ::CoUninitialize();
        }
    }

private:
    HRESULT m_hr;   // CoInitializeEx 的返回值，析构时据它决定要不要配平
};

} // namespace

TEST_CASE("GSSNAP 搜索下拉「消息」「文件」「全部」三个标签的效果图", "[.][snapshot][globalsearchsnap]")
{
    TCHAR path[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariable(_T("FLAMINGO_SNAPSHOT_PATH"), path, MAX_PATH) == 0)
    {
        SUCCEED("FLAMINGO_SNAPSHOT_PATH not set, snapshot skipped");
        return;
    }

    ScopedComForSnap com;
    ScopedGdiplusForSearch gdiplus;
    REQUIRE(gdiplus.Ok());

    const int canvasW = kSnapMargin * 4 + kRowW * 3;
    const int canvasH = kSnapMargin * 2 + kSnapCaptionH + kSnapPanelH;
    SearchCanvas canvas(canvasW, canvasH);
    REQUIRE(canvas.Ok());
    canvas.Fill(RECT{ 0, 0, canvasW, canvasH }, RGB(0xF0, 0xF0, 0xF0));

    const std::wstring keyword = L"设计稿";

    // ---- 第一栏：「消息」标签 ----
    SnapCursor cur;
    cur.m_left = kSnapMargin;
    cur.m_y    = kSnapMargin;
    canvas.DrawCaption(RECT{ cur.m_left, cur.m_y, cur.m_left + kRowW, cur.m_y + kSnapCaptionH },
                       _T("「消息」标签，关键字：设计稿"));
    cur.m_y += kSnapCaptionH;
    canvas.Fill(RECT{ cur.m_left, cur.m_y, cur.m_left + kRowW, cur.m_y + kSnapPanelH },
                RGB(0xFF, 0xFF, 0xFF));
    {
        const int counts[5] = { 9, 0, 1, 3, 5 };
        SnapTabs(canvas, cur, 3, counts);
    }
    SnapSection(canvas, cur, _T("聊天记录（仅搜索本机已保存的记录）"));
    SnapMessageRow(canvas, cur, _T("产品设计组"), RGB(0x4A, 0x90, 0xE2), 12, L"", keyword, _T("14:20"));
    SnapMessageRow(canvas, cur, _T("江怡"), RGB(0xE8, 0x6A, 0x5C), 1,
                   L"首页改版的设计稿我放到共享盘了，你看下", keyword, _T("昨天"));
    SnapMessageRow(canvas, cur, _T("黄涛波"), RGB(0x3F, 0xA8, 0x6B), 1,
                   L"上周评审会上说的那几个问题都改完了，新版设计稿在这里", keyword, _T("09-28"));

    // ---- 第二栏：「文件」标签 ----
    cur.m_left = kSnapMargin * 2 + kRowW;
    cur.m_y    = kSnapMargin;
    canvas.DrawCaption(RECT{ cur.m_left, cur.m_y, cur.m_left + kRowW, cur.m_y + kSnapCaptionH },
                       _T("「文件」标签：聊天文件 + 群文件"));
    cur.m_y += kSnapCaptionH;
    canvas.Fill(RECT{ cur.m_left, cur.m_y, cur.m_left + kRowW, cur.m_y + kSnapPanelH },
                RGB(0xFF, 0xFF, 0xFF));
    {
        const int counts[5] = { 9, 0, 1, 3, 5 };
        SnapTabs(canvas, cur, 4, counts);
    }
    SnapSection(canvas, cur, _T("聊天文件（仅搜索本机已保存的记录）"));
    SnapFileRow(canvas, cur, balloonsearch::kFileHitChatFile, shellicon::kItemFile,
                L"首页设计稿v3.fig", L"江怡", L"18.2 MB", keyword, _T("昨天"));
    SnapFileRow(canvas, cur, balloonsearch::kFileHitFolder, shellicon::kItemFolder,
                L"设计稿素材", L"产品设计组", L"1.4 GB", keyword, _T("09-30"));
    SnapFileRow(canvas, cur, balloonsearch::kFileHitFileShare, shellicon::kItemFolder,
                L"设计稿归档", L"黄涛波", L"", keyword, _T("09-12"));
    SnapSection(canvas, cur, _T("群文件"));
    SnapFileRow(canvas, cur, balloonsearch::kFileHitGroupFile, shellicon::kItemFile,
                L"2026Q4 官网改版设计稿评审意见.docx", L"产品设计组", L"86 KB", keyword, _T("10-01"));
    SnapFileRow(canvas, cur, balloonsearch::kFileHitGroupFile, shellicon::kItemFile,
                L"设计稿交付清单.xlsx", L"研发周会", L"24 KB", keyword, _T("2025-12-30"));

    // ---- 第三栏：「全部」标签（离线时群文件一段在「文件」标签里显示提示，这里只示意分段）----
    cur.m_left = kSnapMargin * 3 + kRowW * 2;
    cur.m_y    = kSnapMargin;
    canvas.DrawCaption(RECT{ cur.m_left, cur.m_y, cur.m_left + kRowW, cur.m_y + kSnapCaptionH },
                       _T("「全部」标签：每段最多 3 条 + 查看更多"));
    cur.m_y += kSnapCaptionH;
    canvas.Fill(RECT{ cur.m_left, cur.m_y, cur.m_left + kRowW, cur.m_y + kSnapPanelH },
                RGB(0xFF, 0xFF, 0xFF));
    {
        const int counts[5] = { 9, 0, 1, 3, 5 };
        SnapTabs(canvas, cur, 0, counts);
    }
    SnapSection(canvas, cur, _T("群组"));
    {
        balloonmain::MainPanelListItem group;
        group.SetAvatar(_T("设"), RGB(0x9B, 0x59, 0xB6));
        group.SetName(_T("设计稿评审群"));
        group.SetNameHighlight(0, 3);
        group.SetRightText(_T("8 人"));
        group.SetSubText(_T("群号 700123"));
        RECT rc = { cur.m_left, cur.m_y, cur.m_left + kRowW, cur.m_y + kRowH };
        PaintControlAt(canvas, group, rc);
        cur.m_y += kRowH + 1;
    }
    SnapSection(canvas, cur, _T("聊天记录（仅搜索本机已保存的记录）"));
    SnapMessageRow(canvas, cur, _T("产品设计组"), RGB(0x4A, 0x90, 0xE2), 12, L"", keyword, _T("14:20"));
    SnapMessageRow(canvas, cur, _T("江怡"), RGB(0xE8, 0x6A, 0x5C), 1,
                   L"首页改版的设计稿我放到共享盘了，你看下", keyword, _T("昨天"));
    SnapMessageRow(canvas, cur, _T("黄涛波"), RGB(0x3F, 0xA8, 0x6B), 1,
                   L"新版设计稿在这里", keyword, _T("09-28"));
    SnapSmallRow(canvas, cur, _T("查看更多聊天记录"), kSnapMoreColor, DT_LEFT);
    SnapSection(canvas, cur, _T("文件"));
    SnapFileRow(canvas, cur, balloonsearch::kFileHitChatFile, shellicon::kItemFile,
                L"首页设计稿v3.fig", L"江怡", L"18.2 MB", keyword, _T("昨天"));
    SnapSmallRow(canvas, cur, _T("查看更多文件"), kSnapMoreColor, DT_LEFT);

    CHECK(canvas.SavePng(path));
}
