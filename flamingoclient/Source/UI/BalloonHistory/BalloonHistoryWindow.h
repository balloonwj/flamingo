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
#include "HistoryConversationList.h"   // balloonhistory::ManagerConversationItem（左栏会话列表，成员需完整类型）
#include "HistoryScopePopup.h"         // balloonhistory::CHistoryScopePopup（搜索范围弹层，成员需完整类型）
#include "HistoryLibraryModel.h"       // balloonhistory::HistoryLibraryModel（文件 / 图片页的浏览模型，成员需完整类型）
#include "HistoryTimeline.h"           // balloonhistory::HistoryHitNavigator 等（时间轴模式，成员需完整类型）
#include "HistoryLibraryLayout.h"      // balloonhistory::HistoryLibraryViewData（文件 / 图片页的渲染数据）
#include "HistoryStorageStats.h"       // balloonhistory::HistoryStorageStats（本机存储占用页的统计结果，成员需完整类型）
#include "HistoryExportPopup.h"        // balloonhistory::CHistoryExportPopup（导出选项浮层，成员需完整类型）
#include <thread>                      // 导出在后台线程执行
#include <atomic>
#include "../../Feature/Voice/VoiceHub.h"       // balloonvoice::IVoicePlayListener（语音条播放，第 4 期）
#include "../BalloonChatWnd/ChatMessageList.h"  // WM_CHAT_VOICE_ACTION（消息映射里要用）
#include "../BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：任务栏与 Alt+Tab 的程序图标

class CFlamingoClient;
class CBalloonMainPanel;
namespace balloonchatwnd { class ChatVoiceStrip; }

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

//聊天记录管理器文件 / 图片页（RefreshLibrary）的刷新范围
enum HistoryLibraryRefreshScope
{
    kLibRefreshPage = 0,   //只换页：筛选条件与数据都没变，沿用上次统计的总数，只取新一页
    kLibRefreshList = 1,   //重新统计总数并取当前页（左栏的计数不变时用）
    kLibRefreshAll  = 2    //连同各会话的计数与左栏一起刷新：筛选条件或数据变了时用，不确定时也用它
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

//聊天记录管理器左栏会话行的右键菜单延迟弹出消息。左栏的 DuiTreeView 在处理鼠标右键时发出
//DUITVN_RCLICK，在那里直接跑 TrackPopupMenu 的模态循环会与 DuiHost 的鼠标捕获冲突（理由同
//WM_HISTORY_SHOW_ROW_MENU），故投递本消息，等事件解栈之后再弹。wParam=左栏会话树的节点 id，
//lParam=菜单落点（屏幕坐标，MAKELPARAM 打包；取出时须按有符号还原）。
#define WM_HISTORY_SHOW_MGR_MENU    (WM_APP + 0x013C)

//聊天记录管理器左栏跨会话搜索的结果（搜索挪到后台线程执行）。由主面板持有的
//balloonhistory::CV2MsgSearchWorker 在后台线程查完后投递。wParam=请求序号，lParam=堆上的
//balloonhistory::V2SearchResponse*，接收方无论采纳与否都要 delete；窗口销毁时由
//CV2MsgSearchWorker::DrainPendingResponses 释放还在队列里的。
#define WM_HISTORY_MGR_SEARCH_DONE  (WM_APP + 0x013D)

//聊天记录管理器文件 / 图片页（docs/聊天记录管理器方案.md §11.5、§11.6）一行（一张图）的右键菜单与「⋯」菜单，
//延后到鼠标事件解栈之后再弹（理由同 WM_HISTORY_SHOW_ROW_MENU）。wParam=当前页的行下标（图片页为第几张），
//lParam=菜单落点（屏幕坐标，MAKELPARAM 打包；取出时须按有符号还原）。
#define WM_HISTORY_SHOW_LIB_MENU    (WM_APP + 0x013E)

//时间轴导航条（docs/聊天记录管理器方案.md §11.7）上按在了某个年标题行或月份行上：处理时会重建导航条，故等鼠标
//事件处理完再做（理由同 WM_HISTORY_SHOW_ROW_MENU）。wParam=被按下那一行的控件编号，lParam 不用。
#define WM_HISTORY_TIMELINE_NAV_CLICK (WM_APP + 0x013F)

//时间轴模式的统计结果（统计挪到后台线程执行）。由主面板持有的 balloonhistory::CV2MsgSearchWorker 查完后
//投递，wParam=请求序号，lParam=堆上的 balloonhistory::V2SearchResponse*，接收方无论采纳与否都要 delete；窗口销毁时
//由 CV2MsgSearchWorker::DrainPendingResponses 释放还在队列里的。
#define WM_HISTORY_TIMELINE_STATS_DONE (WM_APP + 0x0140)

//「本机存储占用」页（docs/聊天记录管理器方案.md §12.6）的统计或清理结果。由主面板持有的
//balloonhistory::CV2MsgSearchWorker 在后台线程上做完后投递，wParam=请求序号，lParam=堆上的
//balloonhistory::V2SearchResponse*（种类为 kV2SearchStorageStats 或 kV2SearchStorageClean），接收方无论采纳与否都要
//delete；窗口销毁时由 CV2MsgSearchWorker::DrainPendingResponses 释放还在队列里的。
#define WM_HISTORY_STORAGE_DONE     (WM_APP + 0x0141)

//导出增强（docs/聊天记录管理器方案.md §12.7）的进度：由执行导出的后台线程投递，wParam=已完成的步数，lParam=总步数。
#define WM_HISTORY_EXPORT_PROGRESS  (WM_APP + 0x0142)

//导出增强的结果：后台线程执行完后投递，lParam=堆上的 balloonhistory::HistoryExportResult*，接收方负责 delete；
//投递失败（窗口已销毁）时由后台线程自己释放。
#define WM_HISTORY_EXPORT_DONE      (WM_APP + 0x0143)

//帖子卡片缩略图（docs/帖子群方案.md 14.5）的下载结果：本窗口经 net::ImageTransfer::EnqueuePlainDownload 按原图的 fileid 与
//md5 下载到聊天图片缓存，结果投回本窗口。lParam=堆上的 net::PlainTransferResult*，接收方负责 delete；窗口销毁时清掉
//队列里还没送达的。
#define WM_HISTORY_CARD_IMAGE_DONE  (WM_APP + 0x0144)

//看帖子卡片的图片时，看图窗口里按了 ← / →：进程内唯一的翻页回调对象按本窗口句柄同步发来本消息（窗口已关闭时不发），
//wParam=翻页方向（按有符号整数还原，-1 上一张、1 下一张），lParam 不用。
#define WM_HISTORY_CARD_NAVIGATE    (WM_APP + 0x0145)

namespace balloonhistory { struct V2SearchResponse; }

//历史消息记录窗口。继承 balloonwjui::DuiFrameWindow，由它提供无边框自绘窗体与
//标题栏（本窗只保留关闭按钮）。窗口可缩放，设最小尺寸。
class CBalloonHistoryWindow : public balloonwjui::DuiFrameWindow,
                              public balloonvoice::IVoicePlayListener
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

    //打开（或重定向到）某会话，并预设右侧的搜索条件：关键字、类型胶囊，以及要定位高亮的那条消息。
    //主面板搜索下拉点了一条结果后经 CBalloonMainPanel::OpenHistoryWithSearch 调用。
    //  pMainPanel / pStore / ctype / cid / titleName：同 OpenForConversation。
    //  keywordUtf8：预填进右侧搜索框并按它筛选的关键字（UTF-8）；空串表示不预填。
    //  typeFilter： 预选的类型胶囊，取 balloonhistory::HistoryTypeFilter 的值；越界按「全部」。
    //  locateMsgid：大于 0 时，按本地库里这条消息的时间算出它在上述筛选条件下所在的页，翻过去、
    //               加定位高亮并滚到它（与「查看上下文」同一套高亮）；本地库里没有这条、或它已撤回
    //               时只打开、不定位。
    void OpenForConversationWithSearch(CBalloonMainPanel* pMainPanel,
                                       balloonhistory::CV2MsgStore* pStore,
                                       int ctype, int64_t cid, LPCTSTR titleName,
                                       const std::string& keywordUtf8, int typeFilter,
                                       int64_t locateMsgid);

    //从主面板系统菜单「聊天记录管理」打开（docs/聊天记录管理器方案.md §3.4）。窗口已开时只是还原
    //并置前，不换会话。新打开时，上次查看的会话仍在左栏里就选中它，否则选中左栏第一个会话，再经
    //主面板向服务端补拉该会话；左栏为空时右侧显示空状态。
    //  pMainPanel：主面板（名字、头像、补拉请求都经它，引用不持有，须覆盖本窗生命周期）。
    //  pStore：    本地消息库（引用不持有）。
    void OpenManager(CBalloonMainPanel* pMainPanel, balloonhistory::CV2MsgStore* pStore);

    //打开聊天记录管理器（同 OpenManager），并把关键字填进左栏搜索框立即跨会话搜索。主面板搜索
    //下拉「消息」标签底部的「在聊天记录管理器中查看全部」经 CBalloonMainPanel::OpenHistoryManagerWithSearch
    //调用。左栏折叠着时先展开；窗口已开且限定了搜索范围时恢复为全部会话。
    //  pMainPanel / pStore：同 OpenManager。
    //  query：预填的搜索词（UTF-16）；空串时只打开，不搜索。
    void OpenManagerWithSearch(CBalloonMainPanel* pMainPanel, balloonhistory::CV2MsgStore* pStore,
                               const std::wstring& query);

    //打开（或重定向到）某会话并按关键字筛选（同 OpenForConversationWithSearch，类型为「全部」），再跳到
    //timeMs 所在那天、定位到那天及以前最近的一条命中。本地还没有那天的记录时，与「跳到这一天」一样
    //向服务端往前拉取到那天为止再定位。主面板搜索下拉「服务器上的聊天记录」点了一个会话后经
    //CBalloonMainPanel::OpenHistoryAtTime 调用。
    //  pMainPanel / pStore / ctype / cid / titleName / keywordUtf8：同 OpenForConversationWithSearch。
    //  timeMs：要跳到的时刻（Unix 毫秒），一般是该会话在服务器上最近一条命中的时间；不大于 0 时只打开。
    void OpenForConversationAtTime(CBalloonMainPanel* pMainPanel, balloonhistory::CV2MsgStore* pStore,
                                   int ctype, int64_t cid, LPCTSTR titleName,
                                   const std::string& keywordUtf8, int64_t timeMs);

    //左栏的数据来源有了变化（主面板会话缓存更新、群列表到达、离开群、本地库增删消息等）时调用：
    //窗口开着时稍后合并刷新一次左栏，短时间内的多次通知只刷新一次；未开窗时忽略。
    //刷新时当前会话已不在左栏里（例如本人刚离开该群）就切到相邻的会话。
    void NotifyConversationsChanged();

    //若本窗正打开且显示的就是 (ctype,cid) 这个会话，则局部刷新（重查本地库）。供主面板
    //在回填同步落库后调用，让新拉到的消息即时出现；会话不匹配 / 未开窗则忽略。
    //本地库变了，左栏也一并安排一次刷新（见 NotifyConversationsChanged）。
    void RefreshIfConversation(int ctype, int64_t cid);

    //本窗是否正打开、且显示的就是 (ctype,cid) 这个会话。主面板在本人离开某群、删掉该群的
    //本地记录后据此决定要不要关掉本窗。
    //@return 窗口已创建且当前会话匹配时返回 true
    bool IsShowingConversation(int ctype, int64_t cid) const;

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
     *  一段语音下载有了结果（主面板收到 FMG_MSG_V2_VOICE_DOWNLOAD_RESULT 后按 md5 广播进来，语音消息第 4b 步）。
     *  本窗口发起过这一段时把它移出「下载中」并刷新列表（成功后语音条变为就绪，失败时变回「需要下载」）；
     *  没发起过时也刷新——别处下好了同一段语音，本窗口的语音条同样可以直接播放。
     *  @param md5 语音文件内容 MD5
     *  @param ok  是否下载成功
     */
    void OnVoiceDownloadFinished(const std::string& md5, bool ok);

    /**
     *  一条语音在别处（某个聊天窗、或本窗口自己）播放过了（主面板写库后转来，语音消息第 6a 步）。
     *  会话与本窗口当前显示的相符时记进「本窗口已知已听」的集合（翻页、重建后仍不画红点），并把当前页里
     *  同一条语音条的红点去掉；窗口没打开或会话不符时什么也不做。
     *  @param ctype 那条语音所在会话的类型（1 单聊 2 群聊）
     *  @param cid   会话 ID（单聊为对端 userid，群聊为群 ID）
     *  @param msgid 那条语音的服务器 msgid
     */
    void OnVoicePlayedElsewhere(int ctype, int64_t cid, long long msgid);

    // 以下为 balloonvoice::IVoicePlayListener 的实现（语音消息第 4b 步）
    /**
     *  播放进度：更新这条语音的语音条喇叭帧。
     *  @param key      正在播的语音
     *  @param playedMs 已播放的毫秒数
     *  @param totalMs  总毫秒数
     */
    virtual void OnVoicePlayProgress(const balloonvoice::VoicePlayKey& key, int64_t playedMs,
                                     int64_t totalMs) override;
    /**
     *  播放结束：语音条退出播放中。消息记录窗不自动连播。
     *  @param key    结束的语音
     *  @param reason 取值见 balloonvoice::VoicePlayEnd
     */
    virtual void OnVoicePlayEnded(const balloonvoice::VoicePlayKey& key, int reason) override;

    /**
     *  群文件信息缓存表有了变化（主面板拉齐了某个群、收到群文件增删推送、本人离开了某个群）：窗口开着且停在
     *  文件页时，稍后合并刷新一次文件页（短时间内的多次通知只刷新一次）；其余情况忽略。
     */
    void NotifyGroupFileCacheChanged();

    /**
     *  一笔群文件下载有了结果（主面板收到 FMG_MSG_V2_GROUP_FILE_DOWNLOAD_RESULT 后转来，不论是谁发起的）。
     *  本窗口文件页发起的那一笔：移出「下载中」，失败时提示；无论是谁发起的，停在文件页时都刷新一次，
     *  下载成功的群文件随之显示为「已下载」（落地路径已由主面板登记进缓存表）。
     *    groupid / entryid：群与群文件条目。ok：是否成功。localPath：成功时的落地路径。
     */
    void OnGroupFileDownloadFinished(int64_t groupid, const std::string& entryid, bool ok,
                                     const std::wstring& localPath);

    /**
     *  一笔群文件下载的进度（主面板转来）。是本窗口文件页发起的、且那一行在当前页时，就地更新这一行的状态文字。
     *    groupid / entryid：群与群文件条目。received / total：已收与总字节数。
     */
    void OnGroupFileDownloadProgress(int64_t groupid, const std::string& entryid, int64_t received, int64_t total);

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
        //聊天记录管理器左栏会话行的右键菜单
        MESSAGE_HANDLER(WM_HISTORY_SHOW_MGR_MENU, OnShowManagerMenuMsg)
        //聊天记录管理器左栏跨会话搜索的结果（后台线程投递）
        MESSAGE_HANDLER(WM_HISTORY_MGR_SEARCH_DONE, OnManagerSearchDoneMsg)
        //文件 / 图片页一行（一张图）的右键菜单与「⋯」菜单
        MESSAGE_HANDLER(WM_HISTORY_SHOW_LIB_MENU, OnShowLibraryMenuMsg)
        //时间轴导航条上按下的年标题行或月份行
        MESSAGE_HANDLER(WM_HISTORY_TIMELINE_NAV_CLICK, OnTimelineNavClickMsg)
        //时间轴模式的统计结果（后台线程投递）
        MESSAGE_HANDLER(WM_HISTORY_TIMELINE_STATS_DONE, OnTimelineStatsDoneMsg)
        //本机存储占用页的统计或清理结果（后台线程投递）
        MESSAGE_HANDLER(WM_HISTORY_STORAGE_DONE, OnStorageDoneMsg)
        //导出的进度与结果（后台导出线程投递）
        MESSAGE_HANDLER(WM_HISTORY_EXPORT_PROGRESS, OnExportProgressMsg)
        MESSAGE_HANDLER(WM_HISTORY_EXPORT_DONE, OnExportDoneMsg)
        //帖子卡片缩略图的下载结果，以及看这些图片时看图窗口的翻页
        MESSAGE_HANDLER(WM_HISTORY_CARD_IMAGE_DONE, OnCardImageDoneMsg)
        MESSAGE_HANDLER(WM_HISTORY_CARD_NAVIGATE, OnCardNavigateMsg)
        //左键按下旁听：记下这一下是否落在消息列表里，Ctrl+C 据此判断复制的是列表里的
        //选区还是搜索框里的文字。只旁听、不消费。
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDownMsg)
        //Ctrl+C：最近一次点在列表里且有选区时复制选区并吞掉按键，否则照常交给搜索框。
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDownMsg)
        MESSAGE_HANDLER(WM_CHAT_OPEN_LINK, OnChatOpenLinkMsg)
        //语音条上点了一下（ChatVoiceStrip 投递）：播放、停止或下载
        MESSAGE_HANDLER(WM_CHAT_VOICE_ACTION, OnChatVoiceActionMsg)
        //代码片段卡片请求打开代码查看窗
        MESSAGE_HANDLER(WM_CHAT_OPEN_CODE_VIEWER, OnChatOpenCodeViewer)
        //Markdown 卡片请求打开 Markdown 预览窗口
        MESSAGE_HANDLER(WM_CHAT_OPEN_MARKDOWN_PREVIEW, OnChatOpenMarkdownPreview)
        //缩略图后台解码完成的通知（本窗口在 BuildWindow 里登记为接收窗口）。
        MESSAGE_HANDLER(WM_CHAT_IMAGE_READY, OnImageReadyMsg)
        //销毁时从图片缓存注销，并把窗口位置与大小存进账号配置。只旁听、不消费，
        //DuiFrameWindow 照常收尾。
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        //位置或大小变化后记下还原态的窗口矩形，关窗时保存。只旁听、不消费。
        MESSAGE_HANDLER(WM_WINDOWPOSCHANGED, OnWindowPosChangedMsg)
        //往前拉取更早消息的超时定时器、两个搜索框停止输入后才查询的延迟定时器，以及左栏重建与
        //补拉的合并定时器。只认这几个定时器 id，其余交回基类。
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

    //「导出」按钮：先弹导出选项浮层（docs/聊天记录管理器方案.md §12.7），选定后由 BeginExport 继续。导出当前会话时，
    //服务端还有更早的消息没拉下来先问用户：先加载全部再导出 / 只导出已加载的 / 取消；选"先加载全部"时记下导出目标
    //与选项、开始往前拉取，拉到最早一条后由 OnExportReadyMsg 导出。
    void ExportHistory();

    //清空当前查看的会话（底部按钮与行右键菜单共用）：交给主面板的 RequestClearConversation，
    //确认框、离线判断与发请求都在那里，确认框弹在本窗口上；成功后主面板经 RefreshIfConversation
    //刷新本窗口。调试入口没有主面板时什么都不做
    void RequestClearConversation();

    //服务端还有更早的消息时问用户怎么导出（三个按钮的 TaskDialog）。
    //@return 取值见 .cpp 里的 HistoryExportChoice：先加载全部 / 只导出已加载的 / 取消
    int AskUnloadedBeforeExport();

    //为「先加载全部再导出」开始往前拉取：已有一轮在途就等它，否则立即发起一轮。
    void StartFetchAllForExport();

    //WM_HISTORY_EXPORT_READY：「先加载全部再导出」拉取完毕，按记下的选项导出。
    LRESULT OnExportReadyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_HISTORY_SHOW_MGR_MENU：事件解栈后弹出左栏会话行的右键菜单（见 ShowManagerConvMenu）。
    LRESULT OnShowManagerMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_HISTORY_MGR_SEARCH_DONE：后台线程查完了一次左栏跨会话搜索。是最新一次请求的结果就记下并
    //重填左栏，旧请求的结果丢弃；无论哪种都释放 lParam 指向的结果对象。
    LRESULT OnManagerSearchDoneMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

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

    // ---- 时间轴模式（docs/聊天记录管理器方案.md §11.7，实现在 BalloonHistoryWindowTimeline.cpp） ----

    //切换「列表 | 时间轴」：改模型的关键字解释方式（时间轴模式下只高亮、不筛选），切换导航条与胶囊，
    //清掉定位高亮与命中位置，完整刷新。取值不变时什么都不做。
    //  timeline：true 时间轴，false 列表。
    void SetTimelineMode(bool timeline);

    //新打开窗口时调用：回到列表模式，清空命中位置与导航数据（不刷新界面，界面随后由 BuildWindow 重建）。
    void ResetTimelineState();

    //按当前条件重新统计导航数据（各月条数与命中数）与命中位置。不在时间轴模式时清空两者。
    //有主面板的后台查询线程时只提交请求、不等结果，结果由 OnTimelineStatsDoneMsg 接收；没有时（调试入口）
    //在界面线程上直接统计。
    //  force：true 时即便条件没变也重新统计（完整刷新时数据可能变了）。
    //  返回：在界面线程上直接统计、关键字或通配符开关与上次统计时不同、并且找到了命中时返回 true —— 调用方据此
    //        跳到第一处命中。交给后台线程时恒返回 false，跳转在结果到达后进行。
    bool RefreshTimelineStats(bool force);

    //采用一次统计结果：换上导航数据与命中位置（关键字没变时保留当前那一处），记日志。
    //  resp：统计结果（kV2SearchTimelineStats），只读。
    //  keywordChanged：这次统计的关键字或通配符开关与上一次不同。
    //  返回：keywordChanged 为 true、关键字筛选生效且找到了命中时返回 true（应跳到第一处命中）。
    bool ApplyTimelineStats(const balloonhistory::V2SearchResponse& resp, bool keywordChanged);

    //WM_HISTORY_TIMELINE_STATS_DONE：采用后台线程交回的统计结果，更新命中条与导航条；刚换了关键字时跳到第一处
    //命中，否则按只换页刷新一次列表，让命中的行节点标黄。过期（序号不是最新）的结果直接释放。
    LRESULT OnTimelineStatsDoneMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //按 m_timelineYears 重建导航条：高亮当前列表所在的月份，并展开它所在的那一年。
    void RebuildTimelineNav();

    //BuildViewData 的最后一步（只在时间轴模式下调用）：各行标记为时间轴样式；日期分组头改写成完整日期，
    //右侧写星期几与当天条数（按当前页所跨的日期查本地库）；命中关键字的行把节点标黄。
    //  data：[入出参] 刚建好的整页渲染数据，m_rows 与模型当前页各条一一对应。
    void DecorateTimelineRows(balloonhistory::HistoryViewData& data);

    //按 m_hits 更新命中条（时间轴模式且有关键字时显示）。
    void UpdateHitBar();

    //F3 / Shift+F3 与命中条的两个按钮：跳到下一处 / 上一处命中。还没有当前处时从当前页开始找。
    //  next：true 下一处（列表里往下、时间更早），false 上一处。
    void GoToHit(bool next);

    //跳到第 index 处命中：翻到它所在的页，高亮并滚到那一行，更新命中条。
    //  index：m_hits 里的下标；越界时什么都不做。
    void JumpToHit(int index);

    //导航条上点了某个月份：先清掉时间筛选（与「跳到这一天」相同），再翻到那个月最晚的一条；本地还没有
    //那个月的记录而服务端还有更早的消息时，沿用「跳到这一天」的往前拉取机制。
    //  year / month：年、月。
    void JumpToMonth(int year, int month);

    //左键按下时看落点是不是导航条上的年标题行或月份行（不含右侧滚动条的命中带）；是的话投递
    //WM_HISTORY_TIMELINE_NAV_CLICK，等鼠标事件处理完再展开 / 收起或跳转。
    //  pt：客户区坐标。
    //  返回：落点在导航条的某一行上、已投递时返回 true。
    bool HandleTimelineNavClick(POINT pt);

    //WM_HISTORY_TIMELINE_NAV_CLICK：年标题行展开 / 收起，月份行跳到那个月。
    LRESULT OnTimelineNavClickMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

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

    //把搜索输入里 from:名字 换成 userid（搜索语法，docs/聊天记录管理器方案.md §12.3）：候选人的显示名经
    //ResolveSenderName 解析后按 MatchSearchFromIds 匹配。
    //@param keywordUtf8          搜索框里的输入（UTF-8）
    //@param wildcard             是否勾选了通配符（只影响解析时丢弃哪些词，不影响名字匹配）
    //@param acrossConversations  true 时候选人是本地库里发过言的所有人（跨会话搜索、链接页、与我有关页，结果缓存在
    //                            m_allSenders），false 时是右侧当前会话里发过言的人
    //@return 匹配上的 userid；输入里没有 from: 时返回空表
    std::vector<int64_t> ResolveSyntaxFromIds(const std::string& keywordUtf8, bool wildcard, bool acrossConversations);

    //右侧会话内搜索：按模型当前的关键字重新解析 from:名字，结果交给 HistoryModel::SetSyntaxFromIds。
    //每次 m_model.SetKeyword / SetWildcard 之后、刷新列表之前调用。
    void SyncSyntaxFromIds();

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

    /**
     *  WM_CHAT_VOICE_ACTION 处理（语音消息第 4b 步）：语音条上点了一下。wParam 为 ChatVoiceAction（只会是播放 / 停止），
     *  lParam 为堆上的 balloonchatwnd::VoiceActionRequest*，处理后 delete。按 DecideVoiceClick 播放、停止、提示正在
     *  下载或下载。
     */
    LRESULT OnChatVoiceActionMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_OPEN_CODE_VIEWER 处理（docs/代码片段方案.md 第 3.4 节）：代码片段行的卡片上点了正文或「查看全部」。
     *  lParam 为堆上的 balloonchatwnd::CodeViewerOpenRequest*，打开（或调到前台）代码查看窗后 delete。
     */
    LRESULT OnChatOpenCodeViewer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_OPEN_MARKDOWN_PREVIEW 处理（docs/Markdown消息方案.md 第 12 节）：Markdown 消息行的卡片被点了。
     *  lParam 为堆上的 balloonchatwnd::MarkdownPreviewOpenRequest*，打开（或激活）预览窗口后释放。
     */
    LRESULT OnChatOpenMarkdownPreview(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  在当前列表里找某条语音的语音条控件。
     *  @param msgid 语音消息的服务器 msgid
     *  @return 语音条控件（借用，属于列表）；当前页没有这条时为 NULL
     */
    balloonchatwnd::ChatVoiceStrip* FindVoiceStrip(long long msgid);

    /**
     *  在本窗口播放了一条对方发来、还没听过的语音（语音消息第 6a 步）：投 FMG_MSG_VOICE_PLAYED 给主面板，由主面板写
     *  本地库并让已打开的聊天窗去掉同一条的红点；没有主面板（调试入口）时直接写本地库。
     *  @param msgid 那条语音的服务器 msgid，会话取本窗口当前显示的会话
     */
    void MarkVoicePlayedEverywhere(long long msgid);

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

    // ---- 帖子卡片与补充卡片的图片（docs/帖子群方案.md 14.5） ----

    /**
     *  为带图片的帖子卡片行填写缩略图（m_cardThumbs、m_cardThumbExtra）：最多 kHistoryCardMaxThumbs 张，状态按
     *  ResolveHistoryThumb 判定。本机没有、也没有下载失败过的图片随即开始下载（帖子群窗口看过的图已在同一个缓存里）。
     *    row：   [出参] 要填写的行数据。
     *    images：卡片带的全部图片（ParseContentBrief 取出的 m_threadImages）。
     *    maxShown：最多填几张：列表里为 kHistoryCardMaxThumbs（多出的写「+N」），「图片」分类网格里为全部（每张一格）。
     */
    void FillCardThumbs(balloonhistory::HistoryRowView& row, const std::vector<balloonhistory::HistoryCardImage>& images,
                        size_t maxShown);

    /**
     *  按原图的 fileid 与 md5 下载一张卡片图片到聊天图片缓存，结果经 WM_HISTORY_CARD_IMAGE_DONE 回来。同一张图
     *  （按 md5）正在下载时不重复发起；缺少 fileid 或 md5、窗口还没建好时什么也不做。
     */
    void RequestCardImage(const balloonhistory::HistoryCardImage& image);

    /**
     *  单击了当前页第 row 行卡片下的第 thumb 张缩略图：记下这张卡片的全部图片，打开看图窗口从这一张开始看，可以
     *  左右翻看；本机还没有这张图时先下载，下完再打开。
     */
    void OpenRowCardImage(int row, int thumb);

    //按 m_cardViewImages / m_cardViewIndex 打开看图窗口；本机没有那张图时发起下载并登记「下完要打开」
    void ShowCardViewerImage();

    /**
     *  看一张帖子卡片里的图片：取卡片的全部图片，从第 index 张开始看，可以左右翻看。消息记录列表、「图片」分类网格与
     *  聊天记录管理器的「图片」页共用。
     *    msgtype / content：卡片消息的类型与本地库 content（ParseContentBrief 从中取图片）。
     *    index：从第几张开始（0 起）；越界时什么也不做。
     *    msgid：卡片消息的 msgid，只用于日志。
     */
    void OpenCardImages(int msgtype, const std::string& content, int index, int64_t msgid);

    /**
     *  算出一张卡片图片的缩略图状态（本机路径、显示状态、提示），不发起下载。
     *    image：卡片里的一张图片。
     *  @return 缩略图的渲染数据；调试入口没有登录态时路径为空、状态为破图
     */
    balloonhistory::HistoryCardThumbView ResolveCardThumbView(const balloonhistory::HistoryCardImage& image) const;

    //聊天记录管理器「图片」页的一项如果是帖子卡片里的一张图片，取出那张图片的下载信息（文件 ID、MD5、大小）
    static balloonhistory::HistoryCardImage CardImageOfLibraryItem(const balloonhistory::HistoryLibraryItem& item);

    //WM_HISTORY_CARD_IMAGE_DONE：记下结果；等着看的那张图下完就打开看图窗口；随后刷新本页，缩略图换成图片或破图
    LRESULT OnCardImageDoneMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_HISTORY_CARD_NAVIGATE：看图窗口里按了 ← / →，在同一张卡片的图片里前后翻，到头或到尾时停在原处
    LRESULT OnCardNavigateMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // ---- 聊天记录管理器左栏（docs/聊天记录管理器方案.md） ----

    //右侧整套界面的根容器（kIdHistoryBody）；窗口还没建好时返回 NULL。提示条挂在它下面，
    //多选操作栏显隐后按它重排 —— 客户区根容器现在是左右分栏，不再是这个 vbox。
    balloonwjui::DuiControl* BodyRoot() const;

    //左栏用的本地库：真实会话下是主面板注入的本地库，调试入口下是预置的假库；都没有时返回 NULL。
    balloonhistory::CV2MsgStore* ManagerStore();

    //按左栏是否折叠设置窗口的最小尺寸。展开时左栏要占宽度，最小宽度 = 左栏最小宽度 + 分隔条 +
    //右侧最小宽度；折叠时退回原来消息记录窗的最小宽度。
    //  growIfNeeded：为 true 且窗口当前比新的最小宽度窄时，把窗口加宽到最小宽度（保持左上角不动）。
    void ApplyManagerMinSize(bool growIfNeeded);

    //重新合并左栏数据（主面板会话缓存 + 本地库会话汇总）并逐项解析名字，结果写进 m_mgrItems。
    //只改数据、不碰控件，窗口还没建好时也可以调用（OpenManager 据它决定打开哪个会话）。
    void RebuildManagerItems();

    //RebuildManagerItems 之后重填两棵树。重建前在左栏里的当前会话重建后不见了（离开群、群被
    //解散等），就按 ResolveManagerSelection 切到相邻的会话。
    void RebuildManagerList();

    //按类型筛选、名字搜索词与搜索范围重填左栏的会话树；有搜索词时再跨会话搜聊天内容填命中树。
    //填完设置两段的显隐与高度，并把选中项对齐到右侧当前的会话。
    void FillManagerTrees();

    //把左栏会话树的选中项对齐到右侧当前显示的会话（不在树里时清掉选中；会话树里没有而命中树里
    //有时选中命中树那一行）。
    //  scrollIntoView：为 true 时选中项在滚动视口之外就把它滚进来。只在用户明确换了会话、或刚打开
    //                  窗口时传 true；新消息到达等被动的刷新传 false，不打断用户在左栏里的滚动浏览。
    void SyncManagerSelection(bool scrollIntoView);

    //切换右侧显示的会话：撤掉定位高亮、多选、往前拉取与待导出任务，搜索条件恢复为初始状态
    //（与 OpenForConversation 换会话时相同），按 keywordUtf8 预填关键字，刷新右侧，并安排补拉。
    //  ctype / cid：  要显示的会话。与当前会话相同且没有关键字时只对齐左栏选中项。
    //  name：         会话名（标题区显示）。
    //  keywordUtf8：  预填的关键字（左栏搜索命中点进来时带上搜索词）；空串表示不预填。
    //  keywordWildcard：预填的关键字是否按通配符解释（取左栏「通配符」复选框的状态）；右侧复选框随之
    //                 勾选或取消。keywordUtf8 为空时不使用，右侧复选框保持原状
    void SwitchConversation(int ctype, int64_t cid, const CString& name, const std::string& keywordUtf8,
                            bool keywordWildcard = false);

    //用户点了右侧搜索框旁的「通配符」复选框：写进模型，有关键字时投递一次完整刷新，按新的解释方式重查。
    //  wildcard：复选框的新状态（true 勾选）。与模型当前取值相同时什么都不做
    void ApplyRightWildcard(bool wildcard);

    //由程序改动右侧的「通配符」状态（从左栏命中点进来、从主面板搜索下拉打开等）：写进模型，并把复选框
    //改成同样的勾选状态（不发通知）。不刷新，由调用方随后刷新
    void SyncRightWildcard(bool wildcard);

    //用户点了左栏范围行的「通配符」复选框：记下新状态；左栏正在搜索时按同一个搜索词重新跨会话搜索。
    //  wildcard：复选框的新状态（true 勾选）。与当前状态相同时什么都不做
    void SetManagerWildcard(bool wildcard);

    //由程序改动左栏的「通配符」状态：记下新状态，并把复选框改成同样的勾选状态（不发通知）。不重新搜索。
    //@return 状态确有变化时返回 true
    bool SyncManagerWildcard(bool wildcard);

    // ---- 按一条记录执行的行操作（聊天记录页与文件 / 图片页共用；按行的版本取出记录后转调） ----

    //对一条记录发起下载（图片走 ImageTransfer、文件走 FileTransfer），规则同 StartRowDownload。
    bool StartRecordDownload(const balloonhistory::HistoryRecord& rec, bool viewAfter, bool refreshAfter);

    //取消一条记录正在进行的下载，规则同 CancelRowDownload。
    void CancelRecordDownload(const balloonhistory::HistoryRecord& rec);

    //把一个本地文件另存到用户选定的位置（整字节拷贝）。fileName 作默认文件名，为空时用「文件」。
    void SaveLocalFileAs(const CString& fileName, const CString& localPath);

    //转发一条记录，规则同 ForwardRow。
    void ForwardRecord(const balloonhistory::HistoryRecord& rec);

    //跳到聊天窗并定位到一条记录，规则同 LocateRow。
    void LocateRecord(const balloonhistory::HistoryRecord& rec);

    //打开一条图片记录的大图（本地没有原图时先下载、下完自动打开），规则同 ViewRowImage。
    void ViewRecordImage(const balloonhistory::HistoryRecord& rec);

    // ---- 聊天记录管理器的文件页与图片页（BalloonHistoryWindowLibrary.cpp，docs/聊天记录管理器方案.md §11.5、§11.6） ----

    //切换左栏标签（取值见 balloonhistory::HistoryManagerTab）：显示对应的右侧页面，左栏列表随之换成会话列表或按
    //会话计数的列表；切到文件页时请主面板拉齐群文件信息缓存。与当前标签相同时什么都不做。
    void SwitchManagerTab(int tab);

    //从「消息记录」各入口打开某个会话时调用：停在文件 / 图片页的话切回聊天记录页（不刷新，由调用方随后刷新）。
    void EnsureChatTab();

    //刷新文件 / 图片页。scope 取值见 HistoryLibraryRefreshScope：只换页、重新统计并取页、连同各会话的计数与
    //左栏一起刷新。不在文件 / 图片页时什么都不做。
    void RefreshLibrary(int scope);

    //把文件 / 图片页模型的当前状态翻译成界面渲染数据。
    balloonhistory::HistoryLibraryViewData BuildLibraryViewData() const;

    //链接页、与我有关页一行的显示内容：链接页取第一个链接的域名与完整链接，与我有关页取正文预览；第二行是来源。
    //@param item      查询结果的一行
    //@param keywordW  要高亮的搜索输入（UTF-16）；关键字筛选不生效时传空串
    //@param nowMs     当前时间（Unix 毫秒），排时间文字用
    balloonhistory::HistoryLibraryTextRowView BuildLibraryTextRow(const balloonhistory::HistoryLibraryItem& item,
                                                                  const std::wstring& keywordW, int64_t nowMs) const;

    //左栏在文件 / 图片页时的列表：第一行「全部会话」，其余是有内容的会话及条数。
    void FillLibraryManagerTree();

    //左栏选中项对齐到文件 / 图片页当前看的会话（「全部会话」或某个会话）。
    void SyncLibrarySelection(bool scrollIntoView);

    //文件 / 图片页里左栏某一行被单击：只看那个会话（或全部会话）。
    void OnLibraryConvClicked(int nodeId);

    //处理文件 / 图片页里控件的通知。已处理返回 true；不是本页的控件返回 false，交回原来的分发。
    bool HandleLibraryNotify(const balloonwjui::DuiNotify* pNotify);

    //处理文件 / 图片页的定时器（文件名搜索的延迟、缓存变化的合并刷新）。已处理返回 true。
    bool HandleLibraryTimer(UINT_PTR timerId);

    //按文件名搜索框当前的文字（去掉首尾空白）设置关键字，并投递一次刷新。
    void ApplyLibrarySearch();

    //文件 / 图片页当前页第 index 行（张）；越界时返回 NULL。
    const balloonhistory::HistoryLibraryItem* LibraryItemAt(int index) const;

    //一行在本机的可用路径：聊天文件按记录查（ResolveRecordLocalPath），群文件取缓存表登记的落地路径并核对文件还在。
    //@return 本机有可用的文件（文件夹记录为文件夹）时返回 true，outPath 为路径；否则返回 false 并清空 outPath
    bool ResolveLibraryItemLocal(const balloonhistory::HistoryLibraryItem& item, CString& outPath) const;

    //一行的状态：本机有、正在下载、可以下载、无法下载（取值见 HistoryLibraryFileState）；outProgress 为下载进度
    int LibraryItemState(const balloonhistory::HistoryLibraryItem& item, int& outProgress) const;

    //一行所在会话的显示名（群名或联系人名）
    CString LibraryConversationName(int ctype, int64_t cid) const;

    //文件 / 图片页一行的几个操作。index 为当前页行下标（张），越界时什么都不做
    void OpenLibraryItem(int index);         //打开文件（文件夹记录打开文件夹）
    void OpenLibraryItemFolder(int index);   //在资源管理器里定位
    void ToggleLibraryDownload(int index);   //下载；正在下载时取消
    void LocateLibraryItem(int index);       //聊天文件、图片跳到聊天里那一条；群文件打开群聊窗的群文件页
    void ActivateLibraryItem(int index);     //双击：本机有就打开（图片看大图），没有就下载
    void SaveLibraryItemAs(int index);       //另存为（本机有文件时）
    void CopyLibraryItemName(int index);     //复制文件名
    void DownloadLibraryPage();              //「下载本页全部」：本页里本机还没有的聊天文件与图片
    void OpenLibraryLink(int index);         //链接页：用浏览器打开这条消息的第一个链接（只放行 http / https）
    void CopyLibraryItemText(int index);     //链接页复制这条消息里的全部链接；与我有关页复制正文

    //左栏列表是不是「聊天」标签的会话列表（聊天，或「聊天」标签下选中了「与我有关」）
    bool ManagerShowsChatList() const;

    //左栏「聊天」标签列表第一行「与我有关」被单击：右侧换成与我有关页（docs/聊天记录管理器方案.md §12.5）。
    void EnterMentionsPage();

    //停在与我有关页时，左栏点了某个会话或命中：先回到聊天记录页（不刷新，由调用方随后切换会话并刷新）。
    void LeaveMentionsPage();

    //链接页、与我有关页一行的菜单：打开链接（链接页）、复制、定位到聊天、转发
    void ShowLibraryTextMenu(int index, POINT ptScreen);

    // ---- 聊天记录管理器的本机存储占用页（BalloonHistoryWindowStorage.cpp，docs/聊天记录管理器方案.md §12.6） ----

    //点了左栏最下面的「本机存储占用」：右侧换成存储页（第一次进入时把页面加进右侧容器）；还没有统计过时发起统计。
    void EnterStoragePage();

    //离开存储页，回到当前标签对应的页面并完整刷新一次。不在存储页时什么都不做。
    void LeaveStoragePage();

    //不刷新、只把存储页状态撤掉（其它入口随后会切换页面并刷新时用）。
    void ResetStorageMode();

    //当前账号的缓存目录与本地库路径；未登录（调试入口）时各目录为空串。
    balloonhistory::HistoryStorageDirs CurrentStorageDirs();

    //发起一次统计：有后台查询线程时交给它，结果经 WM_HISTORY_STORAGE_DONE 送回；调试入口直接在界面线程上算。
    void SubmitStorageStats();

    //清理一个会话（rowIndex 为存储页当前列表的行下标）或全部会话（rowIndex 为 -1）的缓存：先弹确认框，确认后交给
    //后台线程删除，删完重新统计。
    void CleanStorage(int rowIndex);

    //把当前统计结果翻译成存储页的渲染数据并局部更新页面。
    void ApplyStorageView();

    //处理存储页里控件与左栏入口的通知。已处理返回 true。
    bool HandleStorageNotify(const balloonwjui::DuiNotify* pNotify);

    //WM_HISTORY_STORAGE_DONE：采用统计结果并更新页面；清理结果提示删了多少个并重新统计。过期的结果直接释放。
    LRESULT OnStorageDoneMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // ---- 导出增强（BalloonHistoryWindowExport.cpp，docs/聊天记录管理器方案.md §12.7） ----

    //在「↓ 导出」按钮上方打开导出选项浮层（初始值为上一次的选择）。已有导出在执行时只提示稍候。
    void OpenExportPopup();

    //导出浮层的提交回调（静态转发到窗口实例）。
    static void OnExportPopupThunk(void* userdata, const balloonhistory::HistoryExportOptions& opts);

    //按导出选项询问保存位置（单个会话为文档路径，多个会话为文件夹，打包为 zip 文件），需要时先「加载全部」，
    //再交给 StartExportJob。
    void BeginExport(const balloonhistory::HistoryExportOptions& opts);

    //问保存位置。convCount 为要导出的会话个数。
    //@return 用户选定时返回 true，outTarget 为路径；取消返回 false
    bool AskExportTarget(const balloonhistory::HistoryExportOptions& opts, size_t convCount, std::wstring& outTarget);

    //在界面线程上查库、解析名字，把各会话整理成导出任务，再交给后台线程执行。
    void StartExportJob(const balloonhistory::HistoryExportOptions& opts, const std::wstring& target);

    //把一个会话的消息（时间倒序）整理成文档与附件。
    //  title：文档标题里的会话名；all：消息（新 → 旧）；docName：文档文件名；opts：导出选项
    balloonhistory::HistoryExportConvJob BuildExportConvJob(const CString& title,
                                                            const std::vector<balloonhistory::HistoryRecord>& all,
                                                            const std::wstring& docName,
                                                            const balloonhistory::HistoryExportOptions& opts);

    //取某个会话按导出时间范围的全部消息（新 → 旧，按覆盖区间只取可见的）。
    bool QueryExportRecords(int ctype, int64_t cid, int range, std::vector<balloonhistory::HistoryRecord>& out);

    //WM_HISTORY_EXPORT_PROGRESS：在提示条上显示导出进度。
    LRESULT OnExportProgressMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_HISTORY_EXPORT_DONE：等后台线程结束，提示结果，成功时可打开所在文件夹。
    LRESULT OnExportDoneMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //窗口销毁时让后台导出线程中止并等它结束。
    void StopExportThread();

    //弹出一行的菜单：打开、打开所在文件夹、下载 / 取消下载、另存为、定位、转发、复制文件名（按状态取舍）
    void ShowLibraryMenu(int index, POINT ptScreen);

    //WM_HISTORY_SHOW_LIB_MENU：事件解栈后弹出文件 / 图片页一行的菜单。
    LRESULT OnShowLibraryMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //文件 / 图片页里客户区落点 pt 所在的行（张）的下标；不在任何一行上或在列表视口之外时返回 -1。
    int LibraryIndexAtPoint(POINT pt) const;

    //文件页按落点更新哪一行处于悬停态（显示行内按钮）。pt 为 (-1,-1) 时全部撤销。
    void UpdateLibraryRowHover(POINT pt);

    //安排一次向服务端补拉（经主面板 RequestHistoryBackfill）：等 kMgrBackfillDelayMs 毫秒，期间
    //又切换了会话就只补拉最后那一个，免得快速连点左栏时连发一串请求。调试入口没有主面板，不发。
    void ScheduleBackfill(int ctype, int64_t cid);

    //折叠或展开左栏，并把新的最小尺寸、标题区的「»」、右侧列表的行宽一并更新。
    void SetManagerCollapsed(bool collapsed);

    //按左栏搜索框当前的文字（去掉首尾空白）更新搜索词并重填左栏。
    void ApplyManagerSearch();

    //按当前的搜索词与范围发起一次左栏跨会话搜索。真实会话下提交给主面板的后台查询线程，结果经
    //WM_HISTORY_MGR_SEARCH_DONE 回来；调试入口没有主面板，直接在界面线程上查预置的假库并写好结果。
    //搜索词为空时只清掉旧结果，不发请求。
    void SubmitManagerSearch();

    //解析左栏一行的显示名、右上角时间文字与头像。真实会话下经主面板（与会话列表同一口径）；
    //调试入口下名字取 ResolveSenderName（群聊为「群<id>」）、时间取 FormatTimeShort、没有头像。
    //  item：左栏的一项；outName / outTime：[出参] 名字与时间；outAvatar：[出参] 头像，借用不持有，可能为 NULL。
    void ResolveManagerRow(int ctype, int64_t cid, int64_t timestampMs,
                           CString& outName, CString& outTime, HBITMAP& outAvatar);

    //左栏会话树里某个节点被单击：切到那个会话。nodeId 不在树里时忽略。
    void OnManagerConvClicked(int nodeId);

    //左栏命中树里某个节点被单击：切到那个会话，并把左栏搜索词预填进右侧搜索框。
    void OnManagerHitClicked(int nodeId);

    //左栏会话树里某个节点被右键：弹出「打开聊天窗口 / 导出聊天记录 / 清空聊天记录」菜单并执行。
    //  nodeId：被右键的节点；ptScreen：菜单落点（屏幕坐标）。
    void ShowManagerConvMenu(int nodeId, POINT ptScreen);

    //打开搜索范围弹层（勾选若干会话作为左栏搜索的范围）。
    void OpenManagerScopePopup();

    //范围弹层提交后的回调转接函数（C 风格回调没有 this）。userdata 即本窗口；keys 为勾选的会话。
    static void OnScopePopupThunk(void* userdata, const std::vector<balloonhistory::HistoryConversationKey>& keys);

    //应用新的搜索范围：更新范围按钮、重填左栏。keys 为空表示不限。
    void ApplyManagerScope(const std::vector<balloonhistory::HistoryConversationKey>& keys);

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
    //为导出把往前拉取重新开始过几次，与 kMaxPendingJumpRestarts 共用同一个上限
    int                          m_pendingExportRestarts;
    //「先加载全部再导出」时这一次的导出选项（导出浮层选的，docs/聊天记录管理器方案.md §12.7）
    balloonhistory::HistoryExportOptions m_pendingExportOptions;
    //上一次在导出浮层里选的选项：下次打开浮层时作为初始值（窗口打开期间保持）
    balloonhistory::HistoryExportOptions m_lastExportOptions;
    //导出选项浮层（DuiPopupHost 子类，窗口自己持有）
    balloonhistory::CHistoryExportPopup m_exportPopup;
    //执行导出的后台线程；没有在导出时不可 join。窗口销毁时置取消标志并等它结束
    std::thread                  m_exportThread;
    //通知后台导出线程中止（窗口销毁时置 true）
    std::atomic<bool>            m_exportCancel;
    //是否正有一次导出在后台执行（期间再点导出只提示稍候）
    bool                         m_exportRunning;
    HWND                         m_hMainDlg;      //主面板句柄（仅调试入口下关闭即退出用；真实打开为 NULL）
    CBalloonMainPanel*           m_pMainPanel;    //主面板（真实会话下解析昵称用，引用不持有；调试入口为 NULL）

    balloonhistory::HistoryModel m_model;         //浏览状态 / 分页模型（驱动列表）
    balloonhistory::CV2MsgStore  m_seedStore;     //调试入口用的 in-memory 预置本地库
    CString                      m_titleName;     //对话名（标题“消息记录 · <名>”用）
    std::map<int64_t, CString>   m_senderNames;   //调试期 fromid→昵称映射
    //跨会话 from:名字 的候选人：本地库里发过言的所有人（CV2MsgStore::QueryAllSenders），第一次用到时读出并
    //缓存到窗口关闭；扫整张表要几十毫秒，不在每次搜索时重读。新来的发言人要到下次打开窗口才进候选表
    std::vector<int64_t>         m_allSenders;
    bool                         m_allSendersLoaded;   //m_allSenders 是否已读出

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
    //语音（第 4b 步）：本窗口发起、还在下载的语音 md5。行数据据此把语音条画成「下载中」；下载结果到达时移除
    std::set<std::string>        m_downloadingVoices;
    //本窗口正在播放的语音 msgid；0 表示没有在播放。行数据据此画播放中，刷新列表后不会丢
    int64_t                      m_playingVoiceMsgid;
    //本窗口里播放过的语音 msgid：本地库已标为已听，但模型里缓存的行要等下次查询才更新，期间据此不画红点
    std::set<int64_t>            m_playedVoices;
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
    //帖子卡片图片的下载（docs/帖子群方案.md 14.5），都只在本窗口存活期间有效，窗口销毁时清空：
    //请求号 → 那张图的 md5
    std::map<uint32_t, std::string> m_cardRequests;
    //正在下载的卡片图片（按 md5 记），据此画「下载中」并避免重复下载
    std::set<std::string>        m_cardDownloading;
    //下载失败过的卡片图片（按 md5 记）：缩略图画破图，不再自动重试；单击看图时重新下载
    std::set<std::string>        m_cardFailed;
    //下一个下载请求号，从 1 起递增
    uint32_t                     m_nextCardRequest;
    //已为卡片图片的下载结果投递过刷新、还没执行：一批结果只刷新一次
    bool                         m_cardRefreshPosted;
    //正在看图的那张卡片的全部图片与当前看到第几张；没在看卡片图片时为空
    std::vector<balloonhistory::HistoryCardImage> m_cardViewImages;
    size_t                       m_cardViewIndex;
    //下完就要打开看图窗口的那张卡片图片的 md5；空串表示没有这样的待办
    std::string                  m_cardPendingViewMd5;
    //「图片」分类网格里每一格对应的记录与帖子卡片里的第几张（ExpandHistoryGridCells 的结果）；第 i 格的编号为
    //kIdHistoryGridBase + i。每次建网格时重算，不是网格模式时为空
    std::vector<balloonhistory::HistoryGridCellRef> m_gridCells;
    //最近一次左键按下是否落在消息列表的视口里。true 时 Ctrl+C 复制列表里的选区；
    //false（点在搜索框等别处）时 Ctrl+C 交还给获得焦点的控件。
    bool                         m_lastPressInList;
    //当前这次行右键菜单里的「复制」是否复制选区：弹菜单前按右键落点是否在选区上算好，
    //执行「复制」时读取并复位。
    bool                         m_menuCopySelection;

    //标题栏左侧图标位图（程序图标合成到标题栏底色上）。DuiFrameWindow::SetIcon 只
    //借用不释放，故本类持有、析构时 DeleteObject。
    HBITMAP                      m_hTitleIcon;
    //任务栏与 Alt+Tab 的程序图标（2026-10-07，UI 约定第 10 条：没有所有者、会出现在任务栏的窗口另设任务栏图标）；本对象持有，析构时释放
    balloonmain::ProgramWindowIcons m_taskIcons;

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

    // ---- 聊天记录管理器左栏（docs/聊天记录管理器方案.md） ----

    //左栏合并后的全部会话（MergeManagerConversations 的结果，m_name 已填好），按最后时间倒序。
    //RebuildManagerList 整体替换；窗口关闭后保留，下次打开时重建
    std::vector<balloonhistory::ManagerConversationItem> m_mgrItems;
    //左栏会话树的节点 id → m_mgrItems 下标。FillManagerTrees 重填时整体替换
    std::map<int, size_t>        m_mgrConvNodes;
    //左栏搜索「聊天记录」一段当前显示的命中（m_mgrHitsRaw 按类型筛选后的结果）
    std::vector<balloonhistory::ConversationHit> m_mgrHits;
    //最近一次采纳的跨会话搜索结果（未按类型筛选）。类型筛选变化时据它重填，不必重新查库
    std::vector<balloonhistory::ConversationHit> m_mgrHitsRaw;
    //m_mgrHitsRaw 对应的搜索词；与 m_mgrQuery 不同表示结果已过期、新的还没回来
    std::wstring                 m_mgrHitsQuery;
    //最近一次提交给后台线程的请求序号；结果回来时只采纳这个序号的，0 表示没有在途的请求
    uint32_t                     m_mgrSearchSeq;
    //左栏命中树的节点 id → m_mgrHits 下标。FillManagerTrees 重填时整体替换
    std::map<int, size_t>        m_mgrHitNodes;
    //左栏的类型筛选，取值见 ManagerTypeFilter；窗口对象存在期间保留
    int                          m_mgrTypeFilter;
    //左栏当前生效的搜索词（已去掉首尾空白）；空串表示不在搜索
    std::wstring                 m_mgrQuery;
    //左栏的搜索范围（已去重排序）；为空表示所有会话。只在窗口对象存在期间有效，不存盘；
    //每次新打开窗口时清空
    std::vector<balloonhistory::HistoryConversationKey> m_mgrScope;
    //左栏是否折叠。建窗口时从账号配置读入，关窗时写回
    bool                         m_mgrCollapsed;
    //左栏展开时的宽度（像素）。建窗口时从账号配置读入（没有保存过时为默认宽度），拖完分隔条时更新
    int                          m_mgrPaneWidth;
    //等待补拉的会话（见 ScheduleBackfill）；m_pendingBackfillCid 为 0 表示没有
    int                          m_pendingBackfillCtype;
    int64_t                      m_pendingBackfillCid;
    //左栏列表的滚动视口（所有权在控件树，InitUi 时取回）。滚动条淡入淡出的定时器要推进它
    balloonwjui::DuiScrollView*  m_pMgrScroll;
    //是否已经安排了一次左栏刷新（kMgrRebuildTimerId 在计时）。已安排时再收到通知不重新计时，
    //保证消息连续到达时左栏最多晚 kMgrRebuildDelayMs 毫秒刷新，而不是一直被往后推
    bool                         m_mgrRebuildPending;
    //搜索范围弹层。窗口对象存在期间一直复用，第一次打开时创建弹层窗口
    balloonhistory::CHistoryScopePopup m_scopePopup;
    //左栏范围行「通配符」复选框的状态：左栏跨会话搜索的关键字是否按通配符解释。窗口打开期间保持，
    //每次新打开窗口时恢复为 false（docs/聊天记录管理器方案.md §11.4）。右侧的对应状态在模型的 m_wildcard 里
    bool                         m_mgrWildcard;

    // ---- 聊天记录管理器的文件页与图片页（docs/聊天记录管理器方案.md §11.5、§11.6） ----

    //左栏当前的标签，取值见 balloonhistory::HistoryManagerTab。每次新打开窗口时回到聊天记录页
    int                          m_mgrTab;
    //文件 / 图片页的浏览状态与当前页数据。窗口对象存在期间保留，每次新打开窗口时恢复为初始筛选条件
    balloonhistory::HistoryLibraryModel m_libModel;
    //左栏在文件 / 图片页时「全部会话」那一行的节点 id；-1 表示还没有这一行
    int                          m_libAllNode;
    //左栏「聊天」标签列表第一行「与我有关」的节点 id；-1 表示这一次没有这一行（左栏在搜索时不列它）
    int                          m_mentionsNode;
    //右侧是否正显示「本机存储占用」页（与 m_mgrTab 独立：离开时回到 m_mgrTab 对应的页面）
    bool                         m_storageMode;
    //存储页在途请求（统计或清理）的序号；0 表示没有在途请求。结果回来时序号对不上的丢弃
    uint32_t                     m_storageSeq;
    //在途请求是不是清理（清理期间页面显示「正在清理…」）
    bool                         m_storageCleaning;
    //m_storageStats / m_storageRefs 是否来自一次成功的统计；窗口新打开时清掉，进入存储页时据此决定要不要统计
    bool                         m_storageValid;
    //最近一次统计的结果与读出的缓存引用（清理某个会话时据引用找要删的路径）
    balloonhistory::HistoryStorageStats          m_storageStats;
    std::vector<balloonhistory::HistoryCacheRef> m_storageRefs;
    //左栏在文件 / 图片页时，不在 m_mgrItems 里的会话行（例如只有群文件、没有聊天记录的群）：节点 id → 会话
    std::map<int, balloonhistory::HistoryConversationKey> m_libExtraNodes;
    //文件页发起、还没有结果的群文件下载：条目 ID → 下载进度（0～100）。下载结果到达时移除，关窗时清空
    std::map<std::string, int>   m_libGroupDownloads;
    //是否已经安排了一次文件页的合并刷新（群文件缓存变化时，见 NotifyGroupFileCacheChanged）
    bool                         m_libRefreshPending;
    //上一次显示文件 / 图片页列表时的页号与筛选条件；换了一页或换了条件就把列表滚回顶部
    balloonhistory::HistoryLibraryFilter m_libShownFilter;

    // ---- 聊天记录页的时间轴模式（docs/聊天记录管理器方案.md §11.7，实现在 BalloonHistoryWindowTimeline.cpp） ----

    //是否处于时间轴模式（搜索行最右边「列表 | 时间轴」）。窗口打开期间保持，每次新打开窗口时回到列表模式
    bool                                             m_timelineMode;
    //时间轴模式下按关键字找出的命中位置与当前第几处；没有关键字、或不在时间轴模式时为空
    balloonhistory::HistoryHitNavigator              m_hits;
    //导航条的按年数据（上次统计的结果）
    std::vector<balloonhistory::HistoryTimelineYear> m_timelineYears;
    //导航条里展开的年份；换会话、切换模式时清空，重新按当前月份展开一年
    std::set<int>                                    m_navExpandedYears;
    //导航条高亮的月份（年*100+月）；0 表示不高亮。每次刷新列表后按定位行或当前页第一条算
    int64_t                                          m_navCurrentMonthKey;
    //上次统计导航数据与命中位置时的条件（含关键字与页号以外的全部筛选条件）
    balloonhistory::HistoryFilter                    m_timelineStatsFilter;
    //m_timelineStatsFilter 是否来自一次统计；切换模式、新打开窗口时置 false，下次刷新必定重新统计
    bool                                             m_timelineStatsValid;
    //交给后台线程、还没收到结果的那次统计的请求序号；0 表示没有在途的统计。结果到达时序号不等于它就丢弃
    uint32_t                                         m_timelineStatsSeq;
    //在途的那次统计是因为换了关键字而发起的：结果到达后若有命中，跳到第一处
    bool                                             m_timelineJumpPending;
};
