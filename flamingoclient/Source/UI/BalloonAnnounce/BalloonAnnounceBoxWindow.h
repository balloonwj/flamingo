/**
 *  「公告中心」窗口（DuiFrameWindow 外壳），BalloonAnnounceBoxWindow.h
 *
 *  平台公告的收件箱 —— 公告由企业管理控制台(flamingoAdmin)发布、经 chatserver 投递，
 *  落进本地库 CAnnounceStore，本窗口把它们列出来给用户看。详见
 *  docs/公告中心接入方案.md。
 *
 *  入口只有一个：主面板 Banner 底部那一行（铃铛 + 「公告中心」/「你有 N 条新公告」）。
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

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "AnnounceBoxLayout.h"                 // balloonannounce::AnnounceBoxViewData / AnnounceRowView
#include "../BalloonHistory/AnnounceStore.h"   // balloonhistory::CAnnounceStore
#include "../../App/CustomMsgDef.h"                   // WM_CHAT_OPEN_LINK（消息映射里要用）
#include "../BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：任务栏与 Alt+Tab 的程序图标

class CFlamingoClient;
class CBalloonMainPanel;

namespace balloonmain { class MainPanelScrollView; }

//整窗重建的延迟消息：重建会销毁现有控件，若在控件自己的事件回调里做就会 use-after-free，
//故把它 PostMessage 到事件解栈之后再做。这个坑历史窗那边踩过（见 WM_HISTORY_REFRESH_CONTENT
//的注释），本窗照同一办法处理。
//
//如今只有 RefreshIfOpen（收到新公告 / 撤回推送）会投递本消息 —— 那种场景下公告的集合确实
//变了，非重建不可。点选某行、标记已读都改走定点更新（ApplyStateToUi），不销毁任何控件，
//也就不需要延迟。
#define WM_ANNBOX_REFRESH_CONTENT   (WM_APP + 0x0139)

//正文高度补算消息：窗口尺寸变化后控件重排发生在 WM_SIZE 继续传递之后，故投递本消息
//等布局落定再按新宽度算正文高度（直接在 WM_SIZE 里算会拿到旧的控件矩形）。
#define WM_ANNBOX_SYNC_BODY_HEIGHT  (WM_APP + 0x013A)

/**
 *  CBalloonAnnounceBoxWindow —— 公告中心窗口。
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
     *  打开（或前置）公告中心窗口。主面板 OpenAnnounceBox 调用。
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
        MESSAGE_HANDLER(WM_CHAT_OPEN_LINK, OnChatOpenLinkMsg)
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

    /**
     *  重查本地库并整棵重建客户区控件树。
     *
     *  只用于"公告的集合变了"的场景（收到新公告、公告被撤回）—— 那要新增 / 删除行控件，
     *  绕不开重建。单纯的选中变化与已读变化走 ApplyStateToUi 定点更新，不到这里来。
     *
     *  重建会把列表的滚动位置清零，故本方法会先记下再恢复（超出新范围时自动夹回）。
     */
    void RefreshContent();

    /**
     *  把当前的选中态 / 已读态落到<u>既有</u>控件上，不重建控件树。
     *
     *  点选一条公告、把公告标为已读时走这条路：公告的集合没变，重建整棵树纯属浪费，
     *  还会让列表跳回顶部。
     *
     *    refreshDetail：true 时连右栏三项一起换成当前选中的那条（选中行确实变了才需要，
     *                   仅仅是已读状态变了的话右栏内容并没有变）。
     */
    void ApplyStateToUi(bool refreshDetail);

    //选中第 index 行：更新选中态、右栏正文；该行若未读则标已读并上报服务端。
    //index 越界、或点的还是当前这条且它早已读过（界面不会有任何变化）时忽略。
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
     *  WM_CHAT_OPEN_LINK 处理：右栏正文里的超链接被点击。lParam 为堆上的
     *  std::wstring*（规范化后的 URL），本函数负责 delete。打开前先过
     *  balloonchatwnd::IsSafeLinkUrl 的协议白名单 —— 只放行 http / https。
     */
    LRESULT OnChatOpenLinkMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  按右栏当前实际宽度算出正文折行后的高度，喂给正文滚动视口。
     *
     *  取出正文视口与正文控件后转交 balloonannounce::SyncAnnounceDetailBodyHeight，
     *  算法与"为什么设完高度还要重排一次"都写在那边。
     *
     *  须在布局完成之后调用（此时控件的 GetRect 才有效）：建窗后、每次局部刷新后、
     *  以及窗口尺寸变化后各调一次。控件尚未就绪时静默返回。
     */
    void SyncDetailBodyHeight();

    /**
     *  按当前窗口边框与 DPI，告诉两个滚动视口右侧要给窗口缩放边框让出多少像素。
     *
     *  滚动条是悬浮式的、贴着视口右缘画，视口右缘若落在 DuiFrameWindow 的缩放边框
     *  里，鼠标靠近就变成左右双向箭头、滑块根本抓不住（原委见 OverlayScrollBarGeometry.h）。
     *  本窗右栏视口离客户区右缘还有一段内边距，通常已经躲开了边框，故这里按"边框
     *  宽度减去这段距离"算出真正还需要让的量，避免无谓地把滚动条往里推。
     *
     *  须在布局落定之后调用（建窗后、窗口尺寸 / DPI 变化后）。控件尚未就绪时静默返回。
     */
    void SyncScrollBarEdgeInset();

    /**
     *  判断某个点是不是落在左栏列表那条悬浮滚动条的命中带上。
     *
     *  本窗的行悬停与行点击是按几何位置自己判的（不走 DUI 事件派发，理由见
     *  AnnounceRow 的头注释），而悬浮滚动条正压在列表上层。不把命中带排掉的话，
     *  拖完滑块一松手就会顺带选中滑块底下那条公告。
     *
     *    pt：客户区坐标。
     *  @return 落在命中带上返回 true；列表不需要滚动条、或视口尚未就绪时返回 false。
     */
    bool PointOnListScrollBar(POINT pt) const;

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
    balloonmain::MainPanelScrollView* m_pScroll;

    //标题栏左侧图标位图；本窗持有，析构时释放。
    HBITMAP m_hTitleIcon;
    //任务栏与 Alt+Tab 的程序图标（2026-10-07，UI 约定第 10 条：没有所有者、会出现在任务栏的窗口另设任务栏图标）；本对象持有，析构时释放
    balloonmain::ProgramWindowIcons m_taskIcons;
};
