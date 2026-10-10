/**
 *  凭分享链接加入改用确认小窗（标签 [sharelinkjoin]），ShareLinkJoinDialogTests.cpp
 *
 *  背景：docs/屏幕共享与录制方案.md 第 14.5 节（2026-10-07 用户确认）。凭链接加入时的确认原先用系统消息框显示，改为方案
 *  第 12.4.5 节原定的小窗：复用屏幕共享模块的确认小窗 CShareConfirmDialog（标题栏程序图标，按钮「取消」「加入」），提问区
 *  按文字的行数加高。本文件覆盖：
 *    - SLJD1 估算文字行数：按换行分段、每段按每行字数折行、空段算一行，结果限制在上下限之间；
 *    - SLJD2 确认小窗：可以设整窗宽与提问区行数，提问区按行数加高，按钮都在窗口里；点「加入」向通知窗口投结果 1；
 *            不设时与原来相同（宽 360、两行）；
 *    - SLJD3 源文件守卫：解析链接后不再弹系统消息框，改为打开确认小窗；结果为 1 时才加入。
 *  SLJD3 在运行时读取源文件，位置取自 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>
#include "../../FlamingoMedia/Share/ShareConfirmDialog.h"
#include "../../FlamingoMedia/Share/ShareLinkInvite.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenshare;

namespace {

//测试用的结果消息号（只投给测试自己的通知窗口）
const UINT kSljdResultMsg = WM_APP + 0x7FF0;
//设定的整窗宽与提问区行数；默认值（与 ShareConfirmDialog.cpp 一致）
const int kSljdWidth = 420;
const int kSljdLines = 6;
const int kSljdDefaultWidth = 360;
const int kSljdLineH = 20;
//读文件时每次读的字节数、本文件在 flamingoclient 下的层数
const size_t kSljdReadChunk = 4096;
const int kSljdDepth = 3;

HWND SljdNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

balloonwjui::DuiControl* SljdCtrl(CShareConfirmDialog& dlg, UINT id)
{
    balloonwjui::DuiControl* content = dlg.GetClientContent();
    return content != NULL ? content->FindCtrlById(id) : NULL;
}

void SljdClick(HWND wnd, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(wnd, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

std::string SljdReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kSljdReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

std::string SljdClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kSljdDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

std::string SljdFunctionBody(const std::string& source, const std::string& signature, const std::string& nextPrefix)
{
    const size_t begin = source.find(signature);
    if (begin == std::string::npos)
        return std::string();
    const size_t next = source.find("\n" + nextPrefix, begin + signature.size());
    return source.substr(begin, next == std::string::npos ? std::string::npos : next - begin);
}

} // namespace

TEST_CASE("SLJD1 estimate text lines", "[sharelinkjoin]")
{
    //三段：「a」、空段、「b」
    CHECK(EstimateShareTextLines(L"a\n\nb", 10, 1, 20) == 3);
    //一段 25 字、每行 10 字：3 行
    CHECK(EstimateShareTextLines(std::wstring(25, L'字'), 10, 1, 20) == 3);
    //正好整行
    CHECK(EstimateShareTextLines(std::wstring(20, L'字'), 10, 1, 20) == 2);
    //上下限
    CHECK(EstimateShareTextLines(L"a", 10, 3, 20) == 3);
    CHECK(EstimateShareTextLines(std::wstring(500, L'字'), 10, 1, 8) == 8);
    //每行字数不合法时按 1 处理，不会除以 0
    CHECK(EstimateShareTextLines(L"abc", 0, 1, 20) == 3);
}

TEST_CASE("SLJD2 confirm dialog with a taller prompt", "[sharelinkjoin]")
{
    const HWND notify = SljdNotifyWindow();
    CShareConfirmDialog dlg;
    ShareConfirmSpec spec;
    spec.m_title = L"加入屏幕共享";
    spec.m_prompt = L"加入 张三 的屏幕共享？\n\n「周会」\n说明\n\n这场共享设了密码，加入时需要输入密码。";
    spec.m_okText = L"加入";
    spec.m_resultMsg = kSljdResultMsg;
    spec.m_promptLines = kSljdLines;
    spec.m_width = kSljdWidth;
    dlg.OpenCentered(NULL, notify, spec, false);
    REQUIRE(dlg.IsOpen());
    RECT rc = { 0, 0, 0, 0 };
    ::GetWindowRect(dlg.m_hWnd, &rc);
    CHECK(rc.right - rc.left == kSljdWidth);
    balloonwjui::DuiControl* prompt = SljdCtrl(dlg, CShareConfirmDialog::kIdPrompt);
    REQUIRE(prompt != NULL);
    CHECK(prompt->GetRect().bottom - prompt->GetRect().top == kSljdLines * kSljdLineH);
    RECT client;
    ::GetClientRect(dlg.m_hWnd, &client);
    balloonwjui::DuiControl* ok = SljdCtrl(dlg, CShareConfirmDialog::kIdOk);
    REQUIRE(ok != NULL);
    CHECK(ok->GetRect().bottom <= client.bottom);
    //点「加入」：向通知窗口投 1
    MSG msg;
    while (::PeekMessageW(&msg, notify, 0, 0, PM_REMOVE))
    {
    }
    SljdClick(dlg.m_hWnd, CShareConfirmDialog::kIdOk);
    bool got = false;
    while (::PeekMessageW(&msg, notify, kSljdResultMsg, kSljdResultMsg, PM_REMOVE))
    {
        got = true;
        CHECK(msg.wParam == 1);
    }
    CHECK(got);
    CHECK_FALSE(dlg.IsOpen());

    //不设宽与行数：与原来相同
    ShareConfirmSpec plain;
    plain.m_title = L"移出观看者";
    plain.m_prompt = L"确定将 李四 移出屏幕共享吗？";
    plain.m_okText = L"移出";
    plain.m_resultMsg = kSljdResultMsg;
    dlg.OpenCentered(NULL, notify, plain, false);
    REQUIRE(dlg.IsOpen());
    ::GetWindowRect(dlg.m_hWnd, &rc);
    CHECK(rc.right - rc.left == kSljdDefaultWidth);
    prompt = SljdCtrl(dlg, CShareConfirmDialog::kIdPrompt);
    REQUIRE(prompt != NULL);
    CHECK(prompt->GetRect().bottom - prompt->GetRect().top == 2 * kSljdLineH);
    dlg.Close();
    ::DestroyWindow(notify);
}

TEST_CASE("SLJD3 joining by link no longer uses a system message box", "[sharelinkjoin]")
{
    const std::string root = SljdClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string src = SljdReadFile(root + "\\FlamingoMedia\\Share\\ShareController.cpp");
    REQUIRE_FALSE(src.empty());
    const std::string resolve =
        SljdFunctionBody(src, "void CShareController::onLinkResolveResult(", "LRESULT CShareController::");
    REQUIRE_FALSE(resolve.empty());
    CHECK(resolve.find("MessageBoxW") == std::string::npos);
    CHECK(resolve.find("m_linkJoinDlg.OpenCentered(") != std::string::npos);
    const std::string result =
        SljdFunctionBody(src, "LRESULT CShareController::OnLinkJoinResultMsg(", "std::wstring CShareController::");
    REQUIRE_FALSE(result.empty());
    const size_t cancel = result.find("if (wParam == 0)");
    const size_t join = result.find("joinRoom(");
    REQUIRE(cancel != std::string::npos);
    REQUIRE(join != std::string::npos);
    CHECK(cancel < join);
}
