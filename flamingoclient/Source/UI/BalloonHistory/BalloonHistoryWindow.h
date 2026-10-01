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
 *  调试期由命令行 --history 入口直接弹出：用一个 in-memory 预置本地库
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

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../App/CustomMsgDef.h"       // WM_CHAT_OPEN_LINK（消息映射里要用）
#include "HistoryWindowLayout.h"   // balloonhistory::HistoryViewData / HistoryRowView
#include "HistoryModel.h"          // balloonhistory::HistoryModel
#include "HistoryRowMenu.h"        // balloonhistory::HistoryRowMenuContext（成员函数返回它）
#include "V2MsgStore.h"            // balloonhistory::CV2MsgStore
#include "HistoryDateJumpPopup.h"  // balloonhistory::CHistoryDateJumpPopup（成员需完整类型）
#include "HistorySelection.h"     // balloonhistory::HistorySelection（多选集合，成员需完整类型）
#include "HistoryImageGrid.h"     // balloonhistory::HistoryBatchDownload（「全部下载」的批次，成员需完整类型）

class CFlamingoClient;
class CBalloonMainPanel;

namespace balloonwjui { class DuiScrollView; class DuiEdit; class DuiToast; }
namespace balloonhistory { class HistoryContentView; }

//局部刷新延迟消息：翻页等交互会销毁正被点击的控件（重建分页器 / 列表），故把刷新
//PostMessage 到事件解栈之后再做，避免在控件自身事件回调里销毁它导致 use-after-free。
//wParam 取 HistoryRefreshScope，说明这次刷新要不要重新统计总数；lParam 未用。
#define WM_HISTORY_REFRESH_CONTENT  (WM_APP + 0x0137)

//WM_HISTORY_REFRESH_CONTENT 的 wParam：本次刷新的范围。
enum HistoryRefreshScope
{
    //完整刷新：重新统计总数并重查当前页。筛选条件变了、本地库内容可能变了（新消息、
    //Pin 状态、下载状态、往前拉取并入等）时都取此值；不确定时也取此值。
    kHistoryRefreshFull     = 0,
    //只换页：筛选条件与本地库内容都没变，只是页号变了（首页 / 上一页 / 下一页 / 末页、
    //页码按钮、跳页框、翻页快捷键）。沿用上次统计的总数，只查新一页，见 HistoryModel::RefreshPage。
    kHistoryRefreshPageOnly = 1
};

//行右键菜单延迟弹出消息：TrackPopupMenu 是个模态消息循环，直接在 WM_RBUTTONUP 的
//处理里跑会与 DuiHost 的鼠标捕获打架（菜单收起后界面点击失灵），故投递本消息、
//延后到鼠标事件解栈之后再弹。wParam=当前页行下标，lParam=菜单落点（屏幕坐标，
//MAKELPARAM 打包；坐标可能为负，取出时须按有符号还原）。
#define WM_HISTORY_SHOW_ROW_MENU    (WM_APP + 0x0138)

//「先加载全部再导出」：往前拉取拉到最早一条之后投递本消息，事件解栈后再弹另存为结果提示、写文件。
//拉取完成的判断发生在刷新的调用栈里（由主面板的网络处理触发），不宜在那里弹模态框。
//wParam / lParam 未用。
#define WM_HISTORY_EXPORT_READY     (WM_APP + 0x013B)

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
    //  hMainDlg：主面板的窗口句柄；关闭本窗口时据此通知其收尾。
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

    //若本窗正打开，不论显示的是哪个会话都局部刷新（重查本地库）。供主面板在按时间范围
    //清理本地库之后调用 —— 区间清理作用于全部会话，本窗当前显示的会话必然也受影响。
    //未开窗时忽略。
    void RefreshIfOpen();

    //某页同步应答是不是本窗为"往前拉更早的消息"发出的那一轮。主面板据此决定这页要不要
    //再转给已打开的聊天窗：聊天窗只靠自己的翻页标志区分首屏与翻页，一页更早的消息转过去
    //会被当成首屏，把聊天窗清空、换成一页旧消息。
    //@param ctype / cid   应答所属会话
    //@param beforeMsgid   应答回带的请求锚点
    //@return 本窗开着、正有一轮在途、会话与锚点都对得上时返回 true
    bool IsOwnOlderFetch(int ctype, int64_t cid, int64_t beforeMsgid) const;

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

    /**
     *  在本窗口客户区顶部显示一条轻提示，几秒后自动淡出，不打断用户操作。
     *  目前用于主面板在本窗口发起的转发全部成功后提示「已转发」。
     *    text：提示文字；为 NULL 时忽略。
     *  窗口未打开或提示条尚未建好时什么都不做。
     */
    void ShowToast(LPCTSTR text);

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
        //「先加载全部再导出」拉取完毕：写导出文件
        MESSAGE_HANDLER(WM_HISTORY_EXPORT_READY, OnExportReadyMsg)
        //左键按下旁听：记下这一下是否落在消息列表里，Ctrl+C 据此判断复制的是列表里的
        //选区还是搜索框里的文字。只旁听、不消费。
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDownMsg)
        //Ctrl+C：最近一次点在列表里且有选区时复制选区并吞掉按键，否则照常交给搜索框。
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDownMsg)
        MESSAGE_HANDLER(WM_CHAT_OPEN_LINK, OnChatOpenLinkMsg)
        //缩略图后台解码完成的通知（本窗口在 BuildWindow 里登记为接收窗口）。
        MESSAGE_HANDLER(WM_CHAT_IMAGE_READY, OnImageReadyMsg)
        //销毁时从图片缓存注销，并把窗口位置与大小存进账号配置。只旁听、不消费，
        //DuiFrameWindow 照常收尾。
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        //位置或大小变化后记下还原态的窗口矩形，关窗时保存。只旁听、不消费。
        MESSAGE_HANDLER(WM_WINDOWPOSCHANGED, OnWindowPosChangedMsg)
        //往前拉取更早消息的超时定时器、列表滚动条淡入淡出的 pulse 定时器，以及搜索框
        //停止输入后才查询的延迟定时器。只认这三个定时器 id，其余交回基类。
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //创建窗口、配置外观、构建客户区并显示。成功返回 true。
    bool BuildWindow(HWND hWndParent);

    //全量构建客户区控件树（由当前模型状态生成），捕获要复用的控件指针，就绪搜索框。
    void InitUi();

    //局部刷新：按当前模型重查本地库，只换列表内容 + 重填分页器 + 更新底部信息与胶囊
    //选中态与标题副行；不动搜索框（保住焦点）。
    //  pageOnly：true 表示只是页号变了，沿用上次统计的总数、只查新一页（筛选条件若已变，
    //            模型会自动改做完整刷新）；false 表示完整刷新。外部通知数据变化时一律传 false。
    void RefreshContent(bool pageOnly = false);

    //按当前会话的覆盖区间给模型设显示下界（HistoryCoverage.h 的 CoverageMinVisibleMsgid）。
    //每次重查本地库之前调用；调试入口（没有注入真实本地库）下不限制。
    void ApplyCoverageBound();

    //重查本地库之后调用：有一轮往前拉取在途时，看它的应答是否已经并入覆盖区间，并入了就
    //按 ShouldContinueFetchOlder 决定接着拉还是停；没有在途时，按 ShouldFetchOlder 决定
    //要不要开始拉。调试入口下什么都不做。
    void CheckFetchOlderAfterRefresh();

    //读出判断"还有没有更早的没加载 / 要不要去拉"所需的几项：覆盖区间、时间筛选的下界（没有下界
    //时 hasTimeLowerBound 为 false）、区间内最早一条的时间（区间内一行都没有时取 INT64_MAX）。
    //调试入口（没有本地库或主面板）或读不到覆盖区间时返回 false，出参无意义。
    bool LoadOlderFetchInputs(balloonhistory::CoverRange& cover, bool& hasTimeLowerBound,
                              int64_t& timeLowerBoundMs, int64_t& oldestCoveredMs);

    //当前筛选条件下服务端是否还有更早的记录没有加载到本地（见 HasUnloadedOlder）。
    //调试入口返回 false。副标题与底部信息栏据它决定写"共 N 条"还是"已加载 N 条"。
    bool HasUnloadedOlderNow();

    //以覆盖区间下界为锚点向服务端要更早的一页，记下在途状态并启动超时定时器。
    //@param cover  当前会话的覆盖区间（须已建立、未到最早）
    void RequestOlderPage(const balloonhistory::CoverRange& cover);

    //结束往前拉取：清掉在途状态、停掉超时定时器。不刷新界面，由调用方决定。
    void StopFetchingOlder();

    //按模型当前的总数、页码与是否在往前拉取，重写底部信息栏"共 N 条 · 第 x/y 页"；
    //拉取在途时末尾追加"正在加载更早的消息…"。改完随即重排底部一行（见 RelayoutFooter）。
    void UpdateFooterInfo();

    //重排底部一行（信息 / 分页栏 / 跳至 / 导出）。分页栏重建或信息文字变化后都要调用：
    //分页栏按内容宽度占位，宽度不够时隐藏哪些控件也是排版时才决定的（HistoryFooterBar），
    //只重排分页栏自己的话，这两件事都不会重新计算。
    void RelayoutFooter();

    //WM_TIMER：处理本窗的三个定时器，其余交回基类：
    //  · 往前拉取超时：放弃这一轮，底部信息栏去掉"正在加载更早的消息…"；
    //  · 列表滚动条淡入淡出：推进一帧，滚动条稳定后停掉；
    //  · 搜索延迟：搜索框停止输入一段时间后，按当前文字查询。
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

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

    //当前页第 row 行被双击：图片行打开看图窗（本地没有原图时先下载、下完自动打开），
    //其余类型定位到聊天窗。双击整行与双击行内昵称、时间、空白处、图片预览都走这里。
    //  row：当前页的行下标（从0起）；越界时按"定位"处理，由 LocateRow 自己忽略。
    void OnRowDoubleClicked(int row);

    //跳到指定页并局部刷新。供底部「跳至第 N 页」微调框的 ▲▼ 与手输入提交共用。
    //  page：目标页号（从1起）。<=0 视为"用户没真的输入"，直接忽略；越界由模型
    //        自己夹到 [1, PageCount]。与当前页相同时不做无谓的重建。
    void JumpToPage(int page);

    //把当前模型状态翻译成整窗渲染数据（标题 / 总数 / 当前页 / 各行 / 选中胶囊与时间档）。
    //  pageOnly：含义同 RefreshContent 的同名参数，决定重查时调 HistoryModel 的
    //            RefreshPage 还是 Refresh。
    balloonhistory::HistoryViewData BuildViewData(bool pageOnly);

    //「导出」按钮：把当前筛选条件下的**全部**消息（非仅当前页）导出为 .txt 或网页（.html）。
    //服务端还有更早的消息没拉下来时先问用户：先加载全部再导出 / 只导出已加载的 / 取消。
    //选"先加载全部"时记下导出目标、开始往前拉取，拉到最早一条后由 OnExportReadyMsg 写文件。
    void ExportHistory();

    //弹另存为对话框选导出路径与格式。
    //  outPath：[出参] 选定的完整路径；outHtml：[出参] true 表示网页格式，false 表示文本格式。
    //@return 用户点了「保存」返回 true，取消返回 false
    bool AskExportPath(CString& outPath, bool& outHtml);

    //服务端还有更早的消息时问用户怎么导出（三个按钮的 TaskDialog）。
    //@return 取值见 .cpp 里的 HistoryExportChoice：先加载全部 / 只导出已加载的 / 取消
    int AskUnloadedBeforeExport();

    //为「先加载全部再导出」开始往前拉取：已有一轮在途就等它，否则立即发起一轮。
    void StartFetchAllForExport();

    //按当前筛选条件取出全部消息，写成文本或网页。网页格式会把本地有的图片复制到
    //<网页名>_files 文件夹（按文件头补扩展名），本地没有的写「[图片未下载]」。
    //  path：导出文件的完整路径；html：true 写网页，false 写文本。
    void WriteExport(const CString& path, bool html);

    //WM_HISTORY_EXPORT_READY：「先加载全部再导出」拉取完毕，写导出文件。
    LRESULT OnExportReadyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //右键菜单「查看上下文」：清掉全部筛选（并同步搜索框、下拉框），翻到这一行在全量列表里的
    //那一页，刷新后高亮并滚到这一行（见 ApplyLocatedRow）。
    //  row：当前页行下标；越界时什么都不做。
    void ViewRowContext(int row);

    //在某个控件下方打开「选日期」弹层（「从 / 到」两组年月日）。预置日期由
    //ComputeHistoryDatePopupDefaults 算出：「只看这段时间」正在生效时取它的起止，否则取当前页
    //第一条消息的日期（当前页为空时取今天）；年份范围从本地最早一条消息的年份到今年。
    //  anchorId：锚点控件编号，kIdHistoryDatePick（「选日期」按钮）或 kIdHistoryDateRangeLabel
    //            （日期区间标签）；找不到该控件时什么都不做。
    void OpenDateJumpPopup(UINT anchorId);

    //「选日期」弹层提交回调的转接函数（C 风格回调没有 this）。userdata 即本窗口；action 取值见
    //HistoryDatePopupAction。返回 true 表示已接受（弹层关闭），false 表示日期有误（弹层保持打开）。
    static bool OnDatePopupThunk(void* userdata, int action, const balloonhistory::HistoryDate& from,
                                 const balloonhistory::HistoryDate& to);

    //「跳到这一天」：先清掉时间筛选（回到全部时间，关键字、类型、发送人保留），再在筛选结果里
    //翻到那天最晚的一条（那天没有就取之前最近的一条，之前也没有就翻到末页），高亮并滚到那一行；
    //后两种情况给出轻提示。
    //  day：弹层交回的日期，可能是不存在的日期（如 2 月 30 日）。
    //  返回：日期存在、已开始跳转时返回 true；日期不存在时给出轻提示并返回 false。
    bool JumpToDay(const balloonhistory::HistoryDate& day);

    //「只看这段时间」：检查起止日期，合法时把时间筛选设为这段日期（含两端整天），筛选行显示日期
    //区间标签并完整刷新。
    //  from / to：弹层交回的开始、结束日期，可能不存在或先后颠倒。
    //  返回：已应用时返回 true；日期不存在或开始晚于结束时给出轻提示并返回 false。
    bool FilterByDateRange(const balloonhistory::HistoryDate& from, const balloonhistory::HistoryDate& to);

    //按模型当前的时间筛选同步筛选行：时间下拉的选中项，以及日期区间标签的显隐与文字。
    //局部刷新不重建筛选行，凡是由程序改动时间筛选之后都要调用一次。不发出下拉的取值变化通知。
    void SyncTimeFilterControls();

    //在当前筛选结果里跳到 [startMs, endMs] 那一天，并按结果设定位高亮、给轻提示。那天还没从
    //服务端拉下来（结果为"之前没有"、而服务端还有更早的消息）时把它记为待跳日期，翻到末页让
    //往前拉取照常进行，之后每次完整刷新都由 RefreshContent 再调本函数重试，直到找到或拉到最早。
    //  startMs / endMs：那天的起止（见 balloonhistory::LocalDayBoundsMs）。
    //  retry：true 表示是待跳日期的重试，"正在加载"的提示不再重复显示。
    void ApplyDateJump(int64_t startMs, int64_t endMs, bool retry);

    //右键菜单「多选」：进入多选模式（已在多选模式时只是把这一行加进来），预先选中这一行，用多选
    //操作栏换下底部分页栏，并按只换页刷新列表让每行出现复选框。
    //  row：当前页行下标；越界时不预选任何一行。
    void EnterMultiSelect(int row);

    //退出多选模式：清空选择、换回底部分页栏、刷新列表去掉复选框。
    void ExitMultiSelect();

    //换会话时退出多选模式：清空选择、换回分页栏，但不刷新（调用方随后会整页刷新）。
    void ResetMultiSelect();

    //显示 / 隐藏多选操作栏，同时隐藏 / 显示底部分页栏（两者同高、互相替换），并重排客户区。
    void ShowSelectBar(bool show);

    //把操作栏左侧的「已选 N 条」更新为当前选择集合的条数。
    void UpdateSelectInfo();

    //行首复选框被勾选或取消：更新选择集合。已经选满时不选中这一行、把复选框恢复为未勾选并提示。
    //  row：当前页行下标；越界时什么都不做。
    void ToggleRowSelection(int row);

    //操作栏「合并转发」「逐条转发」：把能转发的选中消息交给主面板的转发目标选择窗，随后退出多选。
    //  merge：true 合并转发为一条聊天记录，false 逐条转发。
    void ForwardSelection(bool merge);

    //操作栏「删除」：确认后逐条发出删除请求（与单条删除同口径，界面等服务端推送回来再更新），
    //随后退出多选。调试入口没有服务端，只提示不执行。
    void DeleteSelection();

    //RefreshContent 换完列表后调用：m_locateMsgid 在当前页里时给那一行加定位高亮；
    //m_scrollToLocated 为 true 时再把列表滚到让它居中，并把该标志清掉（只滚一次）。
    void ApplyLocatedRow();

    //撤掉定位高亮（用户做了下一次操作）。当前页里那一行立即恢复原样，m_locateMsgid 清零。
    void ClearLocatedRow();

    //按本地库重填「发送人」下拉：第 0 项「全部成员」，其余为当前会话里发过言的人（最近发过言
    //的在前，名字经 ResolveSenderName 解析），并选中模型当前的发送人筛选。当前筛选的人已经不在
    //名单里时改回「全部成员」并投递一次刷新。打开窗口、换会话、往前拉取到更早的消息之后调用。
    void RefreshSenderCombo();

    //窗口开着时搜索条件被重置后（换到别的会话），把搜索框清空、时间下拉恢复为模型当前的
    //时间范围（含日期区间标签），并取消还在计时的搜索。局部刷新不重建这几个控件，故需另行同步。
    //清空搜索框时不发出文字变化通知。
    void ResetSearchControls();

    //预置 in-memory 调试本地库并灌入若干假消息（调试入口用）。
    void SeedDebugStore();

    //解析发送者昵称（调试期查 m_senderNames；查不到回退“用户<id>”）。
    CString ResolveSenderName(int64_t fromid) const;

    //WM_CLOSE：调试入口下本窗口是唯一可见窗口，关闭即请求退出程序。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DUI_NOTIFY：路由类型胶囊 / 时间下拉 / 搜索框 / 分页按钮的交互到模型，改完模型后
    //PostMessage(WM_HISTORY_REFRESH_CONTENT) 延迟刷新。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_HISTORY_REFRESH_CONTENT：事件解栈后执行局部刷新（见 RefreshContent）。
    LRESULT OnRefreshContentMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_CHAT_IMAGE_READY：ChatImageCache 在后台把某张缩略图解码完成后投递。重画窗口，
    //让缩略图从灰底换成真图。wParam / lParam 未用。
    LRESULT OnImageReadyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DESTROY 旁听：从 ChatImageCache 注销本窗口，并保存窗口位置（见 SaveWindowPlacement）。
    //恒置 bHandled=FALSE，让 DuiFrameWindow 继续处理销毁。
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_WINDOWPOSCHANGED 旁听：窗口处于还原态（既没有最大化也没有最小化）时，把当前窗口
    //矩形记进 m_lastNormalRect。恒置 bHandled=FALSE，系统据此照常生成 WM_SIZE / WM_MOVE。
    LRESULT OnWindowPosChangedMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //按账号配置里保存的位置与大小摆放窗口（BuildWindow 里、显示之前调用）。没有保存过、
    //或者调试入口没有客户端对象时什么都不做，窗口保持默认大小居中。
    //@return 上次关闭时是否处于最大化；为 true 时调用方应以最大化方式显示
    bool RestoreWindowPlacement();

    //把还原态的窗口矩形与是否最大化写进账号配置并立即存盘（窗口销毁时调用）。
    //调试入口没有登录账号，CFlamingoClient::SaveUserConfig 内部会直接返回，不会写文件。
    void SaveWindowPlacement();

    //WM_RBUTTONUP 旁听：按落点算出点在哪一行，投递 WM_HISTORY_SHOW_ROW_MENU 延迟弹菜单。
    //恒置 bHandled=FALSE（理由同悬停那两个处理器）。
    LRESULT OnRButtonUpMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_HISTORY_SHOW_ROW_MENU：事件解栈后真正弹出行右键菜单并分发选中项。
    LRESULT OnShowRowMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_OPEN_LINK 处理：列表行正文里的超链接被点击。lParam 为堆上的
     *  std::wstring*（规范化后的 URL），本函数负责 delete。打开前先过
     *  balloonchatwnd::IsSafeLinkUrl 的协议白名单 —— 只放行 http / https。
     */
    LRESULT OnChatOpenLinkMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // ---- 行右键菜单：命中、上下文、以及各菜单项的处理 ----

    //按落点（客户区坐标）算出点在当前页的第几行。
    //@param pt  客户区坐标
    //@return 行下标（从0起）；没落在任何一行上、或落在滚动视口之外时返回 -1
    int RowIndexAtPoint(POINT pt) const;

    //「图片」分类网格模式下，客户区落点 pt 所在的缩略图对应的当前页记录下标；不在任何缩略图上、
    //或落点在滚动视口之外时返回 -1。
    int GridIndexAtPoint(POINT pt) const;

    //当前是否以缩略图网格显示列表：选了「图片」分类、且不在多选模式（多选要在行首放复选框，
    //仍用逐行列表）。
    bool IsImageGridMode() const;

    //当前页里要被「全部下载」下载的文件：文件消息、本地没有、也没在下载（规则见 HistoryImageGrid.h
    //的 PickFilesToDownload）。返回它们的当前页行下标，按显示顺序。
    std::vector<int> PendingFileDownloadRows() const;

    //「全部下载」：对当前页每个待下载的文件发起下载（下完不打开）。
    void DownloadAllFilesOnPage();

    //取第 row 行那条消息在本地的可用文件路径。
    //
    //图片按内容 MD5 换算缓存路径；文件先查登记表、失效再回退 MD5 缓存路径（复用纯逻辑
    //ResolveLocalFilePath，那套优先级与回退已有单测 Suite CFL）。两处都没有即判定为
    //"本地没有、需要下载"。
    //@param row       当前页行下标
    //@param outPath   [出参] 可用的本地完整路径；返回 false 时被清空
    //@return 本地确有该文件返回 true
    bool ResolveRowLocalFile(int row, CString& outPath) const;

    //同 ResolveRowLocalFile，但直接给出记录，不限于当前页（导出时对全部记录逐条查找）。
    //@param rec      消息记录
    //@param outPath  [出参] 可用的本地完整路径；返回 false 时被清空
    //@return 本地确有该文件返回 true
    bool ResolveRecordLocalFile(const balloonhistory::HistoryRecord& rec, CString& outPath) const;

    //组装第 row 行的右键菜单上下文（消息类型 / 本地文件在不在 / 是否在下载 / 是否已 Pin）。
    //@param row  当前页行下标；越界时返回一个空上下文（菜单只会剩通用项）
    balloonhistory::HistoryRowMenuContext BuildRowMenuContext(int row) const;

    //执行一个菜单项。row 越界或该项在当前状态下不适用时安静忽略。
    //@param menuId  菜单项标识（取值见 balloonhistory::HistoryRowMenuId）
    //@param row     当前页行下标
    void ExecuteRowMenuCommand(int menuId, int row);

    //右键菜单「复制」：右键点在某条正文的选区上时复制选中部分，否则复制第 row 行的
    //整条正文。都按聊天内容格式写剪贴板（与聊天窗复制同口径）：表情粘回聊天输入框
    //能还原，纯文字形式里写成“[表情]”。
    void CopyRowText(int row);

    //WM_LBUTTONDOWN 旁听：记下按下处是否落在消息列表的视口里。恒置 bHandled=FALSE。
    LRESULT OnLButtonDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_KEYDOWN：先按 DecideHistoryKey（HistoryWindowKeys.h）处理快捷键 —— Esc 清空搜索或
    //关窗、PageUp / PageDown 翻页、Ctrl+Home / Ctrl+End 首页末页、Ctrl+F 到搜索框、搜索框里
    //回车立即搜索；再处理 Ctrl+C：最近一次左键按下落在列表里、并且某条正文有选区时，把选区
    //写进剪贴板并消费这次按键。其余按键置 bHandled=FALSE，照常交给 DuiHost 分给获得焦点的
    //控件（如搜索框）。
    LRESULT OnKeyDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //执行一个快捷键动作。
    //  action：取值见 balloonhistory::HistoryKeyAction（不会是"不处理"）。
    //  search：搜索框控件（所有权在控件树）；找不到时为 NULL，与搜索框有关的动作随之跳过。
    void ExecuteKeyAction(int action, balloonwjui::DuiEdit* search);

    //按搜索框当前的文字设置关键字并重查（投递 WM_HISTORY_REFRESH_CONTENT，事件解栈后刷新）。
    void ApplySearchKeyword();

    //翻到指定页并重查（只换页，不重新统计总数）。页号越界由模型限制到 [1, PageCount]。
    void GoToPageAndRefresh(int page);

    //在客户区控件树里找当前带选区的那条正文（同一时刻至多一条）。
    //@return 找到时返回控件指针（所有权在控件树）；没有选区返回 NULL
    balloonhistory::HistoryContentView* FindSelectedContentView() const;

    //用系统默认程序打开本地文件；失败弹提示。hasLocal 为 false 时什么都不做。
    void OpenRowFile(const CString& localPath, bool hasLocal);

    //在资源管理器里定位并选中本地文件。hasLocal 为 false 时什么都不做。
    void OpenRowFolder(const CString& localPath, bool hasLocal);

    //把第 row 行对应的本地文件另存到用户选定位置（整字节拷贝）。
    //@param row        当前页行下标（用来取原始文件名作默认名）
    //@param localPath  源文件的本地完整路径；空串直接返回
    void SaveRowFileAs(int row, const CString& localPath);

    //对第 row 行发起一次下载（图片走 ImageTransfer、文件走 FileTransfer）。
    //@param row           当前页行下标
    //@param viewAfter     下载成功后是否自动打开看图窗（仅图片行有意义）
    //@param refreshAfter  发起后是否投递一次列表刷新，让菜单项变为「取消下载」。「全部下载」
    //                     连续发起多笔时传 false，由调用方在全部发起后只刷新一次
    //@return 确实发起了一笔新的下载时返回 true；该行无可下载内容、缺少文件信息（会弹框说明）、
    //        已在下载中，或处于没有连接的调试入口时返回 false
    bool StartRowDownload(int row, bool viewAfter, bool refreshAfter = true);

    //作废「全部下载」的当前批次（关窗、切换会话时调用），并停止跟踪本批尚未结束的下载，
    //使它们结束时既不计入任何批次、也不逐条提示。reason 只用于日志。
    void AbandonBatchDownload(const char* reason);

    //取消第 row 行正在进行的下载。图片没有取消接口，此时只撤掉本窗口的跟踪状态。
    void CancelRowDownload(int row);

    //打开第 row 行对应的图片：本地已有直接开看图窗；没有则发起下载、完成后自动开。
    void ViewRowImage(int row);

    //第 row 行是合并转发的聊天记录时，请主面板打开聊天记录查看窗（FMG_MSG_OPEN_MERGED_VIEWER）。
    //  row：当前页行下标；越界、不是合并记录、或调试入口下没有主面板时只记日志
    void OpenRowMergedRecord(int row);

    //转发第 row 行这一条消息：按逐条转发组好 balloonmain::ForwardRequest，以
    //FMG_MSG_OPEN_FORWARD_PICKER 投给主面板打开目标选择窗；发送与结果提示都由主面板负责。
    //  row：当前页行下标；越界、本条不能转发（判定见 CanForwardHistoryRow）、或调试入口下
    //       没有主面板时只记日志
    void ForwardRow(int row);

    //单击第 row 行的图片缩略图：本地没有原图、且当前可以下载时发起下载，只下载、不打开
    //看图窗；本地已有原图、正在下载或无从下载时什么都不做。能否下载按当前状态重新判定，
    //不依赖建列表时记在缩略图控件上的快照。
    //@param row  当前页行下标；越界时忽略
    void DownloadRowThumb(int row);

    /**
     *  为图片行填写缩略图相关的字段（m_hasImageThumb / m_thumbImagePath / m_thumbState /
     *  m_thumbClickable / m_thumbTooltip）。状态按 balloonchatwnd::ResolveHistoryThumb 判定。
     *    row：         [出参] 要填写的行数据。
     *    msgid：       该消息的服务器 msgid，用来查"是否正在下载 / 是否下载失败过"。
     *    imageMd5：    图片内容 MD5（UTF-8）；为空时算不出本地缓存路径。
     *    imageFileId： 图片的服务器文件 ID（UTF-8）；为空时无从下载。
     *    imageOrdinal：本页第几张图片（从 0 起）。仅调试入口使用：没有登录态时按它轮流套用
     *                  几种占位状态，好在调试入口下核对各状态的样子。
     */
    void FillImageThumb(balloonhistory::HistoryRowView& row, int64_t msgid,
                        const std::string& imageMd5, const std::string& imageFileId,
                        int imageOrdinal) const;

    //当前登录用户 userid（缓存与下载目录按账号隔离）。未登录 / 调试入口返回 0。
    int64_t SelfUserId() const;

private:

private:
    CFlamingoClient*             m_lpFMGClient;   //所属客户端对象（引用，不持有）
    //最近一次处于还原态时的窗口矩形（屏幕坐标，物理像素）。最大化或最小化时拿不到还原态的
    //矩形，关窗时就用它保存。由 OnWindowPosChangedMsg 更新，每次打开窗口重新开始记录
    RECT                         m_lastNormalRect;
    bool                         m_hasNormalRect; //m_lastNormalRect 是否已经记录过
    //「发送人」下拉各项对应的 userid，下标与下拉项一一对应；第 0 项「全部成员」对应 0。
    //由 RefreshSenderCombo 重建，选中项变化时据此取出要筛选的 userid
    std::vector<int64_t>         m_senderIds;
    //要加定位高亮的那条消息的 msgid（「查看上下文」「跳到这一天」翻到的那一条）；0 表示没有。
    //保持到用户下一次操作，由 ClearLocatedRow 清零
    int64_t                      m_locateMsgid;
    //下一次 RefreshContent 之后是否要把列表滚到高亮的那一行。只在刚翻过去时滚一次，之后的
    //刷新（例如图片下载完成）只保持高亮、不再改动用户的滚动位置
    bool                         m_scrollToLocated;
    //上一次显示列表时的筛选条件（含页号）。RefreshContent 换列表后与当前条件比较，页号或筛选条件
    //变了就把列表滚回顶部（见 ShouldResetListScroll）；InitUi 建好界面时记下第一份
    balloonhistory::HistoryFilter m_shownFilter;
    //「选日期」弹层。窗口对象存在期间一直复用，第一次打开时创建弹层窗口
    balloonhistory::CHistoryDateJumpPopup m_dateJumpPopup;
    //「跳到这一天」等待往前拉取的那一天（起止 Unix 毫秒，见 ApplyDateJump）；m_pendingJumpEndMs 为 0
    //表示没有待跳的日期。用户做了下一次操作（ClearLocatedRow）或换会话时清零
    int64_t                      m_pendingJumpStartMs;
    int64_t                      m_pendingJumpEndMs;
    //为待跳日期把往前拉取重新开始过几次（每次最多 kMaxFetchOlderRounds 轮），与
    //kMaxPendingJumpRestarts 比较，防止日期选得过早时无休止地向服务端要数据
    int                          m_pendingJumpRestarts;
    //是否处于多选模式。为 true 时每行行首有复选框、底部是多选操作栏、右键不弹菜单
    bool                         m_multiSelect;
    //多选模式下勾选的消息（按 msgid 记，翻页不丢，上限与转发上限 net::kForwardMaxMessages 一致）。
    //退出多选或换会话时清空
    balloonhistory::HistorySelection m_selection;
    //「先加载全部再导出」的导出目标：路径非空表示正在为导出往前拉取，拉到最早一条后写这个文件
    CString                      m_pendingExportPath;
    //上面那个导出目标是否为网页格式（false 为文本格式）
    bool                         m_pendingExportHtml;
    //为导出把往前拉取重新开始过几次，与 kMaxPendingJumpRestarts 共用同一个上限
    int                          m_pendingExportRestarts;
    HWND                         m_hMainDlg;      //主面板句柄（仅调试入口下关闭即退出用；真实打开为 NULL）
    CBalloonMainPanel*           m_pMainPanel;    //主面板（真实会话下解析昵称用，引用不持有；调试入口为 NULL）

    balloonhistory::HistoryModel m_model;         //浏览状态 / 分页模型（驱动列表）
    balloonhistory::CV2MsgStore  m_seedStore;     //调试入口用的 in-memory 预置本地库
    CString                      m_titleName;     //对话名（标题“消息记录 · <名>”用）
    std::map<int64_t, CString>   m_senderNames;   //调试期 fromid→昵称映射

    //从控件树捕获的指针（所有权在控件树，本类只引用，用于局部刷新）。
    balloonwjui::DuiScrollView*  m_pScroll;       //列表滚动视口
    balloonwjui::DuiControl*     m_pPager;        //分页器容器
    balloonwjui::DuiControl*     m_pFooterInfo;   //底部“共 N 条 · 第 x/y 页”标签
    //客户区顶部的轻提示条（「已转发」等）。所有权在控件树；每次建窗口时由 InitUi 重新创建并
    //记下，窗口销毁后指针失效，使用前须先判 IsWindow
    balloonwjui::DuiToast*       m_pToast;

    //本窗口发起、尚未收到结果的下载（按消息 msgid 记）。菜单据它把「下载」换成
    //「取消下载」，收到结果时再移除。只装本窗口发起的那些 —— 聊天窗自己发起的下载
    //不进这个集合，免得两处互相干扰。
    std::set<int64_t>            m_downloadingMsgids;
    //「全部下载」发起的这一批文件下载。本批的下载失败时不逐条弹框，整批结束后汇总提示一次；
    //关窗或切换会话时作废（见 AbandonBatchDownload）。
    balloonhistory::HistoryBatchDownload m_batchDownload;
    //本窗口发起的下载里失败了的图片（按消息 msgid 记）。缩略图据此显示破图并允许单击
    //重试；再次发起下载时移除，下载成功时也移除。只在本窗口存活期间有效，关窗即清空。
    std::set<int64_t>            m_failedImageMsgids;
    //"下载完成后要自动打开看图窗"的那条图片消息 msgid；0 表示没有这样的待办。
    //只在用户双击缩略图（而不是从菜单点「下载」）时置上 —— 双击的意图是"我要看这张图"，
    //下载只是达成它的手段；从菜单点「下载」则纯粹是把文件拿到本地，不该弹窗打断。
    int64_t                      m_pendingViewMsgid;
    //最近一次左键按下是否落在消息列表的视口里。true 时 Ctrl+C 复制列表里的选区；
    //false（点在搜索框等别处）时 Ctrl+C 交还给获得焦点的控件。
    bool                         m_lastPressInList;
    //当前这次行右键菜单里的「复制」是否复制选区：弹菜单前按右键落点是否在选区上算好，
    //执行「复制」时读取并复位。
    bool                         m_menuCopySelection;

    //标题栏左侧图标位图（程序图标合成到标题栏底色上）。DuiFrameWindow::SetIcon 只
    //借用不释放，故本类持有、析构时 DeleteObject。
    HBITMAP                      m_hTitleIcon;

    // ---- 按覆盖区间显示、翻到末页时向服务端往前拉（docs/清理聊天记录方案.md §9.2）----

    //真实会话下注入的本地库（引用不持有，与传给 m_model 的是同一个）；用来读覆盖区间与
    //区间内最早一条的时间。调试入口为 NULL，此时既不按区间收窄，也不向服务端拉取。
    balloonhistory::CV2MsgStore* m_pStore;
    //是否有一轮往前拉取在途。在途期间底部信息栏追加"正在加载更早的消息…"。
    bool                         m_fetchingOlder;
    //本次翻到末页以来已完成的拉取轮数，与 kMaxFetchOlderRounds 比较决定是否接着拉。
    int                          m_fetchRounds;
    //开始这一串拉取之前，当前筛选条件下的总条数；与每轮之后的总条数比较，看多出了几条。
    int64_t                      m_fetchCountBefore;
    //在途那一轮请求的锚点（发出时覆盖区间的下界）。覆盖区间的下界降到它以下、或已拉到
    //最早，即表示这一轮的应答已经并入；主面板据它认出哪一页应答是本窗发起的。
    int64_t                      m_fetchAnchor;
};
