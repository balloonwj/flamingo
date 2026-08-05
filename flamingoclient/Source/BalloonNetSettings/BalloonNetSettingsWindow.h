/**
 *  BalloonNetSettingsWindow.h
 *
 *  新版网络设置窗口 —— 基于 balloonui，对照 design/netsettings.png。
 *  顶部三张服务器卡片（聊天 / 图片 / 文件）可单选切换，下方表单分别
 *  编辑当前选中那一组的"地址 / 端口 / TLS 加密"。"保存"把三组配置写回
 *  flamingo.ini 的 [server] 段。
 *
 *  与现有 CLoginSettingsDlg（旧版 MFC 网络设置框）并存：本窗口是独立的
 *  balloonui 窗口，调试期由 CMainDlg 的 --netsettings 入口直接弹出；接入
 *  登录流程 / 替换旧框另议。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "NetSettingsModel.h"

class CFlamingoClient;

// "测试连接"按钮发起的后台探测完成时回投给本窗口的消息。
// wParam 未用；lParam 为 NetTestTask*（堆对象，由 OnTestResult 取用并释放）。
#define WM_NETTEST_RESULT   (WM_APP + 0x21)

// 开窗时对三组服务器各自发起的后台探测完成时回投给本窗口的消息。与
// WM_NETTEST_RESULT 分开是因为二者的善后不同：本消息只回写卡片状态、
// 不弹对话框，也不涉及"测试连接"按钮的文字与防重入。
// wParam 未用；lParam 为 NetTestTask*（堆对象，由 OnProbeResult 取用并释放）。
#define WM_NETPROBE_RESULT  (WM_APP + 0x22)

/**
 *  CBalloonNetSettingsWindow
 *
 *  网络设置窗口。继承 balloonwjui::DuiFrameWindow —— 由它提供无边框
 *  自绘窗体与标题栏。固定尺寸、不可缩放，标题栏只保留关闭按钮。
 */
class CBalloonNetSettingsWindow : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象（消息中枢），本类只引用、不持有。
     */
    explicit CBalloonNetSettingsWindow(CFlamingoClient* pFMGClient);
    ~CBalloonNetSettingsWindow();

    /**
     *  创建并显示网络设置窗口（非模态、无父窗口）。
     *    hMainDlg：CMainDlg 的窗口句柄，<u>只为 --netsettings 调试入口而设</u>：
     *              那条路径下本窗口是唯一可见窗口，关闭它就该结束进程，故关闭时
     *              据此向 CMainDlg 投 WM_CLOSE_MAINDLG（见 OnCloseMsg）。
     *              <b>登录后从主面板系统菜单打开时必须传 NULL</b> —— 否则关掉这个
     *              小窗会把整个程序一起带走。与主面板开群管理窗 / 资料窗同一口径。
     */
    void CreateAndShow(HWND hMainDlg);

    /**
     *  以模态对话框形式打开网络设置窗口（供登录窗等调用）。
     *  在 hWndParent 上跑独立消息循环，模态期间禁用父窗口，关闭后返回。
     *    hWndParent：父窗口句柄（如登录窗），模态期间被禁用。
     *  返回值：IDCANCEL（本窗口仅"关闭"一种退出方式）。
     */
    INT_PTR DoModal(HWND hWndParent);

    BEGIN_MSG_MAP(CBalloonNetSettingsWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_NETTEST_RESULT, OnTestResult)
        MESSAGE_HANDLER(WM_NETPROBE_RESULT, OnProbeResult)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    // 创建窗口、配置外观、构建客户区并显示。hWndParent 为父窗口（可为
    // NULL）。成功返回 true。CreateAndShow 与 DoModal 共用此方法。
    bool BuildWindow(HWND hWndParent);

    // 结束 DoModal 的模态循环，result 为 DoModal 的返回值。
    void EndModal(INT_PTR result);

    // 构建客户区控件树、接好输入框、从 ini 载入并填充表单。
    void InitUi();

    // WM_CLOSE：调试入口下关闭即结束程序，转交 CMainDlg。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_DUI_NOTIFY：处理卡片切换 / 测试连接 / 保存 / TLS 开关。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 从 flamingo.ini 读三组服务器配置到 m_model。
    void LoadModelFromIni();

    // 把 m_model 三组服务器配置写回 flamingo.ini。
    void SaveModelToIni();

    // 把表单（地址 / 端口 / TLS）的当前内容提交回 m_model 的选中组。
    void CommitFormToModel();

    // 把 m_model 选中组的配置灌进表单。
    void LoadFormFromModel();

    // 按 m_model 刷新三张卡片的选中态 / 端点副标题 / 连接状态。
    void RefreshCards();

    // 切换正在编辑的服务器组：先提交当前表单、再载入新组、刷新卡片。
    //   kind：目标服务器组，取值见 balloonnetsettings::NetServerKind。
    void SwitchToServer(int kind);

    // "测试连接"按钮：在后台线程做 TCP 连接探测，避免阻塞 UI。
    void DoTestConnection();

    // 对三组服务器各起一个后台线程做一次 TCP 可达性探测，三张卡片先置为
    // "连接中"，各自的结果回来后再改成"已连接" / "未连接"。开窗时与"保存"
    // 之后（地址可能被改过）各调一次。地址为空 / 端口非法的那组不起线程，
    // 直接判为"未连接"。
    void StartProbeAll();

    // 起一个后台探测线程。
    //   kind：      本次探测针对哪一组服务器（见 NetServerKind），随结果回来
    //               用于定位要回写的卡片。
    //   host/port： 待探测的地址与端口。
    //   resultMsg： 探测完成后回投本窗口的消息（WM_NETTEST_RESULT 或
    //               WM_NETPROBE_RESULT）。
    //   返回值：线程创建成功返回 true；失败返回 false（任务对象已就地释放）。
    bool StartProbe(int kind, LPCTSTR host, int port, UINT resultMsg);

    // 后台探测线程入口，"测试连接"按钮与开窗自动探测共用。pParam 为
    // NetTestTask*（堆对象）；探测完把它作为 LPARAM、经任务里记的 resultMsg
    // 回投本窗口。窗口已销毁导致回投失败时由本线程自行释放，杜绝泄漏。
    static unsigned __stdcall ProbeThreadProc(void* pParam);

    // WM_NETTEST_RESULT：按钮发起的探测完成 —— 回写卡片状态、恢复按钮文字，
    // 并弹出结果对话框。
    LRESULT OnTestResult(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_NETPROBE_RESULT：开窗自动探测完成 —— 只回写卡片状态，不打扰用户。
    LRESULT OnProbeResult(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 把一次探测结果回写到对应卡片并重绘。
    //   kind：      结果属于哪一组服务器。
    //   host/port： 发起探测时用的地址端口，用于识别过期结果。
    //   ok：        true=连得上。
    // 若发起探测后模型里这一组的地址端口已经被改过（切卡时提交了编辑 /
    // 保存过新值），这次结果就是过期的：丢弃它，并按新值重新探一次，
    // 避免卡片长期停在"连接中"或显示旧地址的结论。
    void ApplyProbeResult(int kind, LPCTSTR host, int port, bool ok);

    // 设置"测试连接"按钮的显示文字（"测试中…" / "测试连接"）并触发重绘。
    void SetTestButtonText(LPCTSTR text);

    // "保存"按钮：提交表单 + 写回 flamingo.ini。
    void DoSave();

private:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端（消息中枢），只引用不持有
    HWND             m_hMainDlg;      // CMainDlg 句柄（仅 --netsettings 调试入口下"关闭即退出程序"用；真实打开时为 NULL）
    bool             m_modalRunning;  // DoModal 模态循环是否进行中
    INT_PTR          m_modalResult;   // DoModal 的返回值
    bool             m_testing;       // "测试连接"后台探测是否进行中（防重入）
    balloonnetsettings::NetSettingsModel m_model;   // 三组服务器配置模型

    // 三张卡片当前显示的连接状态，下标对应 NetServerKind，取值见 NetConnState。
    // 由后台探测结果驱动：发起探测时置"连接中"，结果回来改成"已连接" / "未连接"。
    // 只在 UI 线程读写（后台线程一律经消息回投，不直接碰它）。
    int              m_connState[balloonnetsettings::kNetServerCount];
};
