/**
 *  「消息盒子」窗口（DuiFrameWindow 外壳），BalloonAnnounceBoxWindow.h
 *
 *  平台公告的收件箱 —— 公告由企业管理控制台(flamingoAdmin)发布、经 chatserver 投递，
 *  落进本地库 CAnnounceStore，本窗口把它们列出来给用户看。详见
 *  docs/消息盒子接入方案.md。
 *
 *  入口只有一个：主面板 Banner 底部那一行（铃铛 + 「消息盒子」/「你有 N 条新公告」）。
 *
 *  左右双栏：左栏公告列表（置顶优先、时间倒序），右栏当前选中那条的正文。客户区控件
 *  树由 AnnounceBoxLayout 构造，数据直接从 CAnnounceStore 查。
 *
 *  交互采用局部刷新：选中行变化 / 收到新公告 / 收到撤回时只重建「列表 + 右栏」，
 *  不重建整窗。
 *
 *  <u>注意与群公告窗口区分</u>：那个属于某个具体的群（见 BalloonGroupManage），
 *  本窗口装的是平台级公告，两者互不相干。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo
 *  侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#pragma once

#include <vector>

#include "../../balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "AnnounceBoxLayout.h"                 // balloonannounce::AnnounceBoxViewData / AnnounceRowView
#include "../BalloonHistory/AnnounceStore.h"   // balloonhistory::CAnnounceStore

class CFlamingoClient;
class CBalloonMainPanel;

namespace balloonwjui { class DuiScrollView; }

//局部刷新延迟消息：点选某行会销毁正被点击的那个行控件（重建列表），故把刷新
//PostMessage 到事件解栈之后再做，避免在控件自身的事件回调里销毁它导致 use-after-free。
//这个坑历史窗那边踩过（见 WM_HISTORY_REFRESH_CONTENT 的注释），本窗照同一办法处理。
#define WM_ANNBOX_REFRESH_CONTENT   (WM_APP + 0x0139)

//正文高度补算消息：窗口尺寸变化后控件重排发生在 WM_SIZE 继续传递之后，故投递本消息
//等布局落定再按新宽度算正文高度（直接在 WM_SIZE 里算会拿到旧的控件矩形）。
#define WM_ANNBOX_SYNC_BODY_HEIGHT  (WM_APP + 0x013A)

/**
 *  CBalloonAnnounceBoxWindow —— 消息盒子窗口。
 *
 *  继承 balloonwjui::DuiFrameWindow，由它提供无边框自绘窗体与标题栏。非模态，
 *  由主面板创建并持有；关闭只销毁本窗，不影响主程序。
 */
class CBalloonAnnounceBoxWindow : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象（消息中枢），引用不持有；本窗用它取当前账号信息。
     */
    explicit CBalloonAnnounceBoxWindow(CFlamingoClient* pFMGClient);
    ~CBalloonAnnounceBoxWindow();

    /**
     *  打开（或前置）消息盒子窗口。主面板 OpenAnnounceBox 调用。
     *
     *  已打开则前置并刷新一遍（期间可能收到过新公告）；未打开则创建。
     *
     *    pMainPanel：主面板（收到已读变化后请它刷 Banner 角标；引用不持有，
     *                须保证生命周期覆盖本窗）。
     */
    void OpenBox(CBalloonMainPanel* pMainPanel);

    /**
     *  若本窗正开着，重查本地库并局部刷新（列表 + 右栏 + 顶部信息）。
     *  供主面板在收到新公告 / 撤回推送后调用，让变化即时反映到界面上。
     *  窗口没开时什么都不做。
     */
    void RefreshIfOpen();

    BEGIN_MSG_MAP(CBalloonAnnounceBoxWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_ANNBOX_REFRESH_CONTENT, OnRefreshContentMsg)
        MESSAGE_HANDLER(WM_ANNBOX_SYNC_BODY_HEIGHT, OnSyncBodyHeightMsg)
        //列表行的悬停要按几何位置自己判（理由见 AnnounceRow 的头注释：enter/leave
        //只发给最深的子控件，行容器收不到）。这两个处理器只<u>旁听</u>、不消费：
        //处理完置 bHandled=FALSE 让 DuiFrameWindow / DuiHost 照常继续处理，
        //否则整个界面的鼠标交互都会失灵。
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMoveMsg)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeaveMsg)
        //行的单击同理：点击落在行内的 Label 上，行容器收不到，故按落点自己判。
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUpMsg)
        //窗口尺寸变化后正文的折行宽度变了，要重算它的内容高度，否则拉宽窗口后
        //滚动范围还停在旧值（拉宽后本该变矮、却仍能滚出一大截空白）。
        //同样只旁听、不消费：DuiFrameWindow 还要靠它做客户区重排。
        MESSAGE_HANDLER(WM_SIZE, OnSizeMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //创建窗口、配置外观、构建客户区并显示。成功返回 true。
    bool BuildWindow();

    //全量构建客户区控件树（由当前数据生成），并捕获要复用的控件指针。
    void InitUi();

    //从本地公告库重查全部公告，填进 m_rows 并修正选中下标。
    void ReloadRows();

    //按当前 m_rows / m_selectedIndex 组装 AnnounceBoxViewData。
    void BuildViewData(balloonannounce::AnnounceBoxViewData& out) const;

    //局部刷新：重查本地库、换列表内容、更新右栏与顶部信息，不重建整窗。
    void RefreshContent();

    //选中第 index 行：更新选中态、右栏正文；该行若未读则标已读并上报服务端。
    //index 越界时忽略。
    void SelectRow(int index);

    //把全部未读公告标为已读并逐条上报服务端。
    void MarkAllRead();

    //WM_CLOSE：销毁本窗（不影响主程序）。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DUI_NOTIFY：接收客户区控件冒上来的通知（当前只处理「全部标为已读」按钮）。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //延迟局部刷新消息的处理。
    LRESULT OnRefreshContentMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_MOUSEMOVE 旁听：按落点更新列表里哪一行处于悬停态。恒置 bHandled=FALSE。
    LRESULT OnMouseMoveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_MOUSELEAVE 旁听：鼠标离开窗口时把悬停态全撤掉。恒置 bHandled=FALSE。
    LRESULT OnMouseLeaveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_LBUTTONUP 旁听：按落点判定点中了哪一行并选中它。恒置 bHandled=FALSE。
    LRESULT OnLButtonUpMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_SIZE 旁听：窗口尺寸变了，投递一条延迟消息等布局落定后重算正文高度。恒置 bHandled=FALSE。
    LRESULT OnSizeMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //正文高度补算消息的处理（见 WM_ANNBOX_SYNC_BODY_HEIGHT 的说明）。
    LRESULT OnSyncBodyHeightMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  按右栏当前实际宽度算出正文折行后的高度，喂给正文滚动视口。
     *
     *  正文是一个多行 Label，它折行后有多高只有拿到<u>实际宽度</u>才能算（
     *  DuiLabel::MeasureHeight）。滚动视口的内容高度就是这个值——不设的话视口
     *  以为内容只有一屏高，长公告滚不下去。
     *
     *  须在布局完成之后调用（此时控件的 GetRect 才有效）：建窗后、每次局部刷新后、
     *  以及窗口尺寸变化后各调一次。控件尚未就绪时静默返回。
     */
    void SyncDetailBodyHeight();

    //把列表里包含 pt（客户区坐标）的那一行置为悬停、其余撤销。
    //pt 传 (-1,-1) 表示"鼠标不在列表里"，即全部撤销。
    void UpdateRowHover(POINT pt);

    //返回列表里包含 pt（客户区坐标）的那一行的下标；不在任何行内时返回 -1。
    int  HitTestRow(POINT pt) const;

private:
    CFlamingoClient*   m_lpFMGClient;   //所属客户端（消息中枢），引用不持有
    CBalloonMainPanel* m_pMainPanel;    //主面板（刷 Banner 角标用），引用不持有

    //当前渲染的全部公告行（从本地库查得，已按展示顺序排好）。
    std::vector<balloonannounce::AnnounceRowView> m_rows;

    //与 m_rows 一一对应的正文（右栏显示用）。单独存一份而不是塞进 AnnounceRowView：
    //行视图是给<u>列表</u>用的，正文可能很长，塞进去会让每次重建列表都拷一大堆用不上的文本。
    std::vector<CString> m_bodies;

    //当前选中行下标；-1 表示没有选中（右栏显示占位提示）。
    int m_selectedIndex;

    //列表滚动视口（局部刷新时按 id 取回、SetContent 换列表）。窗口未建时为 NULL。
    balloonwjui::DuiScrollView* m_pScroll;

    //标题栏左侧图标位图；本窗持有，析构时释放。
    HBITMAP m_hTitleIcon;
};
