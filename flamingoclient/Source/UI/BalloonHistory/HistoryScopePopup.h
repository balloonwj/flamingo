/**
 *  HistoryScopePopup.h
 *
 *  聊天记录管理器左栏「范围」按钮弹出的勾选列表：上面一个搜索框，中间是全部会话的勾选列表（复选框 +
 *  头像 + 名字，形式与转发目标选择窗的「最近会话」页相同），底部是「已选 N 个」与「全部会话」「确定」
 *  两个按钮。子类化 balloonui 的 DuiPopupHost（无边框 WS_POPUP 浮层，自带 ESC / 失焦自动关闭）。
 *
 *  提交时机：
 *    · 点「确定」       → 以勾选的会话提交（一个都没勾时等同于「全部会话」）。
 *    · 点「全部会话」   → 以空集合提交，即不限范围。
 *    · ESC、点弹层外面 → 关闭，不提交，范围保持原样。
 *  勾选状态只在弹层里改动，提交之前不影响左栏。
 *
 *  方案见 docs/聊天记录管理器方案.md §5.2。内容树的骨架由 HistoryWindowLayout 的
 *  BuildHistoryScopePopupContent 构造（有单元测试），本类负责填行、记录勾选、回调宿主。
 *
 *  典型用法（消息记录窗口）：
 *      m_scopePopup.SetSubmitCallback(&OnScopePopupThunk, this);
 *      m_scopePopup.OpenBelow(buttonScreenRect, entries, m_mgrScope);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiPopupHost.h"
#include "V2MsgQuery.h"   // HistoryConversationKey

namespace balloonwjui { class DuiScrollView; class DuiLabel; class DuiButton; }

namespace balloonhistory {

/**
 *  CHistoryScopePopup —— 聊天记录管理器的搜索范围弹层。
 */
class CHistoryScopePopup : public balloonwjui::DuiPopupHost
{
public:
    //弹层列表里的一个会话
    struct Entry
    {
        HistoryConversationKey m_key;     //会话
        std::wstring           m_name;    //显示名（与左栏相同）
        HBITMAP                m_avatar;  //头像位图，借用不持有（归主面板头像缓存所有）；NULL 时画名字首字

        Entry()
            : m_avatar(NULL)
        {
        }
    };

    // 提交回调：用户点了「确定」或「全部会话」时触发一次，随后弹层关闭。
    //   userdata：注册时传入的不透明指针，本类只透传、不解释。
    //   keys：    勾选的会话，已去重并按 (ctype, cid) 升序；为空表示不限范围。
    typedef void (*SubmitCallback)(void* userdata, const std::vector<HistoryConversationKey>& keys);

    CHistoryScopePopup();
    ~CHistoryScopePopup();

    /**
     *  注册提交回调。
     *    cb：回调函数指针；NULL 表示取消注册。
     *    userdata：回调时原样带回的不透明指针；本类不持有其生命周期，调用方须保证它活得
     *              比本弹层久（一般传宿主窗口 this）。
     */
    void SetSubmitCallback(SubmitCallback cb, void* userdata);

    /**
     *  在锚点矩形下方打开弹层（左缘与锚点左缘对齐；下方放不下时 DuiPopupHost 自动翻到上方）。
     *    anchorScreen：锚点矩形（屏幕坐标），一般是左栏的「范围」按钮。
     *    entries：     列出的会话，按给定顺序显示（一般就是左栏的顺序）。复制一份保存。
     *    selected：    已勾选的会话；不在 entries 里的项保留在勾选集合里，提交时一并交回。
     */
    void OpenBelow(const RECT& anchorScreen, const std::vector<Entry>& entries,
                   const std::vector<HistoryConversationKey>& selected);

    BEGIN_MSG_MAP(CHistoryScopePopup)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnChildNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiPopupHost)
    END_MSG_MAP()

private:
    /**
     *  内容树里的控件发来的 DUI 通知：搜索框文字变化、复选框单击、两个按钮单击。判定时通知码与
     *  控件编号一起比对（开发约定文档 UI 约定第 4 条）。
     *    lParam：balloonwjui::DuiNotify 的地址，仅在本次同步调用期间有效。
     *    bHandled：出参，未匹配时置 FALSE 让消息继续走基类的处理链。
     *    返回：恒为 0。
     */
    LRESULT OnChildNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 首次打开时构建一次内容树，并取回列表滚动视口与「已选 N 个」标签的借用指针。重复调用无副作用。
    void BuildContentOnce();

    // 按搜索框当前的文字重建列表各行（复选框的勾选状态取自 m_selected）。
    void RebuildRows();

    // 把底部「已选 N 个」改成当前勾选的个数。
    void UpdateSelectedInfo();

    // 以 keys 回调宿主并关闭弹层。一次打开只提交一次（m_committed 守卫）。
    void DoSubmit(const std::vector<HistoryConversationKey>& keys);

private:
    std::vector<Entry>                  m_entries;     // 列出的会话（OpenBelow 时复制）
    std::vector<HistoryConversationKey> m_selected;    // 当前勾选的会话，已去重排序
    std::wstring                        m_query;       // 搜索框当前的文字（已去首尾空白）
    std::map<UINT, size_t>              m_rowByCheckId;// 复选框编号 → m_entries 下标；RebuildRows 重建
    balloonwjui::DuiScrollView*         m_scroll;      // 列表滚动视口（借用，所有权在内容树里）
    balloonwjui::DuiLabel*              m_info;        // 底部「已选 N 个」（借用，同上）
    bool                                m_built;       // 内容树是否已构建（只建一次）
    bool                                m_committed;   // 本次打开是否已经提交过
    SubmitCallback                      m_submitCb;    // 提交回调；NULL 表示未注册
    void*                               m_submitUd;    // 提交回调的用户数据，本类只透传
};

} // namespace balloonhistory
