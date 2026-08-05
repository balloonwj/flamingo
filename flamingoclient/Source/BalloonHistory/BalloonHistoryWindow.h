/**
 *  历史消息记录窗口（DuiFrameWindow 外壳），BalloonHistoryWindow.h
 *
 *  新版“消息记录”窗口 —— 基于 balloonui，对照 design/历史消息记录窗口1-3.png。
 *  透明纤细标题栏（只保留关闭按钮）+ 客户区（标题、搜索框、类型筛选胶囊、时间
 *  下拉、消息列表、底部分页 + 导出）。客户区控件树由 HistoryWindowLayout 构造，
 *  数据经 HistoryModel 从本地库 CV2MsgStore 查询。
 *
 *  交互采用局部刷新：改类型胶囊 / 时间档 / 关键字 / 翻页时只重建“列表 + 分页器”
 *  并更新底部信息与胶囊选中态，**不重建搜索框**（保住正在输入的焦点）。
 *
 *  调试期由 CMainDlg 的 --history 命令行入口直接弹出：用一个 in-memory 预置本地库
 *  （SeedDebugStore）灌入若干假消息，使筛选 / 翻页 / 搜索都能真点真用、便于目检。
 *  接真实会话数据（取主面板本地库 + 联系人昵称解析）与两个入口留待 2c-2。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo
 *  侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <map>
#include <set>
#include <string>

#include "../../balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "HistoryWindowLayout.h"   // balloonhistory::HistoryViewData / HistoryRowView
#include "HistoryModel.h"          // balloonhistory::HistoryModel
#include "HistoryRowMenu.h"        // balloonhistory::HistoryRowMenuContext（成员函数返回它）
#include "V2MsgStore.h"            // balloonhistory::CV2MsgStore

class CFlamingoClient;
class CBalloonMainPanel;

namespace balloonwjui { class DuiScrollView; }

//局部刷新延迟消息：翻页等交互会销毁正被点击的控件（重建分页器 / 列表），故把刷新
//PostMessage 到事件解栈之后再做，避免在控件自身事件回调里销毁它导致 use-after-free。
#define WM_HISTORY_REFRESH_CONTENT  (WM_APP + 0x0137)

//行右键菜单延迟弹出消息：TrackPopupMenu 是个模态消息循环，直接在 WM_RBUTTONUP 的
//处理里跑会与 DuiHost 的鼠标捕获打架（菜单收起后界面点击失灵），故投递本消息、
//延后到鼠标事件解栈之后再弹。wParam=当前页行下标，lParam=菜单落点（屏幕坐标，
//MAKELPARAM 打包；坐标可能为负，取出时须按有符号还原）。
#define WM_HISTORY_SHOW_ROW_MENU    (WM_APP + 0x0138)

//历史消息记录窗口。继承 balloonwjui::DuiFrameWindow，由它提供无边框自绘窗体与
//标题栏（本窗只保留关闭按钮）。窗口可缩放，设最小尺寸。
class CBalloonHistoryWindow : public balloonwjui::DuiFrameWindow
{
public:
    //构造函数。
    //  pFMGClient：所属客户端对象（消息中枢），引用不持有；2c-2 接真实数据时用其本地库 / 联系人缓存。
    explicit CBalloonHistoryWindow(CFlamingoClient* pFMGClient);
    ~CBalloonHistoryWindow();

    //创建并显示历史窗（调试入口 --history 用，非模态、无父窗口）。内部预置一个 in-memory
    //本地库灌假消息，使筛选 / 翻页 / 搜索可真点真用。
    //  hMainDlg：CMainDlg 的窗口句柄（隐藏的消息中枢）；关闭本窗口时据此通知其收尾。
    void CreateAndShow(HWND hMainDlg);

    //为某个真实会话打开 / 重定向历史窗（主面板 OpenHistory 调用）。用主面板的本地库
    //取数、用主面板解析发送者昵称；关闭只销毁本窗、不退出程序。已开则重定向到新会话
    //并刷新置顶。须在调用前由主面板备好本地库。
    //  pMainPanel：主面板（解析昵称用，引用不持有，须覆盖本窗生命周期）。
    //  pStore：    本地消息库（引用不持有）。
    //  ctype：     会话类型（1单聊 2群聊）。
    //  cid：       会话ID（我方视角）。
    //  titleName： 对话名（标题“消息记录 · <名>”用）。
    //  initialTypeFilter：打开后预选哪个类型胶囊，取 balloonhistory::HistoryTypeFilter
    //                     的值。默认 0（kHistoryTypeAll）即“全部”，与老行为一致；聊天窗
    //                     顶部的「已 Pin N 条」提示带传 6（kHistoryTypePinned），让窗口
    //                     直接开在 Pin 分类上。越界取值一律按“全部”处理。
    void OpenForConversation(CBalloonMainPanel* pMainPanel,
                             balloonhistory::CV2MsgStore* pStore,
                             int ctype, int64_t cid, LPCTSTR titleName,
                             int initialTypeFilter = 0);

    //若本窗正打开且显示的就是 (ctype,cid) 这个会话，则局部刷新（重查本地库）。供主面板
    //在回填同步落库后调用，让新拉到的消息即时出现；会话不匹配 / 未开窗则忽略。
    void RefreshIfConversation(int ctype, int64_t cid);

    /**
     *  一笔由本窗口发起的图片下载有结果了（主面板收到 FMG_MSG_V2_IMAGE_DOWNLOAD_RESULT
     *  后路由进来）。
     *
     *  本窗口的图片下载只在用户<u>明确双击缩略图 / 点了菜单里的「下载」</u>时才发起
     *  （消息记录窗是轻量浏览界面，翻页绝不批量拉），故这里的收尾也很直接：成功就刷新
     *  该行的缩略图，若这笔下载正是"双击要看大图"引起的，还顺带把看图窗打开；失败弹提示。
     *    msgid：    该图片消息的服务器 msgid。本窗口没发起过这一笔时安静忽略。
     *    ok：       是否下载成功。
     *    localPath：成功时原图落地的本地完整路径；失败时无意义。
     *  副作用：可能弹出看图窗或提示框，并触发列表局部刷新。
     */
    void OnImageDownloadFinished(int64_t msgid, bool ok, const std::wstring& localPath);

    /**
     *  一笔由本窗口发起的文件下载有结果了（主面板收到 FMG_MSG_V2_FILE_DOWNLOAD_RESULT
     *  后路由进来）。
     *    msgid：    该文件消息的服务器 msgid。本窗口没发起过这一笔时安静忽略。
     *    ok：       是否下载成功。
     *    localPath：成功时文件落地的本地完整路径；失败时无意义。
     *    cancelled：是否为用户主动取消（取消不算失败，<u>不弹提示</u>）。
     *  副作用：失败时弹提示框；无论成败都触发列表局部刷新（菜单项要跟着变）。
     */
    void OnFileDownloadFinished(int64_t msgid, bool ok, const std::wstring& localPath,
                                bool cancelled);

    BEGIN_MSG_MAP(CBalloonHistoryWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_HISTORY_REFRESH_CONTENT, OnRefreshContentMsg)
        //列表行的悬停要按几何位置自己判（理由见 HistoryRow::SetHovered）。这两个
        //处理器只<u>旁听</u>、不消费：处理完置 bHandled=FALSE 让 DuiFrameWindow /
        //DuiHost 照常继续处理，否则整个界面的鼠标交互都会失灵。
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMoveMsg)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeaveMsg)
        //行右键菜单同样按几何位置自己判（理由与悬停一致：DuiHost 只把右键发给最深的
        //那个控件，行容器收不到）。本处理器只<u>旁听</u>、不消费。
        MESSAGE_HANDLER(WM_RBUTTONUP, OnRButtonUpMsg)
        MESSAGE_HANDLER(WM_HISTORY_SHOW_ROW_MENU, OnShowRowMenuMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //创建窗口、配置外观、构建客户区并显示。成功返回 true。
    bool BuildWindow(HWND hWndParent);

    //全量构建客户区控件树（由当前模型状态生成），捕获要复用的控件指针，就绪搜索框。
    void InitUi();

    //局部刷新：按当前模型重查本地库，只换列表内容 + 重填分页器 + 更新底部信息与胶囊
    //选中态与标题副行；不动搜索框（保住焦点）。
    void RefreshContent();

    //WM_MOUSEMOVE 旁听：按落点更新列表里哪一行处于悬停态。恒置 bHandled=FALSE。
    LRESULT OnMouseMoveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_MOUSELEAVE 旁听：鼠标离开窗口时把悬停态全撤掉。恒置 bHandled=FALSE。
    LRESULT OnMouseLeaveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //把列表里包含 pt（客户区坐标）的那一行置为悬停、其余撤销。pt 传 (-1,-1)
    //表示"鼠标不在列表里"，即全部撤销。
    void UpdateRowHover(POINT pt);

    //定位到当前页第 row 行那条消息：跳到聊天窗并滚过去。供行内「定位」按钮与
    //"双击整行"两条路共用 —— 两者认的是同一个行下标。
    //  row：当前页的行下标（从0起）；越界、或调试入口下没有主面板时直接忽略。
    void LocateRow(int row);

    //跳到指定页并局部刷新。供底部「跳至第 N 页」微调框的 ▲▼ 与手输入提交共用。
    //  page：目标页号（从1起）。<=0 视为"用户没真的输入"，直接忽略；越界由模型
    //        自己夹到 [1, PageCount]。与当前页相同时不做无谓的重建。
    void JumpToPage(int page);

    //把当前模型状态翻译成整窗渲染数据（标题 / 总数 / 当前页 / 各行 / 选中胶囊与时间档）。
    balloonhistory::HistoryViewData BuildViewData();

    //把当前筛选条件下的**全部**消息（非仅当前页）导出为 .txt：另存对话框选路径，按
    //“[时间] 昵称：内容”时间正序写 UTF-8（带 BOM）文本。无匹配时提示。
    void ExportToTxt();

    //显示 / 隐藏“自定义时间区间”行（时间下拉选“自定义”时显示），并强制重排客户区。
    void SetCustomDateVisible(bool show);

    //把“自定义”起止日期下拉默认选成“本月 1 日 → 今天”（首次切到自定义时调用）。
    void DefaultCustomSelections();

    //读 6 个日期下拉算出起止时间戳，写进模型（SetCustomRange，含 range=自定义）。
    void ApplyCustomRangeFromCombos();

    //预置 in-memory 调试本地库并灌入若干假消息（调试入口用）。
    void SeedDebugStore();

    //解析发送者昵称（调试期查 m_senderNames；查不到回退“用户<id>”）。
    CString ResolveSenderName(int64_t fromid) const;

    //WM_CLOSE：调试入口下本窗口是唯一可见窗口，关闭即退出程序，转交 CMainDlg。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DUI_NOTIFY：路由类型胶囊 / 时间下拉 / 搜索框 / 分页按钮的交互到模型，改完模型后
    //PostMessage(WM_HISTORY_REFRESH_CONTENT) 延迟刷新。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_HISTORY_REFRESH_CONTENT：事件解栈后执行局部刷新（见 RefreshContent）。
    LRESULT OnRefreshContentMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_RBUTTONUP 旁听：按落点算出点在哪一行，投递 WM_HISTORY_SHOW_ROW_MENU 延迟弹菜单。
    //恒置 bHandled=FALSE（理由同悬停那两个处理器）。
    LRESULT OnRButtonUpMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_HISTORY_SHOW_ROW_MENU：事件解栈后真正弹出行右键菜单并分发选中项。
    LRESULT OnShowRowMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // ---- 行右键菜单：命中、上下文、以及各菜单项的处理 ----

    //按落点（客户区坐标）算出点在当前页的第几行。
    //@param pt  客户区坐标
    //@return 行下标（从0起）；没落在任何一行上、或落在滚动视口之外时返回 -1
    int RowIndexAtPoint(POINT pt) const;

    //取第 row 行那条消息在本地的可用文件路径。
    //
    //图片按内容 MD5 换算缓存路径；文件先查登记表、失效再回退 MD5 缓存路径（复用纯逻辑
    //ResolveLocalFilePath，那套优先级与回退已有单测 Suite CFL）。两处都没有即判定为
    //"本地没有、需要下载"。
    //@param row       当前页行下标
    //@param outPath   [出参] 可用的本地完整路径；返回 false 时被清空
    //@return 本地确有该文件返回 true
    bool ResolveRowLocalFile(int row, CString& outPath) const;

    //组装第 row 行的右键菜单上下文（消息类型 / 本地文件在不在 / 是否在下载 / 是否已 Pin）。
    //@param row  当前页行下标；越界时返回一个空上下文（菜单只会剩通用项）
    balloonhistory::HistoryRowMenuContext BuildRowMenuContext(int row) const;

    //执行一个菜单项。row 越界或该项在当前状态下不适用时安静忽略。
    //@param menuId  菜单项标识（取值见 balloonhistory::HistoryRowMenuId）
    //@param row     当前页行下标
    void ExecuteRowMenuCommand(int menuId, int row);

    //把第 row 行的整条正文写进剪贴板，内联表情写成“[表情]”（与气泡复制同口径）。
    void CopyRowText(int row);

    //对第 row 行发起一次下载（图片走 ImageTransfer、文件走 FileTransfer）。
    //@param row        当前页行下标
    //@param viewAfter  下载成功后是否自动打开看图窗（仅图片行有意义）
    void StartRowDownload(int row, bool viewAfter);

    //打开第 row 行对应的图片：本地已有直接开看图窗；没有则发起下载、完成后自动开。
    void ViewRowImage(int row);

private:

private:
    CFlamingoClient*             m_lpFMGClient;   //所属客户端对象（引用，不持有）
    HWND                         m_hMainDlg;      //CMainDlg 句柄（仅调试入口下关闭即退出用；真实打开为 NULL）
    CBalloonMainPanel*           m_pMainPanel;    //主面板（真实会话下解析昵称用，引用不持有；调试入口为 NULL）

    balloonhistory::HistoryModel m_model;         //浏览状态 / 分页模型（驱动列表）
    balloonhistory::CV2MsgStore  m_seedStore;     //调试入口用的 in-memory 预置本地库
    CString                      m_titleName;     //对话名（标题“消息记录 · <名>”用）
    std::map<int64_t, CString>   m_senderNames;   //调试期 fromid→昵称映射

    //从控件树捕获的指针（所有权在控件树，本类只引用，用于局部刷新）。
    balloonwjui::DuiScrollView*  m_pScroll;       //列表滚动视口
    balloonwjui::DuiControl*     m_pPager;        //分页器容器
    balloonwjui::DuiControl*     m_pFooterInfo;   //底部“共 N 条 · 第 x/y 页”标签
    balloonwjui::DuiControl*     m_pCustomRow;    //“自定义时间区间”行（显隐切换 + 重排用）

    //本窗口发起、尚未收到结果的下载（按消息 msgid 记）。菜单据它把「下载」换成
    //「取消下载」，收到结果时再移除。只装本窗口发起的那些 —— 聊天窗自己发起的下载
    //不进这个集合，免得两处互相干扰。
    std::set<int64_t>            m_downloadingMsgids;
    //"下载完成后要自动打开看图窗"的那条图片消息 msgid；0 表示没有这样的待办。
    //只在用户双击缩略图（而不是从菜单点「下载」）时置上 —— 双击的意图是"我要看这张图"，
    //下载只是达成它的手段；从菜单点「下载」则纯粹是把文件拿到本地，不该弹窗打断。
    int64_t                      m_pendingViewMsgid;

    //标题栏左侧图标位图（程序图标合成到标题栏底色上）。DuiFrameWindow::SetIcon 只
    //借用不释放，故本类持有、析构时 DeleteObject。
    HBITMAP                      m_hTitleIcon;
};
