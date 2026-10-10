/**
 *  录制管理窗口，RecordManagerWindow.h
 *
 *  主面板系统菜单「录制管理」打开（docs/屏幕共享与录制方案.md 第 11.6 节 F9）：列出本账号录过的录像（只列表，不带缩略图），
 *  最新的在最上面。选中一行后可以播放、打开所在文件夹、发送到会话、重命名（在列表里原地编辑）、删除（确认后移到回收站）；
 *  录像文件在外部被删除或改名后，这一行换成灰色的「文件缺失」图标、副文字写「文件已不存在」，只能从列表移除。
 *  双击播放；右键弹出与工具栏相同的菜单；F2 重命名、Delete 删除。
 *  语音字幕（docs/语音字幕方案.md 第 2 节第 9 条）：字幕可用时工具栏与右键菜单多一项「编辑字幕」，副文字末尾写字幕状态；
 *  重命名、删除录像时连带改名或删除它的字幕草稿。
 *  第 27 期 27d（docs/屏幕共享与录制方案.md 12.6，效果图 2026-10-06 已确认）：最上面多「本机 / 云端」页签。云端页列出本人的
 *  云端录像（数据与操作都经 CCloudRecordCenter），每行右侧的状态列写「可观看」「合成中 45%」「上传中 72%」等；工具栏为播放、
 *  下载…、发送到会话…、重命名（原地编辑标题）、删除，右侧显示用量；右键菜单另有「重新合成」（本机合成失败的共享录制）。
 *  本机页的右键菜单另有「上传到云端」。关闭窗口不影响合成与上传，完成时照常在右下角提示。
 *
 *  录像记录存在 exe 持有的本账号本地库里，本窗口经 IMediaHost 的 ListRecordings / UpdateRecordingPath / RemoveRecording
 *  读写；录制完成时由录屏控制器经 AddRecording 写入，并调 ReloadIfOpen 刷新已打开的窗口。窗口激活时也会重新读取，
 *  以反映在外部删除或改名的文件。
 *
 *  窗口可改大小、可最小化与最大化、带任务栏按钮；重复打开时恢复并激活原窗口。控件树见 RecordManagerLayout.h，
 *  列表的右键与原地编辑见 RecordManagerList.h，纯逻辑见 RecordManagerModel.h。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法（UI 线程，录屏控制器持有本对象）：
 *      m_manager.Open(hostMainWnd, m_host);
 *      ...（录制完成写入记录后）
 *      m_manager.ReloadIfOpen();
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons（标题栏与任务栏图标）

#include <stdint.h>
#include <string>
#include <vector>
#include "RecordMessages.h"
#include "CloudRecordCenter.h"   // ICloudRecordObserver：云端页随列表与进度刷新
#include "../Api/FlamingoMediaApi.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace balloonwjui
{
    class DuiButton;
    class DuiLabel;
    class DuiProgressBar;
    class DuiTab;
}

namespace screenrecord {

class CRecordManagerList;

//录制管理窗口
class CRecordManagerWindow : public balloonwjui::DuiFrameWindow, private ICloudRecordObserver
{
public:
    CRecordManagerWindow();
    ~CRecordManagerWindow();

    /**
     *  打开窗口并读取录像记录。已经开着时恢复（最小化了的）、重新读取并激活。
     *  @param owner 属主窗口（exe 主面板），窗口居中在它所在的显示器上；可为 NULL
     *  @param host  宿主接口（读写录像记录、发送到会话）；不拥有，须比本窗口活得久；为 NULL 时不打开
     */
    void Open(HWND owner, flamingomedia::IMediaHost* host);

    //窗口开着时重新读取录像记录（录制完成写入新记录后由录屏控制器调用）；正在原地重命名时先不刷新，避免打断输入
    void ReloadIfOpen();

    //关闭窗口；正在原地重命名时放弃这次修改。可重复调用
    void CloseWindow();

    //窗口是否开着
    bool IsOpen() const;

    /**
     *  登记云端录像中心（第 27 期 27d）：云端页的数据与操作都经它；为 NULL 时云端页只显示「云端录制不可用」。
     *  @param center 云端录像中心，不拥有；模块关闭它之前传 NULL 解除
     */
    void SetCloudCenter(CCloudRecordCenter* center);

    //经 kMsgRecordManagerDeferred 延后处理的事
    enum DeferredAction
    {
        deferred_context_menu = 1,   //右键按下之后弹出菜单：lParam 为行号
        deferred_commit_edit  = 2,   //提交原地重命名（回车、失去焦点、列表被单击或滚动、窗口改变大小）
        deferred_cancel_edit  = 3,   //放弃原地重命名（Esc）
        deferred_cloud_context_menu = 4,   //云端页：右键按下之后弹出菜单，lParam 为行号
        deferred_cloud_commit_edit  = 5,   //云端页：提交原地重命名
        deferred_cloud_cancel_edit  = 6,   //云端页：放弃原地重命名
        deferred_cloud_reload       = 7,   //云端页：列表变了，重建列表（观察者回调可能在别的窗口消息中途，延后到下一条消息）
    };

    BEGIN_MSG_MAP(CRecordManagerWindow)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(kMsgRecordManagerDeferred, OnDeferredMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //列表里的一行
    struct Row
    {
        int64_t      m_id;           //记录 id
        std::wstring m_path;         //录像文件完整路径
        int64_t      m_durationMs;   //时长（毫秒）
        int64_t      m_sizeBytes;    //文件字节数（录制完成时）
        int64_t      m_endTimeMs;    //录制结束时间（Unix 毫秒，UTC）
        bool         m_exists;       //读取时文件是否还在
        int          m_captionStatus;   //字幕文件状态（caption::CaptionFileStatus 的组合）；字幕不可用时为 0

        Row();
    };

    //第一次打开时建窗口：标题栏按钮、可改大小、最小尺寸、任务栏图标，居中在属主所在的显示器上
    void ensureWindow(HWND owner);
    //重新读取记录并重建列表；尽量保持原来选中的那条记录
    void reload();
    //按选中行刷新五个按钮的可用状态与「删除 / 从列表移除」的文字
    void updateButtons();
    //当前选中的行；没有选中时返回 NULL
    const Row* selectedRow() const;
    //对选中行执行一项操作（RecordRowAction 之一）
    void doAction(int action);
    //开始原地重命名选中行；prefill 为空时预填不含扩展名的原文件名
    void beginRename(const std::wstring& prefill);
    //提交原地重命名：校验新文件名、改文件名、改记录；不合法时提示并回到编辑
    void commitRename();
    //放弃原地重命名
    void cancelRename();
    //删除选中行：文件在时确认后移到回收站再删记录，文件已不存在时直接删记录
    void deleteSelected();
    //在鼠标位置弹出选中行的右键菜单
    void showContextMenu();
    //恢复（最小化了的）并提到前台
    void bringToFront();
    //按字幕是否可用显示或隐藏「编辑字幕」，显隐变了时重排工具栏
    void applyCaptionButton();
    //打开选中行的字幕编辑器
    void openCaptionEditor();
    /**
     *  录像改名后连带处理字幕：字幕可用时交给字幕模块（草稿与导出记录一并改），否则按命名规则把草稿文件改名。
     *  @param oldPath 原录像路径
     *  @param newPath 新录像路径
     */
    void renameCaptionFiles(const std::wstring& oldPath, const std::wstring& newPath);
    /**
     *  录像删除（或从列表移除）后连带删除它的字幕草稿：字幕可用时交给字幕模块，否则按命名规则删除草稿文件。
     *  @param videoPath 录像路径
     */
    void deleteCaptionFiles(const std::wstring& videoPath);

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //======== 云端页（第 27 期 27d） ========
    //ICloudRecordObserver
    virtual void OnCloudRecordsChanged() override;
    virtual void OnCloudProgressChanged() override;
    //切换页签：显示对应的页面，第一次切到云端页时请求列表
    void showPage(int page);
    //重建云端页的列表、用量与提示；尽量保持原来选中的录像
    void reloadCloud();
    //只刷新每行的状态列（进度）
    void refreshCloudTrailing();
    //填一行的状态列
    void applyCloudTrailing(int index, const CloudRecordInfo& info, int64_t nowMs);
    //按选中行刷新云端页五个按钮
    void updateCloudButtons();
    //云端页当前选中的录像；没有选中时返回 NULL
    const CloudRecordInfo* selectedCloud() const;
    //云端页一行的状态
    int cloudStateOf(const CloudRecordInfo& info) const;
    //对选中的云端录像执行一项操作（CloudRowAction 之一）
    void doCloudAction(int action);
    void beginCloudRename(const std::wstring& prefill);
    void commitCloudRename();
    void cancelCloudRename();
    void deleteCloudSelected();
    void downloadCloudSelected();
    void showCloudContextMenu();
    //本机页：把选中的录像上传到云端（27d-4）
    void uploadLocalSelected();

    flamingomedia::IMediaHost* m_host;          //宿主接口（不拥有）；Open 时登记
    std::vector<Row>           m_rows;          //列表里的行，与列表的行号一一对应
    int64_t                    m_editId;        //正在原地重命名的记录 id；没在编辑时为 0
    CRecordManagerList*        m_list;          //录像列表（归控件树所有）；窗口销毁后为 NULL
    balloonwjui::DuiLabel*     m_empty;         //没有录像时的提示（归控件树所有）
    balloonwjui::DuiLabel*     m_count;         //「共 N 个」（归控件树所有）
    balloonwjui::DuiButton*    m_play;          //「播放」（归控件树所有）
    balloonwjui::DuiButton*    m_openFolder;    //「打开所在文件夹」（归控件树所有）
    balloonwjui::DuiButton*    m_send;          //「发送到会话…」（归控件树所有）
    balloonwjui::DuiButton*    m_rename;        //「重命名」（归控件树所有）
    balloonwjui::DuiButton*    m_delete;        //「删除 / 从列表移除」（归控件树所有）
    balloonwjui::DuiButton*    m_caption;       //「编辑字幕」（归控件树所有；语音字幕不可用时隐藏）
    bool                       m_captionAvailable;   //上次读取时语音字幕是否可用（决定「编辑字幕」显隐与是否查字幕状态）
    HBITMAP                    m_iconPresent;   //文件还在的行的图标（32 位预乘 alpha）；本类持有，析构时释放
    HBITMAP                    m_iconMissing;   //文件已不存在的行的图标；本类持有，析构时释放
    HBITMAP                    m_iconAudio;     //只录声音的录音（.m4a，第 24 期）还在时的图标（话筒）；本类持有，析构时释放
    HBITMAP                    m_iconAudioMissing;   //录音文件已不存在时的图标；本类持有，析构时释放
    balloonmain::ProgramWindowIcons m_programIcons; //标题栏与任务栏的程序图标；本类持有
    //第 27 期 27d：页签与云端页
    CCloudRecordCenter*        m_cloud;         //云端录像中心（不拥有）；没有登记时为 NULL
    int                        m_page;          //当前页签：kRecordMgrPageLocal / kRecordMgrPageCloud；关窗后保留，下次打开回到这一页
    bool                       m_cloudRequested; //本次打开窗口后是否已请求过云端列表
    std::vector<int64_t>       m_cloudRows;     //云端列表每一行的录像 id，与列表的行号一一对应
    int64_t                    m_cloudEditId;   //云端页正在原地重命名的录像 id；没在编辑时为 0
    bool                       m_cloudReloadPosted; //延后的重建是否已投递、还没处理
    balloonwjui::DuiTab*       m_tabs;          //页签（归控件树所有）
    balloonwjui::DuiControl*   m_localPage;     //本机页（归控件树所有）
    balloonwjui::DuiControl*   m_cloudPage;     //云端页（归控件树所有）
    CRecordManagerList*        m_cloudList;     //云端录像列表（归控件树所有）
    balloonwjui::DuiLabel*     m_cloudEmpty;    //云端页的提示（归控件树所有）
    balloonwjui::DuiLabel*     m_cloudUsage;    //用量文字（归控件树所有）
    balloonwjui::DuiProgressBar* m_cloudUsageBar;   //用量条（归控件树所有）
    balloonwjui::DuiButton*    m_cloudPlay;     //「播放」（归控件树所有）
    balloonwjui::DuiButton*    m_cloudDownload; //「下载…」（归控件树所有）
    balloonwjui::DuiButton*    m_cloudSend;     //「发送到会话…」（归控件树所有）
    balloonwjui::DuiButton*    m_cloudRename;   //「重命名」（归控件树所有）
    balloonwjui::DuiButton*    m_cloudDelete;   //「删除」（归控件树所有）
    balloonwjui::DuiButton*    m_cloudMore;     //「加载更多」（归控件树所有）
};

} // namespace screenrecord
