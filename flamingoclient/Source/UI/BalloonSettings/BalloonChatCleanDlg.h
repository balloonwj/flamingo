/**
 *  「清理聊天记录」对话框，BalloonChatCleanDlg.h
 *
 *  从「系统设置 → 聊天 → 清理聊天记录」弹出，按时间范围删除本人在本机与服务器上的单聊和群聊
 *  记录，对方不受影响（docs/清理聊天记录方案.md §11）。时间范围可选 10 个预设档位之一，也可在
 *  开始、结束两组「年 / 月 / 日 / 时 / 分」下拉框里自定义；用户手动改了任一时间，档位切到「自定义」。
 *
 *  职责分工：
 *    · 时间换算、校验、说明文字由 ChatCleanRange 计算（纯逻辑、有单测）；
 *    · 按钮可用性、"清理中"、成功 / 失败 / 超时 / 断线的走向由 ChatCleanFlow 管理（纯逻辑、有单测）；
 *    · 本类只管控件树、把界面事件转告上面两者、按它们的结果刷新界面，以及发请求和计时。
 *
 *  请求发出后界面等 1521 的应答（经主面板 → 系统设置窗转交 OnCleanResult）；本机记录的删除由主面板
 *  收到 1522 区间推送后分批完成，与本对话框无关 —— 即使用户在清理中就关掉了对话框，删除照常进行。
 *
 *  全部使用 balloonui 现成控件（DuiComboBox / DuiLabel / DuiButton / 布局盒），不改 balloonui。
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

#include <stdint.h>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY
#include "ChatCleanFlow.h"    // 成员需完整类型
#include "ChatCleanRange.h"   // LocalMinute

/**
 *  CBalloonChatCleanDlg —— 清理聊天记录对话框（非模态浮窗，owner = 系统设置窗）。
 */
class CBalloonChatCleanDlg : public balloonwjui::DuiFrameWindow
{
public:
    CBalloonChatCleanDlg();
    ~CBalloonChatCleanDlg();

    /**
     *  创建并显示对话框（已创建则前置显示，保留当前选择与进度）。
     *    owner：    宿主窗口（系统设置窗）句柄，仅作语义留存。
     *    connected：当前是否已连接服务器；未连接时「清理」置灰并说明原因。
     */
    void OpenDialog(HWND owner, bool connected);

    /**
     *  连接状态变化（主面板 → 系统设置窗转交）。对话框未打开时忽略。
     *    connected：true 表示已连接且已登录。
     */
    void SetConnected(bool connected);

    /**
     *  收到 1521 的应答（主面板 → 系统设置窗转交）。对话框未打开、或没有在途请求时忽略。
     *  成功时提示实际生效的区间并关闭对话框；失败时恢复可用并说明原因。
     *    code：     应答的 code，0 表示成功。
     *    beginTime：实际生效的区间开始（Unix 毫秒），失败时为 0。
     *    endTime：  实际生效的区间结束（Unix 毫秒），失败时为 0。
     *  @return 应答被本对话框处理时返回 true。
     */
    bool OnCleanResult(int code, int64_t beginTime, int64_t endTime);

    BEGIN_MSG_MAP(CBalloonChatCleanDlg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 建客户区控件树：时间范围行、开始行、结束行、说明文字、提示文字、按钮行。
    void InitUi();

    // 往一行里追加一组「年 / 月 / 日 / 时 / 分」下拉框。
    //   row：   目标行（所有权在调用方）。
    //   idBase：这一组的第一个控件 id（年），其后依次为月、日、时、分。
    void AppendTimeCombos(balloonwjui::DuiControl* row, UINT idBase);

    // 把某一组下拉框设成 t 所示的时刻（程序填入，不发变化通知；日的选项随年月重建）。
    void FillTimeCombos(UINT idBase, const balloonsettings::LocalMinute& t);

    // 读出某一组下拉框当前所示的时刻（控件树查找接口不是 const，故本方法也不是）。
    balloonsettings::LocalMinute ReadTimeCombos(UINT idBase);

    // 按所示年月重建这一组里「日」的选项，并把超出当月天数的日改成当月最后一天。
    void RebuildDayOptions(UINT idBase);

    // 选中预设档位后，按档位换算出的区间填好两组下拉框（自定义档位不改动）。
    void ApplyPreset(int preset);

    /**
     *  按当前档位或两组下拉框算出要清理的区间。
     *    beginMs / endMs：[出参] 区间；仅返回 kCleanRangeOk 时有效。
     *  @return 校验结果，取值见 ChatCleanRangeError。
     */
    int ComputeRange(int64_t& beginMs, int64_t& endMs);

    // 按当前区间刷新说明文字，按流程状态刷新提示文字与「清理」按钮。
    void RefreshTexts();

    // 用户点了「清理」：校验、确认、发请求、进入清理中。
    void OnCleanClicked();

    // 设置某个标签的文字（找不到控件时忽略）。
    void SetLabelText(UINT ctrlId, LPCWSTR text);

private:
    HWND                          m_ownerHwnd;  // 宿主（系统设置窗）句柄，仅作语义留存
    balloonsettings::ChatCleanFlow m_flow;      // 流程状态；每次新建窗口时 Reset
    bool                          m_timerOn;    // 等应答的超时定时器是否在跑；关窗与收到应答时停掉
};
