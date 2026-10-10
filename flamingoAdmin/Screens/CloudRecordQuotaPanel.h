/**
 *  flamingoAdmin —— 「云端录像空间」面板（docs/屏幕共享与录制方案.md 12.6.5，第 27 期 27g）：按企业设置云端录像的总配额
 *  （cloudrecord.quotagb，GB）与保留天数（cloudrecord.retaindays，0 为永久保留），并显示已用空间。
 *
 *  两个设置项都存在服务端的 t_system_setting 里，与功能开关共用 setting/get.do / setting/set.do；已用空间由 get.do 应答里
 *  的 cloudrecordusedbytes 给出。面板放在「云端录制」开关（CloudRecordSwitchSpec）的下面，2026-10-08 起只在企业详情的
 *  「功能开关」页（FeatureSwitchesPage）的「音视频」卡片里使用（此前在企业设置弹窗与平台设置页两处），三行：名称与已用空间；
 *  「配额 [  ] GB  保留 [  ] 天  ……  [保存]」；一行说明。
 *  点「保存」时校验输入（不带符号的十进制整数，配额 0～1000000、天数 0～3650），只提交有变化的项；两项都变时连发两个
 *  请求，全部应答回来后提示一次。改天数只影响之后结束的录像（到期时间在录像结束时算出），面板的说明里写明这一点。
 *
 *  本面板不直接接触网络与对话框（见 FileShareUi.h），单元测试见 Tests/FileShareScreensTests.cpp 的 CRQ 系列。
 *
 *  典型用法：
 *      std::unique_ptr<CloudRecordQuotaPanel> panel(new CloudRecordQuotaPanel(kApiTagCloudQuotaGet, kApiTagCloudQuotaSet,
 *          kIdCloudQuotaSave, kIdCloudQuotaQuota, kIdCloudQuotaDays));
 *      panel->SetRequestSink(&AdminFrame::SensitiveRequestThunk, frame);
 *      panel->SetUiRequestHandler(&AdminFrame::FileShareUiRequestThunk, frame);
 *      panel->Load(enterpriseId);
 *      //AdminFrame 收到「保存」的 DUIN_CLICK 时
 *      panel->OnSaveClicked();
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include "Controls/Layout/DuiLayout.h"
#include "Screens/FileShareUi.h"

namespace balloonwjui { class DuiLabel; class DuiEdit; class DuiButton; }

namespace flamingoadmin {

// 「云端录像空间」面板，高 kPanelHeight。
class CloudRecordQuotaPanel : public balloonwjui::DuiVBox
{
public:
    // 面板高度（像素）：名称与已用空间一行 24 + 间距 6 + 输入行 34 + 间距 4 + 说明行 20
    static const int kPanelHeight = 88;

    // 配额的取值范围（GB）与保留天数的取值范围（天），与服务端 FeatureSwitchManager 的 kKnownNumbers 一致
    static const long long kQuotaGbMax = 1000000;
    static const long long kRetainDaysMax = 3650;

    // 加载状态
    enum State
    {
        kStateIdle    = 0,   // 尚未加载（还没调用 Load）
        kStateLoading = 1,   // 读设置的请求已发出，等待应答
        kStateLoaded  = 2,   // 已读到设置，可以修改
        kStateFailed  = 3,   // 读设置失败
        kStateSaving  = 4,   // 改设置的请求已发出，等待应答
    };

    // 构造时建好控件树，此时不发请求。
    //   getTag / setTag：读、改设置所用的结果标签（每个面板用不同的标签，宿主据此分发应答）。
    //   saveCtrlId / quotaCtrlId / daysCtrlId：「保存」按钮、配额输入框与天数输入框的控件 ID。
    CloudRecordQuotaPanel(ApiTag getTag, ApiTag setTag, int saveCtrlId, int quotaCtrlId, int daysCtrlId);

    // 设置请求出口与界面请求处理函数，含义见 FileShareUi.h。user 所有权归调用方。
    void SetRequestSink(fsui::RequestSinkFn fn, void* user) { m_port.SetRequestSink(fn, user); }
    void SetUiRequestHandler(fsui::UiRequestFn fn, void* user) { m_port.SetUiRequestHandler(fn, user); }

    // 读某个企业的设置；应答回来之前输入框与「保存」不可用。
    //   enterpriseId：平台管理员要设置的企业（大于 0）；企业管理员传 0，由服务端按本企业处理。
    void Load(long long enterpriseId);

    // 接收转交来的异步接口结果；标签不是本面板的 getTag / setTag 时不做任何事。
    //   tag：结果标签。http：HTTP 往返结果（仅本次调用内有效）。
    void OnApiResult(ApiTag tag, const HttpResult& http);

    // 用户点了「保存」：校验输入，不合法时提示并不提交；没有变化时提示「没有修改」；否则提交有变化的项。
    void OnSaveClicked();

    // ---- 只读访问（宿主与单元测试用）----

    // 读设置与改设置所用的结果标签。
    ApiTag GetTag() const { return m_getTag; }
    ApiTag SetTag() const { return m_setTag; }

    // 当前状态，取值见 State。
    int GetState() const { return m_state; }

    // 当前作用的企业（Load 时传入的值）。
    long long EnterpriseId() const { return m_enterpriseId; }

    // 服务端的取值：配额（GB）、保留天数、已用字节数（应答里没有时为 -1）。
    long long QuotaGb() const { return m_quotaGb; }
    long long RetainDays() const { return m_retainDays; }
    long long UsedBytes() const { return m_usedBytes; }

    // 状态文字（名称右边）。
    CString StatusText() const;

    // 两个输入框与「保存」按钮（单元测试据此模拟输入与点击）。
    balloonwjui::DuiEdit* QuotaEdit() { return m_quotaEdit; }
    balloonwjui::DuiEdit* DaysEdit() { return m_daysEdit; }
    balloonwjui::DuiButton* SaveButton() { return m_saveBtn; }

    // 校验并解析两个输入框的文字（静态函数，便于单元测试）。去掉首尾空白后须为不带符号的十进制整数，配额 0～kQuotaGbMax，
    // 天数 0～kRetainDaysMax。
    //   quotaText / daysText：输入框里的文字。quotaGb / days：[出参] 解析结果，返回 false 时不确定。
    //   error：[出参] 不合法时给用户看的中文说明。
    //   返回：两项都合法返回 true。
    static bool ParseInputs(const CString& quotaText, const CString& daysText, long long& quotaGb, long long& days,
                            CString& error);

    // 已读到设置时的状态文字（静态函数，便于单元测试）：「已用 1.2 GB / 共 50 GB」；已用为 0 时写「已用 0 B」；配额为 0 时
    // 为「已用 0 B，配额为 0，不能再新增」；已用空间未知（usedBytes < 0）时只写「共 50 GB」。
    static CString UsageText(long long usedBytes, long long quotaGb);

private:
    // 读设置的应答。
    void OnLoaded(const HttpResult& http);

    // 改设置的应答（一次保存可能有两个）。
    void OnSaved(const HttpResult& http);

    // 发一个改设置的请求。key：设置项名。value：新取值。
    void SendSet(LPCTSTR key, long long value);

    // 按状态与取值刷新输入框、按钮与状态文字。refillEdits：是否把服务端的取值写回输入框。
    void UpdateView(bool refillEdits);

    fsui::Port               m_port;          // 请求出口与界面请求的转发端口
    ApiTag                   m_getTag;        // 读设置的结果标签
    ApiTag                   m_setTag;        // 改设置的结果标签
    balloonwjui::DuiEdit*    m_quotaEdit;     // 配额输入框（借用，所有权在控件树中）
    balloonwjui::DuiEdit*    m_daysEdit;      // 保留天数输入框（借用，所有权在控件树中）
    balloonwjui::DuiButton*  m_saveBtn;       // 「保存」按钮（借用，所有权在控件树中）
    balloonwjui::DuiLabel*   m_status;        // 状态文字（借用，所有权在控件树中）
    int                      m_state;         // 当前状态，取值见 State
    long long                m_enterpriseId;  // 当前作用的企业；企业管理员为 0
    long long                m_quotaGb;       // 服务端的配额（GB）
    long long                m_retainDays;    // 服务端的保留天数
    long long                m_usedBytes;     // 服务端给出的已用字节数；不知道时为 -1
    long long                m_savingQuota;   // 提交中的配额；这次没有提交配额时为 -1
    long long                m_savingDays;    // 提交中的保留天数；这次没有提交天数时为 -1
    int                      m_pendingSets;   // 还没回来的改设置应答个数
    bool                     m_saveFailed;    // 这次保存是否有一个请求失败
    CString                  m_saveError;     // 这次保存第一个失败的原因（中文）
    CString                  m_error;         // 最近一次读设置失败的原因（中文）
};

} // namespace flamingoadmin
