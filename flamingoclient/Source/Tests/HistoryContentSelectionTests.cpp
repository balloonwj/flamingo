/**
 *  HistoryContentSelectionTests.cpp —— 「消息记录」正文拖选复制的单元测试（Suite HCS）
 *
 *  被测的是 HistoryContentView 的文字选区：按下拖动选出一段、单击不留选区、双击选中
 *  整条、选区里的表情复制成表情段、同一时刻只有一条消息有选区、折叠态下拖选不越过
 *  可见部分。
 *
 *  做法：把控件画到一块离屏画布上（绘制时才会建出字符级排版模型），再按模型算出某个
 *  字符的坐标，直接调用控件的鼠标处理函数模拟拖动。控件没有挂到窗口上，抓取鼠标一步
 *  什么都不做，不影响选区逻辑。
 *
 *  含表情的正文画出来需要 GDI+ 与 Bin/Face 下的表情图片（见 HistoryContentViewTests.cpp
 *  文件头），所以涉及表情的用例 HCS4 用双击构造选区，不经过绘制。
 *
 *  balloonwj@qq.com   2026-09-30
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonHistory/HistoryContentView.h"
#include "../UI/BalloonChatWnd/ChatClipboardContent.h"
#include "../UI/BalloonChatWnd/ChatTextSelection.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

#include <memory>
#include <string>
#include <vector>

using namespace balloonhistory;

namespace {

// 用例里控件的左上角（宿主客户区坐标）。故意不取 (0,0)：坐标换算若漏了减去控件原点，
// 选区端点就会算错，用例能暴露出来。
const int kOriginX = 12;
const int kOriginY = 34;

// 普通用例的正文区宽度（像素），足够把样本文本排在一行里。
const int kWideW = 400;

// 折叠用例的正文区宽度（像素）。取得很窄，长文一定会超过折叠上限的 5 行。
const int kNarrowW = 120;

// 折叠用例的正文长度（字符数）。窄宽度下远超 5 行。
const int kLongTextLen = 400;

// 表情编号：取一个常见的系统表情编号即可，本组用例不加载表情图片。
const int kSampleFaceId = 5;

/**
 *  一块离屏画布：内存 DC + 与屏幕兼容的位图。本组用例只需要"画一遍"来建出排版
 *  模型，不检查像素，所以不必像 HistoryContentViewTests 那样用 DIB。
 */
class Canvas
{
public:
    Canvas(int w, int h)
        : m_hdc(NULL)
        , m_bmp(NULL)
        , m_oldBmp(NULL)
    {
        HDC screen = ::GetDC(NULL);
        m_hdc = ::CreateCompatibleDC(screen);
        m_bmp = ::CreateCompatibleBitmap(screen, w, h);
        ::ReleaseDC(NULL, screen);
        if (m_hdc != NULL && m_bmp != NULL)
        {
            m_oldBmp = (HBITMAP)::SelectObject(m_hdc, m_bmp);
        }
    }

    ~Canvas()
    {
        if (m_hdc != NULL && m_oldBmp != NULL)
        {
            ::SelectObject(m_hdc, m_oldBmp);
        }
        if (m_bmp != NULL)
        {
            ::DeleteObject(m_bmp);
        }
        if (m_hdc != NULL)
        {
            ::DeleteDC(m_hdc);
        }
    }

    HDC Get() const { return m_hdc; }

private:
    HDC     m_hdc;      // 内存 DC；构造时创建、析构时删除
    HBITMAP m_bmp;      // 选进内存 DC 的位图；构造时创建、析构时删除
    HBITMAP m_oldBmp;   // 内存 DC 原先的位图，析构时选回
};

// 控件在宿主客户区里的矩形：左上角固定，宽取 width，高按控件自己量出来的算。
RECT ViewRect(const HistoryContentView& view, int width)
{
    RECT rc;
    rc.left   = kOriginX;
    rc.top    = kOriginY;
    rc.right  = kOriginX + width;
    rc.bottom = kOriginY + view.MeasureHeight(width);
    return rc;
}

// 定好矩形并画一遍，让控件建出字符级排版模型。
void LayoutAndPaint(HistoryContentView& view, const RECT& rc)
{
    Canvas canvas(rc.right, rc.bottom);
    view.ForceLayout(rc);
    view.OnPaint(canvas.Get(), rc);
}

// 在排版模型里找到第 index 个字符所在的行与段；找到时回填并返回 true。
bool FindChar(const balloonchatwnd::SelLayout& layout, int index,
              const balloonchatwnd::SelLine*& lineOut, const balloonchatwnd::SelRun*& runOut)
{
    for (size_t i = 0; i < layout.m_lines.size(); ++i)
    {
        const balloonchatwnd::SelLine& line = layout.m_lines[i];
        for (size_t k = 0; k < line.m_runs.size(); ++k)
        {
            const balloonchatwnd::SelRun& run = line.m_runs[k];
            if (index >= run.m_charBegin && index < run.m_charBegin + run.m_charCount)
            {
                lineOut = &line;
                runOut  = &run;
                return true;
            }
        }
    }
    return false;
}

// 第 index 个字符左半边里的一点（宿主客户区坐标）：拖选端点落在这里时，下标取该字符之前。
POINT PointBeforeChar(const balloonchatwnd::SelLayout& layout, const RECT& rc, int index)
{
    POINT pt = { rc.left, rc.top };
    const balloonchatwnd::SelLine* line = NULL;
    const balloonchatwnd::SelRun*  run  = NULL;
    if (!FindChar(layout, index, line, run))
    {
        return pt;
    }
    const int k    = index - run->m_charBegin;
    const int left = (k == 0) ? run->m_left : run->m_charRights[(size_t)k - 1];
    pt.x = rc.left + left + 1;
    pt.y = rc.top + (line->m_top + line->m_bottom) / 2;
    return pt;
}

// 第 index 个字符右半边里的一点（宿主客户区坐标）：拖选端点落在这里时，下标取该字符之后。
POINT PointAfterChar(const balloonchatwnd::SelLayout& layout, const RECT& rc, int index)
{
    POINT pt = { rc.left, rc.top };
    const balloonchatwnd::SelLine* line = NULL;
    const balloonchatwnd::SelRun*  run  = NULL;
    if (!FindChar(layout, index, line, run))
    {
        return pt;
    }
    const int k = index - run->m_charBegin;
    pt.x = rc.left + run->m_charRights[(size_t)k] - 1;
    pt.y = rc.top + (line->m_top + line->m_bottom) / 2;
    return pt;
}

// 模拟一次完整的拖选：在 from 按下、移动到 to、在 to 抬起。
void DragSelect(HistoryContentView& view, POINT from, POINT to)
{
    view.OnLButtonDown(from, MK_LBUTTON);
    view.OnMouseMove(to, MK_LBUTTON);
    view.OnLButtonUp(to, 0);
}

// 当前选区的纯文字形式（表情写成 "[表情]"），即粘到记事本里看到的内容。
std::wstring SelectedPlain(const HistoryContentView& view)
{
    return balloonchatwnd::ClipSegmentsToPlainText(view.GetSelectedClipSegments());
}

// 排版模型里可见部分的末尾下标（各段 m_charBegin + m_charCount 的最大值）。
int VisibleEnd(const balloonchatwnd::SelLayout& layout)
{
    int end = 0;
    for (size_t i = 0; i < layout.m_lines.size(); ++i)
    {
        const balloonchatwnd::SelLine& line = layout.m_lines[i];
        for (size_t k = 0; k < line.m_runs.size(); ++k)
        {
            const int runEnd = line.m_runs[k].m_charBegin + line.m_runs[k].m_charCount;
            if (runEnd > end)
            {
                end = runEnd;
            }
        }
    }
    return end;
}

} // namespace

TEST_CASE("HCS1 从第 1 个字拖到第 5 个字，选中前 5 个字", "[histsel]")
{
    HistoryContentView view;
    view.SetExpanded(true);
    view.SetContent(_T("abcdefghij"));
    const RECT rc = ViewRect(view, kWideW);
    LayoutAndPaint(view, rc);

    const balloonchatwnd::SelLayout* layout = view.Test_GetSelLayout();
    REQUIRE(layout != NULL);

    DragSelect(view, PointBeforeChar(*layout, rc, 0), PointAfterChar(*layout, rc, 4));
    CHECK(view.HasTextSelection());
    CHECK(SelectedPlain(view) == L"abcde");
}

TEST_CASE("HCS2 单击不拖动，不留下选区", "[histsel]")
{
    HistoryContentView view;
    view.SetExpanded(true);
    view.SetContent(_T("abcdefghij"));
    const RECT rc = ViewRect(view, kWideW);
    LayoutAndPaint(view, rc);

    const balloonchatwnd::SelLayout* layout = view.Test_GetSelLayout();
    REQUIRE(layout != NULL);

    const POINT pt = PointAfterChar(*layout, rc, 2);
    view.OnLButtonDown(pt, MK_LBUTTON);
    view.OnLButtonUp(pt, 0);
    CHECK_FALSE(view.HasTextSelection());
    CHECK(view.GetSelectedClipSegments().empty());
}

TEST_CASE("HCS3 双击正文，选中整条", "[histsel]")
{
    HistoryContentView view;
    view.SetExpanded(true);
    view.SetContent(_T("你好 world"));
    const RECT rc = ViewRect(view, kWideW);
    LayoutAndPaint(view, rc);

    const balloonchatwnd::SelLayout* layout = view.Test_GetSelLayout();
    REQUIRE(layout != NULL);

    CHECK(view.OnLButtonDblClk(PointAfterChar(*layout, rc, 1), MK_LBUTTON));
    CHECK(view.HasTextSelection());
    CHECK(SelectedPlain(view) == L"你好 world");
}

TEST_CASE("HCS4 选区里包含表情时，复制出的片段带表情段", "[histsel]")
{
    HistoryContentView view;
    view.SetExpanded(true);
    view.SetContent(_T("前/f[\"5\"]后"));

    //双击选中整条，不经过绘制（含表情的绘制依赖 GDI+ 与表情图片，见文件头）
    POINT pt = { kOriginX, kOriginY };
    view.OnLButtonDblClk(pt, MK_LBUTTON);

    const std::vector<balloonchatwnd::ChatClipSegment> segs = view.GetSelectedClipSegments();
    REQUIRE(segs.size() == 3);
    CHECK(segs[0].m_kind == balloonchatwnd::kChatClipText);
    CHECK(segs[0].m_text == L"前");
    CHECK(segs[1].m_kind == balloonchatwnd::kChatClipFace);
    CHECK(segs[1].m_faceId == kSampleFaceId);
    CHECK(segs[2].m_kind == balloonchatwnd::kChatClipText);
    CHECK(segs[2].m_text == L"后");
    CHECK(balloonchatwnd::ClipSegmentsToPlainText(segs) == L"前[表情]后");
}

TEST_CASE("HCS5 在另一条消息上开始拖选，前一条的选区被清掉", "[histsel]")
{
    //两条正文挂在同一个父容器下，与消息记录列表里各行的关系一致
    balloonwjui::DuiVBox parent;
    HistoryContentView* first  = new HistoryContentView();
    HistoryContentView* second = new HistoryContentView();
    parent.AddChild(std::unique_ptr<balloonwjui::DuiControl>(first));
    parent.AddChild(std::unique_ptr<balloonwjui::DuiControl>(second));

    first->SetExpanded(true);
    first->SetContent(_T("first message"));
    second->SetExpanded(true);
    second->SetContent(_T("second message"));

    const RECT rcFirst = ViewRect(*first, kWideW);
    RECT rcSecond = ViewRect(*second, kWideW);
    ::OffsetRect(&rcSecond, 0, rcFirst.bottom - rcSecond.top);
    LayoutAndPaint(*first, rcFirst);
    LayoutAndPaint(*second, rcSecond);

    const balloonchatwnd::SelLayout* layoutFirst  = first->Test_GetSelLayout();
    const balloonchatwnd::SelLayout* layoutSecond = second->Test_GetSelLayout();
    REQUIRE(layoutFirst != NULL);
    REQUIRE(layoutSecond != NULL);

    DragSelect(*first, PointBeforeChar(*layoutFirst, rcFirst, 0),
               PointAfterChar(*layoutFirst, rcFirst, 4));
    REQUIRE(first->HasTextSelection());

    DragSelect(*second, PointBeforeChar(*layoutSecond, rcSecond, 0),
               PointAfterChar(*layoutSecond, rcSecond, 5));
    CHECK_FALSE(first->HasTextSelection());
    CHECK(second->HasTextSelection());
    CHECK(SelectedPlain(*second) == L"second");
}

TEST_CASE("HCS6 折叠态下拖到可见末尾之外，选区只到可见部分为止", "[histsel]")
{
    HistoryContentView view;
    view.SetExpanded(false);
    view.SetContent(CString(std::wstring((size_t)kLongTextLen, L'x').c_str()));
    const RECT rc = ViewRect(view, kNarrowW);
    REQUIRE(view.NeedsExpandLink(kNarrowW));
    LayoutAndPaint(view, rc);

    const balloonchatwnd::SelLayout* layout = view.Test_GetSelLayout();
    REQUIRE(layout != NULL);
    const int visibleEnd = VisibleEnd(*layout);
    REQUIRE(visibleEnd > 0);
    REQUIRE(visibleEnd < kLongTextLen);

    //终点取在控件右下角之外：映射规则会把它归到"全文末尾"，控件须把它限制在可见末尾
    POINT beyond;
    beyond.x = rc.right + kNarrowW;
    beyond.y = rc.bottom + kNarrowW;
    DragSelect(view, PointBeforeChar(*layout, rc, 0), beyond);

    CHECK(view.HasTextSelection());
    CHECK(SelectedPlain(view) == std::wstring((size_t)visibleEnd, L'x'));
}
