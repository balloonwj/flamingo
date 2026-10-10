/**
 *  flamingoAdmin —— 带行内文字操作链接的虚拟表格（共用控件）。
 *
 *  在 balloonui 的 DuiVirtualList 之上加三样东西：
 *    - 覆盖提示文字：加载中、加载失败、列表为空、「请先选择企业」这类情况下行数为 0，基类只画空白，
 *      本控件在表格区中央补画一行提示；
 *    - 按坐标找行：RowFromPoint；
 *    - 每行最右侧的操作列里画一排文字链接（如「编辑」「删除」），点中时通过回调告诉宿主
 *      「第几行的第几个操作」，鼠标停在链接上时显示手形光标。
 *  每行其余部分仍由宿主的绘行回调来画。敏感词页首先使用（2026-10-02），以后其它需要行内操作的
 *  表格也应复用本控件。单元测试见 Tests/SharedTableWidgetsTests.cpp 的 AT-1、AT-2。
 *  某个操作只对部分行有意义时（如「强制取消」只对有效的共享），宿主可以注册「链接是否显示」回调
 *  （SetActionVisibility，2026-10-03 起）：不显示的链接不画、不响应点击，其余链接位置不变。
 *  单元测试见 Tests/FileShareScreensTests.cpp 的 AT-5。
 *  同一位置按行显示不同链接时（如机器人页的「停用」「启用」二选一，2026-10-08 起），宿主可以再打开
 *  SetCollapseHiddenActions：不显示的链接不占位置，后面的链接向左靠拢。单元测试见 Tests/EnterpriseBotTests.cpp。
 *
 *  注意：本控件已经占用了基类的 SetPaintRowCallback（用来在宿主画完一行之后补画链接），宿主要改用
 *  SetRowPainter 注册绘行回调，不要再调用 SetPaintRowCallback。
 *
 *  典型用法：
 *      auto table = std::make_unique<ui::ActionTable>();
 *      table->SetRowHeight(kRowHeight);
 *      table->SetRowPainter(&MyScreen::PaintRowThunk, this);
 *      table->SetActionAreaWidth(kColActions);
 *      table->AddAction(_T("编辑"), theme::kColorInfo);
 *      table->AddAction(_T("删除"), theme::kColorDanger);
 *      table->SetActionCallback(&MyScreen::OnRowActionThunk, this);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include "Controls/List/DuiListBox.h"

#include <vector>

namespace flamingoadmin { namespace ui {

// 带行内文字操作链接的虚拟表格。
class ActionTable : public balloonwjui::DuiVirtualList
{
public:
    // 行内操作回调。
    //   user：SetActionCallback 时传入的指针。
    //   rowIndex：被点中的行，[0, GetRowCount())。
    //   actionIndex：被点中的操作，按 AddAction 的先后从 0 起编号。
    typedef void (*ActionFn)(void* user, int rowIndex, int actionIndex);

    // 链接是否显示的回调。
    //   user：SetActionVisibility 时传入的指针。rowIndex：行下标。actionIndex：操作下标。
    //   返回：该行的这个链接要显示时返回 true。
    typedef bool (*ActionVisibleFn)(void* user, int rowIndex, int actionIndex);

    ActionTable();

    // 析构时取消悬停提示的登记，免得 DuiToolTipMgr 留着指向已销毁控件的指针。
    ~ActionTable();

    // 注册绘行回调，代替基类的 SetPaintRowCallback（基类的那个已被本控件占用）。
    // 回调里只画行内容，操作列里的链接由本控件在回调之后补画。
    //   fn：绘行回调；为空时只画链接。user：原样传回，所有权归调用方。
    void SetRowPainter(PaintRowFn fn, void* user);

    // 设置操作列宽度：每行最右侧 width 像素为操作列，链接从操作列左边（加内边距）起依次排开。
    //   width：像素，小于 0 时按 0 处理。
    void SetActionAreaWidth(int width);

    // 追加一个操作链接。
    //   label：链接文字，如「编辑」。color：链接文字颜色。
    void AddAction(const CString& label, COLORREF color);

    // 清空全部操作链接。
    void ClearActions();

    // 操作链接个数。
    int ActionCount() const { return static_cast<int>(m_actions.size()); }

    // 注册行内操作回调。fn 为空表示不接收（点中链接没有任何效果）。
    //   user：原样传回，所有权归调用方。
    void SetActionCallback(ActionFn fn, void* user);

    // 注册「链接是否显示」回调。fn 为空表示每一行都显示全部链接（默认）。
    // 不显示的链接不画、不响应点击、鼠标停在那里也不显示手形光标；其余链接的位置不因此移动。
    //   user：原样传回，所有权归调用方。
    void SetActionVisibility(ActionVisibleFn fn, void* user);

    // 第 rowIndex 行的第 actionIndex 个链接当前是否显示（按回调判断；没有回调时为 true）。
    bool IsActionVisible(int rowIndex, int actionIndex) const;

    // 设置不显示的链接是否让出位置。默认 false：每个链接的位置固定，不显示的链接留出空位；
    // true：按行计算位置，不显示的链接不占位置，后面的链接依次向左靠拢。
    //   collapse：是否让出位置。
    void SetCollapseHiddenActions(bool collapse);

    // 设置覆盖提示文字并重绘。空串表示不显示覆盖层；显示覆盖层时链接不画、也不响应点击。
    void SetOverlayText(const CString& text);

    // 当前的覆盖提示文字。
    const CString& GetOverlayText() const { return m_overlayText; }

    // 命中测试：宿主客户区坐标 pt 落在第几行（已计入滚动）。
    //   返回：行下标；不在任何数据行上返回 -1。
    int RowFromPoint(POINT pt) const;

    // 命中测试：pt 落在哪一行的哪个操作链接上。
    //   rowIndex：[出参，可为 NULL] 命中时为行下标，未命中时为 -1。
    //   返回：操作下标；不在任何链接上（含显示覆盖层时）返回 -1。
    int ActionFromPoint(POINT pt, int* rowIndex) const;

    // 某行某个操作链接的矩形（宿主客户区坐标，已计入滚动），供命中测试与单元测试使用。
    //   rowIndex / actionIndex 越界时返回空矩形。
    RECT GetLinkRect(int rowIndex, int actionIndex) const;

    // 悬停提示的取文字回调：按鼠标位置返回要显示的提示文字，空串表示此处不显示提示。
    //   user：SetTooltipProvider 时传入的指针。pt：鼠标位置（宿主客户区坐标）。
    typedef CString (*TooltipFn)(void* user, POINT pt);

    // 注册悬停提示的取文字回调。鼠标在表格上移动、提示文字随位置变化时，本控件重新登记提示
    // （DuiToolTipMgr 按控件登记，同一控件内换行时宿主不会再触发悬停，所以由本控件自己处理）。
    // 显示覆盖提示时不显示悬停提示。
    //   fn：取文字回调，为空表示不显示悬停提示。user：原样传回，所有权归调用方。
    void SetTooltipProvider(TooltipFn fn, void* user);

    // 当前登记的悬停提示文字（单元测试用）；空串表示没有登记。
    const CString& CurrentTooltip() const { return m_tooltipText; }

    // 可见滚动条占用的宽度（像素）：滚动条隐藏时为 0。数据行画到控件右边减去这个宽度为止，
    // 表头应让出同样的宽度才能与数据行对齐（见 ColumnHeader::SetTrailingInset）。
    int ScrollBarWidth() const;

    // ---- DuiControl 覆写 ----
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;
    bool OnMouseMove(POINT pt, UINT mkFlags) override;

private:
    // 一个操作链接
    struct ActionLink
    {
        CString  m_label;   // 链接文字
        COLORREF m_color;   // 链接文字颜色
        int      m_width;   // 文字宽度（像素），追加时量好
    };

    // 基类绘行回调的转接：先调宿主的绘行回调，再补画本行的操作链接。
    static void PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect,
                              bool selected, bool hover);

    // 在一行的矩形里画出该行要显示的操作链接。
    void PaintActions(HDC hdc, int rowIndex, const RECT& rowRect) const;

    // 数据行的右边界：控件右边减去可见滚动条的宽度。
    int BodyRight() const;

    // 按行矩形算出第 rowIndex 行第 actionIndex 个链接的矩形（m_collapseHidden 为 true 时跳过该行不显示的链接）。
    RECT LinkRectInRow(const RECT& rowRect, int rowIndex, int actionIndex) const;

    PaintRowFn              m_rowPainter;      // 宿主的绘行回调，可为空
    void*                   m_rowPainterUser;  // 绘行回调的用户数据，所有权归调用方
    std::vector<ActionLink> m_actions;         // 操作链接，按显示顺序排列
    int                     m_actionAreaWidth; // 操作列宽度（像素）
    ActionFn                m_onAction;        // 行内操作回调，可为空
    void*                   m_onActionUser;    // 行内操作回调的用户数据，所有权归调用方
    ActionVisibleFn         m_actionVisible;   // 链接是否显示的回调，可为空（为空时全部显示）
    void*                   m_actionVisibleUser; // 链接是否显示回调的用户数据，所有权归调用方
    bool                    m_collapseHidden;  // 不显示的链接是否让出位置（默认 false，见 SetCollapseHiddenActions）
    CString                 m_overlayText;     // 覆盖提示文字，空串表示不显示
    TooltipFn               m_tooltipFn;       // 悬停提示的取文字回调，可为空
    void*                   m_tooltipUser;     // 悬停提示回调的用户数据，所有权归调用方
    CString                 m_tooltipText;     // 当前登记给 DuiToolTipMgr 的提示文字，空串表示没有登记
};

}} // namespace flamingoadmin::ui
