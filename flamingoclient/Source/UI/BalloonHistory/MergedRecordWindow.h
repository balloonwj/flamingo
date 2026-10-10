/**
 *  合并转发的聊天记录查看窗，MergedRecordWindow.h
 *
 *  在聊天窗里点合并卡片后打开：只读，按时间先后列出记录里每条子消息的头像、名字、时间
 *  与内容，带日期分组头。列表直接复用消息记录窗的行控件（HistoryWindowLayout 的
 *  BuildListContent），行数据由 MergedRecordRows 生成；查看窗不驱动行的悬停态，行上的
 *  「Pin / 定位」按钮因此不会出现。
 *
 *  标题栏显示记录的标题（如「张三和李四的聊天记录」），客户区顶部一行写"共 N 条消息"。
 *  窗口可缩放：宽度变化后按新宽度重建列表，行高随正文折行重新计算。
 *
 *  交互：文件行用聊天窗的文件卡片显示，卡片上的「下载 / 打开文件 / 打开所在文件夹 / 另存为 /
 *  取消 / 重新下载」与聊天窗里的行为一致，下载中显示进度；单击图片缩略图下载原图；双击图片行
 *  打开看图窗、双击文件行用关联程序打开，本地没有时先下载、下完自动打开；双击嵌套记录再打开一层（有主面板时请主面板打开，调试入口
 *  没有主面板时由本窗口自己打开）；长文的「展开 / 收起」链接切换折叠。子消息的 msgid 是
 *  原消息的 msgid，下载按它发起，进度与结果由主面板按 msgid 转给各查看窗。
 *
 *  典型用法：
 *      CMergedRecordWindow* wnd = new CMergedRecordWindow();
 *      wnd->Open(mergedContentJson, fallbackNames);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由
 *  stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../App/CustomMsgDef.h"           // WM_CHAT_OPEN_LINK / WM_CHAT_IMAGE_READY
#include "../../net/MessageProtocolClient.h"  // net::MergedRecord
#include "HistoryWindowLayout.h"              // balloonhistory::HistoryRowView
#include "MergedRecordRows.h"                 // balloonhistory::MergedFileStateTable
#include "../../Feature/Voice/VoiceHub.h"       // balloonvoice::IVoicePlayListener（语音条播放，第 4 期）
#include "../BalloonChatWnd/ChatMessageList.h"  // WM_CHAT_VOICE_ACTION（消息映射里要用）
#include "../BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：任务栏与 Alt+Tab 的程序图标

namespace balloonwjui { class DuiScrollView; }
namespace balloonchatwnd { class ChatVoiceStrip; }

//列表重建请求：窗口尺寸变化时投递，在事件解栈之后按客户区新宽度重建列表。不在 WM_SIZE
//里直接重建，是因为 DuiFrameWindow 自己的排版也在处理这条消息，此时重建会与它交错
#define WM_MERGED_VIEWER_REBUILD  (WM_APP + 0x63)

/**
 *  合并转发的聊天记录查看窗。继承 balloonwjui::DuiFrameWindow，由它提供无边框自绘窗体
 *  与标题栏。一个实例显示一份记录；记录里嵌套的记录由另一个实例显示。
 */
class CMergedRecordWindow : public balloonwjui::DuiFrameWindow,
                            public balloonvoice::IVoicePlayListener
{
public:
    CMergedRecordWindow();
    ~CMergedRecordWindow();

    /**
     *  打开查看窗显示一份合并记录；本窗口已打开时换成新内容并调到前台。
     *    contentJson：  合并消息的 content 原文（UTF-8），即 MessageNotify::m_mergedContent
     *                   或本地库 content 里 merged 子对象序列化后的文字。
     *    fallbackNames：子消息没带 fromname 时按 userid 查的名字表（userid → 名字），通常
     *                   由调用方经主面板解析好；可为空表，此时缺名字的写"用户" + userid。
     *    hostPanel：    主面板窗口句柄，双击嵌套记录时向它投递 FMG_MSG_OPEN_MERGED_VIEWER；
     *                   传 NULL（调试入口）时由本窗口自己打开下一层。
     *  @return content 解析成功、窗口已显示时返回 true；解析失败时不建窗口，返回 false
     *  副作用：首次调用时创建窗口并登记为图片缓存的通知接收窗口；换内容时清空下载与展开状态。
     */
    bool Open(const std::string& contentJson, const std::map<int64_t, CString>& fallbackNames,
              HWND hostPanel);

    /**
     *  一笔图片下载有结果了（主面板收到 FMG_MSG_V2_IMAGE_DOWNLOAD_RESULT 后转给每个查看窗）。
     *    msgid：    图片消息的 msgid（合并记录里即原消息的 msgid）。不是本窗口发起的下载时直接忽略。
     *    ok：       是否下载成功。
     *    localPath：成功时原图落地的本地完整路径；失败时无意义。
     *  副作用：重建列表刷新缩略图；这笔下载由双击引起时打开看图窗；失败时缩略图显示破图、可单击重试。
     */
    void OnImageDownloadFinished(int64_t msgid, bool ok, const std::wstring& localPath);

    /**
     *  一笔文件下载有结果了（主面板收到 FMG_MSG_V2_FILE_DOWNLOAD_RESULT 后转给每个查看窗）。
     *    msgid：    文件消息的 msgid。不是本窗口发起的下载时直接忽略。
     *    ok：       是否下载成功。
     *    localPath：成功时文件落地的本地完整路径；失败时无意义。
     *    cancelled：是否为用户主动取消（取消不算失败，不弹提示）。
     *  副作用：这笔下载由双击引起且成功时用关联程序打开文件；失败时弹提示框。
     */
    void OnFileDownloadFinished(int64_t msgid, bool ok, const std::wstring& localPath,
                                bool cancelled);

    /**
     *  一笔文件下载有了新进度（主面板收到 FMG_MSG_V2_FILE_DOWNLOAD_PROGRESS 后转给每个查看窗）。
     *    msgid：   文件消息的 msgid。不是本窗口正在下载的文件时直接忽略。
     *    received：已收到的字节数。
     *    total：   文件总字节数；<=0 时按进度 0 处理。
     *  副作用：就地更新那一行文件卡片的进度条，不重建列表（进度变化不影响行高）。
     */
    void OnFileDownloadProgress(int64_t msgid, int64_t received, int64_t total);

    /**
     *  一段语音下载有了结果（主面板收到 FMG_MSG_V2_VOICE_DOWNLOAD_RESULT 后按 md5 转给每个查看窗，语音消息第 4c 步）。
     *  本窗口发起过这一段时把它移出「下载中」；无论是否本窗口发起都重建列表，语音条的本机文件状态重新判断。
     *  @param md5 语音文件内容 MD5
     *  @param ok  是否下载成功
     */
    void OnVoiceDownloadFinished(const std::string& md5, bool ok);

    // 以下为 balloonvoice::IVoicePlayListener 的实现（语音消息第 4c 步）
    /**
     *  播放进度：更新这条语音的语音条喇叭帧。
     *  @param key      正在播的语音
     *  @param playedMs 已播放的毫秒数
     *  @param totalMs  总毫秒数
     */
    virtual void OnVoicePlayProgress(const balloonvoice::VoicePlayKey& key, int64_t playedMs,
                                     int64_t totalMs) override;
    /**
     *  播放结束：语音条退出播放中。查看窗不自动连播。
     *  @param key    结束的语音
     *  @param reason 取值见 balloonvoice::VoicePlayEnd
     */
    virtual void OnVoicePlayEnded(const balloonvoice::VoicePlayKey& key, int reason) override;

    /**
     *  取本窗口当前显示的记录（只读）。窗口未打开时为空记录。
     *  @return 记录的引用，生命周期同本对象
     */
    const net::MergedRecord& Record() const;

    BEGIN_MSG_MAP(CMergedRecordWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        //文件行卡片上的操作链接被点（下载 / 打开 / 另存为 / 取消 / 重新下载）
        MESSAGE_HANDLER(WM_CHAT_FILE_ACTION, OnChatFileAction)
        //语音条上点了一下（ChatVoiceStrip 投递）：播放、停止或下载
        MESSAGE_HANDLER(WM_CHAT_VOICE_ACTION, OnChatVoiceActionMsg)
        //代码片段卡片请求打开代码查看窗
        MESSAGE_HANDLER(WM_CHAT_OPEN_CODE_VIEWER, OnChatOpenCodeViewer)
        //Markdown 卡片请求打开 Markdown 预览窗口
        MESSAGE_HANDLER(WM_CHAT_OPEN_MARKDOWN_PREVIEW, OnChatOpenMarkdownPreview)
        //尺寸变化只旁听、不消费：DuiFrameWindow 照常排版，本窗口另投一条重建请求
        MESSAGE_HANDLER(WM_SIZE, OnSizeMsg)
        MESSAGE_HANDLER(WM_MERGED_VIEWER_REBUILD, OnRebuildMsg)
        MESSAGE_HANDLER(WM_CHAT_OPEN_LINK, OnChatOpenLinkMsg)
        //缩略图后台解码完成的通知（本窗口在创建时登记为接收窗口）
        MESSAGE_HANDLER(WM_CHAT_IMAGE_READY, OnImageReadyMsg)
        //销毁时从图片缓存注销。只旁听、不消费，DuiFrameWindow 照常收尾
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //创建窗口、配置标题栏外观、搭好客户区并显示。成功返回 true。
    bool BuildWindow();

    //搭客户区控件树（顶部"共 N 条消息" + 分隔线 + 列表滚动视口），捕获滚动视口指针。
    void BuildClient();

    //按当前记录与客户区宽度重建列表内容。宽度与上次相同且 force 为 false 时不重建。
    //  force：true 表示内容变了（换了一份记录），无论宽度是否变化都重建
    void RebuildList(bool force);

    //列表的可用宽度（像素）：客户区宽度减去右侧给缩放边框让出的宽度。
    int ListWidth() const;

    //列表滚动视口右侧给窗口缩放边框让出的宽度（像素），与消息记录窗同一口径。
    int ListRightInset() const;

    //为图片行补上缩略图的本地路径与显示状态（按登录账号的缓存目录判断本地有没有原图、
    //本窗口是否正在下载或下载失败过），并按展开态集合设置各行的 m_expanded。
    //  rows：[出参] BuildMergedRecordRows 的结果，与 m_record.m_messages 一一对应
    void FillRowStates(std::vector<balloonhistory::HistoryRowView>& rows) const;

    //投递一次强制重建列表的请求（内容状态变了，宽度未必变）。在事件解栈之后执行，
    //避免在控件自己的通知回调里销毁它。
    void PostForceRebuild();

    //取第 row 条子消息在本地的可用文件路径：图片查内容 MD5 缓存，文件先查登记表、失效再
    //回退 MD5 缓存。未登录（调试入口）时一律查不到。
    //  row：子消息下标；outPath：[出参] 本地完整路径，返回 false 时被清空
    //  @return 本地确有完整文件时返回 true
    bool ResolveLocalFile(int row, CString& outPath) const;

    //为第 row 条子消息发起下载（图片走 ImageTransfer、文件走 FileTransfer）。
    //  row：子消息下标；越界、非图片 / 文件、缺下载信息、未登录时什么都不做
    //  openAfter：下载成功后是否自动打开（图片开看图窗、文件用关联程序打开）
    void StartDownload(int row, bool openAfter);

    //第 row 条子消息被双击：图片开看图窗、文件用关联程序打开（本地没有就先下载），
    //嵌套记录再打开一层，其余类型不处理。
    void OnRowDoubleClicked(int row);

    //打开一层嵌套记录。有主面板时投递 FMG_MSG_OPEN_MERGED_VIEWER 请主面板打开；
    //没有主面板（调试入口）时由本窗口创建并持有下一层查看窗。
    //  contentJson：嵌套记录的 content 原文（UTF-8）
    void OpenNested(const std::string& contentJson);

    //WM_DUI_NOTIFY：缩略图单击、「展开 / 收起」链接、行双击。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_FILE_ACTION：文件行卡片上的操作链接被点。lParam 为堆上的
     *  balloonchatwnd::ChatFileActionData，处理后 delete。打开 / 打开所在文件夹 / 另存为与聊天窗
     *  共用同一份处理；「下载」「重新下载」发起下载；「取消」取消正在进行的下载。
     */
    LRESULT OnChatFileAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //找出 msgid 对应的文件子消息是第几条。
    //  msgid：文件子消息的 msgid
    //  @return 子消息下标；找不到（不是本记录里的文件）时返回 -1
    int RowOfFileMsgid(int64_t msgid) const;

    //WM_CLOSE：销毁本窗口。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_SIZE 旁听：投递 WM_MERGED_VIEWER_REBUILD。恒置 bHandled=FALSE。
    LRESULT OnSizeMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_MERGED_VIEWER_REBUILD：重建列表（见 RebuildList）。wParam 非 0 表示强制重建
    //（内容状态变了），为 0 表示只在宽度变化时重建。
    LRESULT OnRebuildMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_OPEN_LINK：列表行正文里的超链接被点击。lParam 为堆上的 std::wstring*
     *  （规范化后的 URL），本函数负责 delete。只放行 http / https，与聊天窗同一口径。
     */
    LRESULT OnChatOpenLinkMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_CHAT_IMAGE_READY：某张缩略图在后台解码完成，重画窗口。
    LRESULT OnImageReadyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DESTROY 旁听：从图片缓存注销本窗口，并销毁本窗口自己打开的下一层查看窗。
    //恒置 bHandled=FALSE。
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_VOICE_ACTION 处理（语音消息第 4c 步）：语音条上点了一下。lParam 为堆上的
     *  balloonchatwnd::VoiceActionRequest*，处理后 delete。按 DecideVoiceClick 播放、停止、提示正在下载或下载。
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

private:
    /**
     *  在当前列表里找某条语音的语音条控件。
     *  @param msgid 语音子消息的 msgid
     *  @return 语音条控件（借用，属于列表）；没有这条时为 NULL
     */
    balloonchatwnd::ChatVoiceStrip* FindVoiceStrip(long long msgid);

    net::MergedRecord            m_record;         //当前显示的记录（Open 时解析得到）
    std::map<int64_t, CString>   m_fallbackNames;  //子消息缺 fromname 时的回退名字表
    balloonwjui::DuiScrollView*  m_pScroll;        //列表滚动视口（所有权在控件树，本类只引用）
    balloonwjui::DuiControl*     m_pSubtitle;      //顶部"共 N 条消息"标签（所有权在控件树）
    int                          m_lastListWidth;  //上次建列表时的列表宽度（像素）；0 表示还没建过
    bool                         m_rebuildPending; //尺寸变化引起的重建请求已投递、尚未处理
    HWND                         m_hostPanel;      //主面板窗口句柄（借用）；NULL 表示调试入口、没有主面板

    //本窗口发起、尚未收到结果的下载（按子消息 msgid 记）。只装本窗口发起的那些，
    //别的窗口发起的同一 msgid 的下载结果到达时不弹看图窗、不弹提示
    std::set<int64_t>            m_downloadingMsgids;
    //本窗口发起的图片下载里失败了的（按 msgid 记）。缩略图据此显示破图并允许单击重试；
    //再次发起或下载成功时移除。换内容或关窗即清空
    std::set<int64_t>            m_failedImageMsgids;
    //处于"展开"态的长文子消息（按 msgid 记）。重建列表时据此设置各行的展开态
    std::set<int64_t>            m_expandedMsgids;
    //本窗口发起的文件下载的状态（下载中 / 进度 / 完成 / 失败 / 取消，按 msgid 记）。重建列表
    //时套到文件卡片上；换内容或关窗即清空。图片下载仍记在 m_downloadingMsgids 里
    balloonhistory::MergedFileStateTable m_fileStates;
    //下载完成后要自动打开的那条子消息 msgid（双击引起的下载）；0 表示没有
    int64_t                      m_pendingOpenMsgid;
    //没有主面板时由本窗口打开的下一层查看窗。本窗口销毁时一并销毁
    std::vector<std::unique_ptr<CMergedRecordWindow> > m_ownedNested;
    //标题栏左侧图标位图。DuiFrameWindow::SetIcon 只借用不释放，故本类持有、析构时 DeleteObject
    HBITMAP                      m_hTitleIcon;
    //任务栏与 Alt+Tab 的程序图标（2026-10-07，UI 约定第 10 条：没有所有者、会出现在任务栏的窗口另设任务栏图标）；本对象持有，析构时释放
    balloonmain::ProgramWindowIcons m_taskIcons;
    //语音（第 4c 步）：本窗口发起、还在下载的语音 md5。重建列表时据此把语音条画成「下载中」；下载结果到达时移除
    std::set<std::string>        m_downloadingVoices;
    //本窗口正在播放的语音子消息 msgid；0 表示没有在播放。重建列表后据此画播放中
    int64_t                      m_playingVoiceMsgid;
};
