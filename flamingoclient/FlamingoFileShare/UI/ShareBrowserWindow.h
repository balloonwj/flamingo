/**
 *  共享浏览窗口，ShareBrowserWindow.h
 *
 *  访问者打开一个共享后看到的窗口（docs/文件共享方案.md 7.4「浏览窗口」）。第 1 期第 13 步实现：
 *    - 顶部横幅：共享暂不可用、已失效或打开失败时显示原因，其余时候隐藏；
 *    - 工具栏：「上一级」按钮、当前路径（共享根名 / 各级目录）、「刷新」按钮；
 *    - 表头：名称、大小、修改时间三个按钮，点击按该列排序，再点一次反向，当前列带 ▲ / ▼；目录始终排在文件前面；
 *    - 文件列表：双击目录进入；大目录由状态机按游标分批取完后一次显示；
 *    - 底部状态栏：连接中、加载中、共 N 项。
 *  第 14 步加入传输：
 *    - 工具栏的「下载」（选中的一行；单文件共享就是那个文件）、「上传文件」「上传文件夹」（有上传权限、且不是单文件共享时
 *      才显示）；下载先选本机目录，上传用系统的选文件、选文件夹对话框，上传到当前目录；
 *    - 窗口下方的传输列表：每次传输一行，显示名称、方向、进度条与状态，选中一行点「取消」可取消；有过传输才显示；
 *    - 访问会话空闲回收之后再点下载或上传：先重新打开，打开成功后再发起。
 *  第 2 期第 2 步加入写操作：
 *    - 工具栏的「新建文件夹」（有新建权限、且不是单文件共享时才显示）；
 *    - 文件列表的右键菜单：打开、下载、重命名、删除、移动到…、新建文件夹、刷新，按权限与共享类型显示
 *      （ShareBrowseModel::ContextMenuItems）；右键点在某一行上时先选中该行；
 *    - 新建与重命名弹名字输入窗口（CShareNameDialog），删除先确认，移动弹选目标文件夹窗口（CShareFolderPickDialog，
 *      打开期间本窗口的列目录结果转给它）；结果写在状态栏上，成功后刷新当前目录。
 *  第 2 期第 3 步加入搜索：工具栏拆成两行，导航行是「上一级 / 路径 / 搜索框 / 刷新」，操作行是「下载 / 上传文件 /
 *  上传文件夹 / 新建文件夹」；在搜索框里按回车即在当前目录之下搜索，列表进入搜索结果状态（名称列显示相对共享根的路径，
 *  命中边到边显示），状态栏显示进度、结束与截断的提示；按 Esc、点「上一级」或换目录时回到目录列表。搜索结果上的右键
 *  菜单与普通列表相同，写操作成功后重新搜索一次。
 *  第 2 期第 4 步加入缩略图视图与图片预览：导航行的「缩略图 / 列表」按钮切换视图；缩略图视图下列表的每一行画若干格，
 *  图片格显示从分享者取来的缩略图（定时检查可见区域，最多同时要 kMaxThumbsInFlight 张，解码后按路径缓存，换目录时清空），
 *  目录与其它文件画图标；单击选中、双击打开目录或预览图片、右键菜单作用于光标所在的格子。预览见 PreviewRow。
 *  第 2 期第 5 步加入多选、拖入上传与拖出下载：
 *    - 多选：单击、Ctrl + 单击、Shift + 单击，方向键（Shift 连选）、Ctrl + A、Enter 打开、Delete 删除；按在已选中的多项
 *      之一上时单选推迟到松开鼠标，以便整组拖动；下载、删除、移动作用于全部选中的条目，打开、预览、重命名只在选中
 *      一项时出现；排序之后选择按路径保持；
 *    - 拖入上传：从资源管理器把文件或文件夹拖进窗口即上传到当前目录（有上传权限、不是单文件共享、不在搜索结果时）；
 *      来自不同目录的条目按目录分成几次传输；
 *    - 拖出下载：在选中的条目上按下鼠标时先向分享者要选中目录的整棵目录树；拖动开始后经 ShareDragModel 生成虚拟文件
 *      描述、交给 DoDragDrop（ShareDragData）；放置开始时把条目下载到本机临时目录（CShareService::NewDragTempDir），
 *      资源管理器等文件下载完成后自己复制过去；下载在传输列表里照常显示，取消或失败时资源管理器提示复制失败。
 *
 *  第 4 期（2026-10-07，docs/文件共享方案.md 第 14 节）：关闭窗口不再取消传输，传输在后台继续（拖出下载的完成经结束回调
 *  通知拖出任务，不依赖本窗口）；重新打开时经 CShareService::AdoptVisitorTransfers 接回传输列表。访问会话未就绪时的排队
 *  移到了业务总控，本窗口不再自己排队。完成弹窗里的「重试」「查看」经 RetryTransfer、BrowseTo 回到本窗口。
 *
 *  控件全部用 balloonui 现成的：横幅与状态栏是 DuiLabel，工具栏与表头是 Ghost 样式的 DuiButton，列表是
 *  DuiVirtualList 的子类 CShareFileList 加画行回调（UI 约定第 2 条：只补多选与拖动检测），拖入用 DuiDropTargetHelper。
 *  横幅显隐之后对其父容器 ForceLayout（UI 约定第 5 条）。
 *
 *  每个共享至多一个浏览窗口，由 CFileShareModule 按共享 id 持有；关闭即销毁 HWND 并释放访问状态机，对象保留，
 *  下次打开时重建。窗口是访问状态机（CShareVisitor）的观察者，所有回调都在 UI 线程上。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法（CFileShareModule::OpenShareFromCard）：
 *      CShareBrowserWindow& w = ...;
 *      w.Open(owner, &m_service, shareId);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   //ProgramWindowIcons：标题栏与任务栏的程序图标
#include "../Engine/ShareDragModel.h"
#include "../Engine/ShareVisitor.h"
#include "../Engine/ShareService.h"   //IShareTransferObserver
#include "ShareDragData.h"
#include "ShareFileList.h"
#include "ShareFolderPickDialog.h"
#include "ShareNameDialog.h"
#include "ShareUiHost.h"
#include "ShareWindowPlacement.h"   //窗口位置的保存与恢复、错开摆放（2026-10-07）

namespace balloonwjui {
class DuiDropTargetHelper;
class DuiEdit;
class DuiHBox;
class DuiLabel;
class DuiButton;
class DuiVirtualList;
class DuiVBox;
}

namespace fileshare {

class CShareService;

/**
 *  CShareBrowserWindow —— 共享浏览窗口（非模态顶层窗口）。只在 UI 线程使用。
 */
class CShareBrowserWindow : public balloonwjui::DuiFrameWindow,
                            public IShareVisitorObserver,
                            public IShareTransferObserver,
                            public IShareNameDialogListener,
                            public IShareFolderSource,
                            public IShareFolderPickListener,
                            public IShareImagePreviewSource,
                            public IShareFileListListener,
                            public IShareDragDelegate
{
public:
    CShareBrowserWindow();
    ~CShareBrowserWindow();

    /**
     *  显示窗口并开始浏览共享根目录；已显示时只前置。
     *  @param owner   用来居中对齐的参照窗口，可为 NULL；不转移所有权
     *  @param service 业务总控（不拥有，须在本窗口关闭之前一直有效），不可为 NULL
     *  @param shareId 共享 id
     *  @return 窗口已显示返回 true；创建失败或取不到访问状态机时返回 false（已写日志）
     */
    bool Open(HWND owner, CShareService* service, int64_t shareId);

    /**
     *  关闭窗口：释放访问状态机、销毁 HWND。这个共享上还有传输时访问会话保留、传输在后台继续（第 4 期起），否则结束访问会话。
     *  窗口未创建时调用是安全的。
     */
    void Close();

    /**
     *  设置下一次新建窗口时的摆放参数（2026-10-07）。在 Open 之前调用；窗口已开着时 Open 只前置，不用这些参数。
     *  第一次打开在参照窗口所在显示器的工作区居中，之后按上次关闭时保存的位置与大小打开（所有共享的浏览窗口共用一份）；
     *  同时还开着别的浏览窗口时相对它错开摆放（见 CascadeSharePlacementRect）。
     *  @param iniPath     保存窗口位置的配置文件全路径（<个人目录>ChatLog\fileshare_ui.ini）；为空时不读也不保存
     *  @param cascadeFrom 最近打开、仍开着的另一个浏览窗口，新窗口相对它错开；为 NULL 时不错开。不转移所有权
     */
    void SetPlacement(const std::wstring& iniPath, HWND cascadeFrom);

    /**
     *  重新发起一笔结束了的传输（完成弹窗里的「重试」，第 4 期起）：下载按原来的条目与目标目录再请求一次（从断点接着传），
     *  上传重新上传原来的本机条目；传输列表里新增一行。窗口须已打开。
     *  @param result 那笔传输的结果（带重试所需的条目）
     *  @return 已发起（或已排队）返回 true
     */
    bool RetryTransfer(const ShareVisitorTransferResult& result);

    /**
     *  进入共享内的一个目录（完成弹窗里上传成功的「查看」，第 4 期起）。窗口须已打开。
     *  @param dir 相对共享根的目录，根为空串
     */
    void BrowseTo(const std::string& dir);

    //IShareVisitorObserver：访问状态变化
    virtual void OnVisitorStateChanged(int state, int errCode) override;

    //IShareVisitorObserver：一个目录列完或失败
    virtual void OnVisitorListed(const std::string& path, int code, const std::vector<ShareEntry>& entries) override;

    //IShareVisitorObserver：写权限位变化（第 1 期只记下，上传与新建按钮在后续步骤按它显隐）
    virtual void OnVisitorPermsChanged(int perms) override;

    //IShareVisitorObserver：共享对本人的状态变化（2203）
    virtual void OnVisitorShareState(int state, int reason) override;

    //IShareVisitorObserver：写操作有了结果
    virtual void OnVisitorOpResult(int64_t reqId, int opType, int code, const std::vector<ShareOpItem>& items) override;

    //IShareVisitorObserver：缩略图或预览图有了结果
    virtual void OnVisitorThumbResult(int64_t reqId, const std::string& path, int code, int width, int height,
                                      const std::string& jpeg) override;

    //IShareImagePreviewSource：看图窗里按了 ← / →，翻到上一张 / 下一张图片
    virtual void OnPreviewNavigate(int delta) override;

    /**
     *  设置界面宿主（打开看图窗用）。模块创建本窗口后调用一次。
     *  @param host 界面宿主（不拥有，须在本窗口销毁之前一直有效），可为 NULL（此时不能预览图片）
     */
    void SetUiHost(IShareUiHost* host);

    /**
     *  预览一行图片（与双击图片相同；测试直接调用）：向分享者要一张长边不超过屏幕的预览图，取到后交给看图窗。
     *  同时记下当前列表里的全部图片，看图窗里的 ← / → 在它们之间翻页。
     *  @param rowIndex 列表里的行号
     *  @return 已发出请求返回 true；这一行不是图片、窗口未打开时返回 false
     */
    bool PreviewRow(int rowIndex);

    //IShareVisitorObserver：搜索来了一批结果（或结束）
    virtual void OnVisitorSearchResult(int64_t reqId, const std::vector<ShareEntry>& hits, bool done, bool truncated,
                                       int code) override;

    //IShareVisitorObserver：一个选中目录的整棵目录树收齐了（拖出用）
    virtual void OnVisitorTreeResult(int64_t reqId, const std::string& path, const std::vector<ShareEntry>& entries,
                                     bool truncated, int code) override;

    //IShareFileListListener：文件列表上的鼠标与按键（多选、拖动检测）
    virtual void OnFileListMouseDown(POINT pt, UINT mkFlags) override;
    virtual void OnFileListMouseUp(POINT pt, UINT mkFlags) override;
    virtual void OnFileListDragStart(POINT pressPt) override;
    virtual void OnFileListDoubleClick(POINT pt) override;
    virtual bool OnFileListKeyDown(UINT vk) override;

    //IShareDragDelegate：拖出的放置开始了，把条目下载到拖出任务的临时目录
    virtual bool OnDragDownloadStart(const std::shared_ptr<CShareDragJob>& job) override;

    /**
     *  选中若干条目（替换原有选择；测试与程序选中用）。最后一项成为当前项。
     *  @param rows 列表里的行号；越界的忽略
     */
    void SelectRows(const std::vector<int>& rows);

    //选中的条目（行号从小到大）
    std::vector<int> GetSelectedRows() const;

    /**
     *  下载若干条目到 targetDir（不弹选目录框）。来自不同目录的条目（搜索结果）按目录分成几次传输。
     *  @param rows      列表里的行号
     *  @param targetDir 本机目标目录
     *  @return 至少发起了一次（或已排队等访问会话重新打开）返回 true
     */
    bool DownloadRows(const std::vector<int>& rows, const std::wstring& targetDir);

    /**
     *  删除若干条目（不弹确认框）。
     *  @param rows 列表里的行号
     *  @return 已发出返回 true
     */
    bool DeleteRows(const std::vector<int>& rows);

    /**
     *  把若干条目移到共享内的另一个目录（不弹窗口）。
     *  @param rows  列表里的行号
     *  @param toDir 目标目录（相对路径）
     *  @return 已发出返回 true
     */
    bool MoveRows(const std::vector<int>& rows, const std::string& toDir);

    /**
     *  上传拖进窗口的本机条目到当前目录（与松开鼠标相同；测试直接调用）。来自不同目录的条目按目录分成几次传输。
     *  @param localItems 本机条目的完整路径
     *  @return 至少发起了一次（或已排队）返回 true；没有上传权限、单文件共享、搜索结果状态下返回 false
     */
    bool UploadDroppedItems(const std::vector<std::wstring>& localItems);

    /**
     *  本窗口的放置目标（测试用：以程序方式把资源管理器的数据对象放进来）。
     *  @return 放置目标（所有权归本窗口，不要 Release）；窗口未打开时为 NULL
     */
    IDropTarget* GetUploadDropTarget();

    /**
     *  按当前选择准备一次拖出（拖动开始时调用；测试直接调用后把结果放到本机目录上）：需要时等待选中目录的目录树
     *  （等待期间处理消息，最多 kDragTreeWaitMs 毫秒），生成条目表，分配临时目录，建拖出任务与数据对象。
     *  @param interactive 为真时（用户正在拖动）松开左键即放弃等待
     *  @param result      [出参] share_drag_build；返回 NULL 且为 share_drag_build_ok 时表示没法建临时目录或窗口未打开
     *  @return 数据对象（引用计数为 1，调用方用完 Release）；不能拖出时为 NULL，原因已写在状态栏上
     */
    IDataObject* CreateDragOutData(bool interactive, int& result);

    //正在为拖出下载的传输数（测试用）
    size_t GetDragTransferCount() const;

    //IShareNameDialogListener：新建文件夹或重命名输入了名字
    virtual void OnShareNameEntered(int purpose, const std::wstring& name) override;

    //IShareFolderSource：选目标文件夹窗口要列一个目录
    virtual void RequestFolderList(const std::string& path) override;

    //IShareFolderPickListener：选好了目标文件夹
    virtual void OnShareFolderPicked(const std::string& dir) override;

    //IShareFolderPickListener：选目标文件夹窗口关闭了
    virtual void OnShareFolderPickClosed() override;

    //IShareTransferObserver：传输进度
    virtual void OnTransferProgress(uint32_t localId, int64_t bytesDone, int64_t bytesTotal, int64_t filesDone,
                                    int64_t filesTotal) override;

    //IShareTransferObserver：传输结束
    virtual void OnTransferFinished(uint32_t localId, int code) override;

    /**
     *  下载选中的条目到 targetDir（不弹选目录框；界面上的「下载」按钮先弹框再调用它，测试直接调用）。
     *  @param rowIndex  列表里的行号（已排序后的）；单文件共享传 0
     *  @param targetDir 本机目标目录
     *  @return 已发起（或已排队等访问会话重新打开）返回 true
     */
    bool DownloadRow(int rowIndex, const std::wstring& targetDir);

    /**
     *  把本机条目上传到当前目录（不弹对话框；按钮先弹框再调用它，测试直接调用）。
     *  @param localItems 本机条目的完整路径（同一目录下）
     *  @return 已发起（或已排队）返回 true；清单生成失败时在横幅上显示原因并返回 false
     */
    bool UploadItems(const std::vector<std::wstring>& localItems);

    /**
     *  在当前目录下新建文件夹（不弹窗口；名字输入窗口确认后调用它，测试直接调用）。
     *  @param name 文件夹名
     *  @return 已发出（或已排队等访问会话打开）返回 true
     */
    bool MakeFolder(const std::wstring& name);

    /**
     *  重命名一行（不弹窗口）。
     *  @param rowIndex 列表里的行号
     *  @param newName  新名字
     *  @return 已发出返回 true
     */
    bool RenameRow(int rowIndex, const std::wstring& newName);

    /**
     *  删除一行（不弹确认框；菜单项先确认再调用它）。
     *  @param rowIndex 列表里的行号
     *  @return 已发出返回 true
     */
    bool DeleteRow(int rowIndex);

    /**
     *  把一行移到共享内的另一个目录（不弹窗口）。
     *  @param rowIndex 列表里的行号
     *  @param toDir    目标目录（相对路径）
     *  @return 已发出返回 true
     */
    bool MoveRow(int rowIndex, const std::string& toDir);

    /**
     *  执行右键菜单的一项（与在菜单里点它相同：重命名、新建弹名字输入窗口，删除先确认，移动弹选目录窗口）。
     *  测试用它代替弹出菜单；菜单本身也经它执行。
     *  @param rowIndex 右键点中的行号；点在空白处为 -1
     *  @param item     share_browse_menu_item 之一
     */
    void ExecuteMenuItem(int rowIndex, int item);

    /**
     *  在当前目录之下按文件名搜索（与在搜索框里按回车相同；测试直接调用）。关键字为空时回到目录列表。
     *  @param keyword 关键字
     *  @return 已发出（或已排队）返回 true；关键字为空或窗口未打开时返回 false
     */
    bool StartSearch(const std::wstring& keyword);

    //是否在显示搜索结果（测试用）
    bool IsSearchMode() const;

    /**
     *  切换列表视图与缩略图视图（与点「缩略图 / 列表」按钮相同；测试直接调用）。
     *  @param grid 为真时切到缩略图视图
     */
    void SetGridMode(bool grid);

    //是否在缩略图视图（测试用）
    bool IsGridMode() const;

    //缩略图缓存里已解码好的图片张数（测试用）
    size_t GetCachedThumbCount() const;

    //名字输入窗口与选目标文件夹窗口（测试用）
    CShareNameDialog& GetNameDialog();
    CShareFolderPickDialog& GetFolderPickDialog();

    //状态栏的文字（测试用）
    std::wstring GetStatusText() const;

    //「新建文件夹」按钮是否显示（测试用）
    bool IsNewFolderButtonVisible() const;

    //传输列表的行数与某一行的状态（测试用）：0 进行中、1 完成、2 失败或取消
    size_t GetTransferRowCount() const;
    int GetTransferRowState(size_t index) const;

    /**
     *  当前显示的目录（相对路径），供测试与日志使用。
     *  @return 相对路径；共享根为空串
     */
    const std::string& GetCurrentPath() const;

    /**
     *  当前列表里的条目数（已按当前列排序）。
     */
    size_t GetEntryCount() const;

    /**
     *  按名字找当前列表里的一行（测试用）。
     *  @param name 条目名
     *  @return 行号；没有时为 -1
     */
    int FindRow(const std::wstring& name) const;

    /**
     *  横幅是否显示以及显示的文字，供测试使用。
     */
    bool IsBannerVisible() const;
    std::wstring GetBannerText() const;

    BEGIN_MSG_MAP(CShareBrowserWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        MESSAGE_HANDLER(WM_SIZE, OnSizeMsg)
        MESSAGE_HANDLER(WM_MOVE, OnMoveMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //WM_CLOSE：点标题栏关闭按钮或按 Alt+F4 时关闭窗口
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_TIMER：缩略图视图下定时按列表宽度重排、给可见的图片格子取缩略图；其余定时器交给基类
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_SIZE / WM_MOVE：窗口处于还原态时记下当前矩形（见 RememberNormalRect），不拦截消息，基类照常处理
    LRESULT OnSizeMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMoveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //窗口处于还原态（非最小化、非最大化）时记下当前窗口矩形，关窗时按它保存
    void RememberNormalRect();

    /**
     *  新建窗口后按保存的位置与大小摆放（没有保存过时在 owner 所在显示器的工作区居中），再按 m_cascadeFrom 错开。
     *  @param owner 参照窗口，可为 NULL（取主显示器）
     *  @return 上次关闭时是最大化的，返回 true，调用方显示窗口时最大化
     */
    bool ApplySavedPlacement(HWND owner);

    //关窗时把还原态矩形与是否最大化写进配置文件的 [ShareBrowser] 小节；没有记下矩形或没有配置文件路径时不写
    void SavePlacement();

    //WM_DUI_NOTIFY：工具栏与表头按钮的点击、文件列表上的右键
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //文件列表上点了右键：选中光标所在的行，弹出右键菜单并执行选中的项
    void ShowContextMenu();

    //光标此刻在哪个条目上（列表视图为行，缩略图视图为格子）；不在任何条目上时为 -1
    int RowAtCursor() const;

    //宿主客户区里的一个点落在哪个条目上；不在任何条目上时为 -1
    int EntryAt(POINT pt) const;

    //只选中一个条目；-1 为清空选择
    void SelectEntry(int index);

    //当前项：选中的条目里最近单击的那一个（不在选择里时取第一个选中的）；没有选中时为 -1
    int SelectedEntry() const;

    //菜单项作用的条目：rowIndex 在选择里时为全部选中的条目，否则只有 rowIndex 这一项；rowIndex 为 -1 时为空
    std::vector<int> ActionRows(int rowIndex) const;

    //把选择换成路径（排序、追加搜索结果之前）与按路径恢复选择（之后）
    std::vector<std::string> SelectedPaths() const;
    void RestoreSelection(const std::vector<std::string>& paths);

    //选中的目录里还没有目录树的，排队向分享者要（同一时刻只有一个目录树请求）
    void PrefetchDragTrees();

    //发出排队的下一个目录树请求
    void RequestNextDragTree();

    //拖出：鼠标拖动开始后建数据对象并进入 DoDragDrop
    void StartDragOut();

    //把拖出的顶层条目下载到任务的临时目录（访问会话就绪时调用）
    bool StartDragDownloads(const std::shared_ptr<CShareDragJob>& job);

    //窗口（重新）打开时接回这个共享上仍在后台进行的传输，重建传输列表（第 4 期起）
    void AdoptBackgroundTransfers();

    //下载若干条目：按所在目录分组，每组一次传输；访问会话未就绪时由业务总控排队并重新打开
    bool StartDownloadItems(const std::vector<ShareTransferEntry>& items, const std::wstring& targetDir);

    //传输列表里显示的名称：一项时为它的名字，多项时为「第一项 等 N 项」
    std::wstring TransferDisplayName(const std::vector<ShareTransferEntry>& items) const;

    //拖入：当前是否允许上传到这里（有上传权限、不是单文件共享、不在搜索结果）
    bool CanUploadHere() const;

    //DuiDropTargetHelper 的回调（经 std::bind 绑定到本窗口）：拖入、拖离、放下文件
    bool OnUploadDragEnter(const POINT& pt, bool hasFiles);
    void OnUploadDragLeave();
    void OnUploadFilesDropped(const std::vector<CString>& paths);

    //按当前视图设置列表的行高与行数（列表视图每行一个条目，缩略图视图每行若干格）
    void RefreshListRows();

    //缩略图视图：给可见区域里还没有缩略图的图片发请求（同时在途的不超过 kMaxThumbsInFlight 张）
    void RequestVisibleThumbs();

    //清空缩略图缓存（释放位图），在途的请求一并取消
    void ClearThumbCache();

    //画缩略图视图的一行
    void PaintGridRow(HDC hdc, int rowIndex, const RECT& rowRect);

    //构建客户区控件树。每次创建窗口后调用一次
    void BuildContent();

    //请状态机列一个目录，状态栏显示「正在加载」
    void RequestList(const std::string& path);

    //点了表头的某一列：同一列反向，换列时按默认方向，然后重新排序
    void OnHeaderClicked(int column);

    //按当前列与方向排序，刷新表头文字与列表
    void ApplySort();

    //刷新路径文字（共享名称 / 各级目录）与「上一级」按钮的可用状态
    void UpdatePathBar();

    //共享根在界面上的称呼：窗口标题、路径栏第一节与「移动到」选目录框的根节点都用它。依次取服务端给的共享名称、
    //分享者握手时给的根名（2026-10-04 起只有单文件共享才有），都没有时为窗口默认标题「文件共享」
    std::wstring RootLabel() const;

    //显示或隐藏横幅；显隐变化后对父容器 ForceLayout
    void SetBanner(const std::wstring& text);

    //设置状态栏文字
    void SetStatus(const std::wstring& text);

    //文件列表的画行回调（user 为本窗口）；selected 不用，选中与否按本窗口的选择集
    static void PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);

    //画一行：选中或悬停的底色、类型图标、名称、大小、修改时间
    void PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool hover);

    //一行条目相对共享根的路径：目录列表时为当前目录拼上名字，搜索结果时名字本身就是路径
    std::string RowPath(int rowIndex) const;

    //搜索结果的状态栏文字：进行中、结束、截断
    void UpdateSearchStatus(bool done, bool truncated);

    //请求预览列表里第 m_previewIndex 张图片的预览图
    void RequestPreview();

    //预览图的长边像素数：本窗口所在显示器的长边，不超过 kShareThumbMaxEdge
    int PreviewEdge() const;

    //双击一行：目录则进入
    void OnRowActivated(int rowIndex);

    //「下载」「上传文件」「上传文件夹」「取消传输」按钮
    void OnDownloadClicked();
    void OnUploadClicked(bool folders);
    void OnCancelTransferClicked();

    //按权限与共享类型显示或隐藏上传与新建文件夹按钮；显隐变化后对工具栏 ForceLayout
    void UpdateUploadButtons();

    //新增一行传输并显示传输列表
    void AddTransferRow(uint32_t localId, int direction, const std::wstring& name);

    //一组本机条目上传时显示的名称：一项时为它的文件名，多项时为「第一项 等 N 项」
    std::wstring UploadDisplayName(const std::vector<std::wstring>& localItems) const;

    //传输列表的画行回调
    static void PaintTransferRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);
    void PaintTransferRow(HDC hdc, int rowIndex, const RECT& rowRect);

    //一次拖出的下载进度：拖出任务的顶层条目可能来自不同目录，分成几次传输，全部结束后才标记任务结束。
    //第 4 期起作为这几笔传输的结束回调交给业务总控共同持有，浏览窗口关闭之后下载照常进行，结束时照常标记拖出任务
    class DragDownload : public IShareTransferCompletion
    {
    public:
        /**
         *  @param job       拖出任务（共同持有）
         *  @param transfers 分成的传输笔数，大于 0
         */
        DragDownload(const std::shared_ptr<CShareDragJob>& job, size_t transfers);

        //一笔传输结束：记下第一个失败的错误码，全部结束时标记拖出任务结束（流的读取随即继续或失败）
        virtual void OnTransferDone(int code) override;

    private:
        std::shared_ptr<CShareDragJob> m_job;        //拖出任务
        size_t                         m_remaining;  //还没结束的传输数
        int                            m_code;       //第一个失败的传输的错误码；都成功时为 0
    };

    //传输列表的一行
    struct TransferRow
    {
        uint32_t     m_localId;     //传输编号
        int          m_direction;   //方向
        std::wstring m_name;        //显示的名称
        int64_t      m_bytesDone;   //已完成字节数
        int64_t      m_bytesTotal;  //总字节数
        int          m_state;       //0 进行中、1 完成、2 失败或取消
        int          m_code;        //失败时的错误码

        TransferRow()
            : m_localId(0)
            , m_direction(0)
            , m_bytesDone(0)
            , m_bytesTotal(0)
            , m_state(0)
            , m_code(0)
        {
        }
    };

private:
    CShareService*               m_service;        //业务总控（不拥有）；窗口打开期间有效
    CShareVisitor*               m_visitor;        //访问状态机（归 m_service 所有）；关闭窗口时释放并置 NULL
    int64_t                      m_shareId;        //共享 id
    std::wstring                 m_placementIni;   //保存窗口位置的配置文件全路径（SetPlacement 设置）；为空时不读也不保存
    HWND                         m_cascadeFrom;    //新建窗口时相对它错开的另一个浏览窗口（SetPlacement 设置，不拥有）；用过即清空
    RECT                         m_lastNormalRect; //最近一次处于还原态时的窗口矩形（屏幕坐标），关窗时保存
    bool                         m_hasNormalRect;  //m_lastNormalRect 是否有效（本次打开后记下过）
    std::string                  m_currentPath;    //当前显示的目录（相对路径）
    std::string                  m_requestedPath;  //最近一次请求的目录；应答到达后成为当前目录
    std::vector<ShareEntry>      m_entries;        //当前目录的条目，已按当前列排序
    int                          m_sortColumn;     //当前排序列，share_sort_column
    bool                         m_sortAscending;  //当前排序方向
    int                          m_perms;          //本人的写权限位（来自 sf_hello、sf_perms、2206）
    std::wstring                 m_shareStateText; //2203 带来的横幅文字；为空表示共享可用
    std::wstring                 m_errorText;      //最近一次打开或列目录失败的文字；成功后清空
    std::vector<TransferRow>     m_transfers;      //传输列表的各行，最新的在最后
    std::string                  m_renamePath;     //名字输入窗口正在为它取新名字的条目（相对路径）；新建文件夹时为空
    std::vector<std::string>     m_movePaths;      //选目标文件夹窗口打开期间，要移动的条目（相对路径）
    std::wstring                 m_opNotice;       //写操作成功后要在刷新完的状态栏上附带的文字；显示后清空
    bool                         m_searchMode;     //列表是否在显示搜索结果
    int64_t                      m_searchReqId;    //当前搜索的请求号；没有时为 0
    std::wstring                 m_searchKeyword;  //当前搜索的关键字
    CShareNameDialog             m_nameDialog;     //名字输入窗口（新建文件夹、重命名）
    CShareFolderPickDialog       m_folderPick;     //选目标文件夹窗口（移动到…）
    IShareUiHost*                m_uiHost;         //界面宿主（不拥有）；打开看图窗用，可为 NULL
    std::vector<std::string>     m_previewPaths;   //预览时可以翻页的图片（相对路径），按列表顺序
    size_t                       m_previewIndex;   //当前预览的是 m_previewPaths 里的第几张
    int64_t                      m_previewReqId;   //正在等的预览图请求号；没有时为 0

    //缩略图缓存的一项：解码好的位图，或在途的请求号
    struct ThumbCell
    {
        HBITMAP m_bitmap;   //解码好的位图（本窗口拥有，清空缓存时 DeleteObject）；还没有时为 NULL
        int     m_width;    //位图宽（像素）
        int     m_height;   //位图高（像素）
        int64_t m_reqId;    //在途的请求号；已有结果时为 0
        bool    m_failed;   //取或解码失败（不再重试，画文件图标）

        ThumbCell()
            : m_bitmap(NULL)
            , m_width(0)
            , m_height(0)
            , m_reqId(0)
            , m_failed(false)
        {
        }
    };
    bool                         m_gridMode;       //是否在缩略图视图
    int                          m_gridColumns;    //缩略图视图当前的列数（列表宽度变化时重算）
    std::map<std::string, ThumbCell> m_thumbCache; //缩略图缓存：相对路径 → 位图或在途请求
    size_t                       m_thumbInFlight;  //在途的缩略图请求数

    ShareSelection               m_selection;      //选中的条目（列表视图与缩略图视图共用）；列表内容换掉时清空
    int                          m_pressIndex;     //最近一次左键按下的条目；按在空白处为 -1
    int                          m_deferredSelect; //按在已选中的多项之一上时推迟到松开才单选的条目；没有时为 -1
    std::map<std::string, std::vector<ShareEntry> > m_dragTrees;  //选中目录的整棵目录树（拖出用），列表内容换掉时清空
    std::map<std::string, int>   m_dragTreeErrors; //取目录树失败的目录 → 错误码（条目超过上限记为 kDragTreeTooMany）
    std::vector<std::string>     m_treeQueue;      //排队等着要目录树的目录
    std::string                  m_treePath;       //正在要目录树的目录；没有时为空
    int64_t                      m_treeReqId;      //正在进行的目录树请求号；没有时为 0
    //本窗口发起的、尚未结束的拖出下载：传输编号 → 所属的下载进度（只用于计数；拖出任务的结束由业务总控经结束回调完成）
    std::map<uint32_t, std::shared_ptr<DragDownload> > m_dragByTransfer;
    std::unique_ptr<balloonwjui::DuiDropTargetHelper> m_dropHelper;      //拖入上传的放置目标；窗口打开期间注册
    std::wstring                 m_statusBeforeDrop; //拖入经过窗口时状态栏原来的文字（拖离时恢复）
    bool                         m_dropHintShown;  //状态栏是否正显示拖入上传的提示

    //以下控件归窗口的控件树所有，窗口创建后有效、销毁后失效
    balloonwjui::DuiVBox*        m_root;           //客户区根容器（横幅显隐后对它 ForceLayout）
    balloonwjui::DuiVBox*        m_bannerBox;      //横幅的带底色容器（显隐的是它）
    balloonwjui::DuiLabel*       m_banner;         //横幅文字
    balloonwjui::DuiButton*      m_upButton;       //「上一级」
    balloonwjui::DuiLabel*       m_pathLabel;      //当前路径
    balloonwjui::DuiButton*      m_refreshButton;  //「刷新」
    balloonwjui::DuiHBox*        m_toolbar;        //操作行（上传与新建按钮显隐后对它 ForceLayout）
    balloonwjui::DuiEdit*        m_searchBox;      //搜索框
    balloonwjui::DuiButton*      m_viewToggle;     //「缩略图 / 列表」切换按钮
    balloonwjui::DuiButton*      m_downloadButton; //「下载」
    balloonwjui::DuiButton*      m_uploadFiles;    //「上传文件」
    balloonwjui::DuiButton*      m_uploadFolder;   //「上传文件夹」
    balloonwjui::DuiButton*      m_newFolder;      //「新建文件夹」
    balloonwjui::DuiVBox*        m_transferPanel;  //传输列表区（有过传输才显示）
    balloonwjui::DuiVirtualList* m_transferList;   //传输列表
    balloonwjui::DuiHBox*        m_header;         //表头（缩略图视图下隐藏）
    balloonwjui::DuiButton*      m_headerName;     //表头：名称
    balloonwjui::DuiButton*      m_headerSize;     //表头：大小
    balloonwjui::DuiButton*      m_headerTime;     //表头：修改时间
    CShareFileList*              m_list;           //文件列表
    balloonwjui::DuiLabel*       m_status;         //状态栏
    balloonmain::ProgramWindowIcons m_programIcons;  //标题栏与任务栏 / Alt-Tab 的程序图标，本对象持有，跨关闭再打开复用
};

} // namespace fileshare
