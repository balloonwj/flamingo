/**
 *  flamingoAdmin —— 文件共享管理三个界面组件（功能开关面板、「文件共享」页、「共享审计」页）与宿主
 *  AdminFrame 之间的公共约定：请求出口、界面请求（提示与确认框）及其转发端口 Port。
 *
 *  三个组件都不直接接触网络与对话框，便于单元测试（Tests/FileShareScreensTests.cpp）：
 *    - 要发的请求交给请求出口。AdminFrame 把它接到 HttpWorker，结果按标签（kApiTagFeature*、kApiTagFs*）
 *      转交组件的 OnApiResult；测试里换成记录下来。
 *    - 要显示的提示与要弹的确认框交给界面请求处理函数。确认框里的选择由 AdminFrame 经
 *      ConfirmTarget::OnConfirmResult 告诉发起确认的组件。
 *
 *  典型用法（组件内）：
 *      m_port.Send(request, kApiTagFsCancel);
 *      m_port.Confirm(_T("强制取消共享"), text, _T("强制取消"), true, this);
 *      m_port.Toast(_T("已强制取消"), false);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include "Util/AdminApi.h"

namespace flamingoadmin { namespace fsui {

// 组件请宿主做的事
enum UiKind
{
    kUiToast   = 1,   // 显示一条提示 UiRequest::m_text；m_isError 为 true 时按错误提示显示
    kUiConfirm = 2,   // 弹确认框；用户的选择经 UiRequest::m_target->OnConfirmResult 告诉发起方
};

// 发起确认框的一方。宿主在用户点了确认、取消或直接关闭确认框后调用 OnConfirmResult，每个确认框恰好一次。
class ConfirmTarget
{
public:
    virtual ~ConfirmTarget() {}

    // 确认框的结果。confirmed：点了确认按钮为 true，取消或关闭为 false。
    virtual void OnConfirmResult(bool confirmed) = 0;
};

// 一次界面请求
struct UiRequest
{
    int            m_kind;     // 请求种类，取值见 UiKind
    CString        m_title;    // kUiConfirm：确认框标题
    CString        m_text;     // kUiToast：提示文字；kUiConfirm：确认框正文
    CString        m_okText;   // kUiConfirm：确认按钮的文字
    bool           m_danger;   // kUiConfirm：确认按钮是否按危险操作的样式显示
    bool           m_isError;  // kUiToast：是否按错误提示显示
    ConfirmTarget* m_target;   // kUiConfirm：发起方，不持有；生命周期长于确认框（组件随主窗口常驻）

    UiRequest()
        : m_kind(0), m_danger(false), m_isError(false), m_target(nullptr)
    {
    }
};

// 请求出口：把一个已构造好的请求发出去。请求按空的 ServerConfig 构造，实现方补上服务器地址、回传窗口与
// 消息后交给 HttpWorker（与 SensitiveWordsScreen::RequestSinkFn 的约定相同）。
//   user：注册时传入的指针。request：请求，m_tag 已填好。
typedef void (*RequestSinkFn)(void* user, HttpRequest& request);

// 界面请求的处理函数。
//   user：注册时传入的指针。request：请求内容，仅本次调用内有效。
typedef void (*UiRequestFn)(void* user, const UiRequest& request);

// 组件与宿主之间的转发端口：保存请求出口与界面请求处理函数，组件经它发请求、提示与确认框。
class Port
{
public:
    Port();

    // 设置请求出口。fn 为空时请求被丢弃（记一条日志）。user 所有权归调用方。
    void SetRequestSink(RequestSinkFn fn, void* user);

    // 设置界面请求的处理函数。fn 为空时界面请求被丢弃。user 所有权归调用方。
    void SetUiRequestHandler(UiRequestFn fn, void* user);

    // 发出一个请求。request：已构造好的请求（服务器地址可为空）。tag：结果标签，写进 request.m_tag。
    void Send(HttpRequest& request, ApiTag tag) const;

    // 请宿主显示一条提示。text：提示文字。isError：是否按错误提示显示。
    void Toast(const CString& text, bool isError) const;

    // 请宿主弹确认框。
    //   title / text / okText：标题、正文与确认按钮文字。danger：确认按钮是否为危险样式。
    //   target：发起方，不持有；结果经 target->OnConfirmResult 回来。
    void Confirm(const CString& title, const CString& text, const CString& okText, bool danger,
                 ConfirmTarget* target) const;

private:
    // 交给界面请求处理函数。
    void Raise(const UiRequest& request) const;

    RequestSinkFn m_sink;           // 请求出口，可为空
    void*         m_sinkUser;       // 请求出口的用户数据，所有权归调用方
    UiRequestFn   m_uiHandler;      // 界面请求处理函数，可为空
    void*         m_uiHandlerUser;  // 界面请求处理函数的用户数据，所有权归调用方
};

}} // namespace flamingoadmin::fsui
