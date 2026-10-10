/**
 *  flamingoAdmin —— 列表底部的分页条（共用控件）。
 *
 *  左侧显示「共 N 条」，右侧显示「上一页 / 第 X / 共 Y 页 / 下一页」。点击翻页按钮时通过回调把
 *  目标页码交给宿主，控件自己不改页码：宿主拉到新一页的数据后，再调用 SetPageInfo 更新显示。
 *  企业管理页与敏感词页共用（原先企业管理页里的同类控件写在页面内部，2026-10-02 抽出到这里）。
 *  单元测试见 Tests/SharedTableWidgetsTests.cpp 的 PB-1、PB-2。
 *
 *  典型用法：
 *      auto pager = std::make_unique<ui::PagerBar>();
 *      pager->SetPageCallback(&MyScreen::OnPageThunk, this);
 *      m_pager = pager.get();
 *      card->AddChild(std::move(pager), Hint().Fixed(kFooterH));
 *      //拿到一页数据后
 *      m_pager->SetPageInfo(m_page, total, kPageSize);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include "DuiControl.h"

namespace flamingoadmin { namespace ui {

// 列表底部的分页条。只负责显示总条数与当前页、发出翻页请求；页码由宿主维护。
class PagerBar : public balloonwjui::DuiControl
{
public:
    // 翻页回调。
    //   user：SetPageCallback 时传入的指针。
    //   newPage：目标页码，从 1 起，已保证在 [1, PageCount()] 之内。
    typedef void (*PageChangeFn)(void* user, int newPage);

    PagerBar();

    // 设置分页信息；与当前值相同时什么也不做，不同时重绘。宿主可以在自己的绘制过程中
    // 每次调用它而不会引起重绘循环。
    //   page：当前页码，从 1 起；小于 1 时按 1 处理。
    //   total：满足条件的总条数；负数按 0 处理。
    //   pageSize：每页条数；不大于 0 时按 1 处理。
    void SetPageInfo(int page, int total, int pageSize);

    // 注册翻页回调。fn 为空表示不接收翻页请求（点击按钮没有任何效果）。
    //   user：原样传回给回调，所有权归调用方。
    void SetPageCallback(PageChangeFn fn, void* user);

    // 设置左右内边距（像素），默认与企业管理页表格的单元格内边距相同（14）。
    void SetSidePadding(int px);

    // 当前页码（从 1 起）。
    int Page() const { return m_page; }

    // 总条数。
    int Total() const { return m_total; }

    // 总页数，至少为 1（没有数据时也显示「第 1 / 共 1 页」）。
    int PageCount() const;

    // 页码文字「第 X / 共 Y 页」。各界面的分页都经由它，保证写法一致。
    //   page：当前页码，从 1 起；pageCount：总页数。
    static CString FormatPageText(int page, int pageCount);

    // 当前是否可以往前翻（不在第 1 页）。
    bool CanPrev() const { return m_page > 1; }

    // 当前是否可以往后翻（不在最后一页）。
    bool CanNext() const { return m_page < PageCount(); }

    // 「上一页」「下一页」按钮的命中矩形（宿主客户区坐标），在 Layout 时按控件矩形算出。
    const RECT& PrevRect() const { return m_prevRc; }
    const RECT& NextRect() const { return m_nextRc; }

    // ---- DuiControl 覆写 ----
    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;

private:
    // 按当前控件矩形算出两个按钮的命中矩形与页码文字的矩形。
    void ComputeRects();

    // 画一个翻页按钮；不可用时灰底灰字。
    void DrawPagerButton(HDC hdc, const RECT& rc, LPCTSTR text, bool enabled) const;

    int          m_page;       // 当前页码，从 1 起
    int          m_total;      // 总条数
    int          m_pageSize;   // 每页条数，至少为 1
    int          m_sidePad;    // 左右内边距（像素）
    PageChangeFn m_onPage;     // 翻页回调，可为空
    void*        m_onPageUser; // 翻页回调的用户数据，所有权归调用方
    RECT         m_prevRc;     // 「上一页」按钮的命中矩形
    RECT         m_nextRc;     // 「下一页」按钮的命中矩形
    RECT         m_pageTextRc; // 「第 X / 共 Y 页」文字的矩形
};

}} // namespace flamingoadmin::ui
