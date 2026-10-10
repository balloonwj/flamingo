/**
 *  录制管理窗口的录像列表，RecordManagerList.h
 *
 *  在 balloonui 的 DuiListBox 上做两项扩展（开发约定文档「UI 约定」第 2 条的「子类化扩展」，docs/屏幕共享与录制方案.md
 *  第 11.6 节 F9）：
 *    - 右键：选中鼠标下的那一行，并发 kNotifyContextMenu 通知宿主窗口弹出右键菜单；
 *    - 列表里原地重命名：把一个 DuiEdit 作为子控件盖在行的文字上（DuiListBox 只认得它自己的滚动条，本类补上输入框的
 *      命中与绘制）；编辑中列表被单击或滚动时发 kNotifyEditInterrupted，宿主据此提交编辑，避免输入框停在错位的地方。
 *  行的绘制、选中、双击、滚动都沿用 DuiListBox。
 *  录制管理「云端」页（第 27 期 27d）另用到一项扩展：每行右侧的状态列（文字、颜色与可选的进度条），由宿主经 SetTrailing
 *  给出。DuiListBox 的行只有图标、主文字与副文字，本类在它画完之后，按行当前的底色（普通、悬停、选中）把状态列那一块重新铺底，
 *  再画状态文字与进度条；状态列宽度为 0（本机页）时不画。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法：
 *      CRecordManagerList* list = ...;
 *      balloonwjui::DuiEdit* edit = list->BeginEdit(rc, L"周会录像", kIdRenameEdit);
 *      ...（宿主收到 DUIN_EDIT_ENTER 后）
 *      list->EndEdit();
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <map>
#include <string>
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiListBox.h"

namespace balloonwjui
{
    class DuiEdit;
}

namespace screenrecord {

//录制管理窗口的录像列表
class CRecordManagerList : public balloonwjui::DuiListBox
{
public:
    //本控件的自定义通知码：取 DUIN_CUSTOM + 120 起，避开 DuiListBox 的 +1~+3 与 DuiEdit 的 +80 段（UI 约定第 4 条）
    enum NotifyCode
    {
        //右键按下：已选中鼠标下的那一行，extra = 行号；宿主应在当前鼠标事件处理完之后再弹出菜单
        kNotifyContextMenu = balloonwjui::DUIN_CUSTOM + 120,
        //原地编辑中列表被单击或滚动了：宿主应提交编辑（输入框的位置已与行不再对应）
        kNotifyEditInterrupted = balloonwjui::DUIN_CUSTOM + 121,
    };

    CRecordManagerList();
    ~CRecordManagerList();

    /**
     *  开始原地编辑：在 rc 处显示输入框，预填 text、全选并取得焦点。已在编辑时先结束上一次（不发通知）。
     *  @param rc         输入框的矩形（宿主窗口客户区坐标，见 RecordInlineEditRect）
     *  @param text       预填的文字
     *  @param editCtrlId 输入框的控件 id，宿主据此认出它发来的回车、Esc 与失焦通知
     *  @return 输入框（所有权归本控件，结束编辑后隐藏、下次编辑复用）；创建失败时返回 NULL
     */
    balloonwjui::DuiEdit* BeginEdit(const RECT& rc, const std::wstring& text, UINT editCtrlId);

    //结束原地编辑：隐藏输入框，输入框有焦点时把焦点交还给列表。没在编辑时什么都不做
    void EndEdit();

    //是否正在原地编辑
    bool IsEditing() const;

    //正在编辑时输入框里的文字；没在编辑时为空
    std::wstring EditText() const;

    //行号：鼠标位置（宿主窗口客户区坐标）落在哪一行；不在任何一行上时返回 -1
    int RowFromPoint(POINT pt) const;

    /**
     *  状态列的宽度（像素）；0 表示不画状态列（默认）。
     */
    void SetTrailingWidth(int px);

    /**
     *  设置一行的状态列。
     *  @param index    行号
     *  @param text     状态文字
     *  @param color    文字颜色（进度条的填充色与它相同）
     *  @param progress 进度 0～100；-1 表示不画进度条
     */
    void SetTrailing(int index, const std::wstring& text, COLORREF color, int progress);

    //清掉全部行的状态列（重建列表前调用）
    void ClearTrailing();

    //一行的状态文字（单元测试与宿主核对用）；没有设置时为空
    std::wstring TrailingText(int index) const;

    //DuiControl 覆写：输入框可见时把落在它上面的鼠标交给它
    virtual balloonwjui::DuiControl* HitTest(POINT ptHostClient) override;
    //DuiControl 覆写：先画列表，再画输入框；编辑中发现滚动位置变了时通知宿主提交编辑
    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override;
    //DuiControl 覆写：编辑中单击列表时先通知宿主提交编辑，再按 DuiListBox 处理
    virtual bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    //DuiControl 覆写：右键选中鼠标下的那一行并通知宿主弹菜单
    virtual bool OnRButtonDown(POINT pt, UINT mkFlags) override;
    //DuiControl 覆写：记下鼠标所在的行（状态列按悬停的底色铺底），再按 DuiListBox 处理
    virtual bool OnMouseMove(POINT pt, UINT mkFlags) override;
    virtual bool OnMouseLeave() override;

private:
    //一行的状态列
    struct Trailing
    {
        std::wstring m_text;      //状态文字
        COLORREF     m_color;     //文字与进度条的颜色
        int          m_progress;  //进度 0～100；-1 不画进度条

        Trailing();
    };

    //画状态列（DuiListBox 画完之后）
    void paintTrailing(HDC hdc, const RECT& rcDirty);

    balloonwjui::DuiEdit* m_edit;             //原地编辑的输入框；所有权在 m_children，第一次 BeginEdit 时创建，之后复用
    bool                  m_editing;          //是否正在原地编辑
    int                   m_editScrollPos;    //开始编辑时列表的滚动位置（像素）；绘制时发现与当前不同就说明滚动了
    bool                  m_interruptSent;    //这一次编辑是否已发过 kNotifyEditInterrupted，避免重复通知
    int                   m_trailingW;        //状态列宽度（像素）；0 表示不画
    std::map<int, Trailing> m_trailing;       //行号 → 状态列；重建列表时由宿主 ClearTrailing
    int                   m_hoverRow;         //鼠标所在的行；不在任何一行上时为 -1
};

} // namespace screenrecord
