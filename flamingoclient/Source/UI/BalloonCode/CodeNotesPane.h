/**
 *  CodeNotesPane.h —— 代码窗口右侧的批注栏（docs/代码片段方案.md 第 10.5 节），发送代码窗口与代码查看窗共用。
 *
 *  两个控件：
 *    · CodeNotesSplitter：代码区与批注栏之间的左右分栏（balloonui 的 DuiSplitter 子类）。右栏（批注栏）保持固定宽度，
 *      窗口缩放时变的是代码区；拖动分隔条改变右栏宽度；收起后只排、只画左栏，代码区占满；
 *    · CodeNotesPane：批注栏本身（DuiVBox 子类）。顶部一行「批注 (N)」与收起用的「»」，下面是可以滚动的卡片列表，
 *      每张卡片写「第 3～4 行」（链接，点了在代码里选中这几行）与折行显示的说明；可编辑时卡片上另有「修改」「删除」。
 *      新增或修改时在对应位置插入一张编辑中的卡片（多行输入框加「确定」「取消」）。
 *  卡片的说明按批注栏当前宽度折行，高度在 Layout 里重算，所以拖宽拖窄、窗口缩放后卡片高度随之变化。
 *  追加批注（第 11 节）时同一栏里有多位作者的批注：设了作者名字表后卡片写出作者，这一轮新增或修改的写「新」；
 *  只有本人的批注（SetSelfId）卡片上有「修改」「删除」，别人的只读。
 *  只用 balloonui 现成控件组合与子类化，不改 balloonui。
 *
 *  点击通知：卡片上的链接、编辑卡片的按钮都以 DUIN_CLICK 发给窗口，控件编号由 CodeNotesIdToNote 反查是第几条批注、
 *  哪个部位；顶部「»」是 kCodeNotesIdCollapse。
 *
 *  典型用法（发送代码窗口）：
 *      std::unique_ptr<CodeNotesSplitter> split(new CodeNotesSplitter());
 *      split->SetPane(0, BuildCodeArea());
 *      split->SetPane(1, std::unique_ptr<DuiControl>(new CodeNotesPane()));
 *      ...
 *      pane->SetNotes(notes);
 *      split->SetCollapsed(notes.empty());
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <map>
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiSplitter.h"
#include "../../Common/Code/CodeNotes.h"

namespace balloonwjui { class DuiLabel; class DuiRichEdit; class DuiScrollView; }

namespace ballooncode {

// ---- 控件编号：取 1950 起，避开两个窗口自己的 101~1xx 与编辑框右键菜单的 11xx ----
const UINT kCodeNotesIdHeader     = 1950;   //顶部「批注 (N)」
const UINT kCodeNotesIdCollapse   = 1951;   //顶部右端的「»」：收起批注栏
const UINT kCodeNotesIdScroll     = 1952;   //卡片列表的滚动视图
const UINT kCodeNotesIdEditBox    = 1953;   //编辑中的卡片里的输入框
const UINT kCodeNotesIdEditOk     = 1954;   //编辑中的卡片的「确定」
const UINT kCodeNotesIdEditCancel = 1955;   //编辑中的卡片的「取消」
const UINT kCodeNotesIdEmpty      = 1956;   //没有批注时的提示文字
const UINT kCodeNotesIdCardBase   = 2000;   //第 i 张卡片上部位 p 的编号 = 基址 + i * kCodeNotesIdStride + p
const UINT kCodeNotesIdStride     = 6;      //每张卡片占的编号个数（与 CodeNotesPart 的个数相同）

/**
 *  卡片上的部位（控件编号里的 p）。
 */
enum CodeNotesPart
{
    kCodeNotesPartRange  = 0,   //「第 3～4 行」链接：点了在代码里选中这几行
    kCodeNotesPartEdit   = 1,   //「修改」链接（只读时没有）
    kCodeNotesPartDelete = 2,   //「删除」链接（只读时没有）
    kCodeNotesPartCard   = 3,   //卡片容器本身（高亮时改底色）
    kCodeNotesPartAuthor = 4,   //作者名（设了作者名字表、且作者不为 0 时才有，docs/代码片段方案.md 第 11.3 节）
    kCodeNotesPartNew    = 5,   //「新」标记（这一轮新增或修改的批注才有）
};

// ---- 尺寸（逻辑像素）----
const int kCodeNotesDefaultWidth = 240;   //批注栏默认宽度
const int kCodeNotesMinWidth     = 180;   //批注栏最窄
const int kCodeNotesSplitBarPx   = 5;     //分隔条粗细

/**
 *  由控件编号反查是第几条批注、哪个部位。
 *  @param id    控件编号
 *  @param index [出参] 批注下标（从 0 起，指 SetNotes 给出的顺序）
 *  @param part  [出参] 部位，取值见 CodeNotesPart
 *  @return 是卡片上的控件时返回 true
 */
bool CodeNotesIdToNote(UINT id, int& index, int& part);

/**
 *  展开或收起批注栏时调整窗口宽度（发送代码窗口与代码查看窗共用，docs/代码片段方案.md 第 10.5 节）：
 *  展开时，窗口没有最大化、最小化，且向右加宽后右边仍在显示器工作区内（窗口还没显示时只看宽度放不放得下），
 *  就把窗口加宽 grow 像素，代码区保持原宽；放不下时窗口不变，由代码区让出宽度。收起时缩回之前加宽的宽度。
 *  @param hwnd    窗口
 *  @param open    true 展开，false 收起
 *  @param grow    展开时要加宽的像素数（批注栏宽度加分隔条）
 *  @param grownPx [入参兼出参] 之前加宽的像素数；展开后为这次实际加宽的像素数（没能加宽时为 0），收起后为 0
 */
void ResizeWindowForNotesPane(HWND hwnd, bool open, int grow, int& grownPx);

/**
 *  代码区与批注栏之间的左右分栏：右栏保持固定宽度，可以收起。
 */
class CodeNotesSplitter : public balloonwjui::DuiSplitter
{
public:
    CodeNotesSplitter();

    /**
     *  设置右栏（批注栏）的宽度并重新排版。
     *  @param px 宽度（像素）；小于 kCodeNotesMinWidth 时按 kCodeNotesMinWidth
     */
    void SetRightWidth(int px);

    //右栏当前的宽度（像素）；收起时仍返回展开时的宽度
    int RightWidth() const;

    /**
     *  收起或展开右栏。收起后左栏占满、不画分隔条、右栏隐藏；调用方随后对本控件 ForceLayout。
     *  @param collapsed true 收起
     */
    void SetCollapsed(bool collapsed);

    //右栏是否收起
    bool IsCollapsed() const;

    //DuiSplitter 覆写：收起时左栏占满；展开时按右栏宽度反推左栏宽度后交给基类
    void Layout(const RECT& rcAvail) override;
    //DuiSplitter 覆写：收起时只画左栏，不画分隔条
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    //DuiSplitter 覆写：收起时命中只交给左栏
    balloonwjui::DuiControl* HitTest(POINT ptHostClient) override;
    //DuiSplitter 覆写：收起时不显示拖动光标
    bool OnSetCursor(POINT pt) override;
    //DuiSplitter 覆写：按在分隔条上时开始拖动（记下拖动状态，拖动期间 Layout 不按右栏宽度改位置）；收起时不接受
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    //DuiSplitter 覆写：拖完分隔条后记下新的右栏宽度
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

private:
    int  m_rightWidth;     //右栏宽度（像素）
    bool m_collapsed;      //右栏是否收起
    bool m_inLayout;       //正在 Layout 里调 SetSplitPx：它回调到 Layout 时直接交给基类，避免无限递归
    bool m_userDragging;   //正在拖动分隔条（基类的拖动状态是私有的，本类自己记录）
};

/**
 *  批注栏：顶部「批注 (N)」与「»」，下面是可滚动的卡片列表。
 */
class CodeNotesPane : public balloonwjui::DuiVBox
{
public:
    CodeNotesPane();

    /**
     *  只读模式（代码查看窗）：卡片上没有「修改」「删除」，也不能进入编辑。需在 SetNotes 之前调用。
     */
    void SetReadOnly(bool readOnly);

    /**
     *  设置本人 userid（追加批注时，docs/代码片段方案.md 第 11.3 节）：只有作者是本人或作者为 0 的批注，卡片上才有
     *  「修改」「删除」，也只有这些批注能进入修改。缺省为 0，此时只有作者为 0 的批注（本机正在编写的）可以修改。
     *  需在 SetNotes 之前调用。
     *  @param selfId 本人 userid
     */
    void SetSelfId(long long selfId);

    /**
     *  设置作者名字表：设了之后，作者不为 0 的批注卡片在行范围后面写作者名（表里没有的写 userid）；不设时不写作者。
     *  需在 SetNotes 之前调用。
     *  @param names userid 到显示名的对应
     */
    void SetAuthorNames(const std::map<long long, std::wstring>& names);

    /**
     *  第 index 条批注本人能否修改、删除：只读模式下都不能；否则按 codesnippet::CanEditCodeNote 判断。
     *  @param index 批注下标；越界时返回 false
     */
    bool CanEditNote(int index) const;

    /**
     *  设置可编辑时没有批注的提示文字（代码查看窗里入口叫「添加批注」，与发送代码窗口的「批注」按钮不同）。
     *  需在 SetNotes 之前调用；为空时用默认的提示。
     */
    void SetEmptyText(const std::wstring& text);

    /**
     *  显示一组批注（会结束正在进行的编辑）并重建卡片。
     *  @param notes 批注，按起始行升序（SanitizeCodeNotes 的结果）
     */
    void SetNotes(const std::vector<codesnippet::CodeNote>& notes);

    //当前显示的批注
    const std::vector<codesnippet::CodeNote>& Notes() const;

    /**
     *  进入编辑：在按起始行排定的位置插入一张编辑中的卡片（修改已有批注时代替那一张）。只读模式下、或要修改的
     *  批注本人不能修改（CanEditNote 为 false）时什么都不做。
     *  @param from  起始行（从 1 起）
     *  @param to    结束行（从 1 起，含）
     *  @param text  输入框里预填的说明
     *  @param index 修改第几条批注；新增时为 -1
     */
    void BeginEdit(int from, int to, const std::wstring& text, int index);

    //结束编辑（不保存），恢复为普通卡片
    void EndEdit();

    //是否正在编辑
    bool IsEditing() const;

    //正在修改第几条批注；新增或没有在编辑时为 -1
    int EditingIndex() const;

    //正在编辑的批注的起止行（从 1 起）；没有在编辑时为 0
    int EditingFrom() const;
    int EditingTo() const;

    //编辑框里当前的说明文字（换行为 \r）；没有在编辑时为空串
    std::wstring EditingText() const;

    //编辑框（借用，随卡片重建而失效）；没有在编辑时为 NULL
    balloonwjui::DuiRichEdit* EditBox() const;

    /**
     *  高亮第 index 张卡片（底色加深、加橙色边框），其余恢复。
     *  @param index 批注下标；-1 表示都不高亮
     */
    void SetHighlight(int index);

    //当前高亮的批注下标；没有时为 -1
    int Highlight() const;

    /**
     *  滚动卡片列表，让第 index 张卡片完整可见。须在排过版之后调用。
     */
    void ScrollToNote(int index);

    //DuiVBox 覆写：按当前宽度重算每张卡片说明文字的折行高度与列表总高度，再交给基类排版
    void Layout(const RECT& rcAvail) override;

private:
    /**
     *  一张卡片里需要随宽度调整的控件（借用指针，控件树持有）。
     */
    struct CardRefs
    {
        balloonwjui::DuiVBox*  m_card;     //卡片容器
        balloonwjui::DuiLabel* m_text;     //说明文字；编辑中的卡片为 NULL
        int                    m_fixedH;   //说明文字以外的固定高度（顶部一行、间距、内边距、编辑框与按钮行）

        CardRefs()
            : m_card(NULL)
            , m_text(NULL)
            , m_fixedH(0)
        {
        }
    };

    //重建卡片列表
    void Rebuild();

    //按列表宽度重算各卡片的高度与列表总高度
    void MeasureCards(int listWidth);

    //造一张普通卡片（第 index 条批注）
    std::unique_ptr<balloonwjui::DuiControl> MakeCard(int index, CardRefs& refs);

    //卡片上写的作者名：名字表里有就用它，没有时写 userid
    std::wstring AuthorName(long long authorId) const;

    //造一张编辑中的卡片
    std::unique_ptr<balloonwjui::DuiControl> MakeEditCard(CardRefs& refs);

    //刷新顶部「批注 (N)」
    void UpdateHeader();

private:
    bool m_readOnly;                                   //只读模式
    long long m_selfId;                                //本人 userid（SetSelfId）；0 表示没有设置
    std::map<long long, std::wstring> m_authorNames;   //作者名字表（SetAuthorNames）；为空时卡片上不写作者
    std::wstring m_emptyText;                          //可编辑时没有批注的提示文字（SetEmptyText）；为空时用默认的提示
    std::vector<codesnippet::CodeNote> m_notes;        //显示的批注
    bool m_editing;                                    //是否正在编辑
    int  m_editIndex;                                  //修改第几条；新增为 -1
    int  m_editFrom;                                   //编辑中的起始行（从 1 起）
    int  m_editTo;                                     //编辑中的结束行（从 1 起）
    std::wstring m_editText;                           //进入编辑时预填的说明
    int  m_highlight;                                  //高亮的批注下标；-1 表示没有
    int  m_lastListWidth;                              //最近一次排版时列表的宽度；-1 表示需要重算
    std::vector<CardRefs> m_cards;                     //各卡片（含编辑中的卡片），与列表中的顺序相同
    std::vector<int> m_cardNote;                       //各卡片对应的批注下标；编辑中的新卡片为 -1

    // ---- 控件（借用指针，控件树持有）----
    balloonwjui::DuiLabel*      m_pHeader;             //顶部「批注 (N)」
    balloonwjui::DuiScrollView* m_pScroll;             //卡片列表的滚动视图
    balloonwjui::DuiVBox*       m_pList;               //卡片列表（滚动视图的内容）
    balloonwjui::DuiRichEdit*   m_pEditBox;            //编辑中的输入框；没有在编辑时为 NULL
};

} // namespace ballooncode
