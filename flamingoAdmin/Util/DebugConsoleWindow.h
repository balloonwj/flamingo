/**
 *  flamingoAdmin —— 调试控制台窗口（独立 Win32/ATL 顶层窗口）。
 *
 *  本文件提供调试控制台的【界面层】。数据层 DebugConsole（见 DebugConsole.h）
 *  把每一次「请求—响应」往返记录进环形缓冲；本窗口订阅其 sink 通知，
 *  在 UI 线程把这些记录实时呈现给开发者：
 *    · 顶部工具条 —— 清空 / 暂停 / 导出 / 自动滚动 / 仅看错误 / 计数。
 *    · 中部 ListView —— 报表式列出全部往返，状态用彩色圆点直观区分。
 *    · 底部两栏只读 EDIT —— 显示选中行的请求 JSON 与响应 JSON。
 *
 *  实现仅使用标准 Win32 公共控件（ListView / EDIT / BUTTON / STATIC），
 *  【不依赖】balloonui DUI 库，故可独立编译、独立调试。
 *
 *  线程模型：本窗口的全部代码都在【UI 线程】执行。DebugConsole 在工作线程里
 *  改动缓冲后只对本窗口 PostMessage(WM_DC_REFRESH)，刷新动作落到 UI 线程。
 *
 *  典型用法（在宿主主窗口里）：
 *      // 某菜单 / 快捷键命令处理函数中
 *      flamingoadmin::DebugConsoleWindow::Instance().Toggle(m_hWnd);
 *      // 宿主主窗口销毁前
 *      flamingoadmin::DebugConsoleWindow::Instance().Destroy();
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include <atlbase.h>
#include <atlwin.h>
#include <vector>

#include "Util/DebugConsole.h"

namespace flamingoadmin {

// 调试控制台窗口：进程级单例，独立的 Win32/ATL 顶层窗口。
//
// 单例风格与本仓库其余管理器（DebugConsole / HttpWorker / Logger）一致：
// 整个进程只需一个调试控制台，懒创建、可反复显示 / 隐藏。
//
// 生命周期：
//   · 首次 Toggle() —— 注册窗口类、创建窗口、登记 DebugConsole sink、显示。
//   · 之后 Toggle() —— 仅在显示 / 隐藏间切换，不重建窗口。
//   · WM_CLOSE      —— 仅隐藏窗口，不销毁（保留已加载状态）。
//   · Destroy()     —— 摘除 sink 后真正销毁窗口，宿主退出前调用。
class DebugConsoleWindow : public ATL::CWindowImpl<DebugConsoleWindow>
{
public:
    // 窗口类名。注册自定义窗口类时使用，本类全局唯一。
    DECLARE_WND_CLASS(_T("FlamingoAdmin.DebugConsoleWindow"))

    // 取全局唯一实例。
    //   返回：进程级单例引用，生命周期同进程。
    static DebugConsoleWindow& Instance();

    // 懒创建窗口并切换显示 / 隐藏。
    //   owner：宿主主窗口句柄，作为本窗口的 owner —— 使调试控制台浮在主窗口
    //          之上、随主窗口最小化而最小化。首次调用须传有效句柄；窗口已存在
    //          时本参数被忽略。
    //   副作用：首次调用会注册窗口类、创建窗口并向 DebugConsole 登记 sink。
    void Toggle(HWND owner);

    // 查询窗口当前是否已创建且处于可见态。
    //   返回：true 表示窗口已创建且可见；窗口未创建或已隐藏均返回 false。
    bool IsVisible() const;

    // 摘除 DebugConsole 的 sink 并销毁窗口。
    //   先解除 sink 登记（避免工作线程再向已失效句柄投递消息），再销毁窗口，
    //   最后复位内部状态。窗口从未创建时调用本函数安全、无副作用。
    void Destroy();

private:
    DebugConsoleWindow();
    ~DebugConsoleWindow();
    DebugConsoleWindow(const DebugConsoleWindow&) = delete;
    DebugConsoleWindow& operator=(const DebugConsoleWindow&) = delete;

    // ---- 自定义消息 ----------------------------------------------------
    // DebugConsole 在工作线程改动缓冲后，对本窗口 PostMessage 此消息码；
    // 本窗口在 UI 线程收到后做一次全量刷新。取 WM_APP 之上的值避免与系统、
    // 公共控件的消息冲突。
    static const UINT WM_DC_REFRESH = WM_APP + 0x100;

    // ---- 子控件命令 ID -------------------------------------------------
    // 工具条各控件的命令 ID。从 1001 起顺序编号，避开 0 与系统保留的小值。
    enum ControlId
    {
        kIdBtnClear      = 1001,   // 「清空」按钮
        kIdBtnPause      = 1002,   // 「暂停 / 继续」按钮
        kIdBtnExport     = 1003,   // 「导出…」按钮
        kIdChkAutoScroll = 1004,   // 「自动滚动」复选框
        kIdChkErrorOnly  = 1005,   // 「仅看错误」复选框
        kIdStaticCount   = 1006,   // 「共 N 条」计数静态文本
        kIdListView      = 1007,   // 中部往返记录 ListView
        kIdStaticReqCap  = 1008,   // 「请求」分栏标题静态文本
        kIdStaticRespCap = 1009,   // 「响应」分栏标题静态文本
        kIdEditReq       = 1010,   // 左下请求 JSON 只读 EDIT
        kIdEditResp      = 1011,   // 右下响应 JSON 只读 EDIT
    };

    // ---- ListView 列索引 ----------------------------------------------
    // 报表式 ListView 的 6 个列下标，建表与 LVN_GETDISPINFO 取文本时共用。
    enum ColumnIndex
    {
        kColSeq      = 0,   // 「#」自增序号列
        kColTime     = 1,   // 「时间」请求发起时刻列
        kColApi      = 2,   // 「接口」请求路径末段列
        kColStatus   = 3,   // 「状态」进度 / HTTP 状态 / 传输错误列
        kColElapsed  = 4,   // 「耗时」往返毫秒数列
        kColBizCode  = 5,   // 「业务码」响应体顶层 code 列
        kColumnCount = 6,   // 列总数（建表循环上界，非真实列）
    };

    // ---- ImageList 中状态圆点的图标下标 -------------------------------
    // 与 DebugEntryKind 一一对应：圆点颜色据记录结果类别选取。
    enum StatusIconIndex
    {
        kIconGrey   = 0,   // 灰点：进行中（DebugEntryKind::Pending）
        kIconGreen  = 1,   // 绿点：业务成功（DebugEntryKind::Success）
        kIconOrange = 2,   // 橙点：业务失败（DebugEntryKind::BusinessFail）
        kIconRed    = 3,   // 红点：传输失败（DebugEntryKind::TransportFail）
        kIconCount  = 4,   // 圆点总数（绘制循环上界，非真实图标）
    };

    // ---- 消息映射 -----------------------------------------------------
    BEGIN_MSG_MAP(DebugConsoleWindow)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_DC_REFRESH, OnDcRefresh)
        MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
    END_MSG_MAP()

    // WM_CREATE：创建工具条 / ListView / 两栏 EDIT 等全部子控件，建列、建
    // 状态圆点 ImageList，置控件初始字体。返回 0 表示创建成功。
    LRESULT OnCreate(UINT msg, WPARAM wParam, LPARAM lParam, BOOL& handled);

    // WM_SIZE：按当前客户区尺寸重新排布全部子控件。
    LRESULT OnSize(UINT msg, WPARAM wParam, LPARAM lParam, BOOL& handled);

    // WM_CLOSE：仅隐藏窗口（不销毁），保留已加载状态供下次 Toggle 复用。
    LRESULT OnClose(UINT msg, WPARAM wParam, LPARAM lParam, BOOL& handled);

    // WM_DESTROY：释放本窗口持有的 GDI / ImageList / 字体资源。
    LRESULT OnDestroy(UINT msg, WPARAM wParam, LPARAM lParam, BOOL& handled);

    // WM_DC_REFRESH：收到数据层通知，做一次全量刷新（重建 m_view、刷列表）。
    LRESULT OnDcRefresh(UINT msg, WPARAM wParam, LPARAM lParam, BOOL& handled);

    // WM_NOTIFY：分发 ListView 的 LVN_GETDISPINFO（虚拟列表取文本 / 取图标）
    // 与 LVN_ITEMCHANGED（选中行变化时回填详情面板）。
    LRESULT OnNotify(UINT msg, WPARAM wParam, LPARAM lParam, BOOL& handled);

    // WM_COMMAND：分发工具条按钮 / 复选框命令（清空 / 暂停 / 导出 / 两个复选框）。
    LRESULT OnCommand(UINT msg, WPARAM wParam, LPARAM lParam, BOOL& handled);

    // ---- 内部辅助 -----------------------------------------------------
    // 按当前客户区尺寸重排全部子控件。OnSize / OnCreate 末尾共用。
    void LayoutChildren();

    // 全量刷新：取 DebugConsole 快照、据「仅看错误」重建 m_view、刷新 ListView
    // 行数与计数文本，「自动滚动」开启时滚到末行。
    void RefreshView();

    // 把选中行的请求 / 响应 JSON 回填到底部两栏 EDIT。
    //   index：m_view 中的下标；越界时清空两栏 EDIT。
    void UpdateDetailPanes(int index);

    // 创建 4 张 16x16 状态圆点位图并装入 ImageList，赋给 ListView 的小图标。
    //   返回：true 表示 ImageList 创建并赋值成功。
    bool BuildStatusImageList();

    // 用顶栏「调试控制台」按钮同款的终端矢量图标渲染窗口大 / 小图标，并经
    // WM_SETICON 设给本窗口（标题栏 + 任务栏 + Alt+Tab）。OnCreate 调用一次。
    void ApplyWindowIcon();

    // 「导出…」命令实现：弹保存对话框，把 FormatAllForExport 文本以 UTF-8
    // （含 BOM）写入选定文件。
    void DoExport();

    // 处理 LVN_GETDISPINFO：为虚拟列表的某个单元格填文本 / 填状态圆点图标。
    //   dispInfo：公共控件回传的显示信息结构指针，所有权属控件，本函数只填字段。
    void OnGetDispInfo(NMLVDISPINFO* dispInfo);

    // 把 UTF-8 字节的 JSON 文本转成可直接塞进 EDIT 的 UTF-16 文本：
    // 完成 CP_UTF8 解码，并把裸 '\n' 换行规整为 EDIT 认得的 "\r\n"。
    //   utf8：UTF-8 编码的源文本。
    //   返回：转换后的 UTF-16 文本。
    static CString Utf8ToEditText(const std::string& utf8);

    // ---- 成员变量 -----------------------------------------------------
    HWND     m_hwndOwner;      // 宿主主窗口句柄；首次 Toggle 时记下，用于定位 / 设 owner
    HWND     m_hwndBtnClear;   // 「清空」按钮句柄
    HWND     m_hwndBtnPause;   // 「暂停 / 继续」按钮句柄
    HWND     m_hwndBtnExport;  // 「导出…」按钮句柄
    HWND     m_hwndChkAuto;    // 「自动滚动」复选框句柄
    HWND     m_hwndChkError;   // 「仅看错误」复选框句柄
    HWND     m_hwndCount;      // 「共 N 条」计数静态文本句柄
    HWND     m_hwndList;       // 中部往返记录 ListView 句柄
    HWND     m_hwndReqCap;     // 「请求」分栏标题静态文本句柄
    HWND     m_hwndRespCap;    // 「响应」分栏标题静态文本句柄
    HWND     m_hwndEditReq;    // 左下请求 JSON 只读 EDIT 句柄
    HWND     m_hwndEditResp;   // 右下响应 JSON 只读 EDIT 句柄

    HFONT    m_hMonoFont;      // 两栏 JSON EDIT 用的等宽字体（Consolas）；本类负责销毁
    HIMAGELIST m_hImageList;   // ListView 小图标 ImageList，存 4 张状态圆点；本类负责销毁
    HICON    m_hIconBig;       // 窗口大图标（任务栏 / Alt+Tab）；本类负责 DestroyIcon
    HICON    m_hIconSmall;     // 窗口小图标（标题栏左上角）；本类负责 DestroyIcon

    bool     m_autoScroll;     // 「自动滚动」勾选态：刷新后是否滚到末行
    bool     m_errorOnly;      // 「仅看错误」勾选态：m_view 是否只保留失败记录
    int      m_selectedIndex;  // 当前选中行在 m_view 中的下标；-1 表示无选中

    // 当前展示（可能已按「仅看错误」筛过）的记录集合。每次刷新由 DebugConsole
    // 的快照重建，ListView 为虚拟列表（LVS_OWNERDATA），按下标读本向量取文本。
    std::vector<DebugEntry> m_view;
};

} // namespace flamingoadmin
